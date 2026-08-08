/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned mock of the flpr_handshake API surface used by
 * flpr_runtime_restart(): get_status / disconnect / reconnect /
 * wait_bound / wait_new_ready.
 *
 * The mock models FLPR reboot semantics: a successful disconnect arms a
 * reboot, and wait_new_ready() only succeeds once the mock FLPR epoch
 * differs from the snapshot epoch (the real FLPR boots with a fresh
 * nonce).  Return values for every call are individually controllable.
 */
#ifndef MOCK_FLPR_HANDSHAKE_H_
#define MOCK_FLPR_HANDSHAKE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reset all mock state to defaults:
 *   epoch 0, all results success, reboot-on-disconnect enabled. */
void mock_hs_reset(void);

void mock_hs_set_epoch(uint32_t epoch);
void mock_hs_set_disconnect_result(int ret); /* <0 fails */
void mock_hs_set_reconnect_result(int ret);  /* <0 fails */

/* Wait-result overrides.  When set, wait_bound/wait_new_ready return the
 * given value unconditionally.  Clear to restore default behaviour. */
void mock_hs_set_wait_bound_result(int ret);
void mock_hs_clear_wait_bound_result(void);
void mock_hs_set_wait_ready_result(int ret);
void mock_hs_clear_wait_ready_result(void);

/* When enabled (default), a successful disconnect arms a mock FLPR reboot
 * so wait_new_ready sees a changed epoch.  Disable to force the
 * same-epoch failure path. */
void mock_hs_set_reboot_on_disconnect(bool enable);

/* Observation. */
uint32_t mock_hs_last_prev_epoch(void); /* epoch passed to wait_new_ready */
uint32_t mock_hs_wait_ready_calls(void);
uint32_t mock_hs_disconnect_calls(void);
uint32_t mock_hs_reconnect_calls(void);
uint32_t mock_hs_get_status_calls(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_FLPR_HANDSHAKE_H_ */
