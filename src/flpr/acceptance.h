/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLPR-image acceptance/diagnostic handlers (R8).
 *
 * Owns the acceptance message/state moved out of src/flpr/main.c:
 *   - RING_TEST_START/STOP + the 5-report cascade (exact subtypes);
 *   - RING_STALL persistent/timed handling (stall_flags atomic, one-shot
 *     timer auto-clear, exact packed-value ACK echo);
 *   - STRESS_PING → STRESS_PONG echo;
 *   - FAULT_HANG → FAULT_HANG_ACK sent FIRST, then hang_pending set
 *     (ACK-before-spin) and deps->wake();
 *   - diagnostic counters/state hooks invoked from the production ring
 *     processing in main.c.
 *
 * Compiled only under CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS.  The module
 * never touches IPC/device/DT directly: main.c injects a narrow
 * dependency table (send + wake), which is what makes the message/state
 * logic directly testable on native_sim (tests/unit/flpr_acceptance_flpr).
 * main.c performs the hardware-only hang spin (irq_lock + busy-wait).
 */

#ifndef FLPR_ACCEPTANCE_H_
#define FLPR_ACCEPTANCE_H_

#include <stdbool.h>
#include <stdint.h>

#include "flpr_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Dependency table injected by main.c at boot. */
struct flpr_acceptance_deps {
	/** Send one IPC message to CPUAPP (ipc_service_send wrapper). */
	int (*send)(const struct flpr_msg *msg);
	/** Wake the main loop (give ring_wake_sem). */
	void (*wake)(void);
};

/**
 * @brief Initialize the FLPR acceptance handlers with the injected deps.
 */
void flpr_acceptance_init(const struct flpr_acceptance_deps *deps);

/**
 * @brief Dispatch one acceptance message from main.c's ep_received
 *        default case.
 *
 * @return true when the message was consumed by the acceptance module,
 *         false when unknown (main.c counts err_unknown).
 */
bool flpr_acceptance_handle_msg(const struct flpr_msg *msg);

/**
 * @brief Reset acceptance state after a ring reset (RING_RESET):
 *        stop the stall timer, clear stall flags, reset test/diag
 *        counters.  Called from main.c ring_reset_with_epoch().
 */
void flpr_acceptance_on_ring_reset(void);

/** True when a FAULT_HANG request is pending (main.c spins). */
bool flpr_acceptance_hang_pending(void);

/* ── Hooks invoked from production ring processing (main.c) ──────── */

/** True while a ring test is active (enables CRC verification). */
bool flpr_acceptance_test_active(void);

/** Current stall flags snapshot (FLPR_STALL_* bits). */
uint8_t flpr_acceptance_stall_flags(void);

void flpr_acceptance_note_worker_wake(void);
void flpr_acceptance_note_consume_ok(void);
void flpr_acceptance_note_consume_empty(void);
void flpr_acceptance_note_consume_stale(void);
void flpr_acceptance_note_produce_ok(void);
void flpr_acceptance_note_produce_full(void);
void flpr_acceptance_note_notify_rcv(void);
void flpr_acceptance_note_block_processed(void);
void flpr_acceptance_note_crc_error(void);
void flpr_acceptance_note_empty_poll(void);
void flpr_acceptance_note_epoch_stale(void);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_ACCEPTANCE_H_ */
