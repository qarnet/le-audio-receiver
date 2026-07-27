/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLPR application — Stage 0 handshake + heartbeat.
 * Booting from SRAM after VPR launcher copies image from RRAM.
 * RV32E e/m/c no FPU, no atomic extension.
 *
 * Protocol:
 *   1. Send READY (type=READY, version, epoch=nonce).
 *   2. Wait for ACK from CPUAPP (5 s timeout).
 *   3. Enter heartbeat loop: send hb every 1 s, echo any received hb.
 *   4. Epoch: per-boot nonce from k_uptime_get() at boot start.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>

#include "flpr_protocol.h"

/* ── IPC state ─────────────────────────────────────────────────── */

static struct ipc_ept ipc_ep;
static K_SEM_DEFINE(bound_sem, 0, 1);
static bool acked;

/* Heartbeat counters. */
static uint32_t hb_tx_seq;  /* sent by us */
static uint32_t hb_rx_seq;  /* last received from CPUAPP */
static uint32_t hb_rx_lost; /* cumulative lost (gaps) */

/* Epoch: boot-time nonce for reboot detection. */
static uint32_t boot_nonce;

/* ── IPC callbacks ─────────────────────────────────────────────── */

static void ep_bound(void *priv)
{
	ARG_UNUSED(priv);
	k_sem_give(&bound_sem);
}

static void ep_received(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(priv);

	if (len < sizeof(struct flpr_msg)) {
		return;
	}

	const struct flpr_msg *msg = data;

	switch (msg->type) {

	case FLPR_MSG_ACK:
		/* ACK from CPUAPP to our READY. Also used as heartbeat echo. */
		if (msg->version == FLPR_PROTOCOL_VERSION) {
			if (!acked) {
				acked = true;
			}
			/* Treat ACK as heartbeat echo: record rx seq. */
			uint16_t acked_seq = msg->seq;
			if (flpr_seq_after(acked_seq, (uint16_t)hb_rx_seq)) {
				hb_rx_lost += flpr_seq_gap(acked_seq, (uint16_t)hb_rx_seq) - 1U;
				hb_rx_seq = acked_seq;
			}
		}
		break;

	case FLPR_MSG_HEARTBEAT:
		/* Bidirectional: CPUAPP sent us a heartbeat. Echo it back. */
		if (msg->version == FLPR_PROTOCOL_VERSION) {
			uint16_t cpuapp_seq = msg->seq;
			if (flpr_seq_after(cpuapp_seq, (uint16_t)hb_rx_seq)) {
				hb_rx_lost += flpr_seq_gap(cpuapp_seq, (uint16_t)hb_rx_seq) - 1U;
				hb_rx_seq = cpuapp_seq;
			}
			/* Echo back as ACK with same seq. */
			struct flpr_msg echo = {
				.type = FLPR_MSG_ACK,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = cpuapp_seq,
				.data = k_uptime_get_32(),
			};
			(void)ipc_service_send(&ipc_ep, &echo, sizeof(echo));
		}
		break;

	default:
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

/* ── Helper ────────────────────────────────────────────────────── */

static int send_msg(uint8_t type, uint16_t seq, uint32_t data_val)
{
	struct flpr_msg msg = {
		.type = type,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = seq,
		.data = data_val,
	};

	return ipc_service_send(&ipc_ep, &msg, sizeof(msg));
}

/* ── main ──────────────────────────────────────────────────────── */

int main(void)
{
	const struct device *ipc_dev;
	int ret;

	/* Boot nonce: uptime at boot start. Detects reboot (epoch changes). */
	boot_nonce = k_uptime_get_32();

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

	/* Wait for binding with CPUAPP. */
	k_sem_take(&bound_sem, K_FOREVER);

	/* Send READY with protocol version + epoch. */
	do {
		ret = send_msg(FLPR_MSG_READY, 0, boot_nonce);
	} while (ret == -ENOMEM);

	/* Wait for ACK (timeout 5 s). */
	uint32_t wait_start = k_uptime_get_32();
	while (!acked && (k_uptime_get_32() - wait_start) < 5000U) {
		k_msleep(10);
	}

	/* Heartbeat loop: send every 1 s. CPUAPP sends at 1 Hz too; we echo. */
	while (1) {
		uint16_t seq = (uint16_t)(hb_tx_seq & 0xFFFFU);
		ret = send_msg(FLPR_MSG_HEARTBEAT, seq, k_uptime_get_32());
		if (ret == -ENOMEM) {
			/* Buffer full — back off, skip this beat. */
			k_msleep(FLPR_HEARTBEAT_INTERVAL_MS);
		} else {
			hb_tx_seq++;
			k_sleep(K_MSEC(FLPR_HEARTBEAT_INTERVAL_MS));
		}
	}

	return 0;
}
