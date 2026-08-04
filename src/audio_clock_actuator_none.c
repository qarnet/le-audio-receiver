/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * No-op clock actuator. The controller runs and its ppm output is
 * consumed directly by the ASRC.  NONE is the nRF54L15 production
 * actuator: the ASRC consumes the ppm, so the actuator itself performs
 * no clock steering (there is no steerable audio clock on nRF54L15).
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
