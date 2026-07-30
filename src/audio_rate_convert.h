/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fixed-rate sample-count conversion for I2S drain-rate matching.
 * Pure integer helper: no floats, no heap, no Zephyr dependency.
 */

#ifndef AUDIO_RATE_CONVERT_H
#define AUDIO_RATE_CONVERT_H

#include <stdint.h>
#include <stddef.h>

struct audio_rate_converter {
	uint32_t remainder;      /* fractional-frame accumulator */
	uint32_t input_rate_hz;  /* nominal input sample rate (e.g. 48000) */
	uint32_t output_rate_hz; /* actual I2S output sample rate (e.g. 47619) */
};

/**
 * @brief Initialize a rate converter.
 *
 * @param ctx            Converter context (caller-allocated).
 * @param input_rate_hz  Decoder output sample rate (48 kHz).
 * @param output_rate_hz Actual I2S LRCK sample rate.
 */
void audio_rate_converter_init(struct audio_rate_converter *ctx, uint32_t input_rate_hz,
			       uint32_t output_rate_hz);

/**
 * @brief Compute output frame count for this input block.
 *
 * Uses a remainder accumulator so that over many calls the total output
 * frames equals the proportional down-sample of total input frames.
 * Individual results are bounded by floor/ceil of the ideal ratio.
 *
 * @param ctx          Converter context.
 * @param input_frames Number of input stereo frames (typically 480).
 * @return             Number of output stereo frames for this block.
 */
size_t audio_rate_converter_next_frames(struct audio_rate_converter *ctx, size_t input_frames);

/**
 * @brief Nearest-neighbor stereo resampling.
 *
 * Maps output stereo pairs to the nearest input stereo pairs, preserving
 * L/R interleaving. Uses integer Bresenham rounding.
 *
 * @param input         Interleaved stereo input [L,R,L,R,…].
 * @param input_frames  Number of input stereo frame pairs.
 * @param output        Output buffer, interleaved stereo.
 * @param output_frames Number of output stereo frame pairs.
 */
void audio_rate_converter_nearest_stereo(const int16_t *input, size_t input_frames, int16_t *output,
					 size_t output_frames);

#endif /* AUDIO_RATE_CONVERT_H */
