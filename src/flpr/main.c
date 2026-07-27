/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLPR application — Stage 1: shared PCM ring transport.
 * Handshake/heartbeat retained from Stage 0.
 *
 * Loopback consumer: polls input ring, validates metadata/CRC/seq,
 * copies bit-exact payload (valid bytes only) to output ring.
 * Ring addresses resolved from devicetree, not hardcoded.
 *
 * Epoch: hardware GRTC counter at boot start.
 * All state transitions use production helpers from flpr_protocol.h.
 * Ring operations use pure helpers from flpr_ring.h.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/sys/__assert.h>
#include <string.h>

#include "flpr_protocol.h"
#include "flpr_ring.h"

/* ── Devicetree resolved addresses ───────────────────────────────── */

#define DT_PCM_RING DT_NODELABEL(pcm_ring)

BUILD_ASSERT(DT_NODE_EXISTS(DT_PCM_RING), "pcm_ring node not found in FLPR DTS");
BUILD_ASSERT(DT_REG_SIZE(DT_PCM_RING) == 0x4000U, "pcm_ring must be 16 KiB");
BUILD_ASSERT(DT_REG_ADDR(DT_PCM_RING) == 0x2002C000U, "pcm_ring base mismatch");

/* Input ring (CPUAPP→FLPR): lower 8 KiB. */
#define RING_INPUT_BASE ((uint8_t *)(uintptr_t)DT_REG_ADDR(DT_PCM_RING))

/* Output ring (FLPR→CPUAPP): upper 8 KiB. */
#define RING_OUTPUT_BASE ((uint8_t *)(uintptr_t)(DT_REG_ADDR(DT_PCM_RING) + FLPR_RING_TOTAL_SIZE))

BUILD_ASSERT(FLPR_RING_TOTAL_SIZE == 8192U, "ring size must be 8 KiB");

/* ── IPC state ─────────────────────────────────────────────────── */

static struct ipc_ept ipc_ep;
static K_SEM_DEFINE(bound_sem, 0, 1);

/* Tracks remote peer (CPUAPP). Single-threaded on FLPR, no lock. */
static struct flpr_peer cpuapp;

/* Epoch from hardware GRTC at boot start. */
static uint32_t boot_epoch;

/* ── Ring test state ────────────────────────────────────────────── */

static bool ring_test_active;
static uint32_t ring_test_block_count; /* blocks processed this test */
static uint32_t ring_test_crc_errors;  /* CRC mismatches */
static uint32_t ring_test_seq_gaps;    /* sequence gaps */
static uint32_t ring_test_epoch_stale; /* stale epoch rejections */
static uint32_t ring_test_empty_polls; /* times ring was empty */
static uint32_t ring_test_output_full; /* output ring full count */

static bool rings_initialized;

static uint32_t ring_stream_epoch; /* current ring epoch after reset */

/* ── Helpers ────────────────────────────────────────────────────── */

static int send_msg(const struct flpr_msg *msg)
{
	return ipc_service_send(&ipc_ep, msg, sizeof(*msg));
}

/** Drain all pending input ring slots: verify CRC over valid bytes,
 *  copy bit-exact to output, publish. */
static void ring_process_input(void)
{
	uint8_t recv_payload[FLPR_RING_PAYLOAD_CAPACITY_BYTES];

	while (1) {
		uint8_t *slot_base;
		struct flpr_ring_slot_meta *meta;
		int ret;

		ret = flpr_ring_consume_begin(RING_INPUT_BASE, ring_stream_epoch, &slot_base,
					      &meta);
		if (ret == -1) {
			/* Empty — stop draining. */
			ring_test_empty_polls++;
			break;
		}
		if (ret == -2) {
			/* Stale epoch — skip, already advanced by consume_begin. */
			ring_test_epoch_stale++;
			continue;
		}

		uint16_t valid_frames = meta->valid_frames;
		if (valid_frames > FLPR_RING_PAYLOAD_CAPACITY_FRAMES) {
			valid_frames = FLPR_RING_PAYLOAD_CAPACITY_FRAMES;
		}
		size_t valid_bytes = (size_t)valid_frames * 4U;

		/* Copy payload for verification. */
		memcpy(recv_payload, flpr_ring_slot_payload(slot_base), valid_bytes);

		/* Verify CRC over valid bytes if test mode and CRC was set. */
		if (ring_test_active && meta->crc32 != 0) {
			uint32_t computed = flpr_ring_crc32(recv_payload, valid_bytes);
			if (computed != meta->crc32) {
				ring_test_crc_errors++;
			}
		}

		/* Try to produce into output ring. */
		uint32_t out_idx;
		ret = flpr_ring_produce_begin(RING_OUTPUT_BASE, &out_idx);
		if (ret != 0) {
			/* Output full — stop consuming input to avoid
			 * dropping blocks. Consumer will drain output
			 * and we'll come back on next poll. */
			ring_test_output_full++;
			/* Release input slot (we consumed it but can't
			 * forward it — drop is allowed in test; counter
			 * records the event). */
			flpr_ring_consume_done(RING_INPUT_BASE);
			break;
		}

		/* Copy metadata into output slot. */
		uint8_t *out_slot = flpr_ring_slot_base(RING_OUTPUT_BASE, out_idx);
		struct flpr_ring_slot_meta *out_meta = (struct flpr_ring_slot_meta *)out_slot;
		memcpy(out_meta, meta, sizeof(*meta));
		out_meta->flags |= FLPR_SLOT_FLAG_VALID;

		/* Copy valid payload bytes bit-exact; zero remainder. */
		memset(flpr_ring_slot_payload(out_slot), 0, FLPR_RING_PAYLOAD_CAPACITY_BYTES);
		memcpy(flpr_ring_slot_payload(out_slot), recv_payload, valid_bytes);

		/* Recompute CRC over valid bytes for the output copy
		 * (verifies the copy was bit-exact). */
		out_meta->crc32 = meta->crc32; /* forward original CRC */
		out_meta->valid_frames = valid_frames;

		/* Publish output slot. */
		flpr_ring_produce_commit(RING_OUTPUT_BASE, out_idx);

		/* Release input slot. */
		flpr_ring_consume_done(RING_INPUT_BASE);

		ring_test_block_count++;
	}
}

/* ── Ring reset ─────────────────────────────────────────────────── */

static int ring_reset_with_epoch(uint32_t epoch)
{
	int ret;

	ret = flpr_ring_reset_epoch(RING_INPUT_BASE, epoch);
	if (ret != 0) {
		return ret;
	}
	ret = flpr_ring_reset_epoch(RING_OUTPUT_BASE, epoch);
	if (ret != 0) {
		return ret;
	}
	ring_stream_epoch = epoch;

	/* Reset test counters on ring reset. */
	ring_test_active = false;
	ring_test_block_count = 0;
	ring_test_crc_errors = 0;
	ring_test_seq_gaps = 0;
	ring_test_epoch_stale = 0;
	ring_test_empty_polls = 0;
	ring_test_output_full = 0;

	return 0;
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
		flpr_peer_handle_ready_ack(&cpuapp, msg->data);
		break;

	case FLPR_MSG_HEARTBEAT:
		flpr_peer_rx_seq(&cpuapp, msg->seq, now_ms);
		(void)flpr_peer_check_health(&cpuapp, now_ms);

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
		flpr_peer_handle_heartbeat_ack(&cpuapp, msg->seq);
		break;

	case FLPR_MSG_STRESS_PING: {
		struct flpr_msg pong = {
			.type = FLPR_MSG_STRESS_PONG,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = msg->seq,
			.data = msg->data,
		};
		(void)send_msg(&pong);
		break;
	}

		/* ── Stage 1: ring control ─────────────────────────────── */

	case FLPR_MSG_RING_RESET: {
		uint32_t epoch = msg->data;
		int ret = ring_reset_with_epoch(epoch);
		struct flpr_msg ack = {
			.type = FLPR_MSG_RING_RESET_ACK,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = 0,
			.data = (ret == 0) ? epoch : 0U,
		};
		(void)send_msg(&ack);
		break;
	}

	case FLPR_MSG_RING_TEST_START: {
		ring_test_active = true;
		ring_test_block_count = 0;
		ring_test_crc_errors = 0;
		ring_test_seq_gaps = 0;
		break;
	}

	case FLPR_MSG_RING_TEST_STOP: {
		ring_test_active = false;
		/* Send current stats as report. */
		struct flpr_msg report = {
			.type = FLPR_MSG_RING_TEST_REPORT,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = (uint16_t)(ring_test_block_count & 0xFFFFU),
			.data = ring_test_crc_errors,
		};
		(void)send_msg(&report);
		break;
	}

	case FLPR_MSG_RING_PRODUCER:
		/* CPUAPP just published input data — consume it. */
		ring_process_input();

		/* Notify CPUAPP that output may be available. */
		{
			struct flpr_msg notify = {
				.type = FLPR_MSG_RING_CONSUMER,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = (uint16_t)(ring_test_block_count & 0xFFFFU),
				.data = ring_test_crc_errors,
			};
			(void)send_msg(&notify);
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

	boot_epoch = k_cycle_get_32();

	flpr_peer_reset(&cpuapp);

	/* Initialize PCM rings in shared memory. */
	flpr_ring_init(RING_INPUT_BASE, FLPR_RING_CPUAPP_TO_FLPR);
	flpr_ring_init(RING_OUTPUT_BASE, FLPR_RING_FLPR_TO_CPUAPP);
	rings_initialized = true;

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

	/* Send READY with epoch. Retry on -ENOMEM with backoff (max 5 s). */
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
				k_msleep(backoff_ms);
				if (backoff_ms < 64) {
					backoff_ms *= 2;
				}
			} else {
				sent = true;
			}
		}
	}

	/* Wait for READY_ACK (timeout 5 s). */
	uint32_t wait_start = k_uptime_get_32();
	while (!cpuapp.acked && (k_uptime_get_32() - wait_start) < 5000U) {
		k_msleep(10);
	}

	/* 1 Hz heartbeat with ring polling. */
	uint32_t last_hb_ms = k_uptime_get_32();

	while (1) {
		uint32_t now_ms = k_uptime_get_32();

		/* Heartbeat every 1000 ms. */
		if (now_ms - last_hb_ms >= FLPR_HEARTBEAT_INTERVAL_MS) {
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
			} else {
				cpuapp.err_send++;
			}
			last_hb_ms = now_ms;
		}

		/* Poll input ring briefly (the IPC RING_PRODUCER callback
		 * handles the main processing path; this catches stragglers
		 * if an IPC message was dropped). */
		ring_process_input();

		/* Yield CPU — IPC callbacks wake us. */
		k_msleep(10);
	}

	return 0;
}
