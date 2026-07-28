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
#include <zephyr/sys/atomic.h>
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

/* Wake semaphore: IPC callback gives it, main loop takes it.
 * This eliminates the race between the callback's ring_process_input()
 * and the main loop's.  Timeout provides the polling fallback. */
static K_SEM_DEFINE(ring_wake_sem, 0, K_SEM_MAX_LIMIT);

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

/* Diagnostic counters (no lock — single-threaded FLPR). */
static uint32_t diag_notify_rcv;    /* RING_PRODUCER messages received */
static uint32_t diag_worker_wake;   /* ring_process_input() call count */
static uint32_t diag_consume_ok;    /* slots successfully consumed */
static uint32_t diag_consume_empty; /* consumer found ring empty */
static uint32_t diag_consume_stale; /* consumer found stale epoch */
static uint32_t diag_produce_ok;    /* output slots produced */
static uint32_t diag_produce_full;  /* output ring was full */

static bool rings_initialized;

static uint32_t ring_stream_epoch; /* current ring epoch after reset */

/* Stall control: atomic bitmask (replaces two plain bools for Stage 2).
 * Written by IPC RING_STALL handler, read by poll + callback.
 * Bits: FLPR_STALL_CONSUMER_INPUT (0x01), FLPR_STALL_PRODUCER_OUTPUT (0x02).
 *
 * Timed stall (duration > 0): timer expiry atomically clears all bits
 * and kicks ring_wake_sem so queued input drains even without a later
 * producer notification. */
static atomic_t stall_flags = ATOMIC_INIT(0);

static void stall_timer_expiry(struct k_timer *timer);

/* One-shot timer for timed-stall auto-clear. */
static K_TIMER_DEFINE(stall_timer, stall_timer_expiry, NULL);

/* Diagnostics (no lock — single-threaded FLPR). */
static uint32_t diag_timed_stall_start_count;
static uint32_t diag_timed_stall_expiry_count;

/* ── Helpers ────────────────────────────────────────────────────── */

static int send_msg(const struct flpr_msg *msg)
{
	return ipc_service_send(&ipc_ep, msg, sizeof(*msg));
}

/* Static buffer for ring_process_input — too large for FLPR main
 * thread stack (1924 bytes vs 1024-byte default).  Single-writer:
 * only one thread (FLPR main or IPC callback) runs at a time. */
static uint8_t recv_payload[FLPR_RING_PAYLOAD_CAPACITY_BYTES];

/** Drain all pending input ring slots: verify CRC over valid bytes,
 *  copy bit-exact to output, publish.  Returns number of slots consumed.
 *  Respects stall_consumer_input / stall_producer_output flags.
 *
 *  Safety: output stall/full MUST NOT drop input.  Before consuming
 *  an input slot, check that output ring has space and output is not
 *  stalled.  If output cannot accept, leave input consumer unchanged
 *  so the producer retries on next poll/wake.  No input loss. */
static uint32_t ring_process_input(void)
{
	uint32_t consumed = 0;

	diag_worker_wake++;

	/* One atomic snapshot per decision — consistent view. */
	atomic_val_t sf = atomic_get(&stall_flags);

	while (1) {
		/* Respect consumer-input stall: stop draining input. */
		if (sf & FLPR_STALL_CONSUMER_INPUT) {
			break;
		}

		/* Check output capacity BEFORE consuming input.
		 * If output ring is full or stalled, stop here —
		 * do NOT consume input (no loss). */
		if (sf & FLPR_STALL_PRODUCER_OUTPUT) {
			break;
		}
		{
			struct flpr_ring_header *hdr_out =
				(struct flpr_ring_header *)RING_OUTPUT_BASE;
			if (flpr_ring_space(hdr_out->producer_idx, hdr_out->consumer_idx) == 0) {
				/* Output ring full — backpressure. Producer must
				 * drain before we can forward more input. */
				diag_produce_full++;
				break;
			}
		}

		uint8_t *slot_base;
		struct flpr_ring_slot_meta *meta;
		int ret;

		ret = flpr_ring_consume_begin(RING_INPUT_BASE, ring_stream_epoch, &slot_base,
					      &meta);
		if (ret == -ENOENT) {
			/* Empty — stop draining. */
			ring_test_empty_polls++;
			diag_consume_empty++;
			break;
		}
		if (ret == -ESTALE) {
			/* Stale epoch — skip, already advanced by consume_begin. */
			ring_test_epoch_stale++;
			diag_consume_stale++;
			continue;
		}

		diag_consume_ok++;
		consumed++;

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

		/* Output-capacity check was done above — this produce_begin
		 * MUST succeed (space was reserved before consuming input). */
		uint32_t out_idx;
		ret = flpr_ring_produce_begin(RING_OUTPUT_BASE, &out_idx);
		if (ret != 0) {
			/* Should never happen: we checked space before consuming.
			 * If it does, keep input unconsumed by NOT calling
			 * consume_done.  The slot stays pending; next poll
			 * re-processes it.  Count the anomaly. */
			ring_test_output_full++;
			diag_produce_full++;
			break; /* leave input index unchanged */
		}

		diag_produce_ok++;

		/* Copy metadata into output slot. */
		uint8_t *out_slot = flpr_ring_slot_base(RING_OUTPUT_BASE, out_idx);
		struct flpr_ring_slot_meta *out_meta = (struct flpr_ring_slot_meta *)out_slot;
		memcpy(out_meta, meta, sizeof(*meta));
		out_meta->flags |= FLPR_SLOT_FLAG_VALID;

		/* Copy valid payload bytes bit-exact; zero remainder. */
		memset(flpr_ring_slot_payload(out_slot), 0, FLPR_RING_PAYLOAD_CAPACITY_BYTES);
		memcpy(flpr_ring_slot_payload(out_slot), recv_payload, valid_bytes);

		/* Forward original CRC. */
		out_meta->crc32 = meta->crc32;
		out_meta->valid_frames = valid_frames;

		/* Publish output slot. */
		flpr_ring_produce_commit(RING_OUTPUT_BASE, out_idx);

		/* Release input slot (only after successful output publish). */
		flpr_ring_consume_done(RING_INPUT_BASE);

		ring_test_block_count++;
	}

	return consumed;
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

	/* Clear any active stall on ring reset. */
	k_timer_stop(&stall_timer);
	atomic_clear(&stall_flags);
	diag_timed_stall_start_count = 0;
	diag_timed_stall_expiry_count = 0;

	/* Reset test counters on ring reset. */
	ring_test_active = false;
	ring_test_block_count = 0;
	ring_test_crc_errors = 0;
	ring_test_seq_gaps = 0;
	ring_test_epoch_stale = 0;
	ring_test_empty_polls = 0;
	ring_test_output_full = 0;

	/* Reset diagnostic counters. */
	diag_notify_rcv = 0;
	diag_worker_wake = 0;
	diag_consume_ok = 0;
	diag_consume_empty = 0;
	diag_consume_stale = 0;
	diag_produce_ok = 0;
	diag_produce_full = 0;

	return 0;
}

/** Send RING_CONSUMER notification + diagnostic counters to CPUAPP.
 *  Called from IPC callback and polling path when data was consumed. */
static void ring_notify_cpuapp(uint32_t consumed)
{
	if (consumed == 0) {
		/* Nothing was consumed — don't send spurious notification. */
		return;
	}

	struct flpr_msg notify = {
		.type = FLPR_MSG_RING_CONSUMER,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = (uint16_t)(ring_test_block_count & 0xFFFFU),
		.data = (uint32_t)consumed, /* slots consumed this wake */
	};
	(void)send_msg(&notify);
}

/* ── Stall timer expiry callback ────────────────────────────────────
 * Only fires for timed stalls (duration > 0).  Atomically clears
 * all stall bits and kicks ring_wake_sem so queued input drains
 * even without a later producer notification.
 * ISR context: no logging, no blocking calls. */
static void stall_timer_expiry(struct k_timer *timer)
{
	(void)timer;
	atomic_clear(&stall_flags);
	diag_timed_stall_expiry_count++;
	k_sem_give(&ring_wake_sem);
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
		/* Send multi-report test results.
		 * Subtype encoded in seq high byte:
		 *   0x00: block_count (lo 16-bit) + crc_errors (data)
		 *   0xD1: consume_ok (full 32-bit in data)
		 *   0xD2: produce_ok (full 32-bit in data)
		 *   0xD3: notify_rcv(lo 8) + worker_wake(hi 8 of data)
		 *          produce_full(lo 16) in seq */

		/* Report 1: block_count (32-bit) + crc_errors.
		 *   seq lo 16 = crc_errors, data = block_count. */
		{
			struct flpr_msg r0 = {
				.type = FLPR_MSG_RING_TEST_REPORT,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = (uint16_t)(ring_test_crc_errors & 0xFFFFU),
				.data = ring_test_block_count,
			};
			(void)send_msg(&r0);
		}
		/* Report 2: consume_ok (full 32-bit). */
		{
			struct flpr_msg r1 = {
				.type = FLPR_MSG_RING_TEST_REPORT,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 0xD100U,
				.data = diag_consume_ok,
			};
			(void)send_msg(&r1);
		}
		/* Report 3: produce_ok (full 32-bit). */
		{
			struct flpr_msg r2 = {
				.type = FLPR_MSG_RING_TEST_REPORT,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 0xD200U,
				.data = diag_produce_ok,
			};
			(void)send_msg(&r2);
		}
		/* Report 4: misc diagnostic counters. */
		{
			struct flpr_msg r3 = {
				.type = FLPR_MSG_RING_TEST_REPORT,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = (uint16_t)(0xD300U |
						  (diag_notify_rcv > 255 ? 255 : diag_notify_rcv)),
				.data = (diag_worker_wake & 0xFFFFU) |
					((diag_produce_full & 0xFFFFU) << 16),
			};
			(void)send_msg(&r3);
		}
		/* Report 5: cons_empty + cons_stale (full 32-bit each). */
		{
			struct flpr_msg r4 = {
				.type = FLPR_MSG_RING_TEST_REPORT,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = (uint16_t)(0xD400U),
				.data = (diag_consume_empty & 0xFFFFU) |
					((diag_consume_stale & 0xFFFFU) << 16),
			};
			(void)send_msg(&r4);
		}
		break;
	}

	case FLPR_MSG_RING_PRODUCER:
		/* CPUAPP published input data — signal main loop to consume.
		 * Do NOT call ring_process_input() here (the IPC callback may
		 * race with the main loop).  The semaphore wake is immediate. */
		diag_notify_rcv++;
		k_sem_give(&ring_wake_sem);
		break;

	case FLPR_MSG_RING_STALL: {
		/* Stage 2 packed stall: data[7:0]=mask, data[31:8]=duration_ms.
		 * Duration zero = persistent (stops any prior timer). */
		uint8_t bits = FLPR_STALL_MASK(msg->data);
		uint32_t duration_ms = FLPR_STALL_DURATION(msg->data);

		/* Stop any prior timed stall. */
		k_timer_stop(&stall_timer);

		/* Atomically apply the new mask. */
		atomic_set(&stall_flags, (atomic_val_t)bits);

		if (duration_ms > 0) {
			/* Timed stall: start one-shot timer. */
			k_timer_start(&stall_timer, K_MSEC(duration_ms), K_NO_WAIT);
			diag_timed_stall_start_count++;
		}

		/* ACK with packed value (exact echo). */
		struct flpr_msg ack = {
			.type = FLPR_MSG_RING_STALL_ACK,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = 0,
			.data = msg->data, /* echo packed mask+duration */
		};
		(void)send_msg(&ack);
		break;
	}

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

	/* 1 Hz heartbeat with ring-polling that is now event-driven.
	 * The IPC callback gives ring_wake_sem on RING_PRODUCER.
	 * Main loop takes it (with 10 ms timeout for polling fallback). */
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

		/* Wait for ring event (immediate via IPC callback) or
		 * timeout (10 ms polling fallback). */
		k_sem_take(&ring_wake_sem, K_MSEC(10));

		/* Process ALL pending input ring slots.
		 * This is the ONLY place ring_process_input() runs —
		 * the IPC callback only signals, never processes. */
		uint32_t consumed = ring_process_input();
		ring_notify_cpuapp(consumed);
	}

	return 0;
}
