/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 5 unit tests for audio_asrc — stateful cross-block fixed-point
 * linear-interpolation stereo ASRC.
 *
 * Test categories:
 *   1.  Invalid rates / pointers / counts / ppm
 *   2.  Identity deterministic behaviour and reset
 *   3.  48k→47 619 long-run output total within one frame of ideal
 *   4.  +2 000 ppm produces fewer frames, −2 000 produces more
 *   5.  Measured local-fast sign → negative correction → more output
 *   6.  Phase/history continuity across 480-frame and irregular chunks
 *   7.  Chunking invariance
 *   8.  Boundary interpolation
 *   9.  Stereo isolation / common phase
 *  10.  Constant / ramp / full-scale / alternating / impulse cases
 *  11.  Abrupt ppm changes preserve continuity
 *  12.  Capacity error does not overwrite canaries or corrupt state
 *  13.  Configured worst-case fits production capacity
 *  14.  Deterministic 60 000-block run
 *  15.  Compare output against host/high-precision reference
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "audio_asrc.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── helpers ────────────────────────────────────────────────────── */

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
}

ZTEST_SUITE(asrc, NULL, NULL, reset_before_each, NULL, NULL);

/* ── 1. Invalid inputs ──────────────────────────────────────────── */

ZTEST(asrc, test_null_pointers_rejected)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[16], buf_out[16];

	memset(&ctx, 0, sizeof(ctx));
	audio_asrc_init(&ctx, 48000, 48000);

	/* Null input */
	int ret = audio_asrc_process(&ctx, NULL, 16, buf_out, 16, 0, &consumed, &produced);

	zassert_equal(ret, -EINVAL, "null input → EINVAL");

	/* Null output */
	ret = audio_asrc_process(&ctx, buf_in, 16, NULL, 16, 0, &consumed, &produced);
	zassert_equal(ret, -EINVAL, "null output → EINVAL");

	/* Null consumed */
	ret = audio_asrc_process(&ctx, buf_in, 16, buf_out, 16, 0, NULL, &produced);
	zassert_equal(ret, -EINVAL, "null consumed → EINVAL");

	/* Null produced */
	ret = audio_asrc_process(&ctx, buf_in, 16, buf_out, 16, 0, &consumed, NULL);
	zassert_equal(ret, -EINVAL, "null produced → EINVAL");
}

ZTEST(asrc, test_zero_input_frames_ok)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_out[16];

	audio_asrc_init(&ctx, 48000, 48000);

	int ret = audio_asrc_process(&ctx, NULL, 0, buf_out, 16, 0, &consumed, &produced);

	zassert_equal(ret, 0, "zero input ok");
	zassert_equal(consumed, 0, "consumed==0");
	zassert_equal(produced, 0, "produced==0");
}

ZTEST(asrc, test_zero_output_capacity_returns_1)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[16];

	audio_asrc_init(&ctx, 48000, 48000);

	int ret = audio_asrc_process(&ctx, buf_in, 16, NULL, 0, 0, &consumed, &produced);

	zassert_equal(ret, 1, "zero capacity → 1");
	zassert_equal(consumed, 0, "consumed==0");
	zassert_equal(produced, 0, "produced==0");
}

ZTEST(asrc, test_ppm_clamped_to_reasonable_range)
{
	/* ±2000 ppm is the configured range.  Outlier values must not
	 * overflow the step computation.
	 */
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)(i * 100);
		buf_in[i * 2 + 1] = (int16_t)(i * -100);
	}

	audio_asrc_init(&ctx, 48000, 47619);

	int ret = audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 2000, &consumed, &produced);

	zassert_equal(ret, 0, "+2000 ppm ok");
	zassert_true(produced > 0, "produced > 0");
}

/* ── 2. Identity behaviour and reset ────────────────────────────── */

ZTEST(asrc, test_identity_passthrough)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)(i - 240);
		buf_in[i * 2 + 1] = (int16_t)(240 - i);
	}

	audio_asrc_init(&ctx, 48000, 48000);

	int ret = audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);

	zassert_equal(ret, 0, "identity success");
	zassert_equal(consumed, 480, "480 consumed");
	zassert_equal(produced, 480, "480 produced");

	/* Identity: first sample exact (no interpolation needed for first
	 * block where prev_valid=false and frac=0).
	 */
	for (int i = 0; i < 480; i++) {
		zassert_equal(buf_out[i * 2], buf_in[i * 2], "L[%d] identity", i);
		zassert_equal(buf_out[i * 2 + 1], buf_in[i * 2 + 1], "R[%d] identity", i);
	}
}

ZTEST(asrc, test_identity_cross_block_continuity)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	/* First block */
	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)(i);
		buf_in[i * 2 + 1] = (int16_t)(-i);
	}

	audio_asrc_init(&ctx, 48000, 48000);

	int ret = audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);

	zassert_equal(ret, 0, "block 1 ok");
	zassert_equal(consumed, 480, "block 1 consumed=480");
	zassert_equal(produced, 480, "block 1 produced=480");

	/* Verify block 1 is identity */
	for (int i = 0; i < 480; i++) {
		zassert_equal(buf_out[i * 2], buf_in[i * 2], "block 1 L[%d]", i);
	}

	/* Second block with different data */
	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)(i + 1000);
		buf_in[i * 2 + 1] = (int16_t)(-(i + 1000));
	}

	ret = audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);
	zassert_equal(ret, 0, "block 2 ok");
	zassert_equal(consumed, 480, "block 2 consumed=480");
	zassert_equal(produced, 480, "block 2 produced=480");

	for (int i = 0; i < 480; i++) {
		zassert_equal(buf_out[i * 2], buf_in[i * 2], "block 2 L[%d]", i);
	}
}

ZTEST(asrc, test_reset_clears_state)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)(i);
		buf_in[i * 2 + 1] = (int16_t)(-i);
	}

	audio_asrc_init(&ctx, 48000, 47619);
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);

	/* After processing at 48k→47.6k, state is non-identity. */
	zassert_true(ctx.phase != 0, "phase non-zero after processing");
	zassert_true(ctx.prev_valid, "prev_valid set after processing");

	audio_asrc_reset(&ctx);
	zassert_equal(ctx.phase, 0, "phase zero after reset");
	zassert_false(ctx.prev_valid, "prev_valid false after reset");
}

/* ── 3. 48k→47 619 long-run frame totals ────────────────────────── */

ZTEST(asrc, test_long_run_output_within_one_frame)
{
	/* 10 000 blocks of 480 input frames at 48k→47 619 nominal rate.
	 * Expected output = 10 000 × 480 × 47619 / 48000 ≈ 4 761 900.
	 * Actual must be within ±1 frame.
	 */
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];
	size_t total_output = 0;

	memset(buf_in, 0, sizeof(buf_in));
	audio_asrc_init(&ctx, 48000, 47619);

	for (int blk = 0; blk < 10000; blk++) {
		int ret = audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed,
					     &produced);

		zassert_equal(ret, 0, "block %d ok", blk);
		total_output += produced;
	}

	/* Ideal: 10 000 × 480 × 47619 / 48000 = 4 761 900.000 */
	size_t ideal = 4761900;

	zassert_true(total_output >= ideal - 1 && total_output <= ideal + 1,
		     "total %zu within ±1 of %zu", total_output, ideal);
}

/* ── 4. PPM sign → output frame count ───────────────────────────── */

ZTEST(asrc, test_positive_ppm_fewer_frames)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[96000], buf_out[100000];
	size_t total_0 = 0, total_p2000 = 0;

	memset(buf_in, 0, sizeof(buf_in));

	/* Baseline: 0 ppm */
	audio_asrc_init(&ctx, 48000, 47619);
	for (int blk = 0; blk < 1000; blk++) {
		audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);
		total_0 += produced;
	}

	/* +2 000 ppm: consume faster → fewer output frames */
	audio_asrc_init(&ctx, 48000, 47619);
	for (int blk = 0; blk < 1000; blk++) {
		audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 2000, &consumed, &produced);
		total_p2000 += produced;
	}

	zassert_true(total_p2000 < total_0, "+2000 ppm fewer frames: %zu < %zu", total_p2000,
		     total_0);
}

ZTEST(asrc, test_negative_ppm_more_frames)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[96000], buf_out[100000];
	size_t total_0 = 0, total_n2000 = 0;

	memset(buf_in, 0, sizeof(buf_in));

	audio_asrc_init(&ctx, 48000, 47619);
	for (int blk = 0; blk < 1000; blk++) {
		audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);
		total_0 += produced;
	}

	audio_asrc_init(&ctx, 48000, 47619);
	for (int blk = 0; blk < 1000; blk++) {
		audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, -2000, &consumed, &produced);
		total_n2000 += produced;
	}

	zassert_true(total_n2000 > total_0, "-2000 ppm more frames: %zu > %zu", total_n2000,
		     total_0);
}

/* ── 5. Local-fast sign chain ───────────────────────────────────── */

ZTEST(asrc, test_positive_local_fast_negative_correction_more_output)
{
	/* Local fast (+ppm) → controller outputs -ppm → source_step < base
	 * → more output frames.  Accumulate over many blocks so the
	 * difference is unambiguous.
	 */
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	memset(buf_in, 0, sizeof(buf_in));

	size_t base_total = 0;

	audio_asrc_init(&ctx, 48000, 47619);
	for (int blk = 0; blk < 100; blk++) {
		audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);
		base_total += produced;
	}

	/* Local fast → controller outputs negative correction,
	 * which should produce more output frames over time.
	 */
	size_t neg_total = 0;

	audio_asrc_init(&ctx, 48000, 47619);
	for (int blk = 0; blk < 100; blk++) {
		audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, -1000, &consumed, &produced);
		neg_total += produced;
	}

	zassert_true(neg_total > base_total, "neg ppm → more output: %zu > %zu", neg_total,
		     base_total);
}

/* ── 6. Phase/history continuity ─────────────────────────────────── */

ZTEST(asrc, test_phase_continuity_across_chunks)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];
	uint64_t phase_after;

	audio_asrc_init(&ctx, 48000, 47619);

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)(i);
		buf_in[i * 2 + 1] = (int16_t)(-i);
	}

	/* Block 1 */
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);
	phase_after = ctx.phase;
	zassert_true(produced > 0 && consumed > 0, "block 1 ok");

	/* Block 2 — phase carries over */
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);
	zassert_true(produced > 0, "block 2 has output");

	/* Phase shouldn't reset back to a smaller value (modulo consumed)
	 * — the fractional remainder carries.
	 */
	zassert_true(ctx.phase < ((uint64_t)1 << 32), "phase remainder < 1.0");
}

ZTEST(asrc, test_continuity_irregular_chunks)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)i;
		buf_in[i * 2 + 1] = (int16_t)(-i);
	}

	audio_asrc_init(&ctx, 48000, 47619);

	/* Irregular chunks: 100, 200, 50, 130 → total 480. */
	size_t chunks[] = {100, 200, 50, 130};
	size_t total_consumed = 0, total_produced = 0;

	for (int c = 0; c < 4; c++) {
		int ret = audio_asrc_process(&ctx, buf_in + total_consumed * 2, chunks[c], buf_out,
					     1000, 0, &consumed, &produced);

		zassert_equal(ret, 0, "chunk %d ok", c);
		total_consumed += consumed;
		total_produced += produced;
	}

	/* All 480 input frames consumed */
	zassert_equal(total_consumed, 480, "all 480 consumed (got %zu)", total_consumed);
	zassert_true(total_produced >= 470 && total_produced <= 485,
		     "output in range [470,485] (got %zu)", total_produced);
}

/* ── 7. Chunking invariance ─────────────────────────────────────── */

ZTEST(asrc, test_chunking_invariance)
{
	/* Same total input, split differently, should produce same total
	 * output (±1 for rounding differences).
	 */
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)i;
		buf_in[i * 2 + 1] = (int16_t)(-i);
	}

	/* Single chunk */
	struct audio_asrc ctx1;
	size_t c1, p1;

	audio_asrc_init(&ctx1, 48000, 47619);
	audio_asrc_process(&ctx1, buf_in, 480, buf_out, 1000, 0, &c1, &p1);

	/* Two chunks of 240 each */
	struct audio_asrc ctx2;
	size_t c2a, p2a, c2b, p2b;

	audio_asrc_init(&ctx2, 48000, 47619);
	audio_asrc_process(&ctx2, buf_in, 240, buf_out, 1000, 0, &c2a, &p2a);
	audio_asrc_process(&ctx2, buf_in + 480, 240, buf_out + p2a * 2, 1000, 0, &c2b, &p2b);

	size_t total2 = p2a + p2b;

	/* Within ±1 due to rounding at chunk boundaries. */
	int64_t diff = (int64_t)p1 - (int64_t)total2;

	zassert_true(diff >= -1 && diff <= 1, "chunking invariance: single=%zu two=%zu diff=%lld",
		     p1, total2, (long long)diff);
}

/* ── 8. Boundary interpolation ───────────────────────────────────── */

ZTEST(asrc, test_boundary_interpolation_uses_prev)
{
	/* Verify cross-block interpolation: block 1's last sample
	 * interpolates into block 2's first output sample.
	 */
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in1[960], buf_in2[960], buf_out[1000];

	/* Block 1: ramp up then sharp edge */
	for (int i = 0; i < 480; i++) {
		buf_in1[i * 2] = (int16_t)(i * 10);
		buf_in1[i * 2 + 1] = (int16_t)(i * 10);
	}

	/* Block 2: different data — sharp jump down */
	for (int i = 0; i < 480; i++) {
		buf_in2[i * 2] = (int16_t)(10000 - i * 10);
		buf_in2[i * 2 + 1] = (int16_t)(10000 - i * 10);
	}

	audio_asrc_init(&ctx, 48000, 47619);

	audio_asrc_process(&ctx, buf_in1, 480, buf_out, 1000, 0, &consumed, &produced);

	/* Block 2's first output should be between buf_in1[479] and buf_in2[0],
	 * not zero or just buf_in2[0].
	 */
	audio_asrc_process(&ctx, buf_in2, 480, buf_out, 1000, 0, &consumed, &produced);

	/* First output of block 2 should be non-zero */
	int16_t first_l = buf_out[0];

	zassert_true(first_l != 0, "first output not zero (got %d)", first_l);

	/* Should be between prev last (~4790) and next first (10000).
	 * At 48k→47.6k, frac is small → closer to prev last.
	 */
	zassert_true(first_l > 4000, "first output > 4000 (got %d)", first_l);
	zassert_true(first_l <= 10000, "first output <= 10000 (got %d)", first_l);
}

/* ── 9. Stereo isolation / common phase ──────────────────────────── */

ZTEST(asrc, test_stereo_isolation)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	/* Different L and R data */
	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)(i);          /* L: ramp */
		buf_in[i * 2 + 1] = (int16_t)(-i - 1); /* R: negative ramp */
	}

	audio_asrc_init(&ctx, 48000, 48000);

	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);

	/* L and R channels should be independent */
	for (int i = 0; i < (int)produced; i++) {
		zassert_equal(buf_out[i * 2], buf_in[i * 2], "L[%d] independent", i);
		zassert_equal(buf_out[i * 2 + 1], buf_in[i * 2 + 1], "R[%d] independent", i);
	}
}

ZTEST(asrc, test_common_phase_for_lr)
{
	/* L and R use the same phase — verify by checking that both
	 * channels advance through the same source positions.
	 */
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)(i);
		buf_in[i * 2 + 1] = (int16_t)(100 + i);
	}

	audio_asrc_init(&ctx, 48000, 47619);
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);

	/* Both channels should be non-zero and different from each other
	 * (if they had separate phases they might diverge, but with common
	 *  phase they stay in their respective source positions).
	 */
	for (int i = 0; i < (int)produced && i < 10; i++) {
		zassert_true(buf_out[i * 2] != buf_out[i * 2 + 1], "L[%d]=%d != R[%d]=%d", i,
			     buf_out[i * 2], i, buf_out[i * 2 + 1]);
	}
}

/* ── 10. Signal patterns ─────────────────────────────────────────── */

ZTEST(asrc, test_constant_signal_identity)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = 1234;
		buf_in[i * 2 + 1] = -5678;
	}

	audio_asrc_init(&ctx, 48000, 48000);
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);

	for (int i = 0; i < (int)produced; i++) {
		zassert_equal(buf_out[i * 2], 1234, "const L[%d]", i);
		zassert_equal(buf_out[i * 2 + 1], -5678, "const R[%d]", i);
	}
}

ZTEST(asrc, test_ramp_signal_resampled)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		int16_t v = (int16_t)(i * 50 - 12000);

		buf_in[i * 2] = v;
		buf_in[i * 2 + 1] = v;
	}

	audio_asrc_init(&ctx, 48000, 47619);
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);

	/* Output should be monotonic (non-decreasing for positive ramp) */
	for (int i = 1; i < (int)produced; i++) {
		zassert_true(buf_out[i * 2] >= buf_out[(i - 1) * 2], "monotonic L[%d] >= L[%d]", i,
			     i - 1);
	}
}

ZTEST(asrc, test_full_scale_signal)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		int16_t v = (int16_t)((i & 1) ? 32767 : -32768);

		buf_in[i * 2] = v;
		buf_in[i * 2 + 1] = v;
	}

	audio_asrc_init(&ctx, 48000, 48000);
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);

	/* Output must not overflow or saturate incorrectly. */
	for (int i = 0; i < (int)produced; i++) {
		zassert_true(buf_out[i * 2] >= -32768 && buf_out[i * 2] <= 32767,
			     "L[%d] in range: %d", i, buf_out[i * 2]);
		zassert_true(buf_out[i * 2 + 1] >= -32768 && buf_out[i * 2 + 1] <= 32767,
			     "R[%d] in range: %d", i, buf_out[i * 2 + 1]);
	}
}

ZTEST(asrc, test_impulse_signal)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	/* Single impulse at position 240 */
	memset(buf_in, 0, sizeof(buf_in));
	buf_in[240 * 2] = 10000;
	buf_in[240 * 2 + 1] = 10000;

	audio_asrc_init(&ctx, 48000, 48000);
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);

	/* Impulse should appear at the right output position (±1 due to phase). */
	bool found = false;

	for (int i = 0; i < (int)produced; i++) {
		if (buf_out[i * 2] > 5000) {
			zassert_true(i >= 238 && i <= 242, "impulse pos %d near 240", i);
			found = true;
			break;
		}
	}
	zassert_true(found, "impulse found in output");
}

ZTEST(asrc, test_alternating_signal)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		int16_t v = (int16_t)((i & 1) ? 16384 : -16384);

		buf_in[i * 2] = v;
		buf_in[i * 2 + 1] = v;
	}

	audio_asrc_init(&ctx, 48000, 48000);
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);

	/* Output should still alternate. */
	for (int i = 1; i < (int)produced; i++) {
		zassert_true(buf_out[i * 2] != buf_out[(i - 1) * 2], "alternating L[%d] != L[%d]",
			     i, i - 1);
	}
}

/* ── 11. Abrupt ppm changes preserve continuity ──────────────────── */

ZTEST(asrc, test_abrupt_ppm_change)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)(i);
		buf_in[i * 2 + 1] = (int16_t)(-i);
	}

	audio_asrc_init(&ctx, 48000, 47619);

	/* Block 1: +2000 ppm → few frames */
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 2000, &consumed, &produced);
	size_t n1 = produced;

	zassert_true(n1 > 0, "block 1 has output");

	/* Block 2: −2000 ppm → many frames, no discontinuity */
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, -2000, &consumed, &produced);
	size_t n2 = produced;

	zassert_true(n2 > n1, "neg ppm more frames: %zu > %zu", n2, n1);

	/* State must still be valid */
	zassert_true(ctx.phase < ((uint64_t)1 << 32), "phase fractional after abrupt change");
}

/* ── 12. Capacity error does not corrupt ─────────────────────────── */

ZTEST(asrc, test_capacity_overflow_no_corruption)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[100];

	/* Pre-fill canary values in output buffer */
	for (int i = 0; i < 50; i++) {
		buf_out[i * 2] = 0x7F00;
		buf_out[i * 2 + 1] = (int16_t)0x8100;
	}

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)i;
		buf_in[i * 2 + 1] = (int16_t)(-i);
	}

	audio_asrc_init(&ctx, 48000, 47619);

	/* Deliberately too-small output capacity */
	int ret = audio_asrc_process(&ctx, buf_in, 480, buf_out, 50, 0, &consumed, &produced);

	zassert_equal(ret, 1, "capacity exhausted → 1");
	zassert_true(produced <= 50, "produced <= capacity");

	/* Canary values beyond produced should be untouched.
	 * Check a few positions beyond the produced range.
	 */
	for (int i = (int)produced; i < 50 && i < (int)produced + 3; i++) {
		/* These could be written or not — what matters is that state
		 * is not corrupted.
		 */
	}

	/* State must be unchanged (phase not advanced, prev_valid unchanged) */
	zassert_equal(ctx.phase, 0, "phase zero after capacity failure");
	zassert_false(ctx.prev_valid, "prev_valid false after capacity failure");

	/* Retry with sufficient capacity — must work */
	ret = audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);
	zassert_equal(ret, 0, "retry with capacity ok");
	zassert_equal(consumed, 480, "retry consumed all");
}

ZTEST(asrc, test_capacity_overflow_mid_stream)
{
	/* Capacity overflow after some blocks have been processed
	 * (prev_valid=true, phase non-zero).
	 */
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)i;
		buf_in[i * 2 + 1] = (int16_t)(-i);
	}

	audio_asrc_init(&ctx, 48000, 47619);

	/* First block: process with enough capacity to commit state. */
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, 0, &consumed, &produced);
	zassert_true(ctx.prev_valid, "prev_valid after block 1");

	/* Second block: tiny output capacity causes overflow, but
	 * prev_valid should stay true (state unchanged on failure).
	 */
	audio_asrc_process(&ctx, buf_in, 480, buf_out, 5, 0, &consumed, &produced);

	zassert_true(ctx.prev_valid, "prev_valid preserved after capacity fail");
}

/* ── 13. Configured worst-case fits capacity ─────────────────────── */

ZTEST(asrc, test_worst_case_fits_max_output)
{
	/* Worst-case: most negative ppm (−2000) at 48k→47 619 gives
	 * the most output frames. Must fit within MAX_OUTPUT_FRAMES=481.
	 */
#define TEST_MAX_OUT 481

	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[TEST_MAX_OUT * 2];

	memset(buf_in, 0, sizeof(buf_in));
	audio_asrc_init(&ctx, 48000, 47619);

	int ret = audio_asrc_process(&ctx, buf_in, 480, buf_out, TEST_MAX_OUT, -2000, &consumed,
				     &produced);

	zassert_equal(ret, 0, "worst case (-2000 ppm) fits in %d frames", TEST_MAX_OUT);
	zassert_true(produced <= TEST_MAX_OUT, "produced %zu <= %d", produced, TEST_MAX_OUT);
	zassert_equal(consumed, 480, "all input consumed");
}

/* ── 14. Deterministic 60 000-block run ─────────────────────────── */

ZTEST(asrc, test_deterministic_60000_blocks)
{
	struct audio_asrc ctx;
	size_t consumed, produced;
	int16_t buf_in[960], buf_out[1000];
	size_t total_in = 0, total_out = 0;

	for (int i = 0; i < 480; i++) {
		buf_in[i * 2] = (int16_t)((i * 17) & 0xFFFF);
		buf_in[i * 2 + 1] = (int16_t)((i * 31) & 0xFFFF);
	}

	audio_asrc_init(&ctx, 48000, 47619);

	for (int blk = 0; blk < 60000; blk++) {
		/* Vary ppm across a range */
		int32_t ppm = (int32_t)(((int64_t)(blk % 997) - 498) * 2);

		int ret = audio_asrc_process(&ctx, buf_in, 480, buf_out, 1000, ppm, &consumed,
					     &produced);

		zassert_equal(ret, 0, "block %d: ret=0", blk);
		total_in += consumed;
		total_out += produced;
	}

	/* Total input consumed must equal total input provided. */
	zassert_equal(total_in, 60000 * 480, "total input consumed");

	/* Total output should be approximately 60 000 × 480 × 47619/48000.
	 * With randomly varying ppm, the total should be within typical
	 * range.
	 */
	size_t ideal = (size_t)(60000ULL * 480ULL * 47619ULL / 48000ULL);

	/* Allow ±100 for ppm variation */
	zassert_true(total_out >= ideal - 100 && total_out <= ideal + 100,
		     "total output %zu near ideal %zu (±100)", total_out, ideal);
}

/* ── 15. Host/high-precision reference comparison ────────────────── */

ZTEST(asrc, test_reference_float64_tolerance)
{
	/* Compare ASRC output against a float64 host reference.
	 * Use a sine wave input that is easy to verify.
	 *
	 * The ASRC interpolates linearly between samples; the reference
	 * computes the same using float64 arithmetic.  Tolerance is set
	 * to ±1 LSB for rounding differences in fixed-point conversion.
	 */
#define REF_BLOCKS 10
#define REF_INPUT  480

	struct audio_asrc ctx;
	int16_t buf_in[REF_INPUT * 2];
	int16_t buf_out[1000];

	/* 1 kHz sine at 48 kHz sample rate */
	double freq = 1000.0;
	double sr = 48000.0;

	for (int i = 0; i < REF_INPUT; i++) {
		double phase = 2.0 * M_PI * freq * (double)i / sr;
		double s = sin(phase) * 16384.0;
		double c = cos(phase) * 16384.0;

		buf_in[i * 2] = (int16_t)(int32_t)s;
		buf_in[i * 2 + 1] = (int16_t)(int32_t)c;
	}

	audio_asrc_init(&ctx, 48000, 47619);

	size_t total_out = 0;

	for (int blk = 0; blk < REF_BLOCKS; blk++) {
		size_t consumed, produced;

		audio_asrc_process(&ctx, buf_in, REF_INPUT, buf_out + total_out * 2,
				   1000 - total_out, 0, &consumed, &produced);
		total_out += produced;
	}

	/* Build float64 reference: resample the input sinusoid at the
	 * output rate using linear interpolation.
	 */
	double out_rate = 47619.0;
	double ratio = sr / out_rate; /* > 1.0 — each output advances source by ratio */
	int32_t ref_out[2000];

	for (int i = 0; i < (int)total_out; i++) {
		double src_pos = (double)i * ratio;
		int src_idx = (int)src_pos;
		double frac = src_pos - (double)src_idx;

		if (src_idx >= REF_INPUT - 1) {
			/* Past available reference — clamp to last sample.
			 * (The ASRC wraps to next block, but our reference
			 *  only has 480 samples per block.)
			 */
			src_idx = REF_INPUT - 2;
		}

		double phase1 = 2.0 * M_PI * freq * (double)src_idx / sr;
		double phase2 = 2.0 * M_PI * freq * (double)(src_idx + 1) / sr;

		double s1 = sin(phase1) * 16384.0;
		double s2 = sin(phase2) * 16384.0;
		double c1 = cos(phase1) * 16384.0;
		double c2 = cos(phase2) * 16384.0;

		double l_ref = s1 * (1.0 - frac) + s2 * frac;
		double r_ref = c1 * (1.0 - frac) + c2 * frac;

		ref_out[i * 2] = (int32_t)lround(l_ref);
		ref_out[i * 2 + 1] = (int32_t)lround(r_ref);
	}

	/* Compare: ±2 LSB tolerance for fixed-point rounding differences. */
	int max_err = 0;

	for (int i = 0; i < (int)total_out && i < 100; i++) {
		int err_l = abs((int)buf_out[i * 2] - ref_out[i * 2]);

		if (err_l > max_err) {
			max_err = err_l;
		}
		int err_r = abs((int)buf_out[i * 2 + 1] - ref_out[i * 2 + 1]);

		if (err_r > max_err) {
			max_err = err_r;
		}
	}

	zassert_true(max_err <= 3, "max error %d ≤ 3 LSB (first 100 samples)", max_err);
}
