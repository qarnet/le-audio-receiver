/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Platform-neutral audio timing measurement API.
 *
 * On nRF54L15 this measures I2S LRCK frame-clock progress against
 * Bluetooth controller / GRTC time using hardware GPPI routing
 * (TIMER20 counter + GRTC compare → capture).  On nRF5340 the
 * implementation is a no-op — the nRF5340 uses ISO-timestamp-based
 * PI drift compensation via audio_drift_controller_update().
 */

#ifndef AUDIO_TIMING_H
#define AUDIO_TIMING_H

#include <stdint.h>

/**
 * @brief Initialize platform-specific audio timing measurement.
 *
 * On nRF54L15: allocates a GRTC channel, configures TIMER20 in 32-bit
 * COUNTER mode, and routes I2S20 FRAMESTART → TIMER20 COUNT via GPPI.
 *
 * Must be called after the I2S peripheral is configured but before
 * DMA output starts.
 *
 * @retval 0 on success
 * @retval negative errno on failure
 */
int audio_timing_init(void);

/**
 * @brief Feed an ISO SDU reference timestamp with validated flags.
 *
 * Only the first SDU with both VALID and TS flags sets the
 * presentation-time anchor and schedules the first one-second
 * GRTC compare for diagnostic sampling.
 *
 * On nRF5340 this is a no-op.
 *
 * @param ts_us               ISO timestamp in microseconds (info->ts).
 * @param presentation_delay_us  Negotiated presentation delay (qos->pd).
 */
void audio_timing_sdu_ref_update(uint32_t ts_us, uint32_t presentation_delay_us);

/**
 * @brief Reset timing state (called on disconnect/stream stop).
 *
 * Cancels pending GRTC compare.  Statically allocated hardware
 * resources (GRTC channel, GPPI connections) are NOT freed and
 * will be reused across reconnects.
 */
void audio_timing_reset(void);

#endif /* AUDIO_TIMING_H */
