/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned hook interface for the FLPR ring manager suite.
 *
 * Compiled into src/flpr_ring_mgr.c ONLY under FLPR_RING_MGR_NATIVE_TEST
 * (native_sim).  Production builds never include this header and contain
 * no test symbols.
 *
 * native_sim has no MMU/address translation, so the fixed devicetree reg
 * addresses (0x2002C000..0x20030000) cannot be dereferenced.  This hook
 * replaces the DT ring pointers with two aligned host arrays of
 * FLPR_RING_TOTAL_SIZE each and routes the cycle counter through a
 * test-controlled source.  Production DT assertions are unchanged outside
 * test mode.
 */
#ifndef FLPR_RING_MGR_HOOKS_H_
#define FLPR_RING_MGR_HOOKS_H_

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h> /* struct k_sem declaration (R1 repair: the hook
			    * signatures take k_sem pointers, so the Zephyr
			    * kernel type must be declared here) */

#include "flpr_ring.h" /* FLPR_RING_TOTAL_SIZE */

#ifdef __cplusplus
extern "C" {
#endif

/* Test ring memory accessors (two aligned host arrays). */
uint8_t *flpr_ring_mgr_test_input_ring(void);
uint8_t *flpr_ring_mgr_test_output_ring(void);

/* Test-controlled cycle counter. */
uint32_t flpr_ring_mgr_test_cycle_get(void);
void flpr_ring_mgr_test_set_cycle(uint32_t cycles);

/* Reset all module-static state and semaphores between tests.
 * Implemented inside src/flpr_ring_mgr.c under the test define. */
void flpr_ring_mgr_test_reset_state(void);

/* Arrange test-active mode (enables CRC/payload verification and latency
 * metric accounting inside the production consume paths). */
void flpr_ring_mgr_test_set_test_active(bool on);

/* Semaphore observability where no public API exists. */
uint32_t flpr_ring_mgr_test_consume_sem_count(void);
uint32_t flpr_ring_mgr_test_reset_ack_sem_count(void);
uint32_t flpr_ring_mgr_test_stall_ack_sem_count(void);

/* R1 barrier-test gate: when armed, the next produce_block/produce_asrc
 * signals @p entered after produce begin (while still holding
 * ring_data_lock) and waits on @p release.  One-shot; reset clears it. */
void flpr_ring_mgr_test_arm_pause_after_produce_begin(struct k_sem *entered, struct k_sem *release);
/* Called by production produce paths under the test define (void). */
void flpr_ring_mgr_test_pause_after_produce_begin(void);

/* R1 ACK correlation observability. */
uint32_t flpr_ring_mgr_test_stale_ack_count(void);
void flpr_ring_mgr_test_set_next_reset_token(uint16_t token);
void flpr_ring_mgr_test_set_next_stall_token(uint16_t token);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_RING_MGR_HOOKS_H_ */
