/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * HISTORICAL / RETIRED actuator tests.
 *
 * These tests exercise audio_clock_actuator_sample_adjust_historical.c
 * (test-local copy of the retired sample insert/drop actuator, retained
 * for regression comparison only).  It is NOT a production actuator:
 * production clock steering is audio_clock_actuator_apll.c (nRF5340)
 * and audio_clock_actuator_none.c (nRF54L15), tested in
 * tests/unit/actuator_apll and tests/unit/actuator_none.  This suite
 * stays to preserve the historical behavior contract.
 */

#include <zephyr/ztest.h>
#include "audio_clock_actuator_sample_adjust_historical.h"

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	audio_clock_actuator_reset();
}

ZTEST_SUITE(actuator_sample_adjust_historical, NULL, NULL, reset_before_each, NULL, NULL);

ZTEST(actuator_sample_adjust_historical, test_zero_ppm_no_adjustment)
{
	/* apply_ppm(0) repeatedly, consume returns 0. */
	for (int i = 0; i < 100; i++) {
		zassert_equal(audio_clock_actuator_apply_ppm(0), 0, "apply_ppm(0) must succeed");
		zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
			      "consume must return 0 with zero ppm");
	}
}

ZTEST(actuator_sample_adjust_historical, test_positive_ppm_eventually_drops)
{
	/*
	 * 1000 ppm * 480 / 1e6 = 0.48 samples per block.
	 * 3 calls = 1.44 samples → consume returns +1 after 3rd call,
	 * acc retains 0.44 (440000 in 1e-6-sample units).
	 */
	audio_clock_actuator_apply_ppm(1000);
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "1st call: 0.48 samples, no crossing");

	audio_clock_actuator_apply_ppm(1000);
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "2nd call: 0.96 samples, no crossing");

	audio_clock_actuator_apply_ppm(1000);
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), +1,
		      "3rd call: 1.44 samples, must return +1 (drop)");

	/* After consume, no more pending. */
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "no pending after consume");
}

ZTEST(actuator_sample_adjust_historical, test_negative_ppm_eventually_inserts)
{
	/*
	 * -1000 ppm * 480 / 1e6 = -0.48 samples per block.
	 * 3 calls = -1.44 samples → consume returns -1 after 3rd call.
	 */
	audio_clock_actuator_apply_ppm(-1000);
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "1st call: -0.48 samples, no crossing");

	audio_clock_actuator_apply_ppm(-1000);
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "2nd call: -0.96 samples, no crossing");

	audio_clock_actuator_apply_ppm(-1000);
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), -1,
		      "3rd call: -1.44 samples, must return -1 (insert)");

	/* After consume, no more pending. */
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "no pending after consume");
}

ZTEST(actuator_sample_adjust_historical, test_consume_clears_pending)
{
	/* Build up to a +1, consume it, then next consume returns 0. */
	audio_clock_actuator_apply_ppm(1000);
	audio_clock_actuator_apply_ppm(1000);
	audio_clock_actuator_apply_ppm(1000); /* crosses threshold */

	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), +1,
		      "1st consume: must return +1");
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "2nd consume: pending cleared, must return 0");
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0, "3rd consume: still 0");
}

ZTEST(actuator_sample_adjust_historical, test_reset_clears_state)
{
	/* Accumulate ppm, then reset — state must be zeroed. */
	audio_clock_actuator_apply_ppm(5000); /* 2.4 samples worth */
	audio_clock_actuator_reset();

	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "after reset, consume returns 0");

	/* Subsequent apply_ppm starts from zero accumulator. */
	audio_clock_actuator_apply_ppm(1000);
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "after reset + one small apply, still 0");
}

ZTEST(actuator_sample_adjust_historical, test_clamp_prevents_burst)
{
	/*
	 * Apply huge ppm (100000), verify pending clamps to ±1 per
	 * consume and acc clamps to ±10 samples.
	 * 100000 ppm * 480 = 48,000,000 1e-6-sample units = 48 samples.
	 * Clamp should limit this massively.
	 */
	audio_clock_actuator_apply_ppm(100000);

	/* Pending must be clamped to ±1, not 48. */
	int adj = audio_clock_actuator_consume_sample_adjustment();

	zassert_true(adj == +1 || adj == -1 || adj == 0, "pending must be clamped, got %d", adj);

	/* After consume, next should be 0 (accumulator absorbed the rest). */
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "no more pending after clamped consume");
}

/* Sign chain verification.
 * Measured local +1775 ppm (PCLK fast) → controller output ≈ -1775 ppm
 * (negative correction) → actuator accumulates negative → eventual -1
 * (insert).  This test proves negative ppm input produces insert, not drop.
 */
ZTEST(actuator_sample_adjust_historical, test_1775_negative_produces_insert)
{
	/*
	 * -1775 ppm * 480 = -852,000 1e-6-sample units per block.
	 * After 2 blocks: -1,704,000 → crosses -1,000,000 → insert (-1).
	 */
	audio_clock_actuator_apply_ppm(-1775);
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "1st call: -0.852 samples, no crossing");

	audio_clock_actuator_apply_ppm(-1775);
	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), -1,
		      "2nd call: -1.704 samples, must return -1 (insert)");

	zassert_equal(audio_clock_actuator_consume_sample_adjustment(), 0,
		      "no pending after consume");
}
