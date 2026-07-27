/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP side of FLPR handshake protocol.
 * Opens IPC instance, registers endpoint, handles READY/HEARTBEAT messages.
 * Sends bidirectional heartbeats at 1 Hz, tracks loss/gap/health.
 * Graceful if FLPR absent or version-mismatched — one actionable error logged.
 * No blocking: never hangs the audio pipeline.
 *
 * References: src/flpr_protocol.h (shared wire protocol, single source of truth).
 */

#include "flpr_handshake.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(flpr_hs, LOG_LEVEL_INF);

/* ── IPC variables ──────────────────────────────────────────────── */

static struct ipc_ept flpr_ep;
static K_SEM_DEFINE(bound_sem, 0, 1);

/* ── Spinlock-protected status ──────────────────────────────────── */

static struct k_spinlock flpr_lock;

static bool flpr_ready;
static bool flpr_acked;
static bool flpr_healthy;
static uint32_t flpr_ready_cnt;
static uint32_t flpr_epoch;
static uint32_t flpr_err_cnt;

/* Heartbeat: CPUAPP → FLPR */
static uint32_t flpr_tx_seq;
static uint32_t flpr_tx_lost;
static uint32_t flpr_tx_last_ms; /* last time we sent */

/* Heartbeat: FLPR → CPUAPP */
static uint32_t flpr_rx_seq;
static uint32_t flpr_rx_lost;
static uint32_t flpr_rx_last_ms;
static uint32_t flpr_rx_missed;

/* ── Helpers ────────────────────────────────────────────────────── */

static int send_msg(uint8_t type, uint16_t seq, uint32_t data_val)
{
	struct flpr_msg msg = {
		.type = type,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = seq,
		.data = data_val,
	};
	return ipc_service_send(&flpr_ep, &msg, sizeof(msg));
}

/* ── IPC callbacks ──────────────────────────────────────────────── */

static void ep_bound(void *priv)
{
	ARG_UNUSED(priv);
	LOG_INF("FLPR IPC bound");
	k_sem_give(&bound_sem);
}

static void ep_unbound(void *priv)
{
	ARG_UNUSED(priv);
	LOG_WRN("FLPR IPC unbound");
	k_spinlock_key_t key = k_spin_lock(&flpr_lock);
	flpr_ready = false;
	flpr_acked = false;
	flpr_healthy = false;
	k_spin_unlock(&flpr_lock, key);
}

static void ep_received(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(priv);

	if (len < sizeof(struct flpr_msg)) {
		LOG_ERR("FLPR short message (%zu < %zu)", len, sizeof(struct flpr_msg));
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr_err_cnt++;
		k_spin_unlock(&flpr_lock, key);
		return;
	}

	const struct flpr_msg *msg = data;

	switch (msg->type) {

	case FLPR_MSG_READY: {
		if (msg->version != FLPR_PROTOCOL_VERSION) {
			LOG_ERR("FLPR version mismatch: got %u, expected %u", msg->version,
				FLPR_PROTOCOL_VERSION);
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			flpr_err_cnt++;
			k_spin_unlock(&flpr_lock, key);
			return;
		}

		uint32_t epoch = msg->data;
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		flpr_ready = true;
		flpr_ready_cnt++;
		flpr_epoch = epoch;
		k_spin_unlock(&flpr_lock, key);

		LOG_INF("FLPR READY (version=%u, epoch=%u, count=%u)", msg->version, epoch,
			flpr_ready_cnt);

		/* Send ACK. */
		int ret = send_msg(FLPR_MSG_ACK, 0, k_uptime_get_32());
		if (ret < 0) {
			LOG_ERR("FLPR ACK send failed: %d", ret);
			key = k_spin_lock(&flpr_lock);
			flpr_err_cnt++;
			k_spin_unlock(&flpr_lock, key);
		} else {
			key = k_spin_lock(&flpr_lock);
			flpr_acked = true;
			flpr_healthy = true; /* start healthy */
			k_spin_unlock(&flpr_lock, key);
			LOG_INF("FLPR ACK sent");
		}
		break;
	}

	case FLPR_MSG_HEARTBEAT: {
		if (msg->version != FLPR_PROTOCOL_VERSION) {
			k_spinlock_key_t key = k_spin_lock(&flpr_lock);
			flpr_err_cnt++;
			k_spin_unlock(&flpr_lock, key);
			return;
		}
		uint16_t rx_seq = msg->seq;
		uint32_t now_ms = k_uptime_get_32();
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		/* Gap detection: if we've received heartbeats before. */
		if (flpr_rx_seq != 0 || flpr_rx_last_ms != 0) {
			if (flpr_seq_after(rx_seq, (uint16_t)flpr_rx_seq)) {
				uint16_t gap = flpr_seq_gap(rx_seq, (uint16_t)flpr_rx_seq);
				if (gap > 1) {
					flpr_rx_lost += gap - 1;
				}
				flpr_rx_seq = rx_seq;
				flpr_rx_missed = 0;
				flpr_healthy = true;
			} else if (rx_seq == (uint16_t)flpr_rx_seq) {
				/* Duplicate — don't count as missed. */
			} else {
				/* Wrapped or out of order — reset tracking. */
				flpr_rx_seq = rx_seq;
				flpr_rx_missed = 0;
			}
		} else {
			/* First heartbeat. */
			flpr_rx_seq = rx_seq;
		}
		flpr_rx_last_ms = now_ms;
		k_spin_unlock(&flpr_lock, key);
		break;
	}

	case FLPR_MSG_ACK: {
		/* FLPR echoed our heartbeat back (echo via ACK). */
		if (msg->version != FLPR_PROTOCOL_VERSION) {
			return;
		}
		uint16_t echoed_seq = msg->seq;
		k_spinlock_key_t key = k_spin_lock(&flpr_lock);
		if (flpr_seq_after(echoed_seq, (uint16_t)flpr_tx_seq)) {
			/* FLPR echoed a future seq (gap in our tracking). */
		} else {
			uint16_t gap = flpr_seq_gap((uint16_t)flpr_tx_seq, echoed_seq);
			if (gap > 0 && gap < 32768) {
				flpr_tx_lost += gap;
			}
		}
		k_spin_unlock(&flpr_lock, key);
		break;
	}

	default:
		LOG_WRN("FLPR unknown msg type %u", msg->type);
		break;
	}
}

static void ep_error(const char *message, void *priv)
{
	ARG_UNUSED(priv);
	LOG_ERR("FLPR IPC error: %s", message ? message : "unknown");
	k_spinlock_key_t key = k_spin_lock(&flpr_lock);
	flpr_err_cnt++;
	k_spin_unlock(&flpr_lock, key);
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
	status->ready = flpr_ready;
	status->acked = flpr_acked;
	status->healthy = flpr_healthy;
	status->ready_count = flpr_ready_cnt;
	status->epoch = flpr_epoch;
	status->error_count = flpr_err_cnt;
	status->tx_seq = flpr_tx_seq;
	status->tx_lost = flpr_tx_lost;
	status->rx_seq = flpr_rx_seq;
	status->rx_lost = flpr_rx_lost;
	status->rx_last_ms = flpr_rx_last_ms;
	status->rx_missed = flpr_rx_missed;
	k_spin_unlock(&flpr_lock, key);
}

void flpr_handshake_heartbeat(void)
{
	/* Only send if handshake established. */
	if (!flpr_acked) {
		return;
	}

	uint32_t now_ms = k_uptime_get_32();

	/* Rate-limit to 1 Hz. */
	if (now_ms - flpr_tx_last_ms < FLPR_HEARTBEAT_INTERVAL_MS) {
		return;
	}
	flpr_tx_last_ms = now_ms;

	uint16_t seq = (uint16_t)(flpr_tx_seq & 0xFFFFU);
	int ret = send_msg(FLPR_MSG_HEARTBEAT, seq, now_ms);
	if (ret < 0) {
		/* Skip this beat — buffer full. Don't count as lost. */
		LOG_DBG("FLPR heartbeat send: %d", ret);
		return;
	}
	flpr_tx_seq++;

	/* Health check: if rx has been silent too long, mark unhealthy. */
	k_spinlock_key_t key = k_spin_lock(&flpr_lock);
	if (flpr_rx_last_ms > 0 &&
	    now_ms - flpr_rx_last_ms > FLPR_HEARTBEAT_MISS_MAX * FLPR_HEARTBEAT_INTERVAL_MS) {
		flpr_healthy = false;
	}
	k_spin_unlock(&flpr_lock, key);
}
