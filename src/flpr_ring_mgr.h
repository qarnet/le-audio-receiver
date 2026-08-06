/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP side of shared PCM ring management for FLPR transport.
 * Ring addresses are resolved from devicetree at init, not hardcoded.
 *
 * IPC notifications: this module registers the PRODUCTION handlers with
 * the handshake module for FLPR_MSG_RING_RESET_ACK and
 * FLPR_MSG_RING_CONSUMER (FLPR notifies CPUAPP that output data is
 * available).  The ring manager sends FLPR_MSG_RING_PRODUCER through
 * the handshake module's send function to notify FLPR.  Diagnostic
 * messages (RING_TEST_REPORT, RING_STALL_ACK, STRESS_PONG,
 * FAULT_HANG_ACK) are owned by the acceptance module
 * (src/flpr_acceptance.c) through the handshake diagnostic handler slot;
 * this header is production-only and contains no acceptance-named API.
 *
 * R8: the request/ACK correlation engine (src/flpr_control_ack.c) is the
 * single owner of reset/stall ACK correlation; this module uses it for
 * the coordinated-reset ACK.
 */

#ifndef FLPR_RING_MGR_H_
#define FLPR_RING_MGR_H_

#include <stdint.h>
#include <stdbool.h>

#include "audio_asrc.h"

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

/** Snapshot of production ring status for shell display.  Acceptance
 *  fields (test/stall/FLPR-report/latency) live in
 *  struct flpr_acceptance_status (src/flpr_acceptance.h); the
 *  acceptance shell merges both. */
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

	/* Production notification diagnostics (CPUAPP-local). */
	uint32_t notify_sent;
	uint32_t notify_err;
	uint32_t sem_gives; /* from FLPR → CPUAPP ring consumer notifications */
	uint32_t sem_takes;
	uint32_t stale_notify; /* Stage 2: notifications with wrong epoch rejected */
	uint32_t sem_drained;  /* Stage 2: consume_sem tokens drained at reset */
};

/**
 * @brief Initialize PCM rings in shared memory.
 *
 * Resolves ring addresses from devicetree, does BUILD_ASSERT for size
 * and alignment.  Registers the PRODUCTION IPC receive handlers
 * (RING_RESET_ACK, RING_CONSUMER) with the handshake module.
 *
 * May be called multiple times safely.  Requires FLPR to be ready/acked
 * before IPC messages will be delivered.
 *
 * @return 0 on success, -ENODEV if DT node missing/device not ready.
 */
int flpr_ring_mgr_init(void);

/**
 * @brief Coordinated two-phase reset: CPUAPP proposes epoch to FLPR
 *        via IPC RING_RESET, waits for RING_RESET_ACK, then applies.
 *
 * @param new_epoch  Non-zero epoch (if 0, one is generated from k_cycle_get_32).
 * @param timeout_ms Maximum wait for ACK.
 * @return 0 on success, -ETIMEDOUT if no ACK, -EIO on send failure or ACK
 *         data mismatch, -EOVERFLOW when the 16-bit request-token space of
 *         the current FLPR session is exhausted (cleared only by
 *         flpr_ring_mgr_remote_restarted()).
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
 * @brief Get ring and production status snapshot.
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
 * @brief Wait on the consume semaphore (with timeout) for FLPR output.
 *
 * Used by the offload submit path (and the acceptance ring test) to
 * wait for output data without busy-polling.  Semaphore is given by the
 * IPC callback when FLPR publishes output ring data.
 *
 * @param timeout_ms  Maximum wait time in milliseconds.
 * @return 0 on semaphore acquired, nonzero on timeout.
 */
int flpr_ring_mgr_wait_consume(uint32_t timeout_ms);

/**
 * @brief Reinitialize rings after FLPR remote restart.
 *
 * Callable only while offload is RECOVERING/stopped — no active submit
 * may race this call.  Invalidates local epoch, drains consumer and all
 * registered control-ACK semaphores (reset + stall), reinitializes
 * shared headers and handlers after new READY.  Must be followed by
 * flpr_ring_mgr_coordinated_reset() with nonzero epoch.
 *
 * @return 0 on success, negative errno on failure.
 */
int flpr_ring_mgr_remote_restarted(void);

/* ── Typed ASRC produce / consume ───────────────────────────────────
 *
 * Stage 3B: type-safe wrappers that embed struct audio_asrc_state into
 * slot metadata and read it back post-process.  These preserve the
 * existing produce_block / consume_block transport and tests.
 */

/**
 * @brief Produce a PCM block with typed ASRC pre-state.
 *
 * Wraps flpr_ring_mgr_produce_block() with FLPR_SLOT_FLAG_ASRC_LINEAR
 * set, copies @p pre_state into the slot metadata, and always computes
 * CRC.
 *
 * @param pcm_data        Interleaved stereo 16-bit PCM (480 frames).
 * @param valid_frames    Must equal 480.
 * @param sequence        Monotonic frame counter.
 * @param correction_ppm  Drift correction at capture time.
 * @param pre_state       ASRC continuity state snapshot (24 bytes).
 * @return FLPR_PRODUCE_OK or FLPR_PRODUCE_FULL.
 */
enum flpr_produce_result flpr_ring_mgr_produce_asrc(const int16_t *pcm_data, uint16_t valid_frames,
						    uint32_t sequence, int32_t correction_ppm,
						    const struct audio_asrc_state *pre_state);

/** Typed result from an ASRC consume. */
struct flpr_consume_asrc_result {
	uint16_t output_frames;             /* 1..481 if ok, 0 on error */
	uint32_t sequence;                  /* echoed sequence number */
	uint16_t flags;                     /* slot flags snapshot */
	int32_t correction_ppm;             /* echoed correction ppm */
	uint32_t payload_crc;               /* recomputed CRC over valid payload */
	struct audio_asrc_state post_state; /* post-process continuity state */
	uint32_t processing_cycles;         /* FLPR k_cycle_get_32 elapsed */
	int32_t processing_status;          /* 0 = success, <0 = error */
	uint32_t rtt_cycles;                /* round-trip k_cycle_get_32 latency */
};

/**
 * @brief Consume a PCM block from the output ring with typed ASRC metadata.
 *
 * Validates status=0, ASRC flag, sequence, frame range (1..481).
 * Copies payload, post-state, cycles, RTT into @p result.
 *
 * On any validation failure the caller output and result are UNTOUCHED
 * (except output_frames zeroed).  Error output (status < 0, frames=0)
 * from FLPR is a VALID transport response; error output (status < 0)
 * is returned as CONSUME_OK with result->output_frames = 0 so the
 * caller sees the error distinct from EMPTY/STALE.
 *
 * @param pcm_out          Output buffer for PCM payload (1924 B capacity).
 * @param output_capacity  Capacity in stereo frames (must be ≥ 481).
 * @param result           Filled with typed ASRC result fields.
 * @return FLPR_CONSUME_OK, EMPTY, STALE, or INVALID.
 */
enum flpr_consume_result flpr_ring_mgr_consume_asrc_result(int16_t *pcm_out,
							   uint16_t output_capacity,
							   struct flpr_consume_asrc_result *result);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_RING_MGR_H_ */
