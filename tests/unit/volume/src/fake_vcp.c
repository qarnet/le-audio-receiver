/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fake bt_vcp_vol_rend_register() backend for tests/unit/volume.
 *
 * Captures the registration parameters, stores a COPY of the callback
 * structure (never the pointer to the caller's stack registration
 * parameter), returns a test-controllable error, and lets tests invoke
 * the real production VCP state callback.
 */

#include "fake_vcp.h"

#include <string.h>
#include <errno.h>

static struct bt_vcp_vol_rend_cb captured_cb;
static bool cb_captured;
static uint8_t registered_volume;
static uint8_t registered_mute;
static uint8_t registered_step;
static int register_calls;
static int register_result;

void fake_vcp_reset(void)
{
	cb_captured = false;
	registered_volume = 0;
	registered_mute = 0;
	registered_step = 0;
	register_calls = 0;
	register_result = 0;
}

int bt_vcp_vol_rend_register(struct bt_vcp_vol_rend_register_param *param)
{
	register_calls++;
	registered_volume = param->volume;
	registered_mute = param->mute;
	registered_step = param->step;
	cb_captured = param->cb != NULL;
	if (cb_captured) {
		captured_cb = *param->cb;
	}
	return register_result;
}

int fake_vcp_register_calls(void)
{
	return register_calls;
}

uint8_t fake_vcp_registered_volume(void)
{
	return registered_volume;
}

uint8_t fake_vcp_registered_mute(void)
{
	return registered_mute;
}

uint8_t fake_vcp_registered_step(void)
{
	return registered_step;
}

void fake_vcp_set_register_result(int err)
{
	register_result = err;
}

bool fake_vcp_emit_state(struct bt_conn *conn, int err, uint8_t volume, uint8_t mute)
{
	if (!cb_captured || !captured_cb.state) {
		return false;
	}
	captured_cb.state(conn, err, volume, mute);
	return true;
}

bool fake_vcp_emit_flags(struct bt_conn *conn, int err, uint8_t flags)
{
	if (!cb_captured || !captured_cb.flags) {
		return false;
	}
	captured_cb.flags(conn, err, flags);
	return true;
}
