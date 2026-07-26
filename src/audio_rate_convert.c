/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fixed-rate nearest-neighbor stereo resampling with a remainder
 * accumulator that distributes fractional frames across calls.
 */

#include "audio_rate_convert.h"

void audio_rate_converter_init(struct audio_rate_converter *ctx, uint32_t input_rate_hz,
			       uint32_t output_rate_hz)
{
	ctx->remainder = 0;
	ctx->input_rate_hz = input_rate_hz;
	ctx->output_rate_hz = output_rate_hz;
}

size_t audio_rate_converter_next_frames(struct audio_rate_converter *ctx, size_t input_frames)
{
	/* Widen before multiply to avoid overflow. */
	uint64_t acc = ctx->remainder + (uint64_t)ctx->output_rate_hz * input_frames;

	size_t output = (size_t)(acc / ctx->input_rate_hz);

	ctx->remainder = (uint32_t)(acc % ctx->input_rate_hz);
	return output;
}

void audio_rate_converter_nearest_stereo(const int16_t *input, size_t input_frames, int16_t *output,
					 size_t output_frames)
{
	if (input_frames == 0 || output_frames == 0) {
		return;
	}

	/*
	 * Nearest-neighbor with rounding:
	 *   src = round(i * (input_frames - 1) / max(1, output_frames - 1))
	 * Using integer Bresenham to avoid floats:
	 *   src = (2 * i * scale_num + scale_den) / (2 * scale_den)
	 * where scale_num = input_frames, scale_den = output_frames.
	 *
	 * For identity (input == output) this simplifies to i → i.
	 */
	if (input_frames == output_frames) {
		/* Fast path: identity copy. */
		const size_t bytes = output_frames * 2u * sizeof(int16_t);

		for (size_t i = 0; i < bytes; i++) {
			((uint8_t *)output)[i] = ((const uint8_t *)input)[i];
		}
		return;
	}

	if (output_frames == 1) {
		/* Map single output to the midpoint of the input. */
		size_t src = input_frames / 2;

		if (src >= input_frames) {
			src = input_frames - 1;
		}
		output[0] = input[src * 2];
		output[1] = input[src * 2 + 1];
		return;
	}

	/* General case: nearest-neighbor with rounding. */
	for (size_t i = 0; i < output_frames; i++) {
		/*
		 * src = floor((2*i*input_frames + output_frames) / (2*output_frames))
		 * This gives round-to-nearest for i * (input_frames-1)/(output_frames-1)
		 * when input_frames ≈ output_frames, and general rounding otherwise.
		 */
		uint64_t num = (uint64_t)i * (input_frames - 1) * 2u + (output_frames - 1);
		size_t src = (size_t)(num / ((output_frames - 1) * 2u));

		if (src >= input_frames) {
			src = input_frames - 1;
		}

		output[i * 2] = input[src * 2];
		output[i * 2 + 1] = input[src * 2 + 1];
	}
}
