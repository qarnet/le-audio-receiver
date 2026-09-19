/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * T2B — production decoder validation and routing.
 *
 * Compiles and executes the REAL src/audio_decode.c and src/audio_stats.c
 * against deterministic 48 kHz LC3 fixtures (tests/fixtures/lc3) and the
 * real NCS v3.3.0 liblc3.  See docs/testing/t2-audio-pipeline-tests.md.
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>

#include "audio_decode.h"
#include "audio_stats.h"
#include "decode_pcm_limits.h"
#include "lc3_wrap.h"
#include "pcm_oracle.h"

/* ── checked-in fixtures (embedded at build time) ─────────────────── */

static const uint8_t mono_7p5_lc3[] = {
#include "mono_48k_7p5ms_60b_lc3.inc"
};
static const uint8_t mono_7p5_pcm[] = {
#include "mono_48k_7p5ms_60b_pcm.inc"
};
static const uint8_t mono_10ms_lc3[] = {
#include "mono_48k_10ms_60b_lc3.inc"
};
static const uint8_t mono_10ms_pcm[] = {
#include "mono_48k_10ms_60b_pcm.inc"
};
static const uint8_t modeb_7p5_lc3[] = {
#include "modeb_48k_7p5ms_60b_lc3.inc"
};
static const uint8_t modeb_7p5_pcm[] = {
#include "modeb_48k_7p5ms_60b_pcm.inc"
};
static const uint8_t modeb_10ms_lc3[] = {
#include "modeb_48k_10ms_60b_lc3.inc"
};
static const uint8_t modeb_10ms_pcm[] = {
#include "modeb_48k_10ms_60b_pcm.inc"
};

/* ── test helpers ────────────────────────────────────────────────── */

#define GUARD_VAL 0x5A5A

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	audio_stats_reset();
	lc3_wrap_reset();
}

ZTEST_SUITE(decode, NULL, NULL, reset_before_each, NULL, NULL);

static void fill_guards(int16_t *buf, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		buf[i] = GUARD_VAL;
	}
}

static void assert_guards(const int16_t *buf, size_t from, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		zassert_equal(buf[from + i], GUARD_VAL, "guard %zu corrupted", i);
	}
}

static void assert_portable_channel(const char *tag, int channel, const int16_t *actual,
				    const uint8_t *reference, int samples_per_ch)
{
	struct pcm_oracle oracle;
	struct pcm_oracle_metrics metrics;
	const struct pcm_oracle_limits limits = {
		.min_samples = (uint32_t)samples_per_ch,
		.max_abs_error = DECODE_PCM_MAX_ABS_ERROR,
		.max_rms_error = DECODE_PCM_MAX_RMS_ERROR,
		.min_correlation_q15 = DECODE_PCM_MIN_CORRELATION_Q15,
	};
	enum pcm_oracle_result result;

	zassert_ok(pcm_oracle_init(&oracle), "%s channel %d init", tag, channel);
	zassert_ok(
		pcm_oracle_accumulate(&oracle, actual, 2U, reference, 4U, (uint32_t)samples_per_ch),
		"%s channel %d accumulate", tag, channel);
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "%s channel %d finalize", tag, channel);
	zassert_equal(metrics.samples, (uint32_t)samples_per_ch, "%s channel %d samples", tag,
		      channel);
	zassert_equal(metrics.frames, 1U, "%s channel %d frames", tag, channel);
	zassert_ok(pcm_oracle_evaluate(&metrics, &limits, &result), "%s channel %d evaluate", tag,
		   channel);
	zassert_equal(result, PCM_ORACLE_RESULT_PASS,
		      "%s channel %d result=%s observed max=%u rms=%u corr=%d limits max=%u rms=%u "
		      "corr=%d",
		      tag, channel, pcm_oracle_result_name(result), metrics.max_abs_error,
		      metrics.rms_error, metrics.correlation_q15, limits.max_abs_error,
		      limits.max_rms_error, limits.min_correlation_q15);
}

static void assert_portable_stereo(const char *tag, const int16_t *actual, const uint8_t *reference,
				   size_t reference_len, int samples_per_ch)
{
	zassert_equal(reference_len, (size_t)(4 * samples_per_ch), "fixture size %s", tag);
	assert_portable_channel(tag, 0, actual, reference, samples_per_ch);
	assert_portable_channel(tag, 1, actual + 1, reference + 2, samples_per_ch);
}

/* Full portable fixture check for one decode. */
static void assert_fixture(const char *tag, const uint8_t *lc3, size_t lc3_len, const uint8_t *pcm,
			   size_t pcm_len, int chan_count, int frame_us)
{
	int samples_per_ch = (frame_us * 48000) / 1000000;
	struct audio_decode_ctx ctx;
	int16_t out[2 * 480 + 4];

	zassert_equal(lc3_len, (size_t)(chan_count * 60), "fixture size %s", tag);

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, chan_count, 48000, frame_us, 1), "%s config", tag);

	fill_guards(out, 2 * 480 + 4);
	zassert_ok(audio_decode_sdu(&ctx, lc3, lc3_len, true, out), "%s decode", tag);

	assert_portable_stereo(tag, out, pcm, pcm_len, samples_per_ch);

	/* Sample count and untouched guard values after capacity. */
	assert_guards(out, 2 * samples_per_ch, 4);

	if (chan_count == 1) {
		for (int i = 0; i < samples_per_ch; i++) {
			zassert_equal(out[2 * i], out[2 * i + 1], "%s mono L==R at %d", tag, i);
		}
	} else {
		bool differs = false;

		for (int i = 0; i < samples_per_ch; i++) {
			if (out[2 * i] != out[2 * i + 1]) {
				differs = true;
				break;
			}
		}
		zassert_true(differs, "%s Mode B L/R differ", tag);
	}
}

/* ── routing ─────────────────────────────────────────────────────── */

ZTEST(decode, test_mono_to_stereo_separate_buffers)
{
	int16_t mono[4] = {100, 200, 300, 400};
	int16_t stereo_out[8] = {0};

	audio_decode_mono_to_stereo(mono, stereo_out, 4);

	zassert_equal(stereo_out[0], 100, "L[0]");
	zassert_equal(stereo_out[1], 100, "R[0] = L[0]");
	zassert_equal(stereo_out[2], 200, "L[1]");
	zassert_equal(stereo_out[3], 200, "R[1] = L[1]");
	zassert_equal(stereo_out[4], 300, "L[2]");
	zassert_equal(stereo_out[5], 300, "R[2] = L[2]");
	zassert_equal(stereo_out[6], 400, "L[3]");
	zassert_equal(stereo_out[7], 400, "R[3] = L[3]");
}

ZTEST(decode, test_mono_to_stereo_inplace_overlap_safe)
{
	/* Same base: the old forward loop overwrote mono[1] with mono[0]
	 * before it was read.  In-place expansion must stay exact.
	 */
	int16_t buf[8] = {100, 200, 300, 400, 0, 0, 0, 0};

	audio_decode_mono_to_stereo(buf, buf, 4);

	zassert_equal(buf[0], 100, "L[0]");
	zassert_equal(buf[1], 100, "R[0]");
	zassert_equal(buf[2], 200, "L[1]");
	zassert_equal(buf[3], 200, "R[1]");
	zassert_equal(buf[4], 300, "L[2]");
	zassert_equal(buf[5], 300, "R[2]");
	zassert_equal(buf[6], 400, "L[3]");
	zassert_equal(buf[7], 400, "R[3]");
}

ZTEST(decode, test_mono_to_stereo_zero_samples)
{
	int16_t buf[4] = {GUARD_VAL, GUARD_VAL, GUARD_VAL, GUARD_VAL};

	audio_decode_mono_to_stereo(buf, buf, 0);

	assert_guards(buf, 0, 4);
}

ZTEST(decode, test_interleave)
{
	int16_t l[4] = {10, 20, 30, 40};
	int16_t r[4] = {15, 25, 35, 45};
	int16_t stereo_out[8] = {0};

	audio_decode_interleave(l, r, stereo_out, 4);

	zassert_equal(stereo_out[0], 10, "L[0]");
	zassert_equal(stereo_out[1], 15, "R[0]");
	zassert_equal(stereo_out[2], 20, "L[1]");
	zassert_equal(stereo_out[3], 25, "R[1]");
	zassert_equal(stereo_out[4], 30, "L[2]");
	zassert_equal(stereo_out[5], 35, "R[2]");
	zassert_equal(stereo_out[6], 40, "L[3]");
	zassert_equal(stereo_out[7], 45, "R[3]");
}

ZTEST(decode, test_interleave_zero_samples)
{
	int16_t buf[4] = {GUARD_VAL, GUARD_VAL, GUARD_VAL, GUARD_VAL};

	audio_decode_interleave(buf, buf, buf, 0);

	assert_guards(buf, 0, 4);
}

/* ── configuration ───────────────────────────────────────────────── */

ZTEST(decode, test_config_mono_10ms)
{
	struct audio_decode_ctx ctx;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");
	zassert_equal(ctx.chan_count, 1, "chan_count");
	zassert_equal(ctx.samples_per_ch, 480, "spc");
	zassert_equal(ctx.frames_per_sdu, 1, "fps");
	zassert_not_null(ctx.decoder, "decoder");
	zassert_is_null(ctx.decoder_r, "mono has no right decoder");
}

ZTEST(decode, test_config_mono_7p5ms)
{
	struct audio_decode_ctx ctx;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 7500, 1), "config");
	zassert_equal(ctx.samples_per_ch, 360, "spc");
	zassert_not_null(ctx.decoder, "decoder");
	zassert_is_null(ctx.decoder_r, "mono has no right decoder");
}

ZTEST(decode, test_config_modeb_10ms)
{
	struct audio_decode_ctx ctx;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config");
	zassert_equal(ctx.chan_count, 2, "chan_count");
	zassert_equal(ctx.samples_per_ch, 480, "spc");
	zassert_not_null(ctx.decoder, "decoder");
	zassert_not_null(ctx.decoder_r, "right decoder");
	zassert_not_equal(ctx.decoder, ctx.decoder_r, "independent decoders");
}

ZTEST(decode, test_config_modeb_7p5ms)
{
	struct audio_decode_ctx ctx;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 7500, 1), "config");
	zassert_equal(ctx.samples_per_ch, 360, "spc");
	zassert_not_null(ctx.decoder, "decoder");
	zassert_not_null(ctx.decoder_r, "right decoder");
}

ZTEST(decode, test_config_rejects)
{
	struct audio_decode_ctx ctx;
	int ret;

	memset(&ctx, 0, sizeof(ctx));

	/* channel count other than 1 or 2 */
	ret = audio_decode_config(NULL, 1, 48000, 10000, 1);
	zassert_equal(ret, -EINVAL, "null ctx");
	ret = audio_decode_config(&ctx, 0, 48000, 10000, 1);
	zassert_equal(ret, -EINVAL, "chan 0");
	ret = audio_decode_config(&ctx, 3, 48000, 10000, 1);
	zassert_equal(ret, -EINVAL, "chan 3");
	/* frequency other than 48000 */
	ret = audio_decode_config(&ctx, 1, 44100, 10000, 1);
	zassert_equal(ret, -EINVAL, "44k1");
	ret = audio_decode_config(&ctx, 1, 96000, 10000, 1);
	zassert_equal(ret, -EINVAL, "96k");
	/* frame duration other than 7500/10000 */
	ret = audio_decode_config(&ctx, 1, 48000, 5000, 1);
	zassert_equal(ret, -EINVAL, "5ms");
	ret = audio_decode_config(&ctx, 1, 48000, 20000, 1);
	zassert_equal(ret, -EINVAL, "20ms");
	/* frames_per_sdu other than exactly 1 */
	ret = audio_decode_config(&ctx, 1, 48000, 10000, 2);
	zassert_equal(ret, -EINVAL, "fps 2");
}

ZTEST(decode, test_config_rejection_leaves_ctx_reset)
{
	struct audio_decode_ctx ctx;

	memset(&ctx, 0, sizeof(ctx));

	zassert_equal(audio_decode_config(&ctx, 3, 48000, 10000, 1), -EINVAL, "reject");
	zassert_is_null(ctx.decoder, "decoder reset");
	zassert_is_null(ctx.decoder_r, "decoder_r reset");
	zassert_equal(ctx.chan_count, 0, "chan reset");
	zassert_equal(ctx.samples_per_ch, 0, "spc reset");
	zassert_equal(ctx.frames_per_sdu, 0, "fps reset");

	/* Rejection must not have touched liblc3 (decoder never set up). */
	zassert_equal(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, (int16_t[960]){0}), -EINVAL,
		      "unconfigured sdu rejected");
}

/* ── reset semantics ─────────────────────────────────────────────── */

ZTEST(decode, test_reset_null_harmless)
{
	audio_decode_reset(NULL);
	audio_decode_reset(NULL);
}

ZTEST(decode, test_reset_clears_everything)
{
	struct audio_decode_ctx ctx;
	int16_t out[960];

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config");

	audio_decode_reset(&ctx);

	zassert_is_null(ctx.decoder, "decoder");
	zassert_is_null(ctx.decoder_r, "decoder_r");
	zassert_equal(ctx.chan_count, 0, "chan");
	zassert_equal(ctx.samples_per_ch, 0, "spc");
	zassert_equal(ctx.frames_per_sdu, 0, "fps");

	memset(out, 0, sizeof(out));
	zassert_equal(audio_decode_sdu(&ctx, modeb_10ms_lc3, 120, true, out), -EINVAL,
		      "reset ctx rejects sdu");
}

ZTEST(decode, test_reconfigure_after_reset)
{
	struct audio_decode_ctx ctx;
	int16_t out1[960];

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");
	audio_decode_reset(&ctx);
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "reconfigure");

	memset(out1, 0, sizeof(out1));
	zassert_ok(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, out1), "decode");
	assert_portable_stereo("mono 10 ms after reset", out1, mono_10ms_pcm, sizeof(mono_10ms_pcm),
			       480);
}

/* ── portable fixtures ───────────────────────────────────────────── */

ZTEST(decode, test_fixture_mono_7p5ms)
{
	assert_fixture("mono 7.5 ms", mono_7p5_lc3, sizeof(mono_7p5_lc3), mono_7p5_pcm,
		       sizeof(mono_7p5_pcm), 1, 7500);
}

ZTEST(decode, test_fixture_mono_10ms)
{
	assert_fixture("mono 10 ms", mono_10ms_lc3, sizeof(mono_10ms_lc3), mono_10ms_pcm,
		       sizeof(mono_10ms_pcm), 1, 10000);
}

ZTEST(decode, test_fixture_modeb_7p5ms)
{
	assert_fixture("Mode B 7.5 ms", modeb_7p5_lc3, sizeof(modeb_7p5_lc3), modeb_7p5_pcm,
		       sizeof(modeb_7p5_pcm), 2, 7500);
}

ZTEST(decode, test_fixture_modeb_10ms)
{
	assert_fixture("Mode B 10 ms", modeb_10ms_lc3, sizeof(modeb_10ms_lc3), modeb_10ms_pcm,
		       sizeof(modeb_10ms_pcm), 2, 10000);
}

ZTEST(decode, test_fixture_reconfigure_repeat)
{
	/* Reset + reconfigure + each decode must pass its fixture comparison. */
	struct audio_decode_ctx ctx;
	int16_t out1[960];
	int16_t out2[960];

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");
	zassert_ok(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, out1), "decode");
	assert_portable_stereo("mono 10 ms first", out1, mono_10ms_pcm, sizeof(mono_10ms_pcm), 480);

	audio_decode_reset(&ctx);
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 7500, 1), "reconfig modeb");
	zassert_ok(audio_decode_sdu(&ctx, modeb_7p5_lc3, 120, true, out2), "decode modeb");
	assert_portable_stereo("Mode B 7.5 ms", out2, modeb_7p5_pcm, sizeof(modeb_7p5_pcm), 360);

	audio_decode_reset(&ctx);
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "reconfig mono");
	memset(out2, 0, sizeof(out2));
	zassert_ok(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, out2), "decode repeat");
	assert_portable_stereo("mono 10 ms repeat", out2, mono_10ms_pcm, sizeof(mono_10ms_pcm),
			       480);
}

/* ── SDU validation ──────────────────────────────────────────────── */

ZTEST(decode, test_sdu_null_and_unconfigured)
{
	struct audio_decode_ctx ctx;
	int16_t out[960];

	memset(&ctx, 0, sizeof(ctx));

	zassert_equal(audio_decode_sdu(NULL, mono_10ms_lc3, 60, true, out), -EINVAL, "null ctx");
	zassert_equal(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, NULL), -EINVAL, "null out");
	zassert_equal(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, out), -EINVAL,
		      "unconfigured");
	zassert_equal(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, false, out), -EINVAL,
		      "unconfigured PLC");
}

ZTEST(decode, test_sdu_modeb_missing_right_decoder)
{
	struct audio_decode_ctx ctx;
	int16_t out[960];

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config");

	/* White-box: corrupt the right decoder pointer. */
	ctx.decoder_r = NULL;
	zassert_equal(audio_decode_sdu(&ctx, modeb_10ms_lc3, 120, true, out), -EINVAL,
		      "missing right decoder");
}

ZTEST(decode, test_sdu_stored_shape_unsupported)
{
	struct audio_decode_ctx ctx;
	int16_t out[960];

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");

	ctx.chan_count = 3;
	zassert_equal(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, out), -EINVAL, "chan 3");

	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "reconfig");
	ctx.samples_per_ch = 123;
	zassert_equal(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, out), -EINVAL, "bad spc");

	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "reconfig");
	ctx.frames_per_sdu = 2;
	zassert_equal(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, out), -EINVAL, "fps 2");
}

ZTEST(decode, test_sdu_valid_null_data)
{
	struct audio_decode_ctx ctx;
	int16_t out[960];

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");
	zassert_equal(audio_decode_sdu(&ctx, NULL, 60, true, out), -EINVAL,
		      "valid=true with NULL data");
}

static void assert_rejected_guard_preserved(struct audio_decode_ctx *ctx, const uint8_t *data,
					    size_t len, int tag)
{
	int16_t out[2 * 480 + 4];

	fill_guards(out, 2 * 480 + 4);
	zassert_equal(audio_decode_sdu(ctx, data, len, true, out), -EINVAL, "reject tag %d", tag);
	assert_guards(out, 0, 2 * 480 + 4);
}

ZTEST(decode, test_sdu_malformed_lengths_preserve_output)
{
	struct audio_decode_ctx ctx;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");

	/* zero length */
	assert_rejected_guard_preserved(&ctx, mono_10ms_lc3, 0, 1);
	/* too short (below liblc3 basic 20..400) */
	assert_rejected_guard_preserved(&ctx, mono_10ms_lc3, 19, 2);
	/* too long */
	assert_rejected_guard_preserved(&ctx, mono_10ms_lc3, 401, 3);
	/* oversized input cannot truncate (valid frame bound) */
	assert_rejected_guard_preserved(&ctx, mono_10ms_lc3, 4000, 4);

	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "reconfig modeb");

	/* odd Mode B length not divisible by channel count */
	assert_rejected_guard_preserved(&ctx, modeb_10ms_lc3, 61, 5);
	/* per-channel outside range */
	assert_rejected_guard_preserved(&ctx, modeb_10ms_lc3, 30, 6);
	assert_rejected_guard_preserved(&ctx, modeb_10ms_lc3, 802, 7);
}

ZTEST(decode, test_rejection_then_valid_fixture_decode)
{
	/* Rejected input must leave the decoder state untouched: a valid
	 * fixture decode right after still passes portable comparison.
	 */
	struct audio_decode_ctx ctx;
	int16_t out[960];

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config");

	memset(out, 0, sizeof(out));
	zassert_equal(audio_decode_sdu(&ctx, modeb_10ms_lc3, 61, true, out), -EINVAL, "reject");
	zassert_equal(audio_decode_sdu(&ctx, modeb_10ms_lc3, 401, true, out), -EINVAL, "reject");

	memset(out, 0, sizeof(out));
	zassert_ok(audio_decode_sdu(&ctx, modeb_10ms_lc3, 120, true, out), "decode");
	assert_portable_stereo("Mode B 10 ms after reject", out, modeb_10ms_pcm,
			       sizeof(modeb_10ms_pcm), 480);
}

/* ── PLC ─────────────────────────────────────────────────────────── */

ZTEST(decode, test_plc_mono_10ms)
{
	struct audio_decode_ctx ctx;
	int16_t out[2 * 480 + 4];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");

	fill_guards(out, 2 * 480 + 4);
	zassert_ok(audio_decode_sdu(&ctx, NULL, 60, false, out), "PLC ret 0");
	assert_guards(out, 2 * 480, 4);

	st = audio_stats_get();
	zassert_equal(st.plc_frames, 1, "plc 1");
	zassert_equal(st.total_frames, 1, "total 1");
	zassert_equal(st.decode_errors, 0, "no errors");
}

ZTEST(decode, test_plc_mono_7p5ms)
{
	struct audio_decode_ctx ctx;
	int16_t out[2 * 480 + 4];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 7500, 1), "config");

	fill_guards(out, 2 * 480 + 4);
	zassert_ok(audio_decode_sdu(&ctx, NULL, 60, false, out), "PLC ret 0");
	assert_guards(out, 2 * 360, 4);

	st = audio_stats_get();
	zassert_equal(st.plc_frames, 1, "plc 1");
	zassert_equal(st.total_frames, 1, "total 1");
}

ZTEST(decode, test_plc_modeb_10ms)
{
	struct audio_decode_ctx ctx;
	int16_t out[2 * 480 + 4];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config");

	fill_guards(out, 2 * 480 + 4);
	zassert_ok(audio_decode_sdu(&ctx, NULL, 120, false, out), "PLC ret 0");
	assert_guards(out, 2 * 480, 4);

	/* Both channel decoders conceal: PLC counted once per invocation. */
	st = audio_stats_get();
	zassert_equal(st.plc_frames, 2, "plc 2");
	zassert_equal(st.total_frames, 2, "total 2");
	zassert_equal(st.decode_errors, 0, "no errors");
}

ZTEST(decode, test_plc_modeb_7p5ms)
{
	struct audio_decode_ctx ctx;
	int16_t out[2 * 480 + 4];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 7500, 1), "config");

	fill_guards(out, 2 * 480 + 4);
	zassert_ok(audio_decode_sdu(&ctx, NULL, 120, false, out), "PLC ret 0");
	assert_guards(out, 2 * 360, 4);

	st = audio_stats_get();
	zassert_equal(st.plc_frames, 2, "plc 2");
	zassert_equal(st.total_frames, 2, "total 2");
}

ZTEST(decode, test_plc_does_not_dereference_frame_data)
{
	/* valid=false with a bogus non-null pointer: production must pass
	 * NULL to liblc3 and never dereference the supplied pointer.
	 */
	struct audio_decode_ctx ctx;
	int16_t out[960];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");

	zassert_ok(audio_decode_sdu(&ctx, (const uint8_t *)0x1, 60, false, out),
		   "PLC with bogus pointer");
	st = audio_stats_get();
	zassert_equal(st.plc_frames, 1, "plc 1");
}

/* ── malformed real LC3 data (installed liblc3 semantics) ────────── */

ZTEST(decode, test_malformed_valid_length_data_is_plc)
{
	/* NCS v3.3.0 liblc3 1.1.2 returns 1 (PLC) for a malformed bitstream
	 * of valid length — hard negatives only occur for parameter errors,
	 * which audio_decode_sdu() pre-validates.  This locks the real
	 * accounting: malformed data is concealed, never counted as a
	 * decode error, and never reported as a failure.
	 */
	struct audio_decode_ctx ctx;
	uint8_t garbage[340];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	memset(garbage, 0xFF, sizeof(garbage));

	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");
	zassert_ok(audio_decode_sdu(&ctx, garbage, sizeof(garbage), true, (int16_t[960]){0}),
		   "malformed data handled without failure");

	st = audio_stats_get();
	zassert_equal(st.plc_frames, 1, "malformed -> PLC once");
	zassert_equal(st.total_frames, 1, "malformed -> total once");
	zassert_equal(st.decode_errors, 0, "malformed is not a decode error");
}

/* ── hard decode failure accounting (injected via linker wrap) ───── */

ZTEST(decode, test_mono_hard_failure_accounting)
{
	struct audio_decode_ctx ctx;
	int16_t out[960];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");

	lc3_wrap_fail_decoder(ctx.decoder);
	zassert_equal(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, out), -EBADMSG,
		      "hard failure propagated");
	zassert_equal(lc3_wrap_invocation_count(), 1, "one decoder invocation");

	st = audio_stats_get();
	zassert_equal(st.decode_errors, 1, "error counted once");
	zassert_equal(st.total_frames, 0, "hard failure not counted as total");
	zassert_equal(st.plc_frames, 0, "no PLC");
}

ZTEST(decode, test_modeb_left_failure_counts_and_aligns)
{
	struct audio_decode_ctx ctx;
	int16_t out[960];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config");

	/* Left fails; the right decoder must still be invoked exactly once
	 * so independent decoder state stays aligned.
	 */
	lc3_wrap_fail_decoder(ctx.decoder);
	zassert_equal(audio_decode_sdu(&ctx, modeb_10ms_lc3, 120, true, out), -EBADMSG,
		      "hard failure propagated");
	zassert_equal(lc3_wrap_invocation_count(), 2, "both decoders invoked");

	st = audio_stats_get();
	zassert_equal(st.decode_errors, 1, "left error once");
	zassert_equal(st.total_frames, 1, "right success counted");
}

ZTEST(decode, test_modeb_right_failure_accounting)
{
	struct audio_decode_ctx ctx;
	int16_t out[960];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config");

	lc3_wrap_fail_decoder(ctx.decoder_r);
	zassert_equal(audio_decode_sdu(&ctx, modeb_10ms_lc3, 120, true, out), -EBADMSG,
		      "hard failure propagated");
	zassert_equal(lc3_wrap_invocation_count(), 2, "both decoders invoked");

	st = audio_stats_get();
	zassert_equal(st.decode_errors, 1, "right error once");
	zassert_equal(st.total_frames, 1, "left success counted");
}

ZTEST(decode, test_modeb_both_failures)
{
	struct audio_decode_ctx ctx;
	int16_t out[960];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config");

	lc3_wrap_fail_decoder(ctx.decoder);
	lc3_wrap_fail_decoder(ctx.decoder_r);
	zassert_equal(audio_decode_sdu(&ctx, modeb_10ms_lc3, 120, true, out), -EBADMSG,
		      "hard failure propagated");
	zassert_equal(lc3_wrap_invocation_count(), 2, "both decoders invoked");

	st = audio_stats_get();
	zassert_equal(st.decode_errors, 2, "one error per failed invocation");
	zassert_equal(st.total_frames, 0, "no success");
}

/* ── stats coupling on success paths ─────────────────────────────── */

ZTEST(decode, test_stats_mono_success)
{
	struct audio_decode_ctx ctx;
	int16_t out[960];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");
	zassert_ok(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, out), "decode");

	st = audio_stats_get();
	zassert_equal(st.total_frames, 1, "total 1");
	zassert_equal(st.plc_frames, 0, "no plc");
	zassert_equal(st.decode_errors, 0, "no errors");
}

ZTEST(decode, test_stats_modeb_success_counts_both)
{
	struct audio_decode_ctx ctx;
	int16_t out[960];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config");
	zassert_ok(audio_decode_sdu(&ctx, modeb_10ms_lc3, 120, true, out), "decode");

	st = audio_stats_get();
	zassert_equal(st.total_frames, 2, "both channel decoders counted");
	zassert_equal(st.plc_frames, 0, "no plc");
	zassert_equal(st.decode_errors, 0, "no errors");
}

/* ── size_t-before-int boundary validation ───────────────────────── */

static void assert_huge_rejected(struct audio_decode_ctx *ctx, const uint8_t *data, size_t len,
				 bool valid, int tag)
{
	int16_t out[2 * 480 + 4];

	fill_guards(out, 2 * 480 + 4);
	zassert_equal(audio_decode_sdu(ctx, data, len, valid, out), -EINVAL,
		      "reject tag %d (len=%zu)", tag, len);
	assert_guards(out, 0, 2 * 480 + 4);
}

ZTEST(decode, test_sdu_huge_lengths_rejected)
{
	struct audio_decode_ctx ctx;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config mono");

	/* valid and PLC calls must reject lengths above INT_MAX before any
	 * narrowing cast; output guards stay untouched.
	 */
	assert_huge_rejected(&ctx, mono_10ms_lc3, SIZE_MAX, true, 1);
	assert_huge_rejected(&ctx, mono_10ms_lc3, (size_t)INT_MAX + 1, true, 2);
	assert_huge_rejected(&ctx, mono_10ms_lc3, SIZE_MAX, false, 3);
	assert_huge_rejected(&ctx, mono_10ms_lc3, (size_t)INT_MAX + 1, false, 4);

	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config modeb");

	assert_huge_rejected(&ctx, modeb_10ms_lc3, SIZE_MAX, true, 5);
	assert_huge_rejected(&ctx, modeb_10ms_lc3, (size_t)INT_MAX + 1, true, 6);
	assert_huge_rejected(&ctx, modeb_10ms_lc3, SIZE_MAX, false, 7);
	assert_huge_rejected(&ctx, modeb_10ms_lc3, (size_t)INT_MAX + 1, false, 8);
}

ZTEST(decode, test_sdu_huge_rejection_then_fixture)
{
	/* Rejection of oversized lengths must leave decoder state untouched. */
	struct audio_decode_ctx ctx;
	int16_t out[960];

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");

	memset(out, 0, sizeof(out));
	zassert_equal(audio_decode_sdu(&ctx, mono_10ms_lc3, SIZE_MAX, true, out), -EINVAL,
		      "reject huge");
	zassert_equal(audio_decode_sdu(&ctx, mono_10ms_lc3, (size_t)INT_MAX + 1, false, out),
		      -EINVAL, "reject huge plc");

	memset(out, 0, sizeof(out));
	zassert_ok(audio_decode_sdu(&ctx, mono_10ms_lc3, 60, true, out), "decode");
	assert_portable_stereo("mono 10 ms after huge reject", out, mono_10ms_pcm,
			       sizeof(mono_10ms_pcm), 480);
}

/* ── PLC length semantics ────────────────────────────────────────── */

ZTEST(decode, test_plc_zero_length_mono)
{
	/* BSim startup frames arrive with length 0: must stay supported. */
	struct audio_decode_ctx ctx;
	int16_t out[2 * 480 + 4];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config");

	fill_guards(out, 2 * 480 + 4);
	zassert_ok(audio_decode_sdu(&ctx, NULL, 0, false, out), "zero-length PLC ret 0");
	assert_guards(out, 2 * 480, 4);

	st = audio_stats_get();
	zassert_equal(st.plc_frames, 1, "plc 1");
	zassert_equal(st.total_frames, 1, "total 1");
}

ZTEST(decode, test_plc_zero_length_modeb)
{
	struct audio_decode_ctx ctx;
	int16_t out[2 * 480 + 4];
	struct audio_stats st;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config");

	fill_guards(out, 2 * 480 + 4);
	zassert_ok(audio_decode_sdu(&ctx, NULL, 0, false, out), "zero-length Mode B PLC");
	assert_guards(out, 2 * 480, 4);

	st = audio_stats_get();
	zassert_equal(st.plc_frames, 2, "both channel decoders conceal");
	zassert_equal(st.total_frames, 2, "total 2");
}

ZTEST(decode, test_plc_shape_rejections)
{
	/* Nonzero PLC lengths must form a valid divisible per-channel shape. */
	struct audio_decode_ctx ctx;

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 1, 48000, 10000, 1), "config mono");

	assert_huge_rejected(&ctx, NULL, 19, false, 1);  /* too short */
	assert_huge_rejected(&ctx, NULL, 401, false, 2); /* too long */

	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config modeb");

	assert_huge_rejected(&ctx, NULL, 61, false, 3);  /* odd Mode B PLC length */
	assert_huge_rejected(&ctx, NULL, 30, false, 4);  /* per-channel 15: too short */
	assert_huge_rejected(&ctx, NULL, 802, false, 5); /* per-channel 401: too long */
}

ZTEST(decode, test_plc_shape_rejection_then_valid_fixture)
{
	/* Rejected PLC shapes leave decoder state untouched. */
	struct audio_decode_ctx ctx;
	int16_t out[960];

	memset(&ctx, 0, sizeof(ctx));
	zassert_ok(audio_decode_config(&ctx, 2, 48000, 10000, 1), "config");

	memset(out, 0, sizeof(out));
	zassert_equal(audio_decode_sdu(&ctx, NULL, 61, false, out), -EINVAL, "odd PLC reject");
	zassert_equal(audio_decode_sdu(&ctx, NULL, 30, false, out), -EINVAL, "short PLC reject");

	memset(out, 0, sizeof(out));
	zassert_ok(audio_decode_sdu(&ctx, modeb_10ms_lc3, 120, true, out), "decode");
	assert_portable_stereo("Mode B 10 ms after PLC reject", out, modeb_10ms_pcm,
			       sizeof(modeb_10ms_pcm), 480);
}
