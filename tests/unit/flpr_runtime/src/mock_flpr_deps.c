/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock implementations for flpr_handshake, flpr_ring, audio_offload.
 * Controllable via mock_set_* functions.
 */
#include "mock_flpr_deps.h"

/* ── Internal mock state ─────────────────────────────────── */

static uint32_t mock_epoch;
static int mock_disconnect_ret;
static int mock_reconnect_ret;
static int mock_bound_ret;
static int mock_ready_ret;
static bool mock_crc_match;
static bool mock_healthy;

/* ── flpr_handshake ──────────────────────────────────────── */

void flpr_handshake_get_status(struct flpr_status *out)
{
	if (out) {
		out->epoch = mock_epoch;
	}
}

int flpr_handshake_disconnect(void)
{
	return mock_disconnect_ret;
}

int flpr_handshake_reconnect(void)
{
	return mock_reconnect_ret;
}

int flpr_handshake_wait_bound(int timeout)
{
	(void)timeout;
	return mock_bound_ret;
}

int flpr_handshake_wait_new_ready(uint32_t old_epoch, int timeout)
{
	(void)old_epoch;
	(void)timeout;
	return mock_ready_ret;
}

/* ── flpr_ring.crc32 ─────────────────────────────────────── */

uint32_t flpr_ring_crc32(const uint8_t *data, size_t len)
{
	(void)data;
	(void)len;
	return mock_crc_match ? 0xDEADBEEF : 0xCAFEBABE;
}

/* ── audio_offload ───────────────────────────────────────── */

bool audio_offload_is_healthy(void)
{
	return mock_healthy;
}

/* ── Mock control API ────────────────────────────────────── */

void mock_set_handshake_epoch(uint32_t epoch)
{
	mock_epoch = epoch;
}

void mock_set_disconnect_result(int ret)
{
	mock_disconnect_ret = ret;
}

void mock_set_reconnect_result(int ret)
{
	mock_reconnect_ret = ret;
}

void mock_set_wait_bound_result(int ret)
{
	mock_bound_ret = ret;
}

void mock_set_wait_ready_result(int ret)
{
	mock_ready_ret = ret;
}

void mock_set_crc_match(bool match)
{
	mock_crc_match = match;
}

void mock_set_offload_healthy(bool healthy)
{
	mock_healthy = healthy;
}
