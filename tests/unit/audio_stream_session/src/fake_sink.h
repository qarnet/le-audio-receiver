/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test knobs for the audio_sink.h fake used by the audio_stream_session
 * unit suite.
 */

#ifndef FAKE_SINK_H
#define FAKE_SINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void fake_sink_reset(void);
uint32_t fake_sink_first_push_hash(size_t n);
uint32_t fake_sink_push_count(void);
uint32_t fake_sink_last_sample_count(void);
uint32_t fake_sink_input_frames(void);
uint32_t fake_sink_fail_pushes(void);
void fake_sink_set_fail_next(bool fail);
void fake_sink_set_block_pushes(bool block);
void fake_sink_blocked_enter(void);   /* block until the fake enters a blocked push */
void fake_sink_blocked_release(void); /* let the blocked push finish */

#endif /* FAKE_SINK_H */
