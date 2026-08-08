/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test knobs for the audio_volume.h fake used by the audio_stream_session
 * unit suite.
 */

#ifndef FAKE_VOLUME_H
#define FAKE_VOLUME_H

#include <stdint.h>

void fake_volume_reset(void);
uint32_t fake_volume_apply_count(void);

#endif /* FAKE_VOLUME_H */
