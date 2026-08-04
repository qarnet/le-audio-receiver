/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * HISTORICAL / RETIRED sample-insert/drop actuator.
 *
 * Retired sample insert/drop actuator retained for regression
 * comparison only.  It is NOT a production actuator: production clock
 * steering is audio_clock_actuator_apll.c (nRF5340) and
 * audio_clock_actuator_none.c (nRF54L15).  This file lives under the
 * historical test suite (tests/unit/actuator_sample_adjust_historical)
 * and is not selectable in production Kconfig.
 *
 * It integrates the controller's ppm output in a fixed-point
 * accumulator; when |acc| crosses 1 sample, stages a ±1 adjustment
 * consumed by the historical consume_sample_adjustment().
 */

#include "audio_clock_actuator_sample_adjust_historical.h"

#include <zephyr/sys/util.h>

#define SAMPLES_PER_BLOCK 480
#define ACC_ONE_SAMPLE    1000000LL               /* 1 sample in 1e-6-sample units */
#define ACC_CLAMP         (10LL * ACC_ONE_SAMPLE) /* ±10 samples */

static int64_t acc; /* fixed-point, units = 1e-6 samples */
static int pending; /* -1, 0, or +1 staged for next block */

int audio_clock_actuator_init(void)
{
	acc = 0;
	pending = 0;
	return 0;
}

int audio_clock_actuator_apply_ppm(int32_t ppm)
{
	/* Integrate drift in 1e-6-sample units. 480 samples per 10 ms block. */
	acc += (int64_t)ppm * SAMPLES_PER_BLOCK;

	/* Clamp to prevent unbounded growth under sustained saturation. */
	acc = CLAMP(acc, -ACC_CLAMP, ACC_CLAMP);

	/* Stage adjustments. Multiple SDUs may accumulate before a push. */
	while (acc >= ACC_ONE_SAMPLE) {
		acc -= ACC_ONE_SAMPLE;
		pending++;
		; /* clamp outside loop */
	}
	while (acc <= -ACC_ONE_SAMPLE) {
		acc += ACC_ONE_SAMPLE;
		pending--;
	}
	/* Clamp pending to ±1 per block — we only adjust one sample at a time. */
	pending = CLAMP(pending, -1, 1);
	return 0;
}

int audio_clock_actuator_consume_sample_adjustment(void)
{
	int adj = pending;

	pending = 0;
	return adj;
}

int audio_clock_actuator_reset(void)
{
	acc = 0;
	pending = 0;
	return 0;
}
