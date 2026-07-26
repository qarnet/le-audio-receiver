/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Low-overhead performance instrumentation.
 *
 * Tracks CPU cycles and queue-level metrics for LC3 decode, volume,
 * sink push, and ISO receive paths.  All per-frame work is add/compare
 * only — no division, no heap, no floating point.  Cycle-to-time
 * conversion and deadline comparison happen at snapshot/print time.
 *
 * Gated behind CONFIG_AUDIO_PERF_MEASUREMENT; compiles to no-ops
 * when disabled.
 */

#ifndef AUDIO_PERF_H
#define AUDIO_PERF_H

#include <stdint.h>
#include <stddef.h>

/* Number of measured code paths */
#define AUDIO_PERF_NUM_PATHS 4

/** Code paths tracked independently. */
enum audio_perf_path {
	AUDIO_PERF_PATH_ISO_RECV = 0, /* stream_recv callback (entire) */
	AUDIO_PERF_PATH_LC3_DECODE,   /* lc3_decode calls */
	AUDIO_PERF_PATH_VOLUME,       /* audio_volume_apply */
	AUDIO_PERF_PATH_SINK_PUSH,    /* audio_sink_push (entire) */
};

/** Per-path cycle accumulator snapshot (all fields 32-bit, no division). */
struct audio_perf_path_snapshot {
	uint32_t count;             /* number of samples */
	uint32_t total_cycles;      /* sum of all elapsed cycles (low 32) */
	uint32_t total_cycles_hi;   /* sum high 32 bits */
	uint32_t max_cycles;        /* largest single elapsed */
	uint32_t deadline_overruns; /* count of samples exceeding deadline */
};

/** Queue / data-path metrics snapshot. */
struct audio_perf_queue_snapshot {
	uint32_t slab_min_free;         /* minimum free slab blocks observed */
	uint32_t slab_max_free;         /* maximum free slab blocks observed */
	uint32_t push_failures;         /* audio_sink_push returned <0 */
	uint32_t repeat_fallback_count; /* packet-repeat events */
	uint32_t output_frames_min;     /* minimum output_frames per push */
	uint32_t output_frames_max;     /* maximum output_frames per push */
	uint32_t output_blocks;         /* number of pushes observed */
};

/* ── Per-path cycle accounting ──────────────────────────────────── */

/**
 * Capture cycle counter for start of a measured region.
 * Returns 0 when CONFIG_AUDIO_PERF_MEASUREMENT is disabled (no overhead).
 */
uint32_t audio_perf_cycle_start(void);

/**
 * Record elapsed cycles for @p path.
 * start must come from a prior audio_perf_cycle_start() call.
 * No-op when CONFIG_AUDIO_PERF_MEASUREMENT is disabled.
 */
void audio_perf_cycle_end(uint32_t start, enum audio_perf_path path);

/* ── Queue / data-path metrics ──────────────────────────────────── */

/**
 * Sample slab free count and output frame count for one push.
 * Call from audio_sink_push() after computing output_frames and
 * reading slab free count.
 */
void audio_perf_queue_sample(int slab_free, size_t output_frames);

/** Record an audio_sink_push failure (any negative return). */
void audio_perf_push_failure(void);

/** Record a packet-repeat fallback event. */
void audio_perf_repeat_fallback(void);

/* ── Snapshot and control ───────────────────────────────────────── */

/**
 * Fill all path snapshots and the queue snapshot in one lock-held read.
 * Safe to call from shell or any thread context concurrently with
 * cycle-end / queue-sample updates.
 */
void audio_perf_snapshot(struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS],
			 struct audio_perf_queue_snapshot *queue);

/** Reset all counters to zero.  Thread-safe. */
void audio_perf_reset(void);

#endif /* AUDIO_PERF_H */
