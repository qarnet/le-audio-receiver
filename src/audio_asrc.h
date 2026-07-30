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

/** Q32.32 one (2³²).  Phase identity boundary; phase > Q32_ONE is invalid. */
#define ASRC_Q32_ONE (1ULL << 32)

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

/* ── Generic ASRC state (cross-core wire format) ───────────────────
 *
 * Self-contained snapshot of ASRC continuity state.  Size exactly 24
 * bytes, fixed layout, no pointer/float/bool/enum/size_t fields.
 * Both cores little-endian — no byteswap needed.
 *
 * Integration contract:
 *   - CPUAPP (fallback / reference) exports state after each block.
 *   - FLPR imports state at the start of each request and returns
 *     updated state after processing — FLPR never "owns" continuity.
 *   - If FLPR is unavailable, cpuapp continues from its last exported
 *     state without reset or synchronization protocol.
 */

struct audio_asrc_state {
	uint64_t phase;      /* Q32.32 extended-sequence phase */
	uint64_t step_base;  /* Q32.32 nominal step at input_rate/output_rate */
	int16_t prev_l;      /* previous block's last L sample */
	int16_t prev_r;      /* previous block's last R sample */
	uint8_t prev_valid;  /* 1 after at least one block processed, else 0 */
	uint8_t reserved[3]; /* must be zero */
};

/* Compile-time size check. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(struct audio_asrc_state) == 24, "state must be 24 bytes");
#endif

/**
 * @brief Export ASRC continuity state to a wire-safe fixed-layout buffer.
 *
 * Zeroes reserved bytes.  Caller supplies a pre-zeroed or uninitialized
 * dst buffer.
 *
 * @param ctx     Live ASRC context.
 * @param prev_l  Previous-frame L sample (carried from last process call).
 * @param prev_r  Previous-frame R sample.
 * @param prev_valid  Whether prev_l/prev_r are valid.
 * @param dst     Destination state buffer (24 bytes).
 */
void audio_asrc_state_export(const struct audio_asrc *ctx, int16_t prev_l, int16_t prev_r,
			     bool prev_valid, struct audio_asrc_state *dst);

/**
 * @brief Import ASRC continuity state from a wire-safe buffer.
 *
 * Transactional: validates all fields before writing anything; on rejection
 * the context and prev outputs are unchanged.
 *
 * Rejection rules:
 *   - null ctx or src            → -EINVAL
 *   - step_base == 0             → -EINVAL (uninitialised state)
 *   - prev_valid > 1             → -EINVAL
 *   - any reserved byte non-zero → -EINVAL
 *
 * @param ctx          ASRC context to overwrite.
 * @param src          Source state buffer (24 bytes).
 * @param prev_l_out   [out] Set to src->prev_l.
 * @param prev_r_out   [out] Set to src->prev_r.
 * @param prev_valid_out [out] Set to src->prev_valid != 0.
 *
 * @retval  0   Success — ctx and prev outputs updated.
 * @retval  -EINVAL  Rejected — nothing written.
 */
int audio_asrc_state_import(struct audio_asrc *ctx, const struct audio_asrc_state *src,
			    int16_t *prev_l_out, int16_t *prev_r_out, bool *prev_valid_out);

#endif /* AUDIO_ASRC_H */
