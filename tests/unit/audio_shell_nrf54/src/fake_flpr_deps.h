/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test controls for the nRF54 shell FLPR API fakes (R8): the fake
 * implements the acceptance-module surface (flpr_acceptance_*), the
 * core handshake/ring/runtime/offload status APIs, and exposes setters
 * used by the audio_shell_nrf54 tests.
 */
#ifndef FAKE_FLPR_DEPS_H_
#define FAKE_FLPR_DEPS_H_

#include <stdbool.h>
#include <stdint.h>

#include "audio_offload.h"
#include "flpr_acceptance.h"
#include "flpr_handshake.h"
#include "flpr_ring_mgr.h"
#include "flpr_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* handshake */
void test_flpr_set_status(const struct flpr_status *s);

/* acceptance module */
void test_flpr_set_acceptance_status(const struct flpr_acceptance_status *s);
void test_flpr_set_hang_result(int result);
void test_flpr_set_ring_test_result(int result);
void test_flpr_set_stall_result(int result);
void test_flpr_set_stall_timed_result(int result);
void test_flpr_set_stall_acked(uint32_t value);
void test_flpr_set_stress_active(bool active);
void test_flpr_set_stress_snapshot(const struct flpr_status *s);
void test_flpr_set_gates_result(int result);

/* ring manager (core status) */
void test_flpr_set_ring_status(const struct flpr_ring_status *s);
void test_flpr_set_reset_result(int result);
void test_flpr_set_init_result(int result);

/* offload */
void test_flpr_set_offload_status(const struct audio_offload_status *s);
void test_flpr_set_asrc_stats(const struct audio_offload_asrc_stats *s);
void test_flpr_set_offload_healthy(bool healthy);

/* runtime */
void test_flpr_set_runtime_status(const struct flpr_runtime_status *s);
void test_flpr_set_restart_result(int result);
int test_flpr_restart_calls(void);

/* observation */
bool test_flpr_stall_producer_called(void);
bool test_flpr_stall_producer_value(void);

/* global reset */
void test_flpr_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_FLPR_DEPS_H_ */
