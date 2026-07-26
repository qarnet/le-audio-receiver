/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for audio_timing_math.c — pure helpers with no nrfx
 * hardware dependency.  Runs on native_sim.
 *
 * The tested functions are compiled from src/audio_timing_math.c
 * directly, not replicated here.
 */

#include <zephyr/ztest.h>
#include <stdint.h>

#include "audio_timing_math.h"

/* ── Test: ISO timestamp to 64-bit GRTC conversion ───────────────── */

ZTEST(timing, test_iso_ts_no_wrap)
{
	/* Current GRTC time: 0x00000001_80000000 (ts in same 32-bit window) */
	uint64_t now = 0x0000000180000000ULL;
	uint32_t ts = 0x90000000U; /* ts >= lower 32 bits of now */

	uint64_t result = audio_timing_iso_ts_to_grtc64(ts, now);

	/* Expected: upper unchanged, lower = ts */
	uint64_t expected = 0x0000000190000000ULL;
	zassert_equal(result, expected, "expected 0x%016llx got 0x%016llx",
		      (unsigned long long)expected, (unsigned long long)result);
}

ZTEST(timing, test_iso_ts_wrap)
{
	/* ts < current lower 32 → wrap → add 0x100000000 */
	uint64_t now = 0x0000000580000000ULL;
	uint32_t ts = 0x0FFFFFFFU; /* ts (0x0FFF_FFFF) < 0x8000_0000 → wrap */

	uint64_t result = audio_timing_iso_ts_to_grtc64(ts, now);

	uint64_t expected = 0x000000060FFFFFFFULL;
	zassert_equal(result, expected, "expected 0x%016llx got 0x%016llx",
		      (unsigned long long)expected, (unsigned long long)result);
}

ZTEST(timing, test_iso_ts_same_lower)
{
	/* ts == current lower 32 → no wrap */
	uint64_t now = 0x00000003ABCDEF00ULL;
	uint32_t ts = 0xABCDEF00U;

	uint64_t result = audio_timing_iso_ts_to_grtc64(ts, now);

	uint64_t expected = now; /* same value */
	zassert_equal(result, expected, "same lower case should be identity, got 0x%016llx",
		      (unsigned long long)result);
}

/* ── Test: anchor_to_grtc64 — ts+pd before expansion ─────────────── */

ZTEST(timing, test_anchor_ts_behind_but_ts_plus_pd_ahead)
{
	/* Raw SDU timestamp is behind current GRTC, but the
	 * presentation target (= ts + pd_us) is ahead.
	 * Expanding the raw timestamp first would incorrectly
	 * place the anchor ~71.6 min ahead; the two-step expansion
	 * (ts + pd first, then expand) must return a nearby time.
	 *
	 * now     = 0x00000003_00000010  (upper=3, lower=0x10)
	 * sdu_ts  = 0xFFFF0000          (behind: < lower 0x10)
	 * pd_us   =   0x00020000
	 * target  = 0x00010000          (>= lower → no wrap)
	 * anchor  = 0x00000003_00010000
	 */
	uint64_t now = 0x0000000300000010ULL;
	uint32_t sdu_ts = 0xFFFF0000U;
	uint32_t pd_us = 0x00020000U;

	uint64_t result = audio_timing_anchor_to_grtc64(sdu_ts, pd_us, now);
	uint64_t expected = 0x0000000300010000ULL;

	zassert_equal(result, expected, "expected 0x%016llx got 0x%016llx",
		      (unsigned long long)expected, (unsigned long long)result);
}

ZTEST(timing, test_anchor_ts_pd_wraps_32bit)
{
	/* 32-bit addition wraps near UINT32_MAX.
	 * The wrap is correct: the anchor is one full 32-bit
	 * period ahead of the current GRTC time.
	 *
	 * now     = 0x00000005_00000030  (upper=5, lower=0x30)
	 * sdu_ts  = 0xFFFFFFF0          (near 32-bit max)
	 * pd_us   =   0x00000020
	 * target  = 0x00000010          (wraps: FFFFFFF0+20=10)
	 *               10 < 30 → wrap  → anchor = 0x00000006_00000010
	 */
	uint64_t now = 0x0000000500000030ULL;
	uint32_t sdu_ts = 0xFFFFFFF0U;
	uint32_t pd_us = 0x00000020U;

	uint64_t result = audio_timing_anchor_to_grtc64(sdu_ts, pd_us, now);
	uint64_t expected = 0x0000000600000010ULL;

	zassert_equal(result, expected, "wrap case expected 0x%016llx got 0x%016llx",
		      (unsigned long long)expected, (unsigned long long)result);
}

ZTEST(timing, test_anchor_identical_to_iso_expand)
{
	/* When pd_us == 0, anchor_to_grtc64 must behave identically
	 * to iso_ts_to_grtc64.
	 */
	uint64_t now = 0x0000000180000000ULL;
	uint32_t sdu_ts = 0x90000000U;

	uint64_t result_anchor = audio_timing_anchor_to_grtc64(sdu_ts, 0, now);
	uint64_t result_iso = audio_timing_iso_ts_to_grtc64(sdu_ts, now);

	zassert_equal(result_anchor, result_iso, "anchor(ts,0) != iso(ts): 0x%016llx vs 0x%016llx",
		      (unsigned long long)result_anchor, (unsigned long long)result_iso);
}

/* Stale-target detection boundary: when the target (= ts + pd) is
 * still in the same 32-bit epoch as now_lower, the expansion does
 * not add 0x100000000 and the anchor stays in the current epoch.
 * The caller must verify anchor is safely ahead of 'now' before
 * programming a GRTC compare; the fallback is 'now + 1 s'.
 * See audio_timing_sdu_ref_update() in audio_timing_nrf54.c.
 */
ZTEST(timing, test_anchor_same_epoch_no_wrap)
{
	/* target 0xFFFF0010 >= now_lower 0x00000100 → no wrap
	 * anchor = 0x00000005_FFFF0010
	 */
	uint64_t now = 0x0000000500000100ULL;
	uint32_t sdu_ts = 0xFFFF0000U;
	uint32_t pd_us = 0x00000010U;

	uint64_t anchor = audio_timing_anchor_to_grtc64(sdu_ts, pd_us, now);
	uint64_t expected = 0x00000005FFFF0010ULL;

	zassert_equal(anchor, expected, "expected 0x%016llx got 0x%016llx",
		      (unsigned long long)expected, (unsigned long long)anchor);

	/* Anchor is ahead of 'now' — caller's anchor+1s check passes */
	zassert_true(anchor > now, "anchor should be ahead of now for same-epoch target");
}

/* ── Test: unsigned 32-bit counter delta ────────────────────────── */

ZTEST(timing, test_counter_delta_u32_normal)
{
	uint32_t d = audio_timing_counter_delta_u32(1000, 500);
	zassert_equal(d, 500, "1000-500 = 500");
}

ZTEST(timing, test_counter_delta_u32_wrap)
{
	/* 32-bit wrap: 50 - 0xFFFFFF00 = 306 */
	uint32_t d = audio_timing_counter_delta_u32(50, 0xFFFFFF00U);
	zassert_equal(d, 306, "50 - 0xFFFFFF00 = 306");
}

ZTEST(timing, test_counter_delta_u32_zero)
{
	uint32_t d = audio_timing_counter_delta_u32(0x80000000, 0x80000000);
	zassert_equal(d, 0, "same → 0");
}

ZTEST(timing, test_counter_delta_u32_full_wrap)
{
	/* Exact wrap: 0 - 0xFFFFFFFF = 1 */
	uint32_t d = audio_timing_counter_delta_u32(0, 0xFFFFFFFF);
	zassert_equal(d, 1, "0 - 0xFFFFFFFF = 1");
}

/* ── Test: integer ppm calculation ──────────────────────────────── */

ZTEST(timing, test_ppm_exact_nominal)
{
	/* 47619 frames in 1000000 us @ 47619 Hz → 0 ppm */
	int32_t ppm = audio_timing_compute_ppm(47619, 47619);
	zassert_equal(ppm, 0, "exact nominal → 0 ppm, got %d", ppm);
}

ZTEST(timing, test_ppm_fast)
{
	/* +1 count more than nominal → positive ppm */
	/* 47620 measured, 47619 nominal → (1 * 1e6 / 47619) ≈ 21 ppm */
	int32_t ppm = audio_timing_compute_ppm(47620, 47619);
	zassert_true(ppm > 0, "fast clock → positive ppm, got %d", ppm);
	zassert_equal(ppm, 21, "expected ~21 ppm, got %d", ppm);
}

ZTEST(timing, test_ppm_slow)
{
	/* -1 count less than nominal → negative ppm */
	int32_t ppm = audio_timing_compute_ppm(47618, 47619);
	zassert_true(ppm < 0, "slow clock → negative ppm, got %d", ppm);
	zassert_equal(ppm, -21, "expected ~-21 ppm, got %d", ppm);
}

ZTEST(timing, test_ppm_zero_elapsed)
{
	/* nominal_expected == 0 → guard returns 0 */
	int32_t ppm = audio_timing_compute_ppm(100, 0);
	zassert_equal(ppm, 0, "zero nominal → 0 ppm");
}

ZTEST(timing, test_ppm_48k_nominal)
{
	/* 48000 frames, 48000 nominal → 0 ppm */
	int32_t ppm = audio_timing_compute_ppm(48000, 48000);
	zassert_equal(ppm, 0, "48k exact → 0 ppm");
}

ZTEST(timing, test_ppm_48k_offset)
{
	/* +5 count at 48000 nominal → 5 * 1e6 / 48000 ≈ 104 ppm */
	int32_t ppm = audio_timing_compute_ppm(48005, 48000);
	zassert_equal(ppm, 104, "expected 104 ppm, got %d", ppm);
}

/* ── Suite entry ────────────────────────────────────────────────── */

ZTEST_SUITE(timing, NULL, NULL, NULL, NULL, NULL);
