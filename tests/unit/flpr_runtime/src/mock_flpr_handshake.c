/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock implementation of the flpr_handshake API surface consumed by
 * flpr_runtime_restart().  See mock_flpr_handshake.h for controls.
 */
#include "mock_flpr_handshake.h"

#include <string.h>
#include <errno.h>

#include "flpr_handshake.h" /* real production header */

/* ── Mock state ─────────────────────────────────────────────── */

static uint32_t mock_epoch;
static int mock_disconnect_ret;
static int mock_reconnect_ret;
static bool mock_wait_bound_override;
static int mock_wait_bound_ret;
static bool mock_wait_ready_override;
static int mock_wait_ready_ret;
static bool mock_reboot_on_disconnect = true;
static bool mock_reboot_pending;

static uint32_t mock_wait_ready_calls;
static uint32_t mock_last_prev_epoch;
static uint32_t mock_disconnect_calls;
static uint32_t mock_reconnect_calls;
static uint32_t mock_get_status_calls;

/* ── flpr_handshake API ─────────────────────────────────────── */

void flpr_handshake_get_status(struct flpr_status *out)
{
	mock_get_status_calls++;
	if (out) {
		memset(out, 0, sizeof(*out));
		out->epoch = mock_epoch;
	}
}

int flpr_handshake_disconnect(void)
{
	mock_disconnect_calls++;
	if (mock_disconnect_ret < 0) {
		return mock_disconnect_ret;
	}
	if (mock_reboot_on_disconnect) {
		/* FLPR is being stopped; its next boot uses a fresh nonce. */
		mock_reboot_pending = true;
	}
	return 0;
}

int flpr_handshake_reconnect(void)
{
	mock_reconnect_calls++;
	return mock_reconnect_ret;
}

int flpr_handshake_wait_bound(k_timeout_t timeout)
{
	(void)timeout;
	if (mock_wait_bound_override) {
		return mock_wait_bound_ret;
	}
	return 0;
}

int flpr_handshake_wait_new_ready(uint32_t previous_epoch, k_timeout_t timeout)
{
	(void)timeout;
	mock_wait_ready_calls++;
	mock_last_prev_epoch = previous_epoch;

	if (mock_wait_ready_override) {
		return mock_wait_ready_ret;
	}

	if (mock_reboot_pending) {
		/* FLPR rebooted: new epoch differs from the snapshot. */
		mock_reboot_pending = false;
		mock_epoch = previous_epoch + 1;
		return 0;
	}

	/* No reboot: success requires a changed epoch. */
	return (mock_epoch != previous_epoch) ? 0 : -EAGAIN;
}

/* ── Test controls ──────────────────────────────────────────── */

void mock_hs_reset(void)
{
	mock_epoch = 0;
	mock_disconnect_ret = 0;
	mock_reconnect_ret = 0;
	mock_wait_bound_override = false;
	mock_wait_bound_ret = 0;
	mock_wait_ready_override = false;
	mock_wait_ready_ret = 0;
	mock_reboot_on_disconnect = true;
	mock_reboot_pending = false;
	mock_wait_ready_calls = 0;
	mock_last_prev_epoch = 0;
	mock_disconnect_calls = 0;
	mock_reconnect_calls = 0;
	mock_get_status_calls = 0;
}

void mock_hs_set_epoch(uint32_t epoch)
{
	mock_epoch = epoch;
}

void mock_hs_set_disconnect_result(int ret)
{
	mock_disconnect_ret = ret;
}

void mock_hs_set_reconnect_result(int ret)
{
	mock_reconnect_ret = ret;
}

void mock_hs_set_wait_bound_result(int ret)
{
	mock_wait_bound_override = true;
	mock_wait_bound_ret = ret;
}

void mock_hs_clear_wait_bound_result(void)
{
	mock_wait_bound_override = false;
}

void mock_hs_set_wait_ready_result(int ret)
{
	mock_wait_ready_override = true;
	mock_wait_ready_ret = ret;
}

void mock_hs_clear_wait_ready_result(void)
{
	mock_wait_ready_override = false;
}

void mock_hs_set_reboot_on_disconnect(bool enable)
{
	mock_reboot_on_disconnect = enable;
	mock_reboot_pending = false;
}

uint32_t mock_hs_last_prev_epoch(void)
{
	return mock_last_prev_epoch;
}

uint32_t mock_hs_wait_ready_calls(void)
{
	return mock_wait_ready_calls;
}

uint32_t mock_hs_disconnect_calls(void)
{
	return mock_disconnect_calls;
}

uint32_t mock_hs_reconnect_calls(void)
{
	return mock_reconnect_calls;
}

uint32_t mock_hs_get_status_calls(void)
{
	return mock_get_status_calls;
}
