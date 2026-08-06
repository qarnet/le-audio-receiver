/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLPR-image acceptance/diagnostic handlers (R8).
 *
 * Compiled only under CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS (see
 * src/flpr/Kconfig; enabled in src/flpr/prj.conf for the current lab
 * build).  All IPC goes through the injected deps->send / deps->wake;
 * no ipc_service/device/DT/irq references — native_sim testable.
 * Single-threaded on FLPR, no locks.
 */

#include "acceptance.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

/* Wire-protocol structural pins (unchanged values — see
 * flpr_protocol.h / flpr_ring.h; re-asserted here for the moved code). */
BUILD_ASSERT(FLPR_PROTOCOL_VERSION == 4U, "protocol version must stay 4");
BUILD_ASSERT(FLPR_MSG_RING_TEST_START == 0x12U, "RING_TEST_START wire value");
BUILD_ASSERT(FLPR_MSG_RING_TEST_STOP == 0x13U, "RING_TEST_STOP wire value");
BUILD_ASSERT(FLPR_MSG_RING_TEST_REPORT == 0x14U, "RING_TEST_REPORT wire value");
BUILD_ASSERT(FLPR_MSG_RING_STALL == 0x17U, "RING_STALL wire value");
BUILD_ASSERT(FLPR_MSG_RING_STALL_ACK == 0x18U, "RING_STALL_ACK wire value");
BUILD_ASSERT(FLPR_MSG_STRESS_PING == 0x05U, "STRESS_PING wire value");
BUILD_ASSERT(FLPR_MSG_STRESS_PONG == 0x06U, "STRESS_PONG wire value");
BUILD_ASSERT(FLPR_MSG_FAULT_HANG == 0x20U, "FAULT_HANG wire value");
BUILD_ASSERT(FLPR_MSG_FAULT_HANG_ACK == 0x21U, "FAULT_HANG_ACK wire value");

/* ── Injected deps ───────────────────────────────────────────────── */

static const struct flpr_acceptance_deps *acc_deps;

/* ── Ring test state ─────────────────────────────────────────────── */

static bool ring_test_active;
static uint32_t ring_test_block_count; /* blocks processed this test */
static uint32_t ring_test_crc_errors;  /* CRC mismatches */
static uint32_t ring_test_seq_gaps;    /* sequence gaps */
static uint32_t ring_test_epoch_stale; /* stale epoch rejections */
static uint32_t ring_test_empty_polls; /* times ring was empty */
static uint32_t ring_test_output_full; /* output ring full count */

/* ── Diagnostic counters (no lock — single-threaded FLPR) ────────── */

static uint32_t diag_notify_rcv;    /* RING_PRODUCER messages received */
static uint32_t diag_worker_wake;   /* ring_process_input() call count */
static uint32_t diag_consume_ok;    /* slots successfully consumed */
static uint32_t diag_consume_empty; /* consumer found ring empty */
static uint32_t diag_consume_stale; /* consumer found stale epoch */
static uint32_t diag_produce_ok;    /* output slots produced */
static uint32_t diag_produce_full;  /* output ring was full */

/* ── Stall control ─────────────────────────────────────────────────
 * Bitmask (FLPR_STALL_CONSUMER_INPUT / FLPR_STALL_PRODUCER_OUTPUT).
 * Written by the RING_STALL handler, read by poll + callback.
 * Timed stall (duration > 0): timer expiry atomically clears all bits
 * and kicks ring_wake_sem so queued input drains even without a later
 * producer notification. */

static atomic_t stall_flags = ATOMIC_INIT(0);

static void stall_timer_expiry(struct k_timer *timer);

/* One-shot timer for timed-stall auto-clear. */
static K_TIMER_DEFINE(stall_timer, stall_timer_expiry, NULL);

static uint32_t diag_timed_stall_start_count;
static uint32_t diag_timed_stall_expiry_count;

/* ── Fault hang state ──────────────────────────────────────────────
 * Set by the FAULT_HANG handler AFTER the ACK is sent (ACK-before-spin);
 * main.c's loop reads flpr_acceptance_hang_pending(), then performs the
 * hardware spin (k_busy_wait + irq_lock). */

static atomic_t hang_pending = ATOMIC_INIT(0);

/* ── Timer expiry ──────────────────────────────────────────────────
 * Only fires for timed stalls (duration > 0).  Atomically clears all
 * stall bits and kicks ring_wake_sem so queued input drains even
 * without a later producer notification.  ISR context: no logging, no
 * blocking calls. */

static void stall_timer_expiry(struct k_timer *timer)
{
	(void)timer;
	atomic_clear(&stall_flags);
	diag_timed_stall_expiry_count++;
	if (acc_deps != NULL && acc_deps->wake != NULL) {
		acc_deps->wake();
	}
}

/* ── Public API ──────────────────────────────────────────────────── */

void flpr_acceptance_init(const struct flpr_acceptance_deps *deps)
{
	acc_deps = deps;

	k_timer_stop(&stall_timer);
	atomic_clear(&stall_flags);
	atomic_clear(&hang_pending);
	ring_test_active = false;
	ring_test_block_count = 0;
	ring_test_crc_errors = 0;
	ring_test_seq_gaps = 0;
	ring_test_epoch_stale = 0;
	ring_test_empty_polls = 0;
	ring_test_output_full = 0;
	diag_notify_rcv = 0;
	diag_worker_wake = 0;
	diag_consume_ok = 0;
	diag_consume_empty = 0;
	diag_consume_stale = 0;
	diag_produce_ok = 0;
	diag_produce_full = 0;
	diag_timed_stall_start_count = 0;
	diag_timed_stall_expiry_count = 0;
}

bool flpr_acceptance_handle_msg(const struct flpr_msg *msg)
{
	if (msg == NULL) {
		return false;
	}

	switch (msg->type) {

	case FLPR_MSG_RING_TEST_START: {
		ring_test_active = true;
		ring_test_block_count = 0;
		ring_test_crc_errors = 0;
		ring_test_seq_gaps = 0;
		return true;
	}

	case FLPR_MSG_RING_TEST_STOP: {
		ring_test_active = false;
		/* Send multi-report test results.
		 * Subtype encoded in seq high byte:
		 *   0x00: block_count (lo 16-bit) + crc_errors (data)
		 *   0xD1: consume_ok (full 32-bit in data)
		 *   0xD2: produce_ok (full 32-bit in data)
		 *   0xD3: notify_rcv(lo 8) + worker_wake(hi 8 of data)
		 *          produce_full(lo 16) in seq
		 *   0xD4: cons_empty + cons_stale (data lo/hi 16-bit) */

		/* Report 1: block_count (32-bit) + crc_errors.
		 *   seq lo 16 = crc_errors, data = block_count. */
		{
			struct flpr_msg r0 = {
				.type = FLPR_MSG_RING_TEST_REPORT,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = (uint16_t)(ring_test_crc_errors & 0xFFFFU),
				.data = ring_test_block_count,
			};
			(void)acc_deps->send(&r0);
		}
		/* Report 2: consume_ok (full 32-bit). */
		{
			struct flpr_msg r1 = {
				.type = FLPR_MSG_RING_TEST_REPORT,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 0xD100U,
				.data = diag_consume_ok,
			};
			(void)acc_deps->send(&r1);
		}
		/* Report 3: produce_ok (full 32-bit). */
		{
			struct flpr_msg r2 = {
				.type = FLPR_MSG_RING_TEST_REPORT,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 0xD200U,
				.data = diag_produce_ok,
			};
			(void)acc_deps->send(&r2);
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
			(void)acc_deps->send(&r3);
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
			(void)acc_deps->send(&r4);
		}
		return true;
	}

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

		/* ACK with packed value (exact echo, R1: request sequence
		 * token echoed for correlation). */
		struct flpr_msg ack =
			flpr_control_ack_make(msg, FLPR_MSG_RING_STALL_ACK, msg->data);
		(void)acc_deps->send(&ack);
		return true;
	}

	case FLPR_MSG_STRESS_PING: {
		struct flpr_msg pong = {
			.type = FLPR_MSG_STRESS_PONG,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = msg->seq,
			.data = msg->data,
		};
		(void)acc_deps->send(&pong);
		return true;
	}

	case FLPR_MSG_FAULT_HANG: {
		/* Stage 4B: CPUAPP requests FLPR to hang.
		 * 1. Send ACK immediately (from IPC callback, ISR context OK).
		 * 2. Set atomic flag.
		 * 3. Wake main loop — main loop sees flag, disables IRQs, spins.
		 * After IRQ disable, no more heartbeats, no ring processing. */
		struct flpr_msg ack = {
			.type = FLPR_MSG_FAULT_HANG_ACK,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = 0,
			.data = 0,
		};
		(void)acc_deps->send(&ack);

		atomic_set(&hang_pending, 1);
		if (acc_deps->wake != NULL) {
			acc_deps->wake();
		}
		return true;
	}

	default:
		return false;
	}
}

void flpr_acceptance_on_ring_reset(void)
{
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
}

bool flpr_acceptance_hang_pending(void)
{
	return atomic_get(&hang_pending) != 0;
}

/* ── Hooks invoked from production ring processing (main.c) ──────── */

bool flpr_acceptance_test_active(void)
{
	return ring_test_active;
}

uint8_t flpr_acceptance_stall_flags(void)
{
	return (uint8_t)atomic_get(&stall_flags);
}

void flpr_acceptance_note_worker_wake(void)
{
	diag_worker_wake++;
}

void flpr_acceptance_note_consume_ok(void)
{
	diag_consume_ok++;
}

void flpr_acceptance_note_consume_empty(void)
{
	diag_consume_empty++;
}

void flpr_acceptance_note_consume_stale(void)
{
	diag_consume_stale++;
}

void flpr_acceptance_note_produce_ok(void)
{
	diag_produce_ok++;
}

void flpr_acceptance_note_produce_full(void)
{
	diag_produce_full++;
}

void flpr_acceptance_note_notify_rcv(void)
{
	diag_notify_rcv++;
}

void flpr_acceptance_note_block_processed(void)
{
	ring_test_block_count++;
}

void flpr_acceptance_note_crc_error(void)
{
	ring_test_crc_errors++;
}

void flpr_acceptance_note_empty_poll(void)
{
	ring_test_empty_polls++;
}

void flpr_acceptance_note_epoch_stale(void)
{
	ring_test_epoch_stale++;
}
