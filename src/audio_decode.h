/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUDIO_DECODE_H
#define AUDIO_DECODE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(CONFIG_LIBLC3)
#include "lc3.h"
#endif

#define AUDIO_DECODE_MAX_CHANNELS 2

struct audio_decode_ctx {
	int chan_count;
	int samples_per_ch;
	int frames_per_sdu;
#if defined(CONFIG_LIBLC3)
	lc3_decoder_t decoder;
	lc3_decoder_mem_48k_t dec_mem;
	lc3_decoder_t decoder_r;
	lc3_decoder_mem_48k_t dec_mem_r;
#endif
};

/**
 * Configure decoders from codec parameters.
 * Returns 0 on success, negative errno on failure.
 */
int audio_decode_config(struct audio_decode_ctx *ctx, int chan_count, int freq_hz, int frame_us,
			int frames_per_sdu);

/**
 * Decode one SDU into interleaved stereo PCM.
 *
 * @param ctx        Decoder context (configured).
 * @param frame_data Raw SDU payload (LC3 frames). NULL for PLC (packet loss).
 * @param frame_len  Length of frame_data in bytes.
 * @param valid       True if packet is valid (not lost/corrupt).
 * @param stereo_out  Output buffer, interleaved int16_t [L,R,L,R,...].
 *                   Must hold samples_per_ch * 2 int16_t values.
 * @return 0 on success, negative on error.
 */
int audio_decode_sdu(struct audio_decode_ctx *ctx, const uint8_t *frame_data, size_t frame_len,
		     bool valid, int16_t *stereo_out);

/**
 * Reset decoder state (call on stream start).
 */
void audio_decode_reset(struct audio_decode_ctx *ctx);

/**
 * Mono routing: duplicate a mono buffer into both stereo channels.
 * Called when num_sink_ase == 1 and chan_count == 1.
 */
void audio_decode_mono_to_stereo(const int16_t *mono, int16_t *stereo_out, int samples);

/**
 * Stereo interleave: combine separate L and R buffers into interleaved stereo.
 * Called in Mode A (2 mono ASEs) when both L and R have arrived.
 */
void audio_decode_interleave(const int16_t *l, const int16_t *r, int16_t *stereo_out, int samples);

#endif /* AUDIO_DECODE_H */
