/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * RH1A native public-boundary tests for the deterministic signal
 * generator, the real liblc3 encoder, and Mode A/B packing.
 *
 * Behavior is proven through repeat generation, exact packing, and
 * decoded output; expected PCM values are recomputed from the public
 * hil_source_sine_q15() wave and the frozen spec constants, never from
 * private helpers or copied hashes.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <lc3.h>
#include <zephyr/ztest.h>

#include "hil_source_signal.h"
#include "hil_source_types.h"

ZTEST_SUITE(hil_source_signal, NULL, NULL, NULL, NULL, NULL);

#define TEST_SEED        0x48A31C5DUL
#define TEST_SEED_2      0x12345678UL
#define FRAME_BUFS       480
#define PREAMBLE_SEGMENT HIL_SOURCE_PREAMBLE_SEGMENT_SAMPLES

/* uint32 phase-step constants so test-side phase arithmetic wraps in
 * uint32 (defined behavior) instead of promoting to unsigned long. */
#define LEFT_STEP  ((uint32_t)HIL_SOURCE_LEFT_PHASE_STEP)
#define RIGHT_STEP ((uint32_t)HIL_SOURCE_RIGHT_PHASE_STEP)

/* ── spec-recomputation helpers ───────────────────────────────────── */

static uint32_t xs(uint32_t x)
{
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

static uint32_t derive(uint32_t seed, uint32_t mask)
{
	uint32_t x = seed ^ mask;

	return x == 0U ? 0x6D2B79F5UL : x;
}

static int16_t expected_pcm(uint32_t phase, int16_t amp)
{
	int16_t wave = hil_source_sine_q15((phase >> 22) & 0x3FFU);

	if (amp == 0) {
		return 0;
	}
	return (int16_t)(((int64_t)wave * amp) / 32767);
}

/* Expected ramp amplitude at ramp index 0..239 (C99 truncation). */
static int16_t ramp_amp(int16_t old_amp, int16_t new_amp, uint32_t index)
{
	int64_t diff = (int64_t)new_amp - (int64_t)old_amp;

	return (int16_t)((int32_t)old_amp + (int32_t)((diff * (int64_t)(index + 1U)) /
						      (int64_t)HIL_SOURCE_TRANSITION_RAMP_SAMPLES));
}

static bool any_nonzero(const int16_t *p, size_t n)
{
	size_t i;

	for (i = 0U; i < n; i++) {
		if (p[i] != 0) {
			return true;
		}
	}
	return false;
}

static void init_encoder(struct hil_source_signal_encoder *enc, enum hil_source_mode mode,
			 enum hil_source_profile profile, uint32_t seed)
{
	int ch1 = (mode == HIL_SOURCE_MODE_B) ? HIL_SOURCE_CHANNEL_RIGHT : -1;

	zassert_equal(hil_source_signal_encoder_init(enc, mode, profile, seed,
						     HIL_SOURCE_CHANNEL_LEFT, ch1),
		      0, "encoder init");
}

/* Render the exact full preamble on every configured channel in bounded
 * 480-sample chunks so scored entry is legal. */
static void render_full_preamble(struct hil_source_signal_encoder *enc)
{
	static int16_t chunk[480];
	uint32_t ch;
	uint32_t done;

	for (ch = 0U; ch < enc->channel_count; ch++) {
		done = 0U;
		while (done < HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES) {
			zassert_equal(hil_source_signal_render(enc, ch, chunk, 480U, sizeof(chunk)),
				      480, "preamble chunk ch%u", ch);
			done += 480U;
		}
	}
}

/* Encode the exact profile preamble frame count so LC3 state matches the
 * real runtime preamble -> scored lifecycle. */
static void encode_full_preamble(struct hil_source_signal_encoder *enc)
{
	uint8_t sdu[240];
	uint16_t samples = hil_source_profile_frame_samples(enc->profile);
	uint16_t frames = HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES / samples;
	uint16_t sdu_size = hil_source_profile_sdu_size(enc->profile, enc->mode);
	uint32_t f;

	for (f = 0U; f < frames; f++) {
		zassert_equal(hil_source_signal_encode_next(enc, sdu, sizeof(sdu)),
			      (int)sdu_size, "preamble frame %u", f);
	}
}

/* ── 1. sine LUT endpoints and symmetry ───────────────────────────── */

ZTEST(hil_source_signal, test_sine_lut_endpoints_and_symmetry)
{
	uint32_t i;

	zassert_equal(hil_source_sine_q15(0U), 0, "sin(0)");
	zassert_equal(hil_source_sine_q15(256U), 32767, "sin(pi/2)");
	zassert_equal(hil_source_sine_q15(512U), 0, "sin(pi)");
	zassert_equal(hil_source_sine_q15(768U), -32767, "sin(3pi/2)");
	zassert_equal(hil_source_sine_q15(1024U), 0, "full cycle");
	zassert_true(hil_source_sine_q15(1U) > 0, "monotone start");

	/* Half-wave symmetry about pi/2 (quarter-wave endpoints exact). */
	for (i = 0U; i <= 255U; i++) {
		zassert_equal(hil_source_sine_q15(256U - i), hil_source_sine_q15(256U + i),
			      "even symmetry at %u", i);
	}
	/* Odd symmetry about pi (half wave). */
	for (i = 0U; i <= 256U; i++) {
		zassert_equal(hil_source_sine_q15(512U - i),
			      (int16_t)-hil_source_sine_q15(512U + i), "odd symmetry at %u", i);
	}
}

/* ── 2. profile helpers ───────────────────────────────────────────── */

ZTEST(hil_source_signal, test_profile_helpers)
{
	zassert_equal(hil_source_profile_frame_samples(HIL_SOURCE_PROFILE_48_3_1), 360U);
	zassert_equal(hil_source_profile_frame_samples(HIL_SOURCE_PROFILE_48_4_1), 480U);
	zassert_equal(hil_source_profile_frame_duration_us(HIL_SOURCE_PROFILE_48_3_1), 7500U);
	zassert_equal(hil_source_profile_frame_duration_us(HIL_SOURCE_PROFILE_48_4_1), 10000U);
	zassert_equal(hil_source_profile_octets_per_channel(HIL_SOURCE_PROFILE_48_3_1), 90U);
	zassert_equal(hil_source_profile_octets_per_channel(HIL_SOURCE_PROFILE_48_4_1), 120U);
	zassert_equal(hil_source_profile_sdu_size(HIL_SOURCE_PROFILE_48_3_1, HIL_SOURCE_MODE_MONO),
		      90U);
	zassert_equal(hil_source_profile_sdu_size(HIL_SOURCE_PROFILE_48_3_1, HIL_SOURCE_MODE_A),
		      90U);
	zassert_equal(hil_source_profile_sdu_size(HIL_SOURCE_PROFILE_48_3_1, HIL_SOURCE_MODE_B),
		      180U);
	zassert_equal(hil_source_profile_sdu_size(HIL_SOURCE_PROFILE_48_4_1, HIL_SOURCE_MODE_MONO),
		      120U);
	zassert_equal(hil_source_profile_sdu_size(HIL_SOURCE_PROFILE_48_4_1, HIL_SOURCE_MODE_A),
		      120U);
	zassert_equal(hil_source_profile_sdu_size(HIL_SOURCE_PROFILE_48_4_1, HIL_SOURCE_MODE_B),
		      240U);

	/* Invalid profile and mode rejections return 0. */
	zassert_equal(hil_source_profile_frame_samples((enum hil_source_profile)99), 0U);
	zassert_equal(hil_source_profile_frame_duration_us((enum hil_source_profile)99), 0U);
	zassert_equal(hil_source_profile_octets_per_channel((enum hil_source_profile)99), 0U);
	zassert_equal(
		hil_source_profile_sdu_size((enum hil_source_profile)99, HIL_SOURCE_MODE_MONO), 0U);
	zassert_equal(
		hil_source_profile_sdu_size(HIL_SOURCE_PROFILE_48_4_1, (enum hil_source_mode)99),
		0U);
}

ZTEST(hil_source_signal, test_encoder_init_validation)
{
	struct hil_source_signal_encoder enc;

	zassert_equal(hil_source_signal_encoder_init(&enc, HIL_SOURCE_MODE_MONO,
						     (enum hil_source_profile)99, TEST_SEED,
						     HIL_SOURCE_CHANNEL_LEFT, -1),
		      -EINVAL, "bad profile");
	zassert_equal(hil_source_signal_encoder_init(&enc, (enum hil_source_mode)99,
						     HIL_SOURCE_PROFILE_48_4_1, TEST_SEED,
						     HIL_SOURCE_CHANNEL_LEFT, -1),
		      -EINVAL, "bad mode");
	zassert_equal(hil_source_signal_encoder_init(&enc, HIL_SOURCE_MODE_MONO,
						     HIL_SOURCE_PROFILE_48_4_1, 0U,
						     HIL_SOURCE_CHANNEL_LEFT, -1),
		      -EINVAL, "zero seed");
	zassert_equal(hil_source_signal_encoder_init(&enc, HIL_SOURCE_MODE_MONO,
						     HIL_SOURCE_PROFILE_48_4_1, TEST_SEED,
						     HIL_SOURCE_CHANNEL_RIGHT, -1),
		      -EINVAL, "mono must be left");
	zassert_equal(hil_source_signal_encoder_init(
			      &enc, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED,
			      HIL_SOURCE_CHANNEL_LEFT, HIL_SOURCE_CHANNEL_RIGHT),
		      -EINVAL, "mono single channel");
	zassert_equal(hil_source_signal_encoder_init(
			      &enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED,
			      HIL_SOURCE_CHANNEL_RIGHT, HIL_SOURCE_CHANNEL_LEFT),
		      -EINVAL, "mode_b order");
	zassert_equal(hil_source_signal_encoder_init(NULL, HIL_SOURCE_MODE_MONO,
						     HIL_SOURCE_PROFILE_48_4_1, TEST_SEED,
						     HIL_SOURCE_CHANNEL_LEFT, -1),
		      -EINVAL, "null encoder");
}

/* ── 3. preamble segments, energy, and total frame counts ─────────── */

ZTEST(hil_source_signal, test_preamble_segment_pattern)
{
	static int16_t seg[PREAMBLE_SEGMENT];
	struct hil_source_signal_encoder enc;
	uint32_t s;

	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);

	for (s = 0U; s < HIL_SOURCE_PREAMBLE_SEGMENTS; s++) {
		zassert_equal(
			hil_source_signal_render(&enc, 0U, seg, PREAMBLE_SEGMENT, sizeof(seg)),
			(int)PREAMBLE_SEGMENT, "render L segment %u", s);
		switch (s) {
		case 0U: /* silence */
		case 2U: /* silence */
		case 4U: /* silence */
			zassert_false(any_nonzero(seg, PREAMBLE_SEGMENT),
				      "silence segment %u must be zero", s);
			break;
		case 1U: /* left only at full amplitude */
			zassert_true(any_nonzero(seg, PREAMBLE_SEGMENT),
				     "left segment %u must have energy", s);
			/* No transition ramp: segment starts at full amp. */
			zassert_equal(seg[0],
				      expected_pcm((uint32_t)(s * PREAMBLE_SEGMENT) * LEFT_STEP,
						   HIL_SOURCE_AMPLITUDE_FULL),
				      "left segment starts at full amplitude");
			break;
		case 3U: /* right only: left channel silent */
			zassert_false(any_nonzero(seg, PREAMBLE_SEGMENT),
				      "left must be silent during right segment");
			break;
		case 5U: /* both at full amplitude */
			zassert_true(any_nonzero(seg, PREAMBLE_SEGMENT),
				     "both segment must have energy");
			break;
		default:
			zassert_unreachable("bad segment");
		}
	}
	zassert_equal(enc.channels[0].preamble_rendered, HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES,
		      "preamble consumed");
}

ZTEST(hil_source_signal, test_preamble_channel_separation)
{
	static int16_t seg[PREAMBLE_SEGMENT];
	struct hil_source_signal_encoder enc;
	uint32_t s;

	/* The right channel is silent during silence, left-only, and the
	 * middle silence segments, and active during right-only and both. */
	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	for (s = 0U; s < HIL_SOURCE_PREAMBLE_SEGMENTS; s++) {
		zassert_equal(
			hil_source_signal_render(&enc, 1U, seg, PREAMBLE_SEGMENT, sizeof(seg)),
			(int)PREAMBLE_SEGMENT, "render R seg %u", s);
		switch (s) {
		case 0U: /* silence */
		case 1U: /* left only */
		case 2U: /* silence */
		case 4U: /* silence */
			zassert_false(any_nonzero(seg, PREAMBLE_SEGMENT),
				      "right silent in segment %u", s);
			break;
		case 3U: /* right only */
		case 5U: /* both */
			zassert_true(any_nonzero(seg, PREAMBLE_SEGMENT),
				     "right active in segment %u", s);
			break;
		default:
			zassert_unreachable("bad segment");
		}
	}
}

ZTEST(hil_source_signal, test_preamble_frame_counts_and_overrun)
{
	struct hil_source_signal_encoder enc;
	uint8_t sdu[240];
	uint16_t expected_frames;
	uint16_t samples;
	uint32_t i;

	/* 7.5 ms: 69120 / 360 = 192 frames; 10 ms: 69120 / 480 = 144. */
	for (uint8_t p = 0U; p < 2U; p++) {
		enum hil_source_profile profile =
			p == 0U ? HIL_SOURCE_PROFILE_48_3_1 : HIL_SOURCE_PROFILE_48_4_1;

		samples = hil_source_profile_frame_samples(profile);
		expected_frames = HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES / samples;
		init_encoder(&enc, HIL_SOURCE_MODE_MONO, profile, TEST_SEED);
		for (i = 0U; i < expected_frames; i++) {
			zassert_equal(
				hil_source_signal_encode_next(&enc, sdu, sizeof(sdu)),
				(int)hil_source_profile_sdu_size(profile, HIL_SOURCE_MODE_MONO),
				"preamble frame %u", i);
		}
		zassert_equal(enc.channels[0].preamble_rendered, HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES,
			      "preamble exactly consumed");
		zassert_equal(hil_source_signal_encode_next(&enc, sdu, sizeof(sdu)), -EOVERFLOW,
			      "preamble overrun profile %u", p);
		zassert_equal(enc.channels[0].preamble_rendered, HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES,
			      "overrun must not advance preamble");
	}
}

/* ── 4. chunk-size independence ───────────────────────────────────── */

ZTEST(hil_source_signal, test_chunk_size_independence)
{
	static int16_t buf360[HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES];
	static int16_t buf480[HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES];
	struct hil_source_signal_encoder enc;
	uint32_t done;

	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	done = 0U;
	while (done < HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES) {
		zassert_equal(hil_source_signal_render(&enc, 0U, buf360 + done, 360U,
						       sizeof(buf360) - done * 2U),
			      360, "360-chunk");
		done += 360U;
	}

	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	done = 0U;
	while (done < HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES) {
		zassert_equal(hil_source_signal_render(&enc, 0U, buf480 + done, 480U,
						       sizeof(buf480) - done * 2U),
			      480, "480-chunk");
		done += 480U;
	}

	zassert_mem_equal(buf360, buf480, sizeof(buf360),
			  "360- and 480-sample chunked renders must match");
}

/* ── 5. phase continuity ──────────────────────────────────────────── */

ZTEST(hil_source_signal, test_phase_continuity_across_chunks_and_stages)
{
	static int16_t one[960];
	static int16_t two[960];
	struct hil_source_signal_encoder enc;
	uint32_t pre_phase;

	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	zassert_equal(hil_source_signal_render(&enc, 0U, one, 960U, sizeof(one)), 960,
		      "render 960");
	pre_phase = enc.channels[0].phase;

	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	zassert_equal(hil_source_signal_render(&enc, 0U, two, 480U, sizeof(two)), 480,
		      "render 480a");
	zassert_equal(hil_source_signal_render(&enc, 0U, two + 480U, 480U, sizeof(two) - 480U * 2U),
		      480, "render 480b");
	zassert_mem_equal(one, two, sizeof(one), "chunk split must match");
	zassert_equal(pre_phase, enc.channels[0].phase, "same total phase");

	/* Stage boundary: carrier phase continues from the exact full
	 * preamble into the first scored sample. */
	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0, "to scored");
	{
		uint32_t prng = xs(derive(TEST_SEED, HIL_SOURCE_LEFT_PRNG_MASK));
		int16_t target = (prng & 1U) ? HIL_SOURCE_AMPLITUDE_FULL : HIL_SOURCE_AMPLITUDE_LOW;
		int16_t expected = expected_pcm(
			(uint32_t)HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES * LEFT_STEP,
			ramp_amp(HIL_SOURCE_AMPLITUDE_FULL, target, 0U));

		zassert_equal(hil_source_signal_render(&enc, 0U, two, 1U, sizeof(two)), 1,
			      "first scored sample");
		zassert_equal(two[0], expected, "scored entry continues preamble phase");
	}
}

/* ── 6. scored envelope ───────────────────────────────────────────── */

ZTEST(hil_source_signal, test_scored_envelope_blocks_and_ramps)
{
	static int16_t buf[2 * HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES];
	struct hil_source_signal_encoder enc;
	uint32_t prng;
	uint32_t k;
	int16_t t0;
	int16_t t1;

	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0, "to scored");
	zassert_equal(hil_source_signal_render(&enc, 0U, buf, 2 * HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES,
					       sizeof(buf)),
		      2 * HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES, "two scored blocks");

	/* First block target consumes the first xorshift32 update. */
	prng = xs(derive(TEST_SEED, HIL_SOURCE_LEFT_PRNG_MASK));
	t0 = (prng & 1U) ? HIL_SOURCE_AMPLITUDE_FULL : HIL_SOURCE_AMPLITUDE_LOW;
	t1 = (xs(prng) & 1U) ? HIL_SOURCE_AMPLITUDE_FULL : HIL_SOURCE_AMPLITUDE_LOW;

	/* Block 0: ramp from full preamble amplitude to t0, then hold. */
	for (k = 0U; k < HIL_SOURCE_TRANSITION_RAMP_SAMPLES; k++) {
		int16_t expect = expected_pcm(
			(uint32_t)(HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES + k) * LEFT_STEP,
			ramp_amp(HIL_SOURCE_AMPLITUDE_FULL, t0, k));

		zassert_equal(buf[k], expect, "block0 ramp sample %u", k);
	}
	for (k = HIL_SOURCE_TRANSITION_RAMP_SAMPLES; k < HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES; k++) {
		int16_t expect = expected_pcm(
			(uint32_t)(HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES + k) * LEFT_STEP, t0);

		zassert_equal(buf[k], expect, "block0 hold sample %u", k);
	}

	/* Block 1 starts at exactly 5760 samples (120 ms): ramp t0 -> t1
	 * for the first 240 samples, then hold t1. */
	zassert_equal(buf[HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES],
		      expected_pcm((uint32_t)(HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES +
					      HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES) * LEFT_STEP,
				   ramp_amp(t0, t1, 0U)),
		      "block1 first ramp sample");
	zassert_equal(buf[HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES + HIL_SOURCE_TRANSITION_RAMP_SAMPLES],
		      expected_pcm((uint32_t)(HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES +
					      HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES +
					      HIL_SOURCE_TRANSITION_RAMP_SAMPLES) *
					   LEFT_STEP,
				   t1),
		      "block1 hold sample");
}

ZTEST(hil_source_signal, test_scored_left_right_independence)
{
	static int16_t left[HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES];
	static int16_t right[HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES];
	struct hil_source_signal_encoder enc;

	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0, "to scored");
	zassert_equal(hil_source_signal_render(&enc, 0U, left, HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES,
					       sizeof(left)),
		      HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES, "left block");
	zassert_equal(hil_source_signal_render(&enc, 1U, right, HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES,
					       sizeof(right)),
		      HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES, "right block");

	/* Distinct derived PRNG states must yield distinct targets and
	 * distinct carriers; the rendered blocks must differ. */
	zassert_not_equal(derive(TEST_SEED, HIL_SOURCE_LEFT_PRNG_MASK),
			  derive(TEST_SEED, HIL_SOURCE_RIGHT_PRNG_MASK), "derived states differ");
	zassert_true(memcmp(left, right, sizeof(left)) != 0, "left and right must differ");
}

ZTEST(hil_source_signal, test_scored_repeatability_and_seed_change)
{
	/* Four envelope blocks: early block targets can coincide across
	 * seeds, so divergence is proven over the later blocks too. */
	static int16_t a[4 * HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES];
	static int16_t b[4 * HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES];
	struct hil_source_signal_encoder enc;

	init_encoder(&enc, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0, "to scored");
	zassert_equal(hil_source_signal_render(&enc, 0U, a, 4 * HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES,
					       sizeof(a)),
		      4 * (int)HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES, "run a");

	init_encoder(&enc, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0, "to scored");
	zassert_equal(hil_source_signal_render(&enc, 0U, b, 4 * HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES,
					       sizeof(b)),
		      4 * (int)HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES, "run b");
	zassert_mem_equal(a, b, sizeof(a), "same seed must repeat exactly");

	init_encoder(&enc, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED_2);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0, "to scored");
	zassert_equal(hil_source_signal_render(&enc, 0U, b, 4 * HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES,
					       sizeof(b)),
		      4 * (int)HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES, "different seed");
	zassert_true(memcmp(a, b, sizeof(a)) != 0, "different seed must change output");
}

/* ── 7. tail silence and continued phase ──────────────────────────── */

ZTEST(hil_source_signal, test_tail_exact_zero_and_phase_continues)
{
	static int16_t tail[1000];
	struct hil_source_signal_encoder enc;
	uint32_t phase_before;

	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0, "to scored");
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_TAIL), 0, "to tail");
	phase_before = enc.channels[0].phase;
	zassert_equal(hil_source_signal_render(&enc, 0U, tail, 1000U, sizeof(tail)), 1000,
		      "tail render");
	zassert_false(any_nonzero(tail, 1000U), "tail must be exact PCM zero");
	zassert_equal(enc.channels[0].phase, phase_before + 1000U * LEFT_STEP,
		      "tail keeps phase advancing");
}

/* ── 8. deterministic LC3 bytes ───────────────────────────────────── */

ZTEST(hil_source_signal, test_deterministic_lc3_bytes_all_modes)
{
	static const enum hil_source_mode modes[] = {
		HIL_SOURCE_MODE_MONO,
		HIL_SOURCE_MODE_A,
		HIL_SOURCE_MODE_B,
	};
	uint8_t sdu1[240];
	uint8_t sdu2[240];
	size_t m;
	uint8_t p;
	uint8_t f;

	for (m = 0U; m < sizeof(modes) / sizeof(modes[0]); m++) {
		for (p = 0U; p < 2U; p++) {
			enum hil_source_profile profile =
				p == 0U ? HIL_SOURCE_PROFILE_48_3_1 : HIL_SOURCE_PROFILE_48_4_1;
			struct hil_source_signal_encoder enc1;
			struct hil_source_signal_encoder enc2;
			uint16_t sdu_size = hil_source_profile_sdu_size(profile, modes[m]);

			init_encoder(&enc1, modes[m], profile, TEST_SEED);
			init_encoder(&enc2, modes[m], profile, TEST_SEED);
			for (f = 0U; f < 5U; f++) {
				zassert_equal(
					hil_source_signal_encode_next(&enc1, sdu1, sizeof(sdu1)),
					(int)sdu_size, "enc1 frame %u", f);
				zassert_equal(
					hil_source_signal_encode_next(&enc2, sdu2, sizeof(sdu2)),
					(int)sdu_size, "enc2 frame %u", f);
				zassert_mem_equal(sdu1, sdu2, sdu_size,
						  "mode %zu profile %u frame %u", m, p, f);
			}
		}
	}
}

/* ── 9. Mode A equals Mode B L/R slices ───────────────────────────── */

ZTEST(hil_source_signal, test_mode_a_matches_mode_b_slices)
{
	uint8_t sdu_a_l[240];
	uint8_t sdu_a_r[240];
	uint8_t sdu_b[240];
	uint8_t p;

	for (p = 0U; p < 2U; p++) {
		enum hil_source_profile profile =
			p == 0U ? HIL_SOURCE_PROFILE_48_3_1 : HIL_SOURCE_PROFILE_48_4_1;
		struct hil_source_signal_encoder enc_a_l;
		struct hil_source_signal_encoder enc_a_r;
		struct hil_source_signal_encoder enc_b;
		uint16_t octets = hil_source_profile_octets_per_channel(profile);
		uint8_t f;

		zassert_equal(hil_source_signal_encoder_init(&enc_a_l, HIL_SOURCE_MODE_A, profile,
							     TEST_SEED, HIL_SOURCE_CHANNEL_LEFT,
							     -1),
			      0, "mode_a left init");
		zassert_equal(hil_source_signal_encoder_init(&enc_a_r, HIL_SOURCE_MODE_A, profile,
							     TEST_SEED, HIL_SOURCE_CHANNEL_RIGHT,
							     -1),
			      0, "mode_a right init");
		init_encoder(&enc_b, HIL_SOURCE_MODE_B, profile, TEST_SEED);
		encode_full_preamble(&enc_a_l);
		encode_full_preamble(&enc_a_r);
		encode_full_preamble(&enc_b);
		zassert_equal(hil_source_signal_set_stage(&enc_a_l, HIL_SOURCE_SIGNAL_SCORED), 0);
		zassert_equal(hil_source_signal_set_stage(&enc_a_r, HIL_SOURCE_SIGNAL_SCORED), 0);
		zassert_equal(hil_source_signal_set_stage(&enc_b, HIL_SOURCE_SIGNAL_SCORED), 0);

		for (f = 0U; f < 6U; f++) {
			zassert_equal(
				hil_source_signal_encode_next(&enc_a_l, sdu_a_l, sizeof(sdu_a_l)),
				(int)octets, "a_l frame %u", f);
			zassert_equal(
				hil_source_signal_encode_next(&enc_a_r, sdu_a_r, sizeof(sdu_a_r)),
				(int)octets, "a_r frame %u", f);
			zassert_equal(hil_source_signal_encode_next(&enc_b, sdu_b, sizeof(sdu_b)),
				      (int)(octets * 2U), "b frame %u", f);
			zassert_mem_equal(sdu_a_l, sdu_b, octets,
					  "mode_a left == mode_b L slice (p%u f%u)", p, f);
			zassert_mem_equal(sdu_a_r, sdu_b + octets, octets,
					  "mode_a right == mode_b R slice (p%u f%u)", p, f);
		}
	}
}

/* ── 10. exact SDU lengths and L-before-R packing ─────────────────── */

ZTEST(hil_source_signal, test_sdu_lengths_and_l_before_r)
{
	struct hil_source_signal_encoder enc_l;
	struct hil_source_signal_encoder enc_b;
	uint8_t sdu_l[240];
	uint8_t sdu_b[240];
	uint8_t p;

	for (p = 0U; p < 2U; p++) {
		enum hil_source_profile profile =
			p == 0U ? HIL_SOURCE_PROFILE_48_3_1 : HIL_SOURCE_PROFILE_48_4_1;
		uint16_t octets = hil_source_profile_octets_per_channel(profile);

		zassert_equal(hil_source_signal_encoder_init(&enc_l, HIL_SOURCE_MODE_A, profile,
							     TEST_SEED, HIL_SOURCE_CHANNEL_LEFT,
							     -1),
			      0, "l init");
		init_encoder(&enc_b, HIL_SOURCE_MODE_B, profile, TEST_SEED);
		encode_full_preamble(&enc_l);
		encode_full_preamble(&enc_b);
		zassert_equal(hil_source_signal_set_stage(&enc_l, HIL_SOURCE_SIGNAL_SCORED), 0);
		zassert_equal(hil_source_signal_set_stage(&enc_b, HIL_SOURCE_SIGNAL_SCORED), 0);
		zassert_equal(hil_source_signal_encode_next(&enc_l, sdu_l, sizeof(sdu_l)),
			      (int)octets, "mono/A SDU length");
		zassert_equal(hil_source_signal_encode_next(&enc_b, sdu_b, sizeof(sdu_b)),
			      (int)(octets * 2U), "mode B SDU length");
		zassert_mem_equal(sdu_l, sdu_b, octets, "L packed before R");
	}
}

/* ── 11. decode emitted frames with real liblc3 ───────────────────── */

static void decode_frame(const uint8_t *sdu, uint16_t octets, uint16_t duration_us, int16_t *pcm)
{
	lc3_decoder_mem_48k_t mem;
	lc3_decoder_t dec;
	int ret;

	dec = lc3_setup_decoder(duration_us, HIL_SOURCE_SAMPLE_RATE_HZ, 0, &mem);
	zassert_not_null(dec, "decoder setup");
	ret = lc3_decode(dec, sdu, octets, LC3_PCM_FORMAT_S16, pcm, 1);
	zassert_equal(ret, 0, "decode ret %d", ret);
}

ZTEST(hil_source_signal, test_decode_active_frames)
{
	static int16_t pcm_a[480];
	static int16_t pcm_b[480];
	static int16_t pcm_l[480];
	static int16_t pcm_r[480];
	struct hil_source_signal_encoder enc;
	uint8_t sdu[240];
	uint16_t duration_us;
	uint16_t octets;
	uint16_t samples;
	uint8_t p;

	for (p = 0U; p < 2U; p++) {
		enum hil_source_profile profile =
			p == 0U ? HIL_SOURCE_PROFILE_48_3_1 : HIL_SOURCE_PROFILE_48_4_1;

		duration_us = hil_source_profile_frame_duration_us(profile);
		octets = hil_source_profile_octets_per_channel(profile);
		samples = hil_source_profile_frame_samples(profile);

		init_encoder(&enc, HIL_SOURCE_MODE_MONO, profile, TEST_SEED);
		encode_full_preamble(&enc);
		zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0,
			      "to scored");

		/* Frame 0 decoded from two independently initialized
		 * contexts must be byte-identical. */
		zassert_equal(hil_source_signal_encode_next(&enc, sdu, sizeof(sdu)), (int)octets,
			      "encode mono frame 0");
		decode_frame(sdu, octets, duration_us, pcm_a);
		zassert_true(any_nonzero(pcm_a, samples), "active frame must decode to energy");
		memcpy(pcm_b, pcm_a, sizeof(pcm_b));
		{
			struct hil_source_signal_encoder enc2;

			init_encoder(&enc2, HIL_SOURCE_MODE_MONO, profile, TEST_SEED);
			encode_full_preamble(&enc2);
			zassert_equal(hil_source_signal_set_stage(&enc2, HIL_SOURCE_SIGNAL_SCORED),
				      0, "to scored b");
			zassert_equal(hil_source_signal_encode_next(&enc2, sdu, sizeof(sdu)),
				      (int)octets, "encode mono b frame 0");
			decode_frame(sdu, octets, duration_us, pcm_a);
			zassert_mem_equal(pcm_a, pcm_b, samples * 2U,
					  "independent runs decode identically");
		}

		/* Mode B: independent L and R frames differ. */
		init_encoder(&enc, HIL_SOURCE_MODE_B, profile, TEST_SEED);
		encode_full_preamble(&enc);
		zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0,
			      "to scored b");
		zassert_equal(hil_source_signal_encode_next(&enc, sdu, sizeof(sdu)),
			      (int)(octets * 2U), "encode mode B");
		decode_frame(sdu, octets, duration_us, pcm_l);
		decode_frame(sdu + octets, octets, duration_us, pcm_r);
		zassert_true(any_nonzero(pcm_l, samples), "L has energy");
		zassert_true(any_nonzero(pcm_r, samples), "R has energy");
		zassert_true(memcmp(pcm_l, pcm_r, samples * 2U) != 0,
			     "L and R decoded outputs must differ");
	}
	zassert_equal(lc3_frame_samples(7500, 48000), 360, "7.5 ms samples");
	zassert_equal(lc3_frame_samples(10000, 48000), 480, "10 ms samples");
}

ZTEST(hil_source_signal, test_tail_pcm_zero_at_input_boundary)
{
	static int16_t tail[HIL_SOURCE_PREAMBLE_SEGMENT_SAMPLES];
	struct hil_source_signal_encoder enc;

	init_encoder(&enc, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0, "to scored");
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_TAIL), 0, "to tail");
	zassert_equal(hil_source_signal_render(&enc, 0U, tail, HIL_SOURCE_PREAMBLE_SEGMENT_SAMPLES,
					       sizeof(tail)),
		      (int)HIL_SOURCE_PREAMBLE_SEGMENT_SAMPLES, "tail render");
	/* Tail silence is proven at the exact input PCM boundary: every
	 * sample fed to the encoder is zero.  No lossy-decoder residual
	 * threshold is invented. */
	zassert_false(any_nonzero(tail, HIL_SOURCE_PREAMBLE_SEGMENT_SAMPLES),
		      "tail input PCM must be exactly zero");
}

/* ── 12. atomic failures ──────────────────────────────────────────── */

ZTEST(hil_source_signal, test_failures_are_atomic_and_retry_matches_clean)
{
	struct hil_source_signal_encoder enc;
	struct hil_source_signal_encoder clean;
	uint8_t sdu[240];
	uint8_t sdu_clean[240];
	int16_t pcm[480];
	uint32_t phase_snap;
	uint16_t octets = hil_source_profile_octets_per_channel(HIL_SOURCE_PROFILE_48_4_1);

	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0, "to scored");

	/* Insufficient capacity: nothing changes. */
	phase_snap = enc.channels[0].phase;
	zassert_equal(hil_source_signal_encode_next(&enc, sdu, octets - 1U), -ENOSPC,
		      "capacity one short");
	zassert_equal(enc.channels[0].phase, phase_snap, "phase unchanged");
	zassert_false(enc.unusable, "not unusable");

	/* Null pointers and bad channel indices. */
	zassert_equal(hil_source_signal_encode_next(NULL, sdu, sizeof(sdu)), -EINVAL,
		      "null encoder");
	zassert_equal(hil_source_signal_encode_next(&enc, NULL, sizeof(sdu)), -EINVAL, "null out");
	zassert_equal(hil_source_signal_render(&enc, 9U, pcm, 480U, sizeof(pcm)), -EINVAL,
		      "bad channel index");
	zassert_equal(hil_source_signal_render(&enc, 0U, pcm, 480U, 1U), -ENOSPC,
		      "render capacity");

	/* Retry after the caller errors matches a clean encoder. */
	init_encoder(&clean, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&clean);
	zassert_equal(hil_source_signal_set_stage(&clean, HIL_SOURCE_SIGNAL_SCORED), 0,
		      "clean to scored");
	zassert_equal(hil_source_signal_encode_next(&clean, sdu_clean, sizeof(sdu_clean)),
		      (int)(octets * 2U), "clean encode");
	zassert_equal(hil_source_signal_encode_next(&enc, sdu, sizeof(sdu)), (int)(octets * 2U),
		      "retry encode");
	zassert_mem_equal(sdu, sdu_clean, octets * 2U, "retry bytes match clean encoder");
}

ZTEST(hil_source_signal, test_invalid_transition_atomic)
{
	struct hil_source_signal_encoder enc;
	uint32_t phase_snap;

	init_encoder(&enc, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0, "to scored");
	phase_snap = enc.channels[0].phase;
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_PREAMBLE), -EINVAL,
		      "scored -> preamble");
	zassert_equal(enc.stage, HIL_SOURCE_SIGNAL_SCORED, "stage unchanged");
	zassert_equal(enc.channels[0].phase, phase_snap, "phase unchanged");
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), -EINVAL,
		      "same stage");
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_PREAMBLE), -EINVAL,
		      "preamble stage from scored");
}

ZTEST(hil_source_signal, test_preamble_overrun_atomic)
{
	struct hil_source_signal_encoder enc;
	static int16_t buf[HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES + 8U];
	uint32_t phase_snap;

	init_encoder(&enc, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	phase_snap = enc.channels[0].phase;
	zassert_equal(hil_source_signal_render(&enc, 0U, buf, 69120U + 1U, sizeof(buf)), -EOVERFLOW,
		      "render past preamble");
	zassert_equal(enc.channels[0].phase, phase_snap, "phase unchanged on overrun");
	zassert_equal(enc.channels[0].preamble_rendered, 0U, "preamble counter unchanged");

	/* A huge sample count must be rejected atomically (subtraction
	 * bounds, no wrap) without attempting a huge allocation. */
	zassert_equal(hil_source_signal_render(&enc, 0U, buf, UINT32_MAX, SIZE_MAX), -EOVERFLOW,
		      "huge count rejected");
	zassert_equal(enc.channels[0].phase, phase_snap, "phase unchanged on huge overrun");
	zassert_equal(enc.channels[0].preamble_rendered, 0U, "counter unchanged on huge overrun");
}

/* ── stage gate: scored entry requires the exact preamble ─────────── */

ZTEST(hil_source_signal, test_stage_gate_requires_exact_preamble)
{
	struct hil_source_signal_encoder enc;
	static int16_t chunk[480];
	uint32_t phase_snap;
	uint32_t stage_snap;

	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	zassert_equal(hil_source_signal_render(&enc, 0U, chunk, 100U, sizeof(chunk)), 100,
		      "partial preamble");
	phase_snap = enc.channels[0].phase;
	stage_snap = enc.stage;
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), -EINVAL,
		      "early scored transition rejected");
	zassert_equal(enc.stage, stage_snap, "stage unchanged");
	zassert_equal(enc.channels[0].phase, phase_snap, "phase unchanged");

	/* Exact preamble on every channel then succeeds. */
	init_encoder(&enc, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0,
		      "scored after exact preamble");
	zassert_equal(enc.stage, HIL_SOURCE_SIGNAL_SCORED, "now scored");
}

/* ── 13. 32-bit huge-count capacity guard ─────────────────────────── */

ZTEST(hil_source_signal, test_huge_count_atomic_scored_and_tail)
{
	struct hil_source_signal_encoder enc;
	struct hil_source_signal_encoder snap;
	int16_t canary[8];
	uint8_t i;

	init_encoder(&enc, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, TEST_SEED);
	render_full_preamble(&enc);
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_SCORED), 0, "to scored");

	/* Scored huge count: rejected before any multiplication can wrap
	 * on 32-bit targets; encoder and canary output stay unchanged. */
	for (i = 0U; i < sizeof(canary) / sizeof(canary[0]); i++) {
		canary[i] = (int16_t)0x3C3C;
	}
	snap = enc;
	zassert_equal(hil_source_signal_render(&enc, 0U, canary, UINT32_MAX, sizeof(canary)),
		      -ENOSPC, "scored huge count");
	zassert_mem_equal(&enc, &snap, sizeof(enc), "scored huge count mutated encoder");
	for (i = 0U; i < sizeof(canary) / sizeof(canary[0]); i++) {
		zassert_equal(canary[i], (int16_t)0x3C3C, "scored canary %u", i);
	}

	/* Tail huge count: same fail-closed contract. */
	zassert_equal(hil_source_signal_set_stage(&enc, HIL_SOURCE_SIGNAL_TAIL), 0, "to tail");
	for (i = 0U; i < sizeof(canary) / sizeof(canary[0]); i++) {
		canary[i] = (int16_t)0x5A5A;
	}
	snap = enc;
	zassert_equal(hil_source_signal_render(&enc, 0U, canary, UINT32_MAX, sizeof(canary)),
		      -ENOSPC, "tail huge count");
	zassert_mem_equal(&enc, &snap, sizeof(enc), "tail huge count mutated encoder");
	for (i = 0U; i < sizeof(canary) / sizeof(canary[0]); i++) {
		zassert_equal(canary[i], (int16_t)0x5A5A, "tail canary %u", i);
	}
}
