/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Direct tests of the production Bluetooth pairing adapter
 * (src/bt_bap_pairing_adapter.c) against a fake backend.
 *
 * The adapter compiles against an injected immutable backend table; every
 * Bluetooth call (advertising, filter, bond enumeration, peer
 * disconnect/security, storage deletion, policy mutation, pairing-mode
 * notification) happens inside the fake, which records the exact call
 * order and the typed arguments the adapter passes.  Assertions cover the
 * adapter's decision logic only — operation order, exact errno
 * propagation, applied-access-state publication, the notification gate
 * and payload forwarding — never production Bluetooth behavior (that
 * stays in bt_bap.c / BSim).
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "bt_bap_pairing_adapter.h"

/* ── fake backend model ──────────────────────────────────────────── */

enum fake_op {
	OP_ADV_LOCKED_ENTER,
	OP_ADV_STOP,
	OP_FAL_CLEAR,
	OP_BONDS_REPLACE,
	OP_POLICY_SNAPSHOT,
	OP_ADV_UPDATE_PARAM,
	OP_ADV_START,
	OP_POLICY_SET_MODE,
	OP_POLICY_CLEAR,
	OP_PEER_DISCONNECT,
	OP_PEER_SECURITY,
	OP_STORAGE_DELETE,
	OP_NOTIFY_CONNECTED,
	OP_NOTIFY_DISCONNECTED,
	OP_NOTIFY_PAIRING_COMPLETE,
	OP_NOTIFY_PAIRING_FAILED,
	OP_NOTIFY_SECURITY_CHANGED,
	OP_ADV_LOCKED_EXIT,
	OP_COUNT,
};

#define FAKE_MAX_REC 64
#define TEST_CTX     ((void *)0x1234)

struct fake_rec {
	enum fake_op op;
	int arg;   /* bool payload or recorded errno */
	void *ctx; /* context the backend received */
	int seq;   /* 1-based position in the ledger */
};

struct fake_ctx {
	/* configured per-slot results */
	int adv_stop_result;
	int fal_clear_result;
	int bonds_replace_result;
	int adv_update_param_result;
	int adv_start_result;
	int policy_set_mode_result;
	int storage_delete_result;
	int peer_disconnect_ret;
	int peer_security_ret;
	int notify_pairing_complete_ret;
	struct bt_bap_pairing_peer_result peer_disconnect_result;
	struct bt_bap_pairing_peer_result peer_security_result;

	/* snapshot content the adapter reads for its decision */
	enum bt_pairing_policy_mode snap_mode;
	size_t snap_count;

	/* recorded inputs */
	bool adv_update_param_filter;
	enum bt_pairing_policy_mode policy_set_mode_arg;
	bool policy_clear_called;
	int notify_pairing_complete_bonded[FAKE_MAX_REC];
	size_t notify_pairing_complete_count;
	int notify_security_success[FAKE_MAX_REC];
	int notify_security_bonded[FAKE_MAX_REC];
	size_t notify_security_count;
	int adv_locked_depth;

	/* ledger */
	struct fake_rec rec[FAKE_MAX_REC];
	int rec_count;
	int seq;
};

static struct fake_ctx fctx;

static void fake_record(enum fake_op op, int arg, void *ctx)
{
	struct fake_rec *r;

	zassert_true(fctx.rec_count < FAKE_MAX_REC, "fake ledger overflow");
	r = &fctx.rec[fctx.rec_count++];
	r->op = op;
	r->arg = arg;
	r->ctx = ctx;
	r->seq = ++fctx.seq;
}

static int fake_adv_locked(int (*fn)(void *ctx), void *ctx)
{
	int ret;

	fake_record(OP_ADV_LOCKED_ENTER, 0, ctx);
	fctx.adv_locked_depth++;
	zassert_true(fctx.adv_locked_depth == 1, "adv_locked re-entered");
	ret = fn(ctx);
	fctx.adv_locked_depth--;
	fake_record(OP_ADV_LOCKED_EXIT, 0, ctx);
	return ret;
}

static int fake_adv_stop(void *ctx)
{
	fake_record(OP_ADV_STOP, 0, ctx);
	return fctx.adv_stop_result;
}

static int fake_fal_clear(void *ctx)
{
	fake_record(OP_FAL_CLEAR, 0, ctx);
	return fctx.fal_clear_result;
}

static int fake_bonds_replace(void *ctx)
{
	fake_record(OP_BONDS_REPLACE, 0, ctx);
	return fctx.bonds_replace_result;
}

static void fake_policy_snapshot(struct bt_bap_pairing_policy_snap *snap, void *ctx)
{
	fake_record(OP_POLICY_SNAPSHOT, 0, ctx);
	snap->mode = fctx.snap_mode;
	snap->count = fctx.snap_count;
}

static int fake_adv_update_param(const struct bt_bap_pairing_adv_req *req, void *ctx)
{
	fake_record(OP_ADV_UPDATE_PARAM, req->filter_connections ? 1 : 0, ctx);
	fctx.adv_update_param_filter = req->filter_connections;
	return fctx.adv_update_param_result;
}

static int fake_adv_start(void *ctx)
{
	fake_record(OP_ADV_START, 0, ctx);
	return fctx.adv_start_result;
}

static int fake_policy_set_mode(enum bt_pairing_policy_mode mode, void *ctx)
{
	fake_record(OP_POLICY_SET_MODE, (int)mode, ctx);
	fctx.policy_set_mode_arg = mode;
	return fctx.policy_set_mode_result;
}

static void fake_policy_clear(void *ctx)
{
	fake_record(OP_POLICY_CLEAR, 0, ctx);
	fctx.policy_clear_called = true;
}

static int fake_peer_disconnect(struct bt_bap_pairing_peer_result *result, void *ctx)
{
	fake_record(OP_PEER_DISCONNECT, 0, ctx);
	*result = fctx.peer_disconnect_result;
	return fctx.peer_disconnect_ret;
}

static int fake_peer_security(struct bt_bap_pairing_peer_result *result, void *ctx)
{
	fake_record(OP_PEER_SECURITY, 0, ctx);
	*result = fctx.peer_security_result;
	return fctx.peer_security_ret;
}

static int fake_storage_delete(void *ctx)
{
	fake_record(OP_STORAGE_DELETE, 0, ctx);
	return fctx.storage_delete_result;
}

static int fake_notify_connected(void *ctx)
{
	fake_record(OP_NOTIFY_CONNECTED, 0, ctx);
	return 0;
}

static int fake_notify_disconnected(void *ctx)
{
	fake_record(OP_NOTIFY_DISCONNECTED, 0, ctx);
	return 0;
}

static int fake_notify_pairing_complete(bool bonded, void *ctx)
{
	int ret;

	fake_record(OP_NOTIFY_PAIRING_COMPLETE, bonded ? 1 : 0, ctx);
	zassert_true(fctx.notify_pairing_complete_count < FAKE_MAX_REC, "ledger overflow");
	fctx.notify_pairing_complete_bonded[fctx.notify_pairing_complete_count++] = bonded ? 1 : 0;
	ret = fctx.notify_pairing_complete_ret;
	return ret;
}

static int fake_notify_pairing_failed(void *ctx)
{
	fake_record(OP_NOTIFY_PAIRING_FAILED, 0, ctx);
	return 0;
}

static int fake_notify_security_changed(bool success, bool bonded, void *ctx)
{
	fake_record(OP_NOTIFY_SECURITY_CHANGED, success ? 1 : 0, ctx);
	zassert_true(fctx.notify_security_count < FAKE_MAX_REC, "ledger overflow");
	fctx.notify_security_success[fctx.notify_security_count] = success ? 1 : 0;
	fctx.notify_security_bonded[fctx.notify_security_count] = bonded ? 1 : 0;
	fctx.notify_security_count++;
	return 0;
}

static const struct bt_bap_pairing_backend fake_backend = {
	.adv_locked = fake_adv_locked,
	.adv_stop = fake_adv_stop,
	.fal_clear = fake_fal_clear,
	.bonds_replace = fake_bonds_replace,
	.policy_snapshot = fake_policy_snapshot,
	.adv_update_param = fake_adv_update_param,
	.adv_start = fake_adv_start,
	.policy_set_mode = fake_policy_set_mode,
	.policy_clear = fake_policy_clear,
	.peer_disconnect = fake_peer_disconnect,
	.peer_security = fake_peer_security,
	.storage_delete = fake_storage_delete,
	.notify_connected = fake_notify_connected,
	.notify_disconnected = fake_notify_disconnected,
	.notify_pairing_complete = fake_notify_pairing_complete,
	.notify_pairing_failed = fake_notify_pairing_failed,
	.notify_security_changed = fake_notify_security_changed,
};

/* ── helpers ─────────────────────────────────────────────────────── */

static void fctx_reset(void)
{
	memset(&fctx, 0, sizeof(fctx));
	fctx.snap_mode = BT_PAIRING_POLICY_MODE_OPEN;
}

static void suite_before(void *fixture)
{
	ARG_UNUSED(fixture);
	bt_bap_pairing_adapter_test_reset();
	fctx_reset();
	zassert_equal(bt_bap_pairing_adapter_init(&fake_backend, TEST_CTX), 0);
}

static void expect_order(const enum fake_op *ops, size_t count)
{
	zassert_equal(fctx.rec_count, (int)count, "unexpected extra backend calls");
	for (size_t i = 0; i < count; i++) {
		zassert_equal(fctx.rec[i].op, ops[i], "op order mismatch at %zu", i);
	}
}

static void expect_ctx_passthrough(void)
{
	/* Every recorded backend call must carry the init-installed context
	 * (the operation ctx parameter is ignored by the adapter). */
	for (int i = 0; i < fctx.rec_count; i++) {
		zassert_equal(fctx.rec[i].ctx, TEST_CTX, "ctx not passed through");
	}
}

/* Drop the recorded ledger (keeps configured results/state) so a test can
 * assert the exact call order of one operation in isolation. */
static void ledger_clear(void)
{
	fctx.rec_count = 0;
	fctx.seq = 0;
}

/* A start/suspend rejection happens INSIDE the advertising lock runner:
 * exactly the lock-enter + lock-exit records, no step. */
static void expect_lock_runner_only(void)
{
	const enum fake_op runner_only[] = {OP_ADV_LOCKED_ENTER, OP_ADV_LOCKED_EXIT};

	expect_order(runner_only, ARRAY_SIZE(runner_only));
}

/* ── init contracts ──────────────────────────────────────────────── */

ZTEST(bt_bap_pairing_adapter, test_init_null_backend_einval)
{
	bt_bap_pairing_adapter_test_reset();
	zassert_equal(bt_bap_pairing_adapter_init(NULL, NULL), -EINVAL);
}

ZTEST(bt_bap_pairing_adapter, test_init_missing_slot_einval_atomic)
{
	struct bt_bap_pairing_backend broken = fake_backend;

	broken.adv_stop = NULL;
	bt_bap_pairing_adapter_test_reset();
	zassert_equal(bt_bap_pairing_adapter_init(&broken, NULL), -EINVAL);
	/* Atomic: a subsequent full init succeeds and installs normally. */
	zassert_equal(bt_bap_pairing_adapter_init(&fake_backend, TEST_CTX), 0);
}

ZTEST(bt_bap_pairing_adapter, test_init_ealready)
{
	zassert_equal(bt_bap_pairing_adapter_init(&fake_backend, (void *)0x9999), -EALREADY);
	/* The first backend stays installed: the operation context is
	 * unchanged. */
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), 0);
	zassert_equal(fctx.policy_set_mode_arg, BT_PAIRING_POLICY_MODE_BONDED_ONLY);
}

ZTEST(bt_bap_pairing_adapter, test_ops_before_init_einval)
{
	bool pending;

	bt_bap_pairing_adapter_test_reset();
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), -EINVAL);
	zassert_equal(bt_bap_pairing_adapter_advertising_suspend(NULL), -EINVAL);
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), -EINVAL);
	zassert_equal(bt_bap_pairing_adapter_disconnect_peer(&pending, NULL), -EINVAL);
	zassert_equal(bt_bap_pairing_adapter_delete_all_bonds(NULL), -EINVAL);
	zassert_equal(bt_bap_pairing_adapter_request_security(NULL), -EINVAL);
	zassert_equal(fctx.rec_count, 0, "no backend call before init");
}

ZTEST(bt_bap_pairing_adapter, test_init_starts_suspended)
{
	/* Fresh init: applied SUSPENDED — start rejects with -EACCES inside
	 * the advertising lock and performs no step. */
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), -EACCES);
	expect_lock_runner_only();
	zassert_false(bt_bap_pairing_adapter_notifications_enabled());
}

/* ── set_access_mode ─────────────────────────────────────────────── */

ZTEST(bt_bap_pairing_adapter, test_set_access_normal_selects_bonded_only)
{
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), 0);
	zassert_equal(fctx.policy_set_mode_arg, BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(fctx.rec_count, 1);
	zassert_equal(fctx.rec[0].op, OP_POLICY_SET_MODE);
	expect_ctx_passthrough();

	/* Published NORMAL: advertising start is now permitted. */
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), 0);
}

ZTEST(bt_bap_pairing_adapter, test_set_access_bonding_selects_open_preserves_inventory)
{
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_BONDING, NULL), 0);
	zassert_equal(fctx.policy_set_mode_arg, BT_PAIRING_POLICY_MODE_OPEN);
	/* No inventory clear anywhere in a BONDING entry. */
	zassert_false(fctx.policy_clear_called);
	zassert_equal(fctx.rec_count, 1);
}

ZTEST(bt_bap_pairing_adapter, test_set_access_suspended_no_policy_change)
{
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_SUSPENDED, NULL), 0);
	/* Pure publication: no backend call at all. */
	zassert_equal(fctx.rec_count, 0);
}

ZTEST(bt_bap_pairing_adapter, test_set_access_invalid_atomic)
{
	zassert_equal(bt_bap_pairing_adapter_set_access_mode((enum pairing_access_mode)99, NULL),
		      -EINVAL);
	zassert_equal(fctx.rec_count, 0, "invalid mode must not call the backend");
	/* Applied state unchanged: still SUSPENDED after a fresh init. */
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), -EACCES);
	expect_lock_runner_only();
}

ZTEST(bt_bap_pairing_adapter, test_set_access_policy_failure_not_published)
{
	fctx.policy_set_mode_result = -EIO;
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), -EIO);
	zassert_equal(fctx.rec_count, 1);
	/* Not published: applied stays SUSPENDED, start still rejected. */
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), -EACCES);
	zassert_equal(fctx.rec_count, 3, "rejected start adds only the lock runner");
}

/* ── advertising suspend ─────────────────────────────────────────── */

ZTEST(bt_bap_pairing_adapter, test_suspend_stops_and_publishes_suspended)
{
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), 0);
	ledger_clear();

	zassert_equal(bt_bap_pairing_adapter_advertising_suspend(NULL), 0);
	const enum fake_op suspend_order[] = {OP_ADV_LOCKED_ENTER, OP_ADV_STOP, OP_ADV_LOCKED_EXIT};

	expect_order(suspend_order, ARRAY_SIZE(suspend_order));
	expect_ctx_passthrough();

	/* Published: start is now rejected until NORMAL/BONDING is set. */
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), -EACCES);
}

ZTEST(bt_bap_pairing_adapter, test_suspend_failure_not_published)
{
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), 0);
	ledger_clear();
	fctx.adv_stop_result = -EBUSY;

	zassert_equal(bt_bap_pairing_adapter_advertising_suspend(NULL), -EBUSY);
	zassert_equal(fctx.rec_count, 3, "stop failure inside the lock, nothing after");

	/* Applied state unchanged (NORMAL): start is permitted again once the
	 * injected stop failure is cleared (it must not return -EACCES). */
	fctx.adv_stop_result = 0;
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), 0);
}

/* ── advertising start ───────────────────────────────────────────── */

ZTEST(bt_bap_pairing_adapter, test_start_rejects_suspended_eacces)
{
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), -EACCES);
	expect_lock_runner_only();
}

ZTEST(bt_bap_pairing_adapter, test_start_normal_empty_inventory_still_filters)
{
	/* NORMAL -> policy BONDED_ONLY; empty inventory must still produce
	 * connection-filtered advertising. */
	fctx.snap_mode = BT_PAIRING_POLICY_MODE_BONDED_ONLY;
	fctx.snap_count = 0;
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), 0);
	ledger_clear();

	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), 0);
	const enum fake_op start_order[] = {
		OP_ADV_LOCKED_ENTER, OP_ADV_STOP,         OP_FAL_CLEAR, OP_BONDS_REPLACE,
		OP_POLICY_SNAPSHOT,  OP_ADV_UPDATE_PARAM, OP_ADV_START, OP_ADV_LOCKED_EXIT,
	};

	expect_order(start_order, ARRAY_SIZE(start_order));
	expect_ctx_passthrough();
	zassert_true(fctx.adv_update_param_filter, "empty BONDED_ONLY must filter");
}

ZTEST(bt_bap_pairing_adapter, test_start_bonding_unfiltered_preserves_inventory)
{
	/* BONDING -> policy OPEN with a preserved nonempty inventory: no
	 * connection filtering, entries never cleared. */
	fctx.snap_mode = BT_PAIRING_POLICY_MODE_OPEN;
	fctx.snap_count = 3;
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_BONDING, NULL), 0);

	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), 0);
	zassert_false(fctx.adv_update_param_filter, "OPEN must not filter connections");
	zassert_false(fctx.policy_clear_called, "BONDING never clears inventory");
	/* Exactly one bonds_replace (enumerate + replace) during the start. */
	int replace_count = 0;

	for (int i = 0; i < fctx.rec_count; i++) {
		if (fctx.rec[i].op == OP_BONDS_REPLACE) {
			replace_count++;
		}
	}
	zassert_equal(replace_count, 1);
}

ZTEST(bt_bap_pairing_adapter, test_start_normal_nonempty_filters)
{
	fctx.snap_mode = BT_PAIRING_POLICY_MODE_BONDED_ONLY;
	fctx.snap_count = 2;
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), 0);

	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), 0);
	zassert_true(fctx.adv_update_param_filter);
}

/* Every step failure returns its exact errno and stops the sequence at
 * that step (no later backend call). */
ZTEST(bt_bap_pairing_adapter, test_start_step_failures_exact_errno)
{
	fctx.snap_mode = BT_PAIRING_POLICY_MODE_BONDED_ONLY;
	fctx.snap_count = 0;
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), 0);
	ledger_clear();

	/* stop failure */
	fctx.adv_stop_result = -EIO;
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), -EIO);
	zassert_equal(fctx.rec_count, 3, "stop failed: lock-enter + stop + lock-exit");
	zassert_equal(fctx.rec[1].op, OP_ADV_STOP);

	fctx_reset();
	fctx.snap_mode = BT_PAIRING_POLICY_MODE_BONDED_ONLY;
	fctx.snap_count = 0;
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), 0);
	ledger_clear();

	/* FAL clear failure */
	fctx.fal_clear_result = -ENOMEM;
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), -ENOMEM);
	zassert_equal(fctx.rec_count, 4, "fal_clear failed: lock + stop + fal_clear + exit");
	zassert_equal(fctx.rec[2].op, OP_FAL_CLEAR);

	fctx_reset();
	fctx.snap_mode = BT_PAIRING_POLICY_MODE_BONDED_ONLY;
	fctx.snap_count = 0;
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), 0);
	ledger_clear();

	/* bonds_replace failure */
	fctx.bonds_replace_result = -EAGAIN;
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), -EAGAIN);
	zassert_equal(fctx.rec_count, 5);
	zassert_equal(fctx.rec[3].op, OP_BONDS_REPLACE);

	fctx_reset();
	fctx.snap_mode = BT_PAIRING_POLICY_MODE_BONDED_ONLY;
	fctx.snap_count = 0;
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), 0);
	ledger_clear();

	/* update_param failure */
	fctx.adv_update_param_result = -EINVAL;
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), -EINVAL);
	zassert_equal(fctx.rec_count, 7);
	zassert_equal(fctx.rec[5].op, OP_ADV_UPDATE_PARAM);

	fctx_reset();
	fctx.snap_mode = BT_PAIRING_POLICY_MODE_BONDED_ONLY;
	fctx.snap_count = 0;
	zassert_equal(bt_bap_pairing_adapter_set_access_mode(PAIRING_ACCESS_NORMAL, NULL), 0);
	ledger_clear();

	/* start failure */
	fctx.adv_start_result = -EBUSY;
	zassert_equal(bt_bap_pairing_adapter_advertising_start(NULL), -EBUSY);
	zassert_equal(fctx.rec_count, 8);
	zassert_equal(fctx.rec[6].op, OP_ADV_START);
	zassert_equal(fctx.rec[7].op, OP_ADV_LOCKED_EXIT);
}

/* ── disconnect_peer ─────────────────────────────────────────────── */

ZTEST(bt_bap_pairing_adapter, test_disconnect_null_pending_einval)
{
	zassert_equal(bt_bap_pairing_adapter_disconnect_peer(NULL, NULL), -EINVAL);
	zassert_equal(fctx.rec_count, 0);
}

ZTEST(bt_bap_pairing_adapter, test_disconnect_no_peer_success_no_pending)
{
	bool pending = true;

	fctx.peer_disconnect_result.state = BT_BAP_PAIRING_PEER_ABSENT;
	zassert_equal(bt_bap_pairing_adapter_disconnect_peer(&pending, NULL), 0);
	zassert_false(pending);
	zassert_equal(fctx.rec_count, 1);
	zassert_equal(fctx.rec[0].op, OP_PEER_DISCONNECT);
	expect_ctx_passthrough();
}

ZTEST(bt_bap_pairing_adapter, test_disconnect_connected_pending_on_success)
{
	bool pending = false;

	fctx.peer_disconnect_result.state = BT_BAP_PAIRING_PEER_CONNECTED;
	zassert_equal(bt_bap_pairing_adapter_disconnect_peer(&pending, NULL), 0);
	zassert_true(pending);
}

ZTEST(bt_bap_pairing_adapter, test_disconnect_connected_error_no_pending_exact_errno)
{
	bool pending = true;

	fctx.peer_disconnect_result.state = BT_BAP_PAIRING_PEER_CONNECTED;
	fctx.peer_disconnect_ret = -EBUSY;
	zassert_equal(bt_bap_pairing_adapter_disconnect_peer(&pending, NULL), -EBUSY);
	zassert_false(pending);
}

ZTEST(bt_bap_pairing_adapter, test_disconnect_disconnecting_pending_no_duplicate)
{
	bool pending = false;

	fctx.peer_disconnect_result.state = BT_BAP_PAIRING_PEER_DISCONNECTING;
	zassert_equal(bt_bap_pairing_adapter_disconnect_peer(&pending, NULL), 0);
	zassert_true(pending);
	zassert_equal(fctx.rec_count, 1, "no duplicate disconnect command");
}

ZTEST(bt_bap_pairing_adapter, test_disconnect_other_no_pending)
{
	bool pending = true;

	fctx.peer_disconnect_result.state = BT_BAP_PAIRING_PEER_OTHER;
	fctx.peer_disconnect_result.err = -EIO;
	fctx.peer_disconnect_ret = -EIO;
	zassert_equal(bt_bap_pairing_adapter_disconnect_peer(&pending, NULL), -EIO);
	zassert_false(pending);
}

/* The adapter holds no peer object: exactly one backend lookup per
 * operation and no peer pointer crosses the API. */
ZTEST(bt_bap_pairing_adapter, test_disconnect_single_backend_call)
{
	bool pending;

	fctx.peer_disconnect_result.state = BT_BAP_PAIRING_PEER_ABSENT;
	zassert_equal(bt_bap_pairing_adapter_disconnect_peer(&pending, NULL), 0);
	zassert_equal(fctx.rec_count, 1, "one owned backend lookup, released by the backend");
}

/* ── delete_all_bonds ────────────────────────────────────────────── */

ZTEST(bt_bap_pairing_adapter, test_delete_storage_success_clears_inventory)
{
	zassert_equal(bt_bap_pairing_adapter_delete_all_bonds(NULL), 0);
	const enum fake_op delete_order[] = {OP_STORAGE_DELETE, OP_POLICY_CLEAR};

	expect_order(delete_order, ARRAY_SIZE(delete_order));
	zassert_true(fctx.policy_clear_called);
	expect_ctx_passthrough();
}

ZTEST(bt_bap_pairing_adapter, test_delete_storage_failure_preserves_inventory)
{
	fctx.storage_delete_result = -EIO;
	zassert_equal(bt_bap_pairing_adapter_delete_all_bonds(NULL), -EIO);
	zassert_equal(fctx.rec_count, 1, "no inventory clear after storage failure");
	zassert_false(fctx.policy_clear_called);
}

/* ── request_security ────────────────────────────────────────────── */

ZTEST(bt_bap_pairing_adapter, test_security_no_peer_enotconn)
{
	fctx.peer_security_result.state = BT_BAP_PAIRING_PEER_ABSENT;
	zassert_equal(bt_bap_pairing_adapter_request_security(NULL), -ENOTCONN);
}

ZTEST(bt_bap_pairing_adapter, test_security_disconnecting_enotconn)
{
	fctx.peer_security_result.state = BT_BAP_PAIRING_PEER_DISCONNECTING;
	zassert_equal(bt_bap_pairing_adapter_request_security(NULL), -ENOTCONN);
}

ZTEST(bt_bap_pairing_adapter, test_security_success)
{
	fctx.peer_security_result.state = BT_BAP_PAIRING_PEER_CONNECTED;
	zassert_equal(bt_bap_pairing_adapter_request_security(NULL), 0);
	zassert_equal(fctx.rec_count, 1);
	zassert_equal(fctx.rec[0].op, OP_PEER_SECURITY);
	expect_ctx_passthrough();
}

ZTEST(bt_bap_pairing_adapter, test_security_error_exact_errno)
{
	fctx.peer_security_result.state = BT_BAP_PAIRING_PEER_CONNECTED;
	fctx.peer_security_ret = -EBUSY;
	zassert_equal(bt_bap_pairing_adapter_request_security(NULL), -EBUSY);
}

/* ── notification gate and translation ───────────────────────────── */

ZTEST(bt_bap_pairing_adapter, test_notify_gate_closed_no_backend_call)
{
	zassert_false(bt_bap_pairing_adapter_notifications_enabled());
	bt_bap_pairing_adapter_notify_connected();
	bt_bap_pairing_adapter_notify_disconnected();
	bt_bap_pairing_adapter_notify_pairing_complete(true);
	bt_bap_pairing_adapter_notify_pairing_failed();
	bt_bap_pairing_adapter_notify_security_changed(true, true);
	zassert_equal(fctx.rec_count, 0, "gate closed: legacy behavior, no P1 call");
}

ZTEST(bt_bap_pairing_adapter, test_notify_enable_idempotent_and_gate_opens)
{
	bt_bap_pairing_adapter_notifications_enable();
	zassert_true(bt_bap_pairing_adapter_notifications_enabled());
	bt_bap_pairing_adapter_notifications_enable();
	zassert_true(bt_bap_pairing_adapter_notifications_enabled());

	bt_bap_pairing_adapter_notify_connected();
	bt_bap_pairing_adapter_notify_disconnected();
	const enum fake_op notify_order[] = {OP_NOTIFY_CONNECTED, OP_NOTIFY_DISCONNECTED};

	expect_order(notify_order, ARRAY_SIZE(notify_order));
	expect_ctx_passthrough();
}

ZTEST(bt_bap_pairing_adapter, test_notify_pairing_complete_exact_payloads_and_duplicates)
{
	bt_bap_pairing_adapter_notifications_enable();

	bt_bap_pairing_adapter_notify_pairing_complete(true);
	bt_bap_pairing_adapter_notify_pairing_complete(false);
	bt_bap_pairing_adapter_notify_pairing_complete(true);

	zassert_equal(fctx.notify_pairing_complete_count, 3);
	zassert_equal(fctx.notify_pairing_complete_bonded[0], 1);
	zassert_equal(fctx.notify_pairing_complete_bonded[1], 0);
	zassert_equal(fctx.notify_pairing_complete_bonded[2], 1,
		      "duplicate completion is forwarded honestly");
}

ZTEST(bt_bap_pairing_adapter, test_notify_pairing_failed_and_security_payloads)
{
	bt_bap_pairing_adapter_notifications_enable();

	bt_bap_pairing_adapter_notify_pairing_failed();
	bt_bap_pairing_adapter_notify_security_changed(true, true);
	bt_bap_pairing_adapter_notify_security_changed(false, false);
	bt_bap_pairing_adapter_notify_security_changed(true, false);

	const enum fake_op order[] = {OP_NOTIFY_PAIRING_FAILED, OP_NOTIFY_SECURITY_CHANGED,
				      OP_NOTIFY_SECURITY_CHANGED, OP_NOTIFY_SECURITY_CHANGED};

	expect_order(order, ARRAY_SIZE(order));
	zassert_equal(fctx.notify_security_count, 3);
	zassert_equal(fctx.notify_security_success[0], 1);
	zassert_equal(fctx.notify_security_bonded[0], 1);
	zassert_equal(fctx.notify_security_success[1], 0);
	zassert_equal(fctx.notify_security_bonded[1], 0);
	zassert_equal(fctx.notify_security_success[2], 1);
	zassert_equal(fctx.notify_security_bonded[2], 0);
}

ZTEST(bt_bap_pairing_adapter, test_notify_unexpected_enqueue_error_no_retry_no_hci)
{
	bt_bap_pairing_adapter_notifications_enable();
	fctx.notify_pairing_complete_ret = -EIO;

	/* The adapter logs the unexpected errno; it must not retry, loop,
	 * or perform any other backend call. */
	bt_bap_pairing_adapter_notify_pairing_complete(true);
	zassert_equal(fctx.notify_pairing_complete_count, 1, "exactly one enqueue attempt");
	zassert_equal(fctx.rec_count, 1, "no secondary action after the failed enqueue");
}

ZTEST(bt_bap_pairing_adapter, test_notify_ecanceled_tolerated)
{
	bt_bap_pairing_adapter_notifications_enable();
	fctx.notify_pairing_complete_ret = -ECANCELED;

	/* After a controller fatal, enqueues return -ECANCELED: tolerated,
	 * no recovery action, no further backend calls. */
	bt_bap_pairing_adapter_notify_pairing_complete(true);
	zassert_equal(fctx.notify_pairing_complete_count, 1);
	zassert_equal(fctx.rec_count, 1);
}

ZTEST(bt_bap_pairing_adapter, test_notify_no_inline_transition_or_hci)
{
	bt_bap_pairing_adapter_notifications_enable();
	bt_bap_pairing_adapter_notify_connected();
	bt_bap_pairing_adapter_notify_disconnected();
	bt_bap_pairing_adapter_notify_security_changed(true, true);

	/* Only the notification slots run: no advertising, policy, peer,
	 * or storage work from the callback translation path. */
	for (int i = 0; i < fctx.rec_count; i++) {
		zassert_true(fctx.rec[i].op == OP_NOTIFY_CONNECTED ||
				     fctx.rec[i].op == OP_NOTIFY_DISCONNECTED ||
				     fctx.rec[i].op == OP_NOTIFY_SECURITY_CHANGED,
			     "inline controller action from a callback translation");
	}
}

ZTEST_SUITE(bt_bap_pairing_adapter, NULL, NULL, suite_before, NULL, NULL);
