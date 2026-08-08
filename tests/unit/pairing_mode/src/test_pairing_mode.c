/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Direct tests of the production pairing-mode transition owner
 * (src/pairing_mode.c) against fake injected operations.
 *
 * The fake records every operation call (op id + argument) into a bounded
 * spinlock-protected ledger, posts a per-op event semaphore after each
 * call (so tests can deterministically wait for a specific op), and can
 * block a specific op on a test-owned gate semaphore (so tests can hold
 * the controller mid-transition).  Peer state drives *pending from
 * ops->disconnect_peer; the peer-connected flag is test-controlled.
 *
 * Short Kconfig values preserve the production ratios: bond half-period
 * 100 ms, reset half-period 20 ms, reset feedback 200 ms (= ten
 * half-periods, five complete flashes), bond hold 250 ms, reset hold
 * 500 ms.  The suite runs at CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000 so the
 * native_sim delayed-work deadlines (which land one tick late) stay
 * within 1 ms of nominal and every pattern completes before its expiry.
 *
 * Assertions use public state (pairing_mode_get_status) and the operation
 * ledger — never private fields or helper-call counts.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/ztest.h>

#include "pairing_mode.h"

/* ── fake operation model ────────────────────────────────────────── */

enum fake_op {
	FOP_SET_ACCESS,
	FOP_ADV_SUSPEND,
	FOP_ADV_START,
	FOP_DISCONNECT,
	FOP_DELETE_BONDS,
	FOP_REQUEST_SECURITY,
	FOP_LED,
	FOP_REBOOT,
	FOP_COUNT,
};

#define FAKE_MAX_REC 96

struct fake_rec {
	enum fake_op op;
	int arg;
	uint32_t t_ms; /* controller-side timestamp (k_uptime_get) */
};

struct fake_ctx {
	int ret[FOP_COUNT];  /* configured return per op (0 = ok) */
	bool peer_connected; /* *pending written by disconnect_peer */
	int reboot_count;
	struct fake_rec rec[FAKE_MAX_REC];
	int rec_count;
	struct k_sem ev[FOP_COUNT];    /* posted after each recorded call */
	struct k_sem *gate[FOP_COUNT]; /* optional blocking gate per op */
	struct k_spinlock lock;
};

static struct fake_ctx fctx;

/* Number of LED events consumed via wait_led_polarity/drain (FIFO order:
 * the consumed event is the (led_consumed+1)-th LED record in the
 * ledger, since records are written before their events are given). */
static int led_consumed;

static void fake_record(enum fake_op op, int arg)
{
	k_spinlock_key_t key = k_spin_lock(&fctx.lock);

	if (fctx.rec_count < FAKE_MAX_REC) {
		fctx.rec[fctx.rec_count].op = op;
		fctx.rec[fctx.rec_count].arg = arg;
		fctx.rec[fctx.rec_count].t_ms = (uint32_t)k_uptime_get();
		fctx.rec_count++;
	}
	k_spin_unlock(&fctx.lock, key);

	k_sem_give(&fctx.ev[op]);
	if (fctx.gate[op] != NULL) {
		k_sem_take(fctx.gate[op], K_FOREVER);
	}
}

static int fake_set_access(enum pairing_access_mode mode, void *ctx)
{
	ARG_UNUSED(ctx);
	fake_record(FOP_SET_ACCESS, (int)mode);
	return fctx.ret[FOP_SET_ACCESS];
}

static int fake_advertising_suspend(void *ctx)
{
	ARG_UNUSED(ctx);
	fake_record(FOP_ADV_SUSPEND, 0);
	return fctx.ret[FOP_ADV_SUSPEND];
}

static int fake_advertising_start(void *ctx)
{
	ARG_UNUSED(ctx);
	fake_record(FOP_ADV_START, 0);
	return fctx.ret[FOP_ADV_START];
}

static int fake_disconnect_peer(bool *pending, void *ctx)
{
	ARG_UNUSED(ctx);
	fake_record(FOP_DISCONNECT, fctx.peer_connected ? 1 : 0);
	*pending = fctx.peer_connected;
	return fctx.ret[FOP_DISCONNECT];
}

static int fake_delete_all_bonds(void *ctx)
{
	ARG_UNUSED(ctx);
	fake_record(FOP_DELETE_BONDS, 0);
	return fctx.ret[FOP_DELETE_BONDS];
}

static int fake_request_security(void *ctx)
{
	ARG_UNUSED(ctx);
	fake_record(FOP_REQUEST_SECURITY, 0);
	return fctx.ret[FOP_REQUEST_SECURITY];
}

static int fake_led_set(bool active, void *ctx)
{
	ARG_UNUSED(ctx);
	fake_record(FOP_LED, active ? 1 : 0);
	return fctx.ret[FOP_LED];
}

static void fake_cold_reboot(void *ctx)
{
	ARG_UNUSED(ctx);
	/* Increment BEFORE fake_record() gives the event semaphore so the
	 * higher-priority test thread can never observe the reboot event
	 * before the count is visible (and an aborted test cannot leak a
	 * preempted post-increment into the next test's reset ctx). */
	fctx.reboot_count++;
	fake_record(FOP_REBOOT, 0);
}

static const struct pairing_mode_ops fake_ops = {
	.set_access_mode = fake_set_access,
	.advertising_suspend = fake_advertising_suspend,
	.advertising_start = fake_advertising_start,
	.disconnect_peer = fake_disconnect_peer,
	.delete_all_bonds = fake_delete_all_bonds,
	.request_security = fake_request_security,
	.led_set = fake_led_set,
	.cold_reboot = fake_cold_reboot,
};

/* ── helpers ─────────────────────────────────────────────────────── */

#define WAIT_MS 1000

static void fctx_reset(void)
{
	memset(&fctx, 0, sizeof(fctx));
	for (int i = 0; i < FOP_COUNT; i++) {
		k_sem_init(&fctx.ev[i], 0, 64);
	}
	led_consumed = 0;
}

static void suite_before(void *fixture)
{
	ARG_UNUSED(fixture);
	/* Synchronize the controller FIRST: an aborted test may leave the
	 * controller preempted mid-fake_record, so resetting the fake
	 * context before the seam reset would zero live semaphores. */
	pairing_mode_test_reset();
	fctx_reset();
	zassert_equal(pairing_mode_init(&fake_ops, &fctx), 0);
}

static int rec_count(void)
{
	int n;

	k_spinlock_key_t key = k_spin_lock(&fctx.lock);

	n = fctx.rec_count;
	k_spin_unlock(&fctx.lock, key);
	return n;
}

static enum fake_op rec_op(int i)
{
	enum fake_op op;

	k_spinlock_key_t key = k_spin_lock(&fctx.lock);

	op = (i >= 0 && i < fctx.rec_count) ? fctx.rec[i].op : FOP_COUNT;
	k_spin_unlock(&fctx.lock, key);
	return op;
}

static int rec_arg(int i)
{
	int arg = -1;

	k_spinlock_key_t key = k_spin_lock(&fctx.lock);

	if (i >= 0 && i < fctx.rec_count) {
		arg = fctx.rec[i].arg;
	}
	k_spin_unlock(&fctx.lock, key);
	return arg;
}

static int op_event_count(enum fake_op op)
{
	int n;

	k_spinlock_key_t key = k_spin_lock(&fctx.lock);

	n = 0;
	for (int i = 0; i < fctx.rec_count; i++) {
		if (fctx.rec[i].op == op) {
			n++;
		}
	}
	k_spin_unlock(&fctx.lock, key);
	return n;
}

/* Wait for the next recorded call of op (post-op event semaphore). */
static void wait_op(enum fake_op op)
{
	zassert_equal(k_sem_take(&fctx.ev[op], K_MSEC(WAIT_MS)), 0, "op %d never recorded",
		      (int)op);
}

/* Poll public status until the given phase (bounded). */
static bool wait_phase(enum pairing_mode_phase ph, int timeout_ms)
{
	struct pairing_mode_status st;
	int waited = 0;

	for (;;) {
		pairing_mode_get_status(&st);
		if (st.phase == ph) {
			return true;
		}
		if (waited >= timeout_ms) {
			return false;
		}
		k_sleep(K_MSEC(1));
		waited++;
	}
}

static bool wait_mode(enum pairing_mode m, int timeout_ms)
{
	struct pairing_mode_status st;
	int waited = 0;

	for (;;) {
		pairing_mode_get_status(&st);
		if (st.mode == m) {
			return true;
		}
		if (waited >= timeout_ms) {
			return false;
		}
		k_sleep(K_MSEC(1));
		waited++;
	}
}

/* Wait until at least n records exist (bounded poll). */
static bool wait_rec_count(int n, int timeout_ms)
{
	int waited = 0;

	while (rec_count() < n) {
		if (waited >= timeout_ms) {
			return false;
		}
		k_sleep(K_MSEC(1));
		waited++;
	}
	return true;
}

/* Consume the next LED event and assert its polarity (latest record). */
static void wait_led_polarity(int expected, int timeout_ms)
{
	zassert_equal(k_sem_take(&fctx.ev[FOP_LED], K_MSEC(timeout_ms)), 0,
		      "LED event never arrived");

	/* FIFO: the consumed event is the (led_consumed+1)-th LED record. */
	int seen = 0;
	int target = -1;

	for (int i = 0; i < rec_count(); i++) {
		if (rec_op(i) == FOP_LED) {
			if (seen == led_consumed) {
				target = i;
				break;
			}
			seen++;
		}
	}
	zassert_true(target >= 0, "consumed LED event has no record");
	zassert_equal(rec_arg(target), expected, "LED polarity");
	led_consumed++;
}

/* Assert the record sequence [op0, op1, ..., opN-1] with exact args. */
struct op_expect {
	enum fake_op op;
	int arg;
};

static void assert_op_sequence(const struct op_expect *exp, int n)
{
	zassert_equal(rec_count(), n, "record count %d != %d", rec_count(), n);
	for (int i = 0; i < n; i++) {
		zassert_equal(rec_op(i), exp[i].op, "record %d op", i);
		zassert_equal(rec_arg(i), exp[i].arg, "record %d arg", i);
	}
}

#define OP(op, arg)                                                                                \
	(struct op_expect)                                                                         \
	{                                                                                          \
		op, arg                                                                            \
	}

static void assert_not_fatal(void)
{
	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_false(st.fatal, "controller went fatal");
}

/* Start the controller and wait for NORMAL/IDLE. */
static void start_and_idle(void)
{
	zassert_equal(pairing_mode_start(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "start -> IDLE");
}

/* Enter BONDING without a peer and wait for BONDING/IDLE. */
static void bonding_no_peer(void)
{
	zassert_equal(pairing_mode_request_bonding(), 0);
	zassert_true(wait_mode(PAIRING_MODE_BONDING, WAIT_MS), "bonding entry");
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "bonding IDLE");
}

/* ── sync reset helper thread ────────────────────────────────────── */

static K_THREAD_STACK_DEFINE(sync_helper_stack, 2048);
static struct k_thread sync_helper_thread;
static struct k_sem sync_helper_done;
static int sync_helper_result;

static void sync_helper_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	sync_helper_result = pairing_mode_request_reset_sync(K_MSEC(3000));
	k_sem_give(&sync_helper_done);
}

/* ── suite ───────────────────────────────────────────────────────── */

ZTEST_SUITE(pairing_mode, NULL, NULL, suite_before, NULL, NULL);

/* Init: invalid/missing ops rejected atomically (no calls, no reboot). */
ZTEST(pairing_mode, test_init_invalid_ops_atomic)
{
	zassert_equal(pairing_mode_init(NULL, &fctx), -EINVAL);
	zassert_equal(rec_count(), 0);

	struct pairing_mode_ops bad = fake_ops;

	bad.set_access_mode = NULL;
	zassert_equal(pairing_mode_init(&bad, &fctx), -EINVAL);
	zassert_equal(rec_count(), 0);
	bad = fake_ops;
	bad.advertising_suspend = NULL;
	zassert_equal(pairing_mode_init(&bad, &fctx), -EINVAL);
	bad = fake_ops;
	bad.advertising_start = NULL;
	zassert_equal(pairing_mode_init(&bad, &fctx), -EINVAL);
	bad = fake_ops;
	bad.disconnect_peer = NULL;
	zassert_equal(pairing_mode_init(&bad, &fctx), -EINVAL);
	bad = fake_ops;
	bad.delete_all_bonds = NULL;
	zassert_equal(pairing_mode_init(&bad, &fctx), -EINVAL);
	bad = fake_ops;
	bad.request_security = NULL;
	zassert_equal(pairing_mode_init(&bad, &fctx), -EINVAL);
	bad = fake_ops;
	bad.led_set = NULL;
	zassert_equal(pairing_mode_init(&bad, &fctx), -EINVAL);
	bad = fake_ops;
	bad.cold_reboot = NULL;
	zassert_equal(pairing_mode_init(&bad, &fctx), -EINVAL);
	zassert_equal(rec_count(), 0);
	zassert_equal(fctx.reboot_count, 0);

	/* A subsequent valid init still works (no partial state). */
	pairing_mode_test_reset();
	zassert_equal(pairing_mode_init(&fake_ops, &fctx), 0);
	zassert_equal(pairing_mode_start(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "start -> IDLE");
}

/* Init status: pre-init defaults, post-init, second init -EALREADY. */
ZTEST(pairing_mode, test_init_status)
{
	struct pairing_mode_status st;

	pairing_mode_test_reset();
	pairing_mode_get_status(&st);
	zassert_false(st.initialized);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_UNINITIALIZED);
	zassert_equal(st.mode, PAIRING_MODE_NORMAL);
	zassert_equal(st.access_mode, PAIRING_ACCESS_NORMAL);
	zassert_false(st.fatal);

	zassert_equal(pairing_mode_init(&fake_ops, &fctx), 0);
	pairing_mode_get_status(&st);
	zassert_true(st.initialized);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_UNINITIALIZED);
	zassert_equal(st.mode, PAIRING_MODE_NORMAL);

	zassert_equal(pairing_mode_init(&fake_ops, &fctx), -EALREADY);
}

/* get_status(NULL) is a documented safe no-op. */
ZTEST(pairing_mode, test_get_status_null_safe)
{
	pairing_mode_get_status(NULL);
}

/* API calls before init are rejected with -EINVAL. */
ZTEST(pairing_mode, test_uninitialized_apis_rejected)
{
	/* suite_before already init'ed; reset to uninitialized. */
	pairing_mode_test_reset();

	zassert_equal(pairing_mode_start(), -EINVAL);
	zassert_equal(pairing_mode_request_bonding(), -EINVAL);
	zassert_equal(pairing_mode_request_reset(), -EINVAL);
	zassert_equal(pairing_mode_request_reset_sync(K_MSEC(1)), -EINVAL);
	zassert_equal(pairing_mode_notify_connected(), -EINVAL);
	zassert_equal(pairing_mode_notify_disconnected(), -EINVAL);
	zassert_equal(pairing_mode_notify_pairing_complete(true), -EINVAL);
	zassert_equal(pairing_mode_notify_pairing_failed(), -EINVAL);
	zassert_equal(pairing_mode_notify_security_changed(true, true), -EINVAL);
	zassert_equal(rec_count(), 0);
}

/* Start success: exact op order; second start is an idempotent no-op. */
ZTEST(pairing_mode, test_start_success_idempotent)
{
	start_and_idle();

	static const struct op_expect exp[] = {
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_NORMAL),
		OP(FOP_LED, 0),
		OP(FOP_ADV_START, 0),
	};

	assert_op_sequence(exp, ARRAY_SIZE(exp));

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.transition_generation, 0);
	zassert_equal(st.mode, PAIRING_MODE_NORMAL);
	zassert_equal(st.access_mode, PAIRING_ACCESS_NORMAL);
	zassert_false(st.led_active);

	zassert_equal(pairing_mode_start(), 0);
	k_sleep(K_MSEC(20));
	zassert_equal(rec_count(), (int)ARRAY_SIZE(exp), "second start re-ran");
}

/* Start failures at each stage reboot exactly once. */
ZTEST(pairing_mode, test_start_failure_set_access_reboots)
{
	fctx.ret[FOP_SET_ACCESS] = -EIO;
	zassert_equal(pairing_mode_start(), 0);
	wait_op(FOP_REBOOT);

	static const struct op_expect exp[] = {
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_NORMAL),
		OP(FOP_LED, 0), /* fatal finalizer LED-inactive attempt */
		OP(FOP_REBOOT, 0),
	};

	assert_op_sequence(exp, ARRAY_SIZE(exp));
	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_true(st.fatal);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_FATAL);
}

ZTEST(pairing_mode, test_start_failure_led_reboots)
{
	fctx.ret[FOP_LED] = -EIO;
	zassert_equal(pairing_mode_start(), 0);
	wait_op(FOP_REBOOT);

	static const struct op_expect exp[] = {
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_NORMAL),
		OP(FOP_LED, 0), /* the failing start LED */
		OP(FOP_LED, 0), /* fatal finalizer LED-inactive attempt */
		OP(FOP_REBOOT, 0),
	};

	assert_op_sequence(exp, ARRAY_SIZE(exp));
}

ZTEST(pairing_mode, test_start_failure_advertising_reboots)
{
	fctx.ret[FOP_ADV_START] = -EIO;
	zassert_equal(pairing_mode_start(), 0);
	wait_op(FOP_REBOOT);

	static const struct op_expect exp[] = {
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_NORMAL),
		OP(FOP_LED, 0),
		OP(FOP_ADV_START, 0), /* the failing advertising start */
		OP(FOP_LED, 0),       /* fatal finalizer */
		OP(FOP_REBOOT, 0),
	};

	assert_op_sequence(exp, ARRAY_SIZE(exp));
}

/* Bonding without a peer: exact operation order. */
ZTEST(pairing_mode, test_bonding_no_peer_exact_order)
{
	start_and_idle();
	zassert_equal(pairing_mode_request_bonding(), 0);
	zassert_true(wait_rec_count(9, WAIT_MS), "bonding entry completed");

	static const struct op_expect exp[] = {
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_NORMAL),
		OP(FOP_LED, 0),
		OP(FOP_ADV_START, 0),
		OP(FOP_ADV_SUSPEND, 0),
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_SUSPENDED),
		OP(FOP_DISCONNECT, 0),
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_BONDING),
		OP(FOP_LED, 1),
		OP(FOP_ADV_START, 0),
	};

	assert_op_sequence(exp, ARRAY_SIZE(exp));

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_equal(st.access_mode, PAIRING_ACCESS_BONDING);
	zassert_equal(st.transition_generation, 1);
}

/* Bonding with a peer waits for the disconnect before entering BONDING. */
ZTEST(pairing_mode, test_bonding_connected_waits_for_disconnect)
{
	start_and_idle();
	fctx.peer_connected = true;

	zassert_equal(pairing_mode_request_bonding(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_BONDING, WAIT_MS),
		     "wait disconnect");

	/* No BONDING entry may have happened while the peer is connected:
	 * the only LED record so far is the boot start's LED-inactive. */
	zassert_equal(op_event_count(FOP_LED), 1, "only the start LED");
	zassert_equal(op_event_count(FOP_ADV_START), 1);  /* only the start */
	zassert_equal(op_event_count(FOP_SET_ACCESS), 2); /* NORMAL + SUSPENDED */

	fctx.peer_connected = false;
	zassert_equal(pairing_mode_notify_disconnected(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "bonding entry");

	static const struct op_expect exp[] = {
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_NORMAL),
		OP(FOP_LED, 0),
		OP(FOP_ADV_START, 0),
		OP(FOP_ADV_SUSPEND, 0),
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_SUSPENDED),
		OP(FOP_DISCONNECT, 1),
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_BONDING),
		OP(FOP_LED, 1),
		OP(FOP_ADV_START, 0),
	};

	assert_op_sequence(exp, ARRAY_SIZE(exp));
}

/* Duplicate BONDING while already BONDING and not connected: no-op. */
ZTEST(pairing_mode, test_duplicate_bonding_no_peer_noop)
{
	start_and_idle();
	bonding_no_peer();

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.transition_generation, 1);

	zassert_equal(pairing_mode_request_bonding(), 0);
	k_sleep(K_MSEC(30));
	/* The duplicate must not have executed any transition op (the slow
	 * blink may still toggle the LED, which is expected and harmless). */
	zassert_equal(op_event_count(FOP_ADV_SUSPEND), 1, "no second suspend");
	zassert_equal(op_event_count(FOP_ADV_START), 2, "no second advertising");
	zassert_equal(op_event_count(FOP_DISCONNECT), 1, "no second disconnect");
	zassert_equal(op_event_count(FOP_SET_ACCESS), 3, "no second access change");
	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_IDLE);
	zassert_equal(st.transition_generation, 1);
}

/* Reset without a peer: exact operation order incl. rapid LED feedback. */
ZTEST(pairing_mode, test_reset_no_peer_exact_order)
{
	start_and_idle();
	zassert_equal(pairing_mode_request_reset(), 0);
	zassert_true(wait_rec_count(20, WAIT_MS), "reset -> IDLE");

	/* [0..2] start; [3..6] reset: suspend, SUSPENDED, disconnect,
	 * delete bonds; [7] feedback LED active; [8..16] nine rapid toggles
	 * (F,T,F,T,F,T,F,T,F); [17] BONDING access; [18] BONDING LED active;
	 * [19] advertising start. */
	static const struct op_expect exp[] = {
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_NORMAL),
		OP(FOP_LED, 0),
		OP(FOP_ADV_START, 0),
		OP(FOP_ADV_SUSPEND, 0),
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_SUSPENDED),
		OP(FOP_DISCONNECT, 0),
		OP(FOP_DELETE_BONDS, 0),
		OP(FOP_LED, 1), /* feedback entry */
		OP(FOP_LED, 0),
		OP(FOP_LED, 1),
		OP(FOP_LED, 0),
		OP(FOP_LED, 1),
		OP(FOP_LED, 0),
		OP(FOP_LED, 1),
		OP(FOP_LED, 0),
		OP(FOP_LED, 1),
		OP(FOP_LED, 0),
		OP(FOP_SET_ACCESS, PAIRING_ACCESS_BONDING),
		OP(FOP_LED, 1), /* BONDING entry */
		OP(FOP_ADV_START, 0),
	};

	assert_op_sequence(exp, ARRAY_SIZE(exp));

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_equal(st.access_mode, PAIRING_ACCESS_BONDING);
	zassert_equal(st.transition_generation, 1);
	zassert_false(st.fatal);
}

/* Reset with a peer deletes bonds only after the disconnect. */
ZTEST(pairing_mode, test_reset_connected_deletes_after_disconnect)
{
	start_and_idle();
	fctx.peer_connected = true;

	zassert_equal(pairing_mode_request_reset(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_RESET, WAIT_MS),
		     "wait disconnect");
	zassert_equal(op_event_count(FOP_DELETE_BONDS), 0, "bonds deleted before disconnect");
	zassert_equal(op_event_count(FOP_LED), 1, "only the start LED");

	fctx.peer_connected = false;
	zassert_equal(pairing_mode_notify_disconnected(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "reset -> IDLE");

	zassert_equal(op_event_count(FOP_DELETE_BONDS), 1);
	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
}

/* Reset supersedes a pending BONDING disconnect transition. */
ZTEST(pairing_mode, test_reset_supersedes_pending_bonding_disconnect)
{
	start_and_idle();
	fctx.peer_connected = true;

	zassert_equal(pairing_mode_request_bonding(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_BONDING, WAIT_MS),
		     "bonding wait");
	zassert_equal(op_event_count(FOP_DISCONNECT), 1);

	zassert_equal(pairing_mode_request_reset(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_RESET, WAIT_MS),
		     "reset supersede");
	zassert_equal(op_event_count(FOP_DISCONNECT), 2, "reset re-requests disconnect");
	zassert_equal(op_event_count(FOP_DELETE_BONDS), 0, "bonds deleted before disconnect");

	fctx.peer_connected = false;
	zassert_equal(pairing_mode_notify_disconnected(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "reset -> IDLE");

	zassert_equal(op_event_count(FOP_DELETE_BONDS), 1, "reset deletes once");
	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_equal(st.transition_generation, 2);
}

/* Reset supersedes an active (completed) BONDING. */
ZTEST(pairing_mode, test_reset_supersedes_active_bonding)
{
	start_and_idle();
	bonding_no_peer();
	zassert_equal(op_event_count(FOP_DELETE_BONDS), 0);

	zassert_equal(pairing_mode_request_reset(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_RESET_FEEDBACK, WAIT_MS), "reset feedback");
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "reset -> IDLE");

	zassert_equal(op_event_count(FOP_DELETE_BONDS), 1);
	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_equal(st.transition_generation, 2);
}

/* Reset feedback: exact rapid LED sequence and delayed BONDING start. */
ZTEST(pairing_mode, test_reset_feedback_exact_rapid_sequence)
{
	start_and_idle();
	wait_led_polarity(0, WAIT_MS); /* consume the start LED-inactive */

	zassert_equal(pairing_mode_request_reset(), 0);

	/* Feedback entry LED active: the event is posted after the
	 * RESET_FEEDBACK phase write, so this take cannot race the first
	 * rapid toggle. */
	wait_led_polarity(1, WAIT_MS);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_RESET_FEEDBACK, WAIT_MS), "feedback");

	/* Nine rapid toggles, each within half-period + 100% slack. */
	for (int i = 0; i < 9; i++) {
		wait_led_polarity((i % 2 == 0) ? 0 : 1, 40);
	}

	/* Delayed BONDING start: advertising starts only after feedback. */
	zassert_equal(op_event_count(FOP_ADV_START), 1, "only the start so far");
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "BONDING entry");
	zassert_equal(op_event_count(FOP_ADV_START), 2, "BONDING advertising");

	/* BONDING entry LED active follows the rapid pattern. */
	wait_led_polarity(1, WAIT_MS);

	/* Exact LED ledger: start(off), feedback T, nine rapid toggles
	 * (F,T,F,T,F,T,F,T,F), BONDING T — 12 calls. */
	zassert_equal(op_event_count(FOP_LED), 12, "LED call count != 12");
}

/* Slow blink starts active and follows exact half-periods; completion
 * turns the LED off and stops the pattern. */
ZTEST(pairing_mode, test_slow_blink_starts_active_exact_half_periods)
{
	start_and_idle();
	wait_led_polarity(0, WAIT_MS); /* consume the start LED-inactive */
	bonding_no_peer();

	/* Entry LED active (the BONDING pattern starts active). */
	wait_led_polarity(1, WAIT_MS);

	/* Toggles at 100 ms cadence, each within half-period + 100% slack. */
	for (int i = 0; i < 3; i++) {
		wait_led_polarity((i % 2 == 0) ? 0 : 1, 200);
	}

	/* Pairing completion: LED inactive, pattern stops. */
	zassert_equal(pairing_mode_notify_connected(), 0);
	wait_op(FOP_REQUEST_SECURITY);
	zassert_equal(pairing_mode_notify_pairing_complete(true), 0);

	struct pairing_mode_status st;

	zassert_true(wait_mode(PAIRING_MODE_NORMAL, WAIT_MS), "complete -> NORMAL");
	pairing_mode_get_status(&st);
	zassert_false(st.led_active);

	/* Consume the completion's LED-inactive event, then prove the blink
	 * is cancelled: no LED event within a full half-period. */
	wait_led_polarity(0, WAIT_MS);
	zassert_equal(k_sem_take(&fctx.ev[FOP_LED], K_MSEC(45)), -EAGAIN,
		      "blink continued after completion");
}

/* Stale rapid/slow work cannot modify a newer generation. */
ZTEST(pairing_mode, test_stale_work_cannot_modify_newer_generation)
{
	start_and_idle();
	bonding_no_peer(); /* gen 1: slow blink scheduled at +40 ms */

	zassert_equal(pairing_mode_request_reset(), 0); /* gen 2 */
	zassert_true(wait_phase(PAIRING_MODE_PHASE_RESET_FEEDBACK, WAIT_MS), "reset feedback");
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "reset -> IDLE");
	/* The gen-1 slow toggle fires mid-feedback: it must be a no-op, so
	 * the LED ledger is exactly: start(off), BONDING entry(on),
	 * feedback entry(on), nine rapid toggles, BONDING entry(on) — 13
	 * calls. */
	zassert_equal(op_event_count(FOP_LED), 13, "stale blink modified the LED ledger");

	/* The LED events accumulated during the transitions; drain them,
	 * then prove nothing continues: no LED event in a window strictly
	 * shorter than the next slow half-period (the gen-2 slow toggle
	 * fires at +100 ms). */
	while (k_sem_take(&fctx.ev[FOP_LED], K_NO_WAIT) == 0) {
		led_consumed++;
	}
	zassert_equal(k_sem_take(&fctx.ev[FOP_LED], K_MSEC(30)), -EAGAIN,
		      "rapid/stale work continued");
}

/* Connected in BONDING/IDLE requests security from the work context. */
ZTEST(pairing_mode, test_connected_bonding_requests_security)
{
	start_and_idle();
	bonding_no_peer();
	zassert_equal(op_event_count(FOP_REQUEST_SECURITY), 0);

	zassert_equal(pairing_mode_notify_connected(), 0);
	wait_op(FOP_REQUEST_SECURITY);

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_true(st.connected);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_IDLE);
	assert_not_fatal();
}

/* Connected during a suspended/wait phase is disconnected again. */
ZTEST(pairing_mode, test_connected_during_suspended_disconnected_again)
{
	start_and_idle();
	fctx.peer_connected = true;

	zassert_equal(pairing_mode_request_bonding(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_BONDING, WAIT_MS),
		     "bonding wait");
	wait_op(FOP_DISCONNECT); /* consume the bonding disconnect event */
	zassert_equal(op_event_count(FOP_DISCONNECT), 1);

	/* A stale connection appears while waiting: suspend + disconnect
	 * again; the transition must not advance. */
	zassert_equal(pairing_mode_notify_connected(), 0);
	wait_op(FOP_DISCONNECT); /* the stale re-disconnect */
	zassert_equal(op_event_count(FOP_DISCONNECT), 2, "stale disconnect");
	zassert_equal(op_event_count(FOP_ADV_SUSPEND), 2, "stale suspend");
	zassert_equal(op_event_count(FOP_LED), 1, "only the start LED");

	fctx.peer_connected = false;
	zassert_equal(pairing_mode_notify_disconnected(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "bonding entry");
	zassert_equal(op_event_count(FOP_LED), 2, "start LED + BONDING entry LED");
}

/* New bonded pairing completes to NORMAL without disconnect/start. */
ZTEST(pairing_mode, test_new_bonded_pairing_completes_normal)
{
	start_and_idle();
	bonding_no_peer();
	zassert_equal(pairing_mode_notify_connected(), 0);
	wait_op(FOP_REQUEST_SECURITY);

	zassert_equal(pairing_mode_notify_pairing_complete(true), 0);
	zassert_true(wait_mode(PAIRING_MODE_NORMAL, WAIT_MS), "complete -> NORMAL");

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_NORMAL);
	zassert_equal(st.access_mode, PAIRING_ACCESS_NORMAL);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_IDLE);
	zassert_true(st.connected);
	zassert_false(st.led_active);

	/* No disconnect and no advertising start after the BONDING entry
	 * (the active connection stays). */
	zassert_equal(op_event_count(FOP_DISCONNECT), 1, "no extra disconnect");
	zassert_equal(op_event_count(FOP_ADV_START), 2, "no extra advertising");
}

/* Bonded secure reconnect completes to NORMAL without disconnect/start. */
ZTEST(pairing_mode, test_bonded_secure_reconnect_completes_normal)
{
	start_and_idle();
	bonding_no_peer();
	zassert_equal(pairing_mode_notify_connected(), 0);
	wait_op(FOP_REQUEST_SECURITY);

	zassert_equal(pairing_mode_notify_security_changed(true, true), 0);
	zassert_true(wait_mode(PAIRING_MODE_NORMAL, WAIT_MS), "complete -> NORMAL");

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_NORMAL);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_IDLE);
	zassert_true(st.connected);
	zassert_false(st.led_active);
	zassert_equal(op_event_count(FOP_DISCONNECT), 1, "no disconnect");
	zassert_equal(op_event_count(FOP_ADV_START), 2, "no advertising restart");
}

/* Pairing failure remains BONDING and is never fatal. */
ZTEST(pairing_mode, test_pairing_failed_remains_bonding)
{
	start_and_idle();
	bonding_no_peer();
	zassert_equal(pairing_mode_notify_connected(), 0);
	wait_op(FOP_REQUEST_SECURITY);

	zassert_equal(pairing_mode_notify_pairing_failed(), 0);
	k_sleep(K_MSEC(20));

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_IDLE);
	zassert_false(st.fatal);
	zassert_equal(fctx.reboot_count, 0);

	/* The BONDING LED pattern keeps running. */
	zassert_equal(k_sem_take(&fctx.ev[FOP_LED], K_MSEC(150)), 0,
		      "blink stopped after pairing failure");

	/* Non-bonded completion also stays BONDING. */
	zassert_equal(pairing_mode_notify_pairing_complete(false), 0);
	k_sleep(K_MSEC(20));
	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_false(st.fatal);
}

/* Failed / non-bonded security stays BONDING. */
ZTEST(pairing_mode, test_failed_nonbonded_security_remains_bonding)
{
	start_and_idle();
	bonding_no_peer();
	zassert_equal(pairing_mode_notify_connected(), 0);
	wait_op(FOP_REQUEST_SECURITY);

	zassert_equal(pairing_mode_notify_security_changed(false, true), 0);
	k_sleep(K_MSEC(20));

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_false(st.fatal);

	zassert_equal(pairing_mode_notify_security_changed(true, false), 0);
	k_sleep(K_MSEC(20));
	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_false(st.fatal);
	zassert_equal(fctx.reboot_count, 0);
}

/* Security-request failure is fatal: reboot once. */
ZTEST(pairing_mode, test_security_request_failure_reboots)
{
	start_and_idle();
	bonding_no_peer();
	fctx.ret[FOP_REQUEST_SECURITY] = -EHOSTDOWN;

	zassert_equal(pairing_mode_notify_connected(), 0);
	wait_op(FOP_REBOOT);

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_true(st.fatal);
	zassert_equal(fctx.reboot_count, 1);
}

/* Every mandatory platform-operation failure reboots exactly once and
 * stops the transition at the failing op. */
ZTEST(pairing_mode, test_each_op_failure_reboots_once)
{
	/* set_access_mode failure during start. */
	fctx.ret[FOP_SET_ACCESS] = -EIO;
	zassert_equal(pairing_mode_start(), 0);
	wait_op(FOP_REBOOT);
	zassert_equal(fctx.reboot_count, 1);
	zassert_equal(rec_count(), 3); /* set_access, finalizer LED, reboot */
	suite_before(NULL);

	/* advertising_suspend failure during bonding. */
	start_and_idle();
	fctx.ret[FOP_ADV_SUSPEND] = -EIO;
	zassert_equal(pairing_mode_request_bonding(), 0);
	wait_op(FOP_REBOOT);
	zassert_equal(fctx.reboot_count, 1);
	zassert_equal(rec_op(rec_count() - 3), FOP_ADV_SUSPEND);
	suite_before(NULL);

	/* advertising_start failure during start. */
	fctx.ret[FOP_ADV_START] = -EIO;
	zassert_equal(pairing_mode_start(), 0);
	wait_op(FOP_REBOOT);
	zassert_equal(fctx.reboot_count, 1);
	suite_before(NULL);

	/* disconnect_peer failure during bonding (peer connected). */
	start_and_idle();
	fctx.peer_connected = true;
	fctx.ret[FOP_DISCONNECT] = -EIO;
	zassert_equal(pairing_mode_request_bonding(), 0);
	wait_op(FOP_REBOOT);
	zassert_equal(fctx.reboot_count, 1);
	suite_before(NULL);

	/* delete_all_bonds failure during reset feedback. */
	start_and_idle();
	fctx.ret[FOP_DELETE_BONDS] = -EIO;
	zassert_equal(pairing_mode_request_reset(), 0);
	wait_op(FOP_REBOOT);
	zassert_equal(fctx.reboot_count, 1);
	suite_before(NULL);

	/* request_security failure during BONDING connection. */
	start_and_idle();
	bonding_no_peer();
	fctx.ret[FOP_REQUEST_SECURITY] = -EIO;
	zassert_equal(pairing_mode_notify_connected(), 0);
	wait_op(FOP_REBOOT);
	zassert_equal(fctx.reboot_count, 1);
	suite_before(NULL);

	/* led_set failure during BONDING entry. */
	start_and_idle();
	fctx.ret[FOP_LED] = -EIO;
	zassert_equal(pairing_mode_request_bonding(), 0);
	wait_op(FOP_REBOOT);
	zassert_equal(fctx.reboot_count, 1);
}

/* After a fatal, later events cause no operations. */
ZTEST(pairing_mode, test_after_fatal_no_operations)
{
	fctx.ret[FOP_SET_ACCESS] = -EIO;
	zassert_equal(pairing_mode_start(), 0);
	wait_op(FOP_REBOOT);

	int before = rec_count();

	zassert_equal(pairing_mode_notify_connected(), -ECANCELED);
	zassert_equal(pairing_mode_notify_disconnected(), -ECANCELED);
	zassert_equal(pairing_mode_notify_pairing_complete(true), -ECANCELED);
	zassert_equal(pairing_mode_notify_pairing_failed(), -ECANCELED);
	zassert_equal(pairing_mode_notify_security_changed(true, true), -ECANCELED);
	zassert_equal(pairing_mode_request_bonding(), -ECANCELED);
	zassert_equal(pairing_mode_request_reset(), -ECANCELED);
	zassert_equal(pairing_mode_request_reset_sync(K_MSEC(1)), -ECANCELED);

	k_sleep(K_MSEC(20));
	zassert_equal(rec_count(), before, "operations after fatal");
	zassert_equal(fctx.reboot_count, 1, "cold reboot exactly once");
}

/* Synchronous reset returns only after BONDING advertising is active. */
ZTEST(pairing_mode, test_reset_sync_waits_for_bonding_advertising)
{
	start_and_idle();

	k_sem_init(&sync_helper_done, 0, 1);
	struct k_sem gate;

	k_sem_init(&gate, 0, 1);

	/* Gate the BONDING advertising op so the controller cannot finish
	 * the reset while the sync caller is waiting. */
	fctx.gate[FOP_ADV_START] = &gate;

	k_thread_create(&sync_helper_thread, sync_helper_stack,
			K_THREAD_STACK_SIZEOF(sync_helper_stack), sync_helper_fn, NULL, NULL, NULL,
			7, 0, K_NO_WAIT);
	k_thread_name_set(&sync_helper_thread, "sync_reset_helper");

	/* Consume the boot start's advertising event, then wait for the
	 * reset's BONDING advertising op (~80 ms feedback): the controller
	 * is now blocked inside that op. */
	zassert_equal(k_sem_take(&fctx.ev[FOP_ADV_START], K_MSEC(WAIT_MS)), 0,
		      "boot advertising event");
	zassert_equal(k_sem_take(&fctx.ev[FOP_ADV_START], K_MSEC(WAIT_MS)), 0,
		      "reset advertising op not reached");
	zassert_equal(op_event_count(FOP_ADV_START), 2, "start + reset entry");

	/* The sync caller must still be waiting. */
	zassert_equal(k_sem_take(&sync_helper_done, K_MSEC(60)), -EAGAIN,
		      "sync reset returned before advertising active");

	/* Release: BONDING advertising completes, waiter is signaled. */
	k_sem_give(&gate);
	zassert_equal(k_sem_take(&sync_helper_done, K_MSEC(WAIT_MS)), 0,
		      "sync reset never completed");
	zassert_equal(sync_helper_result, 0, "sync reset result");

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_IDLE);
	zassert_equal(st.access_mode, PAIRING_ACCESS_BONDING);

	fctx.gate[FOP_ADV_START] = NULL;
}

/* Synchronous reset timeout is reported without corrupting the
 * transition: the reset continues and completes to BONDING. */
ZTEST(pairing_mode, test_reset_sync_timeout_continues_transition)
{
	start_and_idle();

	/* Feedback takes 80 ms; the caller gives up after 10 ms. */
	zassert_equal(pairing_mode_request_reset_sync(K_MSEC(10)), -ETIMEDOUT);

	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "reset continued after timeout");
	zassert_equal(op_event_count(FOP_DELETE_BONDS), 1);

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_IDLE);
	zassert_false(st.fatal);
}

/* A second synchronous reset waiter is rejected with -EBUSY while one
 * is registered; the first waiter still completes. */
ZTEST(pairing_mode, test_reset_sync_busy_second_waiter)
{
	start_and_idle();

	k_sem_init(&sync_helper_done, 0, 1);
	struct k_sem gate;

	k_sem_init(&gate, 0, 1);
	fctx.gate[FOP_ADV_START] = &gate;

	k_thread_create(&sync_helper_thread, sync_helper_stack,
			K_THREAD_STACK_SIZEOF(sync_helper_stack), sync_helper_fn, NULL, NULL, NULL,
			7, 0, K_NO_WAIT);
	k_thread_name_set(&sync_helper_thread, "sync_reset_helper");

	/* Wait until the first waiter's reset reaches the gated advertising
	 * op (the waiter is registered and blocked). */
	zassert_equal(k_sem_take(&fctx.ev[FOP_ADV_START], K_MSEC(WAIT_MS)), 0,
		      "boot advertising event");
	zassert_equal(k_sem_take(&fctx.ev[FOP_ADV_START], K_MSEC(WAIT_MS)), 0,
		      "reset advertising op not reached");

	/* A second synchronous waiter is busy. */
	zassert_equal(pairing_mode_request_reset_sync(K_MSEC(10)), -EBUSY);

	/* The first waiter still completes when the gate releases. */
	k_sem_give(&gate);
	zassert_equal(k_sem_take(&sync_helper_done, K_MSEC(WAIT_MS)), 0,
		      "first sync reset never completed");
	zassert_equal(sync_helper_result, 0);
	zassert_equal(fctx.reboot_count, 0);
	fctx.gate[FOP_ADV_START] = NULL;
}

/* The first real disconnect in an idle phase restarts advertising once;
 * a duplicate/stale disconnect (no prior connection) is a no-op. */
ZTEST(pairing_mode, test_duplicate_disconnect_harmless)
{
	start_and_idle();
	bonding_no_peer();
	zassert_equal(pairing_mode_notify_connected(), 0);
	wait_op(FOP_REQUEST_SECURITY);

	struct pairing_mode_status st_before;

	pairing_mode_get_status(&st_before);
	int before = rec_count();

	/* Real disconnect: BONDING/IDLE with a prior connection restarts
	 * advertising exactly once; no mode/access/LED/generation mutation. */
	zassert_equal(pairing_mode_notify_disconnected(), 0);
	wait_op(FOP_ADV_START);

	/* Stale duplicate (was_connected already false): no-op. */
	zassert_equal(pairing_mode_notify_disconnected(), 0);
	k_sleep(K_MSEC(20));

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_false(st.connected);
	zassert_false(st.fatal);
	zassert_equal(fctx.reboot_count, 0);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_IDLE);
	zassert_equal(st.access_mode, PAIRING_ACCESS_BONDING);
	zassert_equal(st.transition_generation, st_before.transition_generation,
		      "idle restart must not bump the generation");
	zassert_equal(st.led_active, st_before.led_active, "idle restart must not touch the LED");
	/* Exactly one extra operation: the advertising restart. */
	zassert_equal(rec_count(), before + 1, "only the idle restart op");
	zassert_equal(rec_op(before), FOP_ADV_START, "idle restart op type");
}

/* A matching NORMAL/IDLE disconnect (real prior connection) restarts
 * NORMAL advertising exactly once without mutating mode/access/LED. */
ZTEST(pairing_mode, test_normal_idle_disconnect_restarts_once)
{
	start_and_idle();

	/* NORMAL with a real peer connection; the connect is a no-op. */
	zassert_equal(pairing_mode_notify_connected(), 0);
	k_sleep(K_MSEC(20));

	struct pairing_mode_status st_before;

	pairing_mode_get_status(&st_before);
	zassert_equal(st_before.mode, PAIRING_MODE_NORMAL);
	zassert_equal(st_before.phase, PAIRING_MODE_PHASE_IDLE);
	zassert_true(st_before.connected);

	int before = rec_count();

	zassert_equal(pairing_mode_notify_disconnected(), 0);
	/* Wait for the disconnect processing + restart record (the FOP_ADV_START
	 * event semaphore carries a stale token from the boot/bonding entry, so
	 * the ledger count is the deterministic synchronization). */
	zassert_true(wait_rec_count(before + 1, WAIT_MS), "idle restart never recorded");

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_false(st.connected);
	zassert_false(st.fatal);
	zassert_equal(fctx.reboot_count, 0);
	zassert_equal(st.mode, PAIRING_MODE_NORMAL);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_IDLE);
	zassert_equal(st.access_mode, PAIRING_ACCESS_NORMAL);
	zassert_equal(st.transition_generation, st_before.transition_generation,
		      "no generation bump");
	zassert_equal(st.led_active, st_before.led_active, "no LED mutation");
	zassert_equal(rec_count(), before + 1, "only the idle restart op");
	zassert_equal(rec_op(before), FOP_ADV_START, "restart op type");
}

/* A matching BONDING/IDLE disconnect (real prior connection) restarts
 * OPEN (BONDING) advertising exactly once. */
ZTEST(pairing_mode, test_bonding_idle_disconnect_restarts_once)
{
	start_and_idle();
	bonding_no_peer();
	zassert_equal(pairing_mode_notify_connected(), 0);
	wait_op(FOP_REQUEST_SECURITY);

	struct pairing_mode_status st_before;

	pairing_mode_get_status(&st_before);
	zassert_equal(st_before.mode, PAIRING_MODE_BONDING);
	zassert_equal(st_before.phase, PAIRING_MODE_PHASE_IDLE);

	int before = rec_count();

	zassert_equal(pairing_mode_notify_disconnected(), 0);
	zassert_true(wait_rec_count(before + 1, WAIT_MS), "idle restart never recorded");

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_false(st.connected);
	zassert_false(st.fatal);
	zassert_equal(fctx.reboot_count, 0);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_IDLE);
	zassert_equal(st.access_mode, PAIRING_ACCESS_BONDING);
	zassert_equal(st.transition_generation, st_before.transition_generation,
		      "no generation bump");
	zassert_equal(rec_count(), before + 1, "only the idle restart op");
	zassert_equal(rec_op(before), FOP_ADV_START, "restart op type");
}

/* A stale/duplicate disconnect with no prior connection never
 * restarts advertising in any idle mode. */
ZTEST(pairing_mode, test_stale_disconnect_noop)
{
	start_and_idle();

	int before = rec_count();

	zassert_equal(pairing_mode_notify_disconnected(), 0);
	k_sleep(K_MSEC(20));

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_false(st.connected);
	zassert_false(st.fatal);
	zassert_equal(fctx.reboot_count, 0);
	zassert_equal(rec_count(), before, "stale disconnect executed ops");
	zassert_equal(st.mode, PAIRING_MODE_NORMAL);
	zassert_equal(st.phase, PAIRING_MODE_PHASE_IDLE);
}

/* A failed idle advertising restart is fatal through the dedicated
 * operation context — exactly one cold reboot, no mode/access/generation
 * mutation, and only the best-effort fatal LED force. */
ZTEST(pairing_mode, test_idle_disconnect_restart_failure_reboots_once)
{
	start_and_idle();
	zassert_equal(pairing_mode_notify_connected(), 0);
	k_sleep(K_MSEC(20));

	struct pairing_mode_status st_before;

	pairing_mode_get_status(&st_before);
	int access_before = op_event_count(FOP_SET_ACCESS);
	int led_before = op_event_count(FOP_LED);

	fctx.ret[FOP_ADV_START] = -EIO;

	zassert_equal(pairing_mode_notify_disconnected(), 0);
	wait_op(FOP_REBOOT);

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_true(st.fatal, "restart failure is fatal");
	zassert_equal(fctx.reboot_count, 1, "exactly one cold reboot");
	/* The idle restart mutates nothing before failing: no access
	 * change, no generation bump, and only the fatal path's best-effort
	 * LED-inactive force. */
	zassert_equal(op_event_count(FOP_SET_ACCESS), access_before, "no access mutation");
	zassert_equal(st.transition_generation, st_before.transition_generation,
		      "no generation bump");
	zassert_equal(op_event_count(FOP_LED), led_before + 1, "only the fatal LED force");
	/* Order: failed restart op, fatal LED force, cold reboot. */
	zassert_equal(rec_op(rec_count() - 3), FOP_ADV_START, "restart op");
	zassert_equal(rec_op(rec_count() - 2), FOP_LED, "fatal LED force");
	zassert_equal(rec_op(rec_count() - 1), FOP_REBOOT, "reboot last");
}

/* A disconnect completing a WAIT_DISCONNECT_* phase never also fires
 * the idle-restart branch (was_connected is true, but the phase is a
 * wait phase): the BONDING and RESET completions each perform exactly
 * one advertising start. */
ZTEST(pairing_mode, test_wait_phase_disconnect_no_double_start)
{
	/* BONDING wait path: exactly one advertising start (the BONDING
	 * entry), no extra idle restart. */
	start_and_idle();
	fctx.peer_connected = true;
	zassert_equal(pairing_mode_request_bonding(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_BONDING, WAIT_MS),
		     "bonding wait");
	fctx.peer_connected = false;
	zassert_equal(pairing_mode_notify_disconnected(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "bonding entry");
	zassert_equal(op_event_count(FOP_ADV_START), 2, "bonding entry start only");

	/* RESET wait path: exactly one advertising start after the feedback
	 * (the reset BONDING entry), no extra idle restart. */
	fctx.peer_connected = true;
	zassert_equal(pairing_mode_request_reset(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_RESET, WAIT_MS),
		     "reset wait");
	fctx.peer_connected = false;
	zassert_equal(pairing_mode_notify_disconnected(), 0);
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "reset -> IDLE");
	zassert_equal(op_event_count(FOP_ADV_START), 3, "reset entry start only");
	zassert_equal(fctx.reboot_count, 0);
}

/* RESET has priority over a concurrently pending BONDING. */
ZTEST(pairing_mode, test_reset_priority_over_pending_bonding)
{
	start_and_idle();

	/* Back-to-back enqueue on the (cooperative) test thread: both bits
	 * are pending before the controller runs. */
	zassert_equal(pairing_mode_request_bonding(), 0);
	zassert_equal(pairing_mode_request_reset(), 0);

	zassert_true(wait_phase(PAIRING_MODE_PHASE_RESET_FEEDBACK, WAIT_MS), "reset feedback");
	zassert_true(wait_phase(PAIRING_MODE_PHASE_IDLE, WAIT_MS), "reset -> IDLE");

	struct pairing_mode_status st;

	pairing_mode_get_status(&st);
	zassert_equal(st.mode, PAIRING_MODE_BONDING);
	/* One generation bump (the reset); the BONDING request never
	 * initiated a transition. */
	zassert_equal(st.transition_generation, 1);
	/* Exactly one advertising start after the boot start: the reset's
	 * BONDING entry only. */
	zassert_equal(op_event_count(FOP_ADV_START), 2);
	zassert_equal(op_event_count(FOP_DELETE_BONDS), 1);
	zassert_equal(fctx.reboot_count, 0);
}
