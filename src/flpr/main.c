/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLPR application — Stage 0 handshake + heartbeat + stress pong.
 * Uses flpr_peer for remote (CPUAPP) state tracking.
 *
 * Epoch: hardware GRTC counter at boot start (non-zero, monotonic
 * across resets). GRTC owned-channels 3,4 available per board DTS.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>

#include "flpr_protocol.h"

/* ── IPC state ─────────────────────────────────────────────────── */

static struct ipc_ept ipc_ep;
static K_SEM_DEFINE(bound_sem, 0, 1);

/* Tracks remote peer (CPUAPP). Single-threaded on FLPR, no lock. */
static struct flpr_peer cpuapp;

/* Epoch from hardware GRTC at boot start. */
static uint32_t boot_epoch;

/* ── Helpers ────────────────────────────────────────────────────── */

static int send_msg(const struct flpr_msg *msg)
{
	return ipc_service_send(&ipc_ep, msg, sizeof(*msg));
}

/* ── IPC callbacks ──────────────────────────────────────────────── */

static void ep_bound(void *priv)
{
	ARG_UNUSED(priv);
	cpuapp.bound = true;
	k_sem_give(&bound_sem);
}

static void ep_received(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(priv);
	uint32_t now_ms = k_uptime_get_32();

	if (!flpr_msg_validate(data, len, &cpuapp)) {
		return;
	}

	const struct flpr_msg *msg = data;

	switch (msg->type) {

	case FLPR_MSG_READY_ACK:
		cpuapp.acked = true;
		cpuapp.healthy = true;
		cpuapp.epoch = msg->data;
		break;

	case FLPR_MSG_HEARTBEAT:
		/* CPUAPP heartbeat → track seq, echo with HEARTBEAT_ACK. */
		flpr_peer_rx_seq(&cpuapp, msg->seq, now_ms);
		cpuapp.healthy = flpr_peer_check_health(&cpuapp, now_ms);

		{
			struct flpr_msg echo = {
				.type = FLPR_MSG_HEARTBEAT_ACK,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = msg->seq,
				.data = now_ms,
			};
			(void)send_msg(&echo);
		}
		break;

	case FLPR_MSG_HEARTBEAT_ACK:
		/* CPUAPP echoed our heartbeat. Track acked seq. */
		{
			uint16_t acked_seq = msg->seq;
			if (flpr_seq_after(acked_seq, (uint16_t)cpuapp.tx_acked_seq) ||
			    acked_seq == (uint16_t)cpuapp.tx_acked_seq) {
				cpuapp.tx_acked_seq = acked_seq;
			}
		}
		break;

	case FLPR_MSG_STRESS_PING:
		/* Stress test: echo back as STRESS_PONG with same cookie. */
		{
			struct flpr_msg pong = {
				.type = FLPR_MSG_STRESS_PONG,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = msg->seq,
				.data = msg->data,
			};
			(void)send_msg(&pong);
		}
		break;

	default:
		cpuapp.err_unknown++;
		break;
	}
}

static const struct ipc_ept_cfg ep_cfg = {
	.name = "flpr_ep",
	.cb =
		{
			.bound = ep_bound,
			.received = ep_received,
		},
};

/* ── main ──────────────────────────────────────────────────────── */

int main(void)
{
	const struct device *ipc_dev;
	int ret;

	/* Epoch: capture GRTC counter at boot. GRTC is a free-running
	 * 32-bit counter driven by LFCLK (32.768 kHz). Nonzero, monotonic
	 * across resets — provides distinct epochs for reboot detection.
	 * k_cycle_get_32() returns GRTC cycle count on nRF54L15. */
	boot_epoch = k_cycle_get_32();

	memset(&cpuapp, 0, sizeof(cpuapp));

	ipc_dev = DEVICE_DT_GET(DT_NODELABEL(ipc0));
	if (!device_is_ready(ipc_dev)) {
		return -ENODEV;
	}

	ret = ipc_service_open_instance(ipc_dev);
	if (ret < 0 && ret != -EALREADY) {
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc_dev, &ipc_ep, &ep_cfg);
	if (ret < 0) {
		return ret;
	}

	k_sem_take(&bound_sem, K_FOREVER);

	/* Send READY with epoch. Retry on -ENOMEM with backoff
	 * (max 5 s). Busy-spin was a bug — unbounded without backoff.
	 * On non-ENOMEM error after bound, enter safe state (spin). */
	{
		struct flpr_msg ready = {
			.type = FLPR_MSG_READY,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = 0,
			.data = boot_epoch,
		};
		uint32_t send_start = k_uptime_get_32();
		uint32_t backoff_ms = 1;
		bool sent = false;
		while (!sent && (k_uptime_get_32() - send_start) < 5000U) {
			ret = send_msg(&ready);
			if (ret == -ENOMEM) {
				k_msleep(backoff_ms);
				if (backoff_ms < 64) {
					backoff_ms *= 2;
				}
			} else if (ret < 0) {
				/* Hard send failure — back off and retry. */
				k_msleep(backoff_ms);
				if (backoff_ms < 64) {
					backoff_ms *= 2;
				}
			} else {
				sent = true;
			}
		}
		/* If timeout: enter safe idle (IPC still accepts incoming). */
	}

	/* Wait for READY_ACK (timeout 5 s). */
	uint32_t wait_start = k_uptime_get_32();
	while (!cpuapp.acked && (k_uptime_get_32() - wait_start) < 5000U) {
		k_msleep(10);
	}

	/* 1 Hz heartbeat loop. Increment tx_seq only on successful send.
	 * On any error: back off, count error, do not increment seq. */
	while (1) {
		uint32_t now_ms = k_uptime_get_32();
		uint16_t seq = (uint16_t)(cpuapp.tx_seq & 0xFFFFU);

		struct flpr_msg hb = {
			.type = FLPR_MSG_HEARTBEAT,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = seq,
			.data = now_ms,
		};
		ret = send_msg(&hb);
		if (ret >= 0) {
			cpuapp.tx_seq++;
			cpuapp.err_send = 0;
			k_sleep(K_MSEC(FLPR_HEARTBEAT_INTERVAL_MS));
		} else {
			cpuapp.err_send++;
			k_msleep(FLPR_HEARTBEAT_INTERVAL_MS);
		}
	}

	return 0;
}
