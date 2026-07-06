/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUDIO_DRIFT_H
#define AUDIO_DRIFT_H

#include <stdint.h>

/*
 * APLL register constants for nRF5340 HFCLKAUDIO (12.288 MHz band).
 * One step ≈ 3.3 ppm. Range spans ≈ ±600 ppm around center.
 * Used by the APLL actuator; kept here for shared definition.
 */
#define AUDIO_DRIFT_APLL_CENTER 0x9BA6U
#define AUDIO_DRIFT_APLL_MIN    0x8FD8U
#define AUDIO_DRIFT_APLL_MAX    0xA774U

/**
 * PI controller update. Called per SDU with ISO timestamp and buffer fill.
 * Returns ppm correction (positive = speed up local clock).
 *
 * @param sdu_ref_us      ISO timestamp in microseconds (info->ts from ISO RX).
 * @param slab_free_count  Number of free blocks in the I2S DMA slab.
 * @return                 PPM correction to apply, 0 if no data yet.
 */
int32_t audio_drift_controller_update(uint32_t sdu_ref_us, int slab_free_count);

/** Reset controller to initial state. */
void audio_drift_reset(void);

/** Current state string for shell diagnostics. */
const char *audio_drift_state_str(void);

/** Current ppm output for shell diagnostics. */
int32_t audio_drift_get_ppm(void);

#endif /* AUDIO_DRIFT_H */
