/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * No-op audio timing for nRF5340.
 *
 * The nRF5340 uses buffer-phase PI drift compensation via
 * audio_drift_controller_update() in audio_i2s.c / audio_sink_push().
 * The GRTC+TIMER20+LRCK measurement path is specific to nRF54L15.
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
