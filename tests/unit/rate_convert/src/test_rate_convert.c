/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for audio_rate_convert — pure-integer nearest-neighbor
 * stereo resampling with remainder accumulation.
 */

#include <zephyr/ztest.h>
#include <string.h>
#include "audio_rate_convert.h"

ZTEST_SUITE(rate_convert, NULL, NULL, NULL, NULL, NULL);

/*
 * Test 1 — identity: 48k → 48k
 *
 * 100 calls × 480 input frames should yield:
 *   - Exactly 48,000 total output frames.
 *   - Every individual block must be exactly 480.
 */
ZTEST(rate_convert, test_identity_100_blocks)
{
	struct audio_rate_converter ctx;

	audio_rate_converter_init(&ctx, 48000, 48000);

	size_t total = 0;
	bool any_non_480 = false;

	for (int i = 0; i < 100; i++) {
		size_t out = audio_rate_converter_next_frames(&ctx, 480);

		total += out;
		if (out != 480) {
			any_non_480 = true;
		}
	}

	zassert_equal(total, 48000, "total output = 48000");
	zassert_false(any_non_480, "every block = 480");
}

/*
 * Test 2 — nRF54L15 baseline: 48k → 47,619
 *
 * 100 calls × 480 input frames should yield:
 *   - Exactly 47,619 total output frames.
 *   - Individual blocks must be 476 or 477 (no other values).
 */
ZTEST(rate_convert, test_nrf54l15_baseline_100_blocks)
{
	struct audio_rate_converter ctx;

	audio_rate_converter_init(&ctx, 48000, 47619);

	size_t total = 0;
	bool any_out_of_range = false;

	for (int i = 0; i < 100; i++) {
		size_t out = audio_rate_converter_next_frames(&ctx, 480);

		total += out;
		if (out < 476 || out > 477) {
			any_out_of_range = true;
		}
	}

	zassert_equal(total, 47619, "total output = 47619");
	zassert_false(any_out_of_range, "every block 476 or 477");
}

/*
 * Test 3 — nearest-stereo preserves L/R pairing
 *
 * Known short input; verify each output pair is a faithful copy
 * of some input pair (not a cross-channel mix).
 */
ZTEST(rate_convert, test_nearest_stereo_lr_pairing)
{
	int16_t input[16] = {
		100,  200,  /* stereo pair 0 */
		300,  400,  /* stereo pair 1 */
		500,  600,  /* stereo pair 2 */
		700,  800,  /* stereo pair 3 */
		900,  1000, /* stereo pair 4 */
		1100, 1200, /* stereo pair 5 */
		1300, 1400, /* stereo pair 6 */
		1500, 1600, /* stereo pair 7 */
	};
	int16_t output[20] = {0}; /* up to 10 stereo pairs */
	size_t output_frames = 5; /* downsample 8 → 5 */

	audio_rate_converter_nearest_stereo(input, 8, output, 5);

	/* Every output pair must be one of the known input pairs. */
	const int16_t *pairs[] = {
		&input[0], &input[2],  &input[4],  &input[6],
		&input[8], &input[10], &input[12], &input[14],
	};

	for (size_t i = 0; i < output_frames; i++) {
		int16_t l = output[i * 2];
		int16_t r = output[i * 2 + 1];

		/* Verify L+R match one of the input pairs */
		bool found = false;

		for (size_t j = 0; j < 8; j++) {
			if (pairs[j][0] == l && pairs[j][1] == r) {
				found = true;
				break;
			}
		}
		zassert_true(found, "output pair %zu (%d,%d) matches an input pair", i, l, r);
	}

	/* Verify endpoints: first output should be near first input. */
	zassert_true(output[0] == 100 && output[1] == 200, "first output pair = first input pair");

	/* Verify at least one pair from near the middle is used. */
	bool saw_middle = false;

	for (size_t i = 0; i < output_frames; i++) {
		if (output[i * 2] >= 700 && output[i * 2] <= 1100) {
			saw_middle = true;
		}
	}
	zassert_true(saw_middle, "middle-range input pair present in output");
}

/*
 * Test 4 — nearest-stereo endpoints for downsample
 *
 * 4 → 3 frames: must use first and last input frames.
 */
ZTEST(rate_convert, test_nearest_stereo_endpoints_downsample)
{
	int16_t input[8] = {10, 20, 30, 40, 50, 60, 70, 80};
	int16_t output[6] = {0};

	audio_rate_converter_nearest_stereo(input, 4, output, 3);

	/* First output should be input[0] */
	zassert_equal(output[0], 10, "L0");
	zassert_equal(output[1], 20, "R0");

	/* Last output should be input[6] */
	zassert_equal(output[4], 70, "L_last");
	zassert_equal(output[5], 80, "R_last");
}

/*
 * Test 5 — nearest-stereo identity (same input/output size)
 *
 * 480 → 480: output must be an exact copy.
 */
ZTEST(rate_convert, test_nearest_stereo_identity_copy)
{
	enum {
		NF = 480
	};
	int16_t input[NF * 2];
	int16_t output[NF * 2] = {0};

	/* Fill input with known pattern: L = i*2, R = i*2+1 */
	for (int i = 0; i < NF; i++) {
		input[i * 2] = (int16_t)(i * 2);
		input[i * 2 + 1] = (int16_t)(i * 2 + 1);
	}

	audio_rate_converter_nearest_stereo(input, NF, output, NF);

	zassert_mem_equal(input, output, sizeof(input), "identity output = input");
}

/*
 * Test 6 — remainder state determinism
 *
 * Two converters initialized identically must produce identical output.
 */
ZTEST(rate_convert, test_remainder_determinism)
{
	struct audio_rate_converter a, b;

	audio_rate_converter_init(&a, 48000, 47619);
	audio_rate_converter_init(&b, 48000, 47619);

	for (int i = 0; i < 50; i++) {
		size_t oa = audio_rate_converter_next_frames(&a, 480);
		size_t ob = audio_rate_converter_next_frames(&b, 480);

		zassert_equal(oa, ob, "iteration %d: identical output", i);
	}
}

/*
 * Test 7 — re-init resets state
 *
 * After some calls, re-initializing returns to base behavior.
 */
ZTEST(rate_convert, test_reinit_resets)
{
	struct audio_rate_converter ctx;

	audio_rate_converter_init(&ctx, 48000, 47619);

	/* Advance 10 iterations to build up remainder. */
	for (int i = 0; i < 10; i++) {
		(void)audio_rate_converter_next_frames(&ctx, 480);
	}

	/* Re-init and verify first output is same as fresh context. */
	audio_rate_converter_init(&ctx, 48000, 47619);

	struct audio_rate_converter fresh;

	audio_rate_converter_init(&fresh, 48000, 47619);

	for (int i = 0; i < 10; i++) {
		size_t o1 = audio_rate_converter_next_frames(&ctx, 480);
		size_t o2 = audio_rate_converter_next_frames(&fresh, 480);

		zassert_equal(o1, o2, "re-initialized matches fresh at iteration %d", i);
	}
}

/*
 * Test 8 — remainder accumulation correctness
 *
 * Verify that 476 and 477 appear in the correct proportion
 * (5:1 ratio = ~83% 476, ~17% 477 for 48k→47,619).
 */
ZTEST(rate_convert, test_remainder_proportion)
{
	struct audio_rate_converter ctx;

	audio_rate_converter_init(&ctx, 48000, 47619);

	size_t count_476 = 0;
	size_t count_477 = 0;

	for (int i = 0; i < 600; i++) {
		size_t out = audio_rate_converter_next_frames(&ctx, 480);

		if (out == 476) {
			count_476++;
		} else if (out == 477) {
			count_477++;
		} else {
			zassert_unreachable("unexpected output frame count");
		}
	}

	/*
	 * Ratio: 48000/47619 → 81% 476-frames, 19% 477-frames.
	 * Over 600 blocks: 486 × 476, 114 × 477.
	 * Allow ±2 for rounding at block boundary.
	 */
	zassert_within(count_476, 486, 2, "476-frames count near expected 486 (got %zu)",
		       count_476);
	zassert_within(count_477, 114, 2, "477-frames count near expected 114 (got %zu)",
		       count_477);
	zassert_equal(count_476 + count_477, 600, "total blocks = 600");
}

/*
 * Test 9 — nearest-stereo with single output
 *
 * Edge case: 480 input → 1 output frame.
 */
ZTEST(rate_convert, test_nearest_stereo_single_output)
{
	int16_t input[960] = {0}; /* 480 stereo pairs */
	int16_t output[2];

	/* Mark the middle frame distinct */
	input[480] = 42; /* L of frame 240 */
	input[481] = 99; /* R of frame 240 */

	audio_rate_converter_nearest_stereo(input, 480, output, 1);

	/* Should use the middle frame (nearest neighbor). */
	zassert_equal(output[0], 42, "L of middle frame");
	zassert_equal(output[1], 99, "R of middle frame");
}

/*
 * Test 10 — empty input/output no-op
 *
 * Verify no crash on degenerate inputs.
 */
ZTEST(rate_convert, test_nearest_stereo_empty)
{
	int16_t buf[4] = {1, 2, 3, 4};

	/* Both zero */
	audio_rate_converter_nearest_stereo(buf, 0, buf, 0);
	zassert_equal(buf[0], 1, "unchanged");

	/* Zero output frames */
	audio_rate_converter_nearest_stereo(buf, 4, buf, 0);
	zassert_equal(buf[0], 1, "unchanged after zero output");
}
