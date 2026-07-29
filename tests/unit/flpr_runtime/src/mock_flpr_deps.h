/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock for flpr_handshake.h, flpr_ring.h, audio_offload.h.
 * All in one file to keep test module simple.
 */
#ifndef MOCK_FLPR_DEPS_H_
#define MOCK_FLPR_DEPS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── flpr_handshake.h mock ──────────────────────────────── */

struct flpr_status {
	uint32_t epoch;
};

void flpr_handshake_get_status(struct flpr_status *out);
int flpr_handshake_disconnect(void);
int flpr_handshake_reconnect(void);
int flpr_handshake_wait_bound(int timeout);
int flpr_handshake_wait_new_ready(uint32_t old_epoch, int timeout);

/* ── flpr_ring.h mock ───────────────────────────────────── */

uint32_t flpr_ring_crc32(const uint8_t *data, size_t len);

/* ── audio_offload.h mock ───────────────────────────────── */

bool audio_offload_is_healthy(void);

/* ── Mock control interface (called from tests) ─────────── */

/* Set the return values for next restart call. */
void mock_set_handshake_epoch(uint32_t epoch);
void mock_set_disconnect_result(int ret);
void mock_set_reconnect_result(int ret);
void mock_set_wait_bound_result(int ret);
void mock_set_wait_ready_result(int ret);
void mock_set_crc_match(bool match);
void mock_set_offload_healthy(bool healthy);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_FLPR_DEPS_H_ */
