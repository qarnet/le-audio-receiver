/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test controls for the fake bt_vcp_vol_rend_register() backend
 * (tests/unit/volume only).
 */

#ifndef FAKE_VCP_H
#define FAKE_VCP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <zephyr/bluetooth/audio/vcp.h>

/* Reset all fake state (call before each test). */
void fake_vcp_reset(void);

/* Registration captured from the production audio_volume_init() call. */
int fake_vcp_register_calls(void);
uint8_t fake_vcp_registered_volume(void);
uint8_t fake_vcp_registered_mute(void);
uint8_t fake_vcp_registered_step(void);

/* Make the next bt_vcp_vol_rend_register() call return @p err
 * (0 = success, negative errno otherwise). */
void fake_vcp_set_register_result(int err);

/* Invoke the captured production state callback exactly like the real
 * VCP stack would.  Only the real production callback may be installed:
 * returns false if no callback was captured. */
bool fake_vcp_emit_state(struct bt_conn *conn, int err, uint8_t volume, uint8_t mute);

/* Invoke the captured production flags callback (unused by production). */
bool fake_vcp_emit_flags(struct bt_conn *conn, int err, uint8_t flags);

#endif /* FAKE_VCP_H */
