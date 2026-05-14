/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_drift.h"

#include <stdlib.h>
#include <zephyr/sys/util.h>

#define DRIFT_MEAS_PERIOD_US    100000U
#define DRIFT_ERR_THRESH_LOCK   16
#define DRIFT_ERR_THRESH_UNLOCK 32
#define APLL_FREQ_ADJ(err_us)   (-((int32_t)(err_us) * 1000) / 331)

enum drift_state {
	DRIFT_INIT,
	DRIFT_CALIB,
	DRIFT_LOCKED,
};

static struct {
	enum drift_state state;
	uint32_t meas_start_us;
	uint16_t center_freq;
} drift = {
	.state = DRIFT_INIT,
	.center_freq = AUDIO_DRIFT_APLL_CENTER,
};

uint16_t audio_drift_update(uint32_t sdu_ref_us)
{
	if (sdu_ref_us == 0) {
		return 0;
	}

	switch (drift.state) {
	case DRIFT_INIT:
		drift.meas_start_us = sdu_ref_us;
		drift.state = DRIFT_CALIB;
		return 0;

	case DRIFT_CALIB: {
		uint32_t elapsed = sdu_ref_us - drift.meas_start_us;

		if (elapsed < DRIFT_MEAS_PERIOD_US) {
			return 0;
		}
		if (elapsed > 3 * DRIFT_MEAS_PERIOD_US) {
			drift.meas_start_us = sdu_ref_us;
			return 0;
		}

		int32_t err_us = (int32_t)DRIFT_MEAS_PERIOD_US - (int32_t)elapsed;
		int32_t adj = APLL_FREQ_ADJ(err_us);

		drift.center_freq = (uint16_t)CLAMP((int32_t)AUDIO_DRIFT_APLL_CENTER + adj,
						    (int32_t)AUDIO_DRIFT_APLL_MIN,
						    (int32_t)AUDIO_DRIFT_APLL_MAX);
		drift.meas_start_us = sdu_ref_us;

		if (abs(err_us) <= DRIFT_ERR_THRESH_LOCK) {
			drift.state = DRIFT_LOCKED;
		}

		return drift.center_freq;
	}

	case DRIFT_LOCKED: {
		uint32_t elapsed = sdu_ref_us - drift.meas_start_us;

		if (elapsed < DRIFT_MEAS_PERIOD_US) {
			return 0;
		}
		if (elapsed > 3 * DRIFT_MEAS_PERIOD_US) {
			drift.state = DRIFT_CALIB;
			drift.meas_start_us = sdu_ref_us;
			return 0;
		}

		int32_t err_us = (int32_t)DRIFT_MEAS_PERIOD_US - (int32_t)elapsed;
		int32_t adj = APLL_FREQ_ADJ(err_us / 2);

		uint16_t new_freq = (uint16_t)CLAMP((int32_t)drift.center_freq + adj,
						    (int32_t)AUDIO_DRIFT_APLL_MIN,
						    (int32_t)AUDIO_DRIFT_APLL_MAX);
		drift.meas_start_us = sdu_ref_us;

		if (abs(err_us) > DRIFT_ERR_THRESH_UNLOCK) {
			drift.state = DRIFT_CALIB;
			drift.center_freq = AUDIO_DRIFT_APLL_CENTER;
		}

		return new_freq;
	}
	}

	return 0;
}

void audio_drift_reset(void)
{
	drift.state = DRIFT_INIT;
	drift.center_freq = AUDIO_DRIFT_APLL_CENTER;
	drift.meas_start_us = 0;
}

const char *audio_drift_state_str(void)
{
	switch (drift.state) {
	case DRIFT_INIT:   return "INIT";
	case DRIFT_CALIB:  return "CALIB";
	case DRIFT_LOCKED: return "LOCKED";
	default:           return "?";
	}
}
