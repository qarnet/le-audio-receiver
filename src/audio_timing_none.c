/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * No-op audio timing for the nRF54L15BSim receiver.
 *
 * The simulator has no hardware PCLK measurement; buffer-phase PI
 * still runs via audio_drift_controller_update() in audio_sink_push().
 * Physical nRF54L15 uses GRTC+TIMER20+GPPI for PCLK measurement.
 * No timing sample is submitted by this backend.
 */

#include "audio_timing.h"

int audio_timing_init(void)
{
	return 0;
}

void audio_timing_sdu_ref_update(uint32_t ts_us, uint32_t presentation_delay_us)
{
	(void)ts_us;
	(void)presentation_delay_us;
}

void audio_timing_reset(void)
{
}
