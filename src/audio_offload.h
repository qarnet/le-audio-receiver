/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Audio offload interface — transport decoded PCM through FLPR identity
 * loopback (nRF54L15) or direct bypass (nRF5340).
 *
 * On nRF54L15, submits a stereo PCM block to the FLPR input ring, waits
 * for the identity-copied output, and validates it (CRC + payload memcmp).
 * On any fault the call returns an error, the output buffer is UNTOUCHED,
 * and the caller falls back to the original PCM.
 *
 * Submit is serialised by a mutex — only one block in-flight at a time.
 * Scratch output buffer is module-static (no stack allocation).  BT
 * callback stack headroom verified via build-time map analysis.
 *
 * Fault state machine: ANY timeout / CRC / payload / seq / frame / empty /
 * stale fault marks offload unhealthy immediately.  Bounded recovery
 * work (k_work_delayable) runs outside BT callback with backoff.
 * Recovery success starts a probation window; faults during probation
 * escalate backoff and count as relapses.  Only after 100 consecutive
 * successes does probation clear and escalation reset.  Max tries
 * enters FALLBACK cleanly — a fresh stream_start resets the policy.
 * While recovering, submits return -EAGAIN and caller uses CPUAPP ASRC.
 * Stream stop cancels recovery and resets generation.
 *
 * nRF5340 compile-time bypass: all functions are identity no-ops with
 * zero cost overhead.
 */

#ifndef AUDIO_OFFLOAD_H
#define AUDIO_OFFLOAD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "audio_asrc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Offload pipeline state. */
enum audio_offload_state {
	AUDIO_OFFLOAD_STOPPED = 0, /* not active */
	AUDIO_OFFLOAD_PREPARING,   /* epoch reset in progress */
	AUDIO_OFFLOAD_ACTIVE,      /* healthy, accepting submits */
	AUDIO_OFFLOAD_FALLBACK,    /* never made it to active (init failure) */
	AUDIO_OFFLOAD_RECOVERING,  /* faulted, recovery work pending */
};

/** Status snapshot for shell / instrumentation. */
struct audio_offload_status {
	/* Transport state. */
	bool initialized;
	bool healthy;
	uint32_t epoch;
	uint32_t generation; /* monotonic: bumped on stream start + recovery */
	enum audio_offload_state state;

	/* Block counters. */
	uint32_t submit_count;
	uint32_t success_count;
	uint32_t fallback_count; /* total: every nonzero submit counted once here + once category */
	uint32_t timeout_count;
	uint32_t full_count;
	uint32_t stale_count;
	uint32_t seq_fault_count;
	uint32_t frame_fault_count;
	uint32_t crc_fault_count;
	uint32_t payload_fault_count;  /* memcmp mismatch */
	uint32_t recovery_attempts;    /* lifetime: total recovery cycles attempted */
	uint32_t recovery_fail_count;  /* recoveries that failed / retried out */
	uint32_t recovery_relapses;    /* faults occurring during probation window */
	uint32_t probation_success;    /* consecutive successes since last recovery */
	bool probation_active;         /* true while probation window open */
	uint32_t max_exhaustion_count; /* times MAX_TRIES boundary was reached */
	uint32_t probation_cleared;    /* times probation completed (100+ consecutive) */
	uint32_t busy_count;           /* mutex-timeout rejections */

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
 * nRF54L15: initialises FLPR ring manager.  Non-blocking.
 * nRF5340: no-op, always returns 0.
 *
 * @return 0 on success, negative errno on failure.
 */
int audio_offload_init(void);

/**
 * @brief Start a new audio stream epoch.
 *
 * Initiates non-blocking coordinated ring reset with FLPR.
 * Sets state to PREPARING; while PREPARING, submits return -EAGAIN
 * and caller uses CPUAPP ASRC fallback.  When epoch reset completes
 * (via FLPR ACK in IPC callback), state transitions to ACTIVE.
 *
 * nRF5340: no-op.
 */
void audio_offload_stream_start(void);

/**
 * @brief Stop the audio offload pipeline.
 *
 * Cancels any pending recovery work, drains/cancels late output,
 * increments generation so late output from prior epoch is rejected.
 * Sets state to STOPPED.
 * nRF5340: no-op.
 */
void audio_offload_stream_stop(void);

/**
 * @brief Check whether the offload path is healthy.
 *
 * nRF54L15: true when state == ACTIVE.
 * nRF5340: always true (bypass is always available).
 */
bool audio_offload_is_healthy(void);

/**
 * @brief Submit a stereo PCM block through the offload pipeline.
 *
 * Mutex-serialised — only one block in-flight at a time.
 *
 * nRF54L15 path:
 *   1. Validate args (null, sample count, state)
 *   2. Produce block into input ring WITH CRC
 *   3. Notify FLPR
 *   4. Wait synchronously for output (deadline 8 ms)
 *   5. Consume output, validate epoch + sequence + frame count + CRC (recomputed)
 *   6. memcmp output payload against original input — bit-exact identity
 *   7. On all checks pass: copy verified payload to output, count success
 *   8. On ANY fault: poison healthy, schedule recovery, return error,
 *      output buffer UNTOUCHED
 *
 * On any fault the function returns a negative errno.  The caller
 * MUST use the original @p input PCM as fallback — do not discard audio.
 *
 * On nRF5340 (bypass): copies input to output unchanged, returns 0.
 *
 * @param input           Input PCM (interleaved stereo 16-bit).
 * @param samples         Number of int16_t samples (must be even, 960).
 * @param sequence        Monotonic stream frame counter.
 * @param correction_ppm  Current drift correction (metadata only).
 * @param output          Output buffer (same size as input).  On success
 *                        contains the FLPR-identity-copied PCM.
 *                        On failure the buffer is UNTOUCHED.
 * @return 0 on success,
 *         -ETIMEDOUT if FLPR did not respond within deadline,
 *         -ENOSPC if input ring is full,
 *         -ESTALE if stale epoch,
 *         -EFAULT if sequence/frame/CRC/payload mismatch,
 *         -EAGAIN if offload unhealthy/recovering/preparing (fallback).
 */
int audio_offload_submit(const int16_t *input, size_t samples, uint32_t sequence,
			 int32_t correction_ppm, int16_t *output);

/**
 * @brief Get a snapshot of offload status/instrumentation.
 */
void audio_offload_get_status(struct audio_offload_status *status);

/* ── Stage 3B: ASRC offload ──────────────────────────────────────── */

/** Typed result from FLPR ASRC processing. */
struct audio_offload_asrc_result {
	uint16_t output_frames;             /* 1..481 if ok, 0 on error */
	struct audio_asrc_state post_state; /* post-process continuity state */
	uint32_t processing_cycles;         /* FLPR k_cycle_get_32 elapsed */
};

/**
 * @brief Route decoded PCM through FLPR ASRC offload.
 *
 * nRF54L15 only (compile-time guarded by CONFIG_AUDIO_OFFLOAD_ASRC).
 * On nRF5340: compile-time stub returns -ENOSYS.
 *
 * Produces 480 stereo frames with typed ASRC pre-state and drift ppm,
 * waits for FLPR to process, consumes typed result including variable
 * output frame count, post-process ASRC state, and processing cycles.
 *
 * Validates sequence, status=0, ASRC flag, frame range 1..481,
 * correction echo, output payload CRC, typed post-state import,
 * unchanged step_base, and reserved bytes.  On any fault, output
 * and result are untouched.
 *
 * Error output from FLPR (status < 0, frames=0) is a valid transport
 * response returned as success with result.output_frames=0.
 *
 * @param input           Input PCM (interleaved stereo 16-bit, 960 samples).
 * @param input_frames    Must equal 480.
 * @param sequence        Monotonic stream frame counter.
 * @param correction_ppm  Current drift correction.
 * @param pre_state       ASRC continuity state at submission time.
 * @param output          Output buffer (≥ 481 × 4 = 1924 bytes).
 * @param output_capacity Capacity in stereo frames (≥ 481).
 * @param result          On success: output_frames, post_state, processing_cycles.
 *                        On failure: untouched.
 * @return 0 on success, negative errno on any fault.
 */
int audio_offload_process_asrc(const int16_t *input, uint16_t input_frames, uint32_t sequence,
			       int32_t correction_ppm, const struct audio_asrc_state *pre_state,
			       int16_t *output, uint16_t output_capacity,
			       struct audio_offload_asrc_result *result);

/** ASRC-specific statistics (supplemental to audio_offload_status). */
struct audio_offload_asrc_stats {
	uint32_t submit_count;   /* ASRC produce calls */
	uint32_t success_count;  /* successful ASRC round-trips */
	uint32_t fallback_count; /* faults that fell back to cpu ASRC */
	uint32_t timeout_count;
	uint32_t full_count;
	uint32_t stale_count;
	uint32_t seq_fault_count;
	uint32_t frame_fault_count; /* frames out of range */
	uint32_t crc_fault_count;
	uint32_t state_fault_count;  /* post-state import rejected */
	uint32_t verify_fault_count; /* shadow-verify mismatch detected */
	uint32_t rtt_min_cycles;
	uint32_t rtt_max_cycles;
	uint64_t rtt_sum_cycles;
	uint32_t rtt_count;
	uint32_t cycles_min; /* FLPR processing_cycles */
	uint32_t cycles_max;
	uint64_t cycles_sum;
	uint32_t cycles_count;
};

/** Get ASRC-specific offload statistics. */
void audio_offload_get_asrc_stats(struct audio_offload_asrc_stats *s);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_OFFLOAD_H */
