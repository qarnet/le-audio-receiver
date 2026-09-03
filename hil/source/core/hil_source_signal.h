/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic source signal generator and LC3 frame packing (RH1A).
 *
 * One independent `struct hil_source_signal_channel` per semantic
 * channel.  No heap, no floating-point runtime math, no frame-boundary
 * phase reset.  Phases, PRNG/envelope counters, and LC3 encoder state
 * are only advanced on success; caller-controlled failures (bad stage,
 * overrun, null pointers, insufficient capacity) leave everything
 * unchanged.  An unexpected liblc3 failure cannot be rolled back, so the
 * encoder context is marked unusable and later calls fail fast.
 */

#ifndef HIL_SOURCE_SIGNAL_H
#define HIL_SOURCE_SIGNAL_H

#include <stdint.h>

#include <lc3.h>

#include "hil_source_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Quarter-wave Q15 sine lookup, mirrored into 1024 phase indices.
 * `phase_index` is taken modulo 1024.  lut[0] == 0, lut[256] == 32767. */
int16_t hil_source_sine_q15(uint32_t phase_index);

/* Profile helpers; 0 on an invalid profile (or invalid mode for the SDU
 * size). */
uint16_t hil_source_profile_frame_samples(enum hil_source_profile profile);
uint16_t hil_source_profile_frame_duration_us(enum hil_source_profile profile);
uint16_t hil_source_profile_octets_per_channel(enum hil_source_profile profile);
uint16_t hil_source_profile_sdu_size(enum hil_source_profile profile, enum hil_source_mode mode);

/* One independent signal channel.  All fields are public contract: the
 * phase accumulator (Q0.32), the scored-stage PRNG/envelope state, and
 * the per-channel preamble sample counter. */
struct hil_source_signal_channel {
	enum hil_source_semantic_channel semantic;
	uint32_t phase;             /* Q0.32 phase accumulator */
	uint32_t prng;              /* xorshift32 state (scored stage) */
	uint32_t block_index;       /* scored 5760-sample block counter */
	uint32_t block_pos;         /* sample offset inside the current block */
	uint32_t ramp_index;        /* 0..239 inside the ramp, 240 = hold */
	int16_t ramp_old;           /* amplitude at ramp start */
	int16_t ramp_next;          /* amplitude target for the current block */
	uint32_t preamble_rendered; /* samples rendered in preamble stage */
	bool scored_entered;        /* scored-stage entry init done */
};

/* Encoder: profile plus one or two semantic channel selections.  mono and
 * each Mode A stream use one channel; Mode B uses left+right and packs
 * [L frame][R frame] into one SDU. */
struct hil_source_signal_encoder {
	enum hil_source_mode mode;
	enum hil_source_profile profile;
	uint32_t signal_seed;
	enum hil_source_signal_stage stage;
	uint8_t channel_count;
	struct hil_source_signal_channel channels[2];
	lc3_encoder_t encoder[2];
	lc3_encoder_mem_48k_t encoder_mem[2];
	bool unusable; /* set after an unexpected liblc3 failure */
};

/* Initialize an encoder.  `ch1` selects the second semantic channel or is
 * -1 for a single-channel instance:
 *   mono  -> ch0 == LEFT,  ch1 == -1
 *   mode_a-> ch0 == LEFT or RIGHT, ch1 == -1
 *   mode_b-> ch0 == LEFT,  ch1 == RIGHT
 * Returns 0, -EINVAL for bad arguments/selections, or -EIO if liblc3
 * encoder setup fails (encoder then unusable). */
int hil_source_signal_encoder_init(struct hil_source_signal_encoder *enc, enum hil_source_mode mode,
				   enum hil_source_profile profile, uint32_t signal_seed,
				   enum hil_source_semantic_channel ch0, int ch1);

/* Stage transitions only allow preamble -> scored -> tail.  Entering
 * scored succeeds only when every configured semantic channel has
 * rendered exactly HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES; it derives each
 * channel PRNG from the configured seed, applies one xorshift32 step,
 * and picks the first block target from the updated LSB.  Every later
 * 5760-sample block boundary applies one further update.  Returns 0 or
 * -EINVAL without state change for any rejected transition. */
int hil_source_signal_set_stage(struct hil_source_signal_encoder *enc,
				enum hil_source_signal_stage stage);

/* Render `samples` PCM samples for one channel at the current stage
 * position (bounded by `cap` bytes).  The phase advances once per sample
 * in every stage, including silence and tail.  Returns the sample count,
 * or -EINVAL (bad args), -ENOSPC (capacity), -EOVERFLOW (preamble past
 * HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES) with all state unchanged. */
int hil_source_signal_render(struct hil_source_signal_encoder *enc, uint32_t channel_index,
			     int16_t *out, uint32_t samples, size_t cap);

/* Encode one frame's SDUs: checks stage, output capacity, and expected
 * frame size before generating PCM.  Returns the exact SDU length on
 * success, or -EINVAL/-ENOSPC/-EOVERFLOW with signal and encoder state
 * unchanged; an unexpected liblc3 failure returns -EIO and marks the
 * encoder unusable. */
int hil_source_signal_encode_next(struct hil_source_signal_encoder *enc, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_SIGNAL_H */
