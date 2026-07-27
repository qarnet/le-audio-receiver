/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stateful cross-block fixed-point linear-interpolation stereo ASRC.
 *
 * Extended-sequence coordinate model:
 *   ext[0] = previous block's last frame (prev)
 *   ext[j] = input[j−1]  for j = 1 … input_frames
 *
 * Phase (Q32.32) is the source position within ext[].  The first
 * block starts at phase = 1·2³² (ext[1] = input[0]); every
 * subsequent block inherits the carry-over phase (0 ≤ phase < 2³²
 * for fractional boundaries, or exactly 2³² for identity).
 *
 * source_step = input_rate / output_rate · (1 + ppm / 10⁶)
 * Positive ppm → consume faster → fewer output frames.
 */

#ifndef AUDIO_ASRC_H
#define AUDIO_ASRC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** Maximum absolute ppm accepted by audio_asrc_process. */
#define ASRC_MAX_PPM 3000

/** Minimum / maximum input / output rate (Hz). */
#define ASRC_RATE_MIN 1
#define ASRC_RATE_MAX 192000

struct audio_asrc {
	/** Q32.32 phase within the extended sequence [0, input_frames·2³²]. */
	uint64_t phase;

	/** Q32.32 nominal source_step (at 0 ppm), set at init. */
	uint64_t step_base;
};

/**
 * @brief Initialise the ASRC.
 *
 * @param ctx            Zeroed caller-allocated context.
 * @param input_rate_hz  Decoder output rate (48 000).
 * @param output_rate_hz Physical I2S output rate (47 619 on nRF54L15).
 *
 * @retval  0   Success.  ctx->phase seeded to 1·2³².
 * @retval  −EINVAL  Null ctx, rate out of [ASRC_RATE_MIN, ASRC_RATE_MAX],
 *                   or step underflow.
 */
int audio_asrc_init(struct audio_asrc *ctx, uint32_t input_rate_hz, uint32_t output_rate_hz);

/**
 * @brief Process one block through the resampler.
 *
 * On success all @p input_frames are consumed and @p output_produced > 0.
 * State is updated only on full success (transactional).
 *
 * @param ctx             ASRC context.
 * @param input           Interleaved stereo input [L,R,…].
 * @param input_frames    Stereo input frames.
 * @param output          Output buffer.
 * @param output_capacity Max stereo output frames.
 * @param correction_ppm  Drift-controller ppm (−3000 … +3000).
 * @param prev_l          Previous block's last L sample.
 * @param prev_r          Previous block's last R sample.
 * @param prev_valid      true after the first block has been processed.
 * @param input_consumed  [out] Stereo frames consumed.
 * @param output_produced [out] Stereo frames written.
 * @param next_prev_l     [out] Next-block prev L (= input[consumed−1]).
 * @param next_prev_r     [out] Next-block prev R.
 *
 * @retval  0   All input consumed, output_produced > 0.
 * @retval  1   Output capacity exhausted (state unchanged).
 * @retval  −EINVAL  Invalid argument.
 */
int audio_asrc_process(struct audio_asrc *ctx, const int16_t *input, size_t input_frames,
		       int16_t *output, size_t output_capacity, int32_t correction_ppm,
		       int16_t prev_l, int16_t prev_r, bool prev_valid, size_t *input_consumed,
		       size_t *output_produced, int16_t *next_prev_l, int16_t *next_prev_r);

/** Reset phase to 1·2³² (first-block state). */
void audio_asrc_reset(struct audio_asrc *ctx);

#endif /* AUDIO_ASRC_H */
