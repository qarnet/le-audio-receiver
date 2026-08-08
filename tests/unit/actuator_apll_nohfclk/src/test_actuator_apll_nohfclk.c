/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * No-HFCLKAUDIO variant of the production APLL actuator suite.
 *
 * Compiles the same src/audio_clock_actuator_apll.c with
 * NRF_CLOCK_HAS_HFCLKAUDIO=0 and proves every API is a no-op with no
 * register writes.  No conversion logic is duplicated here.
 */

#include <zephyr/ztest.h>

#include <stdint.h>

#include "audio_clock_actuator.h"
#include "mock_clock.h"

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	mock_clock_reset();
}

ZTEST_SUITE(actuator_apll_nohfclk, NULL, NULL, reset_before_each, NULL, NULL);

ZTEST(actuator_apll_nohfclk, test_all_noop_without_hfclkaudio)
{
	zassert_equal(audio_clock_actuator_init(), 0, "init no-op");
	zassert_equal(audio_clock_actuator_apply_ppm(0), 0, "zero no-op");
	zassert_equal(audio_clock_actuator_apply_ppm(1000), 0, "positive no-op");
	zassert_equal(audio_clock_actuator_apply_ppm(-1000), 0, "negative no-op");
	zassert_equal(audio_clock_actuator_apply_ppm(INT32_MAX), 0, "INT32_MAX no-op");
	zassert_equal(audio_clock_actuator_apply_ppm(INT32_MIN), 0, "INT32_MIN no-op");
	zassert_equal(audio_clock_actuator_reset(), 0, "reset no-op");

	zassert_equal(mock_clock_write_count(), 0, "no register writes without HFCLKAUDIO");
}
