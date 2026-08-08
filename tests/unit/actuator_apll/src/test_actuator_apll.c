/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Production APLL actuator suite.
 *
 * Compiles the real src/audio_clock_actuator_apll.c against a
 * test-owned shadow of the installed <hal/nrf_clock.h> /
 * <nrfx_clock_hfclkaudio.h> and a mock that captures every register
 * write.  The ppm→register conversion is exercised through the
 * production function; no conversion logic is duplicated here.
 */

#include <zephyr/ztest.h>

#include <stdint.h>

#include "audio_clock_actuator.h"
#include "audio_drift.h"
#include "mock_clock.h"

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	mock_clock_reset();
}

ZTEST_SUITE(actuator_apll, NULL, NULL, reset_before_each, NULL, NULL);

/* ── init / reset write the center value ─────────────────────────── */

ZTEST(actuator_apll, test_init_writes_center)
{
	zassert_equal(audio_clock_actuator_init(), 0, "init succeeds");
	zassert_equal(mock_clock_write_count(), 1, "one register write");
	zassert_equal(mock_clock_write_at(0), AUDIO_DRIFT_APLL_CENTER, "center written");
}

ZTEST(actuator_apll, test_reset_writes_center)
{
	zassert_equal(audio_clock_actuator_reset(), 0, "reset succeeds");
	zassert_equal(mock_clock_write_count(), 1, "one register write");
	zassert_equal(mock_clock_write_at(0), AUDIO_DRIFT_APLL_CENTER, "center written");
}

/* ── exact conversions (offset = (ppm * 10) / 33) ────────────────── */

ZTEST(actuator_apll, test_exact_positive_conversion)
{
	/* 33 ppm → offset +10. */
	zassert_equal(audio_clock_actuator_apply_ppm(33), 0, "apply succeeds");
	zassert_equal(mock_clock_write_count(), 1, "one register write");
	zassert_equal(mock_clock_write_at(0), AUDIO_DRIFT_APLL_CENTER + 10, "center + 10");
}

ZTEST(actuator_apll, test_exact_negative_conversion)
{
	/* -33 ppm → offset -10. */
	zassert_equal(audio_clock_actuator_apply_ppm(-33), 0, "apply succeeds");
	zassert_equal(mock_clock_write_count(), 1, "one register write");
	zassert_equal(mock_clock_write_at(0), AUDIO_DRIFT_APLL_CENTER - 10, "center - 10");
}

/* ── near-zero truncation toward zero ────────────────────────────── */

ZTEST(actuator_apll, test_near_zero_truncation_stays_center)
{
	/* ±1, ±2, ±3 ppm → (ppm * 10) / 33 truncates to 0 → center. */
	for (int32_t ppm = -3; ppm <= 3; ppm++) {
		zassert_equal(audio_clock_actuator_apply_ppm(ppm), 0, "apply %d", ppm);
	}
	zassert_equal(mock_clock_write_count(), 7, "seven writes");
	for (int i = 0; i < 7; i++) {
		zassert_equal(mock_clock_write_at(i), AUDIO_DRIFT_APLL_CENTER,
			      "ppm near zero stays at center (write %d)", i);
	}
}

/* ── exact MIN/MAX values ────────────────────────────────────────── */

ZTEST(actuator_apll, test_exact_min_max_values)
{
	/* 9973 ppm → offset +3022 → exactly MAX; -9973 → MIN. */
	zassert_equal(audio_clock_actuator_apply_ppm(9973), 0, "apply +9973");
	zassert_equal(mock_clock_write_at(0), AUDIO_DRIFT_APLL_MAX, "exact MAX");

	zassert_equal(audio_clock_actuator_apply_ppm(-9973), 0, "apply -9973");
	zassert_equal(mock_clock_write_at(1), AUDIO_DRIFT_APLL_MIN, "exact MIN");
}

/* ── beyond both rails, including int32 extremes ─────────────────── */

ZTEST(actuator_apll, test_beyond_rails_clamped)
{
	zassert_equal(audio_clock_actuator_apply_ppm(10000), 0, "apply +10000");
	zassert_equal(mock_clock_write_at(0), AUDIO_DRIFT_APLL_MAX, "+10000 clamps to MAX");

	zassert_equal(audio_clock_actuator_apply_ppm(-10000), 0, "apply -10000");
	zassert_equal(mock_clock_write_at(1), AUDIO_DRIFT_APLL_MIN, "-10000 clamps to MIN");

	zassert_equal(audio_clock_actuator_apply_ppm(INT32_MAX), 0, "apply INT32_MAX");
	zassert_equal(mock_clock_write_at(2), AUDIO_DRIFT_APLL_MAX, "INT32_MAX clamps to MAX");

	zassert_equal(audio_clock_actuator_apply_ppm(INT32_MIN), 0, "apply INT32_MIN");
	zassert_equal(mock_clock_write_at(3), AUDIO_DRIFT_APLL_MIN, "INT32_MIN clamps to MIN");
}

/* ── repeated calls ──────────────────────────────────────────────── */

ZTEST(actuator_apll, test_repeated_calls)
{
	for (int i = 0; i < 3; i++) {
		zassert_equal(audio_clock_actuator_apply_ppm(33), 0, "apply %d", i);
	}
	zassert_equal(mock_clock_write_count(), 3, "three register writes");
	for (int i = 0; i < 3; i++) {
		zassert_equal(mock_clock_write_at(i), AUDIO_DRIFT_APLL_CENTER + 10, "write %d", i);
	}
}
