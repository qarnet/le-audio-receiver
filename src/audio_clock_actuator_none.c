/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * No-op clock actuator. Controller runs but ppm output is discarded.
 * For platforms without a clock steering mechanism (e.g. nRF54L15
 * until Phase 4 sample_adjust lands).
 */

#include "audio_clock_actuator.h"

int audio_clock_actuator_init(void)
{
	return 0;
}

int audio_clock_actuator_apply_ppm(int32_t ppm)
{
	(void)ppm;
	return 0;
}

int audio_clock_actuator_reset(void)
{
	return 0;
}

int audio_clock_actuator_consume_sample_adjustment(void)
{
	return 0;
}
