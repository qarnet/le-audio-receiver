/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP side of shared PCM ring management for FLPR transport.
 * Ring addresses are resolved from devicetree at init, not hardcoded.
 *
 * IPC notifications: this module registers callback with the handshake
 * module for FLPR_MSG_RING_CONSUMER (FLPR notifies CPUAPP that output
 * data is available).  The ring manager sends FLPR_MSG_RING_PRODUCER
 * through the handshake module's send function to notify FLPR.
 */

#ifndef FLPR_RING_MGR_H_
#define FLPR_RING_MGR_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pure pacing calculation: target elapsed ms for N blocks at given rate.
 *
 * Returns @p blocks_sent * 1000 / @p rate_per_sec.  Uses 64-bit multiply
 * to avoid overflow at high block counts; safe for up to 2^32 blocks.
 *
 * Unit-testable with no kernel/hardware dependency.  The return can be
 * compared against an actual uptime delta to decide whether to sleep.
 *
 * Example:
 *   100 blk @ 100/s  →   100 * 1000 / 100 = 1000 ms
 *   6000 blk @ 100/s →  6000 * 1000 / 100 = 60000 ms
 *   100000 blk @ 100/s → 100000 * 1000 / 100 = 1000000 ms
 */
static inline uint64_t flpr_rate_limit_target_ms(uint64_t blocks_sent, uint32_t rate_per_sec)
{
	if (rate_per_sec == 0) {
		return 0;
	}
	return (blocks_sent * 1000ULL) / (uint64_t)rate_per_sec;
}

/* Outcome of a produce attempt (for test use).
 * Uses POSIX errno values for consistency. */
enum flpr_produce_result {
	FLPR_PRODUCE_OK = 0,        /* success */
	FLPR_PRODUCE_FULL = -28,    /* -ENOSPC: ring full */
	FLPR_PRODUCE_INVALID = -22, /* -EINVAL: bad params (valid_frames, flags) */
};

/* Outcome of a consume attempt. */
enum flpr_consume_result {
	FLPR_CONSUME_OK = 0,        /* success */
	FLPR_CONSUME_EMPTY = -2,    /* -ENOENT: ring empty */
	FLPR_CONSUME_STALE = -116,  /* -ESTALE: stale epoch */
	FLPR_CONSUME_INVALID = -22, /* -EINVAL: bad slot metadata */
};

/** Snapshot of ring and test status for shell display. */
struct flpr_ring_status {
	bool initialized;
	uint32_t epoch;

	/* Input ring (CPUAPP → FLPR). */
	uint32_t in_producer;
	uint32_t in_consumer;
	uint32_t in_used;  /* flpr_ring_used */
	uint32_t in_space; /* flpr_ring_space */
	uint32_t in_epoch;

	/* Output ring (FLPR → CPUAPP). */
	uint32_t out_producer;
	uint32_t out_consumer;
	uint32_t out_used;
	uint32_t out_space;
	uint32_t out_epoch;

	/* Diagnostic counters (CPUAPP-local). */
	uint32_t notify_sent;
	uint32_t notify_err;
	uint32_t sem_gives; /* from FLPR → CPUAPP ring consumer notifications */
	uint32_t sem_takes;
	uint32_t stale_notify; /* Stage 2: notifications with wrong epoch rejected */
	uint32_t sem_drained;  /* Stage 2: consume_sem tokens drained at reset */

	/* Test */
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
};

/**
 * @brief Callback type for ring consumer notifications.
 * CPUAPP registers this; called from IPC context when FLPR sends
 * FLPR_MSG_RING_CONSUMER (output ring has data).
 */
typedef void (*flpr_ring_consume_cb_t)(void *user_data);

/**
 * @brief Initialize PCM rings in shared memory.
 *
 * Resolves ring addresses from devicetree, does BUILD_ASSERT for size
 * and alignment.  Registers IPC receive callback for RING_RESET_ACK,
 * RING_CONSUMER, RING_TEST_REPORT messages with the handshake module.
 *
 * May be called multiple times safely.  Requires FLPR to be ready/acked
 * before IPC messages will be delivered.
 *
 * @return 0 on success, -ENODEV if DT node missing/device not ready.
 */
int flpr_ring_mgr_init(void);

/**
 * @brief Register callback for output-ring data availability.
 * Called from IPC context when FLPR publishes output data.
 */
void flpr_ring_mgr_set_consume_cb(flpr_ring_consume_cb_t cb, void *user_data);

/**
 * @brief Coordinated two-phase reset: CPUAPP proposes epoch to FLPR
 *        via IPC RING_RESET, waits for RING_RESET_ACK, then applies.
 *
 * @param new_epoch  Non-zero epoch (if 0, one is generated from k_cycle_get_32).
 * @param timeout_ms Maximum wait for ACK.
 * @return 0 on success, -ETIMEDOUT if no ACK, -EIO on send failure.
 */
int flpr_ring_mgr_coordinated_reset(uint32_t new_epoch, uint32_t timeout_ms);

/**
 * @brief Reset both rings locally (caller must coordinate with FLPR).
 *
 * @param new_epoch  Non-zero epoch.
 * @return 0 on success, -EINVAL on bad epoch.
 */
int flpr_ring_mgr_reset(uint32_t new_epoch);

/**
 * @brief Get ring and test status snapshot.
 */
void flpr_ring_mgr_get_status(struct flpr_ring_status *status);

/**
 * @brief Produce a PCM block into the input ring.
 *
 * CRC is computed over valid_frames × 4 bytes (stereo 16-bit),
 * NOT over the full payload capacity.
 *
 * CPU timestamp (k_cycle_get_32) captured at produce time for
 * roundtrip latency measurement.
 *
 * @param pcm_data        Interleaved stereo 16-bit PCM (may be NULL).
 * @param valid_frames    Valid stereo frames (≤ FLPR_RING_PAYLOAD_MAX_INPUT).
 * @param sequence        Monotonic frame counter.
 * @param correction_ppm  Drift correction at capture time.
 * @param compute_crc     If true, compute CRC32 over valid payload bytes.
 * @return FLPR_PRODUCE_OK or FLPR_PRODUCE_FULL.
 */
enum flpr_produce_result flpr_ring_mgr_produce_block(const uint8_t *pcm_data, uint16_t valid_frames,
						     uint32_t sequence, int32_t correction_ppm,
						     bool compute_crc);

/**
 * @brief Consume a PCM block from the output ring.
 *
 * CRC is verified over valid_frames × 4 bytes.  Also reads
 * cpu_timestamp from slot metadata for latency measurement.
 *
 * @param pcm_out          Buffer for payload (may be NULL to skip).
 * @param valid_frames_out Optional valid frame count output.
 * @param sequence_out     Optional sequence number output.
 * @param crc32_out        Optional CRC-32 output.
 * @param latency_cycles_out Optional CPU cycle latency output (0 if unused).
 * @return FLPR_CONSUME_OK, FLPR_CONSUME_EMPTY, or FLPR_CONSUME_STALE.
 */
enum flpr_consume_result flpr_ring_mgr_consume_block(uint8_t *pcm_out, uint16_t *valid_frames_out,
						     uint32_t *sequence_out, uint32_t *crc32_out,
						     uint32_t *latency_cycles_out);

/**
 * @brief Send RING_PRODUCER notification to FLPR.
 * Tells FLPR that input ring has new data.  Returns 0 on success.
 */
int flpr_ring_mgr_notify_producer(void);

/**
 * @brief Stall injection: force producer to pretend full.
 * When enabled, every produce_block call returns FULL without
 * actually filling a slot.
 */
void flpr_ring_mgr_stall_producer(bool stall);

/**
 * @brief Send FLPR stall config via IPC.
 *
 * Packed data: bits[7:0]=mask, bits[31:8]=duration_ms.
 * Duration zero means persistent (stops any prior timed stall).
 *
 * Stall bits:
 *   FLPR_STALL_CONSUMER_INPUT (0x01): FLPR stops consuming input ring.
 *   FLPR_STALL_PRODUCER_OUTPUT (0x02): FLPR stops producing output ring.
 *   Bit 0 → clear stall (resume normal operation).
 *
 * @param stall_bits  Bitmask of stalls to apply (persistent, duration=0).
 * @param timeout_ms  Max wait for STALL_ACK.
 * @return 0 on success, negative on error.
 */
int flpr_ring_mgr_flpr_stall(uint8_t stall_bits, uint32_t timeout_ms);

/**
 * @brief Send timed FLPR stall config via IPC (Stage 2).
 *
 * Same as flpr_ring_mgr_flpr_stall but with an auto-clear duration.
 * After the FLPR receives this command, the stall is applied immediately
 * and automatically cleared after @p duration_ms by the FLPR timer.
 * The ACK echoes the exact packed value; caller can verify.
 *
 * @param stall_bits  Bitmask of stalls to apply (must be nonzero).
 * @param duration_ms Auto-clear duration in milliseconds (1 .. 0x00FFFFFF).
 * @param timeout_ms  Max wait for STALL_ACK from FLPR.
 * @return 0 on success, negative on error.
 */
int flpr_ring_mgr_flpr_stall_timed(uint8_t stall_bits, uint32_t duration_ms, uint32_t timeout_ms);

/**
 * @brief Get last acked FLPR stall packed value for diagnostics.
 */
uint32_t flpr_ring_mgr_flpr_stall_acked(void);

/**
 * @brief Run ring throughput test with independent payload verification.
 *
 * Generates deterministic payload from sequence number, produces
 * into input ring via produce_block, notifies FLPR, drains output ring
 * by consuming blocks and independently regenerating expected payload
 * for memcmp verification.  Tracks CRC errors AND payload errors.
 *
 * Staleness protocol: notify sent AFTER slot publish; no duplicate
 * same sequence.  Final drain waits until recv == sent or global timeout.
 *
 * Latency: cpu_timestamp captured at produce, compared at consume.
 * Reports min/max/avg in k_cycle_get_32 cycles.
 *
 * Returns nonzero if sent != target OR recv != target OR any errors.
 *
 * @param block_count  Number of blocks to transfer.
 * @param timeout_ms   Maximum duration in milliseconds.
 * @param out          Filled with final test status on return.
 * @return 0 on success (all gates), -1 on failure.
 */
int flpr_ring_mgr_test_run(uint32_t block_count, uint32_t timeout_ms, struct flpr_ring_status *out);

/**
 * @brief Run ring throughput test with rate limiting.
 *
 * Identical to flpr_ring_mgr_test_run() but limits production to at most
 * @p rate_per_sec blocks per second (throttled via k_msleep between batches).
 * Pass 0 for unlimited (same as test_run).  Useful for concurrent testing
 * where the ring test must not saturate the link.
 *
 * @param rate_per_sec  Maximum blocks per second (0 = unlimited).
 */
int flpr_ring_mgr_test_run_rate(uint32_t block_count, uint32_t timeout_ms, uint32_t rate_per_sec,
				struct flpr_ring_status *out);

/**
 * @brief Probe: produce a slot with stale epoch directly into the OUTPUT ring.
 * Bypasses normal epoch validation so consumer will see ESTALE.
 * Test-use only — not for production data paths.
 *
 * @param stale_epoch  An epoch value that does NOT match the current epoch.
 * @return 0 on success, negative on error.
 */
int flpr_ring_mgr_produce_stale_test(uint32_t stale_epoch);

/**
 * @brief Wait on the consume semaphore (with timeout) for FLPR output.
 *
 * Used by acceptance suite to wait for output data without busy-polling.
 * Semaphore is given by IPC callback when FLPR publishes output ring data.
 *
 * @param timeout_ms  Maximum wait time in milliseconds.
 * @return 0 on semaphore acquired, nonzero on timeout.
 */
int flpr_ring_mgr_wait_consume(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_RING_MGR_H_ */
