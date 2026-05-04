/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUDIO_I2S_H
#define AUDIO_I2S_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Initialize I2S output for PCM5102A.
 *
 * Configures the nRF5340 I2S0 as master (48kHz, 16-bit, stereo, standard
 * I2S format) and starts continuous DMA-driven output.  Initial buffers
 * are primed with silence.
 *
 * @retval 0 on success
 * @retval -ENODEV if I2S device not ready
 * @retval negative errno on other failures
 */
int audio_i2s_init(void);

/**
 * @brief Push stereo interleaved PCM data to I2S output.
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
 * @retval -EIO if I2S not initialized
 * @retval -ENOMEM if no free DMA slot (underrun; drop frame, PLC fills gap)
 */
int audio_i2s_push(const int16_t *stereo_data, size_t sample_count);

/**
 * @brief Stop I2S output (DROP trigger, frees in-flight buffers).
 */
void audio_i2s_stop(void);

#endif /* AUDIO_I2S_H */
