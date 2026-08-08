/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * HISTORICAL / RETIRED sample-insert/drop actuator interface.
 *
 * Test-local header for the retired sample-adjust actuator
 * (audio_clock_actuator_sample_adjust_historical.c).  This API is NOT
 * part of the production actuator contract: production clock steering
 * is audio_clock_actuator_apll.c (nRF5340) and
 * audio_clock_actuator_none.c (nRF54L15), whose public interface is
 * init/apply_ppm/reset only.  The consume_sample_adjustment() symbol
 * survives solely so the historical regression suite can exercise the
 * retired implementation.  Do not include this header from production
 * code.
 */

#ifndef AUDIO_CLOCK_ACTUATOR_SAMPLE_ADJUST_HISTORICAL_H
#define AUDIO_CLOCK_ACTUATOR_SAMPLE_ADJUST_HISTORICAL_H

#include <stdint.h>

/** Historical: init (zeroes accumulator and pending state). */
int audio_clock_actuator_init(void);

/** Historical: integrate ppm into the sample accumulator, stage ±1. */
int audio_clock_actuator_apply_ppm(int32_t ppm);

/** Historical: consume one staged ±1/0 adjustment. */
int audio_clock_actuator_consume_sample_adjustment(void);

/** Historical: reset accumulator and pending state. */
int audio_clock_actuator_reset(void);

#endif /* AUDIO_CLOCK_ACTUATOR_SAMPLE_ADJUST_HISTORICAL_H */
