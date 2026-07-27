/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stateful cross-block fixed-point linear-interpolation stereo ASRC.
 *
 * Implements stereo s16 → stereo s16 resampling with a continuous
 * Q32.32 source-phase accumulator and one-sample history per channel
 * carried across block boundaries.
 *
 * source_step = input_rate / physical_output_rate
 *               * (1 + correction_ppm / 1_000_000)
 *
 * Step computed each block from the per-block correction_ppm so
 * abrupt ppm changes take effect without state discontinuity.
 */

#include "audio_asrc.h"
#include <errno.h>

#define Q32_FRAC_MASK 0xFFFFFFFFULL
#define Q32_ONE       (1ULL << 32)

/**
 * Linear interpolation between two s16 samples via fixed-point.
 *
 *   result = a * (2^32 - frac) / 2^32  +  b * frac / 2^32
 *
 * Uses 64-bit intermediate to avoid overflow, then rounds.
 */
static inline int16_t interp_s16(int16_t a, int16_t b, uint32_t frac)
{
	/* (a * (1-frac) + b * frac) >> 32, rounded */
	uint64_t term_a = (uint64_t)(int64_t)a * (Q32_ONE - frac);
	uint64_t term_b = (uint64_t)(int64_t)b * frac;

	return (int16_t)((int64_t)(term_a + term_b + (Q32_ONE >> 1)) >> 32);
}

void audio_asrc_init(struct audio_asrc *ctx, uint32_t input_rate_hz, uint32_t output_rate_hz)
{
	ctx->phase = 0;
	ctx->prev_valid = false;
	ctx->prev_l = 0;
	ctx->prev_r = 0;

	/* step_base = input_rate / output_rate  in Q32.32.
	 * Use uint64_t multiply-shift-divide; the numerator
	 * input_rate_hz << 32 fits comfortably for ≤ 192 000.
	 */
	ctx->step_base = ((uint64_t)input_rate_hz << 32) / output_rate_hz;
}

/**
 * Compute effective Q32.32 source_step for the given ppm correction.
 *
 *   step = step_base * (1 + ppm / 1_000_000)
 *        = step_base + step_base * ppm / 1_000_000
 *
 * Uses int64_t for the delta so negative ppm produces subtraction.
 */
static uint64_t compute_step(uint64_t step_base, int32_t ppm)
{
	if (ppm == 0) {
		return step_base;
	}

	/* Cast step_base to int64_t before multiplying by the signed ppm
	 * so the product is signed — otherwise the usual arithmetic
	 * conversions promote ppm to uint64_t and wrap negative values
	 * into enormous positive numbers.
	 */
	int64_t delta = (int64_t)(((int64_t)step_base * (int64_t)ppm) / 1000000LL);

	if (delta >= 0) {
		return step_base + (uint64_t)delta;
	}
	/* uint64_t subtraction with guard: step_base >> |delta| for small
	 * corrections on reasonable rates.  The worst |delta| for
	 * ppm ∈ [−2000,2000] at 48k→47.6k is ~0.2 % of step_base,
	 * so underflow cannot happen.
	 */
	return step_base - (uint64_t)(-delta);
}

int audio_asrc_process(struct audio_asrc *ctx, const int16_t *input, size_t input_frames,
		       int16_t *output, size_t output_capacity, int32_t correction_ppm,
		       size_t *input_consumed, size_t *output_produced)
{
	if (!input_consumed || !output_produced) {
		return -EINVAL;
	}
	if (input_frames == 0) {
		*input_consumed = 0;
		*output_produced = 0;
		return 0;
	}
	if (output_capacity == 0) {
		*input_consumed = 0;
		*output_produced = 0;
		return 1;
	}
	if (!input || !output) {
		return -EINVAL;
	}

	uint64_t step = compute_step(ctx->step_base, correction_ppm);

	/* Work with a local copy; only commit after successful completion. */
	uint64_t phase = ctx->phase;
	size_t out = 0;
	bool have_prev = ctx->prev_valid;

	while (out < output_capacity) {
		size_t pos = (size_t)(phase >> 32); /* integer source position */
		uint32_t frac = (uint32_t)(phase & Q32_FRAC_MASK);

		if (pos >= input_frames) {
			/* Consumed all input — stop. */
			break;
		}

		int16_t l, r;

		if (pos == 0) {
			/* Source position is within [0, 1).
			 * Left neighbour is the previous block's last
			 * sample (ctx->prev_l / ctx->prev_r); right
			 * neighbour is input[0] / input[1].
			 *
			 * At frac == 0 the output lands exactly on
			 * input[0] — use it directly so identity
			 * passthrough works without cross-block
			 * blending.
			 */
			if (frac == 0 || !have_prev) {
				l = input[0];
				r = input[1];
			} else {
				l = interp_s16(ctx->prev_l, input[0], frac);
				r = interp_s16(ctx->prev_r, input[1], frac);
			}
		} else if (pos + 1 < input_frames) {
			/* Both neighbours are inside the current block:
			 * lerp(s[pos], s[pos+1], frac).
			 */
			l = interp_s16(input[pos * 2], input[(pos + 1) * 2], frac);
			r = interp_s16(input[pos * 2 + 1], input[(pos + 1) * 2 + 1], frac);
		} else {
			/* pos == input_frames - 1: right neighbour
			 * (s[pos+1]) is the next block's first sample.
			 * Use self-clamp (right=left) so no sample is
			 * lost — cross-block interpolation happens
			 * via prev_l on the next call instead.
			 */
			l = interp_s16(input[pos * 2], input[pos * 2], frac);
			r = interp_s16(input[pos * 2 + 1], input[pos * 2 + 1], frac);
		}

		output[out * 2] = l;
		output[out * 2 + 1] = r;
		out++;

		phase += step;

		/* Overflow guard: if phase wraps, stop.  With 64-bit
		 * phase and step < 5e9, this would need > 3.7e9 output
		 * samples in a single block — cannot happen.
		 */
		if (phase < step) {
			break;
		}
	}

	/* Determine how many whole input frames were consumed. */
	size_t consumed = (size_t)(phase >> 32);

	if (consumed > input_frames) {
		consumed = input_frames;
	}

	if (out == output_capacity && consumed < input_frames) {
		/* Output capacity exhausted before all input processed.
		 * Do NOT advance state — caller must handle the overrun.
		 * Report what we would have done for diagnostics.
		 */
		*input_consumed = consumed;
		*output_produced = out;
		return 1;
	}

	/* Commit: update state. */
	if (consumed > 0) {
		ctx->prev_l = input[(consumed - 1) * 2];
		ctx->prev_r = input[(consumed - 1) * 2 + 1];
		ctx->prev_valid = true;
	}
	/* else: consumed == 0 — prev samples stay as-is from previous block. */

	/* Phase remainder for the next block (modulo current input range). */
	ctx->phase = phase - ((uint64_t)consumed << 32);

	*input_consumed = consumed;
	*output_produced = out;
	return 0;
}

void audio_asrc_reset(struct audio_asrc *ctx)
{
	ctx->phase = 0;
	ctx->prev_valid = false;
	ctx->prev_l = 0;
	ctx->prev_r = 0;
}
