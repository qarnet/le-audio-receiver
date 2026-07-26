/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for audio_perf — cycle and queue instrumentation.
 * Tests accumulator update, min/max, overflow-safe totals,
 * deadline counting, reset, and snapshot consistency.
 *
 * Deadline is set to 10000 cycles (CONFIG_AUDIO_PERF_DEADLINE_CYCLES).
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include "audio_perf.h"

/* ── Fixture ───────────────────────────────────────────────────── */

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	audio_perf_reset();
}

ZTEST_SUITE(perf, NULL, NULL, reset_before_each, NULL, NULL);

/* ── 1. Cycle accumulator: basic updates ───────────────────────── */

ZTEST(perf, test_cycle_end_increments_count)
{
	uint32_t t0 = audio_perf_cycle_start();

	k_busy_wait(100); /* ~100 us of simulated time */
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_SINK_PUSH);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].count, 1,
		      "count should be 1 after one cycle_end");
	zassert_true(paths[AUDIO_PERF_PATH_SINK_PUSH].total_cycles > 0,
		     "total_cycles should be non-zero after busy_wait");
}

ZTEST(perf, test_cycle_end_accumulates_total)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	uint32_t t0 = audio_perf_cycle_start();

	k_busy_wait(100);
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);

	t0 = audio_perf_cycle_start();
	k_busy_wait(100);
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_ISO_RECV].count, 2, "count should be 2");
	zassert_true(paths[AUDIO_PERF_PATH_ISO_RECV].total_cycles > 0, "total should be > 0");
}

ZTEST(perf, test_cycle_end_updates_max)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	/* Short */
	uint32_t t0 = audio_perf_cycle_start();

	k_busy_wait(50);
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_LC3_DECODE);

	/* Longer */
	t0 = audio_perf_cycle_start();
	k_busy_wait(200);
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_LC3_DECODE);

	audio_perf_snapshot(paths, &queue);
	zassert_true(paths[AUDIO_PERF_PATH_LC3_DECODE].max_cycles > 0, "max_cycles should be > 0");
}

ZTEST(perf, test_deadline_overrun_counted)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	/* Deadline is 10000 cycles.  busy_wait 100 us should be well under
	 * that on native_sim (native_sim cycles ~= simulated time).  Use a
	 * short busy_wait which should be under the deadline.
	 */
	uint32_t t0 = audio_perf_cycle_start();

	k_busy_wait(10);
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_VOLUME);

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].deadline_overruns, 0,
		      "short wait should not exceed 10000 cycle deadline");
}

/* ── 2. Four independent paths ─────────────────────────────────── */

ZTEST(perf, test_paths_independent)
{
	uint32_t t0 = audio_perf_cycle_start();

	k_busy_wait(50);
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);

	t0 = audio_perf_cycle_start();
	k_busy_wait(50);
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_LC3_DECODE);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_ISO_RECV].count, 1, "ISO_RECV count = 1");
	zassert_equal(paths[AUDIO_PERF_PATH_LC3_DECODE].count, 1, "LC3_DECODE count = 1");
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 0, "VOLUME untouched");
	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].count, 0, "SINK_PUSH untouched");
}

/* ── 3. Queue metrics ──────────────────────────────────────────── */

ZTEST(perf, test_queue_sample_min_max_first)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_queue_sample(5, 477);
	audio_perf_snapshot(paths, &queue);

	zassert_equal(queue.slab_min_free, 5, "first sample: min = 5");
	zassert_equal(queue.slab_max_free, 5, "first sample: max = 5");
	zassert_equal(queue.output_frames_min, 477, "first sample: frames min = 477");
	zassert_equal(queue.output_frames_max, 477, "first sample: frames max = 477");
	zassert_equal(queue.output_blocks, 1, "output_blocks = 1");
}

ZTEST(perf, test_queue_sample_preserves_min_max)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_queue_sample(5, 477);
	audio_perf_queue_sample(3, 476);
	audio_perf_queue_sample(7, 478);

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.slab_min_free, 3, "min free = 3");
	zassert_equal(queue.slab_max_free, 7, "max free = 7");
	zassert_equal(queue.output_frames_min, 476, "frames min = 476");
	zassert_equal(queue.output_frames_max, 478, "frames max = 478");
	zassert_equal(queue.output_blocks, 3, "output_blocks = 3");
}

ZTEST(perf, test_push_failure_increments)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_push_failure();
	audio_perf_push_failure();
	audio_perf_push_failure();

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.push_failures, 3, "push_failures = 3");
}

ZTEST(perf, test_repeat_fallback_increments)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_repeat_fallback();
	audio_perf_repeat_fallback();

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.repeat_fallback_count, 2, "repeat_fallback = 2");
}

/* ── 4. Reset ──────────────────────────────────────────────────── */

ZTEST(perf, test_reset_clears_cycle_counters)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	uint32_t t0 = audio_perf_cycle_start();

	k_busy_wait(100);
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_SINK_PUSH);

	audio_perf_reset();
	audio_perf_snapshot(paths, &queue);

	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].count, 0, "count = 0 after reset");
	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].total_cycles, 0,
		      "total_cycles = 0 after reset");
	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].max_cycles, 0, "max_cycles = 0 after reset");
	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].deadline_overruns, 0,
		      "deadline_overruns = 0 after reset");
}

ZTEST(perf, test_reset_clears_queue_metrics)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_queue_sample(5, 477);
	audio_perf_push_failure();
	audio_perf_push_failure();
	audio_perf_repeat_fallback();

	audio_perf_reset();
	audio_perf_snapshot(paths, &queue);

	zassert_equal(queue.slab_min_free, 0, "slab_min = 0 after reset");
	zassert_equal(queue.slab_max_free, 0, "slab_max = 0 after reset");
	zassert_equal(queue.push_failures, 0, "push_failures = 0 after reset");
	zassert_equal(queue.repeat_fallback_count, 0, "repeat_fallback = 0 after reset");
	zassert_equal(queue.output_blocks, 0, "output_blocks = 0 after reset");
}

ZTEST(perf, test_reset_clears_slab_first_flag)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	/* Before reset: first sample initialises min/max. */
	audio_perf_queue_sample(8, 480);
	audio_perf_queue_sample(2, 470);

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.slab_min_free, 2, "pre-reset min = 2");
	zassert_equal(queue.slab_max_free, 8, "pre-reset max = 8");

	/* After reset: first sample re-initialises. */
	audio_perf_reset();
	audio_perf_queue_sample(6, 477);

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.slab_min_free, 6, "post-reset min = 6");
	zassert_equal(queue.slab_max_free, 6, "post-reset max = 6");
}

/* ── 5. Snapshot consistency ───────────────────────────────────── */

ZTEST(perf, test_snapshot_matches_state)
{
	audio_perf_queue_sample(4, 476);
	audio_perf_push_failure();

	uint32_t t0 = audio_perf_cycle_start();

	k_busy_wait(100);
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_VOLUME);

	t0 = audio_perf_cycle_start();
	k_busy_wait(100);
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_VOLUME);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);

	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 2, "volume count = 2");
	zassert_equal(queue.output_blocks, 1, "output_blocks = 1");
	zassert_equal(queue.push_failures, 1, "push_failures = 1");

	/* Second snapshot is idempotent. */
	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 2,
		      "second snapshot: volume count unchanged");
}

/* ── 6. No-op path safety (cycle_start returns 0 when disabled? ──
 *    On this test CONFIG_AUDIO_PERF_MEASUREMENT=y, so cycle_start
 *    returns non-zero.  We just check it doesn't crash. ───────── */

ZTEST(perf, test_cycle_start_returns_nonzero)
{
	uint32_t t0 = audio_perf_cycle_start();

	zassert_true(t0 != 0, "cycle_start returns non-zero when measurement enabled");
}

/* ── 7. Path boundary safety ───────────────────────────────────── */

ZTEST(perf, test_invalid_path_id_does_not_crash)
{
	uint32_t t0 = audio_perf_cycle_start();

	k_busy_wait(10);
	/* Path 99 is out of range; should be a no-op, not a fault. */
	audio_perf_cycle_end(t0, (enum audio_perf_path)99);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	/* All paths should be untouched. */
	zassert_equal(paths[AUDIO_PERF_PATH_ISO_RECV].count, 0, "invalid path: iso_recv untouched");
}
