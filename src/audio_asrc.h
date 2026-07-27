/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stateful cross-block fixed-point linear-interpolation stereo ASRC.
 *
 * Continuous source-phase accumulator and previous-frame sample
 * history carried across calls.  Common phase for L/R, 64-bit
 * intermediate arithmetic.  No heap, no float, no Zephyr dependency.
 *
 * source_step = input_rate / output_rate * (1 + ppm / 1_000_000)
 * Positive ppm → consume source faster → fewer output frames.
 * Controller output sign preserved unchanged.
 */

#ifndef AUDIO_ASRC_H
#define AUDIO_ASRC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** Maximum absolute ppm accepted by audio_asrc_process. */
#define ASRC_MAX_PPM 3000

struct audio_asrc {
	/** Q32.32 fractional source position relative to the current
	 *  input block.  0 ≤ phase < input_frames · 2³².
	 */
	uint64_t phase;

	/** Q32.32 nominal source_step (at 0 ppm), set at init. */
	uint64_t step_base;

	/** Last L/R sample from the *consumed* portion of the previous
	 *  block — used as s[−1] in the extended sequence.
	 */
	int16_t prev_l;
	int16_t prev_r;

	/** True after at least one input frame has been fully consumed. */
	bool prev_valid;
};

/**
 * @brief Initialise the ASRC.
 *
 * @param ctx            Zeroed caller-allocated context.
 * @param input_rate_hz  Decoder output rate (48 000).
 * @param output_rate_hz Physical I2S output rate (47 619 on nRF54L15).
 *
 * @retval  0   Success.
 * @retval -EINVAL  Null ctx, zero rate, or rate overflow.
 */
int audio_asrc_init(struct audio_asrc *ctx, uint32_t input_rate_hz, uint32_t output_rate_hz);

/**
 * @brief Process one block of input through the resampler.
 *
 * Resampled interleaved int16_t stereo output is written to @p output.
 * The per-block @p correction_ppm changes the effective source_step.
 *
 * On success (return 0) all @p input_frames are consumed and
 * @p output_produced > 0.  Phase and prev samples are updated only
 * on a fully successful call; on any failure the context is unchanged
 * (transactional).
 *
 * @param ctx             ASRC state (updated in-place only on success).
 * @param input           Interleaved stereo input [L,R,…].
 * @param input_frames    Stereo input frames.
 * @param output          Output buffer, interleaved stereo.
 * @param output_capacity Max stereo frames that fit in @p output.
 * @param correction_ppm  Drift-controller ppm (−3000 … +3000).
 * @param input_consumed  [out] Stereo frames consumed (0 on non-zero ret).
 * @param output_produced [out] Stereo frames written.
 *
 * @retval  0   All input consumed, output_produced > 0.
 * @retval  1   Output capacity exhausted before all input consumed;
 *              context unchanged.  input_consumed / output_produced
 *              report what WOULD have been consumed/produced.
 * @retval -EINVAL  Null pointer or |ppm| > ASRC_MAX_PPM.
 */
int audio_asrc_process(struct audio_asrc *ctx, const int16_t *input, size_t input_frames,
		       int16_t *output, size_t output_capacity, int32_t correction_ppm,
		       size_t *input_consumed, size_t *output_produced);

/**
 * @brief Reset ASRC state (phase, history).
 *
 * Call on stream stop / disconnect.
 */
void audio_asrc_reset(struct audio_asrc *ctx);

#endif /* AUDIO_ASRC_H */
