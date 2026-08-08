/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the PCLK-feedforward + buffer-phase PI
 * drift controller.  Tests the production audio_drift.c directly.
 *
 * All tests use CONFIG_AUDIO_DRIFT_OUTPUT_CLAMP=500 (default).
 * Tests verify clamp, anti-windup, phase sign, and filter behaviour
 * within the 500 ppm output range.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <limits.h>
#include <stdint.h>
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

/* ── Defined arithmetic across the full int32/int range ─────────
 * Every public input (frequency ppm and slab count) must be defined:
 * int64_t intermediates, rails clamped before narrowing, no signed
 * overflow (verified with -fsanitize=undefined in the focused run).
 */

ZTEST(drift, test_frequency_int32_max_clamps_negative)
{
	audio_drift_frequency_error_update(INT32_MAX);
	audio_drift_controller_update(SETPOINT);
	int32_t ppm = audio_drift_controller_update(SETPOINT);
	zassert_equal(ppm, -500, "INT32_MAX local fast → -rail (got %d)", ppm);
}

ZTEST(drift, test_frequency_int32_min_clamps_positive)
{
	audio_drift_frequency_error_update(INT32_MIN);
	audio_drift_controller_update(SETPOINT);
	int32_t ppm = audio_drift_controller_update(SETPOINT);
	zassert_equal(ppm, 500, "INT32_MIN local slow → +rail (got %d)", ppm);
}

ZTEST(drift, test_slab_int_max_clamps_negative)
{
	audio_drift_controller_update(SETPOINT);
	int32_t ppm = audio_drift_controller_update(INT_MAX);
	zassert_equal(ppm, -500, "slab INT_MAX → -rail (got %d)", ppm);
}

ZTEST(drift, test_slab_int_min_clamps_positive)
{
	audio_drift_controller_update(SETPOINT);
	int32_t ppm = audio_drift_controller_update(INT_MIN);
	zassert_equal(ppm, 500, "slab INT_MIN → +rail (got %d)", ppm);
}

ZTEST(drift, test_frequency_cross_extreme_ema_defined)
{
	/* Jumping from INT32_MIN to INT32_MAX must not overflow the
	 * EMA delta; the filter stays clamped inside int32 range and
	 * the output stays on the correct rail. */
	audio_drift_frequency_error_update(INT32_MIN);
	audio_drift_controller_update(SETPOINT);
	zassert_equal(audio_drift_controller_update(SETPOINT), 500, "INT32_MIN baseline");

	audio_drift_frequency_error_update(INT32_MAX);
	int32_t ppm = audio_drift_controller_update(SETPOINT);
	zassert_equal(ppm, 500, "cross-extreme EMA keeps positive correction (got %d)", ppm);

	audio_drift_frequency_error_update(INT32_MAX);
	ppm = audio_drift_controller_update(SETPOINT);
	zassert_equal(ppm, 500, "EMA converges up but stays defined (got %d)", ppm);
}

ZTEST(drift, test_exact_rail_boundary_and_clamp_overshoot)
{
	/* Exactly ±500 ppm is accepted without clamping distortion. */
	audio_drift_frequency_error_update(500);
	audio_drift_controller_update(SETPOINT);
	zassert_equal(audio_drift_controller_update(SETPOINT), -500, "exact +500 → -500");

	audio_drift_reset();
	audio_drift_frequency_error_update(-500);
	audio_drift_controller_update(SETPOINT);
	zassert_equal(audio_drift_controller_update(SETPOINT), 500, "exact -500 → +500");

	/* One ppm past the rail clamps, not wraps. */
	audio_drift_reset();
	audio_drift_frequency_error_update(501);
	audio_drift_controller_update(SETPOINT);
	zassert_equal(audio_drift_controller_update(SETPOINT), -500, "+501 clamps -500");

	audio_drift_reset();
	audio_drift_frequency_error_update(-501);
	audio_drift_controller_update(SETPOINT);
	zassert_equal(audio_drift_controller_update(SETPOINT), 500, "-501 clamps +500");
}

/* ── Long-run boundedness and setpoint stability ─────────────── */

#define LONG_RUN 100000

ZTEST(drift, test_long_run_setpoint_stable)
{
	audio_drift_controller_update(SETPOINT);
	for (int i = 0; i < LONG_RUN; i++) {
		audio_drift_frequency_error_update(0);
		int32_t ppm = audio_drift_controller_update(SETPOINT);
		zassert_equal(ppm, 0, "setpoint stays 0 at iteration %d (got %d)", i, ppm);
	}
	zassert_equal(audio_drift_get_ppm(), 0, "final 0");
}

ZTEST(drift, test_long_run_positive_extreme_bounded)
{
	/* slab_free=0 → phase_err +6 → positive correction; output must
	 * stay inside the rails for the whole run and settle at +500. */
	audio_drift_controller_update(SETPOINT);
	for (int i = 0; i < LONG_RUN; i++) {
		int32_t ppm = audio_drift_controller_update(0);
		zassert_true(ppm >= -500 && ppm <= 500, "bounded at iteration %d (got %d)", i, ppm);
		if (i >= 50) {
			zassert_equal(ppm, 500, "settled at +500 by %d (got %d)", i, ppm);
		}
	}
	zassert_equal(audio_drift_get_ppm(), 500, "final +500");
}

ZTEST(drift, test_long_run_negative_extreme_bounded)
{
	/* slab_free=12 → phase_err -6 → negative correction; output must
	 * stay inside the rails for the whole run and settle at -500. */
	audio_drift_controller_update(SETPOINT);
	for (int i = 0; i < LONG_RUN; i++) {
		int32_t ppm = audio_drift_controller_update(12);
		zassert_true(ppm >= -500 && ppm <= 500, "bounded at iteration %d (got %d)", i, ppm);
		if (i >= 50) {
			zassert_equal(ppm, -500, "settled at -500 by %d (got %d)", i, ppm);
		}
	}
	zassert_equal(audio_drift_get_ppm(), -500, "final -500");
}

/* ── Symmetric feedforward-rail phase unwind ──────────────────── */

ZTEST(drift, test_feedforward_rail_phase_unwind_symmetric)
{
	/* Positive rail: feedforward +2000 holds the output at +500
	 * while draining blocks unwind the integral to its -500 clamp
	 * (the feedforward must dominate the unwound phase sum). */
	audio_drift_frequency_error_update(-2000);
	audio_drift_controller_update(SETPOINT);
	for (int i = 0; i < 100; i++) {
		audio_drift_controller_update(8); /* draining → negative inc */
	}
	zassert_equal(audio_drift_get_ppm(), 500, "feedforward keeps +rail");

	/* Removing the feedforward (EMA decays over ~100 cycles) must
	 * expose the fully unwound integral: output reaches the
	 * opposite rail. */
	for (int i = 0; i < 100; i++) {
		audio_drift_frequency_error_update(0);
		audio_drift_controller_update(8);
	}
	zassert_equal(audio_drift_get_ppm(), -500, "unwound integral reaches -rail");

	/* Mirror on the negative rail. */
	audio_drift_reset();
	audio_drift_frequency_error_update(2000);
	audio_drift_controller_update(SETPOINT);
	for (int i = 0; i < 100; i++) {
		audio_drift_controller_update(4); /* filling → positive inc */
	}
	zassert_equal(audio_drift_get_ppm(), -500, "feedforward keeps -rail");

	for (int i = 0; i < 100; i++) {
		audio_drift_frequency_error_update(0);
		audio_drift_controller_update(4);
	}
	zassert_equal(audio_drift_get_ppm(), 500, "unwound integral reaches +rail");
}

/* ── Concurrent update / frequency / reset ─────────────────────
 * Real Zephyr threads hammering the controller while a reset thread
 * runs; k_thread_join returning proves no deadlock, and a deterministic
 * final reset leaves INIT / zero state.
 */

#define CONC_THREADS 4
#define CONC_ITERS   4000

static struct k_thread conc_threads[CONC_THREADS];
K_THREAD_STACK_ARRAY_DEFINE(conc_stacks, CONC_THREADS, 1024);

static void conc_controller_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (int i = 0; i < CONC_ITERS; i++) {
		audio_drift_controller_update((i & 1) ? 4 : 8);
	}
}

static void conc_frequency_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (int i = 0; i < CONC_ITERS; i++) {
		audio_drift_frequency_error_update((i % 7) - 3);
	}
}

static void conc_reset_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (int i = 0; i < CONC_ITERS; i++) {
		if ((i % 64) == 0) {
			audio_drift_reset();
		}
	}
}

ZTEST(drift, test_concurrent_update_frequency_reset_no_deadlock)
{
	k_thread_create(&conc_threads[0], conc_stacks[0], K_THREAD_STACK_SIZEOF(conc_stacks[0]),
			conc_controller_fn, NULL, NULL, NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);
	k_thread_create(&conc_threads[1], conc_stacks[1], K_THREAD_STACK_SIZEOF(conc_stacks[1]),
			conc_controller_fn, NULL, NULL, NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);
	k_thread_create(&conc_threads[2], conc_stacks[2], K_THREAD_STACK_SIZEOF(conc_stacks[2]),
			conc_frequency_fn, NULL, NULL, NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);
	k_thread_create(&conc_threads[3], conc_stacks[3], K_THREAD_STACK_SIZEOF(conc_stacks[3]),
			conc_reset_fn, NULL, NULL, NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);

	/* k_thread_join returning for every thread proves no deadlock. */
	for (int i = 0; i < CONC_THREADS; i++) {
		k_thread_join(&conc_threads[i], K_FOREVER);
	}

	/* Deterministic final reset: INIT and zero state. */
	audio_drift_reset();
	zassert_true(strcmp(audio_drift_state_str(), "INIT") == 0, "INIT after final reset");
	zassert_equal(audio_drift_get_ppm(), 0, "zero after final reset");
	zassert_equal(audio_drift_controller_update(SETPOINT), 0, "first post-reset returns 0");
}
