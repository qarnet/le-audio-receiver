/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned hook interface for the FLPR acceptance suite.
 *
 * Declares the FLPR_ACCEPTANCE_NATIVE_TEST hooks implemented inside
 * src/flpr_acceptance.c (test-only, GCOVR-excluded, absent from
 * production builds): reset state between tests, test-active control,
 * stall-ACK semaphore observability, stall token setter, and stall-side
 * stale-ACK count.  The ring-mgr side hooks come from
 * ../flpr_ring_mgr/src/flpr_ring_mgr_hooks.h.
 */
#ifndef FLPR_ACCEPTANCE_HOOKS_H_
#define FLPR_ACCEPTANCE_HOOKS_H_

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h> /* struct k_sem */

#ifdef __cplusplus
extern "C" {
#endif

/* Reset all acceptance module-static state and semaphores between
 * tests.  Implemented inside src/flpr_acceptance.c under the define. */
void flpr_acceptance_test_reset_state(void);

/* Arrange acceptance test-active mode (enables CRC/payload verification
 * and latency accounting inside the core consume paths). */
void flpr_acceptance_test_set_active(bool on);

/* Stall-ACK correlation observability (engine-owned instance). */
void flpr_acceptance_test_set_next_stall_token(uint16_t token);
uint32_t flpr_acceptance_test_stall_ack_sem_count(void);
uint32_t flpr_acceptance_test_stall_stale_count(void);

/* Fault-hang / stress semaphore observability. */
uint32_t flpr_acceptance_test_hang_ack_sem_count(void);
uint32_t flpr_acceptance_test_stress_sem_count(void);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_ACCEPTANCE_HOOKS_H_ */
