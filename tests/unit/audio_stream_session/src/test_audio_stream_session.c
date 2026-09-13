/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Direct production-source tests for src/audio_stream_session.c (R6).
 *
 * Compiles the real session + audio_decode + audio_modea + audio_iso_seq +
 * audio_stats + audio_perf + audio_volume against a faithful fake sink and
 * fake observer, with the checked-in 48 kHz LC3 fixtures and a
 * linker-wrapped lc3_decode for hard-failure injection.  All assertions
 * are on public-boundary observable behavior (pushes, hashes, counters,
 * stats, observer events, return values, admission state) — never on
 * private session fields or helper-call counts.
 */

#include <zephyr/ztest.h>

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "audio_stream_session.h"
#include "audio_stats.h"
#include "audio_perf.h"
#include "audio_sink.h"
#include "fake_volume.h"
#include "lc3_wrap.h"
#include "fake_sink.h"
#include "fake_observer.h"

/* ── fixtures (checked-in 48 kHz LC3, same set as tests/unit/decode) ── */

static const uint8_t mono10_lc3[] = {
#include "mono_48k_10ms_60b_lc3.inc"
};
static const uint8_t mono10_pcm[] = {
#include "mono_48k_10ms_60b_pcm.inc"
};
static const uint8_t modeb10_lc3[] = {
#include "modeb_48k_10ms_60b_lc3.inc"
};
static const uint8_t modeb10_pcm[] = {
#include "modeb_48k_10ms_60b_pcm.inc"
};

#define MONO_LC3_LEN  sizeof(mono10_lc3)  /* 60 bytes, one 10 ms mono frame */
#define MODEB_LC3_LEN sizeof(modeb10_lc3) /* 120 bytes, [L][R] per frame */

static const struct audio_stream_codec_shape mono_shape = {
	.freq_hz = 48000,
	.frame_dur_us = 10000,
	.octets_per_frame = 60,
	.frame_blocks_per_sdu = 1,
	.chan_count = 1,
};

static const struct audio_stream_codec_shape modeb_shape = {
	.freq_hz = 48000,
	.frame_dur_us = 10000,
	.octets_per_frame = 60,
	.frame_blocks_per_sdu = 1,
	.chan_count = 2,
};

/* ── helpers ─────────────────────────────────────────────────────── */

static void full_reset(void)
{
	fake_sink_reset();
	fake_observer_reset();
	fake_volume_reset();
	lc3_wrap_reset();
	audio_stats_reset();
	audio_perf_reset();
	zassert_ok(audio_sink_init());
	zassert_ok(audio_stream_session_init());
}

static void setup_mono(void)
{
	full_reset();
	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_ok(audio_stream_session_enable(0));
	audio_stream_session_rx_open();
}

static void setup_modeb(void)
{
	full_reset();
	zassert_ok(audio_stream_session_config(0, &modeb_shape));
	zassert_ok(audio_stream_session_enable(0));
	audio_stream_session_rx_open();
}

static void setup_modea(void)
{
	full_reset();
	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_ok(audio_stream_session_config(1, &mono_shape));
	zassert_ok(audio_stream_session_enable(0));
	zassert_ok(audio_stream_session_enable(1));
	audio_stream_session_rx_open();
}

static void assert_rx_stats_zero(size_t idx)
{
	struct audio_stream_rx_stats stats = audio_stream_session_rx_stats_get(idx);

	zassert_equal(0U, stats.valid, "valid status counter not zero");
	zassert_equal(0U, stats.error, "error status counter not zero");
	zassert_equal(0U, stats.lost, "lost status counter not zero");
	zassert_equal(0U, stats.unknown, "unknown status counter not zero");
	zassert_equal(0U, stats.no_ts, "missing timestamp counter not zero");
}

/* FNV-1a over one int16 value (host byte order, matches the fake sink). */
static uint32_t fnv_u16(uint32_t hash, int16_t v)
{
	uint16_t u = (uint16_t)v;

	hash ^= (uint8_t)(u & 0xFF);
	hash *= 0x01000193UL;
	hash ^= (uint8_t)((u >> 8) & 0xFF);
	hash *= 0x01000193UL;
	return hash;
}

/* Expected pushed hash: the checked-in .pcm fixtures ARE the stereo
 * interleaved decode output (audio_decode_sdu mono duplicates to both
 * channels, Mode B splits per channel) and the suite's volume seam is
 * identity, so the pushed PCM is exactly [pcm[0], pcm[1], ...] — the
 * session decode→volume→push pipeline with real liblc3 decoding. */
static uint32_t golden_hash_flat(const int16_t *pcm, size_t n)
{
	uint32_t hash = 0x811c9dc5UL;

	for (size_t i = 0; i < n; i++) {
		hash = fnv_u16(hash, pcm[i]);
	}
	return hash;
}

/* ── init / config / accessors / invalid slots ───────────────────── */

ZTEST(audio_stream_session, test_init_and_invalid_slots)
{
	full_reset();

	zassert_equal(-EINVAL, audio_stream_session_config(99, &mono_shape), "idx out of range");
	zassert_equal(-EINVAL, audio_stream_session_config(0, NULL), "NULL shape");
	zassert_equal(-EINVAL,
		      audio_stream_session_recv(99, true, true, 1, 1, mono10_lc3, MONO_LC3_LEN),
		      "recv idx out of range");
	zassert_equal(-EINVAL,
		      audio_stream_session_recv(0, true, true, 1, 1, mono10_lc3, MONO_LC3_LEN),
		      "recv on unconfigured slot");
	zassert_false(audio_stream_session_configured(0), "not configured");
	zassert_is_null(audio_stream_session_shape(0), "no shape");
	zassert_equal(AUDIO_STREAM_MODE_MONO, audio_stream_session_mode(0), "inert mode");
	zassert_equal(0U, audio_stream_session_pd(0), "inert pd");
	assert_rx_stats_zero(0);
	zassert_equal(0U, audio_stream_session_recv_count(0), "inert recv count");
	zassert_equal(0U, audio_stream_session_configured_count(), "no slots");
	zassert_equal(0U, fake_sink_push_count(), "no pushes");
}

ZTEST(audio_stream_session, test_config_accessors_and_qos)
{
	full_reset();

	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_true(audio_stream_session_configured(0), "configured");
	zassert_false(audio_stream_session_configured(1), "slot 1 free");
	zassert_equal(1U, audio_stream_session_configured_count(), "one slot");
	const struct audio_stream_codec_shape *sh = audio_stream_session_shape(0);

	zassert_not_null(sh, "shape present");
	zassert_equal(48000, sh->freq_hz);
	zassert_equal(10000, sh->frame_dur_us);
	zassert_equal(60, sh->octets_per_frame);
	zassert_equal(1, sh->frame_blocks_per_sdu);
	zassert_equal(1, sh->chan_count);
	zassert_equal(AUDIO_STREAM_MODE_MONO, audio_stream_session_mode(0), "mono mode");
	zassert_is_null(audio_stream_session_shape(1), "no shape for free slot");

	zassert_ok(audio_stream_session_qos(0, 42000));
	zassert_equal(42000U, audio_stream_session_pd(0), "pd stored");
	zassert_equal(-EINVAL, audio_stream_session_qos(99, 1), "qos idx out of range");
	zassert_equal(-EINVAL, audio_stream_session_qos(1, 1), "qos unconfigured slot");
}

ZTEST(audio_stream_session, test_mode_classification_transitions)
{
	full_reset();

	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_equal(AUDIO_STREAM_MODE_MONO, audio_stream_session_mode(0), "mono");

	audio_stream_session_release(0);
	zassert_ok(audio_stream_session_config(0, &modeb_shape));
	zassert_equal(AUDIO_STREAM_MODE_MODEB, audio_stream_session_mode(0), "mode B");

	audio_stream_session_release(0);
	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_ok(audio_stream_session_config(1, &mono_shape));
	zassert_equal(AUDIO_STREAM_MODE_MODEA, audio_stream_session_mode(0), "mode A (two ASEs)");
	zassert_equal(AUDIO_STREAM_MODE_MODEA, audio_stream_session_mode(1), "mode A slot 1");

	audio_stream_session_release(1);
	zassert_equal(AUDIO_STREAM_MODE_MONO, audio_stream_session_mode(0),
		      "single remaining ASE is mono again");
	zassert_equal(1U, audio_stream_session_configured_count(), "one slot left");
}

/* ── valid decoding and push ─────────────────────────────────────── */

ZTEST(audio_stream_session, test_mono_valid_decode_push_golden)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "one push");
	zassert_equal(960U, fake_sink_last_sample_count(), "stereo 480 frames");
	zassert_equal(960U, fake_sink_input_frames() * 2U, "sink frames 480 from enable");

	const int16_t *pcm = (const int16_t *)mono10_pcm;

	zassert_equal(golden_hash_flat(pcm, 960), fake_sink_first_push_hash(960),
		      "pushed PCM = volume-scaled golden mono");

	struct audio_stats stats = audio_stats_get();

	zassert_equal(1U, stats.total_frames, "one frame decoded");
	zassert_equal(0U, stats.plc_frames, "no PLC");
	zassert_equal(0U, stats.decode_errors, "no errors");
	zassert_true(fake_observer_last_push_l_valid(), "pre_push l valid");
	zassert_true(fake_observer_last_push_r_valid(), "pre_push r valid");
	zassert_equal(1U, fake_observer_pre_push_count(), "one pre-push event");
	zassert_equal(1U, fake_volume_apply_count(), "volume applied before push");
}

ZTEST(audio_stream_session, test_modeb_valid_decode_push_golden)
{
	setup_modeb();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, modeb10_lc3, MODEB_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "one push");
	zassert_equal(960U, fake_sink_last_sample_count(), "stereo 480 frames");

	const int16_t *pcm = (const int16_t *)modeb10_pcm;

	zassert_equal(golden_hash_flat(pcm, 960), fake_sink_first_push_hash(960),
		      "pushed PCM = volume-scaled golden mode B (L/R interleaved)");

	struct audio_stats stats = audio_stats_get();

	zassert_equal(2U, stats.total_frames, "two decoders per SDU");
	zassert_equal(0U, stats.decode_errors, "no errors");
}

ZTEST(audio_stream_session, test_modea_equal_ts_pair_golden)
{
	setup_modea();

	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(0U, fake_sink_push_count(), "first half alone does not emit");

	zassert_ok(audio_stream_session_recv(1, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "equal-TS pair emits once");

	const int16_t *pcm = (const int16_t *)mono10_pcm;

	zassert_equal(golden_hash_flat(pcm, 960), fake_sink_first_push_hash(960),
		      "Mode A pair = interleaved volume-scaled mono halves");

	struct audio_stats stats = audio_stats_get();

	zassert_equal(2U, stats.total_frames, "two decoders, one event");
	zassert_equal(0U, stats.decode_errors, "no errors");
	zassert_true(fake_observer_last_push_l_valid(), "left half source-valid");
	zassert_true(fake_observer_last_push_r_valid(), "right half source-valid");
}

ZTEST(audio_stream_session, test_modea_one_sided_loss_plc)
{
	setup_modea();

	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(0U, fake_sink_push_count(), "left pending");

	/* Right delivers the NEXT event: left's older half is concealed. */
	zassert_ok(audio_stream_session_recv(1, true, true, 20000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "one PLC-backed event");
	zassert_true(fake_observer_last_push_l_valid(), "real left half valid");
	zassert_false(fake_observer_last_push_r_valid(), "concealed right half");

	struct audio_stats stats = audio_stats_get();

	zassert_equal(1U, stats.plc_frames, "one PLC half");
	zassert_equal(1U, stats.total_frames - stats.plc_frames, "one decoded half");
	zassert_equal(0U, stats.decode_errors, "no errors");
}

ZTEST(audio_stream_session, test_modea_missing_ts_valid_rejected)
{
	setup_modea();

	/* VALID SDU without the TS flag: half skipped (observer + decode
	 * error), no pairing-state mutation, no push. */
	zassert_ok(audio_stream_session_recv(0, true, false, 0, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_observer_missing_ts(), "missing-TS observer event");
	zassert_equal(1U, audio_stats_get().decode_errors, "one receive/decode fault");
	zassert_equal(0U, fake_sink_push_count(), "no push");

	/* The skipped half must not corrupt the assembler: the next valid
	 * halves pair normally. */
	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(0U, fake_sink_push_count(), "still pending one side");
	zassert_ok(audio_stream_session_recv(1, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "normal pair after skipped half");
}

/* ── malformed SDU rejection ─────────────────────────────────────── */

ZTEST(audio_stream_session, test_malformed_sdu_rejects_then_resumes_mono)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN - 1));
	zassert_equal(1U, fake_observer_malformed_sdu(), "malformed observer event");
	zassert_equal(1U, audio_stats_get().decode_errors, "exactly one decode error");
	zassert_equal(0U, fake_sink_push_count(), "no decode/push");
	zassert_equal(0U, fake_observer_pre_push_count(), "no pre-push observer");

	/* Valid input proceeds normally. */
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "valid SDU pushes");
	zassert_equal(1U, audio_stats_get().decode_errors, "error count unchanged by valid input");
	zassert_equal(1U, audio_stats_get().total_frames, "one decoded frame");
}

ZTEST(audio_stream_session, test_malformed_sdu_no_modea_mutation)
{
	setup_modea();

	zassert_ok(
		audio_stream_session_recv(0, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN - 1));
	zassert_equal(1U, fake_observer_malformed_sdu(), "malformed observer event");
	zassert_equal(1U, audio_stats_get().decode_errors, "one decode error");
	zassert_equal(0U, fake_sink_push_count(), "no push");

	/* The malformed half was rejected BEFORE the assembler store: the
	 * next valid half on the same channel still pairs. */
	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_ok(audio_stream_session_recv(1, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "valid pair after malformed rejection");
}

/* ── PLC / decoder readiness / hard failures ─────────────────────── */

ZTEST(audio_stream_session, test_lost_sdu_plc_push)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, false, false, 0, 1, NULL, 0U));
	zassert_equal(1U, fake_sink_push_count(), "LOST conceals to one push");
	zassert_false(fake_observer_last_push_l_valid(), "PLC push never source-valid");
	zassert_false(fake_observer_last_push_r_valid(), "PLC push never source-valid");
	zassert_equal(1U, audio_stats_get().plc_frames, "one PLC frame");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");
}

/* ── valid empty ISO SDU concealment ───────────────────────────────
 * Some controllers send empty HCI ISO packets (BT_ISO_FLAGS_VALID set,
 * zero length) when the remote produced no SDU.  These are NOT malformed
 * LC3 payloads: each is counted as one empty-SDU event and rendered as
 * PLC at normal cadence — no decode-error increment, no malformed
 * observer event, and the following valid SDU resumes normally.
 */

ZTEST(audio_stream_session, test_valid_empty_sdu_mono_plc_resumes)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, NULL, 0U));
	zassert_equal(1U, audio_stats_get().empty_sdus, "one empty-SDU event");
	zassert_equal(1U, fake_sink_push_count(), "PLC to one push");
	zassert_equal(1U, audio_stats_get().plc_frames, "one PLC frame");
	zassert_equal(1U, audio_stats_get().total_frames, "PLC accounts as total");
	zassert_equal(0U, audio_stats_get().decode_errors, "no decode errors");
	zassert_equal(0U, fake_observer_malformed_sdu(), "no malformed observer event");
	zassert_false(fake_observer_last_push_l_valid(), "normalized PLC push never source-valid");
	zassert_false(fake_observer_last_push_r_valid(), "normalized PLC push never source-valid");

	/* The next valid SDU resumes normally and stays contiguous. */
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "valid SDU pushes");
	zassert_equal(2U, audio_stats_get().total_frames, "one PLC + one decoded");
	zassert_equal(1U, audio_stats_get().empty_sdus, "empty count unchanged by valid input");
	zassert_equal(0U, audio_stats_get().decode_errors, "still no errors");
}

ZTEST(audio_stream_session, test_valid_empty_sdu_modeb_plc)
{
	setup_modeb();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, NULL, 0U));
	zassert_equal(1U, audio_stats_get().empty_sdus, "one empty-SDU event");
	zassert_equal(1U, fake_sink_push_count(), "one stereo push");
	zassert_equal(2U, audio_stats_get().plc_frames, "one PLC per channel");
	zassert_equal(2U, audio_stats_get().total_frames, "two PLC decoders accounted");
	zassert_equal(0U, audio_stats_get().decode_errors, "no decode errors");
	zassert_equal(0U, fake_observer_malformed_sdu(), "no malformed observer event");
}

ZTEST(audio_stream_session, test_valid_empty_sdu_modea_pairs_by_ts)
{
	setup_modea();

	/* Left: valid empty SDU WITH a timestamp — normalized source-invalid
	 * but must keep its event position so it pairs with the right
	 * half at the same event timestamp. */
	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 1, NULL, 0U));
	zassert_equal(0U, fake_sink_push_count(), "empty left half alone does not emit");
	zassert_equal(1U, audio_stats_get().empty_sdus, "one empty-SDU event");

	zassert_ok(audio_stream_session_recv(1, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "equal-TS pair emits once");
	zassert_false(fake_observer_last_push_l_valid(),
		      "empty side concealed, never source-valid");
	zassert_true(fake_observer_last_push_r_valid(), "real side source-valid");

	struct audio_stats stats = audio_stats_get();

	zassert_equal(1U, stats.plc_frames, "one PLC half");
	zassert_equal(1U, stats.total_frames - stats.plc_frames, "one decoded half");
	zassert_equal(0U, stats.decode_errors, "no errors");
	zassert_equal(0U, fake_observer_malformed_sdu(), "no malformed observer event");

	/* No stale mutation: the next fully valid event pairs normally. */
	zassert_ok(audio_stream_session_recv(0, true, true, 20000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_ok(audio_stream_session_recv(1, true, true, 20000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "next event emits once");
	zassert_equal(4U, audio_stats_get().total_frames, "two events, two decoders each");
	zassert_equal(1U, audio_stats_get().empty_sdus, "empty count unchanged");
}

ZTEST(audio_stream_session, test_decoder_not_ready_skip)
{
	full_reset();
	zassert_ok(audio_stream_session_config(0, &mono_shape));
	audio_stream_session_rx_open();

	/* Admission open but no Enable: decoder not ready — no push, no
	 * stats, no observer events. */
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(0U, fake_sink_push_count(), "no push without decoder");
	zassert_equal(0U, audio_stats_get().total_frames, "no decode");
	zassert_equal(0U, fake_observer_pre_push_count(), "no observer event");

	zassert_ok(audio_stream_session_enable(0));
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "push after enable");
}

ZTEST(audio_stream_session, test_hard_decode_failure_skips_push_mono)
{
	setup_mono();

	lc3_wrap_fail_all(true);
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	lc3_wrap_fail_all(false);

	zassert_equal(0U, fake_sink_push_count(), "hard failure skips push");
	zassert_equal(1U, audio_stats_get().decode_errors, "failure accounted");
	zassert_equal(0U, audio_stats_get().total_frames, "no decoded frame");

	/* Recovery: valid input proceeds. */
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "push after failure");
	zassert_equal(1U, audio_stats_get().total_frames, "decoded after failure");
}

ZTEST(audio_stream_session, test_hard_decode_failure_modeb_no_push)
{
	setup_modeb();

	lc3_wrap_fail_all(true);
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, modeb10_lc3, MODEB_LC3_LEN));
	lc3_wrap_fail_all(false);

	zassert_equal(0U, fake_sink_push_count(), "hard failure skips push");
	zassert_equal(2U, audio_stats_get().decode_errors, "both Mode B decoders accounted");
	zassert_equal(0U, audio_stats_get().total_frames, "no decoded frame");

	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 2, modeb10_lc3, MODEB_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "push after failure");
}

ZTEST(audio_stream_session, test_modea_hard_failure_consumes_event)
{
	setup_modea();

	lc3_wrap_fail_all(true);
	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_ok(audio_stream_session_recv(1, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	lc3_wrap_fail_all(false);

	zassert_equal(0U, fake_sink_push_count(), "failed event never pushes");
	zassert_equal(2U, audio_stats_get().decode_errors, "both halves accounted");

	/* The failed event is consumed: the next event resolves normally
	 * instead of re-emitting the stale one. */
	zassert_ok(audio_stream_session_recv(0, true, true, 20000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_ok(audio_stream_session_recv(1, true, true, 20000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "next event emits once");
	zassert_equal(2U, audio_stats_get().total_frames, "next event decoded");
}

/* ── sequence-gap concealment ────────────────────────────────────── */

ZTEST(audio_stream_session, test_seq_gap_mono_plc_cadence)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 5, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first SDU");

	/* seq 8 after 5: two omitted SDUs concealed as PLC BEFORE the
	 * current SDU, then the current SDU decodes. */
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 8, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(4U, fake_sink_push_count(), "2 PLC + 1 valid");
	zassert_equal(2U, audio_stats_get().plc_frames, "two concealed frames");
	zassert_equal(2U, audio_stats_get().total_frames - audio_stats_get().plc_frames,
		      "two decoded frames (first + current SDU)");
	zassert_true(fake_observer_last_push_l_valid(), "final push valid (PLC came first)");
}

ZTEST(audio_stream_session, test_seq_gap_modeb_plc)
{
	setup_modeb();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 5, modeb10_lc3, MODEB_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first SDU");

	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 8, modeb10_lc3, MODEB_LC3_LEN));
	zassert_equal(4U, fake_sink_push_count(), "2 PLC + 1 valid");
	/* Each Mode B PLC push invokes both decoders. */
	zassert_equal(4U, audio_stats_get().plc_frames, "two PLC SDUs × 2 channels");
	zassert_equal(4U, audio_stats_get().total_frames - audio_stats_get().plc_frames,
		      "two valid SDUs × 2 channels");
}

ZTEST(audio_stream_session, test_seq_gap_modea_synthetic_lost)
{
	setup_modea();

	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_ok(audio_stream_session_recv(1, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "event 1 pair");

	/* Left delivers event 3 with seq 3: event 2 omitted on left.  The
	 * synthetic sentinel pairs with right's event 2 as one PLC-backed
	 * event; left's current event 3 stays pending. */
	zassert_ok(audio_stream_session_recv(0, true, true, 30000, 3, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "sentinels cannot resolve alone");

	zassert_ok(audio_stream_session_recv(1, true, true, 20000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "sentinel + real half = PLC-backed event");
	zassert_false(fake_observer_last_push_l_valid(), "left half concealed");
	zassert_true(fake_observer_last_push_r_valid(), "right half real");

	struct audio_stats stats = audio_stats_get();

	zassert_equal(1U, stats.plc_frames, "one PLC half");
	zassert_equal(4U, stats.total_frames, "event1 pair (2) + PLC-backed (2)");
}

ZTEST(audio_stream_session, test_seq_resync_no_synthesis)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 5, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first SDU");

	/* Out-of-window jump (495 > MAX+1): resync, NO synthesis — the
	 * current SDU still decodes once. */
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 500, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "no PLC synthesis on resync");
	zassert_equal(0U, audio_stats_get().plc_frames, "no concealment");
	zassert_equal(2U, audio_stats_get().total_frames, "both SDUs decoded");

	/* Contiguous after resync. */
	zassert_ok(audio_stream_session_recv(0, true, true, 3000, 501, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(3U, fake_sink_push_count(), "contiguous after resync");
	zassert_equal(0U, audio_stats_get().plc_frames, "still no synthesis");
}

/* ── admission / lease / generation ──────────────────────────────── */

ZTEST(audio_stream_session, test_admission_closed_then_open)
{
	setup_mono();

	audio_stream_session_rx_close();
	zassert_false(audio_stream_session_test_admission_open(), "admission closed");
	zassert_equal(-EINVAL,
		      audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN),
		      "closed admission rejects");
	zassert_equal(0U, fake_sink_push_count(), "no decode/push");
	zassert_equal(0U, audio_stats_get().total_frames, "no decode");
	zassert_equal(0U, fake_observer_pre_push_count(), "no observer event");

	audio_stream_session_rx_open();
	zassert_true(audio_stream_session_test_admission_open(), "admission reopened");
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "push after rx_open");
}

/* ── release / reset / reconnect ─────────────────────────────────── */

ZTEST(audio_stream_session, test_release_slot_reuse)
{
	setup_mono();
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_VALID, true),
		      "adapter-style counting");
	zassert_equal(1U, audio_stream_session_recv_count(0), "counted");

	audio_stream_session_release(0);
	zassert_false(audio_stream_session_configured(0), "released");
	zassert_equal(0U, audio_stream_session_configured_count(), "no slots");
	zassert_is_null(audio_stream_session_shape(0), "shape cleared");
	assert_rx_stats_zero(0);
	zassert_equal(0U, audio_stream_session_recv_count(0), "count cleared");
	zassert_equal(0U, audio_stream_session_pd(0), "pd cleared");
	zassert_equal(-EINVAL,
		      audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN),
		      "released slot rejects");

	/* Reuse: fresh config/enable/admission works. */
	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_ok(audio_stream_session_enable(0));
	zassert_equal(1U, audio_stream_session_configured_count(), "reconfigured");
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "slot reuse pushes (1 fresh + 1 reused)");
}

ZTEST(audio_stream_session, test_reset_all_clears_session)
{
	full_reset();
	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_ok(audio_stream_session_config(1, &mono_shape));
	zassert_ok(audio_stream_session_enable(0));
	zassert_ok(audio_stream_session_enable(1));
	audio_stream_session_rx_open();
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_VALID, true),
		      "adapter-style counting");
	zassert_equal(1U, audio_stream_session_recv_count(0), "counted");

	audio_stream_session_reset_all();
	zassert_equal(0U, audio_stream_session_configured_count(), "all slots cleared");
	zassert_false(audio_stream_session_configured(0), "slot 0 cleared");
	zassert_false(audio_stream_session_configured(1), "slot 1 cleared");
	zassert_is_null(audio_stream_session_shape(0), "shape cleared");
	assert_rx_stats_zero(0);
	zassert_equal(0U, audio_stream_session_recv_count(0), "count cleared");
	zassert_equal(-EINVAL,
		      audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN),
		      "unconfigured after reset");
}

ZTEST(audio_stream_session, test_reconnect_fresh_session)
{
	setup_mono();
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_VALID, true),
		      "adapter-style counting");
	zassert_equal(1U, audio_stream_session_recv_count(0), "first session counted");

	/* Disconnect-style teardown: close admission, drain, reset all. */
	audio_stream_session_rx_close();
	audio_stream_session_reset_all();
	zassert_false(audio_stream_session_test_admission_open(), "admission stays closed");
	assert_rx_stats_zero(0);
	zassert_equal(0U, audio_stream_session_recv_count(0), "count reset at reconnect");

	/* Reconnect: fresh config/enable + gate-open edge rx_open. */
	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_ok(audio_stream_session_enable(0));
	audio_stream_session_rx_open();
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "fresh session pushes (1 + 1)");
	zassert_equal(1U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_VALID, true),
		      "fresh counting");
	zassert_equal(1U, audio_stream_session_recv_count(0), "count from fresh session only");
	zassert_true(audio_stream_session_test_admission_open(), "admission open after reconnect");
}

/* ── sink failure / disable / counting / start-clear ─────────────── */

ZTEST(audio_stream_session, test_sink_failure_accounting)
{
	setup_mono();

	fake_sink_set_fail_next(true);
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_fail_pushes(), "push attempted and failed");
	zassert_equal(0U, fake_sink_push_count(), "failed push not recorded");

	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);
	zassert_equal(1U, queue.push_failures, "perf push-failure accounted");

	/* Next push succeeds and is not miscounted. */
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "successful push recorded");
}

ZTEST(audio_stream_session, test_disable_keeps_shape_decoder_inert)
{
	setup_mono();
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "streaming");

	audio_stream_session_disable(0);
	zassert_true(audio_stream_session_configured(0), "configured kept");
	zassert_equal(AUDIO_STREAM_MODE_MONO, audio_stream_session_mode(0), "shape kept");
	zassert_not_null(audio_stream_session_shape(0), "shape kept");

	/* Decoder nulled: recv is a no-op until re-enabled.  The skipped
	 * SDU does not consume its sequence position (the decoder-readiness
	 * check precedes the sequence update), so the next enabled SDU
	 * with the SAME sequence stays contiguous. */
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "no push while disabled");
	zassert_equal(1U, audio_stats_get().total_frames, "no decode while disabled");

	zassert_ok(audio_stream_session_enable(0));
	zassert_ok(audio_stream_session_recv(0, true, true, 3000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "push after re-enable");
	zassert_equal(0U, audio_stats_get().plc_frames, "no concealment on the resumed SDU");
}

ZTEST(audio_stream_session, test_rx_status_record_gate_independent)
{
	full_reset();
	zassert_ok(audio_stream_session_config(0, &mono_shape));

	/* Status recording happens in the adapter before any gate/admission
	 * check; it must work without an enabled decoder. */
	zassert_equal(1U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_VALID, true),
		      "first valid count");
	zassert_equal(2U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_VALID, true),
		      "second valid count");
	zassert_equal(3U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_VALID, true),
		      "third valid count");
	zassert_equal(3U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_ERROR, false),
		      "error leaves valid count");
	zassert_equal(3U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_LOST, false),
		      "lost leaves valid count");
	zassert_equal(
		3U, audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_UNKNOWN, false),
		"unknown leaves valid count");

	struct audio_stream_rx_stats stats = audio_stream_session_rx_stats_get(0);
	zassert_equal(3U, stats.valid, "valid callbacks");
	zassert_equal(1U, stats.error, "error callbacks");
	zassert_equal(1U, stats.lost, "lost callbacks");
	zassert_equal(1U, stats.unknown, "unknown callbacks");
	zassert_equal(3U, stats.no_ts, "callbacks without timestamps");
	zassert_equal(3U, audio_stream_session_recv_count(0), "accessor matches");

	audio_stream_session_recv_reset(0);
	assert_rx_stats_zero(0);
	zassert_equal(0U, audio_stream_session_recv_count(0), "reset zeroes");
	zassert_equal(
		0U, audio_stream_session_rx_status_record(1, (enum audio_stream_rx_status)99, true),
		"in-range slot records invalid status regardless of configured state");
	stats = audio_stream_session_rx_stats_get(1);
	zassert_equal(0U, stats.valid, "unconfigured slot valid count");
	zassert_equal(0U, stats.error, "unconfigured slot error count");
	zassert_equal(0U, stats.lost, "unconfigured slot lost count");
	zassert_equal(1U, stats.unknown, "unconfigured slot unknown count");
	zassert_equal(0U, stats.no_ts, "unconfigured slot timestamp count");
	zassert_equal(
		0U, audio_stream_session_rx_status_record(99, AUDIO_STREAM_RX_STATUS_VALID, false),
		"out-of-range slot inert");
	assert_rx_stats_zero(99);
}

ZTEST(audio_stream_session, test_start_clear_resets_seq_and_assembler)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 5, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first SDU");
	zassert_equal(1U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_VALID, true),
		      "status before start_clear");

	/* start_clear re-bases the per-CIS tracker: a same-sequence SDU in
	 * the next stream must not be read as a gap. */
	audio_stream_session_start_clear();
	struct audio_stream_rx_stats stats = audio_stream_session_rx_stats_get(0);
	zassert_equal(1U, stats.valid, "start_clear retains valid status");
	zassert_equal(0U, stats.error, "start_clear retains error status");
	zassert_equal(0U, stats.lost, "start_clear retains lost status");
	zassert_equal(0U, stats.unknown, "start_clear retains unknown status");
	zassert_equal(0U, stats.no_ts, "start_clear retains timestamp status");
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 5, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "no PLC after start_clear");
	zassert_equal(0U, audio_stats_get().plc_frames, "no concealment");

	/* Mode A: pending halves are cleared so stale halves cannot pair
	 * across streams. */
	setup_modea();
	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(0U, fake_sink_push_count(), "left pending");
	audio_stream_session_start_clear();
	zassert_ok(audio_stream_session_recv(1, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(0U, fake_sink_push_count(), "stale left half cleared — no stale pair");
	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "fresh pair after clear");
}

/* ── concurrency: rx_close drain + no lock across decode/sink ────── */

static K_THREAD_STACK_DEFINE(recv_stack, 16384);
static K_THREAD_STACK_DEFINE(close_stack, 4096);
static struct k_thread recv_thread;
static struct k_thread close_thread;
static atomic_bool recv_done;
static atomic_bool close_done;
static atomic_int recv_ret;

static void recv_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	atomic_store(&recv_ret,
		     audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	atomic_store(&recv_done, true);
}

static void close_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	audio_stream_session_rx_close();
	atomic_store(&close_done, true);
}

ZTEST(audio_stream_session, test_rx_close_waits_for_admitted_lease)
{
	setup_mono();

	/* Park a receive lease inside the fake sink push. */
	fake_sink_set_block_pushes(true);
	atomic_store(&recv_done, false);
	atomic_store(&close_done, false);
	atomic_store(&recv_ret, 0);

	k_thread_create(&recv_thread, recv_stack, K_THREAD_STACK_SIZEOF(recv_stack), recv_thread_fn,
			NULL, NULL, NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);

	/* Wait until the lease is admitted and parked inside the push. */
	fake_sink_blocked_enter();

	/* No lock may be held across the decode/sink path: the session
	 * mutex is free while thread A sits inside audio_sink_push. */
	zassert_equal(0, audio_stream_session_test_lock_try(),
		      "session mutex not held during push");
	audio_stream_session_test_lock_release();
	zassert_equal(1U, audio_stream_session_test_in_flight(), "one admitted lease");

	/* rx_close from another thread must block until the lease drains. */
	k_thread_create(&close_thread, close_stack, K_THREAD_STACK_SIZEOF(close_stack),
			close_thread_fn, NULL, NULL, NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);
	k_busy_wait(2000); /* give the close thread time to reach the wait */
	zassert_false(atomic_load(&close_done), "rx_close blocks on the admitted lease");

	/* Release the lease: both the recv and the close complete. */
	fake_sink_blocked_release();
	k_thread_join(&recv_thread, K_FOREVER);
	k_thread_join(&close_thread, K_FOREVER);

	zassert_equal(0, atomic_load(&recv_ret), "admitted recv completed normally");
	zassert_true(atomic_load(&recv_done), "recv finished");
	zassert_true(atomic_load(&close_done), "rx_close returned after drain");
	zassert_equal(0U, audio_stream_session_test_in_flight(), "no leases left");
	zassert_false(audio_stream_session_test_admission_open(), "admission closed");
	zassert_equal(1U, fake_sink_push_count(), "admitted push recorded");

	/* Late RX after the drain is rejected (generation reset). */
	zassert_equal(-EINVAL,
		      audio_stream_session_recv(0, true, true, 2000, 2, mono10_lc3, MONO_LC3_LEN),
		      "late RX rejected after close");
}

ZTEST(audio_stream_session, test_no_lock_held_during_push)
{
	setup_mono();

	/* Park a receive lease inside the fake sink push again. */
	fake_sink_set_block_pushes(true);
	atomic_store(&recv_done, false);
	atomic_store(&close_done, false);
	atomic_store(&recv_ret, 0);

	k_thread_create(&recv_thread, recv_stack, K_THREAD_STACK_SIZEOF(recv_stack), recv_thread_fn,
			NULL, NULL, NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);
	fake_sink_blocked_enter();

	/* The session mutex must be FREE while the lease is inside the
	 * decode/sink path: a different thread can acquire it. */
	zassert_equal(0, audio_stream_session_test_lock_try(),
		      "session mutex not held during decode/sink");
	audio_stream_session_test_lock_release();
	zassert_equal(1U, audio_stream_session_test_in_flight(), "one admitted lease");

	/* Let the lease finish; no teardown involved. */
	fake_sink_blocked_release();
	k_thread_join(&recv_thread, K_FOREVER);
	zassert_true(atomic_load(&recv_done), "recv finished");
	zassert_equal(0, atomic_load(&recv_ret), "recv completed normally");
	zassert_equal(0U, audio_stream_session_test_in_flight(), "lease released");
	zassert_true(audio_stream_session_test_admission_open(), "admission still open");
	zassert_equal(1U, fake_sink_push_count(), "push recorded");
}

/* ── Release/reset/admission matrix ─────────────────────────────
 * The R7 teardown coordinator composes these session primitives: a
 * slot releases once independently, a duplicate release is an
 * observable no-op, admission stays closed after teardown until rx_open,
 * duplicate rx_close cannot deadlock, and release-then-reset-then-
 * reconfigure starts fresh.
 */

ZTEST(audio_stream_session, test_release_twice_no_configured_change)
{
	setup_mono();

	audio_stream_session_release(0);
	audio_stream_session_release(0); /* duplicate: observable no-op */

	zassert_equal(0U, audio_stream_session_configured_count(),
		      "configured count never goes negative");
	zassert_false(audio_stream_session_configured(0), "slot released");
	zassert_is_null(audio_stream_session_shape(0), "shape cleared");
	assert_rx_stats_zero(0);
	zassert_equal(0U, audio_stream_session_recv_count(0), "recv count cleared");
	zassert_equal(0U, audio_stream_session_pd(0), "pd cleared");
	zassert_equal(-EINVAL,
		      audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN),
		      "released slot rejects");
	zassert_equal(0U, fake_sink_push_count(), "no push");
}

ZTEST(audio_stream_session, test_independent_per_slot_cleanup)
{
	setup_modea();

	audio_stream_session_release(0);
	zassert_false(audio_stream_session_configured(0), "slot 0 released");
	zassert_true(audio_stream_session_configured(1), "slot 1 still configured");
	zassert_equal(1U, audio_stream_session_configured_count(), "one slot left");
	zassert_equal(AUDIO_STREAM_MODE_MONO, audio_stream_session_mode(1),
		      "single remaining slot is mono again");

	audio_stream_session_release(1);
	zassert_false(audio_stream_session_configured(1), "slot 1 released");
	zassert_equal(0U, audio_stream_session_configured_count(), "no slots left");
}

ZTEST(audio_stream_session, test_admission_stays_closed_after_release_reset_until_rx_open)
{
	setup_mono();

	/* Teardown sequence: close admission + drain, release, reset. */
	audio_stream_session_rx_close();
	audio_stream_session_release(0);
	audio_stream_session_reset_all();
	zassert_false(audio_stream_session_test_admission_open(), "admission closed");

	/* Fresh config/enable still cannot receive while admission is
	 * closed — only the gate-open rx_open() edge re-enables it. */
	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_ok(audio_stream_session_enable(0));
	zassert_equal(-EINVAL,
		      audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN),
		      "admission closed rejects");
	zassert_equal(0U, fake_sink_push_count(), "no push");

	audio_stream_session_rx_open();
	zassert_true(audio_stream_session_test_admission_open(), "admission reopened");
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "push after rx_open");
}

ZTEST(audio_stream_session, test_release_then_reset_then_reconfigure_fresh)
{
	setup_mono();
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first session push");
	zassert_equal(1U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_VALID, true),
		      "counted");

	/* Full teardown: close/drain, release, reset all. */
	audio_stream_session_rx_close();
	audio_stream_session_release(0);
	audio_stream_session_reset_all();
	assert_rx_stats_zero(0);

	/* Reconnect: fresh config/enable + gate-open edge. */
	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_ok(audio_stream_session_enable(0));
	audio_stream_session_rx_open();
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "fresh session push (1 + 1)");
	zassert_equal(1U,
		      audio_stream_session_rx_status_record(0, AUDIO_STREAM_RX_STATUS_VALID, true),
		      "fresh counting");
	zassert_equal(1U, audio_stream_session_recv_count(0), "fresh count only");
	zassert_equal(2U, audio_stats_get().total_frames, "no state leakage");
}

ZTEST(audio_stream_session, test_duplicate_rx_close_no_deadlock)
{
	setup_mono();

	/* Duplicate rx_close returns immediately (idempotent, no hang). */
	audio_stream_session_rx_close();
	audio_stream_session_rx_close();

	zassert_false(audio_stream_session_test_admission_open(), "admission closed");
	zassert_equal(-EINVAL,
		      audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN),
		      "late RX rejected after close");
	zassert_equal(0U, fake_sink_push_count(), "no push");

	audio_stream_session_rx_open();
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "push after reopen");
}

ZTEST(audio_stream_session, test_modea_first_slot_release_then_second_slot_cleanup)
{
	setup_modea();
	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_ok(audio_stream_session_recv(1, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "Mode A pair emitted");

	/* First slot release (the coordinator already closed the gate):
	 * the second slot stays configured and cleans independently. */
	audio_stream_session_release(0);
	zassert_false(audio_stream_session_configured(0), "slot 0 released");
	zassert_true(audio_stream_session_configured(1), "slot 1 still configured");
	zassert_equal(1U, audio_stream_session_configured_count(), "one slot left");

	audio_stream_session_release(1);
	zassert_false(audio_stream_session_configured(1), "slot 1 released");
	zassert_equal(0U, audio_stream_session_configured_count(), "all slots cleared");
	zassert_equal(-EINVAL,
		      audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN),
		      "released slot rejects");
	zassert_equal(1U, fake_sink_push_count(), "no further push");
}

/* ── timestamp-cadence concealment (mono / Mode B) ─────────────────
 * The HCI packet sequence number can stay contiguous for a
 * controller-side omission (SW Split advances its session sequence only
 * on emitted SDUs), so one-CIS modes also merge the timestamp-cadence
 * evidence: omitted = MAX(seq_omitted, cadence_omitted), never the sum.
 * Mode A never runs cadence synthesis.
 */

ZTEST(audio_stream_session, test_ts_cadence_mono_gap_plc)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first SDU");

	/* Contiguous sequence but a 20 ms timestamp jump: one controller-
	 * side omitted event concealed as PLC BEFORE the current SDU. */
	zassert_ok(audio_stream_session_recv(0, true, true, 21000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(3U, fake_sink_push_count(), "1 PLC + 1 valid");
	zassert_equal(1U, audio_stats_get().plc_frames, "one concealed frame");
	zassert_equal(3U, audio_stats_get().total_frames, "first + PLC + current");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");
	zassert_true(fake_observer_last_push_l_valid(), "current SDU valid after PLC");
	zassert_true(fake_observer_last_push_r_valid(), "current SDU valid after PLC");
}

ZTEST(audio_stream_session, test_ts_cadence_modeb_gap_plc)
{
	setup_modeb();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, modeb10_lc3, MODEB_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first SDU");

	zassert_ok(audio_stream_session_recv(0, true, true, 21000, 2, modeb10_lc3, MODEB_LC3_LEN));
	zassert_equal(3U, fake_sink_push_count(), "1 PLC + 1 valid");
	zassert_equal(2U, audio_stats_get().plc_frames, "one PLC per channel");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");
}

ZTEST(audio_stream_session, test_ts_cadence_and_seq_merge_max)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first SDU");

	/* Sequence jump 1->4 reports 2 omitted; timestamp jump 1000->31000
	 * reports 2 omitted.  MAX merges to exactly TWO PLC pushes, never
	 * four (the sum). */
	zassert_ok(audio_stream_session_recv(0, true, true, 31000, 4, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(4U, fake_sink_push_count(), "2 PLC + 1 valid");
	zassert_equal(2U, audio_stats_get().plc_frames, "two concealed frames");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");
}

ZTEST(audio_stream_session, test_ts_cadence_no_ts_callback_no_false_plc)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first SDU");

	/* LOST callback (no TS) for the next grid event: consumed as a
	 * delivered position, no cadence synthesis beyond its own PLC
	 * render. */
	zassert_ok(audio_stream_session_recv(0, false, false, 0, 2, NULL, 0U));
	zassert_equal(2U, fake_sink_push_count(), "LOST renders its own PLC push");
	zassert_equal(1U, audio_stats_get().plc_frames, "only the LOST's own PLC");

	/* One true omitted event (timestamp span 1000 -> 31000 with the
	 * LOST covering position 2): exactly one cadence PLC, then the
	 * current SDU. */
	zassert_ok(audio_stream_session_recv(0, true, true, 31000, 3, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(4U, fake_sink_push_count(), "1 cadence PLC + 1 valid");
	zassert_equal(2U, audio_stats_get().plc_frames, "LOST PLC + one cadence PLC");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");
}

ZTEST(audio_stream_session, test_ts_cadence_resync_no_plc)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first SDU");

	/* Non-integral timestamp (half-interval offset): cadence RESYNC —
	 * no PLC synthesis, the current SDU still decodes once. */
	zassert_ok(audio_stream_session_recv(0, true, true, 15000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "current SDU only");
	zassert_equal(0U, audio_stats_get().plc_frames, "no concealment");
	zassert_equal(2U, audio_stats_get().total_frames, "both SDUs decoded");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");
}

ZTEST(audio_stream_session, test_ts_cadence_scaled_tolerance_boundary)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first SDU");
	zassert_equal(0U, audio_stats_get().plc_frames, "no PLC yet");

	/* Off-grid two-event span within the scaled tolerance (32 us base
	 * + 20 us two-event 10 ms SCA budget = 52 us; +20 us accepted):
	 * one cadence PLC concealment, current SDU decodes once.  The
	 * fixed 10 us contract resynced this delta; the scaled contract
	 * conceals it. */
	zassert_ok(audio_stream_session_recv(0, true, true, 21020, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(3U, fake_sink_push_count(), "1 PLC + 1 valid");
	zassert_equal(1U, audio_stats_get().plc_frames,
		      "one concealed frame within the scaled tolerance");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");

	/* One-event span +43 us: beyond the scaled 42 us tolerance →
	 * cadence RESYNC, no PLC synthesis, the current SDU still
	 * decodes once. */
	zassert_ok(audio_stream_session_recv(0, true, true, 31063, 3, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(4U, fake_sink_push_count(), "current SDU only after resync");
	zassert_equal(1U, audio_stats_get().plc_frames, "no concealment on resync");
	zassert_equal(4U, audio_stats_get().total_frames, "1 PLC + 3 decoded");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");
}

ZTEST(audio_stream_session, test_ts_cadence_modea_no_synthesis)
{
	setup_modea();

	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_ok(audio_stream_session_recv(1, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "event 1 pair");

	/* Left's next timestamp spans a silent interval (10000 -> 30000)
	 * but Mode A never runs cadence synthesis: no extra push, the
	 * assembler just stores the pending half. */
	zassert_ok(audio_stream_session_recv(0, true, true, 30000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "no cadence synthesis in Mode A");
	zassert_equal(0U, audio_stats_get().plc_frames, "no concealment");

	zassert_ok(audio_stream_session_recv(1, true, true, 30000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "event 2 pair after silent interval");
	zassert_equal(0U, audio_stats_get().plc_frames, "still no concealment");
	zassert_equal(4U, audio_stats_get().total_frames, "two events, two decoders each");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");
}

ZTEST(audio_stream_session, test_ts_cadence_reset_rebase_sessions)
{
	setup_mono();

	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(2U, fake_sink_push_count(), "two pushes");
	zassert_equal(0U, audio_stats_get().plc_frames, "no concealment");

	/* start_clear re-bases both trackers: a same-timestamp SDU in the
	 * next stream is FIRST, not a cadence gap. */
	audio_stream_session_start_clear();
	zassert_ok(audio_stream_session_recv(0, true, true, 2000, 2, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(3U, fake_sink_push_count(), "no PLC after start_clear");
	zassert_equal(0U, audio_stats_get().plc_frames, "no concealment");

	/* Release + reconfigure also re-bases: the same ts/seq as the very
	 * first session is a fresh FIRST. */
	audio_stream_session_release(0);
	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_ok(audio_stream_session_enable(0));
	audio_stream_session_rx_open();
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(4U, fake_sink_push_count(), "fresh session push");
	zassert_equal(0U, audio_stats_get().plc_frames, "no cadence gap across release");

	/* reset_all re-bases the cadence tracker as well. */
	audio_stream_session_rx_close();
	audio_stream_session_reset_all();
	zassert_ok(audio_stream_session_config(0, &mono_shape));
	zassert_ok(audio_stream_session_enable(0));
	audio_stream_session_rx_open();
	zassert_ok(audio_stream_session_recv(0, true, true, 1000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(5U, fake_sink_push_count(), "fresh after reset_all");
	zassert_equal(0U, audio_stats_get().plc_frames, "no cadence gap across reset_all");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");
}

ZTEST(audio_stream_session, test_lost_with_ts_consumes_cadence_position)
{
	setup_mono();

	/* A delivered LOST callback that CARRIES a timestamp (SW Split emits
	 * a timestamp on START/SINGLE HCI ISO packets, including emitted
	 * LOST SDUs) must consume its own cadence grid position: timestamps
	 * 10000/20000/30000 stay exactly contiguous, so the cadence tracker
	 * synthesizes nothing and the only PLC is the LOST SDU's own
	 * render.  Without position consumption the 10000 -> 30000 span
	 * would read as one omitted event and add an extra PLC push. */
	zassert_ok(audio_stream_session_recv(0, true, true, 10000, 1, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(1U, fake_sink_push_count(), "first valid SDU");
	zassert_equal(0U, audio_stats_get().plc_frames, "no PLC yet");

	zassert_ok(audio_stream_session_recv(0, false, true, 20000, 2, NULL, 0U));
	zassert_equal(2U, fake_sink_push_count(), "LOST renders its own PLC push");
	zassert_equal(1U, audio_stats_get().plc_frames, "exactly one PLC frame from the LOST SDU");
	zassert_equal(2U, audio_stats_get().total_frames, "one decoded + one PLC");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");

	zassert_ok(audio_stream_session_recv(0, true, true, 30000, 3, mono10_lc3, MONO_LC3_LEN));
	zassert_equal(3U, fake_sink_push_count(), "no extra cadence PLC before third callback");
	zassert_equal(1U, audio_stats_get().plc_frames, "still exactly one PLC frame");
	zassert_equal(3U, audio_stats_get().total_frames, "two decoded + one PLC");
	zassert_equal(0U, audio_stats_get().decode_errors, "no errors");
	zassert_true(fake_observer_last_push_l_valid(), "final push valid");
	zassert_true(fake_observer_last_push_r_valid(), "final push valid");
}

ZTEST_SUITE(audio_stream_session, NULL, NULL, NULL, NULL, NULL);
