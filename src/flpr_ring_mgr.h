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

/* Outcome of a produce attempt (for test use). */
enum flpr_produce_result {
	FLPR_PRODUCE_OK = 0,
	FLPR_PRODUCE_FULL = -1,
};

/* Outcome of a consume attempt. */
enum flpr_consume_result {
	FLPR_CONSUME_OK = 0,
	FLPR_CONSUME_EMPTY = -1,
	FLPR_CONSUME_STALE = -2,
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

	/* Test */
	bool test_active;
	uint32_t test_blocks_sent;
	uint32_t test_blocks_recv;
	uint32_t test_crc_errors;
	uint32_t test_seq_gaps;
	uint32_t test_full_events;
	uint32_t test_empty_events;
	uint32_t test_stale_events;
	uint32_t test_producer_blocks; /* FLPR-side block count */
	uint32_t test_output_full;     /* FLPR-side output-full count */
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
 * @param pcm_data        Interleaved stereo 16-bit PCM.
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
 * CRC is verified over valid_frames × 4 bytes.
 *
 * @param pcm_out          Buffer for payload (may be NULL to skip).
 * @param valid_frames_out Optional valid frame count output.
 * @param sequence_out     Optional sequence number output.
 * @param crc32_out        Optional CRC-32 output.
 * @return FLPR_CONSUME_OK, FLPR_CONSUME_EMPTY, or FLPR_CONSUME_STALE.
 */
enum flpr_consume_result flpr_ring_mgr_consume_block(uint8_t *pcm_out, uint16_t *valid_frames_out,
						     uint32_t *sequence_out, uint32_t *crc32_out);

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
 * @brief Run ring throughput test (blocking, with notifications).
 *
 * Produces blocks with CRC into input ring, notifies FLPR,
 * waits for CONSUMER callback to drain output ring, verifies CRC.
 * Runs until block_count reached or timeout.  Total blocks =
 * sent blocks; received is counted from FLPR reports.
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
