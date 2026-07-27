/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLPR application — Stage 0 handshake.
 * Booting from SRAM after VPR launcher copies image from RRAM.
 * Sends READY over IPC, waits for ACK, starts heartbeat.
 * RV32E e/m/c no FPU, no atomic extension.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <string.h>
#include <zephyr/ipc/ipc_service.h>

/* Protocol: version must match on both sides. */
#define FLPR_PROTOCOL_VERSION 1

/* Message types — keep in sync with cpuapp handshake code. */
#define FLPR_MSG_READY     0x01U /* FLPR -> CPUAPP: I'm alive */
#define FLPR_MSG_ACK       0x02U /* CPUAPP -> FLPR: acknowledged */
#define FLPR_MSG_HEARTBEAT 0x03U /* Bidirectional liveness counter */

/* Fixed-size message. RV32E has no atomic extension; SPSC-safe. */
struct flpr_msg {
	uint8_t type;
	uint8_t version;
	uint16_t seq;
	uint32_t data; /* heartbeat counter / epoch */
};

/* ── IPC state ──────────────────────────────────────────────── */

static struct ipc_ept ipc_ep;
static K_SEM_DEFINE(bound_sem, 0, 1);
static bool acked;
static uint32_t heartbeat_seq;
static uint32_t epoch; /* increments on each fresh boot */

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

	if (msg->type == FLPR_MSG_ACK && msg->version == FLPR_PROTOCOL_VERSION) {
		acked = true;
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

/* ── Helper: send a message ─────────────────────────────────── */

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

/* ── main ───────────────────────────────────────────────────── */

int main(void)
{
	const struct device *ipc_dev;
	int ret;

	/* Open IPC instance — DT node ipc0 from overlay. */
	ipc_dev = DEVICE_DT_GET(DT_NODELABEL(ipc0));
	if (!device_is_ready(ipc_dev)) {
		/* IPC device not ready — FLPR cannot communicate.
		 * VPR launcher already released us; nothing to do but spin.
		 */
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

	/* Send READY. Retry on -ENOMEM (buffer not yet available). */
	do {
		ret = send_msg(FLPR_MSG_READY, 0, epoch);
	} while (ret == -ENOMEM);

	/* Wait for ACK (timeout 5 s). If no ACK, CPUAPP will log the error;
	 * FLPR still sends heartbeats — CPUAPP can recover later.
	 */
	uint32_t wait_start = k_uptime_get_32();
	while (!acked && (k_uptime_get_32() - wait_start) < 5000U) {
		k_msleep(10);
	}

	/* Stage 0: handshake only (READY + ACK). No ongoing traffic.
	 * Heartbeats disabled to avoid ICMsg PBUF accumulation issue.
	 * ICMsg assertion: len_available > sizeof(rx_buffer) fires if
	 * pbuf is not drained fast enough. Stage 0 only proves boot. */
	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
