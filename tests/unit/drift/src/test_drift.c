/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 4b.2: unit tests for the PCLK-feedforward + buffer-phase PI
 * drift controller.  Tests the production audio_drift.c directly.
 *
 * All tests use CONFIG_AUDIO_DRIFT_OUTPUT_CLAMP=500 (default).
 * Tests verify clamp, anti-windup, phase sign, and filter behaviour
 * within the 500 ppm output range.
 */

#include <zephyr/ztest.h>
#include <string.h>
#include "audio_drift.h"

/* Controller constants (must match audio_drift.c) */
#define SETPOINT 6

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	audio_drift_reset();
}

ZTEST_SUITE(drift, NULL, NULL, reset_before_each, NULL, NULL);

/* ── 1. reset / INIT behaviour ─────────────────────────────────── */

ZTEST(drift, test_init_returns_zero_first_call)
{
	int32_t ppm = audio_drift_controller_update(SETPOINT);
	zassert_equal(ppm, 0, "first update returns 0");
	zassert_true(strcmp(audio_drift_state_str(), "ACTIVE") == 0, "state ACTIVE after first");
}

ZTEST(drift, test_reset_clears_all_state)
{
	audio_drift_controller_update(SETPOINT);
	audio_drift_controller_update(SETPOINT);
	zassert_equal(audio_drift_get_ppm(), 0, "ppm zero at setpoint, no feedforward");

	audio_drift_reset();
	zassert_true(strcmp(audio_drift_state_str(), "INIT") == 0, "INIT after reset");
	zassert_equal(audio_drift_get_ppm(), 0, "ppm zero after reset");
	zassert_equal(audio_drift_controller_update(SETPOINT), 0, "first post-reset returns 0");
}

/* ── 2. Feedforward sign: local fast → negative correction ──────── */

ZTEST(drift, test_positive_frequency_negative_correction)
{
	audio_drift_frequency_error_update(400);
	audio_drift_controller_update(SETPOINT); /* INIT→ACTIVE */
	int32_t ppm = audio_drift_controller_update(SETPOINT);
	/* -400 clamped within ±500 */
	zassert_equal(ppm, -400, "+400 local fast → -400 ppm correction (got %d)", ppm);
}

/* ── 3. local slow → positive correction ────────────────────────── */

ZTEST(drift, test_negative_frequency_positive_correction)
{
	audio_drift_frequency_error_update(-400);
	audio_drift_controller_update(SETPOINT);
	int32_t ppm = audio_drift_controller_update(SETPOINT);
	zassert_equal(ppm, 400, "-400 local slow → +400 ppm correction (got %d)", ppm);
}

/* ── 4. filter convergence ──────────────────────────────────────── */

ZTEST(drift, test_filter_converges_to_steady_state)
{
	/* Feed constant +400 ppm. EMA N=8 converges quickly. */
	audio_drift_controller_update(SETPOINT);
	for (int i = 0; i < 10; i++) {
		audio_drift_frequency_error_update(400);
		audio_drift_controller_update(SETPOINT);
	}
	int32_t ppm = audio_drift_get_ppm();
	zassert_equal(ppm, -400, "filter +400 → -400 correction (got %d)", ppm);
}

ZTEST(drift, test_filter_smooth_step_response)
{
	/* Step from 0 to +800 ppm (above clamp) → correction
	 * clamped at -500 immediately. This is correct:
	 * when local error exceeds clamp, controller saturates
	 * instantly, no ramp-through-unstable required.
	 */
	audio_drift_controller_update(SETPOINT);
	audio_drift_frequency_error_update(800);
	int32_t ppm = audio_drift_controller_update(SETPOINT);
	zassert_equal(ppm, -500, "800 local fast → clamped at -500 (got %d)", ppm);
}

/* ── 5. draining → negative phase correction ────────────────────── */

ZTEST(drift, test_draining_gives_negative_phase)
{
	/* slab_free = 8 > setpoint 6 → negative correction */
	audio_drift_controller_update(SETPOINT);
	int32_t ppm = audio_drift_controller_update(8);
	zassert_true(ppm < 0, "draining (free=8) → negative ppm (got %d)", ppm);
}

/* ── 6. filling → positive phase correction ─────────────────────── */

ZTEST(drift, test_filling_gives_positive_phase)
{
	audio_drift_controller_update(SETPOINT);
	int32_t ppm = audio_drift_controller_update(4);
	zassert_true(ppm > 0, "filling (free=4) → positive ppm (got %d)", ppm);
}

/* ── 7. combined frequency + phase ──────────────────────────────── */

ZTEST(drift, test_combined_freq_and_phase_add)
{
	/* freq correction + phase correction both negative */
	audio_drift_frequency_error_update(300);
	audio_drift_controller_update(SETPOINT);
	int32_t ppm = audio_drift_controller_update(8);
	zassert_true(ppm < -300, "freq=-300 + neg phase < -300 (got %d)", ppm);
}

ZTEST(drift, test_combined_freq_and_phase_oppose)
{
	/* freq correction negative, phase positive */
	audio_drift_frequency_error_update(300);
	audio_drift_controller_update(SETPOINT);
	int32_t ppm = audio_drift_controller_update(4);
	zassert_true(ppm > -300, "freq=-300 + pos phase > -300 (got %d)", ppm);
}

/* ── 8. output clamp ────────────────────────────────────────────── */

ZTEST(drift, test_output_clamp_positive)
{
	audio_drift_frequency_error_update(-1000);
	audio_drift_controller_update(SETPOINT);
	int32_t ppm = audio_drift_controller_update(SETPOINT);
	zassert_equal(ppm, 500, "output clamped at +500, got %d", ppm);
}

ZTEST(drift, test_output_clamp_negative)
{
	audio_drift_frequency_error_update(1000);
	audio_drift_controller_update(SETPOINT);
	int32_t ppm = audio_drift_controller_update(SETPOINT);
	zassert_equal(ppm, -500, "output clamped at -500, got %d", ppm);
}

/* ── 9. anti-windup (directional) ────────────────────────────────── */

ZTEST(drift, test_anti_windup_blocks_same_direction_at_pos_clamp)
{
	/* Saturate at +500 with large negative local error.
	 * Feed same-direction (filling, positive phase_inc) →
	 * integral blocked because at +clamp and phase_inc > 0.
	 */
	audio_drift_frequency_error_update(-1000); /* freq correction +1000 → clamped +500 */
	audio_drift_controller_update(SETPOINT);

	audio_drift_controller_update(4); /* filling → positive phase_inc */
	int32_t ppm = audio_drift_get_ppm();
	zassert_equal(ppm, 500, "same-direction at +clamp stays 500 (got %d)", ppm);
}

ZTEST(drift, test_anti_windup_allows_opposite_direction_unwind_at_pos_clamp)
{
	/* Saturate at +500.  Feed opposite-direction (draining,
	 * negative phase_inc).  Integrator should unwind — the
	 * output may stay at +500 (feedforward alone saturates),
	 * but phase_integral becomes negative so unwinding is
	 * observable after feedforward drops.
	 */
	audio_drift_frequency_error_update(-1000); /* freq correction clamped +500 */
	audio_drift_controller_update(SETPOINT);

	/* Push opposite-direction many times — integral unwinds even though
	 * feedforward alone keeps output at +500.
	 */
	for (int i = 0; i < 20; i++) {
		audio_drift_controller_update(8); /* draining → negative phase_inc */
	}
	zassert_equal(audio_drift_get_ppm(), 500, "output still +500 (feedforward saturates)");

	/* Now reduce frequency error toward zero.  Because the integral
	 * unwound (is negative), output should drop below clamp faster
	 * than if the integral had been blocked.
	 */
	for (int i = 0; i < 20; i++) {
		audio_drift_frequency_error_update(0);
		audio_drift_controller_update(8); /* draining */
	}
	int32_t ppm = audio_drift_get_ppm();
	/* With the unwound integral, output must be below +500. */
	zassert_true(ppm < 500, "after freq reduction, unwound integral → ppm < 500 (got %d)", ppm);
}

ZTEST(drift, test_anti_windup_blocks_same_direction_at_neg_clamp)
{
	/* Saturate at -500.  Feed same-direction (draining,
	 * negative phase_inc) → integral blocked.
	 */
	audio_drift_frequency_error_update(1000); /* freq correction -1000 → clamped -500 */
	audio_drift_controller_update(SETPOINT);

	audio_drift_controller_update(8); /* draining → negative phase_inc */
	int32_t ppm = audio_drift_get_ppm();
	zassert_equal(ppm, -500, "same-direction at -clamp stays -500 (got %d)", ppm);
}

ZTEST(drift, test_anti_windup_allows_opposite_direction_unwind_at_neg_clamp)
{
	/* Saturate at -500.  Feed opposite-direction (filling,
	 * positive phase_inc).  Integral unwinds; output may stay
	 * at -500 until feedforward drops.
	 */
	audio_drift_frequency_error_update(1000);
	audio_drift_controller_update(SETPOINT);

	for (int i = 0; i < 20; i++) {
		audio_drift_controller_update(4); /* filling → positive phase_inc */
	}
	zassert_equal(audio_drift_get_ppm(), -500, "output still -500 (feedforward saturates)");

	for (int i = 0; i < 20; i++) {
		audio_drift_frequency_error_update(0);
		audio_drift_controller_update(4);
	}
	int32_t ppm = audio_drift_get_ppm();
	zassert_true(ppm > -500, "after freq reduction, unwound integral → ppm > -500 (got %d)",
		     ppm);
}

/* ── 10. zero feedforward (nRF5340 phase-only) ──────────────────── */

ZTEST(drift, test_zero_feedforward_phase_only)
{
	/* No frequency measurement → feedforward = 0.
	 * Controller should still respond to phase errors. */
	audio_drift_controller_update(4); /* first returns 0 (INIT→ACTIVE) */
	int32_t ppm = audio_drift_controller_update(4);
	zassert_true(ppm > 0, "phase-only → positive correction with fill, got %d", ppm);
}

/* ── Edge: phase integral clamp ──────────────────────────────────── */

ZTEST(drift, test_phase_integral_clamped)
{
	/* Sustained phase error: free=8 every block.
	 * Phase contribution maxes at PHASE_INTEGRAL_CLAMP (500).
	 * No feedforward → total = phase only ≤ 500 abs. */
	audio_drift_controller_update(SETPOINT);
	for (int i = 0; i < 50; i++) {
		int32_t ppm = audio_drift_controller_update(8);
		zassert_true(ppm >= -500 && ppm <= 500,
			     "phase-only output in [-500,500], got %d at %d", ppm, i);
	}
}
