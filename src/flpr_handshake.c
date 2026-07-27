/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP side of FLPR handshake, heartbeat (k_work_delayable), and stress.
 * Uses flpr_peer state machine from flpr_protocol.h.
 *
 * Heartbeat runs at 1 Hz via k_work_delayable, independent of main loop.
 * Stress-test runs synchronously with stop-and-wait ping/pong.
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

/* ── State ──────────────────────────────────────────────────────── */

static struct flpr_peer flpr; /* tracks FLPR (remote) */

/* ── Delayed work for heartbeat ─────────────────────────────────── */

static struct k_work_delayable hb_work;
static bool hb_started;

/* ── Stress ─────────────────────────────────────────────────────── */

static struct k_sem stress_sem;
static uint32_t stress_count;
static uint32_t stress_sent;
static uint32_t stress_recv;
static uint32_t stress_timeouts;
static bool stress_active;
static uint32_t stress_cookie; /* increments per ping */

/* ── Helpers ────────────────────────────────────────────────────── */

/* Send a message. Must NOT be called under spinlock (IPC callbacks may re-enter). */
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

	if (!flpr.acked) {
		goto reschedule;
	}

	uint32_t now_ms = k_uptime_get_32();
	uint16_t seq = (uint16_t)(flpr.tx_seq & 0xFFFFU);

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
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr.ready = true;
		flpr.ready_count++;
		flpr.epoch = epoch;
		flpr.healthy = true;
		k_spin_unlock(&flpr_lock, key);

		LOG_INF("FLPR READY (epoch=%u, count=%u)", epoch, flpr.ready_count);

		/* Send READY_ACK (outside lock). */
		struct flpr_msg ack = {
			.type = FLPR_MSG_READY_ACK,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = 0,
			.data = now_ms,
		};
		int ret = ipc_service_send(&flpr_ep, &ack, sizeof(ack));
		if (ret < 0) {
			key = k_spin_lock(&flpr_lock);
			flpr.err_send++;
			k_spin_unlock(&flpr_lock, key);
			LOG_ERR("FLPR READY_ACK send failed: %d", ret);
		} else {
			key = k_spin_lock(&flpr_lock);
			flpr.acked = true;
			k_spin_unlock(&flpr_lock, key);
			LOG_INF("FLPR READY_ACK sent");

			/* Start heartbeat work if not already. */
			if (!hb_started) {
				hb_started = true;
				k_work_schedule(&hb_work, K_NO_WAIT);
			}
		}
		break;
	}

	case FLPR_MSG_HEARTBEAT: {
		/* Remote heartbeat: track sequence. */
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr_peer_rx_seq(&flpr, msg->seq, now_ms);
		flpr.healthy = flpr_peer_check_health(&flpr, now_ms);
		k_spin_unlock(&flpr_lock, key);

		/* Echo back as HEARTBEAT_ACK (outside lock). */
		struct flpr_msg echo = {
			.type = FLPR_MSG_HEARTBEAT_ACK,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = msg->seq,
			.data = now_ms,
		};
		int ret = ipc_service_send(&flpr_ep, &echo, sizeof(echo));
		if (ret < 0) {
			key = k_spin_lock(&flpr_lock);
			flpr.err_send++;
			k_spin_unlock(&flpr_lock, key);
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
		/* FLPR responded to our stress ping. */
		if (stress_active && msg->data == stress_cookie) {
			stress_recv++;
			k_sem_give(&stress_sem);
		}
		break;
	}

	case FLPR_MSG_STRESS_PING:
	case FLPR_MSG_READY_ACK:
		/* CPUAPP receives these — unexpected but not errors.
		 * FLPR may send READY_ACK if it receives our READY_ACK. */
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

	memset(&flpr, 0, sizeof(flpr));

	k_work_init_delayable(&hb_work, hb_work_fn);
	k_sem_init(&stress_sem, 0, 1);

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
	status->rx_consec_missed = flpr.rx_consec_missed;
	status->stress_active = stress_active;
	status->stress_count = stress_count;
	status->stress_sent = stress_sent;
	status->stress_recv = stress_recv;
	status->stress_timeouts = stress_timeouts;
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

	/* Guard against concurrent stress runs. */
	if (stress_active) {
		if (out) {
			flpr_handshake_get_status(out);
		}
		return;
	}

	stress_active = true;
	stress_count = count;
	stress_sent = 0;
	stress_recv = 0;
	stress_timeouts = 0;
	stress_cookie = 0;

	LOG_INF("FLPR stress start: %u pings", count);

	for (uint32_t i = 0; i < count; i++) {
		stress_cookie++;

		struct flpr_msg ping = {
			.type = FLPR_MSG_STRESS_PING,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = (uint16_t)(i & 0xFFFFU),
			.data = stress_cookie,
		};

		int ret = ipc_service_send(&flpr_ep, &ping, sizeof(ping));
		if (ret < 0) {
			stress_timeouts++;
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			flpr.err_send++;
			k_spin_unlock(&flpr_lock, key);
			k_msleep(10);
			continue;
		}
		stress_sent++;

		/* Wait for PONG with 200 ms timeout. */
		ret = k_sem_take(&stress_sem, K_MSEC(200));
		if (ret != 0) {
			stress_timeouts++;
		}
	}

	LOG_INF("FLPR stress done: sent=%u recv=%u lost=%u timeouts=%u", stress_sent, stress_recv,
		count - stress_recv, stress_timeouts);

	stress_active = false;

	if (out) {
		flpr_handshake_get_status(out);
	}
}
