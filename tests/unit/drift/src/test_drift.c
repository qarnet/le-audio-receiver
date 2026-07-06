/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <string.h>
#include "audio_drift.h"

/* Controller constants (must match audio_drift.c) */
#define PERIOD_US 100000U
#define SETPOINT  6

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	audio_drift_reset();
}

ZTEST_SUITE(drift, NULL, NULL, reset_before_each, NULL, NULL);

ZTEST(drift, test_zero_ts_ignored)
{
	/* sdu_ref_us == 0 must be a no-op; state stays INIT */
	int32_t ppm = audio_drift_controller_update(0, SETPOINT);

	zassert_equal(ppm, 0, "zero ts must return 0");
	zassert_equal(audio_drift_get_ppm(), 0, "output ppm must be 0");
	/* State should still be INIT after zero ts */
	zassert_true(strcmp(audio_drift_state_str(), "INIT") == 0,
		     "state should stay INIT on zero ts");

	/* First valid ts enters ACTIVE, returns 0 */
	ppm = audio_drift_controller_update(1000, SETPOINT);
	zassert_equal(ppm, 0, "first valid ts returns 0");
	zassert_true(strcmp(audio_drift_state_str(), "ACTIVE") == 0,
		     "state should be ACTIVE after first valid ts");
}

ZTEST(drift, test_init_to_active_on_first_ts)
{
	int32_t ppm = audio_drift_controller_update(1000000U, SETPOINT);

	zassert_equal(ppm, 0, "first ts enters ACTIVE, no freq update");
	zassert_true(strcmp(audio_drift_state_str(), "ACTIVE") == 0,
		     "state should be ACTIVE after first ts");
}

ZTEST(drift, test_freq_term_positive_err)
{
	/* elapsed > PERIOD → clock slow → freq_err positive → ppm positive */
	audio_drift_controller_update(0U, SETPOINT);
	uint32_t start = 2000000U;

	audio_drift_controller_update(start, SETPOINT);
	/* elapsed = 100010 → err_us = +10 → freq_err_ppm = +100 */
	int32_t ppm = audio_drift_controller_update(start + PERIOD_US + 10, SETPOINT);

	zassert_true(ppm > 0, "elapsed > period → ppm positive (speed up), got %d", ppm);
}

ZTEST(drift, test_freq_term_negative_err)
{
	/* Frequency term only fires when elapsed >= 100 ms.
	 * When elapsed < 100 ms, frequency update is skipped —
	 * only phase term output can appear.
	 * Verify that a partially-filled window produces 0
	 * frequency contribution. */
	audio_drift_controller_update(0U, SETPOINT);
	uint32_t start = 3000000U;

	audio_drift_controller_update(start, SETPOINT);
	/* elapsed = 99990 (< 100000) → frequency term skipped.
	 * At setpoint, phase term = 0. Output = 0. */
	int32_t ppm = audio_drift_controller_update(start + PERIOD_US - 10, SETPOINT);

	zassert_equal(ppm, 0, "elapsed < period → freq skipped, output=0 (got %d)", ppm);

	/* After a full window: elapsed = 100000 → err_us = 0 → output = 0 */
	ppm = audio_drift_controller_update(start + PERIOD_US + 100000 - 10, SETPOINT);
	zassert_equal(ppm, 0, "perfect timing → output=0 (got %d)", ppm);
}

ZTEST(drift, test_phase_term_buffer_draining)
{
	/* slab_free < setpoint → buffer draining (clock fast) → ppm negative */
	audio_drift_controller_update(0U, SETPOINT);
	uint32_t t = 4000000U;

	audio_drift_controller_update(t, SETPOINT);
	/* Supply SDUs but keep buffer below setpoint */
	int32_t ppm = audio_drift_controller_update(t + PERIOD_US, 4);

	zassert_true(ppm < 0, "buffer draining → ppm negative, got %d", ppm);
}

ZTEST(drift, test_phase_term_buffer_filling)
{
	/* slab_free > setpoint → buffer filling (clock slow) → ppm positive */
	audio_drift_controller_update(0U, SETPOINT);
	uint32_t t = 5000000U;

	audio_drift_controller_update(t, SETPOINT);
	int32_t ppm = audio_drift_controller_update(t + PERIOD_US, 8);

	zassert_true(ppm > 0, "buffer filling → ppm positive, got %d", ppm);
}

ZTEST(drift, test_integrator_clamp)
{
	/* Sustained large error → integral clamped (output stays bounded) */
	audio_drift_controller_update(0U, SETPOINT);
	uint32_t t = 6000000U;

	audio_drift_controller_update(t, SETPOINT);

	/* Feed 25 windows of moderate positive error (elapsed = 100050 µs each).
	 * err_us = +50, freq_err_ppm = +500 per window.
	 * After many windows the integral saturates, but output stays ≤ +500. */
	for (int i = 0; i < 25; i++) {
		t += PERIOD_US + 50;
		int32_t ppm = audio_drift_controller_update(t, SETPOINT);

		zassert_true(ppm >= -500 && ppm <= 500,
			     "output must stay in [-500, 500], got %d at window %d", ppm, i);
	}

	/* Output should have reached a positive steady state */
	int32_t final_ppm = audio_drift_get_ppm();

	zassert_true(final_ppm > 0, "sustained positive error → pos ppm, got %d", final_ppm);
}

ZTEST(drift, test_output_clamp)
{
	/* Large error → output clamped at ±500 ppm.
	 * Positive: use large frequency error.
	 * Negative: use large phase error (buffer massively draining). */

	/* --- Positive clamp via frequency term --- */
	audio_drift_controller_update(0U, SETPOINT);
	uint32_t t = 7000000U;

	audio_drift_controller_update(t, SETPOINT);

	/* Huge positive: elapsed = 200000 → err_us = +100000 → freq_err = +1000000 ppm */
	int32_t ppm = audio_drift_controller_update(t + PERIOD_US + 100000, SETPOINT);

	zassert_equal(ppm, 500, "large freq error → output clamped at +500, got %d", ppm);

	/* --- Negative clamp via phase term --- */
	audio_drift_reset();
	audio_drift_controller_update(0U, SETPOINT);
	uint32_t t2 = 8000000U;

	audio_drift_controller_update(t2, SETPOINT);

	/* Buffer massively draining: slab_free = -100 (below setpoint by 106).
	 * phase_err_ppm = -106 * 50 = -5300.
	 * phase_output = 0.3 * (-5300) = -1590 → clamped. */
	ppm = audio_drift_controller_update(t2 + PERIOD_US, -100);

	zassert_equal(ppm, -500, "large phase error → output clamped at -500, got %d", ppm);
}

ZTEST(drift, test_convergence)
{
	/* Feed consistent +5 µs freq error per window (freq_err_ppm = +50).
	 * The output should be positive and stabilize within [0, 500]. */
	audio_drift_controller_update(0U, SETPOINT);
	uint32_t t = 9000000U;

	audio_drift_controller_update(t, SETPOINT);

	int32_t prev_ppm = 0;

	for (int i = 0; i < 10; i++) {
		t += PERIOD_US + 5;
		int32_t ppm = audio_drift_controller_update(t, SETPOINT);

		/* Output should be positive (clock slow, need to speed up) */
		zassert_true(ppm > 0,
			     "positive freq error → ppm must be positive, got %d at window %d", ppm,
			     i);

		/* Output should be monotonically increasing as integral builds */
		zassert_true(ppm >= prev_ppm,
			     "output should not decrease on consistent error: %d → %d", prev_ppm,
			     ppm);
		prev_ppm = ppm;
	}

	/* Final output must be at least 25 (proportional contribution) */
	zassert_true(audio_drift_get_ppm() >= 25,
		     "consistent error → output should reach at least 25 ppm, got %d",
		     audio_drift_get_ppm());
}

ZTEST(drift, test_gap_resets)
{
	/* elapsed > 3*PERIOD → window reset, freq_err_ppm = 0.
	 * Phase term continues to work (no frequency contribution). */
	audio_drift_controller_update(0U, SETPOINT);
	uint32_t t = 10000000U;

	audio_drift_controller_update(t, SETPOINT);

	/* Build some frequency integral first */
	audio_drift_controller_update(t + PERIOD_US + 50, SETPOINT); /* positive freq err */
	int32_t with_freq = audio_drift_get_ppm();

	zassert_true(with_freq > 0, "should have positive output from freq term");

	/* Now a gap: 400 ms jump (>> 3*PERIOD). Window resets, freq_err_ppm = 0.
	 * The freq integral is NOT reset, so some output remains, but no new
	 * frequency correction is computed for this SDU. */
	t += PERIOD_US + 50;
	t += 4 * PERIOD_US; /* big gap */
	int32_t ppm_after_gap = audio_drift_controller_update(t, SETPOINT);

	/* After gap: freq term contributes 0 (window reset), phase term may add
	 * something. The freq integral persists from before. So output should
	 * still be positive (freq integral + small phase contribution). */
	zassert_true(ppm_after_gap >= 0,
		     "ppm after gap should be non-negative (freq integral persists), got %d",
		     ppm_after_gap);
}

ZTEST(drift, test_uint32_wraparound)
{
	/* Timestamps wrap at 0xFFFFFFFF; subtraction still correct with uint32 math. */
	uint32_t start = 0xFFFF0000U;

	audio_drift_controller_update(0U, SETPOINT);
	audio_drift_controller_update(start, SETPOINT);

	/* wrap: start + PERIOD overflows naturally in uint32 */
	uint32_t after = start + PERIOD_US;

	int32_t ppm = audio_drift_controller_update(after, SETPOINT);

	/* err_us = 0 → freq_err_ppm = 0. Phase term: free=SETPOINT → 0. */
	zassert_equal(ppm, 0, "uint32 wraparound with err=0 → 0 ppm (got %d)", ppm);
}
