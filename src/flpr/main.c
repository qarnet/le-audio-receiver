/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLPR application (RISC-V VPR): READY/heartbeat handshake, shared PCM
 * ring transport, and fixed-point ASRC processing offloaded from cpuapp.
 *
 * Ring consumer: polls the input ring (event-driven via IPC wake with a
 * 10 ms polling fallback), passes each slot through flpr_audio_process()
 * (ASRC), and publishes the output slot.  Ring addresses resolved from
 * devicetree, not hardcoded.
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
#include <zephyr/irq.h>
#include <string.h>

#include "flpr_protocol.h"
#include "flpr_ring.h"
#include "flpr_audio_process.h"

#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
#include "acceptance.h"
#endif

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

/* The acceptance state (ring-test counters, stall flags/timer,
 * fault-hang pending, stress, diagnostic counters reported at test
 * stop) lives in src/flpr/acceptance.c; main.c invokes the acceptance
 * hooks below only under CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS.  The
 * diag_* counters that ring_process_input() used to own are likewise
 * acceptance-owned now (they are reported in the RING_TEST_STOP
 * cascade); production ring processing delegates via the hooks. */

static bool rings_initialized;

static uint32_t ring_stream_epoch; /* current ring epoch after reset */

static void ring_notify_cpuapp(uint32_t consumed);

/* ── Helpers ────────────────────────────────────────────────────── */

static int send_msg(const struct flpr_msg *msg)
{
	return ipc_service_send(&ipc_ep, msg, sizeof(*msg));
}

#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
/* Acceptance wake hook — kick the main loop's ring wake semaphore
 * (used for timed-stall expiry and the FAULT_HANG wake). */
static void flpr_acceptance_wake(void)
{
	k_sem_give(&ring_wake_sem);
}
#endif

/** Drain all pending input ring slots: run flpr_audio_process()
 *  (identity/passthrough or ASRC) on each slot, publish output.
 *  Returns number of slots consumed.
 *  Respects stall_consumer_input / stall_producer_output flags.
 *
 *  Safety: output stall/full MUST NOT drop input.  Before consuming
 *  an input slot, check that output ring has space and output is not
 *  stalled.  If output cannot accept, leave input consumer unchanged
 *  so the producer retries on next poll/wake.  No input loss. */
static uint32_t ring_process_input(void)
{
	uint32_t consumed = 0;

#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
	flpr_acceptance_note_worker_wake();

	/* One atomic snapshot per decision — consistent view. */
	uint8_t sf = flpr_acceptance_stall_flags();
#else
	uint8_t sf = 0;
#endif

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
#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
				flpr_acceptance_note_produce_full();
#endif
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
#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
			flpr_acceptance_note_empty_poll();
			flpr_acceptance_note_consume_empty();
#endif
			break;
		}
		if (ret == -ESTALE) {
			/* Stale epoch — skip, already advanced by consume_begin. */
#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
			flpr_acceptance_note_epoch_stale();
			flpr_acceptance_note_consume_stale();
#endif
			continue;
		}

#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
		flpr_acceptance_note_consume_ok();
#endif
		consumed++;

		/* ── Audio processing ───────────────────────────────────
		 * Measure processor cycles with k_cycle_get_32(),
		 * call flpr_audio_process(), store delta + status
		 * in output metadata.  Failed processing increments
		 * diagnostic and emits error output.
		 *
		 * CRC verification moved into processor for test mode. */
		{
			uint32_t out_idx;
			uint32_t t0;
			uint32_t t1;
			int proc_ret;

			/* Allocate output slot BEFORE calling processor. */
			ret = flpr_ring_produce_begin(RING_OUTPUT_BASE, &out_idx);
			if (ret != 0) {
#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
				flpr_acceptance_note_produce_full();
#endif
				break; /* leave input index unchanged */
			}

			uint8_t *out_slot = flpr_ring_slot_base(RING_OUTPUT_BASE, out_idx);
			struct flpr_ring_slot_meta *out_meta =
				(struct flpr_ring_slot_meta *)out_slot;

			/* Verify CRC over input if test mode and CRC was set.
			 * Read payload direct from ring — zero copy. */
#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
			if (flpr_acceptance_test_active() && meta->crc32 != 0) {
				uint16_t vf = meta->valid_frames;
				if (vf > FLPR_RING_PAYLOAD_CAPACITY_FRAMES) {
					vf = FLPR_RING_PAYLOAD_CAPACITY_FRAMES;
				}
				uint32_t computed = flpr_ring_crc32(
					flpr_ring_slot_payload(slot_base), (size_t)vf * 4U);
				if (computed != meta->crc32) {
					flpr_acceptance_note_crc_error();
				}
			}
#endif

			t0 = k_cycle_get_32();
			proc_ret = flpr_audio_process(meta, flpr_ring_slot_payload(slot_base),
						      FLPR_RING_PAYLOAD_CAPACITY_BYTES, out_meta,
						      flpr_ring_slot_payload(out_slot),
						      FLPR_RING_PAYLOAD_CAPACITY_BYTES);
			t1 = k_cycle_get_32();

			flpr_ring_slot_set_processing(out_meta, t1 - t0,
						      proc_ret == 0 ? 0 : proc_ret);
			/* processor already constructs error metadata
			 * (sequence/epoch/ppm/timestamp/flags/status);
			 * set_processing is the final metadata mutation
			 * before publish — no field rebuild here. */

#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
			flpr_acceptance_note_produce_ok();
#endif

			/* Publish output slot. */
			flpr_ring_produce_commit(RING_OUTPUT_BASE, out_idx);
		}

		/* Release input slot (only after successful output publish). */
		flpr_ring_consume_done(RING_INPUT_BASE);

#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
		flpr_acceptance_note_block_processed();
#endif
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

#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
	/* Acceptance state (stall timer/flags, test + diagnostic
	 * counters) is reset by the acceptance module. */
	flpr_acceptance_on_ring_reset();
#endif

	return 0;
}

/** Send RING_CONSUMER notification + diagnostic counters to CPUAPP.
 *  Called from IPC callback and polling path when data was consumed.
 *
 *  The data field carries the current ring_stream_epoch (non-zero) so
 *  stale notifications from an expired epoch are rejected by cpuapp.
 *  Consumed count is not needed for wake semantics; diagnostic block
 *  count retained in seq. */
static void ring_notify_cpuapp(uint32_t consumed)
{
	if (consumed == 0) {
		/* Nothing was consumed — don't send spurious notification. */
		return;
	}

	struct flpr_msg notify = {
		.type = FLPR_MSG_RING_CONSUMER,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = (uint16_t)(consumed & 0xFFFFU),
		.data = ring_stream_epoch,
	};
	(void)send_msg(&notify);
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

		/* ── Ring control ─────────────────────────────────────── */

	case FLPR_MSG_RING_RESET: {
		uint32_t epoch = msg->data;
		int ret = ring_reset_with_epoch(epoch);
		/* ACK echoes the request sequence token so the cpuapp
		 * side can correlate by sequence (late/stale ACKs are
		 * rejected there). */
		struct flpr_msg ack = flpr_control_ack_make(msg, FLPR_MSG_RING_RESET_ACK,
							    (ret == 0) ? epoch : 0U);
		(void)send_msg(&ack);
		break;
	}

	case FLPR_MSG_RING_PRODUCER:
		/* CPUAPP published input data — signal main loop to consume.
		 * Do NOT call ring_process_input() here (the IPC callback may
		 * race with the main loop).  The semaphore wake is immediate. */
#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
		flpr_acceptance_note_notify_rcv();
#endif
		k_sem_give(&ring_wake_sem);
		break;

#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
	/* ── Acceptance messages ──────────────────────────────────
	 * RING_TEST_START/STOP (report cascade), RING_STALL (timer +
	 * ACK echo), STRESS_PING (PONG echo), and FAULT_HANG
	 * (ACK-before-spin) are owned by src/flpr/acceptance.c and
	 * dispatched through its handler.  Config-off builds have no
	 * acceptance symbol and these messages fall to the default
	 * err_unknown branch below (current unknown/error behavior). */
	default:
		if (!flpr_acceptance_handle_msg(msg)) {
			cpuapp.err_unknown++;
		}
		break;
#else
	default:
		cpuapp.err_unknown++;
		break;
#endif
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

#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
	/* Initialize the acceptance handlers with the injected
	 * transport (send via this module's IPC endpoint, wake via the
	 * ring wake semaphore). */
	{
		static const struct flpr_acceptance_deps acc_deps = {
			.send = send_msg,
			.wake = flpr_acceptance_wake,
		};

		flpr_acceptance_init(&acc_deps);
	}
#endif

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

		/* Check the fault-hang flag.
		 * ACK was already sent from IPC callback before setting this flag.
		 * Disable all interrupts and spin forever — halts ring processing,
		 * heartbeat transmission, and all further IPC activity.
		 * Config-off builds have no acceptance state and never spin. */
#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)
		if (flpr_acceptance_hang_pending()) {
			/* Brief busy-wait for ACK delivery to complete. */
			k_busy_wait(1000);
			irq_lock();
			while (1) {
				/* Nothing — spin forever. */
			}
		}
#endif

		/* Process ALL pending input ring slots.
		 * This is the ONLY place ring_process_input() runs —
		 * the IPC callback only signals, never processes. */
		uint32_t consumed = ring_process_input();
		ring_notify_cpuapp(consumed);
	}

	return 0;
}
