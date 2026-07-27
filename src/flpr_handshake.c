/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP side of FLPR handshake protocol.
 * Opens IPC instance, registers endpoint, handles READY/HEARTBEAT messages.
 * Graceful if FLPR absent or version-mismatched — one actionable error logged.
 * Non-blocking: never hangs the audio pipeline.
 */

#include "flpr_handshake.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(flpr_hs, LOG_LEVEL_INF);

/* ── IPC variables ──────────────────────────────────────────── */

static struct ipc_ept flpr_ep;
static K_SEM_DEFINE(bound_sem, 0, 1);

/* ── Status counters (atomic, read-safe from any context) ──── */

static atomic_t flpr_ready;
static atomic_t flpr_acked;
static uint32_t flpr_rx_hb; /* last received heartbeat counter */
static uint32_t flpr_tx_hb; /* last sent heartbeat counter */
static uint32_t flpr_ready_cnt;
static uint32_t flpr_err_cnt;
static uint32_t last_hb_uptime;

/* ── IPC callbacks ──────────────────────────────────────────── */

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
	atomic_clear(&flpr_ready);
	atomic_clear(&flpr_acked);
}

static void ep_received(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(priv);

	if (len < sizeof(struct flpr_msg)) {
		LOG_ERR("FLPR short message (%zu < %zu)", len, sizeof(struct flpr_msg));
		flpr_err_cnt++;
		return;
	}

	const struct flpr_msg *msg = data;

	switch (msg->type) {

	case FLPR_MSG_READY:
		if (msg->version != FLPR_PROTOCOL_VERSION) {
			LOG_ERR("FLPR version mismatch: got %u, expected %u", msg->version,
				FLPR_PROTOCOL_VERSION);
			flpr_err_cnt++;
			return;
		}
		atomic_set(&flpr_ready, 1);
		flpr_ready_cnt++;
		LOG_INF("FLPR READY (epoch %u)", flpr_ready_cnt);

		/* Send ACK immediately. */
		{
			struct flpr_msg ack = {
				.type = FLPR_MSG_ACK,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 0,
				.data = k_uptime_get_32(),
			};
			int ret = ipc_service_send(&flpr_ep, &ack, sizeof(ack));
			if (ret < 0) {
				LOG_ERR("FLPR ACK send failed: %d", ret);
				flpr_err_cnt++;
			} else {
				atomic_set(&flpr_acked, 1);
				LOG_INF("FLPR ACK sent");
			}
		}
		break;

	case FLPR_MSG_HEARTBEAT:
		if (msg->version != FLPR_PROTOCOL_VERSION) {
			flpr_err_cnt++;
			return;
		}
		flpr_rx_hb = msg->seq;
		last_hb_uptime = msg->data;
		LOG_DBG("FLPR heartbeat seq=%u uptime=%u ms", msg->seq, msg->data);
		break;

	case FLPR_MSG_ACK:
		/* CPUAPP doesn't expect ACK from FLPR. */
		break;

	default:
		LOG_WRN("FLPR unknown msg type %u", msg->type);
		break;
	}
}

static void ep_error(const char *message, void *priv)
{
	ARG_UNUSED(priv);
	LOG_ERR("FLPR IPC error: %s", message ? message : "unknown");
	flpr_err_cnt++;
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

/* ── Public API ──────────────────────────────────────────────── */

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

	status->ready = atomic_get(&flpr_ready) != 0;
	status->acked = atomic_get(&flpr_acked) != 0;
	status->rx_heartbeat = flpr_rx_hb;
	status->tx_heartbeat = flpr_tx_hb;
	status->ready_count = flpr_ready_cnt;
	status->error_count = flpr_err_cnt;
}

void flpr_handshake_heartbeat(void)
{
	/* Stage 0: heartbeats disabled to avoid ICMsg PBUF accumulation.
	 * ICMsg assertion fires if pbuf is not drained fast enough.
	 * Once the handshake is established, no further communication
	 * is needed for Stage 0 gate — only boot + READY + ACK. */
	(void)flpr_tx_hb;
	(void)last_hb_uptime;
}
