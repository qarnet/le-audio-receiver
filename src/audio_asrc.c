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
 * source_step = input_rate / output_rate * (1 + ppm / 1_000_000)
 *
 * Interpolation uses a signed-int64 formulation that is correct for
 * all s16 extreme combinations and fractional endpoints.
 */

#include "audio_asrc.h"
#include <errno.h>

#define Q32_FRAC_MASK 0xFFFFFFFFULL
#define Q32_ONE       (1ULL << 32)

/**
 * Linear interpolation between two s16 values.
 *
 *   result = round((int64_t)a * (2³² − frac) + (int64_t)b * frac) / 2³²)
 *
 * All arithmetic is signed int64_t; the intermediate sum is rounded
 * by adding 2³¹ before shifting.  The result is guaranteed to be
 * within s16 range (the weighted average of two s16 values cannot
 * overflow).
 */
static inline int16_t interp_s16(int16_t a, int16_t b, uint32_t frac)
{
	int64_t wa = (int64_t)a * (int64_t)(Q32_ONE - frac);
	int64_t wb = (int64_t)b * (int64_t)frac;
	int64_t sum = wa + wb;

	/* Round to nearest (ties away from zero). */
	sum += (int64_t)(Q32_ONE >> 1);

	return (int16_t)(sum >> 32);
}

/**
 * Compute effective Q32.32 source_step for the given ppm correction.
 *
 *   step = step_base · (1 + ppm / 1_000_000)
 *
 * step_base is cast to int64_t before multiplying by the signed ppm
 * so the product stays signed; otherwise C's usual arithmetic
 * conversions promote ppm to uint64_t, wrapping negative values.
 */
static uint64_t compute_step(uint64_t step_base, int32_t ppm)
{
	if (ppm == 0) {
		return step_base;
	}

	int64_t base_signed = (int64_t)step_base;
	int64_t delta = (base_signed * (int64_t)ppm) / 1000000LL;

	if (delta >= 0) {
		return (uint64_t)(base_signed + delta);
	}
	/* delta < 0: step = step_base − |delta|.
	 * For |ppm| ≤ 3000 and realistic rates this cannot underflow.
	 */
	return (uint64_t)(base_signed + delta); /* delta is negative */
}

/* ── public API ──────────────────────────────────────────────────── */

int audio_asrc_init(struct audio_asrc *ctx, uint32_t input_rate_hz, uint32_t output_rate_hz)
{
	if (!ctx) {
		return -EINVAL;
	}
	if (input_rate_hz == 0 || output_rate_hz == 0) {
		return -EINVAL;
	}

	/* step_base = input_rate / output_rate  in Q32.32.
	 * input_rate_hz ≤ 192 000, output_rate_hz ≥ 1  →
	 * numerator ≤ 192 000 · 2³² ≈ 8.2·10¹⁴, fits in uint64_t.
	 */
	uint64_t step = ((uint64_t)input_rate_hz << 32) / output_rate_hz;

	if (step == 0) {
		/* Division result < 1 — rate pair unsupported. */
		return -EINVAL;
	}

	ctx->phase = 0;
	ctx->step_base = step;
	ctx->prev_valid = false;
	ctx->prev_l = 0;
	ctx->prev_r = 0;
	return 0;
}

int audio_asrc_process(struct audio_asrc *ctx, const int16_t *input, size_t input_frames,
		       int16_t *output, size_t output_capacity, int32_t correction_ppm,
		       size_t *input_consumed, size_t *output_produced)
{
	if (!ctx || !input || !output || !input_consumed || !output_produced) {
		return -EINVAL;
	}
	if (correction_ppm < -ASRC_MAX_PPM || correction_ppm > ASRC_MAX_PPM) {
		return -EINVAL;
	}

	/* Zero input / zero capacity handled after validation so
	 * output parameters are set predictably.
	 */
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

	uint64_t step = compute_step(ctx->step_base, correction_ppm);

	if (step == 0) {
		/* Configuration error — division overflow, unsupported
		 * rate + ppm combination.
		 */
		*input_consumed = 0;
		*output_produced = 0;
		return -EINVAL;
	}

	/* ── work on locals; commit only on full success ──────────── */
	uint64_t phase = ctx->phase;
	size_t out = 0;
	bool have_prev = ctx->prev_valid;

	while (out < output_capacity) {
		size_t pos = (size_t)(phase >> 32);
		uint32_t frac = (uint32_t)(phase & Q32_FRAC_MASK);

		if (pos >= input_frames) {
			break;
		}

		int16_t l, r;

		if (pos == 0) {
			/* Extended sequence: s[−1] = prev, s[0] = input[0].
			 * At frac == 0 the output lands exactly on s[0]
			 * — use it directly so identity passes through.
			 * At frac > 0 and have_prev: interpolate prev→input[0].
			 * If !have_prev (first block after reset): use
			 * input[0] directly (phase is 0 after reset, so
			 * frac == 0 here).
			 */
			if (frac == 0 || !have_prev) {
				l = input[0];
				r = input[1];
			} else {
				l = interp_s16(ctx->prev_l, input[0], frac);
				r = interp_s16(ctx->prev_r, input[1], frac);
			}
		} else if (pos + 1 < input_frames) {
			/* Both neighbours are inside the current block. */
			l = interp_s16(input[pos * 2], input[(pos + 1) * 2], frac);
			r = interp_s16(input[pos * 2 + 1], input[(pos + 1) * 2 + 1], frac);
		} else {
			/* pos == input_frames − 1 and pos+1 is the next
			 * block's first sample, which is unavailable.
			 * Emit only when frac == 0 (the right-neighbour
			 * weight is zero, so the result is exactly the
			 * left neighbour).  Otherwise carry the phase to
			 * the next block.
			 */
			if (frac == 0) {
				l = input[pos * 2];
				r = input[pos * 2 + 1];
			} else {
				break;
			}
		}

		output[out * 2] = l;
		output[out * 2 + 1] = r;
		out++;

		phase += step;

		/* Overflow guard — 64-bit phase with step < 2³⁵
		 * means > 5·10⁸ output samples per block needed
		 * to wrap; cannot happen.
		 */
		if (phase < step) {
			break;
		}
	}

	/* Consumed = whole input frames whose samples have been
	 * fully covered by the phase advance.
	 */
	size_t consumed = (size_t)(phase >> 32);

	if (consumed > input_frames) {
		consumed = input_frames;
	}

	if (out == output_capacity && consumed < input_frames) {
		/* Output exhausted before all input — do NOT commit. */
		*input_consumed = consumed;
		*output_produced = out;
		return 1;
	}

	/* Full success: commit state. */
	if (consumed > 0) {
		ctx->prev_l = input[(consumed - 1) * 2];
		ctx->prev_r = input[(consumed - 1) * 2 + 1];
		ctx->prev_valid = true;
	}
	/* consumed == 0: prev stays as-is. */

	ctx->phase = phase - ((uint64_t)consumed << 32);

	*input_consumed = consumed;
	*output_produced = out;
	return 0;
}

void audio_asrc_reset(struct audio_asrc *ctx)
{
	if (!ctx) {
		return;
	}
	ctx->phase = 0;
	ctx->prev_valid = false;
	ctx->prev_l = 0;
	ctx->prev_r = 0;
}
