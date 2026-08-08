/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned hook interface for the FLPR handshake suite.
 *
 * Compiled into src/flpr_handshake.c ONLY under FLPR_HANDSHAKE_NATIVE_TEST
 * (native_sim).  Production builds never include this header and contain
 * no test symbols.
 *
 * Hooks cover: heartbeat work lifecycle (cancel/reset, one synchronous
 * iteration, record-only async start), peer health/timestamp arrangement
 * (so the healthy→unhealthy transition is testable without a five-second
 * wall-clock wait), and semaphore observability.
 */
#ifndef FLPR_HANDSHAKE_HOOKS_H_
#define FLPR_HANDSHAKE_HOOKS_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cancel heartbeat work and reset ALL module-static state, semaphores,
 * and registered callbacks for a clean test. */
void flpr_handshake_test_reset(void);

/* Invoke one heartbeat work iteration synchronously in the calling
 * thread.  The handler reschedules; teardown cancels the pending work. */
void flpr_handshake_test_heartbeat_once(void);

/* Arrange peer health/timestamp state (bound/ready/acked/healthy and the
 * last remote heartbeat time) so transition tests need no wall-clock
 * wait.  State transitions still run through production helpers. */
void flpr_handshake_test_set_peer_state(bool bound, bool ready, bool acked, bool healthy,
					uint32_t rx_last_ms);

/* Heartbeat scheduling hooks (invoked by production code in test mode):
 * the READY-triggered async start is recorded but never submitted; the
 * per-iteration reschedule is submitted for real and canceled at
 * teardown. */
void flpr_handshake_test_work_start(void);
void flpr_handshake_test_work_reschedule(void);

uint32_t flpr_handshake_test_hb_start_requests(void);
uint32_t flpr_handshake_test_hb_reschedules(void);
bool flpr_handshake_test_work_pending(void);

/* Semaphore observability. */
uint32_t flpr_handshake_test_bound_sem_count(void);
uint32_t flpr_handshake_test_new_ready_sem_count(void);
uint32_t flpr_handshake_test_stress_sem_count(void);
uint32_t flpr_handshake_test_hang_ack_sem_count(void);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_HANDSHAKE_HOOKS_H_ */
