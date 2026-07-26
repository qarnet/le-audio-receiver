/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_decode.h"
#include "audio_stats.h"
#include "audio_perf.h"

#include <string.h>
#include <errno.h>

#include <zephyr/sys/clock.h>

#if defined(CONFIG_LIBLC3)
#include "lc3.h"
#endif

#if defined(CONFIG_LIBLC3)

int audio_decode_config(struct audio_decode_ctx *ctx, int chan_count, int freq_hz, int frame_us,
			int frames_per_sdu)
{
	ctx->chan_count = chan_count;
	ctx->samples_per_ch = (frame_us * freq_hz) / USEC_PER_SEC;
	ctx->frames_per_sdu = frames_per_sdu;

	ctx->decoder = lc3_setup_decoder(frame_us, freq_hz, 0, &ctx->dec_mem);
	if (!ctx->decoder) {
		return -1;
	}

	if (chan_count >= 2) {
		ctx->decoder_r = lc3_setup_decoder(frame_us, freq_hz, 0, &ctx->dec_mem_r);
		if (!ctx->decoder_r) {
			return -1;
		}
	}

	return 0;
}

int audio_decode_sdu(struct audio_decode_ctx *ctx, const uint8_t *frame_data, size_t frame_len,
		     bool valid, int16_t *stereo_out)
{
	if (!ctx->decoder) {
		return -EINVAL;
	}

	const int f_per_sdu = ctx->frames_per_sdu;
	const int spc = ctx->samples_per_ch;
	const int octets_per_frame = f_per_sdu > 0 ? ((int)frame_len / f_per_sdu) : (int)frame_len;

	if (ctx->chan_count >= 2) {
		/* Mode B: stereo single-ASE — split SDU per-channel,
		 * two independent decoders with stride=2.
		 */
		const int octets_per_channel = octets_per_frame / ctx->chan_count;
		const uint8_t *ptr = frame_data;

		for (int i = 0; i < f_per_sdu; i++) {
			const void *l_data = valid ? ptr : NULL;

			if (valid) {
				ptr += octets_per_channel;
			}
			const void *r_data = valid ? ptr : NULL;

			if (valid) {
				ptr += octets_per_channel;
			}

			uint32_t t0 = audio_perf_cycle_start();
			int err = lc3_decode(ctx->decoder, l_data, octets_per_channel,
					     LC3_PCM_FORMAT_S16, stereo_out, 2);
			audio_perf_cycle_end(t0, AUDIO_PERF_PATH_LC3_DECODE);

			if (err == 1) {
				audio_stats_frame_plc();
			} else if (err < 0) {
				audio_stats_decode_error();
			} else {
				audio_stats_frame_decoded();
			}

			t0 = audio_perf_cycle_start();
			err = lc3_decode(ctx->decoder_r, r_data, octets_per_channel,
					 LC3_PCM_FORMAT_S16, stereo_out + 1, 2);
			audio_perf_cycle_end(t0, AUDIO_PERF_PATH_LC3_DECODE);
			if (err < 0) {
				audio_stats_decode_error();
			}
		}
	} else {
		/* Mono: decode with stride=1 into even positions, then
		 * duplicate to odd positions.
		 */
		const uint8_t *ptr = frame_data;

		for (int i = 0; i < f_per_sdu; i++) {
			const void *data = valid ? ptr : NULL;

			if (valid) {
				ptr += octets_per_frame;
			}

			uint32_t t0 = audio_perf_cycle_start();
			int err = lc3_decode(ctx->decoder, data, octets_per_frame,
					     LC3_PCM_FORMAT_S16, stereo_out, 1);
			audio_perf_cycle_end(t0, AUDIO_PERF_PATH_LC3_DECODE);

			if (err == 1) {
				audio_stats_frame_plc();
			} else if (err < 0) {
				audio_stats_decode_error();
			} else {
				audio_stats_frame_decoded();
			}
		}

		/* Duplicate mono into both stereo channels */
		audio_decode_mono_to_stereo(stereo_out, stereo_out, spc);
	}

	return 0;
}

void audio_decode_reset(struct audio_decode_ctx *ctx)
{
	ctx->decoder = NULL;
	ctx->decoder_r = NULL;
}

#else /* !CONFIG_LIBLC3 */

int audio_decode_config(struct audio_decode_ctx *ctx, int chan_count, int freq_hz, int frame_us,
			int frames_per_sdu)
{
	(void)ctx;
	(void)chan_count;
	(void)freq_hz;
	(void)frame_us;
	(void)frames_per_sdu;
	return -ENOSYS;
}

int audio_decode_sdu(struct audio_decode_ctx *ctx, const uint8_t *frame_data, size_t frame_len,
		     bool valid, int16_t *stereo_out)
{
	(void)ctx;
	(void)frame_data;
	(void)frame_len;
	(void)valid;
	(void)stereo_out;
	return -ENOSYS;
}

void audio_decode_reset(struct audio_decode_ctx *ctx)
{
	(void)ctx;
}

#endif /* CONFIG_LIBLC3 */

void audio_decode_mono_to_stereo(const int16_t *mono, int16_t *stereo_out, int samples)
{
	for (int i = 0; i < samples; i++) {
		stereo_out[2 * i] = mono[i];
		stereo_out[2 * i + 1] = mono[i];
	}
}

void audio_decode_interleave(const int16_t *l, const int16_t *r, int16_t *stereo_out, int samples)
{
	for (int i = 0; i < samples; i++) {
		stereo_out[2 * i] = l[i];
		stereo_out[2 * i + 1] = r[i];
	}
}
