/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_drift.h"

#include <zephyr/sys/util.h>

/* Gains — conservative defaults, tunable via Kconfig later */
#define KP_F 0.5f
#define KI_F 0.1f
#define KP_P 0.3f
#define KI_P 0.05f

#define PHASE_SCALE    50 /* ppm per block of phase error */
#define PHASE_SETPOINT 6  /* target slab free count (pre-fill depth) */

#define MEAS_PERIOD_US 100000U
#define MAX_WINDOW_US  (3U * MEAS_PERIOD_US)

/* Clamps */
#define INTEGRAL_CLAMP 1000 /* ±1000 ppm each integrator */
#define OUTPUT_CLAMP   500  /* ±500 ppm total output */

enum drift_state {
	DRIFT_INIT,
	DRIFT_ACTIVE,
};

struct drift_controller {
	/* Frequency term (computed per 100 ms window) */
	uint32_t meas_start_us;
	int32_t freq_err_ppm;

	/* Phase term (computed per SDU) */
	int32_t phase_err_ppm;

	/* PI integrators */
	float freq_integral;
	float phase_integral;

	/* Output */
	int32_t output_ppm;

	/* State */
	enum drift_state state;
};

static struct drift_controller ctrl;

int32_t audio_drift_controller_update(uint32_t sdu_ref_us, int slab_free_count)
{
	if (sdu_ref_us == 0) {
		return ctrl.output_ppm;
	}

	/* First valid timestamp — enter ACTIVE, set baseline */
	if (ctrl.state == DRIFT_INIT) {
		ctrl.meas_start_us = sdu_ref_us;
		ctrl.state = DRIFT_ACTIVE;
		return 0;
	}

	/* --- Phase term (every SDU) --- */
	int phase_err = slab_free_count - PHASE_SETPOINT;

	ctrl.phase_err_ppm = phase_err * PHASE_SCALE;

	ctrl.phase_integral += KI_P * ctrl.phase_err_ppm;
	ctrl.phase_integral =
		CLAMP(ctrl.phase_integral, (float)-INTEGRAL_CLAMP, (float)INTEGRAL_CLAMP);

	float phase_output = KP_P * ctrl.phase_err_ppm + ctrl.phase_integral;

	/* --- Frequency term (every 100 ms window) --- */
	uint32_t elapsed = sdu_ref_us - ctrl.meas_start_us;
	float freq_output = 0.0f;

	if (elapsed >= MEAS_PERIOD_US && elapsed <= MAX_WINDOW_US) {
		/* err_us > 0 → local clock slow (elapsed > nominal)
		 * err_us < 0 → local clock fast
		 * freq_err_ppm = err_us * 10  (1 µs = 10 ppm in a 100 ms window)
		 */
		int32_t err_us = (int32_t)(elapsed - MEAS_PERIOD_US);

		ctrl.freq_err_ppm = err_us * 10;

		ctrl.freq_integral += KI_F * ctrl.freq_err_ppm;
		ctrl.freq_integral =
			CLAMP(ctrl.freq_integral, (float)-INTEGRAL_CLAMP, (float)INTEGRAL_CLAMP);

		freq_output = KP_F * ctrl.freq_err_ppm + ctrl.freq_integral;

		ctrl.meas_start_us = sdu_ref_us;
	} else if (elapsed > MAX_WINDOW_US) {
		/* Large gap — reset window */
		ctrl.freq_err_ppm = 0;
		ctrl.meas_start_us = sdu_ref_us;
	}

	/* --- Combined output --- */
	int32_t total = (int32_t)(freq_output + phase_output);

	ctrl.output_ppm = CLAMP(total, -OUTPUT_CLAMP, OUTPUT_CLAMP);

	return ctrl.output_ppm;
}

void audio_drift_reset(void)
{
	ctrl.state = DRIFT_INIT;
	ctrl.meas_start_us = 0;
	ctrl.freq_err_ppm = 0;
	ctrl.phase_err_ppm = 0;
	ctrl.freq_integral = 0.0f;
	ctrl.phase_integral = 0.0f;
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
