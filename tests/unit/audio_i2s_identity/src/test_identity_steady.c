/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Identity/APLL steady-state tests (identity resampler + APLL actuator).
 *
 * Locks: one drift update per started block before slab allocation; ppm
 * passed exactly once to the actuator; exact data bytes; write-error
 * ownership; -EIO PREPARE recovery and fresh restart; repeat fallback
 * ownership and counting; perf push timing balance; queue metrics.
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/i2s.h>

#include "audio_i2s_test_helpers.h"

/* ── drift/actuator ──────────────────────────────────────────────── */

ZTEST(audio_i2s, test_no_drift_update_before_start)
{
	test_init_ok();

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "startup push");
	zassert_true(audio_i2s_test_is_started(), "started");
	zassert_equal(mock_drift_update_calls, 0, "no drift update before START");
}

ZTEST(audio_i2s, test_one_drift_update_per_started_block)
{
	test_start_stream();

	/* Eleven startup blocks queued → free = 16 - 11 = 5 before alloc. */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "steady push 1");
	zassert_equal(mock_drift_update_calls, 1, "one update");
	zassert_equal(mock_drift_last_slab_free, 5, "pre-allocation free count");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - 12, "one more block queued");

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "steady push 2");
	zassert_equal(mock_drift_update_calls, 2, "second update");
	zassert_equal(mock_drift_last_slab_free, 4, "pre-allocation free count 2");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - 13, "two more blocks queued");
}

ZTEST(audio_i2s, test_zero_ppm_no_actuator_apply)
{
	test_start_stream();
	mock_drift_update_ret = 0;

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "push");
	zassert_equal(mock_actuator_apply_calls, 0, "zero ppm never applied");
}

ZTEST(audio_i2s, test_ppm_passed_exactly_once_to_actuator)
{
	test_start_stream();

	mock_drift_update_ret = 500;
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "positive ppm");
	zassert_equal(mock_actuator_apply_calls, 1, "applied once");
	zassert_equal(mock_actuator_last_ppm, 500, "positive ppm exact");

	mock_drift_update_ret = -300;
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "negative ppm");
	zassert_equal(mock_actuator_apply_calls, 2, "applied once more");
	zassert_equal(mock_actuator_last_ppm, -300, "negative ppm exact");
}

/* ── data path ───────────────────────────────────────────────────── */

ZTEST(audio_i2s, test_identity_data_block_exact)
{
	test_start_stream();

	const struct fake_i2s_write_rec *data = fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX);

	zassert_equal(data->size, TEST_BYTES_480, "data block size");
	zassert_true(test_rec_matches_input(data, test_input_480()), "exact input bytes");
	zassert_true(test_rec_silence(fake_i2s_write_rec(0)), "silence stays silent");
}

/* ── write errors ────────────────────────────────────────────────── */

ZTEST(audio_i2s, test_write_error_non_eio_keeps_started)
{
	test_start_stream();
	fake_i2s_set_write_fail_errno(-EBUSY);
	fake_i2s_fail_write_at(STARTUP_FIRST_STEADY_WRITE_IDX);

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EBUSY,
		      "write error returned");

	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - STARTUP_TOTAL_BLOCKS,
		      "caller block freed");
	zassert_equal(fake_i2s_queued_count(), STARTUP_TOTAL_BLOCKS, "driver queue untouched");
	zassert_true(audio_i2s_test_is_started(), "started kept for non -EIO");
	zassert_equal(fake_i2s_trigger_calls(), 1, "no recovery triggers");
	zassert_equal(mock_stats_stream_reset_calls, 0, "no stream reset");

	/* Stream still usable. */
	fake_i2s_fail_write_at(-1);
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "next push ok");
	zassert_equal(mock_drift_update_calls, 2, "drift ran for both started pushes");
}

ZTEST(audio_i2s, test_eio_write_prepare_recovery)
{
	test_start_stream();
	fake_i2s_set_write_fail_errno(-EIO);
	fake_i2s_fail_write_at(STARTUP_FIRST_STEADY_WRITE_IDX);

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EIO, "EIO returned");

	zassert_equal(fake_i2s_trigger_calls(), 2, "START then PREPARE");
	zassert_equal(fake_i2s_trigger_rec(1)->cmd, I2S_TRIGGER_PREPARE, "PREPARE recovery");
	zassert_equal(mock_stats_stream_reset_calls, 1, "stream reset counted");
	zassert_false(audio_i2s_test_is_started(), "started cleared");
	zassert_true(audio_i2s_test_is_configured(), "configured retained");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "slab fully reclaimable");
	zassert_equal(fake_i2s_queued_count(), 0, "PREPARE purged queued blocks");

	/* Next valid push: fresh ten-silence prefill + data + START. */
	fake_i2s_reset();
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "fresh start");

	zassert_equal(fake_i2s_write_calls(), STARTUP_TOTAL_BLOCKS, "fresh eleven-block prefill");
	zassert_equal(fake_i2s_trigger_calls(), 1, "fresh START");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_START, "START");
	zassert_true(audio_i2s_test_is_started(), "started again");
}

/* ── repeat fallback ─────────────────────────────────────────────── */

ZTEST(audio_i2s, test_repeat_fallback_at_threshold)
{
	test_start_stream();
	fake_i2s_release_all(); /* free = 16 ≥ threshold */

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "steady push");

	zassert_equal(mock_perf_repeat_fallback_calls, 1, "repeat counted once");
	zassert_equal(fake_i2s_write_calls(), STARTUP_TOTAL_BLOCKS + 2,
		      "11 startup + data + repeat block");
	zassert_equal(fake_i2s_queued_count(), 2, "both queued");

	const struct fake_i2s_write_rec *data = fake_i2s_write_rec(STARTUP_FIRST_STEADY_WRITE_IDX);
	const struct fake_i2s_write_rec *dup =
		fake_i2s_write_rec(STARTUP_FIRST_STEADY_WRITE_IDX + 1);

	zassert_not_equal(dup->ptr, data->ptr, "separate slab block");
	zassert_equal(dup->size, data->size, "repeat size equals saved frame");
	zassert_equal(dup->size, TEST_BYTES_480, "saved frame size");
	zassert_equal(memcmp(dup->snapshot, data->snapshot, FAKE_I2S_SNAPSHOT_BYTES), 0,
		      "repeat copies latest saved frame exactly");
	test_assert_no_duplicate_writes();
}

ZTEST(audio_i2s, test_repeat_not_triggered_below_threshold)
{
	test_start_stream();
	/* After the 11-block startup pre-fill, free = 16 − 11 = 5.  Two
	 * releases bring free to 7, still below DRIFT_THRESHOLD (12): no
	 * repeat attempt. */
	fake_i2s_release(fake_i2s_queued_ptr(0));
	fake_i2s_release(fake_i2s_queued_ptr(0)); /* free = 7 */

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "steady push");
	zassert_equal(mock_perf_repeat_fallback_calls, 0, "no repeat below threshold");
	zassert_equal(fake_i2s_write_calls(), STARTUP_TOTAL_BLOCKS + 1, "only data write");
}

ZTEST(audio_i2s, test_repeat_alloc_fail_ownership)
{
	test_start_stream();
	fake_i2s_release_all();
	audio_i2s_test_set_slab_alloc_failure(true);

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "steady push");

	zassert_equal(mock_perf_repeat_fallback_calls, 1, "attempted fallback counted once");
	zassert_equal(fake_i2s_write_calls(), STARTUP_TOTAL_BLOCKS + 1,
		      "data written, no dup write");
	zassert_equal(fake_i2s_queued_count(), 1, "only data block queued");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - 1, "no leaked block");
	test_assert_no_duplicate_writes();
}

ZTEST(audio_i2s, test_repeat_write_fail_ownership)
{
	test_start_stream();
	fake_i2s_release_all();
	fake_i2s_set_write_fail_errno(-EBUSY);
	fake_i2s_fail_write_at(STARTUP_FIRST_STEADY_WRITE_IDX + 1);

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0,
		      "data write ok, dup write fails");

	zassert_equal(mock_perf_repeat_fallback_calls, 1, "attempted fallback counted once");
	zassert_equal(fake_i2s_write_calls(), STARTUP_TOTAL_BLOCKS + 2, "data + failed dup write");
	zassert_equal(fake_i2s_queued_count(), 1, "dup never owned by driver");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - 1, "dup block caller-freed");
	test_assert_no_duplicate_writes();
}

/* ── perf push timing / queue metrics ────────────────────────────── */

ZTEST(audio_i2s, test_perf_push_timing_started_pushes)
{
	test_start_stream();

	/* Startup push is not measured. */
	zassert_equal(mock_perf_cycle_start_calls, 0, "startup push not measured");

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "ok push");
	zassert_equal(mock_perf_cycle_start_calls, 1, "started push measured");
	zassert_equal(mock_perf_cycle_end_calls, 1, "ended exactly once");

	fake_i2s_set_write_fail_errno(-EBUSY);
	fake_i2s_fail_write_at(STARTUP_FIRST_STEADY_WRITE_IDX + 1);
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EBUSY,
		      "write failure push");
	zassert_equal(mock_perf_cycle_start_calls, 2, "failure push measured");
	zassert_equal(mock_perf_cycle_end_calls, 2, "failure push ended");

	fake_i2s_set_write_fail_errno(-EIO);
	fake_i2s_fail_write_at(STARTUP_FIRST_STEADY_WRITE_IDX + 2); /* next write index */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EIO, "EIO push");
	zassert_equal(mock_perf_cycle_start_calls, 3, "EIO push measured");
	zassert_equal(mock_perf_cycle_end_calls, 3, "EIO push ended");
}

ZTEST(audio_i2s, test_queue_metrics_prealloc_count_and_frames)
{
	test_start_stream();

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "steady push");

	zassert_equal(mock_perf_queue_sample_calls, 1, "one queue sample");
	zassert_equal(mock_perf_last_slab_free, 5, "pre-allocation slab count");
	zassert_equal(mock_perf_last_output_frames, TEST_FRAMES_480, "output frame count");
}

/* ── underrun accounting ─────────────────────────────────────────── */

ZTEST(audio_i2s, test_slab_full_underrun)
{
	test_start_stream();
	test_prealloc_blocks(5); /* exhaust all remaining blocks */

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -ENOMEM, "slab full");

	zassert_equal(mock_stats_underrun_calls, 1, "underrun counted");
	zassert_equal(fake_i2s_write_calls(), STARTUP_TOTAL_BLOCKS, "no new writes");
	zassert_true(audio_i2s_test_is_started(), "stream stays started");
}
