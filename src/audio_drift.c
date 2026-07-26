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
 *   High slab_free (queue draining, many free slots)
 *     → negative correction (slow consumption down).
 *   Low slab_free (queue filling, few free slots)
 *     → positive correction (speed consumption up).
 *   PI gains in milli-units (1/1000) for integer-only calculation.
 *
 * Combined output:
 *   output = filtered_frequency_correction + phase_PI
 *   Clamped to CONFIG_AUDIO_DRIFT_OUTPUT_CLAMP.
 *
 * Anti-windup (directional):
 *   When the output is at a saturation rail, the phase integrator is
 *   blocked only if the increment would push farther into saturation
 *   (same-direction).  Opposite-direction increments are always allowed,
 *   so the integrator can unwind toward range even if a single step does
 *   not immediately exit the clamp.  Phase integrator is separately
 *   clamped to CONFIG_AUDIO_DRIFT_PHASE_INTEGRAL_CLAMP.
 *
 * Thread safety:
 *   audio_drift_frequency_error_update() runs from system workqueue,
 *   audio_drift_controller_update() runs from Bluetooth/audio path,
 *   and audio_drift_reset() can be called from the disconnect path.
 *   A k_spinlock serialises all public API calls.  Every function is
 *   bounded and nonblocking.
 */

#include "audio_drift.h"

#include <zephyr/autoconf.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

/* ── Tuning constants ───────────────────────────────────────────── */

#define PHASE_SETPOINT 6  /* target slab free count (pre-fill depth) */
#define PHASE_SCALE    50 /* ppm per block of phase error            */

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
static struct k_spinlock ctrl_lock;

/* Clamp values — resolved at compile time from Kconfig */
#define OUTPUT_CLAMP         CONFIG_AUDIO_DRIFT_OUTPUT_CLAMP
#define PHASE_INTEGRAL_CLAMP CONFIG_AUDIO_DRIFT_PHASE_INTEGRAL_CLAMP

/* ── Public API ──────────────────────────────────────────────────── */

void audio_drift_frequency_error_update(int32_t local_clock_error_ppm)
{
	k_spinlock_key_t key = k_spin_lock(&ctrl_lock);

	/* First measurement: initialize filter to measured value
	 * to jump-start tracking (no ramp from zero).
	 */
	if (!ctrl.freq_once_received) {
		ctrl.freq_filtered = local_clock_error_ppm;
		ctrl.freq_once_received = true;
		k_spin_unlock(&ctrl_lock, key);
		return;
	}

	/* EMA: filtered = filtered + (measured - filtered) / 8.
	 * Integer arithmetic with power-of-two shift — avoids
	 * overflow for moderate ppm values.
	 */
	ctrl.freq_filtered += (local_clock_error_ppm - ctrl.freq_filtered) >> FREQ_FILTER_SHIFT;

	k_spin_unlock(&ctrl_lock, key);
}

int32_t audio_drift_controller_update(int slab_free_count)
{
	k_spinlock_key_t key = k_spin_lock(&ctrl_lock);

	int32_t freq_correction = 0;
	int32_t phase_output = 0;

	/* ── State transition: first call → ACTIVE, output zero ─── */
	if (ctrl.state == DRIFT_INIT) {
		ctrl.state = DRIFT_ACTIVE;
		ctrl.output_ppm = 0;
		k_spin_unlock(&ctrl_lock, key);
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
	 *   High slab_free (queue draining, many free) → negative err → slow down
	 *   Low slab_free (queue filling, few free)   → positive err → speed up
	 */
	int phase_err = PHASE_SETPOINT - slab_free_count;

	/* Scale to ppm: phase_err * PHASE_SCALE */
	int32_t phase_err_ppm = (int32_t)phase_err * PHASE_SCALE;

	/* Proportional term in ppm */
	int32_t phase_pp = (int32_t)(((int64_t)phase_err_ppm * KP_MILLI) / 1000);

	/* Integral accumulation (ppm per block) */
	int32_t phase_inc = (int32_t)(((int64_t)phase_err_ppm * KI_MILLI) / 1000);

	/*
	 * Directional anti-windup:
	 *   - At positive clamp (output_ppm >= +OUTPUT_CLAMP):
	 *       block only if phase_inc > 0 (pushes farther positive).
	 *       Allow negative phase_inc to unwind.
	 *   - At negative clamp (output_ppm <= -OUTPUT_CLAMP):
	 *       block only if phase_inc < 0 (pushes farther negative).
	 *       Allow positive phase_inc to unwind.
	 *
	 * Use the previous block's output for the saturation test
	 * (not a tentative of this block) so the decision is stable
	 * and the integrator can unwind even when feedforward alone
	 * keeps output at the rail.
	 */
	bool block_integral = false;

	if (ctrl.output_ppm >= OUTPUT_CLAMP && phase_inc > 0) {
		block_integral = true;
	} else if (ctrl.output_ppm <= -OUTPUT_CLAMP && phase_inc < 0) {
		block_integral = true;
	}

	if (!block_integral) {
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

	k_spin_unlock(&ctrl_lock, key);
	return ctrl.output_ppm;
}

void audio_drift_reset(void)
{
	k_spinlock_key_t key = k_spin_lock(&ctrl_lock);

	ctrl.state = DRIFT_INIT;
	ctrl.freq_filtered = 0;
	ctrl.freq_once_received = false;
	ctrl.phase_integral = 0;
	ctrl.output_ppm = 0;

	k_spin_unlock(&ctrl_lock, key);
}

int32_t audio_drift_get_ppm(void)
{
	k_spinlock_key_t key = k_spin_lock(&ctrl_lock);
	int32_t ppm = ctrl.output_ppm;

	k_spin_unlock(&ctrl_lock, key);
	return ppm;
}

const char *audio_drift_state_str(void)
{
	k_spinlock_key_t key = k_spin_lock(&ctrl_lock);
	const char *str;

	switch (ctrl.state) {
	case DRIFT_INIT:
		str = "INIT";
		break;
	case DRIFT_ACTIVE:
		str = "ACTIVE";
		break;
	default:
		str = "?";
		break;
	}

	k_spin_unlock(&ctrl_lock, key);
	return str;
}
