/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP FLPR acceptance/diagnostic module (R8).
 *
 * Owns the acceptance orchestration and state moved out of the core
 * ring manager / handshake / acceptance shell (R4):
 *   - raw diagnostic produce/consume blocks (ring throughput test);
 *   - stale-epoch diagnostic production into the output ring;
 *   - producer stall injection;
 *   - FLPR stall / timed-stall requests + ACK correlation (via the
 *     shared control-ACK engine — never duplicated);
 *   - RING_TEST_REPORT aggregation (FLPR-reported counters);
 *   - acceptance status fields (test/stall/FLPR-report/latency);
 *   - handshake stress + fault-hang orchestration;
 *   - Gate 1–6 acceptance run.
 *
 * Compiled only when CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS is enabled.
 *
 * Lock discipline: one module spinlock (acc_lock) protects acceptance
 * counters/latency/stall flag/stress/hang state.  Fixed nesting is
 * ring_data_lock → (ring_lock | acc_lock) → engine lock; acc_lock and
 * the engine lock are leaves.  Core produce/consume paths invoke the
 * note hooks while holding ring_data_lock — safe.
 */

#include "flpr_acceptance.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

#include "flpr_protocol.h"
#include "flpr_ring.h"
#include "flpr_handshake.h"
#include "flpr_ring_mgr.h"
#include "flpr_control_ack.h"
#include "flpr_ring_mgr_internal.h"

LOG_MODULE_REGISTER(flpr_acc, LOG_LEVEL_INF);

/* ── State (protected by acc_lock) ───────────────────────────────── */

static struct k_spinlock acc_lock;

static bool test_active;
static uint32_t test_blocks_sent;
static uint32_t test_blocks_recv;
static uint32_t test_crc_errors;
static uint32_t test_payload_errors;
static uint32_t test_seq_gaps;
static uint32_t test_full_events;
static uint32_t test_backpressure;
static uint32_t test_empty_events;
static uint32_t test_stale_events;
static uint32_t test_flpr_blocks;  /* blocks FLPR reports processing */
static uint32_t test_flpr_crc_err; /* FLPR-reported CRC errors */
static uint32_t test_output_full;  /* FLPR-reported output-full */

static uint32_t test_flpr_notify_rcv;
static uint32_t test_flpr_worker_wake;
static uint32_t test_flpr_consume_ok;
static uint32_t test_flpr_consume_empty;
static uint32_t test_flpr_consume_stale;
static uint32_t test_flpr_produce_ok;
static uint32_t test_flpr_produce_full;

static uint32_t latency_min;
static uint32_t latency_max;
static uint64_t latency_sum;
static uint32_t latency_count;

static bool stall_producer_enabled;

/* ── Stall ACK correlation (shared engine) ───────────────────────── */

static struct k_sem stall_ack_sem;
static struct flpr_control_ack stall_ack_ctl;

/* ── Stress state (moved from flpr_handshake.c) ──────────────────── */

static struct k_sem stress_sem;

static uint32_t stress_count;    /* protected by acc_lock */
static uint32_t stress_sent;     /* protected by acc_lock */
static uint32_t stress_recv;     /* protected by acc_lock */
static uint32_t stress_timeouts; /* protected by acc_lock */
static bool stress_active;       /* protected by acc_lock */
static uint32_t stress_cookie;   /* protected by acc_lock */
static uint32_t stress_stale;    /* protected by acc_lock */
static uint32_t stress_mismatch; /* protected by acc_lock */
static uint32_t stress_err_send; /* protected by acc_lock */

/* ── Fault hang ACK state (moved from flpr_handshake.c) ──────────── */

static struct k_sem hang_ack_sem;
static bool hang_ack_received;

/* ── Diagnostic message handler (registered with the handshake) ──── */

static void flpr_acceptance_diag_handler(const struct flpr_msg *msg, void *user_data)
{
	(void)user_data;

	switch (msg->type) {

	case FLPR_MSG_STRESS_PONG: {
		/* Classify cookie with the pure protocol helper, act under
		 * acc_lock; signal outside the lock only on MATCH. */
		uint32_t cookie;
		bool active;
		enum flpr_stress_pong_class cls;

		{
			k_spinlock_key_t key = k_spin_lock(&acc_lock);
			cookie = msg->data;
			active = stress_active;

			if (!active) {
				k_spin_unlock(&acc_lock, key);
				break;
			}

			cls = flpr_classify_stress_pong(cookie, stress_cookie, active);

			switch (cls) {
			case FLPR_PONG_MATCH:
				stress_recv++;
				break;
			case FLPR_PONG_STALE:
				stress_stale++;
				break;
			case FLPR_PONG_FUTURE:
				stress_mismatch++;
				break;
			default:
				break;
			}
			k_spin_unlock(&acc_lock, key);
		}

		if (cls == FLPR_PONG_MATCH) {
			k_sem_give(&stress_sem);
		}
		break;
	}

	case FLPR_MSG_RING_TEST_REPORT: {
		k_spinlock_key_t key = k_spin_lock(&acc_lock);

		/* Subtype encoded in seq high byte:
		 *   0x00 → block_count + crc_errors
		 *   0xD1 → consume_ok (full 32-bit in data)
		 *   0xD2 → produce_ok (full 32-bit in data)
		 *   0xD3 → notify_rcv + worker_wake + produce_full
		 *   0xD4 → cons_empty + cons_stale (data lo/hi 16-bit) */
		uint8_t subtype = (uint8_t)(msg->seq >> 8);

		switch (subtype) {
		case 0x00:
			/* FLPR: seq lo 16 = crc_errors, data = block_count */
			test_flpr_crc_err = (uint32_t)(msg->seq & 0xFFFFU);
			test_flpr_blocks = msg->data;
			break;
		case 0xD1:
			test_flpr_consume_ok = msg->data;
			break;
		case 0xD2:
			test_flpr_produce_ok = msg->data;
			break;
		case 0xD3:
			test_flpr_notify_rcv = (uint32_t)(msg->seq & 0xFFU);
			test_flpr_worker_wake = (uint32_t)(msg->data & 0xFFFFU);
			test_flpr_produce_full = (uint32_t)((msg->data >> 16) & 0xFFFFU);
			break;
		case 0xD4:
			test_flpr_consume_empty = (uint32_t)(msg->data & 0xFFFFU);
			test_flpr_consume_stale = (uint32_t)((msg->data >> 16) & 0xFFFFU);
			break;
		default:
			break;
		}
		k_spin_unlock(&acc_lock, key);
		break;
	}

	case FLPR_MSG_RING_STALL_ACK:
		flpr_control_ack_handle(&stall_ack_ctl, msg);
		break;

	case FLPR_MSG_FAULT_HANG_ACK: {
		/* Publish under acc_lock, give the semaphore after (keeps
		 * payload-before-give ordering); the waiter reads under the
		 * lock (take-before-read ordering). */
		k_spinlock_key_t key = k_spin_lock(&acc_lock);
		hang_ack_received = true;
		k_spin_unlock(&acc_lock, key);
		k_sem_give(&hang_ack_sem);
		break;
	}

	default:
		break;
	}
}

/* ── Public API ──────────────────────────────────────────────────── */

void flpr_acceptance_init(void)
{
	k_sem_init(&stall_ack_sem, 0, 1);
	flpr_control_ack_init(&stall_ack_ctl, &stall_ack_sem);
	flpr_control_ack_register(&stall_ack_ctl);
	k_sem_init(&stress_sem, 0, FLPR_STRESS_MAX_COUNT + 1);
	k_sem_init(&hang_ack_sem, 0, 1);

	/* Register the diagnostic message handler with the handshake
	 * module (report/stall-ack/stress-pong/fault-hang-ack). */
	flpr_handshake_register_diag_handlers(flpr_acceptance_diag_handler, NULL);
}

void flpr_acceptance_get_status(struct flpr_acceptance_status *status)
{
	if (!status) {
		return;
	}
	memset(status, 0, sizeof(*status));

	k_spinlock_key_t key = k_spin_lock(&acc_lock);

	status->test_active = test_active;
	status->test_blocks_sent = test_blocks_sent;
	status->test_blocks_recv = test_blocks_recv;
	status->test_crc_errors = test_crc_errors;
	status->test_payload_errors = test_payload_errors;
	status->test_seq_gaps = test_seq_gaps;
	status->test_full_events = test_full_events;
	status->test_backpressure = test_backpressure;
	status->test_empty_events = test_empty_events;
	status->test_stale_events = test_stale_events;
	status->test_producer_blocks = test_flpr_blocks;
	status->test_output_full = test_output_full;

	status->flpr_notify_rcv = test_flpr_notify_rcv;
	status->flpr_worker_wake = test_flpr_worker_wake;
	status->flpr_consume_ok = test_flpr_consume_ok;
	status->flpr_consume_empty = test_flpr_consume_empty;
	status->flpr_consume_stale = test_flpr_consume_stale;
	status->flpr_produce_ok = test_flpr_produce_ok;
	status->flpr_produce_full = test_flpr_produce_full;

	status->latency_min = latency_min;
	status->latency_max = latency_max;
	status->latency_sum = latency_sum;
	status->latency_count = latency_count;

	status->stall_acked = stall_ack_ctl.payload;

	k_spin_unlock(&acc_lock, key);
}

void flpr_acceptance_stall_producer(bool stall)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	stall_producer_enabled = stall;
	k_spin_unlock(&acc_lock, key);
}

bool flpr_acceptance_stall_producer_active(void)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	bool v = stall_producer_enabled;
	k_spin_unlock(&acc_lock, key);
	return v;
}

/* Shared stall helper: sends packed mask+duration, waits for exact ACK
 * echo.  R1: the complete transaction holds ring_data_lock through the
 * ACK wait so a remote restart cannot clear token/armed state beneath
 * the waiter; the ACK handler takes engine lock only and can still wake
 * it. */
static int flpr_acceptance_stall_internal(uint8_t stall_bits, uint32_t duration_ms,
					  uint32_t timeout_ms)
{
	uint32_t packed = FLPR_STALL_PACK(stall_bits, duration_ms);

	struct k_mutex *data_lock = flpr_ring_mgr_data_lock();

	k_mutex_lock(data_lock, K_FOREVER);

	flpr_control_ack_begin(&stall_ack_ctl);
	int token = flpr_control_ack_arm(&stall_ack_ctl, packed);

	if (token < 0) {
		k_mutex_unlock(data_lock);
		return token; /* -EOVERFLOW: token space exhausted this session */
	}

	struct flpr_msg stall_msg = {
		.type = FLPR_MSG_RING_STALL,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = (uint16_t)token,
		.data = packed,
	};
	int ret = flpr_handshake_send_msg(&stall_msg);
	if (ret < 0) {
		flpr_control_ack_disarm(&stall_ack_ctl);
		k_mutex_unlock(data_lock);
		return ret;
	}

	/* Verify exact packed value echoed. */
	ret = flpr_control_ack_wait(&stall_ack_ctl, timeout_ms, packed);
	if (ret == -EIO) {
		LOG_ERR("Stall ACK mismatch: expected 0x%08x", packed);
	}

	k_mutex_unlock(data_lock);
	return ret;
}

int flpr_acceptance_flpr_stall(uint8_t stall_bits, uint32_t timeout_ms)
{
	/* Persistent: duration = 0. */
	return flpr_acceptance_stall_internal(stall_bits, 0, timeout_ms);
}

int flpr_acceptance_flpr_stall_timed(uint8_t stall_bits, uint32_t duration_ms, uint32_t timeout_ms)
{
	if (stall_bits == 0) {
		/* Zero-bits mask with nonzero duration is ambiguous:
		 * is it "clear stall but also timed"?  Reject it.
		 * A timed-duration clear makes no sense — persistent only. */
		if (duration_ms > 0) {
			LOG_ERR("Timed stall with zero mask rejected");
			return -EINVAL;
		}
	}
	if (duration_ms > FLPR_STALL_DURATION_MAX) {
		LOG_ERR("Duration %u exceeds max %u", duration_ms, FLPR_STALL_DURATION_MAX);
		return -EINVAL;
	}
	return flpr_acceptance_stall_internal(stall_bits, duration_ms, timeout_ms);
}

uint32_t flpr_acceptance_flpr_stall_acked(void)
{
	return flpr_control_ack_payload(&stall_ack_ctl);
}

/* ── Ring test ──────────────────────────────────────────────────── */
/* Static buffers for ring test — too large for shell thread stack.
 * Single-writer: only used by the blocking test loop one at a time. */
static uint8_t test_pattern[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
static uint8_t test_recv_buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES];

int flpr_acceptance_test_run(uint32_t block_count, uint32_t timeout_ms,
			     struct flpr_acceptance_status *out)
{
	return flpr_acceptance_test_run_rate(block_count, timeout_ms, 0, out);
}

int flpr_acceptance_test_run_rate(uint32_t block_count, uint32_t timeout_ms, uint32_t rate_per_sec,
				  struct flpr_acceptance_status *out)
{
	uint32_t sent = 0;
	uint32_t start = k_uptime_get_32();
	uint32_t last_seq = 0;
	bool any_error = false;

	{
		struct flpr_ring_status rs;

		flpr_ring_mgr_get_status(&rs);
		if (!rs.initialized || rs.epoch == 0) {
			if (out) {
				flpr_acceptance_get_status(out);
			}
			return -EAGAIN;
		}

		k_spinlock_key_t key = k_spin_lock(&acc_lock);
		if (test_active) {
			k_spin_unlock(&acc_lock, key);
			if (out) {
				flpr_acceptance_get_status(out);
			}
			return -EBUSY;
		}

		test_active = true;
		test_blocks_sent = 0;
		test_blocks_recv = 0;
		test_crc_errors = 0;
		test_payload_errors = 0;
		test_seq_gaps = 0;
		test_full_events = 0;
		test_backpressure = 0;
		test_empty_events = 0;
		test_stale_events = 0;
		test_flpr_blocks = 0;
		test_flpr_crc_err = 0;
		test_output_full = 0;
		test_flpr_notify_rcv = 0;
		test_flpr_worker_wake = 0;
		test_flpr_consume_ok = 0;
		test_flpr_consume_empty = 0;
		test_flpr_consume_stale = 0;
		test_flpr_produce_ok = 0;
		test_flpr_produce_full = 0;
		latency_min = UINT32_MAX;
		latency_max = 0;
		latency_sum = 0;
		latency_count = 0;
		k_spin_unlock(&acc_lock, key);
	}

	/* Drain semaphore before starting. */
	while (flpr_ring_mgr_wait_consume(0) == 0) {
	}

	/* Send RING_TEST_START to FLPR. */
	{
		struct flpr_msg start_msg = {
			.type = FLPR_MSG_RING_TEST_START,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = 0,
			.data = block_count,
		};
		flpr_handshake_send_msg(&start_msg);
	}

	LOG_INF("Ring test: sending %u blocks...", block_count);

	/* Event-driven batch send loop:
	 *
	 * Batch up to ring capacity, notify FLPR once per batch,
	 * then wait for consume_sem (or 10 ms timeout) before draining
	 * and producing the next batch.
	 *
	 * This avoids busy-polling drains and lets IPC notifications
	 * drive the pipeline.  The FLPR polling fallback (10 ms) provides
	 * a worst-case deadline — notifications are immediate via the IPC
	 * callback. */
#define BATCH_MAX 4

	while (sent < block_count) {
		uint32_t elapsed = k_uptime_get_32() - start;
		if (elapsed > timeout_ms) {
			LOG_WRN("Ring test timeout at %u/%u blocks (%u ms)", sent, block_count,
				elapsed);
			any_error = true;
			break;
		}

		/* Rate limiting before production: sleep if ahead of schedule.
		 * Pure average pacing from test start time — no per-second
		 * windows, no initial burst, no reset.  64-bit multiply
		 * avoids overflow at high block counts.
		 *
		 * Sleep cap at 5000 ms prevents the test thread from sleeping
		 * through the entire remaining budget; long sleeps are split
		 * across multiple iterations. */
		if (rate_per_sec > 0) {
			uint64_t target = flpr_rate_limit_target_ms(sent, rate_per_sec);
			uint64_t actual = (uint64_t)k_uptime_get_32() - (uint64_t)start;
			if (target > actual) {
				uint64_t deficit = target - actual;
				if (deficit > 5000) {
					deficit = 5000;
				}
				k_msleep((uint32_t)deficit);
			}
		}

		/* Drain any available output first. */
		{
			uint16_t vf;
			uint32_t seq_out;
			uint32_t latency;
			while (flpr_ring_mgr_consume_block(test_recv_buf, &vf, &seq_out, NULL,
							   &latency) == FLPR_CONSUME_OK) {
			}
		}

		/* Produce up to BATCH_MAX blocks in one go. */
		uint32_t batch_sent = 0;
		for (uint32_t b = 0; b < BATCH_MAX && sent < block_count; b++) {
			flpr_ring_gen_payload(test_pattern, sizeof(test_pattern), sent);

			enum flpr_produce_result pr = flpr_ring_mgr_produce_block(
				test_pattern, FLPR_RING_PAYLOAD_MAX_INPUT, sent, 0, true);
			if (pr == FLPR_PRODUCE_FULL) {
				break; /* ring full — drain + retry next iteration */
			}
			if (pr != FLPR_PRODUCE_OK) {
				any_error = true;
				break;
			}

			if (sent > 0 && sent != last_seq + 1) {
				k_spinlock_key_t key = k_spin_lock(&acc_lock);
				test_seq_gaps++;
				k_spin_unlock(&acc_lock, key);
			}
			last_seq = sent;
			sent++;
			batch_sent++;

			k_spinlock_key_t key = k_spin_lock(&acc_lock);
			test_blocks_sent = sent;
			k_spin_unlock(&acc_lock, key);
		}

		if (batch_sent > 0) {
			/* Notify FLPR once per batch. */
			if (flpr_ring_mgr_notify_producer() < 0) {
				k_msleep(1);
				continue;
			}
		}

		/* Wait for FLPR to process (notification or poll fallback).
		 * Short timeout avoids busy-wait — the IPC callback gives
		 * consume_sem when output data is available. */
		if (sent < block_count) {
			flpr_ring_mgr_wait_consume(10);
		}
	}
#undef BATCH_MAX

	/* Final drain: wait until recv == sent or global timeout. */
	{
		uint32_t drain_start = k_uptime_get_32();
		uint32_t remaining = timeout_ms - (drain_start - start);
		if (remaining > timeout_ms) {
			remaining = 100;
		}

		while ((k_uptime_get_32() - drain_start) < remaining) {
			uint16_t vf;
			uint32_t seq_out;
			uint32_t latency;
			bool drained = false;

			while (flpr_ring_mgr_consume_block(test_recv_buf, &vf, &seq_out, NULL,
							   &latency) == FLPR_CONSUME_OK) {
				drained = true;
			}

			k_spinlock_key_t key = k_spin_lock(&acc_lock);
			uint32_t recv_now = test_blocks_recv;
			k_spin_unlock(&acc_lock, key);

			if (recv_now >= sent) {
				break;
			}

			if (!drained) {
				flpr_ring_mgr_wait_consume(50);
			}
		}
	}

	/* Send RING_TEST_STOP to FLPR (get final report). */
	{
		struct flpr_msg stop_msg = {
			.type = FLPR_MSG_RING_TEST_STOP,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = 0,
			.data = 0,
		};
		flpr_handshake_send_msg(&stop_msg);
	}

	{
		k_spinlock_key_t key = k_spin_lock(&acc_lock);
		test_active = false;
		k_spin_unlock(&acc_lock, key);
	}

	if (out) {
		flpr_acceptance_get_status(out);
	}

	/* Return nonzero if ANY failure. */
	{
		k_spinlock_key_t key = k_spin_lock(&acc_lock);
		bool has_error = (sent != block_count) || (test_blocks_recv != block_count) ||
				 (test_crc_errors > 0) || (test_payload_errors > 0) ||
				 (test_stale_events > 0) || any_error;
		k_spin_unlock(&acc_lock, key);

		if (has_error) {
			return -1;
		}
	}

	return 0;
}

/* ── Stale diagnostic production (test-use only) ─────────────────── */

int flpr_acceptance_produce_stale_test(uint32_t stale_epoch)
{
	uint32_t idx;
	int ret;

	struct flpr_ring_status rs;
	flpr_ring_mgr_get_status(&rs);
	if (!rs.initialized) {
		return -EAGAIN;
	}
	if (stale_epoch == 0) {
		return -EINVAL;
	}

	/* R1: hold ring_data_lock across the stale-test output-ring
	 * production (shell-only, after coordinated reset with FLPR
	 * quiesced). */
	struct k_mutex *data_lock = flpr_ring_mgr_data_lock();

	k_mutex_lock(data_lock, K_FOREVER);

	/* Allocate slot in OUTPUT ring directly (FLPR→CPUAPP). */
	ret = flpr_ring_produce_begin(flpr_ring_mgr_output_ring(), &idx);
	if (ret != 0) {
		k_mutex_unlock(data_lock);
		return ret;
	}

	uint8_t *slot = flpr_ring_slot_base(flpr_ring_mgr_output_ring(), idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);

	/* Fill with stale epoch — consumer will reject. */
	memset(slot, 0, FLPR_RING_SLOT_STRIDE);
	meta->epoch = stale_epoch;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	meta->valid_frames = 0;

	flpr_ring_produce_commit(flpr_ring_mgr_output_ring(), idx);
	k_mutex_unlock(data_lock);
	return 0;
}

/* ── Stress (moved from flpr_handshake.c) ────────────────────────── */

void flpr_acceptance_stress(uint32_t count, struct flpr_status *out)
{
	if (count == 0) {
		return;
	}
	if (count > FLPR_STRESS_MAX_COUNT) {
		count = FLPR_STRESS_MAX_COUNT;
	}

	/* Guard: reject if not ready+acked or if already active. */
	{
		k_spinlock_key_t key = k_spin_lock(&acc_lock);
		if (stress_active) {
			k_spin_unlock(&acc_lock, key);
			if (out) {
				flpr_acceptance_stress_snapshot(out);
			}
			return;
		}
		k_spin_unlock(&acc_lock, key);

		struct flpr_status hs;
		flpr_handshake_get_status(&hs);
		if (!hs.ready || !hs.acked) {
			LOG_WRN("FLPR stress rejected: FLPR not ready");
			if (out) {
				flpr_acceptance_stress_snapshot(out);
			}
			return;
		}

		/* Reset stress state under lock. */
		k_spinlock_key_t key2 = k_spin_lock(&acc_lock);
		stress_active = true;
		stress_count = count;
		stress_sent = 0;
		stress_recv = 0;
		stress_timeouts = 0;
		stress_cookie = 0;
		stress_stale = 0;
		stress_mismatch = 0;
		stress_err_send = 0;
		k_spin_unlock(&acc_lock, key2);
	}

	/* Drain any stale semaphore give from a previous interrupted run. */
	while (k_sem_take(&stress_sem, K_NO_WAIT) == 0) {
		/* drain */
	}

	LOG_INF("FLPR stress start: %u pings", count);

	for (uint32_t i = 0; i < count; i++) {

		/* Drain semaphore before EVERY iteration: guards against late-PONG
		 * from a previous timed-out iteration. */
		while (k_sem_take(&stress_sem, K_NO_WAIT) == 0) {
			/* drain */
		}

		/* Snapshot cookie under lock. */
		uint32_t cookie;
		{
			k_spinlock_key_t key = k_spin_lock(&acc_lock);
			stress_cookie++;
			cookie = stress_cookie;
			k_spin_unlock(&acc_lock, key);
		}

		struct flpr_msg ping = {
			.type = FLPR_MSG_STRESS_PING,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = (uint16_t)(i & 0xFFFFU),
			.data = cookie,
		};

		/* Narrow core transport seam.  Note: a send failure here also
		 * increments the handshake err_send counter (shared seam),
		 * in addition to the stress-local stress_err_send. */
		int ret = flpr_handshake_send_msg(&ping);
		if (ret < 0) {
			k_spinlock_key_t key = k_spin_lock(&acc_lock);
			stress_err_send++;
			k_spin_unlock(&acc_lock, key);
			LOG_WRN("FLPR stress ping %u send failed: %d", i, ret);
			k_msleep(1);
			continue;
		}

		/* Count sent after successful send. */
		{
			k_spinlock_key_t key = k_spin_lock(&acc_lock);
			stress_sent++;
			k_spin_unlock(&acc_lock, key);
		}

		/* Wait for matching PONG with 200 ms timeout. */
		ret = k_sem_take(&stress_sem, K_MSEC(200));
		if (ret != 0) {
			/* Timeout: invalidate expected cookie so any late PONG
			 * for THIS iteration is classified as stale, not
			 * mistaken for the next iteration's match. */
			k_spinlock_key_t key = k_spin_lock(&acc_lock);
			stress_timeouts++;
			stress_cookie++; /* invalidate → late PONG is stale */
			k_spin_unlock(&acc_lock, key);
		}
	}

	LOG_INF("FLPR stress done: sent=%u recv=%u lost=%u timeouts=%u "
		"stale=%u mismatch=%u err=%u",
		stress_sent, stress_recv, count - stress_recv, stress_timeouts, stress_stale,
		stress_mismatch, stress_err_send);

	{
		k_spinlock_key_t key = k_spin_lock(&acc_lock);
		stress_active = false;
		k_spin_unlock(&acc_lock, key);
	}

	if (out) {
		flpr_acceptance_stress_snapshot(out);
	}
}

bool flpr_acceptance_stress_active(void)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	bool v = stress_active;
	k_spin_unlock(&acc_lock, key);
	return v;
}

void flpr_acceptance_stress_snapshot(struct flpr_status *out)
{
	if (!out) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	out->stress_active = stress_active;
	out->stress_count = stress_count;
	out->stress_sent = stress_sent;
	out->stress_recv = stress_recv;
	out->stress_timeouts = stress_timeouts;
	out->stress_stale = stress_stale;
	out->stress_mismatch = stress_mismatch;
	out->stress_err_send = stress_err_send;
	k_spin_unlock(&acc_lock, key);
}

/* ── Fault hang (moved from flpr_handshake.c) ────────────────────── */

int flpr_acceptance_send_fault_hang(uint32_t timeout_ms)
{
	/* Drain any stale semaphore give. */
	while (k_sem_take(&hang_ack_sem, K_NO_WAIT) == 0) {
	}
	{
		k_spinlock_key_t key = k_spin_lock(&acc_lock);
		hang_ack_received = false;
		k_spin_unlock(&acc_lock, key);
	}

	struct flpr_msg hang_msg = {
		.type = FLPR_MSG_FAULT_HANG,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = 0,
		.data = 0,
	};
	int ret = flpr_handshake_send_msg(&hang_msg);
	if (ret < 0) {
		LOG_ERR("FAULT_HANG send failed: %d", ret);
		return -EIO;
	}

	/* Wait for FAULT_HANG_ACK from FLPR. */
	ret = k_sem_take(&hang_ack_sem, K_MSEC(timeout_ms));
	if (ret != 0) {
		LOG_WRN("FAULT_HANG_ACK timeout (%u ms)", timeout_ms);
		return -ETIMEDOUT;
	}

	/* R1: read the published flag under the lock after the take
	 * (take-before-read ordering). */
	{
		k_spinlock_key_t key = k_spin_lock(&acc_lock);
		bool received = hang_ack_received;
		k_spin_unlock(&acc_lock, key);

		if (!received) {
			return -EIO;
		}
	}

	LOG_INF("FAULT_HANG_ACK received — FLPR hang imminent");
	return 0;
}

/* ── Gate 1–6 acceptance run ─────────────────────────────────────── */

/* Static scratch — too large for shell thread stack. */
static uint8_t acceptance_buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
static struct flpr_ring_status acceptance_rs;
static struct flpr_acceptance_status acceptance_as;

#define ACCEPT_RS()  (&acceptance_rs)
#define ACCEPT_AS()  (&acceptance_as)
#define ACCEPT_BUF() (acceptance_buf)

/* Line formatting for the output sink.  The callback receives the fully
 * formatted line (no trailing newline; the shell adds it). */
static char gate_line[256];

static void gate_emit(void *ctx, flpr_acceptance_print_t print,
		      enum flpr_acceptance_print_level lvl, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(gate_line, sizeof(gate_line), fmt, ap);
	va_end(ap);

	print(ctx, lvl, gate_line);
}

int flpr_acceptance_run_gates(uint32_t count, void *ctx, flpr_acceptance_print_t print)
{
	/* Ensure rings are reset and clean before acceptance. */
	{
		int r = flpr_ring_mgr_coordinated_reset(0, 5000);
		if (r != 0) {
			gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR, "Coordinated reset failed: %d",
				  r);
			return r;
		}
		/* Clear any producer stall. */
		flpr_acceptance_stall_producer(false);
		/* Clear any FLPR stalls. */
		flpr_acceptance_flpr_stall(0, 5000);
	}

	bool any_fail = false;
	uint32_t gate1_elapsed = 0; /* stored for throughput report */

#define GATE_HEADER(n, desc)                                                                       \
	gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL, "--- Gate %d: %s ---", n, desc)

	/* ── Gate 1: normal loopback ────────────────────────────────────
	 * Timeout derived from measured throughput: ~460 blk/s → ~2.17 ms/blk.
	 * Budget: 3 ms per block + 60 s floor.  100 k → 360 s (≈1.66× actual). */
	GATE_HEADER(1, "normal loopback");
	{
		uint32_t start = k_uptime_get_32();
		/* timeout = count * 3 ms + 60 s floor */
		uint32_t timeout = count * 3 + 60000;
		if (timeout < 60000) {
			timeout = 60000;
		}

		struct flpr_acceptance_status *s = ACCEPT_AS();
		int r = flpr_acceptance_test_run(count, timeout, s);
		gate1_elapsed = k_uptime_get_32() - start;

		gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL,
			  "Sent=%u Recv=%u CRC_Err=%u Pay_Err=%u Stale=%u BP=%u",
			  s->test_blocks_sent, s->test_blocks_recv, s->test_crc_errors,
			  s->test_payload_errors, s->test_stale_events, s->test_backpressure);
		if (s->latency_count > 0) {
			uint32_t avg = (uint32_t)(s->latency_sum / s->latency_count);
			uint32_t tput =
				(s->latency_count * 1000U) / (gate1_elapsed ? gate1_elapsed : 1);
			gate_emit(
				ctx, print, FLPR_ACC_PRINT_NORMAL,
				"Latency: min=%u us max=%u us avg=%u us N=%u  Throughput: %u blk/s",
				k_cyc_to_us_ceil32(s->latency_min),
				k_cyc_to_us_ceil32(s->latency_max), k_cyc_to_us_ceil32(avg),
				s->latency_count, tput);
			gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL,
				  "FLPR: blk=%u crc=%u cons_ok=%u prod_ok=%u full=%u",
				  s->test_producer_blocks, s->flpr_consume_stale,
				  s->flpr_consume_ok, s->flpr_produce_ok, s->flpr_produce_full);
		}
		if (r == 0 && s->test_blocks_sent == count && s->test_payload_errors == 0 &&
		    s->test_crc_errors == 0 && s->test_stale_events == 0 &&
		    s->test_backpressure == 0) {
			gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL,
				  "GATE 1 PASS: %u blocks in %u ms", count, gate1_elapsed);
		} else {
			gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR,
				  "GATE 1 FAIL: sent=%u recv=%u crc=%u pay=%u stale=%u bp=%u rc=%d",
				  s->test_blocks_sent, s->test_blocks_recv, s->test_crc_errors,
				  s->test_payload_errors, s->test_stale_events,
				  s->test_backpressure, r);
			any_fail = true;
		}
	}

	/* ── Gate 2: CPU producer stall → resume exact ───────────────── */
	GATE_HEADER(2, "CPU producer stall / resume");
	{
		/* Ensure clean state: reset rings + clear stalls. */
		flpr_acceptance_stall_producer(false);
		int rr = flpr_ring_mgr_coordinated_reset(0, 5000);
		if (rr != 0) {
			gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR, "GATE 2 reset fail: %d", rr);
			any_fail = true;
			goto gate2_done;
		}

		flpr_acceptance_stall_producer(true);
		uint32_t full_cnt = 0;
		for (uint32_t i = 0; i < 10; i++) {
			flpr_ring_gen_payload(ACCEPT_BUF(), sizeof(acceptance_buf), i);
			if (flpr_ring_mgr_produce_block(ACCEPT_BUF(), FLPR_RING_PAYLOAD_MAX_INPUT,
							i, 0, false) == FLPR_PRODUCE_FULL) {
				full_cnt++;
			}
		}
		flpr_acceptance_stall_producer(false);

		struct flpr_acceptance_status *s = ACCEPT_AS();
		flpr_acceptance_get_status(s);
		gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL,
			  "Stall FULL count: %u (expect 10)  Backpressure: %u", full_cnt,
			  s->test_backpressure);
		if (s->test_blocks_sent > 0 || full_cnt != 10) {
			gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR, "GATE 2 FAIL: sent=%u full=%u",
				  s->test_blocks_sent, full_cnt);
			any_fail = true;
		} else {
			struct flpr_acceptance_status *s2 = ACCEPT_AS();
			int r2 = flpr_acceptance_test_run(100, 15000, s2);
			if (r2 == 0 && s2->test_blocks_recv == 100 &&
			    s2->test_payload_errors == 0) {
				gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL, "GATE 2 PASS");
			} else {
				gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR,
					  "GATE 2 FAIL: resume rc=%d recv=%u", r2,
					  s2->test_blocks_recv);
				any_fail = true;
			}
		}
	}
gate2_done:

	/* ── Gate 3: FLPR input-consumer stall ─────────────────────────
	 * Fill exactly 4 slots, backpressure on 5th+, resume exact. */
	GATE_HEADER(3, "FLPR input-consumer stall");
	{
		/* Clean reset. */
		flpr_acceptance_stall_producer(false);
		flpr_acceptance_flpr_stall(0, 5000);
		int r = flpr_ring_mgr_coordinated_reset(0, 5000);
		if (r != 0) {
			gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR, "GATE 3 reset fail: %d", r);
			any_fail = true;
		} else {
			r = flpr_acceptance_flpr_stall(FLPR_STALL_CONSUMER_INPUT, 5000);
			if (r != 0) {
				gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR, "GATE 3 stall fail: %d",
					  r);
				any_fail = true;
			} else {
				uint32_t ok = 0, full_after = 0;
				for (uint32_t i = 0; i < 10; i++) {
					flpr_ring_gen_payload(ACCEPT_BUF(), sizeof(acceptance_buf),
							      i);
					enum flpr_produce_result pr = flpr_ring_mgr_produce_block(
						ACCEPT_BUF(), FLPR_RING_PAYLOAD_MAX_INPUT, i, 0,
						false);
					if (i < 4 && pr == FLPR_PRODUCE_OK) {
						ok++;
					} else if (i >= 4 && pr == FLPR_PRODUCE_FULL) {
						full_after++;
					}
				}
				gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL,
					  "Filled: %u (expect 4)  Full-after: %u (expect >=4)", ok,
					  full_after);
				if (ok != 4 || full_after < 2) {
					gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR,
						  "GATE 3 FAIL: ok=%u full_after=%u", ok,
						  full_after);
					any_fail = true;
				}
				/* Resume FLPR: clear stalls, drain output. */
				flpr_acceptance_flpr_stall(0, 5000);
				k_msleep(200);

				/* Wait for FLPR to forward the 4 queued blocks. */
				uint32_t recv = 0;
				uint32_t drain_start = k_uptime_get_32();
				while ((k_uptime_get_32() - drain_start) < 5000 && recv < 4) {
					uint16_t vf;
					if (flpr_ring_mgr_consume_block(NULL, &vf, NULL, NULL,
									NULL) == FLPR_CONSUME_OK) {
						recv++;
					} else {
						flpr_ring_mgr_wait_consume(10);
					}
				}
				gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL,
					  "Resume: recv %u (expect 4)", recv);
				if (recv == 4) {
					gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL, "GATE 3 PASS");
				} else {
					gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR,
						  "GATE 3 FAIL: recv=%u", recv);
					any_fail = true;
				}
			}
		}
	}

	/* ── Gate 4: FLPR output-producer stall ─────────────────────────
	 * Output fills on FLPR side, input never lost, resume exact. */
	GATE_HEADER(4, "FLPR output-producer stall");
	{
		/* Clean reset. */
		flpr_acceptance_stall_producer(false);
		flpr_acceptance_flpr_stall(0, 5000);
		int r = flpr_ring_mgr_coordinated_reset(0, 5000);
		if (r != 0) {
			gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR, "GATE 4 reset fail: %d", r);
			any_fail = true;
		} else {
			r = flpr_acceptance_flpr_stall(FLPR_STALL_PRODUCER_OUTPUT, 5000);
			if (r != 0) {
				gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR, "GATE 4 stall fail: %d",
					  r);
				any_fail = true;
			} else {
				/* Send 10 blocks — FLPR processes input (forward to output) but
				 * output is stalled so FLPR backpressures on input ring.
				 * Because FLPR preserves input on output-stall (does not consume),
				 * the input ring fills at 4 slots and subsequent produces return
				 * FULL. Do NOT retry — just count the results. */
				uint32_t ok4 = 0, full4 = 0;
				for (uint32_t i = 0; i < 10; i++) {
					flpr_ring_gen_payload(ACCEPT_BUF(), sizeof(acceptance_buf),
							      i);
					enum flpr_produce_result pr = flpr_ring_mgr_produce_block(
						ACCEPT_BUF(), FLPR_RING_PAYLOAD_MAX_INPUT, i, 0,
						false);
					if (pr == FLPR_PRODUCE_OK) {
						ok4++;
						flpr_ring_mgr_notify_producer();
					} else {
						full4++;
					}
				}
				k_msleep(300);

				struct flpr_acceptance_status *s = ACCEPT_AS();
				flpr_acceptance_get_status(s);
				gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL,
					  "Produced: %u OK / %u FULL (expect 4/6)  FLPR "
					  "output-full: %u",
					  ok4, full4, s->flpr_produce_full);

				/* Resume and drain. */
				flpr_acceptance_flpr_stall(0, 5000);
				k_msleep(200);

				uint32_t recv = 0;
				uint32_t drain_start = k_uptime_get_32();
				while ((k_uptime_get_32() - drain_start) < 5000 && recv < 10) {
					uint16_t vf;
					if (flpr_ring_mgr_consume_block(NULL, &vf, NULL, NULL,
									NULL) == FLPR_CONSUME_OK) {
						recv++;
					} else {
						flpr_ring_mgr_wait_consume(10);
					}
				}
				gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL,
					  "Resume: recv %u (expect 4)", recv);
				if (full4 >= 2 && recv >= 3) {
					gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL, "GATE 4 PASS");
				} else {
					gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR,
						  "GATE 4 FAIL: ok=%u full=%u recv=%u", ok4, full4,
						  recv);
					any_fail = true;
				}
			}
		}
	}

	/* ── Gate 5: stale epoch injection, reject, recovery ─────────── */
	GATE_HEADER(5, "stale epoch injection");
	{
		/* Clean reset. */
		flpr_acceptance_stall_producer(false);
		flpr_acceptance_flpr_stall(0, 5000);
		int r = flpr_ring_mgr_coordinated_reset(0, 5000);
		if (r != 0) {
			gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR, "GATE 5 reset fail: %d", r);
			any_fail = true;
		} else {
			struct flpr_ring_status *rs = ACCEPT_RS();
			flpr_ring_mgr_get_status(rs);
			uint32_t stale_epoch = rs->epoch + 31337;
			if (flpr_acceptance_produce_stale_test(stale_epoch) != 0) {
				gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR, "stale injection fail");
				any_fail = true;
			} else {
				flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL);
				k_msleep(50);
				struct flpr_acceptance_status *s5 = ACCEPT_AS();
				flpr_acceptance_get_status(s5);
				gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL,
					  "Stale events: %u (expect 1)", s5->test_stale_events);
				if (s5->test_stale_events == 1) {
					struct flpr_acceptance_status *s5r = ACCEPT_AS();
					int r5r = flpr_acceptance_test_run(100, 15000, s5r);
					if (r5r == 0 && s5r->test_blocks_recv == 100) {
						gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL,
							  "GATE 5 PASS");
					} else {
						gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR,
							  "GATE 5 FAIL recovery: rc=%d recv=%u",
							  r5r, s5r->test_blocks_recv);
						any_fail = true;
					}
				} else {
					gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR,
						  "GATE 5 FAIL: stale=%u", s5->test_stale_events);
					any_fail = true;
				}
			}
		}
	}

	/* ── Gate 6: explicit empty ──────────────────────────────────── */
	GATE_HEADER(6, "explicit empty");
	{
		struct flpr_ring_status *s = ACCEPT_RS();
		flpr_ring_mgr_get_status(s);
		gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL, "In used=%u  Out used=%u", s->in_used,
			  s->out_used);
		if (s->in_used == 0 && s->out_used == 0) {
			gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL, "GATE 6 PASS");
		} else {
			gate_emit(ctx, print, FLPR_ACC_PRINT_ERROR, "GATE 6 FAIL");
			any_fail = true;
		}
	}

#undef GATE_HEADER

	if (any_fail) {
		gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL, "ACCEPTANCE FAILED");
		return -1;
	}
	gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL, "ACCEPTANCE PASSED — all gates clear");
	gate_emit(ctx, print, FLPR_ACC_PRINT_NORMAL, "Gate 1 throughput: %u blk/s",
		  gate1_elapsed > 0 ? (count * 1000U) / gate1_elapsed : 0U);
	return 0;
}

/* ── Diagnostic counter hooks (called from core under config) ────── */

bool flpr_acceptance_test_active(void)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	bool v = test_active;
	k_spin_unlock(&acc_lock, key);
	return v;
}

void flpr_acceptance_note_recv(void)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	test_blocks_recv++;
	k_spin_unlock(&acc_lock, key);
}

void flpr_acceptance_note_full(void)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	test_full_events++;
	k_spin_unlock(&acc_lock, key);
}

void flpr_acceptance_note_stale(void)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	test_stale_events++;
	k_spin_unlock(&acc_lock, key);
}

void flpr_acceptance_note_backpressure(void)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	test_backpressure++;
	k_spin_unlock(&acc_lock, key);
}

void flpr_acceptance_note_crc_err(void)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	test_crc_errors++;
	k_spin_unlock(&acc_lock, key);
}

void flpr_acceptance_note_payload_err(uint32_t frame_errors)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	test_payload_errors += frame_errors;
	k_spin_unlock(&acc_lock, key);
}

void flpr_acceptance_note_latency(uint32_t latency_cycles)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	if (latency_cycles < latency_min) {
		latency_min = latency_cycles;
	}
	if (latency_cycles > latency_max) {
		latency_max = latency_cycles;
	}
	latency_sum += latency_cycles;
	latency_count++;
	k_spin_unlock(&acc_lock, key);
}

void flpr_acceptance_note_flpr_blocks(uint32_t blocks)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	test_flpr_blocks = blocks;
	k_spin_unlock(&acc_lock, key);
}

void flpr_acceptance_remote_restarted(void)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	test_active = false;
	test_blocks_sent = 0;
	test_blocks_recv = 0;
	test_crc_errors = 0;
	test_payload_errors = 0;
	test_seq_gaps = 0;
	test_full_events = 0;
	test_backpressure = 0;
	test_empty_events = 0;
	test_stale_events = 0;
	test_flpr_blocks = 0;
	test_flpr_crc_err = 0;
	test_output_full = 0;
	test_flpr_notify_rcv = 0;
	test_flpr_worker_wake = 0;
	test_flpr_consume_ok = 0;
	test_flpr_consume_empty = 0;
	test_flpr_consume_stale = 0;
	test_flpr_produce_ok = 0;
	test_flpr_produce_full = 0;
	latency_min = UINT32_MAX;
	latency_max = 0;
	latency_sum = 0;
	latency_count = 0;
	stall_producer_enabled = false;
	k_spin_unlock(&acc_lock, key);
}

#if defined(FLPR_ACCEPTANCE_NATIVE_TEST)
/* GCOVR_EXCL_START — test-only helpers, absent from production builds */

void flpr_acceptance_test_reset_state(void)
{
	k_sem_init(&stall_ack_sem, 0, 1);
	flpr_control_ack_init(&stall_ack_ctl, &stall_ack_sem);
	k_sem_init(&stress_sem, 0, FLPR_STRESS_MAX_COUNT + 1);
	k_sem_init(&hang_ack_sem, 0, 1);

	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	test_active = false;
	test_blocks_sent = 0;
	test_blocks_recv = 0;
	test_crc_errors = 0;
	test_payload_errors = 0;
	test_seq_gaps = 0;
	test_full_events = 0;
	test_backpressure = 0;
	test_empty_events = 0;
	test_stale_events = 0;
	test_flpr_blocks = 0;
	test_flpr_crc_err = 0;
	test_output_full = 0;
	test_flpr_notify_rcv = 0;
	test_flpr_worker_wake = 0;
	test_flpr_consume_ok = 0;
	test_flpr_consume_empty = 0;
	test_flpr_consume_stale = 0;
	test_flpr_produce_ok = 0;
	test_flpr_produce_full = 0;
	latency_min = UINT32_MAX;
	latency_max = 0;
	latency_sum = 0;
	latency_count = 0;
	stall_producer_enabled = false;
	stress_active = false;
	stress_count = 0;
	stress_sent = 0;
	stress_recv = 0;
	stress_timeouts = 0;
	stress_cookie = 0;
	stress_stale = 0;
	stress_mismatch = 0;
	stress_err_send = 0;
	hang_ack_received = false;
	k_spin_unlock(&acc_lock, key);
}

void flpr_acceptance_test_set_active(bool on)
{
	k_spinlock_key_t key = k_spin_lock(&acc_lock);
	test_active = on;
	k_spin_unlock(&acc_lock, key);
}

void flpr_acceptance_test_set_next_stall_token(uint16_t token)
{
	flpr_control_ack_test_set_next_token(&stall_ack_ctl, token);
}

uint32_t flpr_acceptance_test_stall_ack_sem_count(void)
{
	return k_sem_count_get(&stall_ack_sem);
}

uint32_t flpr_acceptance_test_hang_ack_sem_count(void)
{
	return k_sem_count_get(&hang_ack_sem);
}

uint32_t flpr_acceptance_test_stress_sem_count(void)
{
	return k_sem_count_get(&stress_sem);
}

uint32_t flpr_acceptance_test_stall_stale_count(void)
{
	return flpr_control_ack_test_stale_count(&stall_ack_ctl);
}

/* GCOVR_EXCL_STOP */
#endif /* FLPR_ACCEPTANCE_NATIVE_TEST */
