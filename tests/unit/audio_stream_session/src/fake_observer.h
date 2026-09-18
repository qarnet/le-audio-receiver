/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test knobs for the fake bsim_observer used by the audio_stream_session
 * unit suite.
 */

#ifndef FAKE_OBSERVER_H
#define FAKE_OBSERVER_H

#include <stdbool.h>
#include <stdint.h>

void fake_observer_reset(void);
uint32_t fake_observer_malformed_sdu(void);
uint32_t fake_observer_stale_half(void);
uint32_t fake_observer_missing_ts(void);
uint32_t fake_observer_pre_push_count(void);
bool fake_observer_last_push_l_valid(void);
bool fake_observer_last_push_r_valid(void);
uint16_t fake_observer_last_push_l_payload_len(void);
uint16_t fake_observer_last_push_r_payload_len(void);
const uint8_t *fake_observer_last_push_l_payload(void);
const uint8_t *fake_observer_last_push_r_payload(void);
bool fake_observer_pre_push_l_valid_at(uint32_t index);
bool fake_observer_pre_push_r_valid_at(uint32_t index);
uint16_t fake_observer_pre_push_l_payload_len_at(uint32_t index);
uint16_t fake_observer_pre_push_r_payload_len_at(uint32_t index);
const uint8_t *fake_observer_pre_push_l_payload_at(uint32_t index);
const uint8_t *fake_observer_pre_push_r_payload_at(uint32_t index);

#endif /* FAKE_OBSERVER_H */
