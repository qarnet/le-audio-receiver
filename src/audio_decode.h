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
 *
 * The receiver supports exactly: 48 kHz sampling, 7500 or 10000 us frame
 * duration, 1 or 2 sink channels (mono or Mode B), and exactly one frame
 * block per SDU.  Anything else returns -EINVAL without calling liblc3
 * and leaves the context fully reset (no usable decoder state).
 *
 * On success: mono creates only the left decoder; Mode B creates two
 * independent decoders; samples_per_ch is exactly 360 (7.5 ms) or 480
 * (10 ms).
 *
 * @return 0 on success, -EINVAL on unsupported parameters.
 */
int audio_decode_config(struct audio_decode_ctx *ctx, int chan_count, int freq_hz, int frame_us,
			int frames_per_sdu);

/**
 * Decode one SDU into interleaved stereo PCM.
 *
 * @param ctx        Decoder context (configured).
 * @param frame_data Raw SDU payload (LC3 frames).  Must be non-NULL when
 *                   @p valid is true; ignored (never dereferenced) when
 *                   @p valid is false.
 * @param frame_len  Length of frame_data in bytes.
 * @param valid      True if packet is valid (not lost/corrupt).
 * @param stereo_out Output buffer, interleaved int16_t [L,R,L,R,...].
 *                   Must hold samples_per_ch * 2 int16_t values.
 *
 * Length semantics: all length arithmetic happens in size_t and every
 * invalid shape is rejected with -EINVAL before output or decoder state
 * is touched.  Valid frames require a per-channel length in the liblc3
 * basic 20..400 byte range (Mode B lengths must split exactly per
 * channel).  PLC (valid=false) accepts a zero length (startup frames)
 * and any nonzero length that forms a valid divisible per-channel
 * 20..400 shape; frame data is never dereferenced on the PLC path.
 * Lengths above INT_MAX are always rejected.
 *
 * @return 0 on success, -EINVAL on invalid arguments/shape (output and
 *         decoder state untouched), -EBADMSG after a hard liblc3 decode
 *         failure (statistics count the failure; the second Mode B
 *         decoder is still invoked so independent decoder state stays
 *         aligned).
 */
int audio_decode_sdu(struct audio_decode_ctx *ctx, const uint8_t *frame_data, size_t frame_len,
		     bool valid, int16_t *stereo_out);

/**
 * Reset decoder state (call on stream start).  NULL is harmless; a real
 * context is returned to a fully unconfigured state (scalar shape fields
 * and both decoder pointers cleared).
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
