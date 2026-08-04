/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_clock_actuator.h"
#include "audio_drift.h"

#include <zephyr/sys/util.h>
#include <hal/nrf_clock.h>

#if NRF_CLOCK_HAS_HFCLKAUDIO
#include <nrfx_clock_hfclkaudio.h>
#endif

/*
 * APLL register constants for nRF5340 HFCLKAUDIO (12.288 MHz band).
 * One step ≈ 3.3 ppm. Range spans ≈ ±600 ppm around center.
 * These are the same constants as in audio_drift.h — kept private here.
 */
#define APLL_CENTER AUDIO_DRIFT_APLL_CENTER
#define APLL_MIN    AUDIO_DRIFT_APLL_MIN
#define APLL_MAX    AUDIO_DRIFT_APLL_MAX

int audio_clock_actuator_init(void)
{
#if NRF_CLOCK_HAS_HFCLKAUDIO
	nrfx_clock_hfclkaudio_config_set(APLL_CENTER);
	return 0;
#else
	return 0;
#endif
}

int audio_clock_actuator_apply_ppm(int32_t ppm)
{
#if NRF_CLOCK_HAS_HFCLKAUDIO
	/* 1 APLL step ≈ 3.3 ppm.  Integer: offset = (ppm * 10) / 33,
	 * preserving C truncation toward zero.  The conversion, the
	 * center addition, and the rail clamp all happen in int64_t so
	 * the full int32_t input range (INT32_MIN..INT32_MAX) is
	 * defined before narrowing to the register value.
	 */
	int64_t offset64 = ((int64_t)ppm * 10) / 33;
	int64_t reg64 = (int64_t)APLL_CENTER + offset64;
	int32_t reg = (int32_t)CLAMP(reg64, (int64_t)APLL_MIN, (int64_t)APLL_MAX);

	nrfx_clock_hfclkaudio_config_set((uint16_t)reg);
	return 0;
#else
	(void)ppm;
	return 0;
#endif
}

int audio_clock_actuator_reset(void)
{
#if NRF_CLOCK_HAS_HFCLKAUDIO
	nrfx_clock_hfclkaudio_config_set(APLL_CENTER);
	return 0;
#else
	return 0;
#endif
}
