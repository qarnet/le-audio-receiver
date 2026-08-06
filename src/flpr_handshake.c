/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP side of FLPR handshake and heartbeat (k_work_delayable).
 * Uses flpr_peer state machine from flpr_protocol.h.
 *
 * R8: production runtime only.  The stress-test ping/pong state and the
 * fault-hang request state moved to src/flpr_acceptance.c; the four
 * diagnostic message types (STRESS_PONG, RING_TEST_REPORT,
 * RING_STALL_ACK, FAULT_HANG_ACK) route to the registered diagnostic
 * handler slot.
 *
 * Lock discipline: all global state accessed only under flpr_lock.
 * ipc_service_send MUST NOT be called under spinlock — IPC callbacks
 * may re-enter.
 *
 * All state transitions go through PRODUCTION helpers in flpr_protocol.h.
 * No raw field assignment on the peer struct — helpers enforce invariants
 * (epoch detection, sequence tracking, health transition counting, etc.).
 */

#include "flpr_handshake.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

LOG_MODULE_REGISTER(flpr_hs, LOG_LEVEL_INF);

/* ── IPC ────────────────────────────────────────────────────────── */

static const struct device *ipc_dev; /* stored after init */
static struct ipc_ept flpr_ep;
static K_SEM_DEFINE(bound_sem, 0, 1);
static K_SEM_DEFINE(new_ready_sem, 0, 1);
static struct k_spinlock flpr_lock;

/* ── State (all protected by flpr_lock) ──────────────────────────── */

static struct flpr_peer flpr;  /* tracks FLPR (remote) */
static bool session_available; /* false between disconnect / reconnect */

/* ── Delayed work for heartbeat ─────────────────────────────────── */

static struct k_work_delayable hb_work;
static bool hb_started; /* protected by flpr_lock */

/* ── Ring control handlers (registered by flpr_ring_mgr) ──────────── */

static flpr_handshake_ring_handler_t ring_reset_ack_fn;
static flpr_handshake_ring_handler_t ring_consumer_fn;
static void *ring_handler_user_data;

/* ── Diagnostic handler (registered by flpr_acceptance) ────────────
 * Receives STRESS_PONG, RING_TEST_REPORT, RING_STALL_ACK, and
 * FAULT_HANG_ACK.  NULL (unregistered) → messages silently dropped
 * (identical to the pre-R8 inert acceptance state). */

static flpr_handshake_ring_handler_t diag_fn;
static void *diag_user_data;

/* ── Health transition callback ────────────────────────────────── */

static flpr_health_transition_cb_t health_cb;
static void *health_cb_user_data;

/* ── Helpers ────────────────────────────────────────────────────── */

/* Send a message. Returns 0 on success, negative errno on failure.
 * Counts err_send under lock on failure.  Never called under spinlock. */
static int send_msg(const struct flpr_msg *msg)
{
	int ret = ipc_service_send(&flpr_ep, msg, sizeof(*msg));
	if (ret < 0) {
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr.err_send++;
		k_spin_unlock(&flpr_lock, key);
	}
	return ret;
}

/* ── Heartbeat work scheduling ─────────────────────────────────────
 * Test mode (FLPR_HANDSHAKE_NATIVE_TEST) records the READY-triggered
 * async start instead of submitting it, so the system workqueue never
 * runs the handler concurrently with the single-threaded ztest runner;
 * tests invoke heartbeat iterations synchronously and teardown cancels
 * the reschedule before it fires. */

#if defined(FLPR_HANDSHAKE_NATIVE_TEST)
#include "flpr_handshake_hooks.h"
#define FLPR_HS_WORK_START()      flpr_handshake_test_work_start()
#define FLPR_HS_WORK_RESCHEDULE() flpr_handshake_test_work_reschedule()
#else
#define FLPR_HS_WORK_START()      k_work_schedule(&hb_work, K_NO_WAIT)
#define FLPR_HS_WORK_RESCHEDULE() k_work_schedule(&hb_work, K_MSEC(FLPR_HEARTBEAT_INTERVAL_MS))
#endif

/* ── Heartbeat work handler ─────────────────────────────────────── */

static void hb_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	bool should_send;
	bool was_healthy;
	uint16_t seq;

	{
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		should_send = flpr.acked && session_available;
		seq = (uint16_t)(flpr.tx_seq & 0xFFFFU);
		was_healthy = flpr.healthy;
		k_spin_unlock(&flpr_lock, key);
	}

	if (!should_send) {
		goto reschedule;
	}

	{
		uint32_t now_ms = k_uptime_get_32();

		struct flpr_msg msg = {
			.type = FLPR_MSG_HEARTBEAT,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = seq,
			.data = now_ms,
		};

		int ret = send_msg(&msg);
		if (ret >= 0) {
			bool now_unhealthy = false;
			flpr_health_transition_cb_t cb;
			void *cb_ud;

			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			flpr.tx_seq++;
			/* Periodic health check of remote (FLPR) heartbeats. */
			(void)flpr_peer_check_health(&flpr, now_ms);
			now_unhealthy = !flpr.healthy;
			/* R1: snapshot the health callback + user-data under the
			 * lock together with the transition state, then invoke
			 * OUTSIDE the lock. */
			if (was_healthy && now_unhealthy) {
				cb = health_cb;
				cb_ud = health_cb_user_data;
			} else {
				cb = NULL;
				cb_ud = NULL;
			}
			k_spin_unlock(&flpr_lock, key);

			if (cb) {
				cb(cb_ud);
			}
		}
		/* On send error: tx_seq not incremented, counted in err_send.
		 * Back off to next interval — no busy retry. */
	}

reschedule:
	/* R1 note: cancelling the heartbeat from inside its own running
	 * work item cannot prevent this final reschedule; that is by design.
	 * A later wake after teardown is a no-op because the inactive
	 * checks (acked/session_available) gate every send. */
	FLPR_HS_WORK_RESCHEDULE();
}

/* ── IPC callbacks ──────────────────────────────────────────────── */

static void ep_bound(void *priv)
{
	ARG_UNUSED(priv);
	k_spinlock_key_t key = k_spin_lock(&flpr_lock);
	flpr.bound = true;
	k_spin_unlock(&flpr_lock, key);
	LOG_INF("FLPR IPC bound");
	k_sem_give(&bound_sem);
}

static void ep_unbound(void *priv)
{
	ARG_UNUSED(priv);
	LOG_WRN("FLPR IPC unbound");
	k_spinlock_key_t key = k_spin_lock(&flpr_lock);

	/* Preserve lifetime counters: ready_count, reboot_count, err_*,
	 * rx_missed_total, and last remote epoch.  Clear bound/ready/
	 * acked/healthy, session sequences and timestamps. */
	uint32_t saved_ready_count = flpr.ready_count;
	uint32_t saved_reboot_count = flpr.reboot_count;
	uint32_t saved_err_len = flpr.err_len;
	uint32_t saved_err_version = flpr.err_version;
	uint32_t saved_err_unknown = flpr.err_unknown;
	uint32_t saved_err_send = flpr.err_send;
	uint32_t saved_rx_missed_total = flpr.rx_missed_total;
	uint32_t saved_epoch = flpr.epoch;

	memset(&flpr, 0, sizeof(flpr));

	flpr.ready_count = saved_ready_count;
	flpr.reboot_count = saved_reboot_count;
	flpr.err_len = saved_err_len;
	flpr.err_version = saved_err_version;
	flpr.err_unknown = saved_err_unknown;
	flpr.err_send = saved_err_send;
	flpr.rx_missed_total = saved_rx_missed_total;
	flpr.epoch = saved_epoch;

	hb_started = false;
	session_available = false;
	k_sem_reset(&new_ready_sem);
	k_spin_unlock(&flpr_lock, key);
}

static void ep_received(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(priv);
	uint32_t now_ms = k_uptime_get_32();

	/* R1: shared validation runs under flpr_lock because it mutates
	 * err_len/err_version read by flpr_handshake_get_status(). */
	bool valid;

	{
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		valid = flpr_msg_validate((const struct flpr_msg *)data, len, &flpr);
		k_spin_unlock(&flpr_lock, key);
	}
	if (!valid) {
		return;
	}

	const struct flpr_msg *msg = data;

	switch (msg->type) {

	case FLPR_MSG_READY: {
		uint32_t epoch = msg->data;
		bool is_new_epoch;
		bool start_hb_now = false;
		bool give_new_ready = false;
		uint32_t ready_count;

		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			is_new_epoch = flpr_peer_handle_ready(&flpr, epoch);
			/* R1: capture the READY count under the lock so the log
			 * below never reads a concurrently mutated counter. */
			ready_count = flpr.ready_count;
			if (session_available && !hb_started) {
				hb_started = true;
				start_hb_now = true;
			}
			/* New epoch → give new_ready_sem (for runtime restart). */
			if (is_new_epoch && session_available) {
				give_new_ready = true;
			}
			k_spin_unlock(&flpr_lock, key);
		}

		LOG_INF("FLPR READY (epoch=%u, count=%u, %s)", epoch, ready_count,
			is_new_epoch ? "new" : "duplicate");

		/* Send READY_ACK outside lock. */
		{
			struct flpr_msg ack = {
				.type = FLPR_MSG_READY_ACK,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 0,
				.data = now_ms,
			};
			int ret = ipc_service_send(&flpr_ep, &ack, sizeof(ack));
			if (ret < 0) {
				k_spinlock_key_t key = k_spin_lock(&flpr_lock);
				flpr.err_send++;
				k_spin_unlock(&flpr_lock, key);
				LOG_ERR("FLPR READY_ACK send failed: %d", ret);
			} else {
				k_spinlock_key_t key = k_spin_lock(&flpr_lock);
				flpr.acked = true;
				k_spin_unlock(&flpr_lock, key);
				LOG_INF("FLPR READY_ACK sent");
				/* Give new_ready_sem AFTER ACK succeeds. */
				if (give_new_ready) {
					k_sem_give(&new_ready_sem);
				}
			}
		}

		/* Start heartbeat work outside lock. */
		if (start_hb_now) {
			FLPR_HS_WORK_START();
		}
		break;
	}

	case FLPR_MSG_HEARTBEAT: {
		/* Remote heartbeat: track sequence, then echo back.
		 * Health updated inside rx_seq (timestamp) + check_health. */
		bool should_echo;

		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			flpr_peer_rx_seq(&flpr, msg->seq, now_ms);
			(void)flpr_peer_check_health(&flpr, now_ms);
			should_echo = flpr.acked;
			k_spin_unlock(&flpr_lock, key);
		}

		if (should_echo) {
			struct flpr_msg echo = {
				.type = FLPR_MSG_HEARTBEAT_ACK,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = msg->seq,
				.data = now_ms,
			};
			int ret = ipc_service_send(&flpr_ep, &echo, sizeof(echo));
			if (ret < 0) {
				k_spinlock_key_t key = k_spin_lock(&flpr_lock);
				flpr.err_send++;
				k_spin_unlock(&flpr_lock, key);
			}
		}
		break;
	}

	case FLPR_MSG_HEARTBEAT_ACK: {
		/* FLPR echoed a heartbeat we sent. Validate and track ack. */
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr_peer_handle_heartbeat_ack(&flpr, msg->seq);
		k_spin_unlock(&flpr_lock, key);
		break;
	}

	case FLPR_MSG_STRESS_PING:
	case FLPR_MSG_READY_ACK:
		/* CPUAPP receives these — unexpected but not errors. */
		break;

	/* ── Stage 1: ring control — route to ring manager ────────
	 * R1: snapshot the handler function + shared user-data under
	 * flpr_lock, then invoke OUTSIDE the lock (a handler may call
	 * back into get_status / take ring locks). */
	case FLPR_MSG_RING_RESET_ACK: {
		flpr_handshake_ring_handler_t fn;
		void *ud;

		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			fn = ring_reset_ack_fn;
			ud = ring_handler_user_data;
			k_spin_unlock(&flpr_lock, key);
		}
		if (fn) {
			fn(msg, ud);
		}
		break;
	}
	case FLPR_MSG_RING_CONSUMER: {
		flpr_handshake_ring_handler_t fn;
		void *ud;

		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			fn = ring_consumer_fn;
			ud = ring_handler_user_data;
			k_spin_unlock(&flpr_lock, key);
		}
		if (fn) {
			fn(msg, ud);
		}
		break;
	}

	/* ── R8: diagnostic message types ────────────────────────
	 * STRESS_PONG, RING_TEST_REPORT, RING_STALL_ACK, and
	 * FAULT_HANG_ACK route to the diagnostic handler slot registered
	 * by flpr_acceptance_init().  Same snapshot-under-lock /
	 * invoke-outside-lock semantics as the production slot.  With no
	 * handler registered the messages are silently dropped (the
	 * pre-R8 acceptance state was core but inert). */
	case FLPR_MSG_STRESS_PONG:
	case FLPR_MSG_RING_TEST_REPORT:
	case FLPR_MSG_RING_STALL_ACK:
	case FLPR_MSG_FAULT_HANG_ACK: {
		flpr_handshake_ring_handler_t fn;
		void *ud;

		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			fn = diag_fn;
			ud = diag_user_data;
			k_spin_unlock(&flpr_lock, key);
		}
		if (fn) {
			fn(msg, ud);
		}
		break;
	}

	default: {
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr.err_unknown++;
		k_spin_unlock(&flpr_lock, key);
	} break;
	}
}

static void ep_error(const char *message, void *priv)
{
	ARG_UNUSED(priv);
	LOG_ERR("FLPR IPC error: %s", message ? message : "unknown");
}

static const struct ipc_ept_cfg flpr_ep_cfg = {
	.name = "cpuapp_ep",
	.cb =
		{
			.bound = ep_bound,
			.unbound = ep_unbound,
			.received = ep_received,
			.error = ep_error,
		},
};

/* ── Public API ──────────────────────────────────────────────────── */

int flpr_handshake_init(void)
{
	const struct device *dev;
	int ret;

	k_work_init_delayable(&hb_work, hb_work_fn);

	{
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr_peer_reset(&flpr);
		hb_started = false;
		session_available = true;
		k_sem_reset(&new_ready_sem);
		k_spin_unlock(&flpr_lock, key);
	}

	dev = DEVICE_DT_GET(DT_NODELABEL(ipc0));
	if (!device_is_ready(dev)) {
		LOG_ERR("FLPR IPC device not ready");
		return -ENODEV;
	}
	ipc_dev = dev;

	ret = ipc_service_open_instance(dev);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("FLPR ipc_service_open_instance failed: %d", ret);
		return ret;
	}

	ret = ipc_service_register_endpoint(dev, &flpr_ep, &flpr_ep_cfg);
	if (ret < 0) {
		LOG_ERR("FLPR ipc_service_register_endpoint failed: %d", ret);
		return ret;
	}

	LOG_INF("FLPR handshake init OK (waiting for FLPR boot)");
	return 0;
}

void flpr_handshake_get_status(struct flpr_status *status)
{
	if (!status) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&flpr_lock);
	status->ready = flpr.ready;
	status->acked = flpr.acked;
	status->healthy = flpr.healthy;
	status->epoch = flpr.epoch;
	status->ready_count = flpr.ready_count;
	status->reboot_count = flpr.reboot_count;
	status->err_len = flpr.err_len;
	status->err_version = flpr.err_version;
	status->err_unknown = flpr.err_unknown;
	status->err_send = flpr.err_send;
	status->tx_seq = flpr.tx_seq;
	status->tx_acked_seq = flpr.tx_acked_seq;
	status->rx_seq = flpr.rx_seq;
	status->rx_lost = flpr.rx_lost;
	status->rx_dup = flpr.rx_dup;
	status->rx_ooo = flpr.rx_ooo;
	status->rx_last_ms = flpr.rx_last_ms;
	status->rx_missed_total = flpr.rx_missed_total;

	/* R8: stress fields are acceptance-owned (flpr_acceptance.c).
	 * Zero them so the shared flpr_status is never garbage; the
	 * shell merges flpr_acceptance_stress_snapshot() when the
	 * acceptance config is enabled. */
	status->stress_active = false;
	status->stress_count = 0;
	status->stress_sent = 0;
	status->stress_recv = 0;
	status->stress_timeouts = 0;
	status->stress_stale = 0;
	status->stress_mismatch = 0;
	status->stress_err_send = 0;
	k_spin_unlock(&flpr_lock, key);
}

/* ── Runtime restart API ──────────────────────────────────────────── */

int flpr_handshake_disconnect(void)
{
	k_spinlock_key_t key = k_spin_lock(&flpr_lock);
	session_available = false;
	k_sem_reset(&new_ready_sem);
	k_spin_unlock(&flpr_lock, key);

	/* Drain bound semaphore (any pending give from prior bound). */
	while (k_sem_take(&bound_sem, K_NO_WAIT) == 0) {
	}

	/* Cancel heartbeat work (no send while disconnected). */
	(void)k_work_cancel_delayable(&hb_work);

	/* Deregister endpoint. */
	int ret = ipc_service_deregister_endpoint(&flpr_ep);
	if (ret < 0) {
		LOG_ERR("FLPR ipc_service_deregister_endpoint failed: %d", ret);
		return ret;
	}

	LOG_INF("FLPR handshake disconnected");
	return 0;
}

int flpr_handshake_reconnect(void)
{
	int ret;

	/* Drain bound semaphore (any stale event). */
	while (k_sem_take(&bound_sem, K_NO_WAIT) == 0) {
	}

	/* Re-drain new_ready_sem. */
	while (k_sem_take(&new_ready_sem, K_NO_WAIT) == 0) {
	}

	ret = ipc_service_register_endpoint(ipc_dev, &flpr_ep, &flpr_ep_cfg);
	if (ret < 0) {
		LOG_ERR("FLPR ipc_service_register_endpoint re-register failed: %d", ret);
		return ret;
	}

	{
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		session_available = true;
		k_spin_unlock(&flpr_lock, key);
	}

	LOG_INF("FLPR handshake reconnected");
	return 0;
}

int flpr_handshake_wait_bound(k_timeout_t timeout)
{
	int ret = k_sem_take(&bound_sem, timeout);
	if (ret == 0) {
		/* Re-post so future waiters also see it. */
		k_sem_give(&bound_sem);
	}
	return ret;
}

int flpr_handshake_wait_new_ready(uint32_t previous_epoch, k_timeout_t timeout)
{
	int ret;

	{
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		if (!session_available) {
			k_spin_unlock(&flpr_lock, key);
			return -ECANCELED;
		}
		/* Fast path: epoch already different from previous AND the
		 * READY_ACK for it succeeded.  The new-ready semaphore is only
		 * given after a successful ACK send, so acked is required here
		 * too — a changed epoch whose ACK failed must not succeed. */
		if (flpr.ready && flpr.acked && flpr.epoch != previous_epoch) {
			/* Epoch already new and acked — drain sem and succeed. */
			k_spin_unlock(&flpr_lock, key);
			while (k_sem_take(&new_ready_sem, K_NO_WAIT) == 0) {
			}
			return 0;
		}
		k_spin_unlock(&flpr_lock, key);
	}

	ret = k_sem_take(&new_ready_sem, timeout);
	if (ret == 0) {
		/* Drain any extra post (edge case). */
		while (k_sem_take(&new_ready_sem, K_NO_WAIT) == 0) {
		}
	}
	return ret;
}

/* ── Ring control IPC helpers ─────────────────────────────────────── */

int flpr_handshake_send_msg(const struct flpr_msg *msg)
{
	if (!msg) {
		return -EINVAL;
	}
	/* Route through send_msg() so send failures increment err_send
	 * exactly as documented in the header. */
	return send_msg(msg);
}

void flpr_handshake_register_ring_handlers(flpr_handshake_ring_handler_t reset_ack_fn,
					   flpr_handshake_ring_handler_t consumer_fn,
					   void *user_data)
{
	k_spinlock_key_t key = k_spin_lock(&flpr_lock);

	ring_reset_ack_fn = reset_ack_fn;
	ring_consumer_fn = consumer_fn;
	ring_handler_user_data = user_data;

	k_spin_unlock(&flpr_lock, key);
}

/* ── Diagnostic handler registration (R8) ─────────────────────────── */

void flpr_handshake_register_diag_handlers(flpr_handshake_ring_handler_t diag_handle_fn,
					   void *user_data)
{
	k_spinlock_key_t key = k_spin_lock(&flpr_lock);

	diag_fn = diag_handle_fn;
	diag_user_data = user_data;

	k_spin_unlock(&flpr_lock, key);
}

/* ── Health transition callback registration ─────────────────────── */

void flpr_handshake_register_health_cb(flpr_health_transition_cb_t cb, void *user_data)
{
	k_spinlock_key_t key = k_spin_lock(&flpr_lock);
	health_cb = cb;
	health_cb_user_data = user_data;
	k_spin_unlock(&flpr_lock, key);
}

#if defined(FLPR_HANDSHAKE_NATIVE_TEST)
/* GCOVR_EXCL_START — test-only helpers, absent from production builds */

/* ── Test-only helpers ─────────────────────────────────────────────
 * Compiled only under FLPR_HANDSHAKE_NATIVE_TEST (native_sim suite).
 * Production builds contain none of these symbols.  These helpers reset
 * module-static state, drive one heartbeat iteration synchronously, and
 * arrange peer health/timestamp state; they never replace protocol
 * helpers or callback switch logic. */

static uint32_t test_hb_start_requests;
static uint32_t test_hb_reschedules;

void flpr_handshake_test_reset(void)
{
	k_work_cancel_delayable(&hb_work);
	k_sem_reset(&bound_sem);
	k_sem_reset(&new_ready_sem);

	k_spinlock_key_t key = k_spin_lock(&flpr_lock);
	flpr_peer_reset(&flpr);
	hb_started = false;
	session_available = false;
	ring_reset_ack_fn = NULL;
	ring_consumer_fn = NULL;
	ring_handler_user_data = NULL;
	diag_fn = NULL;
	diag_user_data = NULL;
	health_cb = NULL;
	health_cb_user_data = NULL;
	k_spin_unlock(&flpr_lock, key);

	test_hb_start_requests = 0;
	test_hb_reschedules = 0;
}

void flpr_handshake_test_heartbeat_once(void)
{
	hb_work_fn(&hb_work.work);
}

void flpr_handshake_test_set_peer_state(bool bound, bool ready, bool acked, bool healthy,
					uint32_t rx_last_ms)
{
	k_spinlock_key_t key = k_spin_lock(&flpr_lock);
	flpr.bound = bound;
	flpr.ready = ready;
	flpr.acked = acked;
	flpr.healthy = healthy;
	flpr.rx_last_ms = rx_last_ms;
	k_spin_unlock(&flpr_lock, key);
}

void flpr_handshake_test_work_start(void)
{
	/* Record the READY-triggered async start; do NOT submit it. */
	test_hb_start_requests++;
}

void flpr_handshake_test_work_reschedule(void)
{
	/* Real reschedule at the documented interval; test teardown
	 * cancels it before it fires (tests complete well under 1 s). */
	test_hb_reschedules++;
	k_work_schedule(&hb_work, K_MSEC(FLPR_HEARTBEAT_INTERVAL_MS));
}

uint32_t flpr_handshake_test_hb_start_requests(void)
{
	return test_hb_start_requests;
}

uint32_t flpr_handshake_test_hb_reschedules(void)
{
	return test_hb_reschedules;
}

bool flpr_handshake_test_work_pending(void)
{
	return k_work_delayable_is_pending(&hb_work);
}

uint32_t flpr_handshake_test_bound_sem_count(void)
{
	return k_sem_count_get(&bound_sem);
}

uint32_t flpr_handshake_test_new_ready_sem_count(void)
{
	return k_sem_count_get(&new_ready_sem);
}
/* GCOVR_EXCL_STOP */

#endif /* FLPR_HANDSHAKE_NATIVE_TEST */
