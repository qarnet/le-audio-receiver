/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared startup/ownership tests for the I2S sink suites.
 *
 * Locks the transactional first-push contract: six distinct silence
 * blocks, one data block, then START; every allocation/write/START
 * failure returns the exact primary error, frees caller-owned blocks,
 * DROP-purges driver-owned blocks, and leaves the slab reclaimable.
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/i2s.h>

#include "audio_i2s_test_helpers.h"

/* ── nominal startup ─────────────────────────────────────────────── */

ZTEST(audio_i2s, test_startup_480_seven_blocks_then_start)
{
	test_init_ok();

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "startup push");

	zassert_equal(fake_i2s_write_calls(), 7, "seven blocks written");
	zassert_equal(fake_i2s_trigger_calls(), 1, "exactly one trigger");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_START, "START");
	zassert_equal(fake_i2s_queued_count(), 7, "all seven queued");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - 7, "seven blocks held");
	zassert_true(audio_i2s_test_is_started(), "started");
	zassert_true(audio_i2s_test_is_configured(), "configured");

	for (int i = 0; i < 6; i++) {
		zassert_true(test_rec_silence(fake_i2s_write_rec(i)), "silence block %d", i);
		zassert_equal(fake_i2s_write_rec(i)->size, TEST_BYTES_480, "silence size %d", i);
	}

	/* Seventh write: exact data (identity) or mock ASRC/offload output. */
#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
	zassert_equal(fake_i2s_write_rec(6)->size, TEST_BYTES_480, "data size");
	zassert_true(test_rec_pattern(fake_i2s_write_rec(6), 0xCC, 0x33), "offload pattern queued");
#else
	zassert_equal(fake_i2s_write_rec(6)->size, TEST_BYTES_480, "data size");
	zassert_true(test_rec_matches_input(fake_i2s_write_rec(6), test_input_480()),
		     "exact input bytes queued");
#endif

	test_assert_distinct_pointers(0, 7);
	test_assert_no_duplicate_writes();
}

ZTEST(audio_i2s, test_startup_360_ordering_and_sizes)
{
	test_init_ok();
	audio_sink_set_input_frames(TEST_FRAMES_360);

	zassert_equal(audio_sink_push(test_input_360(), TEST_FRAMES_360 * 2), 0, "startup push");

	zassert_equal(fake_i2s_write_calls(), 7, "seven blocks");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_START, "START");
	for (int i = 0; i < 6; i++) {
		zassert_true(test_rec_silence(fake_i2s_write_rec(i)), "silence %d", i);
	}
	/* Data block size: identity copies the 360 input frames (1440 B);
	 * ASRC queues the resampler output (mock default 480 frames).
	 */
#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
	zassert_equal(fake_i2s_write_rec(6)->size, TEST_BYTES_480, "data size 480 output");
	zassert_true(test_rec_pattern(fake_i2s_write_rec(6), 0xCC, 0x33), "offload pattern");
#else
	zassert_equal(fake_i2s_write_rec(6)->size, TEST_BYTES_360, "data size 360");
	zassert_true(test_rec_matches_input(fake_i2s_write_rec(6), test_input_360()),
		     "exact 360-frame input");
#endif
	test_assert_distinct_pointers(0, 7);
}

ZTEST(audio_i2s, test_startup_silence_rateselect_sizes)
{
	test_init_ok();
	mock_rate_convert_next_ret = 477;

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "startup push");

	for (int i = 0; i < 6; i++) {
		zassert_equal(fake_i2s_write_rec(i)->size, 477 * 2 * 2,
			      "rate-selected silence size %d", i);
	}
	zassert_equal(mock_rate_convert_next_calls, 6, "six rate-converter calls");
	zassert_equal(mock_rate_convert_last_input_frames, TEST_FRAMES_480,
		      "converter fed input frames");
}

/* ── silence allocation failures (each of six positions) ─────────── */

ZTEST(audio_i2s, test_startup_silence_alloc_fail_each_position)
{
	for (int pos = 0; pos < 6; pos++) {
		test_reset_all();
		test_init_ok();

		/* Data block (1) + pos silence blocks must fit; the
		 * (pos+1)-th silence allocation is the first to fail.
		 * Free before push: 16 - held.  Held = 15 - pos.
		 */
		test_prealloc_blocks(15 - pos);

		zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -ENOMEM,
			      "silence alloc fail at %d", pos);

		zassert_equal(fake_i2s_write_calls(), pos, "only %d silence writes", pos);
		zassert_equal(fake_i2s_queued_count(), 0, "queued silence purged");
		zassert_equal(fake_i2s_trigger_calls(), 1, "DROP issued");
		zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_DROP, "DROP command");
		zassert_false(audio_i2s_test_is_started(), "never started");
		zassert_true(audio_i2s_test_is_configured(), "configured retained");
		zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - (15 - pos),
			      "all non-held blocks reclaimed");
		test_assert_no_duplicate_writes();
	}
}

/* ── silence write failures (each index) ─────────────────────────── */

ZTEST(audio_i2s, test_startup_silence_write_fail_each_index)
{
	for (int idx = 0; idx < 6; idx++) {
		test_reset_all();
		test_init_ok();
		fake_i2s_set_write_fail_errno(-EFAULT);
		fake_i2s_fail_write_at(idx);

		zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EFAULT,
			      "write fail at %d", idx);

		zassert_equal(fake_i2s_queued_count(), 0, "earlier silence DROP-purged");
		zassert_equal(fake_i2s_trigger_calls(), 1, "DROP issued");
		zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_DROP, "DROP command");
		zassert_false(audio_i2s_test_is_started(), "no START");
		zassert_true(audio_i2s_test_is_configured(), "configured retained");
		zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS,
			      "failed + data + purged blocks all reclaimed");
		test_assert_no_duplicate_writes();
	}
}

/* ── data write failure ──────────────────────────────────────────── */

ZTEST(audio_i2s, test_startup_data_write_fail)
{
	test_init_ok();
	fake_i2s_set_write_fail_errno(-EFAULT);
	fake_i2s_fail_write_at(6);

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EFAULT,
		      "data write failure");

	zassert_equal(fake_i2s_write_calls(), 7, "six silence + failed data write");
	zassert_equal(fake_i2s_queued_count(), 0, "silence purged, data never queued");
	zassert_equal(fake_i2s_trigger_calls(), 1, "DROP issued");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_DROP, "DROP command");
	zassert_false(audio_i2s_test_is_started(), "no START after incomplete prefill");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "slab fully reclaimable");
	test_assert_no_duplicate_writes();
}

/* ── START failure ───────────────────────────────────────────────── */

ZTEST(audio_i2s, test_startup_start_fail_purges_all_seven)
{
	test_init_ok();
	fake_i2s_set_trigger_ret(I2S_TRIGGER_START, -EIO);

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EIO,
		      "START failure returned");

	zassert_equal(fake_i2s_write_calls(), 7, "all seven written");
	zassert_equal(fake_i2s_queued_count(), 0, "all seven DROP-purged");
	zassert_equal(fake_i2s_trigger_calls(), 2, "START then DROP");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_START, "START attempted");
	zassert_equal(fake_i2s_trigger_rec(1)->cmd, I2S_TRIGGER_DROP, "DROP cleanup");
	zassert_false(audio_i2s_test_is_started(), "not started");
	zassert_true(audio_i2s_test_is_configured(), "configured retained");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "no leaked driver-owned blocks");
	test_assert_no_duplicate_writes();
}

/* ── DROP cleanup failure ────────────────────────────────────────── */

ZTEST(audio_i2s, test_startup_drop_fail_observable_no_double_free)
{
	test_init_ok();
	fake_i2s_set_write_fail_errno(-EFAULT);
	fake_i2s_fail_write_at(2);
	fake_i2s_set_trigger_ret(I2S_TRIGGER_DROP, -EIO);

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EFAULT,
		      "write failure returned");

	/* DROP failed: the two already-queued silence blocks stay
	 * driver-owned.  Recorded (trigger log) but no double free.
	 */
	zassert_equal(fake_i2s_trigger_calls(), 1, "DROP attempted");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_DROP, "DROP recorded");
	zassert_equal(fake_i2s_queued_count(), 2, "queued blocks survive failed DROP");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - 2, "driver still owns two");

	fake_i2s_release_all();
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "released via captured slab");
	test_assert_no_duplicate_writes();
}

/* ── rate-converter output bounds ────────────────────────────────── */

ZTEST(audio_i2s, test_startup_rateselect_482_fails_without_overflow)
{
	test_init_ok();
	mock_rate_convert_next_ret = 482;

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -ENOSPC,
		      "impossible silence count rejected");

	zassert_equal(fake_i2s_write_calls(), 0, "no write beyond slab capacity");
	zassert_equal(fake_i2s_queued_count(), 0, "nothing queued");
	zassert_equal(fake_i2s_trigger_calls(), 1, "DROP issued");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_DROP, "DROP command");
	zassert_false(audio_i2s_test_is_started(), "not started");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "data block released");
}

ZTEST(audio_i2s, test_startup_rateselect_zero_fails)
{
	test_init_ok();
	mock_rate_convert_next_ret = 0;

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -ENOSPC,
		      "zero silence count rejected");

	zassert_equal(fake_i2s_write_calls(), 0, "no writes");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "slab intact");
}

ZTEST(audio_i2s, test_startup_rateselect_partial_then_482)
{
	test_init_ok();
	mock_rate_convert_next_seq_len = 6;
	mock_rate_convert_next_seq[0] = 480;
	mock_rate_convert_next_seq[1] = 480;
	mock_rate_convert_next_seq[2] = 480;
	mock_rate_convert_next_seq[3] = 480;
	mock_rate_convert_next_seq[4] = 480;
	mock_rate_convert_next_seq[5] = 482;

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -ENOSPC,
		      "sixth silence count rejected");

	zassert_equal(fake_i2s_write_calls(), 5, "five silence writes before failure");
	zassert_equal(fake_i2s_queued_count(), 0, "earlier silence DROP-purged");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_DROP, "DROP command");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "slab fully reclaimable");
	for (int i = 0; i < 5; i++) {
		zassert_equal(fake_i2s_write_rec(i)->size, TEST_BYTES_480, "silence size %d", i);
	}
	zassert_false(audio_i2s_test_is_started(), "not started");
}
