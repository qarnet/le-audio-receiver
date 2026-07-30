/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure FLPR audio processing module — identity pass-through or ASRC.
 *
 * Zero hidden state.  All continuity lives in struct audio_asrc_state
 * which is supplied with every call and returned updated.  No heap,
 * float, atomics, static context, or GRTC access.
 *
 * Built by FLPR firmware AND native unit tests; must not pull in Zephyr
 * kernel deps beyond what flpr_ring.h already requires (which is none).
 */

#ifndef FLPR_AUDIO_PROCESS_H
#define FLPR_AUDIO_PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Return codes ────────────────────────────────────────────────── */

enum flpr_audio_process_result {
	FLPR_AUDIO_OK = 0,              /* processing succeeded */
	FLPR_AUDIO_ERR_NULL_PTR = -1,   /* null input/output pointer */
	FLPR_AUDIO_ERR_BAD_FLAGS = -2,  /* invalid/mixed flags */
	FLPR_AUDIO_ERR_BAD_FRAMES = -3, /* valid_frames > capacity */
	FLPR_AUDIO_ERR_BAD_PPM = -4,    /* correction_ppm out of range */
	FLPR_AUDIO_ERR_CAPACITY = -5,   /* output capacity too small */
	FLPR_AUDIO_ERR_PROD_RANGE = -6, /* produced frames out of expected range */
	FLPR_AUDIO_ERR_STATE = -7,      /* ASRC state import rejected */
	FLPR_AUDIO_ERR_BAD_CRC = -8,    /* input payload CRC mismatch */
};

/* ── Forward declarations ────────────────────────────────────────── */

struct flpr_ring_slot_meta;
struct audio_asrc_state;

/* ── Processing entry point ─────────────────────────────────────────
 *
 * Accepts input metadata + payload, output buffers + metadata capacity.
 * Writes result into output metadata + payload.  Never partially publishes
 * output: on error, output is untouched (zero-initialised by caller).
 *
 * Identity mode (no FLPR_SLOT_FLAG_ASRC_LINEAR):
 *   - Copies input payload to output bit-exact.
 *   - Copies metadata fields (sequence, epoch, correction_ppm, crc32).
 *   - Sets valid_frames = input valid_frames.
 *   - Zeroes processing_status, asrc_state.
 *
 * ASRC mode (FLPR_SLOT_FLAG_ASRC_LINEAR):
 *   - Requires input valid_frames == 480.
 *   - Imports ASRC state from input metadata asrc_state field.
 *   - Calls audio_asrc_process() with correction_ppm and 481-frame capacity.
 *   - Requires consumed == 480, produced in [1, 481].
 *   - Exports post-process state into output metadata asrc_state field.
 *   - Sets output valid_frames = produced.
 *   - Computes CRC-32 over produced output bytes.
 *   - Sets FLPR_SLOT_FLAG_ASRC_LINEAR flag on output.
 *
 * @param input_meta      Input slot metadata (ring-validated).
 * @param input_payload   Input PCM data (valid_frames × 4 bytes).
 * @param input_payload_bytes  Total capacity of input payload buffer.
 * @param output_meta     Output slot metadata to fill.
 * @param output_payload  Output PCM buffer to fill.
 * @param output_payload_bytes  Total capacity of output payload buffer.
 *
 * @retval FLPR_AUDIO_OK  Processing succeeded.
 * @retval <0             Error code (enum flpr_audio_process_result).
 */
int flpr_audio_process(const struct flpr_ring_slot_meta *input_meta, const uint8_t *input_payload,
		       size_t input_payload_bytes, struct flpr_ring_slot_meta *output_meta,
		       uint8_t *output_payload, size_t output_payload_bytes);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_AUDIO_PROCESS_H */
