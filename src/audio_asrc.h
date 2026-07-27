/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stateful cross-block fixed-point linear-interpolation stereo ASRC.
 *
 * Continuous source phase and previous-frame sample history carried
 * across calls.  Uses common phase for L/R and 64-bit intermediate
 * arithmetic.  No heap, no float, no Zephyr dependency.
 *
 * source_step = input_rate / physical_output_rate
 *               * (1 + correction_ppm / 1_000_000)
 *
 * Positive correction consumes source faster; negative consumes slower.
 * Existing drift-controller output sign preserved unchanged.
 *
 * When input_rate == output_rate and correction_ppm == 0 the
 * source_step is exactly 2^32, producing an identity pass-through.
 */

#ifndef AUDIO_ASRC_H
#define AUDIO_ASRC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct audio_asrc {
	/** Q32.32 fractional source position (within current block). */
	uint64_t phase;

	/** Q32.32 nominal source_step (at 0 ppm), computed at init. */
	uint64_t step_base;

	/** Last L sample from previous block for interpolation. */
	int16_t prev_l;

	/** Last R sample from previous block for interpolation. */
	int16_t prev_r;

	/** True after at least one output sample has been produced. */
	bool prev_valid;
};

/**
 * @brief Initialise the ASRC.
 *
 * @param ctx            Caller-allocated context (zeroed).
 * @param input_rate_hz  Decoder output rate (normally 48 000).
 * @param output_rate_hz Physical I2S output rate (e.g. 47 619 on nRF54L15).
 */
void audio_asrc_init(struct audio_asrc *ctx, uint32_t input_rate_hz, uint32_t output_rate_hz);

/**
 * @brief Process one block of input through the resampler.
 *
 * Resampled output is written to @p output (interleaved int16_t stereo).
 * The per-block @p correction_ppm is consumed from the drift controller
 * and changes the effective source_step.
 *
 * @param ctx             ASRC state (updated in-place on success).
 * @param input           Interleaved stereo input [L,R,L,R,…].
 * @param input_frames    Number of stereo input frames.
 * @param output          Output buffer, interleaved stereo.
 * @param output_capacity Maximum stereo frames that fit in @p output.
 * @param correction_ppm  Drift-controller ppm for this block.
 * @param input_consumed  [out] Stereo frames consumed from @p input.
 * @param output_produced [out] Stereo frames written to @p output.
 *
 * @retval  0  Success – all input consumed.
 * @retval  1  Output capacity exhausted before all input processed.
 *             State is left unchanged (phase / prev not advanced
 *             past the partial output).  Caller must retry with
 *             larger capacity or signal an overrun.
 * @retval -EINVAL  Invalid parameter (NULL pointers, zero rates).
 */
int audio_asrc_process(struct audio_asrc *ctx, const int16_t *input, size_t input_frames,
		       int16_t *output, size_t output_capacity, int32_t correction_ppm,
		       size_t *input_consumed, size_t *output_produced);

/**
 * @brief Reset ASRC state (phase, history).
 *
 * Must be called on stream stop / disconnect so a fresh stream
 * starts clean.
 */
void audio_asrc_reset(struct audio_asrc *ctx);

#endif /* AUDIO_ASRC_H */
