/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared stop/reconnect tests for the I2S sink suites.
 *
 * Locks: reset of drift/actuator/rate-converter/timing (+ASRC state)
 * on every stop even when already stopped; PREPARE-then-DROP trigger
 * order when started; no extra triggers on repeated stop; configured
 * retained; fresh pre-fill on the next push; saved-frame/sequence state
 * does not leak across stop; trigger errors never corrupt state.
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/i2s.h>

#include "audio_i2s_test_helpers.h"

/* ── reset on stop ───────────────────────────────────────────────── */

ZTEST(audio_i2s, test_stop_not_started_resets_all)
{
	test_init_ok();
	zassert_false(audio_i2s_test_is_started(), "not started yet");

	audio_sink_stop();

	zassert_equal(mock_drift_reset_calls, 1, "drift reset");
	zassert_equal(mock_actuator_reset_calls, 1, "actuator reset");
	zassert_equal(mock_rate_convert_init_calls, 2, "rate converter re-inited");
	zassert_equal(mock_timing_reset_calls, 1, "timing reset");
#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
	zassert_equal(mock_asrc_reset_calls, 1, "asrc reset");
#endif
	zassert_equal(fake_i2s_trigger_calls(), 0, "no triggers when already stopped");
	zassert_true(audio_i2s_test_is_configured(), "configured retained");
	zassert_equal(audio_i2s_test_saved_frame_len(), 0, "saved frame cleared");

	/* Stop again: resets again, still no extra triggers. */
	audio_sink_stop();
	zassert_equal(mock_drift_reset_calls, 2, "drift reset again");
	zassert_equal(mock_timing_reset_calls, 2, "timing reset again");
	zassert_equal(fake_i2s_trigger_calls(), 0, "still no triggers");
}

ZTEST(audio_i2s, test_stop_started_order_prepare_then_drop)
{
	test_start_stream();

	audio_sink_stop();

	zassert_equal(fake_i2s_trigger_calls(), 3, "START + PREPARE + DROP");
	zassert_equal(fake_i2s_trigger_rec(1)->cmd, I2S_TRIGGER_PREPARE, "PREPARE first");
	zassert_equal(fake_i2s_trigger_rec(2)->cmd, I2S_TRIGGER_DROP, "DROP second");
	zassert_false(audio_i2s_test_is_started(), "started false");
	zassert_true(audio_i2s_test_is_configured(), "configured true");
	zassert_equal(fake_i2s_queued_count(), 0, "DROP purged queued blocks");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "slab fully reclaimable");
}

ZTEST(audio_i2s, test_stop_repeated_no_extra_triggers)
{
	test_start_stream();

	audio_sink_stop();
	int triggers_after_first = fake_i2s_trigger_calls();

	zassert_equal(triggers_after_first, 3, "START + PREPARE + DROP");
	audio_sink_stop();
	audio_sink_stop();

	zassert_equal(fake_i2s_trigger_calls(), triggers_after_first,
		      "no extra triggers after first stop");
	zassert_false(audio_i2s_test_is_started(), "still stopped");
}

/* ── reconnect ───────────────────────────────────────────────────── */

ZTEST(audio_i2s, test_push_after_stop_starts_fresh_prefill)
{
	test_start_stream();
	audio_sink_stop();

	fake_i2s_reset(); /* clear records; nothing queued after stop */

	/* After stop, admission is closed: a valid push is rejected
	 * (-EBUSY) with zero allocation/write/state/counter mutation. */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EBUSY,
		      "push after stop rejected");

	zassert_equal(fake_i2s_write_calls(), 0, "no writes while closed");
	zassert_equal(fake_i2s_queued_count(), 0, "nothing queued");
	zassert_equal(mock_drift_update_calls, 0, "no drift");
	zassert_equal(audio_i2s_test_active_pushes(), 0, "no admitted push");
	zassert_false(audio_i2s_test_is_started(), "still stopped");
	zassert_true(audio_i2s_test_is_configured(), "configured retained");

	/* Explicit stream open (BAP gate closed→open) then reconnects. */
	zassert_equal(audio_sink_stream_open(), 0, "stream open");

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0,
		      "push after stop without re-init");

	zassert_equal(fake_i2s_write_calls(), 7, "fresh seven-block prefill");
	zassert_equal(fake_i2s_trigger_calls(), 1, "fresh START");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_START, "START");
	zassert_true(audio_i2s_test_is_started(), "started again");
	zassert_true(audio_i2s_test_is_configured(), "configured retained");
	zassert_equal(fake_i2s_configure_calls(), 0, "no re-configure");
	test_assert_distinct_pointers(0, 7);
	test_assert_no_duplicate_writes();
}

ZTEST(audio_i2s, test_stop_clears_saved_frame_and_sequence)
{
	test_start_stream();

	/* One steady block: saved frame populated; ASRC sequence advanced
	 * once by the startup block and once by this block.
	 */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "steady push");
	zassert_equal(audio_i2s_test_saved_frame_len(), TEST_BYTES_480, "saved frame set");
#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
	zassert_equal(audio_i2s_test_offload_sequence(), 2, "sequence advanced twice");
	zassert_true(audio_i2s_test_asrc_prev_valid(), "prev valid");
#endif

	audio_sink_stop();

	zassert_equal(audio_i2s_test_saved_frame_len(), 0, "saved frame cleared on stop");
#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
	zassert_equal(audio_i2s_test_offload_sequence(), 0, "sequence cleared on stop");
	zassert_false(audio_i2s_test_asrc_prev_valid(), "prev-valid cleared on stop");
#endif
}

/* ── trigger errors during stop ──────────────────────────────────── */

ZTEST(audio_i2s, test_stop_trigger_errors_keep_config_no_double_free)
{
	test_start_stream();

	fake_i2s_set_trigger_ret(I2S_TRIGGER_PREPARE, -EIO);
	fake_i2s_set_trigger_ret(I2S_TRIGGER_DROP, -EIO);

	audio_sink_stop();

	zassert_equal(fake_i2s_trigger_calls(), 3, "START + both stop triggers attempted");
	zassert_false(audio_i2s_test_is_started(), "started false");
	zassert_true(audio_i2s_test_is_configured(), "configured never flipped");
	zassert_equal(fake_i2s_queued_count(), 7, "failed triggers did not purge");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - 7, "driver still owns blocks");

	/* No double free: blocks freed exactly once via the captured slab. */
	fake_i2s_release_all();
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "slab fully reclaimable");
	test_assert_no_duplicate_writes();
}
