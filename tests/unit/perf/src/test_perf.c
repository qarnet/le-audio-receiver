/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for audio_perf — deterministic injection, min/max,
 * uint64 boundary, deadline overrun, reset, and snapshot consistency.
 *
 * Uses audio_perf_test_inject_cycles() for deterministic cycle counts
 * (no reliance on k_busy_wait timing accuracy).
 *
 * Deadline comparisons use k_cyc_to_us_ceil32(elapsed) vs
 * CONFIG_AUDIO_PERF_DEADLINE_US (10000).  At native_sim 1 MHz,
 * cycles ≈ µs, so values under 10000 always pass and large
 * values (0xFFFFFFFF) always overrun.
 */

#include <errno.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include "audio_perf.h"

/* ── Fixture ───────────────────────────────────────────────────── */

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	audio_perf_test_clear_cycle_now();
	audio_perf_reset();
}

ZTEST_SUITE(perf, NULL, NULL, reset_before_each, NULL, NULL);

/* ── 1. Deterministic injection: basic updates ──────────────────── */

ZTEST(perf, test_inject_increments_count)
{
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_SINK_PUSH, 1000);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].count, 1, "count = 1");
	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].total_cycles, 1000, "total = 1000");
	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].max_cycles, 1000, "max = 1000");
}

ZTEST(perf, test_inject_accumulates_total)
{
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_ISO_RECV, 500);
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_ISO_RECV, 300);
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_ISO_RECV, 200);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_ISO_RECV].count, 3, "count = 3");
	zassert_equal(paths[AUDIO_PERF_PATH_ISO_RECV].total_cycles, 1000, "total = 500+300+200");
}

ZTEST(perf, test_inject_updates_max)
{
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_LC3_DECODE, 50);
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_LC3_DECODE, 200);
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_LC3_DECODE, 100);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_LC3_DECODE].max_cycles, 200,
		      "max = 200 (largest of 50/200/100)");
}

/* ── 2. Deadline overrun: deterministic ─────────────────────────── */

ZTEST(perf, test_inject_below_deadline_no_overrun)
{
	/* Native_sim at 1 MHz → 10000 us = ~10000 cycles.  500 cycles
	 * is well under any platform's 10 ms deadline.
	 */
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_VOLUME, 500);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].deadline_overruns, 0,
		      "500 cycles under 10ms deadline → 0 overruns");
}

ZTEST(perf, test_inject_above_deadline_counts_overrun)
{
	/* Inject a value larger than any conceivable 10ms deadline.
	 * At 128 MHz, 10ms = 1,280,000 cycles.  0xFFFFFFFF is far above.
	 */
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_SINK_PUSH, 0xFFFFFFFF);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].deadline_overruns, 1,
		      "0xFFFFFFFF exceeds any 10ms deadline → 1 overrun");
}

/* ── 3. uint64 total crossing 32-bit boundary ───────────────────── */

ZTEST(perf, test_uint64_total_crosses_32bit)
{
	/* Inject two values whose sum exceeds 2^32. */
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_LC3_DECODE, 0xFFFFFFF0);
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_LC3_DECODE, 0x00000020);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_LC3_DECODE].total_cycles, 0x100000010ULL,
		      "total = 0xFFFFFFF0 + 0x20 = 0x100000010");
}

/* ── 4. Four independent paths ──────────────────────────────────── */

ZTEST(perf, test_paths_independent)
{
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_ISO_RECV, 100);
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_LC3_DECODE, 200);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_ISO_RECV].count, 1, "ISO_RECV count = 1");
	zassert_equal(paths[AUDIO_PERF_PATH_LC3_DECODE].count, 1, "LC3_DECODE count = 1");
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 0, "VOLUME untouched");
	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].count, 0, "SINK_PUSH untouched");
}

/* ── 5. Queue metrics ───────────────────────────────────────────── */

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

ZTEST(perf, test_i2s_write_failure_categories_and_restart)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_i2s_write_failure(-EIO);
	audio_perf_i2s_write_failure(-ENOMSG);
	audio_perf_i2s_write_failure(-EBUSY);
	audio_perf_i2s_dma_restart();
	audio_perf_i2s_dma_restart();

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.i2s_write_failures, 3, "I2S write failures = 3");
	zassert_equal(queue.i2s_write_eio_failures, 1, "I2S -EIO failures = 1");
	zassert_equal(queue.i2s_write_enomsg_failures, 1, "I2S -ENOMSG failures = 1");
	zassert_equal(queue.i2s_write_last_errno, -EBUSY, "last I2S write errno = -EBUSY");
	zassert_equal(queue.i2s_dma_restarts, 2, "I2S DMA restarts = 2");
}

ZTEST(perf, test_public_timing_wrappers_with_injected_cycle_clock)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_test_set_cycle_now(100);
	audio_perf_i2s_dma_started();
	zassert_equal(audio_perf_i2s_write_start(), 100, "write start = 100");

	audio_perf_test_set_cycle_now(130);
	audio_perf_i2s_write_end(100, true);

	audio_perf_rx_callback_start(200);
	audio_perf_rx_callback_start(260);

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.i2s_write_duration_max_cycles, 30, "I2S write duration = 30");
	zassert_equal(queue.i2s_write_gap_max_cycles, 30, "I2S write completion gap = 30");
	zassert_equal(queue.rx_callback_gap_max_cycles, 60, "RX callback gap = 60");

	audio_perf_test_clear_cycle_now();
}

ZTEST(perf, test_timing_telemetry_first_event_rebase_and_max)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	/* First RX callback establishes its baseline; only later starts
	 * contribute a gap. */
	audio_perf_test_inject_rx_callback_start(100);
	audio_perf_test_inject_rx_callback_start(140);
	audio_perf_test_inject_rx_callback_start(210);

	/* DMA START establishes the I2S baseline.  The first successful write
	 * measures completion from START, not from an arbitrary pre-fill or boot
	 * time. */
	audio_perf_test_inject_i2s_dma_start(1000);
	audio_perf_test_inject_i2s_write(1200, 20, true);
	audio_perf_test_inject_i2s_write(1600, 80, true);

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.rx_callback_gap_max_cycles, 70, "RX callback max gap = 70");
	zassert_equal(queue.i2s_write_gap_max_cycles, 460, "I2S write max completion gap = 460");
	zassert_equal(queue.i2s_write_duration_max_cycles, 80, "I2S write max duration = 80");
}

ZTEST(perf, test_timing_telemetry_success_only_gap_and_failure_duration)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_test_inject_i2s_dma_start(100);
	/* Failed write is timed but cannot move successful-write baseline. */
	audio_perf_test_inject_i2s_write(200, 10, false);
	audio_perf_test_inject_i2s_write(350, 20, true);
	/* Another failure must not create a gap from the prior success. */
	audio_perf_test_inject_i2s_write(1000, 500, false);
	audio_perf_test_inject_i2s_write(1600, 30, true);

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.i2s_write_gap_max_cycles, 1260,
		      "failed writes do not rebase successful-write gap");
	zassert_equal(queue.i2s_write_duration_max_cycles, 500,
		      "failed write contributes duration maximum");
}

ZTEST(perf, test_timing_telemetry_wrap_safe_subtraction)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_test_inject_rx_callback_start(0xFFFFFFF0U);
	audio_perf_test_inject_rx_callback_start(0x00000020U);

	audio_perf_test_inject_i2s_dma_start(0xFFFFFFE0U);
	audio_perf_test_inject_i2s_write(0xFFFFFFF0U, 0x05U, true);
	audio_perf_test_inject_i2s_write(0xFFFFFFF8U, 0x30U, false);
	audio_perf_test_inject_i2s_write(0x00000030U, 0x20U, true);

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.rx_callback_gap_max_cycles, 0x30U, "RX callback gap wraps safely");
	zassert_equal(queue.i2s_write_gap_max_cycles, 0x5BU,
		      "I2S write completion gap wraps safely");
	zassert_equal(queue.i2s_write_duration_max_cycles, 0x30U,
		      "I2S write duration wraps safely");
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

/* ── 6. Reset ───────────────────────────────────────────────────── */

ZTEST(perf, test_reset_clears_cycle_counters)
{
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_SINK_PUSH, 1000);

	audio_perf_reset();

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

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

ZTEST(perf, test_reset_clears_i2s_write_diagnostics)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_i2s_write_failure(-EIO);
	audio_perf_i2s_write_failure(-ENOMSG);
	audio_perf_i2s_dma_restart();
	audio_perf_reset();
	audio_perf_snapshot(paths, &queue);

	zassert_equal(queue.i2s_write_failures, 0, "I2S write failures = 0 after reset");
	zassert_equal(queue.i2s_write_eio_failures, 0, "I2S -EIO failures = 0 after reset");
	zassert_equal(queue.i2s_write_enomsg_failures, 0, "I2S -ENOMSG failures = 0 after reset");
	zassert_equal(queue.i2s_write_last_errno, 0, "last I2S write errno = 0 after reset");
	zassert_equal(queue.i2s_dma_restarts, 0, "I2S DMA restarts = 0 after reset");
}

ZTEST(perf, test_reset_clears_timing_telemetry)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_test_inject_rx_callback_start(100);
	audio_perf_test_inject_rx_callback_start(200);
	audio_perf_test_inject_i2s_dma_start(300);
	audio_perf_test_inject_i2s_write(400, 50, true);
	audio_perf_reset();
	audio_perf_snapshot(paths, &queue);

	zassert_equal(queue.rx_callback_gap_max_cycles, 0, "RX callback gap = 0 after reset");
	zassert_equal(queue.i2s_write_gap_max_cycles, 0, "I2S write gap = 0 after reset");
	zassert_equal(queue.i2s_write_duration_max_cycles, 0, "I2S write duration = 0 after reset");
}

ZTEST(perf, test_reset_clears_slab_first_flag)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_queue_sample(8, 480);
	audio_perf_queue_sample(2, 470);

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.slab_min_free, 2, "pre-reset min = 2");

	audio_perf_reset();
	audio_perf_queue_sample(6, 477);

	audio_perf_snapshot(paths, &queue);
	zassert_equal(queue.slab_min_free, 6, "post-reset min = 6");
	zassert_equal(queue.slab_max_free, 6, "post-reset max = 6");
}

ZTEST(perf, test_double_reset_no_double_count)
{
	/* Reset twice is safe. */
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_VOLUME, 100);
	audio_perf_reset();
	audio_perf_reset();

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 0, "double reset: count = 0");
}

ZTEST(perf, test_preserve_semantics_no_auto_reset)
{
	/* Perf counters are NOT auto-cleared between snapshots
	 * (reset only at gate-open or explicit shell command).
	 */
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_SINK_PUSH, 100);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	uint32_t cnt1 = paths[AUDIO_PERF_PATH_SINK_PUSH].count;
	zassert_equal(cnt1, 1, "first snapshot: count = 1");

	/* Snapshot again — state preserved. */
	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_SINK_PUSH].count, 1,
		      "second snapshot: count still 1 (preserved)");
}

/* ── 7. Snapshot consistency ────────────────────────────────────── */

ZTEST(perf, test_snapshot_matches_state)
{
	audio_perf_queue_sample(4, 476);
	audio_perf_push_failure();
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_VOLUME, 100);
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_VOLUME, 200);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 2, "volume count = 2");
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].total_cycles, 300, "volume total = 300");
	zassert_equal(queue.output_blocks, 1, "output_blocks = 1");
	zassert_equal(queue.push_failures, 1, "push_failures = 1");

	/* Second snapshot is idempotent. */
	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 2, "second snapshot: count unchanged");
}

/* ── 8. Null snapshot queue pointer safety ──────────────────────── */

ZTEST(perf, test_null_queue_snapshot_does_not_crash)
{
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_VOLUME, 100);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];

	/* NULL queue pointer: paths still filled, no crash. */
	audio_perf_snapshot(paths, NULL);
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 1, "paths filled with NULL queue");
}

/* ── 9. Path boundary safety ────────────────────────────────────── */

ZTEST(perf, test_invalid_path_id_does_not_crash)
{
	audio_perf_test_inject_cycles((enum audio_perf_path)99, 500);

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(paths[AUDIO_PERF_PATH_ISO_RECV].count, 0, "invalid path: iso_recv untouched");
}
