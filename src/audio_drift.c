/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 4b.2: PCLK feedforward + buffer-phase PI controller.
 *
 * Two explicit inputs:
 *   1. audio_drift_frequency_error_update(local_clock_error_ppm)
 *      — measured PCLK-vs-GRTC ppm from platform timing (nRF54L15).
 *        Zero on nRF5340 (no hardware PCLK measurement).
 *   2. audio_drift_controller_update(slab_free_count)
 *      — called once per rendered stereo block; reads slab free
 *        count, runs phase PI, and returns combined ppm.
 *
 * Frequency:
 *   Measured local PCLK error is filtered through an exponential
 *   moving average (N=8, shift-by-3) to reject one-second jitter while
 *   tracking fixed offset.  Output correction = -filtered_freq_error
 *   (local fast → negative correction → insert → slow down effective
 *   consumption).
 *
 * Phase:
 *   Phase error = PHASE_SETPOINT - slab_free_count.
 *   Low slab_free (draining) → positive correction (speed up).
 *   High slab_free (filling)  → negative correction (slow down).
 *   PI gains in milli-units (1/1000) for integer-only calculation.
 *
 * Combined output:
 *   output = filtered_frequency_correction + phase_PI
 *   Clamped to CONFIG_AUDIO_DRIFT_OUTPUT_CLAMP.
 *
 * Anti-windup:
 *   When the combined output is saturated, the phase integrator is
 *   not advanced further in the saturated direction.
 *   Phase integrator is separately clamped to
 *   CONFIG_AUDIO_DRIFT_PHASE_INTEGRAL_CLAMP.
 */

#include "audio_drift.h"

#include <zephyr/autoconf.h>
#include <zephyr/sys/util.h>

/* ── Tuning constants ───────────────────────────────────────────── */

#define PHASE_SETPOINT 6  /* target slab free count (pre-fill depth) */
#define PHASE_SCALE    50 /* ppm per block of phase error */

/* Phase PI gains in milli-units (1/1000).
 * KP_MILLI = 300  → 0.300 (30 % proportional)
 * KI_MILLI =  50  → 0.050 (5 % integral per block)
 */
#define KP_MILLI 300
#define KI_MILLI 50

/*
 * Frequency Error Filter:
 * Exponential moving average with N=8.  With 1 Hz measurement cadence,
 * the 3 dB point is approximately 0.35 Hz — enough to reject one-second
 * jitter without hiding a fixed PCLK offset.
 */
#define FREQ_FILTER_SHIFT 3 /* divide by 8 */

/* ── State ──────────────────────────────────────────────────────── */

enum drift_state {
	DRIFT_INIT,   /* no SDU / audio block processed yet */
	DRIFT_ACTIVE, /* phase PI running */
};

struct drift_controller {
	/* Frequency feedforward */
	int32_t freq_filtered;   /* filtered local clock error (ppm) */
	bool freq_once_received; /* true after first measurement */

	/* Phase PI */
	int32_t phase_integral; /* in ppm units */

	/* Output */
	int32_t output_ppm;

	/* State */
	enum drift_state state;
};

static struct drift_controller ctrl;

/* Clamp values — resolved at compile time from Kconfig */
#define OUTPUT_CLAMP         CONFIG_AUDIO_DRIFT_OUTPUT_CLAMP
#define PHASE_INTEGRAL_CLAMP CONFIG_AUDIO_DRIFT_PHASE_INTEGRAL_CLAMP

/* ── Public API ──────────────────────────────────────────────────── */

void audio_drift_frequency_error_update(int32_t local_clock_error_ppm)
{
	/* First measurement: initialize filter to measured value
	 * to jump-start tracking (no ramp from zero).
	 */
	if (!ctrl.freq_once_received) {
		ctrl.freq_filtered = local_clock_error_ppm;
		ctrl.freq_once_received = true;
		return;
	}

	/* EMA: filtered = filtered + (measured - filtered) / 8.
	 * Integer arithmetic with power-of-two shift — avoids
	 * overflow for moderate ppm values.
	 */
	ctrl.freq_filtered += (local_clock_error_ppm - ctrl.freq_filtered) >> FREQ_FILTER_SHIFT;
}

int32_t audio_drift_controller_update(int slab_free_count)
{
	int32_t freq_correction = 0;
	int32_t phase_output = 0;

	/* ── State transition: first call → ACTIVE, output zero ─── */
	if (ctrl.state == DRIFT_INIT) {
		ctrl.state = DRIFT_ACTIVE;
		ctrl.output_ppm = 0;
		return 0;
	}

	/* ── Frequency feedforward ──────────────────────────────────
	 * local fast (positive) → negative correction (slow-down / insert).
	 */
	if (ctrl.freq_once_received) {
		freq_correction = -ctrl.freq_filtered;
	}

	/* ── Phase PI ───────────────────────────────────────────────
	 * phase_err = PHASE_SETPOINT - slab_free_count
	 *   low slab_free (draining) → positive phase_err → speed up
	 *   high slab_free (filling) → negative phase_err → slow down
	 */
	int phase_err = PHASE_SETPOINT - slab_free_count;

	/* Scale to ppm: phase_err * PHASE_SCALE */
	int32_t phase_err_ppm = (int32_t)phase_err * PHASE_SCALE;

	/* Proportional term in ppm */
	int32_t phase_pp = (int32_t)(((int64_t)phase_err_ppm * KP_MILLI) / 1000);

	/* Integral accumulation (ppm per block) */
	int32_t phase_inc = (int32_t)(((int64_t)phase_err_ppm * KI_MILLI) / 1000);

	/* Combined (tentative) output for anti-windup check */
	int32_t tentative = freq_correction + phase_pp + ctrl.phase_integral + phase_inc;
	int32_t clamped_tentative = CLAMP(tentative, -OUTPUT_CLAMP, OUTPUT_CLAMP);

	/* Anti-windup:
	 * If the combined output is already at clamp AND the integral
	 * increment would push it farther past the clamp, don't
	 * accumulate the integral.  Otherwise, add the increment.
	 */
	bool at_pos_clamp = (clamped_tentative == OUTPUT_CLAMP && tentative >= OUTPUT_CLAMP);
	bool at_neg_clamp = (clamped_tentative == -OUTPUT_CLAMP && tentative <= -OUTPUT_CLAMP);

	if (!at_pos_clamp && !at_neg_clamp) {
		ctrl.phase_integral += phase_inc;
		/* Clamp phase integral authority separately */
		ctrl.phase_integral =
			CLAMP(ctrl.phase_integral, -PHASE_INTEGRAL_CLAMP, PHASE_INTEGRAL_CLAMP);
	}

	/* Compute final phase contribution */
	phase_output = phase_pp + ctrl.phase_integral;

	/* ── Combined output ─────────────────────────────────────── */
	int32_t total = freq_correction + phase_output;

	ctrl.output_ppm = CLAMP(total, -OUTPUT_CLAMP, OUTPUT_CLAMP);

	return ctrl.output_ppm;
}

void audio_drift_reset(void)
{
	ctrl.state = DRIFT_INIT;
	ctrl.freq_filtered = 0;
	ctrl.freq_once_received = false;
	ctrl.phase_integral = 0;
	ctrl.output_ppm = 0;
}

int32_t audio_drift_get_ppm(void)
{
	return ctrl.output_ppm;
}

const char *audio_drift_state_str(void)
{
	switch (ctrl.state) {
	case DRIFT_INIT:
		return "INIT";
	case DRIFT_ACTIVE:
		return "ACTIVE";
	default:
		return "?";
	}
}
