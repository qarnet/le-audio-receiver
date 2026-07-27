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

static struct ipc_ept flpr_ep;
static K_SEM_DEFINE(bound_sem, 0, 1);
static struct k_spinlock flpr_lock;

/* ── State (all protected by flpr_lock) ──────────────────────────── */

static struct flpr_peer flpr; /* tracks FLPR (remote) */

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
	uint16_t seq;
	uint32_t now_ms = k_uptime_get_32();

	{
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		should_send = flpr.acked;
		seq = (uint16_t)(flpr.tx_seq & 0xFFFFU);
		k_spin_unlock(&flpr_lock, key);
	}

	if (!should_send) {
		goto reschedule;
	}

	struct flpr_msg msg = {
		.type = FLPR_MSG_HEARTBEAT,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = seq,
		.data = now_ms,
	};

	int ret = send_msg(&msg);
	if (ret >= 0) {
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr.tx_seq++;
		flpr.healthy = flpr_peer_check_health(&flpr, now_ms);
		k_spin_unlock(&flpr_lock, key);
	}
	/* On send error: tx_seq not incremented, counted in err_send.
	 * Back off to next interval — no busy retry. */

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
	flpr = (struct flpr_peer){0};
	hb_started = false;
	k_spin_unlock(&flpr_lock, key);
}

static void ep_received(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(priv);
	uint32_t now_ms = k_uptime_get_32();

	if (len != sizeof(struct flpr_msg)) {
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr.err_len++;
		k_spin_unlock(&flpr_lock, key);
		return;
	}

	const struct flpr_msg *msg = data;

	if (msg->version != FLPR_PROTOCOL_VERSION) {
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr.err_version++;
		k_spin_unlock(&flpr_lock, key);
		return;
	}

	switch (msg->type) {

	case FLPR_MSG_READY: {
		uint32_t epoch = msg->data;
		bool start_hb_now = false;
		bool send_ack = false;

		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			flpr.ready = true;
			flpr.ready_count++;
			flpr.epoch = epoch;
			flpr.healthy = true;
			if (!hb_started) {
				hb_started = true;
				start_hb_now = true;
			}
			send_ack = true;
			k_spin_unlock(&flpr_lock, key);
		}

		LOG_INF("FLPR READY (epoch=%u, count=%u)", epoch, flpr.ready_count);

		/* Send READY_ACK outside lock. */
		if (send_ack) {
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
			}
		}

		/* Start heartbeat work outside lock. */
		if (start_hb_now) {
			k_work_schedule(&hb_work, K_NO_WAIT);
		}
		break;
	}

	case FLPR_MSG_HEARTBEAT: {
		/* Remote heartbeat: track sequence, then echo back. */
		bool should_echo;

		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			flpr_peer_rx_seq(&flpr, msg->seq, now_ms);
			flpr.healthy = flpr_peer_check_health(&flpr, now_ms);
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
		/* FLPR echoed a heartbeat we sent. Track ack. */
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		uint16_t acked_seq = msg->seq;
		if (flpr_seq_after(acked_seq, (uint16_t)flpr.tx_acked_seq) ||
		    acked_seq == (uint16_t)flpr.tx_acked_seq) {
			flpr.tx_acked_seq = acked_seq;
		}
		k_spin_unlock(&flpr_lock, key);
		break;
	}

	case FLPR_MSG_STRESS_PONG: {
		/* Stress response: validate cookie under lock,
		 * signal outside lock. */
		bool is_match;
		bool is_stale;
		bool should_give = false;

		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);

			if (!stress_active) {
				k_spin_unlock(&flpr_lock, key);
				break;
			}

			is_match = (msg->data == stress_cookie);
			is_stale = flpr_seq_after(stress_cookie, msg->data) ||
				   stress_cookie == msg->data;
			/* stale = cookie <= current_cookie but not exact match.
			 * Since we just checked is_match, stale means
			 * msg->data < stress_cookie. */
			is_stale = (msg->data < stress_cookie);

			if (is_match) {
				stress_recv++;
				should_give = true;
			} else if (is_stale) {
				stress_stale++;
			} else {
				/* cookie > current — future/unknown */
				stress_mismatch++;
			}
			k_spin_unlock(&flpr_lock, key);
		}

		if (should_give) {
			k_sem_give(&stress_sem);
		}
		break;
	}

	case FLPR_MSG_STRESS_PING:
	case FLPR_MSG_READY_ACK:
		/* CPUAPP receives these — unexpected but not errors. */
		break;

	default:
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr.err_unknown++;
		k_spin_unlock(&flpr_lock, key);
		break;
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
	const struct device *ipc_dev;
	int ret;

	k_work_init_delayable(&hb_work, hb_work_fn);
	k_sem_init(&stress_sem, 0, FLPR_STRESS_MAX_COUNT + 1);

	{
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		memset(&flpr, 0, sizeof(flpr));
		hb_started = false;
		k_spin_unlock(&flpr_lock, key);
	}

	ipc_dev = DEVICE_DT_GET(DT_NODELABEL(ipc0));
	if (!device_is_ready(ipc_dev)) {
		LOG_ERR("FLPR IPC device not ready");
		return -ENODEV;
	}

	ret = ipc_service_open_instance(ipc_dev);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("FLPR ipc_service_open_instance failed: %d", ret);
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc_dev, &flpr_ep, &flpr_ep_cfg);
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
		{
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			stress_cookie++;
			k_spin_unlock(&flpr_lock, key);
		}

		struct flpr_msg ping = {
			.type = FLPR_MSG_STRESS_PING,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = (uint16_t)(i & 0xFFFFU),
			.data = stress_cookie,
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
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			stress_timeouts++;
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
