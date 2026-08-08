/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Production NONE actuator suite.
 *
 * Compiles the real src/audio_clock_actuator_none.c and proves that
 * every API is a deterministic no-op returning zero (no state to
 * observe by construction).
 */

#include <zephyr/ztest.h>

#include <stdint.h>

#include "audio_clock_actuator.h"

ZTEST_SUITE(actuator_none, NULL, NULL, NULL, NULL, NULL);

ZTEST(actuator_none, test_all_returns_zero)
{
	zassert_equal(audio_clock_actuator_init(), 0, "init");
	zassert_equal(audio_clock_actuator_apply_ppm(0), 0, "zero ppm");
	zassert_equal(audio_clock_actuator_apply_ppm(1000), 0, "positive ppm");
	zassert_equal(audio_clock_actuator_apply_ppm(-1000), 0, "negative ppm");
	zassert_equal(audio_clock_actuator_apply_ppm(INT32_MAX), 0, "INT32_MAX ppm");
	zassert_equal(audio_clock_actuator_apply_ppm(INT32_MIN), 0, "INT32_MIN ppm");
	zassert_equal(audio_clock_actuator_reset(), 0, "reset");
}
