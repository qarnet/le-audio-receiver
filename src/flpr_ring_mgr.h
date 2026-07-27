/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP side of shared PCM ring management for FLPR transport.
 */

#ifndef FLPR_RING_MGR_H_
#define FLPR_RING_MGR_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Snapshot of ring and test status for shell display. */
struct flpr_ring_status {
	bool initialized;
	uint32_t epoch;

	/* Input ring (CPUAPP → FLPR). */
	uint32_t in_producer;
	uint32_t in_consumer;
	uint32_t in_epoch;

	/* Output ring (FLPR → CPUAPP). */
	uint32_t out_producer;
	uint32_t out_consumer;
	uint32_t out_epoch;

	/* Test counters. */
	bool test_active;
	uint32_t test_blocks_sent;
	uint32_t test_blocks_recv;
	uint32_t test_crc_errors;
	uint32_t test_full;
	uint32_t test_empty;
	uint32_t test_stale;
};

/**
 * @brief Initialize PCM rings in shared memory.
 *
 * Must be called after flpr_handshake_init() and after FLPR is ready/acked.
 * Uses the same IPC device (ipc0) for control messages.
 *
 * @return 0 on success, -EAGAIN if FLPR not ready, -ENODEV on error.
 */
int flpr_ring_mgr_init(void);

/**
 * @brief Reset both rings to a new stream epoch.
 *
 * Clears indices, sets epoch. Caller must coordinate with FLPR via
 * FLPR_MSG_RING_RESET / RING_RESET_ACK IPC handshake before calling.
 *
 * @param new_epoch  Non-zero epoch agreed with FLPR.
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
 * Single producer (CPUAPP), thread-safe with spinlock.
 *
 * @param pcm_data        Interleaved stereo 16-bit PCM (1920 bytes for 480 frames).
 * @param valid_frames    Number of valid stereo frames (≤ 480).
 * @param sequence        Monotonic frame counter.
 * @param correction_ppm  Drift correction at capture time.
 * @param compute_crc     If true, compute CRC32 over payload.
 * @return 0 on success, -ENOSPC if ring full.
 */
int flpr_ring_mgr_produce_block(const uint8_t *pcm_data, uint16_t valid_frames, uint32_t sequence,
				int32_t correction_ppm, bool compute_crc);

/**
 * @brief Consume a PCM block from the output ring.
 *
 * Single consumer (CPUAPP), thread-safe with spinlock.
 *
 * @param pcm_out          Buffer to receive payload (can be NULL to skip).
 * @param valid_frames_out Optional output for valid frame count.
 * @param sequence_out     Optional output for sequence number.
 * @param crc32_out        Optional output for CRC-32 value.
 * @return 0 on success, -ENOENT if empty, -ESTALE if stale epoch.
 */
int flpr_ring_mgr_consume_block(uint8_t *pcm_out, uint16_t *valid_frames_out,
				uint32_t *sequence_out, uint32_t *crc32_out);

/**
 * @brief Start ring throughput test.
 *
 * @param block_count  Target number of blocks to transfer.
 * @return 0 on success, -EBUSY if test already running.
 */
int flpr_ring_mgr_test_start(uint32_t block_count);

/**
 * @brief Run ring throughput test (blocking).
 *
 * Produces blocks with CRC into input ring, drains output ring,
 * verifies CRC. Runs until block_count reached or timeout.
 *
 * @param block_count  Number of blocks to transfer.
 * @param timeout_ms   Maximum duration in milliseconds.
 * @param out          Filled with final test status on return.
 * @return 0 on success, negative on error.
 */
int flpr_ring_mgr_test_run(uint32_t block_count, uint32_t timeout_ms, struct flpr_ring_status *out);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_RING_MGR_H_ */
