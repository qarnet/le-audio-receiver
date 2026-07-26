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
 * When CONFIG_AUDIO_PERF_MEASUREMENT is disabled, all public functions
 * compile to static inline no-ops that the compiler removes entirely.
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

/** Per-path cycle accumulator snapshot. */
struct audio_perf_path_snapshot {
	uint32_t count;             /* number of samples */
	uint64_t total_cycles;      /* sum of all elapsed cycles */
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

#if defined(CONFIG_AUDIO_PERF_MEASUREMENT)

/* ── Enabled: real implementations (audio_perf.c) ──────────────── */

uint32_t audio_perf_cycle_start(void);
void audio_perf_cycle_end(uint32_t start, enum audio_perf_path path);
void audio_perf_queue_sample(int slab_free, size_t output_frames);
void audio_perf_push_failure(void);
void audio_perf_repeat_fallback(void);
void audio_perf_snapshot(struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS],
			 struct audio_perf_queue_snapshot *queue);
void audio_perf_reset(void);

/**
 * Deterministic test injection: record elapsed cycles for @p path
 * without reading the hardware cycle counter.  Only call from unit
 * tests; never used in production data-path code.
 */
void audio_perf_test_inject_cycles(enum audio_perf_path path, uint32_t elapsed);

#else /* !CONFIG_AUDIO_PERF_MEASUREMENT */

/* ── Disabled: inline no-ops that the compiler removes ──────────── */

static inline uint32_t audio_perf_cycle_start(void)
{
	return 0;
}
static inline void audio_perf_cycle_end(uint32_t start, enum audio_perf_path path)
{
	(void)start;
	(void)path;
}
static inline void audio_perf_queue_sample(int slab_free, size_t output_frames)
{
	(void)slab_free;
	(void)output_frames;
}
static inline void audio_perf_push_failure(void)
{
}
static inline void audio_perf_repeat_fallback(void)
{
}
static inline void audio_perf_snapshot(struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS],
				       struct audio_perf_queue_snapshot *queue)
{
	for (int i = 0; i < AUDIO_PERF_NUM_PATHS; i++) {
		paths[i].count = 0;
		paths[i].total_cycles = 0;
		paths[i].max_cycles = 0;
		paths[i].deadline_overruns = 0;
	}
	if (queue) {
		queue->slab_min_free = 0;
		queue->slab_max_free = 0;
		queue->push_failures = 0;
		queue->repeat_fallback_count = 0;
		queue->output_frames_min = 0;
		queue->output_frames_max = 0;
		queue->output_blocks = 0;
	}
}
static inline void audio_perf_reset(void)
{
}
static inline void audio_perf_test_inject_cycles(enum audio_perf_path path, uint32_t elapsed)
{
	(void)path;
	(void)elapsed;
}

#endif /* CONFIG_AUDIO_PERF_MEASUREMENT */

#endif /* AUDIO_PERF_H */
