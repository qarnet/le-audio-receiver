/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_stats.h"

#include <zephyr/sys/atomic.h>

static atomic_t total_frames;
static atomic_t plc_frames;
static atomic_t decode_errors;
static atomic_t i2s_underruns;
static atomic_t stream_resets;

void audio_stats_frame_decoded(void)  { atomic_inc(&total_frames); }
void audio_stats_frame_plc(void)      { atomic_inc(&plc_frames); atomic_inc(&total_frames); }
void audio_stats_decode_error(void)   { atomic_inc(&decode_errors); }
void audio_stats_i2s_underrun(void)   { atomic_inc(&i2s_underruns); }
void audio_stats_stream_reset(void)   { atomic_inc(&stream_resets); }

void audio_stats_reset(void)
{
	atomic_set(&total_frames, 0);
	atomic_set(&plc_frames, 0);
	atomic_set(&decode_errors, 0);
	atomic_set(&i2s_underruns, 0);
	atomic_set(&stream_resets, 0);
}

struct audio_stats audio_stats_get(void)
{
	return (struct audio_stats){
		.total_frames  = (uint32_t)atomic_get(&total_frames),
		.plc_frames    = (uint32_t)atomic_get(&plc_frames),
		.decode_errors = (uint32_t)atomic_get(&decode_errors),
		.i2s_underruns = (uint32_t)atomic_get(&i2s_underruns),
		.stream_resets = (uint32_t)atomic_get(&stream_resets),
	};
}
