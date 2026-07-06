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

#endif /* AUDIO_CLOCK_ACTUATOR_H */
