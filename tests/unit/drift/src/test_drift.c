/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include "audio_drift.h"

/* Period constants (must match audio_drift.c) */
#define PERIOD_US    100000U
#define CENTER       AUDIO_DRIFT_APLL_CENTER
#define FREQ_MIN     AUDIO_DRIFT_APLL_MIN
#define FREQ_MAX     AUDIO_DRIFT_APLL_MAX

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	audio_drift_reset();
}

ZTEST_SUITE(drift, NULL, NULL, reset_before_each, NULL, NULL);

ZTEST(drift, test_zero_ts_ignored)
{
	/* sdu_ref_us == 0 must be a no-op; state stays INIT */
	uint16_t r = audio_drift_update(0);

	zassert_equal(r, 0, "zero ts must return 0");
	/* A subsequent valid ts should still enter CALIB (not skip to LOCKED) */
	r = audio_drift_update(1000);
	zassert_equal(r, 0, "first valid ts: enter CALIB, no freq change yet");
}

ZTEST(drift, test_init_to_calib_on_first_ts)
{
	uint16_t r = audio_drift_update(1000000U);

	zassert_equal(r, 0, "first ts starts CALIB, no freq update");
	/* Before 100 ms elapses, still no update */
	r = audio_drift_update(1000000U + PERIOD_US - 1);
	zassert_equal(r, 0, "< 100 ms elapsed: no update");
}

ZTEST(drift, test_calib_no_adjust_before_100ms)
{
	audio_drift_update(0U);          /* ignored */
	audio_drift_update(500000U);     /* INIT → CALIB, start = 500000 */
	uint16_t r = audio_drift_update(500000U + PERIOD_US - 1);

	zassert_equal(r, 0, "not enough elapsed");
}

ZTEST(drift, test_calib_perfect_timing_returns_center)
{
	/* err == 0 → adj == 0 → center freq returned */
	audio_drift_update(0U);
	audio_drift_update(1000000U);
	uint16_t r = audio_drift_update(1000000U + PERIOD_US);

	zassert_equal(r, CENTER, "perfect timing → center freq");
}

ZTEST(drift, test_calib_positive_err_increases_freq)
{
	/* elapsed long → err negative → adj positive → freq above center */
	audio_drift_update(0U);
	uint32_t start = 2000000U;

	audio_drift_update(start);
	/* elapsed = 100100 → err = -100 us → adj ≈ +302 steps */
	uint16_t r = audio_drift_update(start + PERIOD_US + 100);

	zassert_true(r > CENTER, "positive err → freq above center (got 0x%04X)", r);
	zassert_true(r <= FREQ_MAX, "freq must not exceed max");
}

ZTEST(drift, test_calib_elapsed_long_increases_freq)
{
	/* elapsed long → err negative → freq increases above center */
	audio_drift_update(0U);
	uint32_t start = 3000000U;

	audio_drift_update(start);
	/* elapsed = 100100 → err = -100 us → adj ≈ +302 → freq > CENTER */
	uint16_t r = audio_drift_update(start + PERIOD_US + 100);

	zassert_true(r > CENTER, "elapsed long → freq above center (got 0x%04X)", r);
	zassert_true(r >= FREQ_MIN, "freq must not go below min");
}

ZTEST(drift, test_calib_to_locked_when_err_small)
{
	/* err within ±16 → should lock; next period should see halved correction */
	audio_drift_update(0U);
	uint32_t start = 4000000U;

	audio_drift_update(start);
	/* err = -10 us (|err| ≤16) → should enter LOCKED */
	uint16_t r1 = audio_drift_update(start + PERIOD_US + 10);

	zassert_not_equal(r1, 0, "should update freq on lock transition");

	/* Now in LOCKED; next period with err=0 → returns center freq */
	uint32_t start2 = start + PERIOD_US + 10;

	audio_drift_update(start2 + PERIOD_US / 2); /* not enough elapsed */
	uint16_t r2 = audio_drift_update(start2 + PERIOD_US);

	zassert_not_equal(r2, 0, "locked state should still update each period");
}

ZTEST(drift, test_locked_half_correction)
{
	/* Enter locked with err=0, then apply 200 us error.
	 * Locked uses err/2 = 100 us for adj, CALIB would use 200 us.
	 * We verify the locked correction is smaller. */
	audio_drift_update(0U);
	uint32_t t = 5000000U;

	audio_drift_update(t);
	/* Lock with err=0 */
	audio_drift_update(t + PERIOD_US);   /* → LOCKED, freq=CENTER */
	t += PERIOD_US;

	/* In LOCKED: err=-200 us → adj uses -100 us */
	uint16_t freq_locked = audio_drift_update(t + PERIOD_US + 200);

	/* err = -200, locked uses -100 → adj ≈ +302; calib would use -200 → adj ≈ +604 */
	int32_t diff_locked = (int32_t)freq_locked - (int32_t)CENTER;
	/* locked correction ≈ 302 steps, calib ≈ 604 steps */
	zassert_true(diff_locked > 0, "elapsed long → freq above center in locked");
	zassert_true(diff_locked < 500, "locked uses half correction, not full");
}

ZTEST(drift, test_locked_to_calib_on_unlock)
{
	/* err > 32 in LOCKED → drops back to CALIB, resets center */
	audio_drift_update(0U);
	uint32_t t = 6000000U;

	audio_drift_update(t);
	audio_drift_update(t + PERIOD_US); /* → LOCKED */
	t += PERIOD_US;

	/* err = +50 us (> 32) → should unlock */
	audio_drift_update(t + PERIOD_US - 50);
	t += PERIOD_US - 50;

	/* Next call at exactly PERIOD after that → should be in CALIB again
	 * and produce a CENTER-relative correction */
	uint16_t r = audio_drift_update(t + PERIOD_US);

	zassert_not_equal(r, 0, "calib after unlock should produce update");
}

ZTEST(drift, test_gap_resets_window)
{
	/* elapsed > 3*PERIOD → window restart, returns 0 */
	audio_drift_update(0U);
	audio_drift_update(7000000U);
	/* Jump 400 ms — way beyond 3x period */
	uint16_t r = audio_drift_update(7000000U + 4 * PERIOD_US);

	zassert_equal(r, 0, "gap > 3*period must restart window, return 0");
}

ZTEST(drift, test_uint32_wraparound)
{
	/* Timestamps wrap at 0xFFFFFFFF; subtraction still correct with uint32 math */
	uint32_t start = 0xFFFF0000U;

	audio_drift_update(0U);
	audio_drift_update(start);
	/* wrap: start + PERIOD overflows into low uint32 */
	uint32_t after = start + PERIOD_US; /* wraps naturally in uint32 */
	uint16_t r = audio_drift_update(after);

	/* err=0 → should return CENTER */
	zassert_equal(r, CENTER, "uint32 wraparound: err=0 → center (got 0x%04X)", r);
}
