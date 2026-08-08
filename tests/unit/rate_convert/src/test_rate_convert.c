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
 * Test 3 — remainder state determinism
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
