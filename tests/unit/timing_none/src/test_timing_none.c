/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Direct no-op timing suite for src/audio_timing_none.c.
 *
 * The nRF5340 timing implementation is intentionally side-effect free.
 * These tests prove the API contract (init returns 0, all calls safe at
 * zero/extreme/typical values, arbitrary call order, repeated use) and
 * give gcovr a real TU to measure — every production function executes.
 */

#include <stdint.h>
#include <limits.h>

#include <zephyr/ztest.h>

#include "audio_timing.h"

ZTEST_SUITE(timing_none, NULL, NULL, NULL, NULL, NULL);

ZTEST(timing_none, test_init_returns_zero)
{
	zassert_equal(0, audio_timing_init(), "init must return 0");
}

ZTEST(timing_none, test_repeated_init_returns_zero)
{
	for (int i = 0; i < 32; i++) {
		zassert_equal(0, audio_timing_init(), "repeated init must return 0 (call %d)", i);
	}
}

ZTEST(timing_none, test_sdu_ref_update_zero_inputs)
{
	audio_timing_sdu_ref_update(0U, 0U);
	/* no-op must be callable with all-zero inputs */
	zassert_true(1, "sdu_ref_update(0,0) must not fault");
}

ZTEST(timing_none, test_sdu_ref_update_extreme_inputs)
{
	audio_timing_sdu_ref_update(UINT32_MAX, UINT32_MAX);
	audio_timing_sdu_ref_update(UINT32_MAX, 0U);
	audio_timing_sdu_ref_update(0U, UINT32_MAX);
	zassert_true(1, "extreme sdu_ref_update inputs must not fault");
}

ZTEST(timing_none, test_sdu_ref_update_typical_values)
{
	/* typical 48 kHz LC3 values: 10 ms ts increments, 40 ms PD */
	uint32_t ts = 1000000U;

	for (int i = 0; i < 64; i++) {
		audio_timing_sdu_ref_update(ts, 40000U);
		ts += 10000U;
	}
	zassert_true(1, "typical sdu_ref_update values must not fault");
}

ZTEST(timing_none, test_reset_repeated)
{
	for (int i = 0; i < 32; i++) {
		audio_timing_reset();
	}
	zassert_true(1, "repeated reset must not fault");
}

ZTEST(timing_none, test_arbitrary_call_order)
{
	audio_timing_reset();
	audio_timing_sdu_ref_update(0U, 0U);
	zassert_equal(0, audio_timing_init());
	audio_timing_sdu_ref_update(UINT32_MAX, 12345U);
	audio_timing_reset();
	audio_timing_sdu_ref_update(777U, 888U);
	zassert_equal(0, audio_timing_init());
	audio_timing_sdu_ref_update(0U, UINT32_MAX);
	audio_timing_reset();
	audio_timing_sdu_ref_update(UINT32_MAX, UINT32_MAX);
	zassert_true(1, "arbitrary call order must not fault");
}

ZTEST(timing_none, test_interleaved_init_sdu_reset_loop)
{
	for (int i = 0; i < 100; i++) {
		zassert_equal(0, audio_timing_init());
		audio_timing_sdu_ref_update((uint32_t)i * 10000U, (uint32_t)i * 100U);
		audio_timing_reset();
	}
	zassert_equal(0, audio_timing_init());
	zassert_true(1, "interleaved loop must not fault");
}
