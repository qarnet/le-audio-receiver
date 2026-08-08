/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private Bluetooth pairing adapter (P4 of the user pairing control plan,
 * docs/development/user-pairing-control-plan.md).  See
 * bt_bap_pairing_adapter.h for the ownership contract.
 *
 * The adapter applies exactly one requested operation or event per call:
 * it owns no visible transition state, phases, or timers — pairing_mode
 * remains the sole transition owner.  All Bluetooth work happens through
 * the injected backend implemented in bt_bap.c; this file contains no
 * Zephyr Bluetooth connection, advertising, or address types.
 *
 * Applied access state (NORMAL / BONDING / SUSPENDED) is an atomic owned
 * here and initialized SUSPENDED by bt_bap_pairing_adapter_init().  Only
 * the operation callbacks mutate it:
 *   - NORMAL    -> policy BONDED_ONLY, inventory preserved, publish NORMAL;
 *   - BONDING   -> policy OPEN, inventory preserved, publish BONDING;
 *   - SUSPENDED -> publish SUSPENDED, policy mode and inventory untouched;
 *   - any other value -> -EINVAL, nothing changes.
 * The advertising start operation rejects applied SUSPENDED with -EACCES:
 * the P1 owner must select NORMAL or BONDING first.
 *
 * Notification translation runs on the BT RX callback path: keep it short.
 * The gate (notifications_enable) is one-way; before it opens, callbacks
 * preserve legacy behavior and never invoke a pairing-mode notification.
 * Enqueue results are logged but never retried, never block, never issue
 * HCI work, and never create a second fatal owner.
 */

#include "bt_bap_pairing_adapter.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(bt_bap_pairing_adapter, LOG_LEVEL_INF);

/* ── Singleton state ─────────────────────────────────────────────── */

/* Immutable after a successful init. */
static const struct bt_bap_pairing_backend *g_backend;
static void *g_ctx;

static atomic_t g_initialized;
static atomic_t g_applied; /* enum pairing_access_mode */
static atomic_t g_notify_enabled;

/* ── Notification enqueue result handling ────────────────────────── */

static void notify_result(const char *event, int ret)
{
	if (ret == 0) {
		return;
	}
	if (ret == -ECANCELED) {
		/* The controller already finalized a fatal and reboots:
		 * informational, never duplicate recovery. */
		LOG_INF("pairing notification '%s' after fatal: -ECANCELED (no recovery)", event);
		return;
	}
	/* Never retry, block, or perform HCI work from a callback; the
	 * controller is the only transition and fatal owner. */
	LOG_WRN("pairing notification '%s' enqueue failed: %d", event, ret);
}

/* ── Public API ──────────────────────────────────────────────────── */

int bt_bap_pairing_adapter_init(const struct bt_bap_pairing_backend *backend, void *ctx)
{
	if (backend == NULL || backend->adv_locked == NULL || backend->adv_stop == NULL ||
	    backend->fal_clear == NULL || backend->bonds_replace == NULL ||
	    backend->policy_snapshot == NULL || backend->adv_update_param == NULL ||
	    backend->adv_start == NULL || backend->policy_set_mode == NULL ||
	    backend->policy_clear == NULL || backend->peer_disconnect == NULL ||
	    backend->peer_security == NULL || backend->storage_delete == NULL ||
	    backend->notify_connected == NULL || backend->notify_disconnected == NULL ||
	    backend->notify_pairing_complete == NULL || backend->notify_pairing_failed == NULL ||
	    backend->notify_security_changed == NULL) {
		return -EINVAL; /* atomic: nothing stored */
	}

	if (atomic_get(&g_initialized)) {
		return -EALREADY;
	}

	g_backend = backend;
	g_ctx = ctx;
	atomic_set(&g_applied, PAIRING_ACCESS_SUSPENDED);
	atomic_set(&g_notify_enabled, 0);
	atomic_set(&g_initialized, 1);
	return 0;
}

int bt_bap_pairing_adapter_set_access_mode(enum pairing_access_mode mode, void *ctx)
{
	int ret;

	ARG_UNUSED(ctx);

	if (!atomic_get(&g_initialized)) {
		return -EINVAL;
	}

	switch (mode) {
	case PAIRING_ACCESS_NORMAL:
		ret = g_backend->policy_set_mode(BT_PAIRING_POLICY_MODE_BONDED_ONLY, g_ctx);
		if (ret < 0) {
			return ret; /* policy unchanged; applied state not published */
		}
		atomic_set(&g_applied, PAIRING_ACCESS_NORMAL);
		return 0;
	case PAIRING_ACCESS_BONDING:
		ret = g_backend->policy_set_mode(BT_PAIRING_POLICY_MODE_OPEN, g_ctx);
		if (ret < 0) {
			return ret;
		}
		atomic_set(&g_applied, PAIRING_ACCESS_BONDING);
		return 0;
	case PAIRING_ACCESS_SUSPENDED:
		/* No policy change: P1 owns the transition; the inventory and
		 * the policy mode are preserved. */
		atomic_set(&g_applied, PAIRING_ACCESS_SUSPENDED);
		return 0;
	default:
		return -EINVAL; /* no change */
	}
}

/* Suspend: stop advertising (idempotent) and publish SUSPENDED only on
 * success, all under the advertising lock. */
static int suspend_locked(void *ctx)
{
	int ret = g_backend->adv_stop(ctx);

	if (ret == 0) {
		atomic_set(&g_applied, PAIRING_ACCESS_SUSPENDED);
	}
	return ret;
}

int bt_bap_pairing_adapter_advertising_suspend(void *ctx)
{
	ARG_UNUSED(ctx);

	if (!atomic_get(&g_initialized)) {
		return -EINVAL;
	}
	return g_backend->adv_locked(suspend_locked, g_ctx);
}

/* Start: reject applied SUSPENDED, then stop -> FAL clear ->
 * enumerate/replace -> policy snapshot -> params (FAL rebuild when
 * BONDED_ONLY) -> start, all under the advertising lock.  The filter
 * decision comes from the snapshot MODE, never from the entry count. */
static int start_locked(void *ctx)
{
	struct bt_bap_pairing_policy_snap snap;
	struct bt_bap_pairing_adv_req req;
	int ret;

	if (atomic_get(&g_applied) == PAIRING_ACCESS_SUSPENDED) {
		return -EACCES;
	}

	ret = g_backend->adv_stop(ctx);
	if (ret < 0) {
		return ret;
	}
	ret = g_backend->fal_clear(ctx);
	if (ret < 0) {
		return ret;
	}
	ret = g_backend->bonds_replace(ctx);
	if (ret < 0) {
		return ret;
	}
	g_backend->policy_snapshot(&snap, ctx);
	req.filter_connections = (snap.mode == BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	ret = g_backend->adv_update_param(&req, ctx);
	if (ret < 0) {
		return ret;
	}
	return g_backend->adv_start(ctx);
}

int bt_bap_pairing_adapter_advertising_start(void *ctx)
{
	ARG_UNUSED(ctx);

	if (!atomic_get(&g_initialized)) {
		return -EINVAL;
	}
	return g_backend->adv_locked(start_locked, g_ctx);
}

int bt_bap_pairing_adapter_disconnect_peer(bool *pending, void *ctx)
{
	struct bt_bap_pairing_peer_result result = {0};
	int ret;

	ARG_UNUSED(ctx);

	if (pending == NULL) {
		return -EINVAL;
	}
	*pending = false;

	if (!atomic_get(&g_initialized)) {
		return -EINVAL;
	}

	ret = g_backend->peer_disconnect(&result, g_ctx);
	if (ret != 0) {
		return ret; /* info/disconnect failure: exact errno, no pending */
	}
	if (result.state == BT_BAP_PAIRING_PEER_CONNECTED ||
	    result.state == BT_BAP_PAIRING_PEER_DISCONNECTING) {
		/* CONNECTED with a successful disconnect request, or an
		 * already-DISCONNECTING link: the caller waits for the
		 * disconnect completion.  No duplicate command. */
		*pending = true;
	}
	return 0;
}

int bt_bap_pairing_adapter_delete_all_bonds(void *ctx)
{
	int ret;

	ARG_UNUSED(ctx);

	if (!atomic_get(&g_initialized)) {
		return -EINVAL;
	}

	/* Storage deletion first; the in-memory inventory is cleared only
	 * after the storage operation succeeded.  The desired mode is
	 * preserved (P1 stays SUSPENDED until the reset feedback ends). */
	ret = g_backend->storage_delete(g_ctx);
	if (ret != 0) {
		return ret;
	}
	g_backend->policy_clear(g_ctx);
	return 0;
}

int bt_bap_pairing_adapter_request_security(void *ctx)
{
	struct bt_bap_pairing_peer_result result = {0};
	int ret;

	ARG_UNUSED(ctx);

	if (!atomic_get(&g_initialized)) {
		return -EINVAL;
	}

	ret = g_backend->peer_security(&result, g_ctx);
	if (ret != 0) {
		return ret; /* set_security or info failure: exact errno */
	}
	if (result.state != BT_BAP_PAIRING_PEER_CONNECTED) {
		return -ENOTCONN; /* absent, disconnecting, or other */
	}
	return 0;
}

void bt_bap_pairing_adapter_notifications_enable(void)
{
	atomic_set(&g_notify_enabled, 1);
}

bool bt_bap_pairing_adapter_notifications_enabled(void)
{
	return atomic_get(&g_notify_enabled) != 0;
}

/* ── Callback-event translation (BT RX context) ──────────────────── */

void bt_bap_pairing_adapter_notify_connected(void)
{
	int ret;

	if (!atomic_get(&g_notify_enabled)) {
		return; /* legacy behavior: never call P1 before enable */
	}
	ret = g_backend->notify_connected(g_ctx);
	notify_result("connected", ret);
}

void bt_bap_pairing_adapter_notify_disconnected(void)
{
	int ret;

	if (!atomic_get(&g_notify_enabled)) {
		return;
	}
	ret = g_backend->notify_disconnected(g_ctx);
	notify_result("disconnected", ret);
}

void bt_bap_pairing_adapter_notify_pairing_complete(bool bonded)
{
	int ret;

	if (!atomic_get(&g_notify_enabled)) {
		return;
	}
	ret = g_backend->notify_pairing_complete(bonded, g_ctx);
	notify_result("pairing_complete", ret);
}

void bt_bap_pairing_adapter_notify_pairing_failed(void)
{
	int ret;

	if (!atomic_get(&g_notify_enabled)) {
		return;
	}
	ret = g_backend->notify_pairing_failed(g_ctx);
	notify_result("pairing_failed", ret);
}

void bt_bap_pairing_adapter_notify_security_changed(bool success, bool bonded)
{
	int ret;

	if (!atomic_get(&g_notify_enabled)) {
		return;
	}
	ret = g_backend->notify_security_changed(success, bonded, g_ctx);
	notify_result("security_changed", ret);
}

#ifdef BT_BAP_PAIRING_ADAPTER_TEST
/* GCOVR_EXCL_START — test seams, absent from production builds */
/*
 * Test seams (tests/unit/bt_bap_pairing_adapter): a single function
 * resets every file-static module state between tests.  The backend and
 * context are cleared and the applied access state returns to SUSPENDED
 * with notifications disabled.  None of this enters production firmware.
 */
void bt_bap_pairing_adapter_test_reset(void)
{
	g_backend = NULL;
	g_ctx = NULL;
	atomic_set(&g_initialized, 0);
	atomic_set(&g_applied, PAIRING_ACCESS_SUSPENDED);
	atomic_set(&g_notify_enabled, 0);
}
/* GCOVR_EXCL_STOP */
#endif /* BT_BAP_PAIRING_ADAPTER_TEST */
