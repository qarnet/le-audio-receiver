/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared common/init/input tests for the I2S sink suites (compiled into
 * both the ASRC and identity variants).
 *
 * Runs real src/audio_i2s.c against the fake I2S driver and dependency
 * mocks.  Variant-specific behavior is guarded with
 * AUDIO_I2S_TEST_MARKER_ASRC (defined only by the ASRC suite).
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/i2s.h>

#include "audio_i2s_test_helpers.h"

/* ── init ────────────────────────────────────────────────────────── */

ZTEST(audio_i2s, test_not_ready_no_calls)
{
	audio_i2s_test_set_device_ready(false);

	zassert_equal(audio_sink_init(), -ENODEV, "init when device not ready");

	zassert_equal(fake_i2s_configure_calls(), 0, "no configure call");
	zassert_equal(mock_rate_convert_init_calls, 0, "no rate-converter init");
	zassert_equal(mock_asrc_init_calls, 0, "no asrc init");
	zassert_equal(mock_actuator_init_calls, 0, "no actuator init");
	zassert_equal(mock_timing_init_calls, 0, "no timing init");
	zassert_false(audio_i2s_test_is_configured(), "not configured");
}

ZTEST(audio_i2s, test_exact_i2s_config)
{
	test_init_ok();

	const struct fake_i2s_cfg_rec *cfg = fake_i2s_cfg_rec();

	zassert_not_null(cfg, "configure captured");
	zassert_equal(cfg->dir, I2S_DIR_TX, "TX direction");
	zassert_equal(cfg->cfg.word_size, 16, "16-bit words");
	zassert_equal(cfg->cfg.channels, 2, "2 channels");
	zassert_equal(cfg->cfg.format, I2S_FMT_DATA_FORMAT_I2S, "I2S format");
	zassert_equal(cfg->cfg.options,
		      (i2s_opt_t)(I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER),
		      "bit/frame master");
	zassert_equal(cfg->cfg.frame_clk_freq, 48000, "nominal 48 kHz");
	zassert_equal(cfg->cfg.mem_slab, audio_i2s_test_get_slab(), "internal slab");
	zassert_equal(cfg->cfg.block_size, 1924, "block size 481*4");
	zassert_equal(cfg->cfg.timeout, 0, "non-blocking timeout");
}

ZTEST(audio_i2s, test_configure_error_propagates)
{
	fake_i2s_set_configure_ret(-EIO);

	zassert_equal(audio_sink_init(), -EIO, "configure error propagated");

	zassert_equal(mock_rate_convert_init_calls, 0, "no later dependency");
	zassert_equal(mock_asrc_init_calls, 0, "no asrc init");
	zassert_equal(mock_actuator_init_calls, 0, "no actuator init");
	zassert_equal(mock_timing_init_calls, 0, "no timing init");
	zassert_false(audio_i2s_test_is_configured(), "not configured");
}

#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
ZTEST(audio_i2s, test_asrc_init_error_propagates)
{
	mock_asrc_init_ret = -EINVAL;

	zassert_equal(audio_sink_init(), -EINVAL, "asrc init error propagated");

	zassert_equal(mock_rate_convert_init_calls, 1, "rate converter ran first");
	zassert_equal(mock_asrc_init_calls, 1, "asrc init attempted");
	zassert_equal(mock_actuator_init_calls, 0, "actuator not called");
	zassert_equal(mock_timing_init_calls, 0, "timing not called");
	zassert_false(audio_i2s_test_is_configured(), "not configured");
}
#endif

ZTEST(audio_i2s, test_actuator_init_error_propagates)
{
	mock_actuator_init_ret = -EBUSY;

	zassert_equal(audio_sink_init(), -EBUSY, "actuator init error propagated");

	zassert_equal(mock_rate_convert_init_calls, 1, "rate converter ran first");
#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
	zassert_equal(mock_asrc_init_calls, 1, "asrc init attempted");
#endif
	zassert_equal(mock_actuator_init_calls, 1, "actuator init attempted");
	zassert_equal(mock_timing_init_calls, 0, "timing not called");
	zassert_false(audio_i2s_test_is_configured(), "not configured");
}

ZTEST(audio_i2s, test_timing_init_error_propagates)
{
	mock_timing_init_ret = -EIO;

	zassert_equal(audio_sink_init(), -EIO, "timing init error propagated");

	zassert_equal(mock_timing_init_calls, 1, "timing init attempted");
	zassert_false(audio_i2s_test_is_configured(), "not configured");
}

ZTEST(audio_i2s, test_init_success_configured_after_all_stages)
{
	test_init_ok();

	zassert_equal(mock_rate_convert_init_calls, 1, "rate converter inited");
	zassert_equal(mock_actuator_init_calls, 1, "actuator inited");
	zassert_equal(mock_timing_init_calls, 1, "timing inited");
	zassert_equal(fake_i2s_configure_calls(), 1, "configured once");
#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
	zassert_equal(mock_asrc_init_calls, 1, "asrc inited");
#endif
}

ZTEST(audio_i2s, test_reinit_failure_clears_stale_state)
{
	/* Successful init + started stream... */
	test_start_stream();
	zassert_true(audio_i2s_test_is_configured(), "configured");
	zassert_true(audio_i2s_test_is_started(), "started");

	/* ...then a failed re-init must not leave stale state behind. */
	fake_i2s_set_configure_ret(-EIO);
	zassert_equal(audio_sink_init(), -EIO, "re-init fails");

	zassert_false(audio_i2s_test_is_configured(), "configured cleared");
	zassert_false(audio_i2s_test_is_started(), "started cleared");
}

/* ── input frame setter ──────────────────────────────────────────── */

ZTEST(audio_i2s, test_input_frames_setter_supported)
{
	audio_sink_set_input_frames(TEST_FRAMES_360);
	zassert_equal(audio_i2s_test_input_frames(), TEST_FRAMES_360, "360 accepted");

	audio_sink_set_input_frames(TEST_FRAMES_480);
	zassert_equal(audio_i2s_test_input_frames(), TEST_FRAMES_480, "480 accepted");
}

ZTEST(audio_i2s, test_input_frames_setter_unsupported_reset)
{
	const uint16_t bad[] = {0, 1, 359, 361, 479, 481, 65535};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		audio_sink_set_input_frames(bad[i]);
		zassert_equal(audio_i2s_test_input_frames(), TEST_FRAMES_480, "bad %u -> 480",
			      bad[i]);
	}
}

/* ── push validation ─────────────────────────────────────────────── */

ZTEST(audio_i2s, test_push_rejections)
{
	test_init_ok();

	int16_t *in = test_input_480();

	zassert_equal(audio_sink_push(NULL, TEST_FRAMES_480 * 2), -EINVAL, "null data");
	zassert_equal(audio_sink_push(in, 0), -EINVAL, "zero samples");
	zassert_equal(audio_sink_push(in, 1), -EINVAL, "odd sample count");
	zassert_equal(audio_sink_push(in, TEST_FRAMES_480 * 2 - 2), -EINVAL, "too short");
	zassert_equal(audio_sink_push(in, TEST_FRAMES_480 * 2 + 2), -EINVAL, "too long");

	/* Wrong configured frame count: 360 configured, 480 pushed. */
	audio_sink_set_input_frames(TEST_FRAMES_360);
	zassert_equal(audio_sink_push(in, TEST_FRAMES_480 * 2), -EINVAL, "wrong frame count");
}

ZTEST(audio_i2s, test_malformed_push_no_side_effects)
{
	/* Started stream: every dependency must be untouched by a
	 * malformed push.
	 */
	test_start_stream();

	int writes_before = fake_i2s_write_calls();
	int drift_before = mock_drift_update_calls;
	int perf_before = mock_perf_cycle_start_calls;

	zassert_equal(audio_sink_push(NULL, TEST_FRAMES_480 * 2), -EINVAL, "null");
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2 - 1), -EINVAL, "odd");

	zassert_equal(fake_i2s_write_calls(), writes_before, "no I2S writes");
	zassert_equal(mock_drift_update_calls, drift_before, "no drift update");
	zassert_equal(mock_perf_cycle_start_calls, perf_before, "no perf start");
	zassert_equal(mock_stats_underrun_calls, 0, "no underrun count");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - fake_i2s_queued_count(),
		      "no slab side effect");
}

ZTEST(audio_i2s, test_push_before_init_eio)
{
	/* Fresh module state (before hook reset): push without init. */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EIO,
		      "push before init");

	zassert_equal(fake_i2s_write_calls(), 0, "no writes");
	zassert_equal(fake_i2s_queued_count(), 0, "nothing queued");
	zassert_equal(mock_drift_update_calls, 0, "no drift");
	zassert_equal(mock_stats_underrun_calls, 0, "no underrun");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "no slab allocation");
}
