/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stateful cross-block fixed-point linear-interpolation stereo ASRC.
 *
 * Extended-sequence coordinate model:
 *   ext[0] = prev  (last frame of previous block)
 *   ext[j] = input[j−1]  for j = 1 … N   (N = input_frames)
 *
 * Phase (Q32.32) indexes into ext[].  First block seeded at
 * phase = 1·2³² so the first output is ext[1] = input[0].
 * Subsequent blocks start at the carry-over phase (0 ≤ phase < 2³²
 * for fractional boundaries, or exactly 2³² for identity).
 *
 * Interpolation:
 *   pos = 0          → lerp(prev, input[0])      (carry-over only)
 *   1 ≤ pos < N      → lerp(input[pos−1], input[pos])
 *   pos = N, frac=0  → input[N−1]                (right weight = 0)
 *   pos = N, frac>0  → break                     (need next block)
 *
 * Commit: consumed = min(phase>>32, N);
 *         prev   = input[N−1];
 *         phase  = phase − consumed·2³².
 */

#include "audio_asrc.h"
#include <string.h>
#include <errno.h>

#define Q32_FRAC_MASK 0xFFFFFFFFULL

/* Use public symbol from header. */
#define Q32_ONE ASRC_Q32_ONE

/* ── interp_s16 ────────────────────────────────────────────────────
 *
 * Linear interpolation between two signed-16 values.
 * Uses signed int64_t exclusively; result obtained by signed
 * division by Q32_ONE (2³²) — no right shift of negative values.
 *
 * Rounding rule: nearest, ties away from zero.
 * Implementation: truncate toward zero via C99 signed division,
 * then inspect the remainder to decide whether to round away.
 *
 *   |remainder| >  Q32_ONE/2  → round away from zero
 *   |remainder| == Q32_ONE/2  → tie, round away from zero
 *
 * Result guaranteed within s16 range (weighted average of two
 * s16 values).
 */

static inline int16_t interp_s16(int16_t a, int16_t b, uint32_t frac)
{
	int64_t wa = (int64_t)a * (int64_t)(Q32_ONE - frac);
	int64_t wb = (int64_t)b * (int64_t)frac;
	int64_t sum = wa + wb;
	int64_t q32 = (int64_t)Q32_ONE;
	int64_t half = q32 / 2;

	int64_t result = sum / q32;       /* truncate toward zero (C99 §6.5.5) */
	int64_t rem = sum - result * q32; /* exact remainder, sign = dividend */

	if (rem > half || rem < -half || rem == half || rem == -half) {
		result += (sum >= 0) ? 1 : -1;
	}
	return (int16_t)result;
}

/* ── compute_step ────────────────────────────────────────────────── */

static uint64_t compute_step(uint64_t step_base, int32_t ppm)
{
	if (ppm == 0) {
		return step_base;
	}

	int64_t base_signed = (int64_t)step_base;
	/* Round the delta to nearest (ties away from zero for
	 * negative values via truncation toward zero in C99).
	 */
	int64_t delta = (base_signed * (int64_t)ppm + 500000) / 1000000LL;

	int64_t step_signed = base_signed + delta;

	if (step_signed <= 0) {
		return 0;
	}
	return (uint64_t)step_signed;
}

/* ── public API ─────────────────────────────────────────────── */

/* ── public API ──────────────────────────────────────────────────── */

int audio_asrc_init(struct audio_asrc *ctx, uint32_t input_rate_hz, uint32_t output_rate_hz)
{
	if (!ctx) {
		return -EINVAL;
	}
	if (input_rate_hz < ASRC_RATE_MIN || input_rate_hz > ASRC_RATE_MAX) {
		return -EINVAL;
	}
	if (output_rate_hz < ASRC_RATE_MIN || output_rate_hz > ASRC_RATE_MAX) {
		return -EINVAL;
	}

	/* Nearest-neighbour rounding of the exact rational step.
	 * Without rounding the truncation bias accumulates, causing
	 * cumulative output drift versus the continuous ideal.
	 */
	uint64_t step = (((uint64_t)input_rate_hz << 32) + output_rate_hz / 2) / output_rate_hz;

	if (step == 0) {
		return -EINVAL;
	}

	ctx->step_base = step;
	ctx->phase = Q32_ONE; /* seeded to ext[1] = input[0] */
	return 0;
}

int audio_asrc_process(struct audio_asrc *ctx, const int16_t *input, size_t input_frames,
		       int16_t *output, size_t output_capacity, int32_t correction_ppm,
		       int16_t prev_l, int16_t prev_r, bool prev_valid, size_t *input_consumed,
		       size_t *output_produced, int16_t *next_prev_l, int16_t *next_prev_r)
{
	if (!ctx || !input || !output || !input_consumed || !output_produced || !next_prev_l ||
	    !next_prev_r) {
		return -EINVAL;
	}
	if (correction_ppm < -ASRC_MAX_PPM || correction_ppm > ASRC_MAX_PPM) {
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

	uint64_t step = compute_step(ctx->step_base, correction_ppm);

	if (step == 0) {
		return -EINVAL;
	}

	uint64_t phase = ctx->phase;
	size_t out = 0;

	while (out < output_capacity) {
		size_t pos = (size_t)(phase >> 32);
		uint32_t frac = (uint32_t)(phase & Q32_FRAC_MASK);

		/* Extended sequence runs 0 … input_frames.
		 * pos >= input_frames+1 → past all available data.
		 */
		if (pos > input_frames) {
			break;
		}

		int16_t l, r;

		if (pos == 0) {
			if (!prev_valid) {
				/* First block seeded at 1.0 —
				 * pos==0 should not be reachable.
				 * Use input[0] as fallback.
				 */
				l = input[0];
				r = input[1];
			} else {
				/* Carry-over: lerp(prev, input[0]). */
				l = interp_s16(prev_l, input[0], frac);
				r = interp_s16(prev_r, input[1], frac);
			}
		} else if (pos < input_frames) {
			/* pos ∈ [1, N−1]:
			 * lerp(ext[pos]=input[pos−1], ext[pos+1]=input[pos]).
			 */
			size_t idx = pos - 1;

			l = interp_s16(input[idx * 2], input[(idx + 1) * 2], frac);
			r = interp_s16(input[idx * 2 + 1], input[(idx + 1) * 2 + 1], frac);
		} else {
			/* pos == input_frames:
			 * ext[N] = input[N−1]; ext[N+1] unavailable.
			 * Emit only when frac == 0 (right weight is zero).
			 */
			if (frac == 0) {
				l = input[(input_frames - 1) * 2];
				r = input[(input_frames - 1) * 2 + 1];
			} else {
				break;
			}
		}

		output[out * 2] = l;
		output[out * 2 + 1] = r;
		out++;

		phase += step;

		if (phase < step) {
			/* uint64_t wrap guard. */
			break;
		}
	}

	/* Consumed = whole frames of input covered by phase.
	 * Since phase indexes ext[0…N], consumed ≤ N.
	 */
	size_t consumed = (size_t)(phase >> 32);

	if (consumed > input_frames) {
		consumed = input_frames;
	}

	if (out == output_capacity && consumed < input_frames) {
		*input_consumed = consumed;
		*output_produced = out;
		return 1;
	}

	/* Commit. */
	ctx->phase = phase - ((uint64_t)consumed << 32);

	/* Always carry the last input frame as prev for the next block. */
	*next_prev_l = input[(input_frames - 1) * 2];
	*next_prev_r = input[(input_frames - 1) * 2 + 1];

	*input_consumed = consumed;
	*output_produced = out;
	return 0;
}

void audio_asrc_reset(struct audio_asrc *ctx)
{
	if (!ctx) {
		return;
	}
	ctx->phase = Q32_ONE;
}

/* ── State export / import ───────────────────────────────────────── */

void audio_asrc_state_export(const struct audio_asrc *ctx, int16_t prev_l, int16_t prev_r,
			     bool prev_valid, struct audio_asrc_state *dst)
{
	if (!ctx || !dst) {
		return;
	}
	memset(dst, 0, sizeof(*dst));
	dst->phase = ctx->phase;
	dst->step_base = ctx->step_base;
	dst->prev_l = prev_l;
	dst->prev_r = prev_r;
	dst->prev_valid = prev_valid ? 1U : 0U;
	/* reserved[] already zeroed by memset */
}

int audio_asrc_state_import(struct audio_asrc *ctx, const struct audio_asrc_state *src,
			    int16_t *prev_l_out, int16_t *prev_r_out, bool *prev_valid_out)
{
	if (!ctx || !src) {
		return -EINVAL;
	}
	if (src->step_base == 0) {
		return -EINVAL;
	}
	if (src->prev_valid > 1U) {
		return -EINVAL;
	}
	if (src->reserved[0] != 0 || src->reserved[1] != 0 || src->reserved[2] != 0) {
		return -EINVAL;
	}

	/* All validations passed — commit. */
	ctx->phase = src->phase;
	ctx->step_base = src->step_base;

	if (prev_l_out) {
		*prev_l_out = src->prev_l;
	}
	if (prev_r_out) {
		*prev_r_out = src->prev_r;
	}
	if (prev_valid_out) {
		*prev_valid_out = (src->prev_valid != 0);
	}

	return 0;
}
