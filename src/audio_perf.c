/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Performance instrumentation — cycle and queue accumulators.
 * Only compiled when CONFIG_AUDIO_PERF_MEASUREMENT is enabled.
 */

#include "audio_perf.h"

#include <zephyr/kernel.h>

#if defined(CONFIG_AUDIO_PERF_MEASUREMENT)

/* Deadline converted from microseconds to cycles once at boot.
 * Lazy-init in first cycle_end call (k_us_to_cyc_ceil32 is cheap).
 */
static uint32_t perf_deadline_cycles;

static void perf_lazy_init_deadline(void)
{
	if (perf_deadline_cycles == 0) {
		perf_deadline_cycles = k_us_to_cyc_ceil32((uint32_t)CONFIG_AUDIO_PERF_DEADLINE_US);
	}
}

/* ── Internal state ─────────────────────────────────────────────── */

struct perf_state {
	struct k_spinlock lock;

	/* Per-path cycle accumulators (64-bit total_cycles avoids overflow) */
	struct {
		uint32_t count;
		uint64_t total_cycles;
		uint32_t max_cycles;
		uint32_t deadline_overruns;
	} paths[AUDIO_PERF_NUM_PATHS];

	/* Queue metrics */
	uint32_t slab_min_free;
	uint32_t slab_max_free;
	bool slab_first; /* first sample initialises min/max */
	uint32_t push_failures;
	uint32_t repeat_fallback_count;
	uint32_t output_frames_min;
	uint32_t output_frames_max;
	uint32_t output_blocks;
};

static struct perf_state perf;

/* ── Public API ─────────────────────────────────────────────────── */

uint32_t audio_perf_cycle_start(void)
{
	return k_cycle_get_32();
}

void audio_perf_cycle_end(uint32_t start, enum audio_perf_path path)
{
	if (path >= AUDIO_PERF_NUM_PATHS) {
		return;
	}

	uint32_t elapsed = k_cycle_get_32() - start;

	perf_lazy_init_deadline();

	k_spinlock_key_t key = k_spin_lock(&perf.lock);

	perf.paths[path].count++;
	perf.paths[path].total_cycles += elapsed;
	if (elapsed > perf.paths[path].max_cycles) {
		perf.paths[path].max_cycles = elapsed;
	}
	if (elapsed > perf_deadline_cycles) {
		perf.paths[path].deadline_overruns++;
	}

	k_spin_unlock(&perf.lock, key);
}

void audio_perf_test_inject_cycles(enum audio_perf_path path, uint32_t elapsed)
{
	if (path >= AUDIO_PERF_NUM_PATHS) {
		return;
	}

	perf_lazy_init_deadline();

	k_spinlock_key_t key = k_spin_lock(&perf.lock);

	perf.paths[path].count++;
	perf.paths[path].total_cycles += elapsed;
	if (elapsed > perf.paths[path].max_cycles) {
		perf.paths[path].max_cycles = elapsed;
	}
	if (elapsed > perf_deadline_cycles) {
		perf.paths[path].deadline_overruns++;
	}

	k_spin_unlock(&perf.lock, key);
}

void audio_perf_queue_sample(int slab_free, size_t output_frames)
{
	k_spinlock_key_t key = k_spin_lock(&perf.lock);

	perf.output_blocks++;

	if (perf.slab_first) {
		perf.slab_min_free = (uint32_t)slab_free;
		perf.slab_max_free = (uint32_t)slab_free;
		perf.output_frames_min = (uint32_t)output_frames;
		perf.output_frames_max = (uint32_t)output_frames;
		perf.slab_first = false;
	} else {
		if ((uint32_t)slab_free < perf.slab_min_free) {
			perf.slab_min_free = (uint32_t)slab_free;
		}
		if ((uint32_t)slab_free > perf.slab_max_free) {
			perf.slab_max_free = (uint32_t)slab_free;
		}
		if ((uint32_t)output_frames < perf.output_frames_min) {
			perf.output_frames_min = (uint32_t)output_frames;
		}
		if ((uint32_t)output_frames > perf.output_frames_max) {
			perf.output_frames_max = (uint32_t)output_frames;
		}
	}

	k_spin_unlock(&perf.lock, key);
}

void audio_perf_push_failure(void)
{
	k_spinlock_key_t key = k_spin_lock(&perf.lock);

	perf.push_failures++;

	k_spin_unlock(&perf.lock, key);
}

void audio_perf_repeat_fallback(void)
{
	k_spinlock_key_t key = k_spin_lock(&perf.lock);

	perf.repeat_fallback_count++;

	k_spin_unlock(&perf.lock, key);
}

void audio_perf_snapshot(struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS],
			 struct audio_perf_queue_snapshot *queue)
{
	k_spinlock_key_t key = k_spin_lock(&perf.lock);

	for (int i = 0; i < AUDIO_PERF_NUM_PATHS; i++) {
		paths[i].count = perf.paths[i].count;
		paths[i].total_cycles = perf.paths[i].total_cycles;
		paths[i].max_cycles = perf.paths[i].max_cycles;
		paths[i].deadline_overruns = perf.paths[i].deadline_overruns;
	}

	if (queue) {
		queue->slab_min_free = perf.slab_min_free;
		queue->slab_max_free = perf.slab_max_free;
		queue->push_failures = perf.push_failures;
		queue->repeat_fallback_count = perf.repeat_fallback_count;
		queue->output_frames_min = perf.output_frames_min;
		queue->output_frames_max = perf.output_frames_max;
		queue->output_blocks = perf.output_blocks;
	}

	k_spin_unlock(&perf.lock, key);
}

void audio_perf_reset(void)
{
	k_spinlock_key_t key = k_spin_lock(&perf.lock);

	for (int i = 0; i < AUDIO_PERF_NUM_PATHS; i++) {
		perf.paths[i].count = 0;
		perf.paths[i].total_cycles = 0;
		perf.paths[i].max_cycles = 0;
		perf.paths[i].deadline_overruns = 0;
	}

	perf.slab_min_free = 0;
	perf.slab_max_free = 0;
	perf.slab_first = true;
	perf.push_failures = 0;
	perf.repeat_fallback_count = 0;
	perf.output_frames_min = 0;
	perf.output_frames_max = 0;
	perf.output_blocks = 0;

	k_spin_unlock(&perf.lock, key);
}

#endif /* CONFIG_AUDIO_PERF_MEASUREMENT */
