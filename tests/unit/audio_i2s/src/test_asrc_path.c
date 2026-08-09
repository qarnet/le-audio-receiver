/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * ASRC/offload path tests (ASRC + NONE actuator + FLPR offload variant).
 *
 * Locks: pre-state export before every offload attempt; offload success
 * with 1/480/481 frames with no CPU process; transactional post-state
 * import; CPU fallback from unchanged pre-state for every offload fault
 * class, zero-frame and oversized outputs, and import rejection;
 * capacity/error/invalid-frame slab release; sequence accounting;
 * separate repeat-fallback slab; stop reset of ASRC state.
 */

#include <zephyr/ztest.h>

#include "audio_i2s_test_helpers.h"

/* ── pre-state export ────────────────────────────────────────────── */

ZTEST(audio_i2s, test_asrc_export_pre_state_before_offload)
{
	test_init_ok();

	/* First block: module prev state is 0/0/invalid. */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "first push");

	zassert_equal(mock_asrc_state_export_calls, 1, "export before offload");
	zassert_equal(mock_offload_calls, 1, "offload attempted");
	zassert_equal(mock_asrc_last_export_prev_l, 0, "prev l 0");
	zassert_equal(mock_asrc_last_export_prev_r, 0, "prev r 0");
	zassert_false(mock_asrc_last_export_prev_valid, "prev invalid");
	zassert_equal(mock_offload_last_pre_state.phase, mock_asrc_export_phase,
		      "exported phase sentinel reached offload");
	zassert_equal(mock_offload_last_pre_state.prev_l, 0, "pre-state prev l");
	zassert_equal(mock_offload_last_pre_state.prev_valid, 0, "pre-state prev invalid");
	zassert_equal(mock_offload_last_pre_state.prev_r, 0, "pre-state prev r");
	zassert_equal(mock_offload_last_input, test_input_480(), "input pointer passed");
	zassert_equal(mock_offload_last_input_frames, TEST_FRAMES_480, "input frames");
	zassert_equal(mock_offload_last_sequence, 0, "sequence 0 on first block");
	zassert_equal(mock_offload_last_capacity, 481, "capacity 481");
	zassert_equal(mock_offload_last_ppm, 0, "no ppm before start");

	/* Second block: export must reflect committed post-state. */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "second push");
	zassert_equal(mock_asrc_state_export_calls, 2, "exported again");
	zassert_equal(mock_asrc_last_export_prev_l, mock_asrc_import_prev_l,
		      "prev l from committed import");
	zassert_equal(mock_asrc_last_export_prev_r, mock_asrc_import_prev_r,
		      "prev r from committed import");
	zassert_true(mock_asrc_last_export_prev_valid, "prev valid after first block");
	zassert_equal(mock_offload_last_pre_state.prev_l, mock_asrc_import_prev_l,
		      "pre-state prev l on second block");
	zassert_equal(mock_offload_last_sequence, 1, "sequence 1 on second block");
}

/* ── offload success ─────────────────────────────────────────────── */

ZTEST(audio_i2s, test_offload_success_frame_counts)
{
	const uint16_t frame_counts[] = {1, 480, 481};

	for (size_t i = 0; i < ARRAY_SIZE(frame_counts); i++) {
		test_reset_all();
		test_init_ok();
		mock_offload_output_frames = frame_counts[i];

		zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0,
			      "offload %u frames", frame_counts[i]);

		zassert_equal(mock_asrc_process_calls, 0, "no CPU process on offload success");
		zassert_equal(fake_i2s_write_calls(), STARTUP_TOTAL_BLOCKS, "eleven writes");
		zassert_equal(fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX)->size,
			      (size_t)frame_counts[i] * 4, "exact output bytes queued");

		const struct fake_i2s_write_rec *data = fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX);

		if (frame_counts[i] == 1) {
			/* Only 4 bytes written; the rest of the block is
			 * zero-filled pre-allocation padding.
			 */
			zassert_equal(data->snapshot[0], 0xCC, "pattern lo");
			zassert_equal(data->snapshot[1], 0x33, "pattern hi");
			zassert_equal(data->snapshot[2], 0xCC, "pattern lo 2");
			zassert_equal(data->snapshot[3], 0x33, "pattern hi 2");
			for (int b = 4; b < FAKE_I2S_SNAPSHOT_BYTES; b++) {
				zassert_equal(data->snapshot[b], 0, "padding zero %d", b);
			}
		} else {
			zassert_true(test_rec_pattern(data, 0xCC, 0x33), "offload pattern queued");
		}
		zassert_equal(mock_asrc_state_import_calls, 1, "post-state imported");
		zassert_true(audio_i2s_test_is_started(), "started");
	}
}

ZTEST(audio_i2s, test_offload_import_commits_state)
{
	test_init_ok();

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "first push");

	zassert_equal(audio_i2s_test_asrc_prev_l(), mock_asrc_import_prev_l, "prev l committed");
	zassert_equal(audio_i2s_test_asrc_prev_r(), mock_asrc_import_prev_r, "prev r committed");
	zassert_true(audio_i2s_test_asrc_prev_valid(), "prev valid committed");
	zassert_equal(audio_i2s_test_offload_sequence(), 1, "sequence advanced once");

	/* Next export reads the committed ctx phase (proves memcpy commit). */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "second push");
	zassert_equal(mock_asrc_last_export_phase, mock_asrc_import_phase,
		      "ctx phase committed from import");
	zassert_equal(mock_asrc_last_export_step_base, mock_asrc_export_step_base,
		      "step_base preserved");
}

/* ── CPU fallback from unchanged pre-state ───────────────────────── */

ZTEST(audio_i2s, test_offload_error_classes_fallback_cpu)
{
	const int errors[] = {-EAGAIN, -EINVAL, -ETIMEDOUT, -EIO};

	for (size_t i = 0; i < ARRAY_SIZE(errors); i++) {
		test_reset_all();
		test_init_ok();
		mock_offload_ret = errors[i];

		zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0,
			      "fallback push (%d)", errors[i]);

		zassert_equal(mock_asrc_process_calls, 1, "CPU fallback ran");
		zassert_equal(mock_asrc_state_import_calls, 0, "no import on offload fault");
		zassert_equal(mock_asrc_last_prev_l, 0, "CPU from unchanged pre-state l");
		zassert_equal(mock_asrc_last_prev_r, 0, "CPU from unchanged pre-state r");
		zassert_false(mock_asrc_last_prev_valid, "CPU from unchanged pre-state valid");
		zassert_equal(fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX)->size, TEST_BYTES_480,
			      "CPU output queued");
		zassert_true(
			test_rec_pattern(fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX), 0x22, 0x11),
			"CPU pattern queued");
		zassert_equal(audio_i2s_test_offload_sequence(), 1,
			      "sequence advances on fallback block");
		zassert_true(audio_i2s_test_asrc_prev_valid(), "CPU committed prev");
	}
}

ZTEST(audio_i2s, test_offload_zero_and_oversized_fallback)
{
	const uint16_t bad_frames[] = {0, 482};

	for (size_t i = 0; i < ARRAY_SIZE(bad_frames); i++) {
		test_reset_all();
		test_init_ok();
		mock_offload_output_frames = bad_frames[i];

		zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0,
			      "fallback push (%u frames)", bad_frames[i]);

		zassert_equal(mock_asrc_process_calls, 1, "CPU fallback ran");
		zassert_equal(mock_asrc_state_import_calls, 0, "no import for invalid frames");
		zassert_equal(fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX)->size, TEST_BYTES_480,
			      "CPU output overwrites untrusted offload output");
		zassert_true(
			test_rec_pattern(fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX), 0x22, 0x11),
			"CPU pattern (untrusted offload bytes overwritten)");
		zassert_equal(audio_i2s_test_offload_sequence(), 1, "sequence advanced once");
	}
}

ZTEST(audio_i2s, test_offload_import_reject_falls_back)
{
	test_init_ok();
	mock_asrc_state_import_ret = -EINVAL;

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0,
		      "import rejection falls back");

	zassert_equal(mock_asrc_state_import_calls, 1, "import attempted");
	zassert_equal(mock_asrc_process_calls, 1, "CPU fallback ran");
	zassert_equal(mock_asrc_last_prev_l, 0, "CPU from unchanged pre-state");
	zassert_false(mock_asrc_last_prev_valid, "pre-state still invalid");
	zassert_equal(fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX)->size, TEST_BYTES_480,
		      "CPU output queued");
	zassert_true(test_rec_pattern(fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX), 0x22, 0x11),
		     "untrusted offload output overwritten");
	zassert_true(audio_i2s_test_asrc_prev_valid(), "CPU committed prev");
	zassert_equal(audio_i2s_test_offload_sequence(), 1, "sequence advanced once");
}

ZTEST(audio_i2s, test_cpu_fallback_updates_prev_and_queues_output)
{
	test_init_ok();
	mock_offload_ret = -EAGAIN;
	mock_asrc_process_produced = 477;
	mock_asrc_process_next_l = -7;
	mock_asrc_process_next_r = 9;

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "push");

	zassert_equal(audio_i2s_test_asrc_prev_l(), -7, "prev l updated");
	zassert_equal(audio_i2s_test_asrc_prev_r(), 9, "prev r updated");
	zassert_true(audio_i2s_test_asrc_prev_valid(), "prev valid");
	zassert_equal(fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX)->size, 477 * 4,
		      "exact CPU output frames queued");
	zassert_true(test_rec_pattern(fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX), 0x22, 0x11),
		     "CPU pattern");
}

/* ── CPU failure paths ───────────────────────────────────────────── */

ZTEST(audio_i2s, test_cpu_capacity_frees_slab_counts_failure)
{
	test_init_ok();
	mock_offload_ret = -EAGAIN;
	mock_asrc_process_ret = 1;

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -ENOSPC,
		      "capacity failure");

	zassert_equal(mock_perf_asrc_capacity_failure_calls, 1, "capacity failure counted");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "slab freed");
	zassert_equal(fake_i2s_write_calls(), 0, "nothing written");
	zassert_false(audio_i2s_test_is_started(), "not started");
	zassert_equal(audio_i2s_test_offload_sequence(), 0, "sequence not committed");
	zassert_false(audio_i2s_test_asrc_prev_valid(), "prev not committed");
}

ZTEST(audio_i2s, test_cpu_error_frees_slab_no_commit)
{
	test_init_ok();
	mock_offload_ret = -EAGAIN;
	mock_asrc_process_ret = -EINVAL;

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EIO,
		      "CPU error mapped");

	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "slab freed");
	zassert_equal(audio_i2s_test_offload_sequence(), 0, "sequence not committed");
	zassert_false(audio_i2s_test_asrc_prev_valid(), "prev not committed");
}

ZTEST(audio_i2s, test_cpu_produced_invalid_frames_rejected)
{
	const size_t bad_produced[] = {0, 482};

	for (size_t i = 0; i < ARRAY_SIZE(bad_produced); i++) {
		test_reset_all();
		test_init_ok();
		mock_offload_ret = -EAGAIN;
		mock_asrc_process_produced = bad_produced[i];

		zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -ENOSPC,
			      "produced %zu rejected", bad_produced[i]);

		zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "slab freed");
		zassert_equal(fake_i2s_write_calls(), 0, "nothing written");
		zassert_equal(audio_i2s_test_offload_sequence(), 0, "sequence not committed");
		zassert_false(audio_i2s_test_asrc_prev_valid(), "prev not committed");
	}
}

/* ── 360-frame input ─────────────────────────────────────────────── */

ZTEST(audio_i2s, test_offload_reject_360_input_falls_back)
{
	test_init_ok();
	audio_sink_set_input_frames(TEST_FRAMES_360);
	mock_offload_ret = -EAGAIN;

	zassert_equal(audio_sink_push(test_input_360(), TEST_FRAMES_360 * 2), 0,
		      "360-frame fallback push");

	zassert_equal(mock_offload_last_input_frames, TEST_FRAMES_360, "offload saw 360 frames");
	zassert_equal(mock_asrc_last_input_frames, TEST_FRAMES_360, "CPU saw 360 frames");
	zassert_equal(fake_i2s_write_rec(STARTUP_DATA_WRITE_IDX)->size, TEST_BYTES_480,
		      "CPU output frames queued");
	zassert_equal(fake_i2s_write_rec(0)->size, TEST_BYTES_480, "silence size 480-based");
}

/* ── sequence accounting ─────────────────────────────────────────── */

ZTEST(audio_i2s, test_sequence_exactly_once_per_rendered_block)
{
	test_init_ok();

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "offload block");
	zassert_equal(audio_i2s_test_offload_sequence(), 1, "seq 1");

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "offload block 2");
	zassert_equal(mock_offload_last_sequence, 1, "offload got seq 1");
	zassert_equal(audio_i2s_test_offload_sequence(), 2, "seq 2");

	mock_offload_ret = -ETIMEDOUT;
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0,
		      "CPU fallback block");
	zassert_equal(mock_offload_last_sequence, 2, "offload got seq 2");
	zassert_equal(audio_i2s_test_offload_sequence(), 3, "seq 3 after fallback");

	/* Failed block never advances the sequence. */
	mock_offload_ret = -EAGAIN;
	mock_asrc_process_ret = 1;
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -ENOSPC,
		      "capacity failure block");
	zassert_equal(audio_i2s_test_offload_sequence(), 3, "seq unchanged after failure");
}

/* ── repeat fallback ─────────────────────────────────────────────── */

ZTEST(audio_i2s, test_repeat_uses_separate_slab_asrc)
{
	test_start_stream();
	fake_i2s_release_all(); /* free 16 → threshold reached */

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "steady push");

	zassert_equal(mock_perf_repeat_fallback_calls, 1, "repeat counted once");
	zassert_equal(fake_i2s_write_calls(), STARTUP_TOTAL_BLOCKS + 2,
		      "11 startup + data + repeat block");
	zassert_equal(fake_i2s_queued_count(), 2, "both queued");

	const struct fake_i2s_write_rec *data = fake_i2s_write_rec(STARTUP_FIRST_STEADY_WRITE_IDX);
	const struct fake_i2s_write_rec *dup =
		fake_i2s_write_rec(STARTUP_FIRST_STEADY_WRITE_IDX + 1);

	zassert_not_equal(dup->ptr, data->ptr, "separate slab block");
	zassert_equal(dup->size, data->size, "repeat copies saved frame size");
	zassert_equal(dup->size, TEST_BYTES_480, "saved frame size");
	zassert_equal(memcmp(dup->snapshot, data->snapshot, FAKE_I2S_SNAPSHOT_BYTES), 0,
		      "repeat copies latest saved frame exactly");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - 2, "two blocks owned by driver");
	test_assert_no_duplicate_writes();
}

/* ── stop resets ASRC state ──────────────────────────────────────── */

ZTEST(audio_i2s, test_stop_resets_asrc_context)
{
	test_init_ok();

	/* Offload success commits import phase (≠ Q32_ONE). */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "first push");
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "second push");
	zassert_equal(mock_asrc_last_export_phase, mock_asrc_import_phase,
		      "import phase committed");

	audio_sink_stop();
	zassert_false(audio_i2s_test_asrc_prev_valid(), "prev-valid cleared");
	zassert_equal(audio_i2s_test_offload_sequence(), 0, "sequence cleared");
	zassert_equal(mock_asrc_reset_calls, 1, "asrc reset called");

	/* Reconnect: BAP gate closed→open restores push admission. */
	zassert_equal(audio_sink_stream_open(), 0, "stream open");
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "post-stop push");
	zassert_equal(mock_asrc_last_export_phase, ASRC_Q32_ONE,
		      "asrc context reset to first-block phase");
}
