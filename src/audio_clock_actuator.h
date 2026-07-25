/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUDIO_CLOCK_ACTUATOR_H
#define AUDIO_CLOCK_ACTUATOR_H

#include <stdint.h>

/**
 * Initialize the clock actuator (e.g. set APLL to center frequency).
 * Returns 0 on success, negative errno on failure.
 */
int audio_clock_actuator_init(void);

/**
 * Apply a ppm correction to the clock.
 * Positive ppm = local clock too slow, speed it up.
 * Negative ppm = local clock too fast, slow it down.
 * Returns 0 on success, negative errno on failure.
 */
int audio_clock_actuator_apply_ppm(int32_t ppm);

/**
 * Reset the actuator to its default/center state.
 */
int audio_clock_actuator_reset(void);

/**
 * Consume a pending single-sample adjustment (insert/drop).
 * Returns:
 *   +1  → drop one stereo sample from the next output block
 *   -1  → insert one stereo sample (duplicate) into the next output block
 *    0  → no adjustment
 *
 * Only meaningful for actuators that manipulate the audio data path
 * (e.g. SAMPLE_ADJUST). APLL actuator returns 0 always (clock steering
 * happens at the clock, not the data).
 */
int audio_clock_actuator_consume_sample_adjustment(void);

#endif /* AUDIO_CLOCK_ACTUATOR_H */
