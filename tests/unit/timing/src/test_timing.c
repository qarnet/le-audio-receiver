/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the pure helper functions from audio_timing_nrf54.c.
 * No nrfx hardware dependency — runs on native_sim.
 */

#include <zephyr/ztest.h>
#include <stdint.h>

/* ── Replicas of pure helpers from audio_timing_nrf54.c ──────────── */

/**
 * Expand a 32-bit ISO timestamp to a full 64-bit GRTC time.
 * (exact replica of iso_ts_to_grtc64 in src/audio_timing_nrf54.c)
 */
static uint64_t iso_ts_to_grtc64(uint32_t ts_us, uint64_t now_us)
{
	uint64_t upper = now_us & 0xFFFFFFFF00000000ULL;
	uint64_t full = upper | ts_us;

	if (ts_us < (now_us & UINT32_MAX)) {
		full += 0x100000000ULL;
	}
	return full;
}

/**
 * Unsigned 32-bit counter delta (handles wrap).
 * (exact replica of counter_delta_u32)
 */
static uint32_t counter_delta_u32(uint32_t current, uint32_t previous)
{
	return current - previous;
}

/**
 * Integer ppm: (measured - nominal) * 1e6 / nominal.
 * (exact replica of compute_ppm)
 */
static int32_t compute_ppm(uint32_t measured, uint32_t nominal_expected)
{
	if (nominal_expected == 0) {
		return 0;
	}
	int64_t diff = (int64_t)measured - (int64_t)nominal_expected;
	return (int32_t)((diff * 1000000LL) / (int64_t)nominal_expected);
}

/* ── Test: ISO timestamp to 64-bit GRTC conversion ───────────────── */

ZTEST(timing, test_iso_ts_no_wrap)
{
	/* Current GRTC time: 0x00000001_80000000 (ts in same 32-bit window) */
	uint64_t now = 0x0000000180000000ULL;
	uint32_t ts = 0x90000000U; /* ts >= lower 32 bits of now */

	uint64_t result = iso_ts_to_grtc64(ts, now);

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

	uint64_t result = iso_ts_to_grtc64(ts, now);

	uint64_t expected = 0x000000060FFFFFFFULL;
	zassert_equal(result, expected, "expected 0x%016llx got 0x%016llx",
		      (unsigned long long)expected, (unsigned long long)result);
}

ZTEST(timing, test_iso_ts_same_lower)
{
	/* ts == current lower 32 → no wrap */
	uint64_t now = 0x00000003ABCDEF00ULL;
	uint32_t ts = 0xABCDEF00U;

	uint64_t result = iso_ts_to_grtc64(ts, now);

	uint64_t expected = now; /* same value */
	zassert_equal(result, expected, "same lower case should be identity, got 0x%016llx",
		      (unsigned long long)result);
}

/* ── Test: unsigned 32-bit counter delta ────────────────────────── */

ZTEST(timing, test_counter_delta_u32_normal)
{
	uint32_t d = counter_delta_u32(1000, 500);
	zassert_equal(d, 500, "1000-500 = 500");
}

ZTEST(timing, test_counter_delta_u32_wrap)
{
	/* 32-bit wrap: 50 - 0xFFFFFF00 = 336 */
	uint32_t d = counter_delta_u32(50, 0xFFFFFF00U);
	zassert_equal(d, 306, "50 - 0xFFFFFF00 = 306");
}

ZTEST(timing, test_counter_delta_u32_zero)
{
	uint32_t d = counter_delta_u32(0x80000000, 0x80000000);
	zassert_equal(d, 0, "same → 0");
}

ZTEST(timing, test_counter_delta_u32_full_wrap)
{
	/* Exact wrap: 0 - 0xFFFFFFFF = 1 */
	uint32_t d = counter_delta_u32(0, 0xFFFFFFFF);
	zassert_equal(d, 1, "0 - 0xFFFFFFFF = 1");
}

/* ── Test: integer ppm calculation ──────────────────────────────── */

ZTEST(timing, test_ppm_exact_nominal)
{
	/* 47619 frames in 1000000 us @ 47619 Hz → 0 ppm */
	int32_t ppm = compute_ppm(47619, 47619);
	zassert_equal(ppm, 0, "exact nominal → 0 ppm, got %d", ppm);
}

ZTEST(timing, test_ppm_fast)
{
	/* +1 count more than nominal → positive ppm */
	/* 47620 measured, 47619 nominal → (1 * 1e6 / 47619) ≈ 21 ppm */
	int32_t ppm = compute_ppm(47620, 47619);
	zassert_true(ppm > 0, "fast clock → positive ppm, got %d", ppm);
	zassert_equal(ppm, 21, "expected ~21 ppm, got %d", ppm);
}

ZTEST(timing, test_ppm_slow)
{
	/* -1 count less than nominal → negative ppm */
	int32_t ppm = compute_ppm(47618, 47619);
	zassert_true(ppm < 0, "slow clock → negative ppm, got %d", ppm);
	zassert_equal(ppm, -21, "expected ~-21 ppm, got %d", ppm);
}

ZTEST(timing, test_ppm_zero_elapsed)
{
	/* nominal_expected == 0 → guard returns 0 */
	int32_t ppm = compute_ppm(100, 0);
	zassert_equal(ppm, 0, "zero nominal → 0 ppm");
}

ZTEST(timing, test_ppm_48k_nominal)
{
	/* 48000 frames, 48000 nominal → 0 ppm */
	int32_t ppm = compute_ppm(48000, 48000);
	zassert_equal(ppm, 0, "48k exact → 0 ppm");
}

ZTEST(timing, test_ppm_48k_offset)
{
	/* +5 count at 48000 nominal → 5 * 1e6 / 48000 ≈ 104 ppm */
	int32_t ppm = compute_ppm(48005, 48000);
	zassert_equal(ppm, 104, "expected 104 ppm, got %d", ppm);
}

/* ── Suite entry ────────────────────────────────────────────────── */

ZTEST_SUITE(timing, NULL, NULL, NULL, NULL, NULL);
