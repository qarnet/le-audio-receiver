/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fixed-rate sample-count conversion for I2S drain-rate matching, with
 * a remainder accumulator that distributes fractional frames across
 * calls.
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
