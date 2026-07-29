/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUDIO_STATS_H
#define AUDIO_STATS_H

#include <stdint.h>

struct audio_stats {
	uint32_t total_frames;  /* good + PLC frames decoded */
	uint32_t plc_frames;    /* LC3 PLC concealment events (err==1) */
	uint32_t decode_errors; /* hard LC3 decode failures (err<0) */
	uint32_t i2s_underruns; /* I2S slab-full drops */
	uint32_t stream_resets; /* I2S DMA restarts due to underrun */
	uint32_t startup_zero;  /* zero-energy pushes before first nonzero PCM */
	uint32_t startup_plc;   /* PLC snapshot at end of startup phase */
};

void audio_stats_frame_decoded(void);
void audio_stats_frame_plc(void);
void audio_stats_decode_error(void);
void audio_stats_i2s_underrun(void);
void audio_stats_stream_reset(void);
void audio_stats_startup_zero(void);         /* increment startup_zero */
void audio_stats_startup_plc_snapshot(void); /* snapshot plc_frames -> startup_plc */
void audio_stats_reset(void);
struct audio_stats audio_stats_get(void);

#endif /* AUDIO_STATS_H */
