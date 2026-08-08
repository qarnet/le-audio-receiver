/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP FLPR acceptance/diagnostic module (R8).
 *
 * Owns the acceptance orchestration and state moved out of the core
 * ring manager, handshake, and acceptance shell (R4):
 *   - raw diagnostic produce/consume blocks (ring throughput test);
 *   - stale-epoch diagnostic production into the output ring;
 *   - producer stall injection;
 *   - FLPR stall / timed-stall requests + ACK correlation;
 *   - RING_TEST_REPORT aggregation (FLPR-reported counters);
 *   - acceptance status fields (test/stall/FLPR-report/latency);
 *   - handshake stress + fault-hang orchestration;
 *   - Gate 1–6 acceptance run.
 *
 * Compiled only when CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS is enabled
 * (nRF54L15 board conf).  The FLPR-image side of the acceptance harness
 * is src/flpr/acceptance.c, gated by CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS.
 *
 * IPC: registers ONE diagnostic message handler with the handshake
 * module (flpr_handshake_register_diag_handlers) that receives
 * RING_TEST_REPORT, RING_STALL_ACK, STRESS_PONG, and FAULT_HANG_ACK.
 * Production ring handlers (reset ACK, consumer) stay in the core ring
 * manager.  All sends go through the narrow core seam
 * flpr_handshake_send_msg().  Stall ACK correlation uses the shared
 * control-ACK engine (src/flpr_control_ack.c) — never duplicated.
 */

#ifndef FLPR_ACCEPTANCE_H_
#define FLPR_ACCEPTANCE_H_

#include <stdint.h>
#include <stdbool.h>

#include "flpr_handshake.h" /* struct flpr_status (stress output) */

#ifdef __cplusplus
extern "C" {
#endif

/** Snapshot of acceptance status.  The core production ring status
 *  (struct flpr_ring_status) stays separate; the acceptance shell
 *  merges both and prints byte-identically. */
struct flpr_acceptance_status {
	/* Ring test */
	bool test_active;
	uint32_t test_blocks_sent;
	uint32_t test_blocks_recv;
	uint32_t test_crc_errors;
	uint32_t test_payload_errors; /* independent memcmp mismatches */
	uint32_t test_seq_gaps;
	uint32_t test_full_events;
	uint32_t test_backpressure; /* stall-producer = full counted */
	uint32_t test_empty_events;
	uint32_t test_stale_events;
	uint32_t test_producer_blocks; /* FLPR-side block count */
	uint32_t test_output_full;     /* FLPR-side output-full count */

	/* FLPR-reported diagnostic counters. */
	uint32_t flpr_notify_rcv;  /* notification received */
	uint32_t flpr_worker_wake; /* ring_process_input() calls */
	uint32_t flpr_consume_ok;  /* slots consumed */
	uint32_t flpr_consume_empty;
	uint32_t flpr_consume_stale;
	uint32_t flpr_produce_ok; /* slots produced to output */
	uint32_t flpr_produce_full;

	/* Latency (cycles, k_cycle_get_32 domain). */
	uint32_t latency_min;   /* minimum roundtrip in cycles */
	uint32_t latency_max;   /* maximum roundtrip */
	uint64_t latency_sum;   /* sum for average */
	uint32_t latency_count; /* number of measurements */

	/* Last acked FLPR stall packed value. */
	uint32_t stall_acked;
};

/**
 * @brief Initialize the acceptance module: register the diagnostic
 *        message handler with the handshake module, initialize the
 *        stall-ACK control state, and zero all state.
 *
 * Called from boot wiring (src/main.c platform_init) under
 * CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS.
 */
void flpr_acceptance_init(void);

/**
 * @brief Snapshot acceptance status.
 */
void flpr_acceptance_get_status(struct flpr_acceptance_status *status);

/**
 * @brief Stall injection: force the cpuapp producer to pretend full.
 * When enabled, every produce_block/produce_asrc call returns FULL
 * without actually filling a slot (counted as backpressure).
 */
void flpr_acceptance_stall_producer(bool stall);

/** True while the cpuapp producer stall is enabled (read by the core
 *  produce paths under CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS). */
bool flpr_acceptance_stall_producer_active(void);

/**
 * @brief Send FLPR stall config via IPC and wait for the exact ACK.
 *
 * Packed data: bits[7:0]=mask, bits[31:8]=duration_ms.  Duration zero
 * means persistent (stops any prior timed stall).  See the core
 * ring manager's pre-R8 contract for stall bits semantics.
 *
 * @param stall_bits  Bitmask of stalls to apply (persistent, duration=0).
 * @param timeout_ms  Max wait for STALL_ACK.
 * @return 0 on success, -ETIMEDOUT if no ACK, -EIO on ACK data mismatch,
 *         -EOVERFLOW when the 16-bit request-token space of the current
 *         FLPR session is exhausted (cleared only by remote restart).
 */
int flpr_acceptance_flpr_stall(uint8_t stall_bits, uint32_t timeout_ms);

/**
 * @brief Send timed FLPR stall config via IPC (auto-clear duration).
 *
 * Same as flpr_acceptance_flpr_stall but the FLPR applies the stall
 * immediately and auto-clears it after @p duration_ms via its timer.
 * The ACK echoes the exact packed value.
 *
 * @param stall_bits  Bitmask of stalls to apply (must be nonzero when
 *                    duration_ms > 0).
 * @param duration_ms Auto-clear duration in milliseconds (1 .. 0x00FFFFFF).
 * @param timeout_ms  Max wait for STALL_ACK from FLPR.
 * @return 0 on success, -ETIMEDOUT, -EIO, -EOVERFLOW, or -EINVAL on a
 *         zero-mask-with-duration or over-max duration.
 */
int flpr_acceptance_flpr_stall_timed(uint8_t stall_bits, uint32_t duration_ms, uint32_t timeout_ms);

/**
 * @brief Last acked FLPR stall packed value for diagnostics.
 */
uint32_t flpr_acceptance_flpr_stall_acked(void);

/**
 * @brief Run ring throughput test with independent payload verification.
 *
 * Generates deterministic payload from sequence number, produces into
 * the input ring via the core produce_block, notifies FLPR, drains the
 * output ring by consuming blocks and independently regenerating the
 * expected payload for memcmp verification.  Tracks CRC and payload
 * errors.  Sends RING_TEST_START/STOP and aggregates the FLPR reports.
 *
 * @param block_count  Number of blocks to transfer.
 * @param timeout_ms   Maximum duration in milliseconds.
 * @param out          Filled with final acceptance status on return.
 * @return 0 on success (all gates), -1 on failure, -EAGAIN when rings
 *         are not initialized/epoch 0, -EBUSY when a test is active.
 */
int flpr_acceptance_test_run(uint32_t block_count, uint32_t timeout_ms,
			     struct flpr_acceptance_status *out);

/**
 * @brief Rate-limited variant of flpr_acceptance_test_run().
 *
 * @param rate_per_sec  Maximum blocks per second (0 = unlimited).
 */
int flpr_acceptance_test_run_rate(uint32_t block_count, uint32_t timeout_ms, uint32_t rate_per_sec,
				  struct flpr_acceptance_status *out);

/**
 * @brief Probe: produce a slot with stale epoch directly into the
 *        OUTPUT ring.  Bypasses normal epoch validation so the consumer
 *        sees ESTALE.  Acceptance/test-use only — not production.
 *
 * @param stale_epoch  An epoch value that does NOT match the current epoch.
 * @return 0 on success, negative on error (-EAGAIN uninitialized,
 *         -EINVAL zero stale epoch).
 */
int flpr_acceptance_produce_stale_test(uint32_t stale_epoch);

/**
 * @brief Start stress test: send STRESS_PING messages, count PONG
 *        replies (moved from the core handshake; the PONG dispatch
 *        arrives through the registered diagnostic handler).
 *
 * Uses stop-and-wait with 200 ms timeout per ping.  Runs synchronously
 * in calling thread context.  Safe to call from shell or test thread
 * only.  Send failures count stress_err_send (and, via the shared
 * flpr_handshake_send_msg() seam, the handshake err_send counter).
 *
 * @param count  Number of ping/pong to attempt (clamped to 1..1,000,000).
 * @param out    Filled with results on return (even on early timeout).
 */
void flpr_acceptance_stress(uint32_t count, struct flpr_status *out);

/** True while a stress run is active (shell pre-check). */
bool flpr_acceptance_stress_active(void);

/** Copy the current stress state into the shared flpr_status stress
 *  fields (for `flpr status` merge in the production shell). */
void flpr_acceptance_stress_snapshot(struct flpr_status *out);

/**
 * @brief Send a fault-hang request to FLPR and wait for ACK.
 *
 * Blocks up to @p timeout_ms for FAULT_HANG_ACK (dispatched through the
 * diagnostic handler).  After ACK, FLPR disables interrupts and spins
 * forever — ring and heartbeat stop, health transitions to unhealthy.
 *
 * @param timeout_ms  Maximum wait for ACK (typically 500 ms).
 * @return 0 on ACK received, -ETIMEDOUT on timeout, -EIO on send failure.
 */
int flpr_acceptance_send_fault_hang(uint32_t timeout_ms);

/* ── Gate 1–6 acceptance run ─────────────────────────────────────── */

/** Print severity for the gate runner output sink. */
enum flpr_acceptance_print_level {
	FLPR_ACC_PRINT_NORMAL = 0,
	FLPR_ACC_PRINT_WARN,
	FLPR_ACC_PRINT_ERROR,
};

/** Output sink: the acceptance module formats each complete line and
 *  hands it to the callback (shell maps to shell_print/warn/error). */
typedef void (*flpr_acceptance_print_t)(void *ctx, enum flpr_acceptance_print_level lvl,
					const char *line);

/**
 * @brief Run the full acceptance gate sequence (gates 1–6) exactly as
 *        the R4 acceptance shell did, printing through @p print.
 *
 * @param count  Block count for gate 1 (1..10,000,000).
 * @param ctx    Opaque context passed to @p print (the shell).
 * @param print  Output sink (never NULL).
 * @return 0 when every gate passed ("ACCEPTANCE PASSED — all gates
 *         clear"), -1 when any gate failed ("ACCEPTANCE FAILED").
 */
int flpr_acceptance_run_gates(uint32_t count, void *ctx, flpr_acceptance_print_t print);

/* ── Diagnostic counter hooks invoked from core production paths ────
 * Called by src/flpr_ring_mgr.c under CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS
 * only; config-off core builds contain no acceptance symbols.  Each
 * hook takes the acceptance spinlock (leaf) — callers may hold
 * ring_data_lock. */

/** True while the acceptance ring test is active (enables CRC/payload
 *  verification and latency accounting in the core consume paths). */
bool flpr_acceptance_test_active(void);

/** A block was consumed by a core consume path (test_blocks_recv). */
void flpr_acceptance_note_recv(void);

/** Core produce hit ENOSPC (test_full_events). */
void flpr_acceptance_note_full(void);

/** Core consume hit ESTALE (test_stale_events). */
void flpr_acceptance_note_stale(void);

/** Core produce was stalled (test_backpressure). */
void flpr_acceptance_note_backpressure(void);

/** Core consume CRC verification mismatch (test_crc_errors). */
void flpr_acceptance_note_crc_err(void);

/** Core consume payload verification mismatches (test_payload_errors). */
void flpr_acceptance_note_payload_err(uint32_t frame_errors);

/** Core consume latency measurement (latency_*). */
void flpr_acceptance_note_latency(uint32_t latency_cycles);

/** Core consumer notification carried an FLPR block count
 *  (test_producer_blocks). */
void flpr_acceptance_note_flpr_blocks(uint32_t blocks);

/** Remote restart: reset acceptance counters/latency/stall state (the
 *  control-ACK token reset is owned by flpr_control_ack_reset_session,
 *  called by flpr_ring_mgr_remote_restarted). */
void flpr_acceptance_remote_restarted(void);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_ACCEPTANCE_H_ */
