/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-local fakes of the FLPR subsystem APIs consumed by the nRF54
 * status commands in src/audio_shell.c, with knobs for the shell test
 * suite.  Only the status/restart paths used by the tested commands
 * carry state; the remaining functions exist so the production TU links.
 */

#ifndef FAKE_FLPR_DEPS_H
#define FAKE_FLPR_DEPS_H

#include <stdbool.h>
#include <stdint.h>

#include "audio_offload.h"
#include "flpr_handshake.h"
#include "flpr_ring_mgr.h"
#include "flpr_runtime.h"

/* ---- handshake ---- */
void test_flpr_set_status(const struct flpr_status *s);

/* ---- ring manager ---- */
void test_flpr_set_ring_status(const struct flpr_ring_status *s);
void test_flpr_set_stall_acked(uint32_t value);

/* ---- offload ---- */
void test_flpr_set_offload_status(const struct audio_offload_status *s);
void test_flpr_set_asrc_stats(const struct audio_offload_asrc_stats *s);
void test_flpr_set_offload_healthy(bool healthy);

/* ---- runtime ---- */
void test_flpr_set_runtime_status(const struct flpr_runtime_status *s);
void test_flpr_set_restart_result(int result);
int test_flpr_restart_calls(void);

/* ---- global reset between tests ---- */
void test_flpr_reset(void);

#endif /* FAKE_FLPR_DEPS_H */
