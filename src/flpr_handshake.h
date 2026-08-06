/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP ↔ FLPR handshake protocol.
 * VPR launcher boots FLPR; this module handles handshake, bidirectional
 * 1 Hz heartbeat (via k_work_delayable, independent of main loop).
 * Graceful if FLPR absent.
 *
 * Uses flpr_protocol.h (shared wire protocol) + flpr_peer (state machine).
 *
 * R8: production runtime only.  The stress-test ping/pong and the
 * fault-hang request state moved to src/flpr_acceptance.c (their
 * blocking state and counters); this module routes STRESS_PONG,
 * RING_TEST_REPORT, RING_STALL_ACK, and FAULT_HANG_ACK to the
 * registered diagnostic handler slot.  The shared flpr_status stress_*
 * fields are zeroed here (the acceptance module owns them and fills
 * them through flpr_acceptance_stress()/flpr_acceptance_stress_snapshot()).
 */

#ifndef FLPR_HANDSHAKE_H_
#define FLPR_HANDSHAKE_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include "flpr_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Snapshot of all handshake/health counters (shell-readable).
 * The stress_* fields are the shared shell-status contract; the
 * handshake module zeroes them (it does not own stress state — the
 * acceptance module does). */
struct flpr_status {
	bool ready;
	bool acked;
	bool healthy;
	uint32_t epoch;
	uint32_t ready_count;
	uint32_t reboot_count;
	uint32_t err_len;
	uint32_t err_version;
	uint32_t err_unknown;
	uint32_t err_send;
	uint32_t tx_seq;
	uint32_t tx_acked_seq;
	uint32_t rx_seq;
	uint32_t rx_lost;
	uint32_t rx_dup;
	uint32_t rx_ooo;
	uint32_t rx_last_ms;
	uint32_t rx_missed_total;

	/* Stress test (owned by flpr_acceptance; zero here). */
	bool stress_active;
	uint32_t stress_count;
	uint32_t stress_sent;
	uint32_t stress_recv;
	uint32_t stress_timeouts;
	uint32_t stress_stale;    /* PONG with stale cookie (before this run) */
	uint32_t stress_mismatch; /* PONG with unknown/future cookie */
	uint32_t stress_err_send; /* IPC send failures during stress */
};

/**
 * @brief Initialize FLPR handshake subsystem.
 *
 * Opens IPC, registers endpoint, starts k_work_delayable for 1 Hz heartbeat.
 * Non-blocking, non-fatal on absence.
 *
 * @return 0 on success, negative errno on failure.
 */
int flpr_handshake_init(void);

/** Race-safe snapshot of current status.  Stress fields are zeroed
 *  (acceptance-owned); use flpr_acceptance_stress_snapshot() to merge. */
void flpr_handshake_get_status(struct flpr_status *status);

/**
 * @brief Disconnect from FLPR: drain semaphores, deregister IPC endpoint.
 *
 * Marks session unavailable. Preserves lifetime counters (ready_count,
 * reboot_count, err_*, rx_missed_total) and last remote epoch.
 * Clears bound/ready/acked/healthy, session sequence/timestamps.
 * Heartbeat work stops sending until new reconnect.
 *
 * @return 0 on success, negative errno on failure.
 */
int flpr_handshake_disconnect(void);

/**
 * @brief Reconnect to FLPR: re-register the same endpoint/config.
 *
 * After this call, the FLPR session is available again. Caller should
 * wait for flpr_handshake_wait_bound() and then
 * flpr_handshake_wait_new_ready() with the previous epoch.
 *
 * @return 0 on success, negative errno on failure.
 */
int flpr_handshake_reconnect(void);

/**
 * @brief Wait for the IPC endpoint to become bound.
 *
 * @param timeout  Maximum time to wait.
 * @return 0 on success, -EAGAIN on timeout.
 */
int flpr_handshake_wait_bound(k_timeout_t timeout);

/**
 * @brief Wait for a new READY with a different epoch from previous_epoch.
 *
 * The semaphore is given only after READY_ACK send succeeds and the
 * FLPR epoch differs from @p previous_epoch.  Duplicate same-epoch
 * READY does NOT give this semaphore.
 *
 * If the epoch already differs from @p previous_epoch AND the READY_ACK
 * for it succeeded, the call succeeds immediately (the signal was
 * already posted); a changed epoch whose READY_ACK failed never
 * succeeds via this fast path.
 *
 * @param previous_epoch  The last known epoch (from disconnect snapshot).
 * @param timeout         Maximum time to wait.
 * @return 0 on success, -EAGAIN on timeout, -ECANCELED if unbound.
 */
int flpr_handshake_wait_new_ready(uint32_t previous_epoch, k_timeout_t timeout);

/**
 * @brief Send an arbitrary IPC message to FLPR on the existing endpoint.
 *
 * Thread-safe — may be called from any context (lock-free for sending).
 * Returns 0 on success, negative errno on failure (counts err_send).
 */
int flpr_handshake_send_msg(const struct flpr_msg *msg);

/**
 * @brief Callback type for ring-control / diagnostic message handlers.
 * Called from IPC receive context.  msg is NOT owned by the handler.
 */
typedef void (*flpr_handshake_ring_handler_t)(const struct flpr_msg *msg, void *user_data);

/**
 * @brief Register the PRODUCTION ring handlers (reset ACK + consumer).
 *
 * When FLPR sends RING_RESET_ACK or RING_CONSUMER, the registered
 * callbacks are invoked from the IPC receive callback WITHOUT holding
 * the module spinlock (flpr_lock is released before dispatch).
 *
 * Call with NULL to unregister.
 *
 * @param reset_ack_fn  Handler for FLPR_MSG_RING_RESET_ACK.
 * @param consumer_fn   Handler for FLPR_MSG_RING_CONSUMER.
 * @param user_data     Opaque pointer passed to each handler.
 */
void flpr_handshake_register_ring_handlers(flpr_handshake_ring_handler_t reset_ack_fn,
					   flpr_handshake_ring_handler_t consumer_fn,
					   void *user_data);

/**
 * @brief Register the DIAGNOSTIC message handler (R8).
 *
 * Receives FLPR_MSG_RING_TEST_REPORT, FLPR_MSG_RING_STALL_ACK,
 * FLPR_MSG_STRESS_PONG, and FLPR_MSG_FAULT_HANG_ACK from the IPC
 * receive callback, WITHOUT holding the module spinlock (snapshot under
 * flpr_lock / invoke outside — same semantics as the production slot).
 * Registered by flpr_acceptance_init() under
 * CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS.
 *
 * With no handler registered (NULL), these four message types are
 * silently dropped — identical to the pre-R8 inert behavior (the
 * acceptance state was core but inactive).  Production FLPR never sends
 * them without an acceptance request.
 *
 * @param diag_fn   Handler for the diagnostic message types.
 * @param user_data Opaque pointer passed to the handler.
 */
void flpr_handshake_register_diag_handlers(flpr_handshake_ring_handler_t diag_fn, void *user_data);

/**
 * @brief Callback type for health transition (healthy → unhealthy).
 * Invoked outside spinlock, from heartbeat work context.
 * Only called on transition — not on every health poll.
 */
typedef void (*flpr_health_transition_cb_t)(void *user_data);

/**
 * @brief Register callback for healthy→unhealthy transitions.
 * Called once per transition episode from heartbeat work context.
 * Pass NULL to unregister.
 */
void flpr_handshake_register_health_cb(flpr_health_transition_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_HANDSHAKE_H_ */
