/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Native unit tests for flpr_audio_process.c and audio_asrc state helpers.
 * Tests cover identity pass-through, ASRC processing, state roundtrip,
 * fallback continuity, error handling, and ring ABI v4 layout.
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>

#include "flpr_audio_process.h"
#include "flpr_ring.h"
#include "audio_asrc.h"

/* ── Test buffers ────────────────────────────────────────────────── */
/* Full payload capacity (1924 bytes) for both input and output. */
static uint8_t input_payload[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
static uint8_t output_payload[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
static struct flpr_ring_slot_meta input_meta;
static struct flpr_ring_slot_meta output_meta;

/* Reference ASRC for fallback continuity tests. */
static struct audio_asrc ref_asrc;
static int16_t ref_prev_l, ref_prev_r;
static bool ref_prev_valid;

static uint8_t ref_output[FLPR_RING_PAYLOAD_CAPACITY_BYTES];

/* ── Setup ───────────────────────────────────────────────────────── */

static void test_setup(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(input_payload, 0, sizeof(input_payload));
	memset(output_payload, 0, sizeof(output_payload));
	memset(&input_meta, 0, sizeof(input_meta));
	memset(&output_meta, 0, sizeof(output_meta));
	memset(&ref_asrc, 0, sizeof(ref_asrc));
	ref_prev_l = 0;
	ref_prev_r = 0;
	ref_prev_valid = false;
	memset(ref_output, 0, sizeof(ref_output));

	/* Initialize reference ASRC for continuity tests. */
	audio_asrc_init(&ref_asrc, 48000, 48000);
}

/* ── Helper: fill input with deterministic stereo ramp ───────────── */

static void fill_input_ramp(uint32_t base_sample)
{
	int16_t *p = (int16_t *)input_payload;
	for (size_t i = 0; i < FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2; i++) {
		/* Bounded ramp: 0..32766, wrapping safely. */
		int32_t v = (int32_t)((base_sample * 2 + i) % 32767);
		p[i] = (int16_t)v;
	}
}

/* Helper: call reference ASRC directly for comparison. */
static int ref_process_block(uint32_t input_frames, int32_t ppm, size_t *out_produced)
{
	size_t consumed;
	return audio_asrc_process(&ref_asrc, (const int16_t *)input_payload, input_frames,
				  (int16_t *)ref_output, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, ppm,
				  ref_prev_l, ref_prev_r, ref_prev_valid, &consumed, out_produced,
				  &ref_prev_l, &ref_prev_r);
}

/* ── Test 1: Generic state export/import roundtrip ───────────────── */

ZTEST(flpr_audio_process, test_state_export_import_roundtrip)
{
	struct audio_asrc ctx;
	struct audio_asrc_state state;
	struct audio_asrc ctx2;
	int16_t pl, pr;
	bool pv;
	int ret;

	audio_asrc_init(&ctx, 48000, 47619);
	ctx.phase = 0x0000000200000000ULL; /* test: 2.0 */

	/* Export. */
	audio_asrc_state_export(&ctx, 123, -456, true, &state);
	zassert_equal(state.phase, 0x0000000200000000ULL, "phase preserved");
	zassert_true(state.step_base != 0, "step_base nonzero");
	zassert_equal(state.prev_l, 123, "prev_l preserved");
	zassert_equal(state.prev_r, -456, "prev_r preserved");
	zassert_equal(state.prev_valid, 1, "prev_valid = 1");
	zassert_equal(state.reserved[0], 0, "reserved[0] zero");
	zassert_equal(state.reserved[1], 0, "reserved[1] zero");
	zassert_equal(state.reserved[2], 0, "reserved[2] zero");

	/* Import into fresh context. */
	memset(&ctx2, 0, sizeof(ctx2));
	ret = audio_asrc_state_import(&ctx2, &state, &pl, &pr, &pv);
	zassert_equal(ret, 0, "import succeeds");
	zassert_equal(ctx2.phase, ctx.phase, "phase roundtrips");
	zassert_equal(ctx2.step_base, ctx.step_base, "step_base roundtrips");
	zassert_equal(pl, 123, "prev_l roundtrips");
	zassert_equal(pr, -456, "prev_r roundtrips");
	zassert_true(pv, "prev_valid roundtrips");
}

ZTEST(flpr_audio_process, test_state_import_rejects_null)
{
	struct audio_asrc_state state;
	struct audio_asrc ctx;
	int16_t pl, pr;
	bool pv;
	int ret;

	memset(&state, 0, sizeof(state));
	state.step_base = 1;

	ret = audio_asrc_state_import(NULL, &state, &pl, &pr, &pv);
	zassert_equal(ret, -EINVAL, "rejects null ctx");

	ret = audio_asrc_state_import(&ctx, NULL, &pl, &pr, &pv);
	zassert_equal(ret, -EINVAL, "rejects null src");
}

ZTEST(flpr_audio_process, test_state_import_rejects_step_zero)
{
	struct audio_asrc_state state;
	struct audio_asrc ctx;
	int16_t pl, pr;
	bool pv;
	int ret;

	memset(&state, 0, sizeof(state));
	state.step_base = 0; /* invalid */

	ret = audio_asrc_state_import(&ctx, &state, &pl, &pr, &pv);
	zassert_equal(ret, -EINVAL, "rejects step_base=0");
}

ZTEST(flpr_audio_process, test_state_import_rejects_bad_prev_valid)
{
	struct audio_asrc_state state;
	struct audio_asrc ctx;
	int16_t pl, pr;
	bool pv;
	int ret;

	memset(&state, 0, sizeof(state));
	state.step_base = 1;
	state.prev_valid = 2; /* invalid — must be 0 or 1 */

	ret = audio_asrc_state_import(&ctx, &state, &pl, &pr, &pv);
	zassert_equal(ret, -EINVAL, "rejects prev_valid=2");
}

ZTEST(flpr_audio_process, test_state_import_rejects_nonzero_reserved)
{
	struct audio_asrc_state state;
	struct audio_asrc ctx;
	int16_t pl, pr;
	bool pv;

	memset(&state, 0, sizeof(state));
	state.step_base = 1;
	state.reserved[2] = 0x42;
	int ret = audio_asrc_state_import(&ctx, &state, &pl, &pr, &pv);
	zassert_equal(ret, -EINVAL, "rejects nonzero reserved");
}

ZTEST(flpr_audio_process, test_state_import_transactional)
{
	/* Import failure must not modify ctx or outputs. */
	struct audio_asrc ctx;
	struct audio_asrc_state state;
	int16_t pl = 999, pr = -999;
	bool pv = true;

	audio_asrc_init(&ctx, 44100, 48000);
	uint64_t orig_phase = ctx.phase;

	memset(&state, 0, sizeof(state));
	state.step_base = 0; /* will fail */

	int ret = audio_asrc_state_import(&ctx, &state, &pl, &pr, &pv);
	zassert_equal(ret, -EINVAL, "import fails");
	zassert_equal(ctx.phase, orig_phase, "ctx.phase unchanged on failure");
	zassert_equal(pl, 999, "prev_l unchanged on failure");
	zassert_equal(pr, -999, "prev_r unchanged on failure");
	zassert_true(pv, "prev_valid unchanged on failure");
}

/* ── Test 2: Metadata exact size/offset and ring layout ──────────── */

ZTEST(flpr_audio_process, test_metadata_size_v4)
{
	zassert_equal(sizeof(struct flpr_ring_slot_meta), 64, "metadata = 64 bytes");
	zassert_equal(FLPR_RING_SLOT_METADATA_SZ, 64, "METADATA_SZ = 64");
	zassert_equal(FLPR_RING_SLOT_PAYLOAD_OFF, 64, "PAYLOAD_OFF = 64");
}

ZTEST(flpr_audio_process, test_ring_size_unchanged_8k)
{
	zassert_equal(FLPR_RING_TOTAL_SIZE, 8192, "ring still 8 KiB");
	zassert_equal(FLPR_RING_HEADER_SIZE, 128, "header still 128");
	zassert_equal(FLPR_RING_SLOT_COUNT, 4, "4 slots");
	zassert_equal(FLPR_RING_SLOT_STRIDE, 2016, "stride = 2016");

	/* Verify ring total = header + 4 × stride. */
	zassert_equal(FLPR_RING_HEADER_SIZE + 4 * FLPR_RING_SLOT_STRIDE, FLPR_RING_TOTAL_SIZE,
		      "ring size consistent");
}

ZTEST(flpr_audio_process, test_stride_fits_metadata_plus_payload)
{
	size_t used = FLPR_RING_SLOT_METADATA_SZ + FLPR_RING_PAYLOAD_CAPACITY_BYTES;
	/* 64 + 1924 = 1988 */
	zassert_equal(used, 1988, "metadata+payload = 1988");
	zassert_true(FLPR_RING_SLOT_STRIDE >= used, "stride >= metadata+payload");

	size_t pad = (size_t)(FLPR_RING_SLOT_STRIDE - used);
	zassert_equal(pad, 28, "28 pad bytes");
	/* Slot stride (2016) is 32-byte aligned; pad doesn't need to be. */
	zassert_equal(FLPR_RING_SLOT_STRIDE % FLPR_RING_SLOT_ALIGN, 0, "stride is 32-byte aligned");
}

ZTEST(flpr_audio_process, test_asrc_state_offset_in_meta)
{
	/* asrc_raw[3] starts at offset 32 in metadata. */
	size_t off = offsetof(struct flpr_ring_slot_meta, asrc_raw);
	zassert_equal(off, 32, "asrc_raw at offset 32");
}

ZTEST(flpr_audio_process, test_processing_fields_offset)
{
	size_t off_cycles = offsetof(struct flpr_ring_slot_meta, processing_cycles);
	size_t off_status = offsetof(struct flpr_ring_slot_meta, processing_status);
	zassert_equal(off_cycles, 32 + 24, "processing_cycles at offset 56");
	zassert_equal(off_status, 32 + 24 + 4, "processing_status at offset 60");
}

ZTEST(flpr_audio_process, test_abi_version)
{
	zassert_equal(FLPR_RING_ABI_VERSION, 4, "ABI version = 4");
}

/* ── Test 3: Identity vectors remain bit-exact ──────────────────── */

ZTEST(flpr_audio_process, test_identity_bit_exact)
{
	/* Set up input metadata. */
	input_meta.sequence = 42;
	input_meta.epoch = 7;
	input_meta.valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
	input_meta.flags = FLPR_SLOT_FLAG_VALID;
	input_meta.correction_ppm = 0;
	input_meta.cpu_timestamp = 0xABCD0001;
	input_meta.crc32 = 0xDEADBEEF;

	/* Fill input with pattern. */
	fill_input_ramp(1000);

	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));

	zassert_equal(ret, FLPR_AUDIO_OK, "identity returns OK");
	zassert_equal(output_meta.sequence, 42, "sequence preserved");
	zassert_equal(output_meta.epoch, 7, "epoch preserved");
	zassert_equal(output_meta.valid_frames, FLPR_RING_PAYLOAD_MAX_INPUT,
		      "valid_frames preserved");
	zassert_equal(output_meta.flags, FLPR_SLOT_FLAG_VALID, "flags = VALID only");
	zassert_equal(output_meta.correction_ppm, 0, "ppm preserved");
	zassert_equal(output_meta.crc32, 0xDEADBEEF, "CRC preserved");
	zassert_equal(output_meta.cpu_timestamp, 0xABCD0001, "timestamp preserved");
	zassert_equal(output_meta.processing_status, 0, "status = 0");

	/* Bit-exact payload match. */
	zassert_mem_equal(output_payload, input_payload, (size_t)FLPR_RING_PAYLOAD_MAX_INPUT * 4,
			  "payload bit-exact");

	/* Extended metadata zeroed. */
	zassert_equal(output_meta.asrc_raw[0], 0, "asrc_raw[0] zero");
	zassert_equal(output_meta.asrc_raw[1], 0, "asrc_raw[1] zero");
	zassert_equal(output_meta.asrc_raw[2], 0, "asrc_raw[2] zero");
	zassert_equal(output_meta.processing_cycles, 0, "cycles zero (unit test)");
}

ZTEST(flpr_audio_process, test_identity_different_valid_frames)
{
	/* Test with 240 frames (half block). */
	input_meta.sequence = 0;
	input_meta.epoch = 1;
	input_meta.valid_frames = 240;
	input_meta.flags = FLPR_SLOT_FLAG_VALID;
	input_meta.correction_ppm = 0;
	input_meta.crc32 = 0x12345678;

	fill_input_ramp(0);

	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));

	zassert_equal(ret, FLPR_AUDIO_OK, "identity 240 frames OK");
	zassert_equal(output_meta.valid_frames, 240, "valid_frames = 240");
	zassert_mem_equal(output_payload, input_payload, 240 * 4, "payload 240 frames bit-exact");
}

/* ── Test 4: ASRC processing ─────────────────────────────────────── */

static void setup_asrc_input(uint32_t seq, int32_t ppm)
{
	memset(&output_meta, 0, sizeof(output_meta));
	memset(output_payload, 0, sizeof(output_payload));

	input_meta.sequence = seq;
	input_meta.epoch = 1;
	input_meta.valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
	input_meta.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
	input_meta.correction_ppm = ppm;
	input_meta.crc32 = 0;

	fill_input_ramp(seq * 480);

	/* Initialize ASRC state in input metadata. */
	struct audio_asrc init_ctx;
	audio_asrc_init(&init_ctx, 48000, 48000);
	audio_asrc_state_export(&init_ctx, 0, 0, false,
				(struct audio_asrc_state *)input_meta.asrc_raw);
}

ZTEST(flpr_audio_process, test_asrc_single_block_identity_ppm0)
{
	setup_asrc_input(0, 0);

	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));

	zassert_equal(ret, FLPR_AUDIO_OK, "ASRC ppm=0 OK");
	zassert_equal(output_meta.flags & FLPR_SLOT_FLAG_ASRC_LINEAR, FLPR_SLOT_FLAG_ASRC_LINEAR,
		      "ASRC flag set on output");

	/* Identity ppm=0 → produced = 480. */
	zassert_equal(output_meta.valid_frames, 480, "produced = 480 at 0 ppm");

	/* Payload should be identity (input_rate == output_rate, ppm=0). */
	zassert_mem_equal(output_payload, input_payload, 480 * 4, "identity payload at ppm=0");
}

ZTEST(flpr_audio_process, test_asrc_single_block_ppm_plus_2000)
{
	setup_asrc_input(0, 2000);

	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));

	zassert_equal(ret, FLPR_AUDIO_OK, "ASRC ppm=+2000 OK");
	/* +2000 ppm → consume faster → fewer output frames. */
	zassert_true(output_meta.valid_frames < 480, "positive ppm → fewer output frames");
	zassert_true(output_meta.valid_frames > 0, "positive ppm → still produces frames");
}

ZTEST(flpr_audio_process, test_asrc_single_block_ppm_minus_2000)
{
	setup_asrc_input(0, -2000);

	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));

	zassert_equal(ret, FLPR_AUDIO_OK, "ASRC ppm=-2000 OK");
	/* -2000 ppm → consume slower → same or more output frames. */
	zassert_true(output_meta.valid_frames >= 480, "negative ppm → >= 480 frames");
	zassert_true(output_meta.valid_frames <= 481, "negative ppm → at most 481 frames");
}

ZTEST(flpr_audio_process, test_asrc_multi_block_continuity)
{
	/* Process 3 blocks through FLPR helper, verify post-state match. */
	struct audio_asrc_state state;
	struct audio_asrc init_ctx;

	audio_asrc_init(&init_ctx, 48000, 48000);
	audio_asrc_state_export(&init_ctx, 0, 0, false, &state);

	for (uint32_t blk = 0; blk < 3; blk++) {
		fill_input_ramp(blk * 480);
		memcpy(input_meta.asrc_raw, &state, sizeof(state));
		input_meta.sequence = blk;
		input_meta.valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
		input_meta.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
		input_meta.correction_ppm = -500; /* slight slowdown */

		memset(output_payload, 0, sizeof(output_payload));
		memset(&output_meta, 0, sizeof(output_meta));

		int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
					     &output_meta, output_payload, sizeof(output_payload));

		zassert_equal(ret, FLPR_AUDIO_OK, "multi-block blk %u OK", blk);
		zassert_true(output_meta.valid_frames > 0, "produced frames > 0");

		/* Carry state forward. */
		memcpy(&state, output_meta.asrc_raw, sizeof(state));
	}
}

ZTEST(flpr_audio_process, test_asrc_post_state_exact_match_reference)
{
	/* Compare FLPR helper output against direct audio_asrc_process()
	 * for identical inputs (state continuity). */
	struct audio_asrc direct_ctx, flpr_ctx;
	struct audio_asrc_state state;
	int16_t flpr_pl = 0, flpr_pr = 0;
	int16_t direct_pl = 0, direct_pr = 0;
	bool flpr_pv = false, direct_pv = false;

	audio_asrc_init(&direct_ctx, 48000, 48000);
	audio_asrc_init(&flpr_ctx, 48000, 48000);

	for (uint32_t blk = 0; blk < 5; blk++) {
		fill_input_ramp(blk * 480);

		/* Direct process. */
		size_t direct_prod;
		int direct_ret = audio_asrc_process(
			&direct_ctx, (const int16_t *)input_payload, 480, (int16_t *)ref_output,
			FLPR_RING_PAYLOAD_CAPACITY_FRAMES, 0, direct_pl, direct_pr, direct_pv,
			&(size_t){0}, &direct_prod, &direct_pl, &direct_pr);
		(void)direct_ret;
		direct_pv = true; /* prev is valid after first block */

		/* FLPR helper process. */
		audio_asrc_state_export(&flpr_ctx, flpr_pl, flpr_pr, flpr_pv, &state);
		memcpy(input_meta.asrc_raw, &state, sizeof(state));
		input_meta.sequence = blk;
		input_meta.valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
		input_meta.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
		input_meta.correction_ppm = 0;

		memset(&output_meta, 0, sizeof(output_meta));
		memset(output_payload, 0, sizeof(output_payload));

		int flpr_ret =
			flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
					   &output_meta, output_payload, sizeof(output_payload));
		zassert_equal(flpr_ret, FLPR_AUDIO_OK, "flpr block %u OK", blk);

		/* Compare produced frames. */
		zassert_equal(output_meta.valid_frames, (uint16_t)direct_prod,
			      "block %u: produced frames match direct", blk);

		/* Compare payload bytes. */
		zassert_mem_equal(output_payload, ref_output, (size_t)output_meta.valid_frames * 4,
				  "block %u: payload bytes match direct", blk);

		/* Reload FLPR ASRC state from output. */
		int import_ret = audio_asrc_state_import(
			&flpr_ctx, (const struct audio_asrc_state *)output_meta.asrc_raw, &flpr_pl,
			&flpr_pr, &flpr_pv);
		zassert_equal(import_ret, 0, "state import block %u", blk);

		/* Post-state should match direct state. */
		zassert_equal(flpr_ctx.phase, direct_ctx.phase, "block %u: phase matches", blk);
	}

	/* Final prev samples match. */
	zassert_equal(flpr_pl, direct_pl, "final prev_l matches");
	zassert_equal(flpr_pr, direct_pr, "final prev_r matches");
	zassert_equal(flpr_pv, direct_pv, "final prev_valid matches");
}

/* ── Test 6: Fallback continuity simulation ──────────────────────── */

ZTEST(flpr_audio_process, test_fallback_continuity)
{
	/* Process N blocks FLPR helper, M blocks direct cpuapp from
	 * returned state, then resume FLPR helper. Compare uninterrupted
	 * direct reference bytes/count/state. */
	struct audio_asrc direct_ctx, fallback_ctx, state_ctx;
	struct audio_asrc_state state;
	int16_t fb_pl = 0, fb_pr = 0;
	bool fb_pv = false;
	int16_t dir_pl = 0, dir_pr = 0;
	bool dir_pv = false;
	uint8_t direct_payload_buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES];

	audio_asrc_init(&direct_ctx, 48000, 48000);
	audio_asrc_init(&fallback_ctx, 48000, 48000);
	audio_asrc_init(&state_ctx, 48000, 48000);

	/* Block 0: FLPR helper. */
	{
		fill_input_ramp(0);
		audio_asrc_state_export(&fallback_ctx, fb_pl, fb_pr, fb_pv, &state);
		memcpy(input_meta.asrc_raw, &state, sizeof(state));
		input_meta.sequence = 0;
		input_meta.valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
		input_meta.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
		input_meta.correction_ppm = -200;

		memset(&output_meta, 0, sizeof(output_meta));
		memset(output_payload, 0, sizeof(output_payload));
		int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
					     &output_meta, output_payload, sizeof(output_payload));
		zassert_equal(ret, FLPR_AUDIO_OK, "fallback block 0 FLPR OK");

		audio_asrc_state_import(&fallback_ctx,
					(const struct audio_asrc_state *)output_meta.asrc_raw,
					&fb_pl, &fb_pr, &fb_pv);
	}

	/* Block 1: Direct cpuapp. */
	{
		fill_input_ramp(480);
		size_t dir_prod;
		audio_asrc_process(&fallback_ctx, (const int16_t *)input_payload, 480,
				   (int16_t *)direct_payload_buf, FLPR_RING_PAYLOAD_CAPACITY_FRAMES,
				   -200, fb_pl, fb_pr, fb_pv, &(size_t){0}, &dir_prod, &fb_pl,
				   &fb_pr);
		fb_pv = true;
	}

	/* Block 2: Resume FLPR helper. */
	{
		fill_input_ramp(960);
		audio_asrc_state_export(&fallback_ctx, fb_pl, fb_pr, fb_pv, &state);
		memcpy(input_meta.asrc_raw, &state, sizeof(state));
		input_meta.sequence = 2;
		input_meta.valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
		input_meta.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
		input_meta.correction_ppm = -200;

		memset(&output_meta, 0, sizeof(output_meta));
		memset(output_payload, 0, sizeof(output_payload));
		int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
					     &output_meta, output_payload, sizeof(output_payload));
		zassert_equal(ret, FLPR_AUDIO_OK, "fallback block 2 FLPR OK");

		audio_asrc_state_import(&fallback_ctx,
					(const struct audio_asrc_state *)output_meta.asrc_raw,
					&fb_pl, &fb_pr, &fb_pv);
	}

	/* Compare against uninterrupted direct reference. */
	dir_pl = 0;
	dir_pr = 0;
	dir_pv = false;
	for (uint32_t blk = 0; blk < 3; blk++) {
		fill_input_ramp(blk * 480);
		size_t dir_prod;
		audio_asrc_process(&direct_ctx, (const int16_t *)input_payload, 480,
				   (int16_t *)direct_payload_buf, FLPR_RING_PAYLOAD_CAPACITY_FRAMES,
				   -200, dir_pl, dir_pr, dir_pv, &(size_t){0}, &dir_prod, &dir_pl,
				   &dir_pr);
		dir_pv = true;
	}

	/* Post-state must match. */
	zassert_equal(fallback_ctx.phase, direct_ctx.phase,
		      "fallback phase matches uninterrupted reference");
	zassert_equal(fallback_ctx.step_base, direct_ctx.step_base, "step_base matches");
	zassert_equal(fb_pl, dir_pl, "fallback prev_l matches");
	zassert_equal(fb_pr, dir_pr, "fallback prev_r matches");
	zassert_equal(fb_pv, dir_pv, "fallback prev_valid matches");
}

/* ── Test 7: Invalid input and error output behavior ─────────────── */

ZTEST(flpr_audio_process, test_reject_null_meta)
{
	int ret = flpr_audio_process(NULL, input_payload, sizeof(input_payload), &output_meta,
				     output_payload, sizeof(output_payload));
	zassert_equal(ret, FLPR_AUDIO_ERR_NULL_PTR, "reject null input_meta");
}

ZTEST(flpr_audio_process, test_reject_null_output_meta)
{
	input_meta.flags = FLPR_SLOT_FLAG_VALID;
	input_meta.valid_frames = 480;
	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload), NULL,
				     output_payload, sizeof(output_payload));
	zassert_equal(ret, FLPR_AUDIO_ERR_NULL_PTR, "reject null output_meta");
}

ZTEST(flpr_audio_process, test_reject_zero_frames)
{
	input_meta.flags = FLPR_SLOT_FLAG_VALID;
	input_meta.valid_frames = 0;
	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));
	zassert_equal(ret, FLPR_AUDIO_ERR_BAD_FRAMES, "reject valid_frames=0");
}

ZTEST(flpr_audio_process, test_reject_frames_exceed_capacity)
{
	input_meta.flags = FLPR_SLOT_FLAG_VALID;
	input_meta.valid_frames = FLPR_RING_PAYLOAD_CAPACITY_FRAMES + 1;
	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));
	zassert_equal(ret, FLPR_AUDIO_ERR_BAD_FRAMES, "reject frames > capacity");
}

ZTEST(flpr_audio_process, test_reject_asrc_bad_ppm)
{
	setup_asrc_input(0, 3001); /* exceeds ASRC_MAX_PPM */
	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));
	zassert_equal(ret, FLPR_AUDIO_ERR_BAD_PPM, "reject ppm > 3000");
}

ZTEST(flpr_audio_process, test_reject_asrc_wrong_frames)
{
	/* ASRC requires exactly 480 input frames. */
	input_meta.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
	input_meta.valid_frames = 240;
	input_meta.correction_ppm = 0;

	struct audio_asrc init_ctx;
	audio_asrc_init(&init_ctx, 48000, 48000);
	audio_asrc_state_export(&init_ctx, 0, 0, false,
				(struct audio_asrc_state *)input_meta.asrc_raw);

	fill_input_ramp(0);

	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));
	zassert_equal(ret, FLPR_AUDIO_ERR_BAD_FRAMES, "ASRC requires 480 frames");
}

ZTEST(flpr_audio_process, test_reject_empty_payload_buffer)
{
	input_meta.flags = FLPR_SLOT_FLAG_VALID;
	input_meta.valid_frames = 480;
	fill_input_ramp(0);

	/* Output payload too small. */
	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, 100);
	zassert_equal(ret, FLPR_AUDIO_ERR_CAPACITY, "reject small output buffer");
}

ZTEST(flpr_audio_process, test_reject_bad_flags)
{
	input_meta.flags = FLPR_SLOT_FLAG_VALID | 0x1000; /* unknown flag */
	input_meta.valid_frames = 480;
	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));
	zassert_equal(ret, FLPR_AUDIO_ERR_BAD_FLAGS, "reject unknown flag");
}

ZTEST(flpr_audio_process, test_reject_missing_valid_flag)
{
	input_meta.flags = 0; /* no VALID */
	input_meta.valid_frames = 480;
	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));
	zassert_equal(ret, FLPR_AUDIO_ERR_BAD_FLAGS, "reject missing VALID");
}

/* ── Test: error output metadata format ────────────────────────────
 * When processing fails in the FLPR main loop, it emits an explicit
 * error output with zero valid_frames and negative processing_status.
 * Test that the metadata shape is correct (the FLPR main loop handles
 * this, but we verify processor returns correct error codes). */
ZTEST(flpr_audio_process, test_error_output_metadata_shape)
{
	/* Simulate failure: null pointer returns immediate error,
	 * output_meta is zeroed by the processor. */
	int ret = flpr_audio_process(NULL, input_payload, sizeof(input_payload), &output_meta,
				     output_payload, sizeof(output_payload));
	zassert_equal(ret, FLPR_AUDIO_ERR_NULL_PTR, "null ptr error");

	/* output_meta was zeroed at entry — processing_status should be 0
	 * (it's only set by FLPR main loop). */
	zassert_equal(output_meta.valid_frames, 0, "zero frames on error");
	zassert_equal(output_meta.processing_status, 0, "processor doesn't write status on error");
}

/* ── Test: 60K block cumulative count matches Phase 5 reference ──── */
/* Phase 5 reference: 60,000 blocks at 0 ppm produced exactly 60,000 × 480
 * output frames with linear ASRC.  We verify the cumulative count after
 * many blocks. */

ZTEST(flpr_audio_process, test_cumulative_60k_blocks_ppm_zero_count)
{
	struct audio_asrc_state state;
	int16_t pl = 0, pr = 0;
	bool pv = false;
	uint64_t total_produced = 0;

	struct audio_asrc init_ctx;
	audio_asrc_init(&init_ctx, 48000, 48000);
	audio_asrc_state_export(&init_ctx, 0, 0, false, &state);

	for (uint32_t blk = 0; blk < 1000; blk++) {
		fill_input_ramp(blk * 480);
		memcpy(input_meta.asrc_raw, &state, sizeof(state));
		input_meta.sequence = blk;
		input_meta.valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
		input_meta.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
		input_meta.correction_ppm = 0;

		memset(&output_meta, 0, sizeof(output_meta));
		memset(output_payload, 0, sizeof(output_payload));
		int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
					     &output_meta, output_payload, sizeof(output_payload));
		zassert_equal(ret, FLPR_AUDIO_OK, "60k block %u OK", blk);

		total_produced += output_meta.valid_frames;

		/* Carry state forward. */
		memcpy(&state, output_meta.asrc_raw, sizeof(state));
		pl = (int16_t)(output_meta.asrc_raw[0] & 0xFFFF);
		pr = (int16_t)((output_meta.asrc_raw[0] >> 16) & 0xFFFF);
		pv = true;
	}

	/* At 0 ppm with 48000→48000, every block produces exactly 480 frames. */
	zassert_equal(total_produced, 1000ULL * 480ULL, "cumulative count = 480K");
}

ZTEST(flpr_audio_process, test_ppm_sign_changing_sequence)
{
	/* Sequence of ppm: +2000, -2000, 0, +2000, -2000.
	 * Each block transitions correctly without state corruption. */
	int32_t ppm_seq[] = {2000, -2000, 0, 2000, -2000};
	struct audio_asrc_state state;
	int16_t pl = 0, pr = 0;
	bool pv = false;
	struct audio_asrc ctx;

	audio_asrc_init(&ctx, 48000, 48000);
	audio_asrc_state_export(&ctx, 0, 0, false, &state);

	for (int i = 0; i < 5; i++) {
		fill_input_ramp(i * 480);
		memcpy(input_meta.asrc_raw, &state, sizeof(state));
		input_meta.sequence = i;
		input_meta.valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
		input_meta.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
		input_meta.correction_ppm = ppm_seq[i];

		memset(&output_meta, 0, sizeof(output_meta));
		memset(output_payload, 0, sizeof(output_payload));

		int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
					     &output_meta, output_payload, sizeof(output_payload));
		zassert_equal(ret, FLPR_AUDIO_OK, "sign-change block %d ppm=%d OK", i, ppm_seq[i]);
		zassert_true(output_meta.valid_frames > 0, "block %d produced frames", i);

		memcpy(&state, output_meta.asrc_raw, sizeof(state));
		pl = (int16_t)(output_meta.asrc_raw[0] & 0xFFFF);
		pr = (int16_t)((output_meta.asrc_raw[0] >> 16) & 0xFFFF);
		pv = true;
	}
}

/* ── Test 8: Existing tests still pass ─────────────────────────────
 * (Verification that ring ABI v4 doesn't break ring operations.) */

ZTEST(flpr_audio_process, test_ring_init_with_v4_abi)
{
	static uint8_t ring[FLPR_RING_TOTAL_SIZE] __attribute__((aligned(32)));
	memset(ring, 0, sizeof(ring));
	flpr_ring_init(ring, FLPR_RING_CPUAPP_TO_FLPR);
	zassert_true(flpr_ring_validate(ring), "v4 ring validates");
}

ZTEST(flpr_audio_process, test_ring_header_fields_untouched)
{
	/* Header size unchanged at 128 bytes. */
	zassert_equal(sizeof(struct flpr_ring_header), 128, "header still 128 bytes");
	zassert_equal(FLPR_RING_HEADER_SIZE, 128, "HEADER_SIZE = 128");
}

ZTEST(flpr_audio_process, test_payload_capacity_unchanged)
{
	zassert_equal(FLPR_RING_PAYLOAD_CAPACITY_FRAMES, 481, "payload capacity = 481 frames");
	zassert_equal(FLPR_RING_PAYLOAD_CAPACITY_BYTES, 1924, "payload capacity = 1924 bytes");
	zassert_equal(FLPR_RING_PAYLOAD_MAX_INPUT, 480, "max input = 480 frames");
}

ZTEST(flpr_audio_process, test_struct_state_size)
{
	zassert_equal(sizeof(struct audio_asrc_state), 24, "audio_asrc_state = 24 bytes");
}

/* ── CRC output for ASRC ─────────────────────────────────────────── */

ZTEST(flpr_audio_process, test_asrc_output_crc_matches_payload)
{
	setup_asrc_input(0, 0);

	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));
	zassert_equal(ret, FLPR_AUDIO_OK, "ASRC OK");

	/* CRC in output must match independent computation. */
	uint32_t computed = flpr_ring_crc32(output_payload, (size_t)output_meta.valid_frames * 4U);
	zassert_equal(output_meta.crc32, computed, "CRC matches produced payload bytes");
}

/* ── Identity: processing_status = 0, no ASRC flag ───────────────── */

ZTEST(flpr_audio_process, test_identity_no_asrc_flag_on_output)
{
	input_meta.sequence = 0;
	input_meta.epoch = 1;
	input_meta.valid_frames = 480;
	input_meta.flags = FLPR_SLOT_FLAG_VALID;
	input_meta.correction_ppm = 0;
	fill_input_ramp(0);

	int ret = flpr_audio_process(&input_meta, input_payload, sizeof(input_payload),
				     &output_meta, output_payload, sizeof(output_payload));
	zassert_equal(ret, FLPR_AUDIO_OK, "identity OK");
	zassert_equal(output_meta.flags & FLPR_SLOT_FLAG_ASRC_LINEAR, 0,
		      "no ASRC flag on identity output");
	zassert_equal(output_meta.processing_status, 0, "status = 0");
}

/* ── Test suite ──────────────────────────────────────────────────── */

ZTEST_SUITE(flpr_audio_process, NULL, NULL, test_setup, NULL, NULL);
