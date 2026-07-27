/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Audio offload interface — transport decoded PCM through FLPR identity
 * loopback (nRF54L15) or direct bypass (nRF5340).
 *
 * On nRF54L15, submits a stereo PCM block to the FLPR input ring, waits
 * for the identity-copied output, and validates it.  On any fault
 * (timeout, ring full, stale epoch, CRC mismatch, unhealthy FLPR), the
 * call returns an error and the caller falls back to the original PCM —
 * the ASRC and I2S path in audio_i2s.c consumes the original input.
 *
 * Synchronous deadline is derived from measured Stage 1 max RTT of
 * ~5.3 ms.  If real Mode B measurements prove the synchronous deadline
 * too tight, the implementation switches to a one-block pipeline.
 *
 * nRF5340 compile-time bypass: all functions are identity no-ops with
 * zero cost overhead.
 */

#ifndef AUDIO_OFFLOAD_H
#define AUDIO_OFFLOAD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Status snapshot for shell / instrumentation. */
struct audio_offload_status {
	/* Transport state. */
	bool initialized;
	bool healthy;   /* FLPR healthy + ring epoch valid */
	uint32_t epoch; /* current stream epoch */

	/* Block counters. */
	uint32_t submit_count;      /* total submit attempts */
	uint32_t success_count;     /* identity loopback OK */
	uint32_t timeout_count;     /* deadline exceeded */
	uint32_t full_count;        /* input ring full */
	uint32_t stale_count;       /* stale epoch rejection */
	uint32_t seq_fault_count;   /* sequence mismatch */
	uint32_t frame_fault_count; /* wrong frame count */
	uint32_t crc_fault_count;   /* CRC mismatch */
	uint32_t fallback_count;    /* total fallback events */
	uint32_t recovery_count;    /* offload re-enabled after fault */

	/* Latency (k_cycle_get_32 cycles). */
	uint32_t rtt_min_cycles;
	uint32_t rtt_max_cycles;
	uint64_t rtt_sum_cycles;
	uint32_t rtt_count;

	/* Last error for debugging. */
	int last_error;
	uint32_t last_error_seq;
};

/**
 * @brief Initialize the audio offload subsystem.
 *
 * nRF54L15: initialises FLPR ring manager (resolves DT, registers IPC
 * handlers).  Non-blocking; returns before FLPR handshake completes.
 * nRF5340: no-op, always returns 0.
 *
 * @return 0 on success, negative errno on failure.
 */
int audio_offload_init(void);

/**
 * @brief Start a new audio stream epoch.
 *
 * Coordinates ring reset with FLPR (new epoch via IPC RING_RESET).
 * Must be called AFTER FLPR handshake is complete (FLPR acked).
 * nRF5340: no-op.
 */
void audio_offload_stream_start(void);

/**
 * @brief Stop the audio offload pipeline.
 *
 * Called on stream disconnect.  Cancels any pending wait and
 * marks the pipeline as inactive so subsequent submits are
 * rejected cleanly.  nRF5340: no-op.
 */
void audio_offload_stream_stop(void);

/**
 * @brief Check whether the offload path is healthy.
 *
 * nRF54L15: true when FLPR is healthy AND ring epoch is non-zero.
 * nRF5340: always true (bypass is always available).
 */
bool audio_offload_is_healthy(void);

/**
 * @brief Submit a stereo PCM block through the offload pipeline.
 *
 * nRF54L15 path:
 *   1. Produce block into input ring (FLPR_RING_PAYLOAD_MAX_INPUT frames)
 *   2. Notify FLPR
 *   3. Wait synchronously for output (deadline bounded)
 *   4. Consume output block, validate sequence + frame count + CRC
 *   5. Copy output PCM to @p output
 *
 * On any fault the function returns a negative errno.  The caller
 * MUST use the original @p input PCM as fallback — do not discard audio.
 *
 * On nRF5340 (bypass): copies input to output unchanged, returns 0.
 *
 * @param input           Input PCM (interleaved stereo 16-bit).
 * @param samples         Number of int16_t samples (must be even).
 * @param sequence        Monotonic stream frame counter.
 * @param correction_ppm  Current drift correction (metadata only).
 * @param output          Output buffer (same size as input).  On success
 *                        contains the FLPR-identity-copied PCM.
 *                        On failure the buffer is UNTOUCHED.
 * @return 0 on success,
 *         -ETIMEDOUT if FLPR did not respond within deadline,
 *         -ENOSPC if input ring is full,
 *         -ESTALE if stale epoch,
 *         -EFAULT if sequence/frame/CRC mismatch,
 *         -EAGAIN if offload unhealthy (fallback).
 */
int audio_offload_submit(const int16_t *input, size_t samples, uint32_t sequence,
			 int32_t correction_ppm, int16_t *output);

/**
 * @brief Get a snapshot of offload status/instrumentation.
 */
void audio_offload_get_status(struct audio_offload_status *status);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_OFFLOAD_H */
