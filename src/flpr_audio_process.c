/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure FLPR audio processing: identity pass-through or ASRC.
 *
 * Zero hidden state.  All continuity lives in struct audio_asrc_state
 * which is supplied with every call and returned updated.
 *
 * No Zephyr deps beyond what flpr_ring.h and audio_asrc.h pull in.
 * Built by FLPR firmware AND native_sim unit tests.
 */

#include "flpr_audio_process.h"
#include "flpr_ring.h"
#include "audio_asrc.h"
#include <string.h>

/* ── Input validation ────────────────────────────────────────────── */

static int validate_input(const struct flpr_ring_slot_meta *meta, const uint8_t *payload,
			  size_t payload_bytes)
{
	if (!meta || !payload) {
		return FLPR_AUDIO_ERR_NULL_PTR;
	}

	uint16_t flags = meta->flags;
	uint16_t valid_frames = meta->valid_frames;

	/* Valid flag must be set. */
	if (!(flags & FLPR_SLOT_FLAG_VALID)) {
		return FLPR_AUDIO_ERR_BAD_FLAGS;
	}

	/* ASRC flag is optional; no other flags should be set. */
	uint16_t known_flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_CRC_OK | FLPR_SLOT_FLAG_STALL |
			       FLPR_SLOT_FLAG_ASRC_LINEAR;
	if (flags & ~known_flags) {
		return FLPR_AUDIO_ERR_BAD_FLAGS;
	}

	/* valid_frames must be nonzero (zero only for error output). */
	if (valid_frames == 0) {
		return FLPR_AUDIO_ERR_BAD_FRAMES;
	}
	if (valid_frames > FLPR_RING_PAYLOAD_CAPACITY_FRAMES) {
		return FLPR_AUDIO_ERR_BAD_FRAMES;
	}

	/* Payload buffer must be large enough. */
	size_t need = (size_t)valid_frames * 4U;
	if (payload_bytes < need) {
		return FLPR_AUDIO_ERR_BAD_FRAMES;
	}

	/* PPM range check (ASRC mode only; identity ignores). */
	if (flags & FLPR_SLOT_FLAG_ASRC_LINEAR) {
		if (meta->correction_ppm < -ASRC_MAX_PPM || meta->correction_ppm > ASRC_MAX_PPM) {
			return FLPR_AUDIO_ERR_BAD_PPM;
		}
	}

	return 0;
}

/* ── Identity pass-through ───────────────────────────────────────── */
/* Bit-exact copy: input payload → output payload, metadata mirrored. */

static int process_identity(const struct flpr_ring_slot_meta *input_meta,
			    const uint8_t *input_payload, size_t input_payload_bytes,
			    struct flpr_ring_slot_meta *output_meta, uint8_t *output_payload,
			    size_t output_payload_bytes)
{
	uint16_t valid_frames = input_meta->valid_frames;
	size_t copy_bytes = (size_t)valid_frames * 4U;

	if (output_payload_bytes < copy_bytes) {
		return FLPR_AUDIO_ERR_CAPACITY;
	}

	/* Copy metadata fields (v4-safe: zero extended fields). */
	memset(output_meta, 0, sizeof(*output_meta));
	output_meta->sequence = input_meta->sequence;
	output_meta->epoch = input_meta->epoch;
	output_meta->valid_frames = valid_frames;
	output_meta->flags = FLPR_SLOT_FLAG_VALID;
	output_meta->correction_ppm = input_meta->correction_ppm;
	output_meta->crc32 = input_meta->crc32;
	output_meta->cpu_timestamp = input_meta->cpu_timestamp;
	/* asrc_raw, processing_cycles, processing_status already zeroed. */

	/* Bit-exact payload copy. */
	memcpy(output_payload, input_payload, copy_bytes);

	/* Zero remainder. */
	if (output_payload_bytes > copy_bytes) {
		memset(output_payload + copy_bytes, 0, output_payload_bytes - copy_bytes);
	}

	return FLPR_AUDIO_OK;
}

/* ── ASRC processing ─────────────────────────────────────────────── */

static int process_asrc(const struct flpr_ring_slot_meta *input_meta, const uint8_t *input_payload,
			size_t input_payload_bytes, struct flpr_ring_slot_meta *output_meta,
			uint8_t *output_payload, size_t output_payload_bytes)
{
	(void)input_payload_bytes;

	/* Require exactly 480 input frames for ASRC. */
	if (input_meta->valid_frames != FLPR_RING_PAYLOAD_MAX_INPUT) {
		return FLPR_AUDIO_ERR_BAD_FRAMES;
	}

	/* Capacity: need at least 481 output frames (1924 bytes). */
	if (output_payload_bytes < FLPR_RING_PAYLOAD_CAPACITY_BYTES) {
		return FLPR_AUDIO_ERR_CAPACITY;
	}

	/* Import ASRC state from input metadata. */
	struct audio_asrc ctx;
	int16_t prev_l, prev_r;
	bool prev_valid;
	int ret;

	ret = audio_asrc_state_import(&ctx, (const struct audio_asrc_state *)input_meta->asrc_raw,
				      &prev_l, &prev_r, &prev_valid);
	if (ret != 0) {
		return FLPR_AUDIO_ERR_STATE;
	}

	/* Call the accepted ASRC processor. */
	const int16_t *input_s16 = (const int16_t *)input_payload;
	int16_t *output_s16 = (int16_t *)output_payload;
	size_t input_consumed;
	size_t output_produced;
	int16_t next_prev_l, next_prev_r;

	ret = audio_asrc_process(&ctx, input_s16, (size_t)FLPR_RING_PAYLOAD_MAX_INPUT, output_s16,
				 (size_t)FLPR_RING_PAYLOAD_CAPACITY_FRAMES,
				 input_meta->correction_ppm, prev_l, prev_r, prev_valid,
				 &input_consumed, &output_produced, &next_prev_l, &next_prev_r);

	/* Must consume exactly 480 input frames. */
	if (input_consumed != FLPR_RING_PAYLOAD_MAX_INPUT) {
		return FLPR_AUDIO_ERR_PROD_RANGE;
	}

	/* Must produce at least 1, at most 481 output frames. */
	if (output_produced == 0 || output_produced > FLPR_RING_PAYLOAD_CAPACITY_FRAMES) {
		return FLPR_AUDIO_ERR_PROD_RANGE;
	}

	/* Fill output metadata. */
	memset(output_meta, 0, sizeof(*output_meta));
	output_meta->sequence = input_meta->sequence;
	output_meta->epoch = input_meta->epoch;
	output_meta->valid_frames = (uint16_t)output_produced;
	output_meta->flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
	output_meta->correction_ppm = input_meta->correction_ppm;
	output_meta->cpu_timestamp = input_meta->cpu_timestamp;

	/* CRC over produced bytes ONLY. */
	size_t produced_bytes = output_produced * 4U;
	output_meta->crc32 = flpr_ring_crc32(output_payload, produced_bytes);

	/* Status zero = success. */
	output_meta->processing_status = 0;

	/* Export post-process state (MUST be AFTER memset). */
	audio_asrc_state_export(&ctx, next_prev_l, next_prev_r,
				true, /* always valid after first block */
				(struct audio_asrc_state *)output_meta->asrc_raw);

	/* Zero remainder of payload. */
	if (output_payload_bytes > produced_bytes) {
		memset(output_payload + produced_bytes, 0, output_payload_bytes - produced_bytes);
	}

	return FLPR_AUDIO_OK;
}

/* ── Public entry point ──────────────────────────────────────────── */

int flpr_audio_process(const struct flpr_ring_slot_meta *input_meta, const uint8_t *input_payload,
		       size_t input_payload_bytes, struct flpr_ring_slot_meta *output_meta,
		       uint8_t *output_payload, size_t output_payload_bytes)
{
	int ret;

	if (!output_meta || !output_payload) {
		return FLPR_AUDIO_ERR_NULL_PTR;
	}

	/* Zero output metadata before filling (safety). */
	memset(output_meta, 0, sizeof(*output_meta));

	/* Validate input. */
	ret = validate_input(input_meta, input_payload, input_payload_bytes);
	if (ret != 0) {
		return ret;
	}

	/* Dispatch by flag. */
	if (input_meta->flags & FLPR_SLOT_FLAG_ASRC_LINEAR) {
		return process_asrc(input_meta, input_payload, input_payload_bytes, output_meta,
				    output_payload, output_payload_bytes);
	}

	return process_identity(input_meta, input_payload, input_payload_bytes, output_meta,
				output_payload, output_payload_bytes);
}
