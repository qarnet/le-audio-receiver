/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUDIO_VOLUME_H
#define AUDIO_VOLUME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Register VCP Volume Renderer and set initial volume/mute state.
 *        Call once after bt_enable().
 * @retval 0 on success, negative errno on failure.
 */
int audio_volume_init(void);

/**
 * @brief Apply current volume and mute state to a PCM buffer in-place.
 *        Scales each int16_t sample by volume/255; zeroes buffer when muted.
 */
void audio_volume_apply(int16_t *buf, size_t samples);

uint8_t audio_volume_get(void);
bool    audio_volume_is_muted(void);

#endif /* AUDIO_VOLUME_H */
