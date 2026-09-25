/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-only identity passthrough suite main: real src/audio_i2s.c with
 * CONFIG_AUDIO_RESAMPLER_IDENTITY + 48000 Hz output.
 */

#include <zephyr/ztest.h>

#include "audio_i2s_test_helpers.h"

static void suite_before(void *unused)
{
	ARG_UNUSED(unused);
	test_reset_all();
}

ZTEST_SUITE(audio_i2s, NULL, NULL, suite_before, NULL, NULL);
