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

/* Receiver-advertised supported shapes.  Anything else is rejected with
 * -EINVAL before liblc3 is touched (see behavior contract CODEC-007).
 */
#define AUDIO_DECODE_FREQ_HZ         48000
#define AUDIO_DECODE_FRAME_US_MIN    7500
#define AUDIO_DECODE_FRAME_US_MAX    10000
#define AUDIO_DECODE_MIN_FRAME_BYTES 20
#define AUDIO_DECODE_MAX_FRAME_BYTES 400

int audio_decode_config(struct audio_decode_ctx *ctx, int chan_count, int freq_hz, int frame_us,
			int frames_per_sdu)
{
	if (!ctx) {
		return -EINVAL;
	}

	/* Full reset first: every failure path leaves a safely reset context. */
	audio_decode_reset(ctx);

	if (chan_count != 1 && chan_count != 2) {
		return -EINVAL;
	}
	if (freq_hz != AUDIO_DECODE_FREQ_HZ) {
		return -EINVAL;
	}
	if (frame_us != AUDIO_DECODE_FRAME_US_MIN && frame_us != AUDIO_DECODE_FRAME_US_MAX) {
		return -EINVAL;
	}
	if (frames_per_sdu != 1) {
		return -EINVAL;
	}

	ctx->chan_count = chan_count;
	ctx->samples_per_ch = (frame_us * freq_hz) / USEC_PER_SEC;
	ctx->frames_per_sdu = frames_per_sdu;

	ctx->decoder = lc3_setup_decoder(frame_us, freq_hz, 0, &ctx->dec_mem);
	if (!ctx->decoder) {
		audio_decode_reset(ctx);
		return -EINVAL;
	}

	if (chan_count == 2) {
		ctx->decoder_r = lc3_setup_decoder(frame_us, freq_hz, 0, &ctx->dec_mem_r);
		if (!ctx->decoder_r) {
			/* No usable decoder state after a partial setup. */
			audio_decode_reset(ctx);
			return -EINVAL;
		}
	}

	return 0;
}

int audio_decode_sdu(struct audio_decode_ctx *ctx, const uint8_t *frame_data, size_t frame_len,
		     bool valid, int16_t *stereo_out)
{
	/* All validation happens before output or decoder state is touched. */
	if (!ctx || !stereo_out) {
		return -EINVAL;
	}
	if (!ctx->decoder) {
		return -EINVAL; /* unconfigured / reset */
	}
	if (ctx->chan_count != 1 && ctx->chan_count != 2) {
		return -EINVAL;
	}
	if (ctx->samples_per_ch != 360 && ctx->samples_per_ch != 480) {
		return -EINVAL;
	}
	if (ctx->frames_per_sdu != 1) {
		return -EINVAL;
	}
	if (ctx->chan_count == 2 && !ctx->decoder_r) {
		return -EINVAL;
	}

	const int f_per_sdu = ctx->frames_per_sdu;
	const int spc = ctx->samples_per_ch;
	const int chan_count = ctx->chan_count;

	/* Per-channel frame-byte shape.  For PLC this is the "supplied valid
	 * configured frame-byte shape": liblc3 still consumes nbytes on the
	 * concealment path (LTPF post-filter strength), so the shape must be
	 * the same value a real frame would carry.
	 */
	int octets_per_channel = (int)(frame_len / (size_t)f_per_sdu);

	if (valid) {
		/* Length/shape checks apply to real frames.  PLC (valid=false)
		 * accepts any supplied length and never dereferences frame data.
		 */
		if (!frame_data) {
			return -EINVAL;
		}
		if (frame_len == 0) {
			return -EINVAL;
		}

		/* Bound the shape before any narrowing cast: per-channel frame
		 * length must stay within liblc3 basic 20..400 byte range.
		 */
		if (frame_len > (size_t)(chan_count == 2 ? 800 : 400)) {
			return -EINVAL;
		}

		if (chan_count == 2) {
			if (octets_per_channel % chan_count != 0) {
				return -EINVAL; /* Mode B must split exactly per channel */
			}
			octets_per_channel /= chan_count;
		}
		if (octets_per_channel < AUDIO_DECODE_MIN_FRAME_BYTES ||
		    octets_per_channel > AUDIO_DECODE_MAX_FRAME_BYTES) {
			return -EINVAL;
		}
	} else if (chan_count == 2) {
		octets_per_channel /= chan_count;
	}

	int ret = 0;

	if (chan_count == 2) {
		/* Mode B: stereo single-ASE — split SDU per-channel,
		 * two independent decoders with stride=2.
		 */
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
				ret = -EBADMSG;
			} else {
				audio_stats_frame_decoded();
			}

			/* Always invoke the second decoder: independent decoder
			 * state stays aligned even when the first failed.
			 */
			t0 = audio_perf_cycle_start();
			err = lc3_decode(ctx->decoder_r, r_data, octets_per_channel,
					 LC3_PCM_FORMAT_S16, stereo_out + 1, 2);
			audio_perf_cycle_end(t0, AUDIO_PERF_PATH_LC3_DECODE);

			if (err == 1) {
				audio_stats_frame_plc();
			} else if (err < 0) {
				audio_stats_decode_error();
				ret = -EBADMSG;
			} else {
				audio_stats_frame_decoded();
			}
		}
	} else {
		/* Mono: decode with stride=1 into even positions, then
		 * duplicate to odd positions (overlap-safe expansion).
		 */
		const uint8_t *ptr = frame_data;

		for (int i = 0; i < f_per_sdu; i++) {
			const void *data = valid ? ptr : NULL;

			if (valid) {
				ptr += octets_per_channel;
			}

			uint32_t t0 = audio_perf_cycle_start();
			int err = lc3_decode(ctx->decoder, data, octets_per_channel,
					     LC3_PCM_FORMAT_S16, stereo_out, 1);
			audio_perf_cycle_end(t0, AUDIO_PERF_PATH_LC3_DECODE);

			if (err == 1) {
				audio_stats_frame_plc();
			} else if (err < 0) {
				audio_stats_decode_error();
				ret = -EBADMSG;
			} else {
				audio_stats_frame_decoded();
			}
		}

		/* Duplicate mono into both stereo channels */
		audio_decode_mono_to_stereo(stereo_out, stereo_out, spc);
	}

	return ret;
}

void audio_decode_reset(struct audio_decode_ctx *ctx)
{
	if (!ctx) {
		return;
	}

	ctx->chan_count = 0;
	ctx->samples_per_ch = 0;
	ctx->frames_per_sdu = 0;
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
	if (mono == stereo_out) {
		/* Overlap-safe in-place expansion.  Backward order: the read
		 * at position i always happens before any write to 2i / 2i+1,
		 * and no later read depends on a position already written.
		 */
		for (int i = samples - 1; i >= 0; i--) {
			stereo_out[2 * i + 1] = mono[i];
			stereo_out[2 * i] = mono[i];
		}
	} else {
		/* Separate buffers: forward order is safe. */
		for (int i = 0; i < samples; i++) {
			stereo_out[2 * i] = mono[i];
			stereo_out[2 * i + 1] = mono[i];
		}
	}
}

void audio_decode_interleave(const int16_t *l, const int16_t *r, int16_t *stereo_out, int samples)
{
	for (int i = 0; i < samples; i++) {
		stereo_out[2 * i] = l[i];
		stereo_out[2 * i + 1] = r[i];
	}
}
