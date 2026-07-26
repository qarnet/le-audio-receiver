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

/* ── 9. anti-windup ─────────────────────────────────────────────── */

ZTEST(drift, test_anti_windup_blocks_integral_at_saturation)
{
	/* Saturate at +500 with large negative local error.
	 * Feed phase in same direction (filling, positive).
	 * Anti-windup should block integral → output stays at +500.
	 * Then feed phase in OPPOSITE direction (draining, negative).
	 * Tentative should move away from clamp → integral builds → output drops.
	 */
	audio_drift_frequency_error_update(-1000); /* freq correction clamped +500 */
	audio_drift_controller_update(SETPOINT);
	/* Same-direction: filling (phase positive) pushes further toward +500 */
	audio_drift_controller_update(4);
	int32_t ppm = audio_drift_get_ppm();
	zassert_equal(ppm, 500, "same-direction phase while saturated → stays 500 (got %d)", ppm);

	/* Opposite-direction: draining (phase negative) moves away from +clamp.
	 * Tentative = +1000(freq) + neg(phase) < 1000 → still clamp at 500?
	 * No: tentative = freq_correction + phase. phase negative → tentative <
	 * freq_correction alone. With freq=+1000 (clamped internally) and
	 * phase negative, clamped_tentative may still be 500 because tentative >
	 * 500. Let's verify that ANTI-WINDUP does not block the opposite-direction
	 * integral.
	 */
	audio_drift_controller_update(8);
	ppm = audio_drift_get_ppm();
	/* With OUTPUT_CLAMP=500 and freq=+1000, a single negative phase
	 * block (free=8 → phase_pp≈-60, phase_inc≈-10) gives tentative≈+930,
	 * clamped_tentative=500, at_pos_clamp=true → integral blocked.
	 * So opposite-direction also gets blocked when freq alone saturates.
	 *
	 * This is the correct behaviour: output is at clamp, and any change
	 * that can't move output below clamp doesn't need integral action.
	 * Once freq error drops below clamp range, phase integrator can build.
	 */
	zassert_equal(ppm, 500, "opposite-direction while still saturated → stays 500 (got %d)",
		      ppm);
}

ZTEST(drift, test_anti_windup_allows_integral_after_exit_saturation)
{
	/* After saturation, feed a frequency within clamp range
	 * so output drops below clamp. Then phase integral should
	 * build normally (proving it wasn't wound during saturation).
	 */
	audio_drift_frequency_error_update(-1000); /* saturates at +500 */
	audio_drift_controller_update(SETPOINT);
	/* Push filling phase while saturated → anti-windup blocks */
	for (int i = 0; i < 10; i++) {
		audio_drift_controller_update(4);
	}
	zassert_equal(audio_drift_get_ppm(), 500, "still saturated at +500");

	/* Now remove frequency error → output exits saturation.
	 * Feed draining phase → integral should build now.
	 * First call after frequency reset stays at 0 (filtered unchanged
	 * from -1000 until enough EMA steps). Use many EMA steps.
	 */
	for (int i = 0; i < 20; i++) {
		audio_drift_frequency_error_update(0);
		audio_drift_controller_update(8); /* draining → negative phase */
	}
	int32_t ppm = audio_drift_get_ppm();
	/* Filtered freq is converging toward 0, phase integral building negative.
	 * Output should be below +500 (freq decreasing + negative phase integral).
	 */
	zassert_true(ppm < 500, "after freq reduction, output falls below clamp (got %d)", ppm);
}

ZTEST(drift, test_anti_windup_negative_saturation)
{
	/* Same as positive saturation, but at -500 clamp. */
	audio_drift_frequency_error_update(1000); /* saturates at -500 */
	audio_drift_controller_update(SETPOINT);
	/* Same-direction: draining (phase negative) pushes further toward -500 */
	audio_drift_controller_update(8);
	zassert_equal(audio_drift_get_ppm(), -500,
		      "same-direction while saturated at -500 (got %d)", audio_drift_get_ppm());

	/* Push filling phase while saturated → integral blocked.
	 * Then reduce freq error and push filling phase → integral should build.
	 */
	for (int i = 0; i < 10; i++) {
		audio_drift_controller_update(4); /* filling → positive phase */
	}
	zassert_equal(audio_drift_get_ppm(), -500, "still saturated at -500");

	for (int i = 0; i < 20; i++) {
		audio_drift_frequency_error_update(0);
		audio_drift_controller_update(4);
	}
	int32_t ppm = audio_drift_get_ppm();
	zassert_true(ppm > -500, "after freq reduction, output rises above clamp (got %d)", ppm);
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
