/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP side of FLPR handshake, heartbeat (k_work_delayable), and stress.
 * Uses flpr_peer state machine from flpr_protocol.h.
 *
 * Lock discipline: all global state accessed only under flpr_lock.
 * ipc_service_send MUST NOT be called under spinlock — IPC callbacks
 * may re-enter.  Stress semaphore give is done outside lock.
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

/* ── Stress ─────────────────────────────────────────────────────── */

static struct k_sem stress_sem;

static uint32_t stress_count;    /* protected by flpr_lock */
static uint32_t stress_sent;     /* protected by flpr_lock */
static uint32_t stress_recv;     /* protected by flpr_lock */
static uint32_t stress_timeouts; /* protected by flpr_lock */
static bool stress_active;       /* protected by flpr_lock */
static uint32_t stress_cookie;   /* protected by flpr_lock */
static uint32_t stress_stale;    /* protected by flpr_lock */
static uint32_t stress_mismatch; /* protected by flpr_lock */
static uint32_t stress_err_send; /* protected by flpr_lock */

/* ── Ring control handlers (registered by flpr_ring_mgr) ──────────── */

static flpr_handshake_ring_handler_t ring_reset_ack_fn;
static flpr_handshake_ring_handler_t ring_consumer_fn;
static flpr_handshake_ring_handler_t ring_report_fn;
static flpr_handshake_ring_handler_t ring_stall_ack_fn;
static void *ring_handler_user_data;

/* ── Health transition callback ────────────────────────────────── */

static flpr_health_transition_cb_t health_cb;
static void *health_cb_user_data;

/* ── Fault hang ACK ───────────────────────────────────────────── */

static struct k_sem hang_ack_sem;
static bool hang_ack_received;

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

			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			flpr.tx_seq++;
			/* Periodic health check of remote (FLPR) heartbeats. */
			(void)flpr_peer_check_health(&flpr, now_ms);
			now_unhealthy = !flpr.healthy;
			k_spin_unlock(&flpr_lock, key);

			/* Invoke health transition callback OUTSIDE lock. */
			if (was_healthy && now_unhealthy && health_cb) {
				health_cb(health_cb_user_data);
			}
		}
		/* On send error: tx_seq not incremented, counted in err_send.
		 * Back off to next interval — no busy retry. */
	}

reschedule:
	k_work_schedule(&hb_work, K_MSEC(FLPR_HEARTBEAT_INTERVAL_MS));
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

	/* Shared validation — rejects short/oversize and wrong version. */
	if (!flpr_msg_validate((const struct flpr_msg *)data, len, &flpr)) {
		return;
	}

	const struct flpr_msg *msg = data;

	switch (msg->type) {

	case FLPR_MSG_READY: {
		uint32_t epoch = msg->data;
		bool is_new_epoch;
		bool start_hb_now = false;
		bool give_new_ready = false;

		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			is_new_epoch = flpr_peer_handle_ready(&flpr, epoch);
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

		LOG_INF("FLPR READY (epoch=%u, count=%u, %s)", epoch, flpr.ready_count,
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
			k_work_schedule(&hb_work, K_NO_WAIT);
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

	case FLPR_MSG_STRESS_PONG: {
		/* Classify cookie with pure helper, act under lock. */
		uint32_t cookie;
		bool active;
		enum flpr_stress_pong_class cls;

		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			cookie = msg->data;
			active = stress_active;

			if (!active) {
				k_spin_unlock(&flpr_lock, key);
				break;
			}

			cls = flpr_classify_stress_pong(cookie, stress_cookie, active);

			switch (cls) {
			case FLPR_PONG_MATCH:
				stress_recv++;
				break;
			case FLPR_PONG_STALE:
				stress_stale++;
				break;
			case FLPR_PONG_FUTURE:
				stress_mismatch++;
				break;
			default:
				break;
			}
			k_spin_unlock(&flpr_lock, key);
		}

		/* Signal outside lock ONLY on MATCH. */
		if (cls == FLPR_PONG_MATCH) {
			k_sem_give(&stress_sem);
		}
		break;
	}

	case FLPR_MSG_STRESS_PING:
	case FLPR_MSG_READY_ACK:
		/* CPUAPP receives these — unexpected but not errors. */
		break;

	/* ── Stage 1: ring control — route to ring manager ──────── */
	case FLPR_MSG_RING_RESET_ACK:
		if (ring_reset_ack_fn) {
			ring_reset_ack_fn(msg, ring_handler_user_data);
		}
		break;
	case FLPR_MSG_RING_CONSUMER:
		if (ring_consumer_fn) {
			ring_consumer_fn(msg, ring_handler_user_data);
		}
		break;
	case FLPR_MSG_RING_TEST_REPORT:
		if (ring_report_fn) {
			ring_report_fn(msg, ring_handler_user_data);
		}
		break;
	case FLPR_MSG_RING_STALL_ACK:
		if (ring_stall_ack_fn) {
			ring_stall_ack_fn(msg, ring_handler_user_data);
		}
		break;

	/* ── Stage 4B: fault hang ACK ───────────────────────── */
	case FLPR_MSG_FAULT_HANG_ACK:
		hang_ack_received = true;
		k_sem_give(&hang_ack_sem);
		break;

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
	k_sem_init(&stress_sem, 0, FLPR_STRESS_MAX_COUNT + 1);
	k_sem_init(&hang_ack_sem, 0, 1);

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
	status->stress_active = stress_active;
	status->stress_count = stress_count;
	status->stress_sent = stress_sent;
	status->stress_recv = stress_recv;
	status->stress_timeouts = stress_timeouts;
	status->stress_stale = stress_stale;
	status->stress_mismatch = stress_mismatch;
	status->stress_err_send = stress_err_send;
	k_spin_unlock(&flpr_lock, key);
}

void flpr_handshake_stress(uint32_t count, struct flpr_status *out)
{
	if (count == 0) {
		return;
	}
	if (count > FLPR_STRESS_MAX_COUNT) {
		count = FLPR_STRESS_MAX_COUNT;
	}

	/* Guard: reject if not ready+healthy or if already active. */
	{
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		if (stress_active) {
			k_spin_unlock(&flpr_lock, key);
			if (out) {
				flpr_handshake_get_status(out);
			}
			return;
		}
		if (!flpr.ready || !flpr.acked) {
			k_spin_unlock(&flpr_lock, key);
			LOG_WRN("FLPR stress rejected: FLPR not ready");
			if (out) {
				flpr_handshake_get_status(out);
			}
			return;
		}
		/* Reset stress state under lock. */
		stress_active = true;
		stress_count = count;
		stress_sent = 0;
		stress_recv = 0;
		stress_timeouts = 0;
		stress_cookie = 0;
		stress_stale = 0;
		stress_mismatch = 0;
		stress_err_send = 0;
		k_spin_unlock(&flpr_lock, key);
	}

	/* Drain any stale semaphore give from a previous interrupted run.
	 * k_sem_reset is not available; use k_sem_take with K_NO_WAIT
	 * until semaphore is empty. */
	while (k_sem_take(&stress_sem, K_NO_WAIT) == 0) {
		/* drain */
	}

	LOG_INF("FLPR stress start: %u pings", count);

	for (uint32_t i = 0; i < count; i++) {

		/* Drain semaphore before EVERY iteration: guards against late-PONG
		 * from a previous timed-out iteration. */
		while (k_sem_take(&stress_sem, K_NO_WAIT) == 0) {
			/* drain */
		}

		/* Snapshot cookie under lock. */
		uint32_t cookie;
		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			stress_cookie++;
			cookie = stress_cookie;
			k_spin_unlock(&flpr_lock, key);
		}

		struct flpr_msg ping = {
			.type = FLPR_MSG_STRESS_PING,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = (uint16_t)(i & 0xFFFFU),
			.data = cookie,
		};

		int ret = ipc_service_send(&flpr_ep, &ping, sizeof(ping));
		if (ret < 0) {
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			stress_err_send++;
			k_spin_unlock(&flpr_lock, key);
			LOG_WRN("FLPR stress ping %u send failed: %d", i, ret);
			k_msleep(1);
			continue;
		}

		/* Count sent after successful send. */
		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			stress_sent++;
			k_spin_unlock(&flpr_lock, key);
		}

		/* Wait for matching PONG with 200 ms timeout. */
		ret = k_sem_take(&stress_sem, K_MSEC(200));
		if (ret != 0) {
			/* Timeout: invalidate expected cookie so any late PONG
			 * for THIS iteration is classified as stale, not
			 * mistaken for the next iteration's match. */
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			stress_timeouts++;
			stress_cookie++; /* invalidate → late PONG is stale */
			k_spin_unlock(&flpr_lock, key);
		}
	}

	LOG_INF("FLPR stress done: sent=%u recv=%u lost=%u timeouts=%u "
		"stale=%u mismatch=%u err=%u",
		stress_sent, stress_recv, count - stress_recv, stress_timeouts, stress_stale,
		stress_mismatch, stress_err_send);

	{
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		stress_active = false;
		k_spin_unlock(&flpr_lock, key);
	}

	if (out) {
		flpr_handshake_get_status(out);
	}
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
		/* If epoch already different from previous, sem already given? */
		if (flpr.ready && flpr.epoch != previous_epoch) {
			/* Epoch already new — drain sem and succeed. */
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
	return ipc_service_send(&flpr_ep, msg, sizeof(*msg));
}

void flpr_handshake_register_ring_handlers(flpr_handshake_ring_handler_t reset_ack_fn,
					   flpr_handshake_ring_handler_t consumer_fn,
					   flpr_handshake_ring_handler_t report_fn,
					   flpr_handshake_ring_handler_t stall_ack_fn,
					   void *user_data)
{
	k_spinlock_key_t key = k_spin_lock(&flpr_lock);

	ring_reset_ack_fn = reset_ack_fn;
	ring_consumer_fn = consumer_fn;
	ring_report_fn = report_fn;
	ring_stall_ack_fn = stall_ack_fn;
	ring_handler_user_data = user_data;

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

/* ── Fault hang injection ────────────────────────────────────────── */

int flpr_handshake_send_fault_hang(uint32_t timeout_ms)
{
	/* Drain any stale semaphore give. */
	while (k_sem_take(&hang_ack_sem, K_NO_WAIT) == 0) {
	}
	hang_ack_received = false;

	struct flpr_msg hang_msg = {
		.type = FLPR_MSG_FAULT_HANG,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = 0,
		.data = 0,
	};
	int ret = ipc_service_send(&flpr_ep, &hang_msg, sizeof(hang_msg));
	if (ret < 0) {
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr.err_send++;
		k_spin_unlock(&flpr_lock, key);
		LOG_ERR("FAULT_HANG send failed: %d", ret);
		return -EIO;
	}

	/* Wait for FAULT_HANG_ACK from FLPR. */
	ret = k_sem_take(&hang_ack_sem, K_MSEC(timeout_ms));
	if (ret != 0) {
		LOG_WRN("FAULT_HANG_ACK timeout (%u ms)", timeout_ms);
		return -ETIMEDOUT;
	}

	if (!hang_ack_received) {
		return -EIO;
	}

	LOG_INF("FAULT_HANG_ACK received — FLPR hang imminent");
	return 0;
}
