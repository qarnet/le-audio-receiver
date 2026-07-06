/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUDIO_SINK_H
#define AUDIO_SINK_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Initialize the audio sink backend.
 *
 * Configures the hardware output path (I2S, etc.) for 48 kHz, 16-bit,
 * stereo operation and starts continuous DMA-driven output. Initial
 * buffers are primed with silence.
 *
 * @retval 0 on success
 * @retval -ENODEV if output device not ready
 * @retval negative errno on other failures
 */
int audio_sink_init(void);

/**
 * @brief Push stereo interleaved PCM data to the audio sink.
 *
 * Data format: [L0, R0, L1, R1, ...] int16_t interleaved.
 * Expects exactly @p sample_count stereo samples (i.e. 2×n int16_t values).
 * Allocates from internal slab and queues via DMA.  Returns immediately
 * (non-blocking) unless all DMA buffer slots are exhausted (underrun).
 *
 * @param stereo_data  Interleaved stereo PCM (int16_t, L/R/L/R/...).
 * @param sample_count Total int16_t values in buffer (must be even).
 *
 * @retval 0 on success
 * @retval -EIO if sink not initialized
 * @retval -ENOMEM if no free DMA slot (underrun; drop frame, PLC fills gap)
 */
int audio_sink_push(const int16_t *stereo_data, size_t sample_count);

/**
 * @brief Stop audio sink output (DROP trigger, frees in-flight buffers).
 */
void audio_sink_stop(void);

/**
 * @brief Feed ISO SDU reference timestamp for APLL drift compensation.
 *
 * Must be called on every ISO RX event (valid or PLC) with the controller-
 * provided anchor timestamp. Drives a state machine (INIT→CALIB→LOCKED) that
 * adjusts the audio clock to track the BLE controller clock. No-op on hardware
 * without adjustable audio clock.
 *
 * @param sdu_ref_us  ISO SDU reference timestamp in microseconds (info->ts).
 */
void audio_sink_sdu_ref_update(uint32_t sdu_ref_us);

#endif /* AUDIO_SINK_H */
