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
 */
#define AUDIO_DRIFT_APLL_CENTER 0x9BA6U
#define AUDIO_DRIFT_APLL_MIN    0x8FD8U
#define AUDIO_DRIFT_APLL_MAX    0xA774U

/**
 * @brief Feed ISO SDU reference timestamp; compute APLL correction.
 *
 * Implements INIT → CALIB → LOCKED state machine over 100 ms measurement
 * windows. Hardware-independent: no nrfx calls; caller applies the result.
 *
 * @param sdu_ref_us  ISO timestamp in microseconds (info->ts from ISO RX).
 * @return            New APLL register value to apply, 0 if no update needed.
 */
uint16_t audio_drift_update(uint32_t sdu_ref_us);

/**
 * @brief Reset state machine to INIT. Call on full disconnect.
 *        After this call apply AUDIO_DRIFT_APLL_CENTER to hardware.
 */
void audio_drift_reset(void);

#endif /* AUDIO_DRIFT_H */
