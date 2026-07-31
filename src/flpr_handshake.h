/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP ↔ FLPR handshake protocol.
 * VPR launcher boots FLPR; this module handles handshake, bidirectional
 * 1 Hz heartbeat (via k_work_delayable, independent of main loop), and
 * stress-test ping/pong. Graceful if FLPR absent.
 *
 * Uses flpr_protocol.h (shared wire protocol) + flpr_peer (state machine).
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

/* Snapshot of all handshake/health counters (shell-readable). */
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

	/* Stress test */
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

/** Race-safe snapshot of current status. */
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
 * @param previous_epoch  The last known epoch (from disconnect snapshot).
 * @param timeout         Maximum time to wait.
 * @return 0 on success, -EAGAIN on timeout, -ECANCELED if unbound.
 */
int flpr_handshake_wait_new_ready(uint32_t previous_epoch, k_timeout_t timeout);

/**
 * @brief Start stress test: send STRESS_PING messages, count PONG replies.
 *
 * Uses stop-and-wait with 200 ms timeout per ping. Runs synchronously in
 * calling thread context (NOT audio callback). Blocks for ~count*200ms.
 * Safe to call from shell or test thread only.
 *
 * @param count  Number of ping/pong to attempt (clamped to 1..1,000,000).
 * @param out    Filled with results on return (even on early timeout).
 */
void flpr_handshake_stress(uint32_t count, struct flpr_status *out);

/**
 * @brief Send an arbitrary IPC message to FLPR on the existing endpoint.
 *
 * Thread-safe — may be called from any context (lock-free for sending).
 * Returns 0 on success, negative errno on failure (counts err_send).
 */
int flpr_handshake_send_msg(const struct flpr_msg *msg);

/**
 * @brief Callback type for ring-control message handlers.
 * Called from IPC receive context.  msg is NOT owned by the handler.
 */
typedef void (*flpr_handshake_ring_handler_t)(const struct flpr_msg *msg, void *user_data);

/**
 * @brief Register handlers for ring-control IPC messages.
 *
 * When FLPR sends RING_RESET_ACK, RING_CONSUMER, RING_TEST_REPORT,
 * or RING_STALL_ACK, the registered callbacks are invoked from the
 * IPC receive callback WITHOUT holding the module spinlock
 * (flpr_lock is released before dispatch).
 *
 * Call with NULL to unregister.
 *
 * @param reset_ack_fn   Handler for FLPR_MSG_RING_RESET_ACK.
 * @param consumer_fn    Handler for FLPR_MSG_RING_CONSUMER.
 * @param report_fn      Handler for FLPR_MSG_RING_TEST_REPORT.
 * @param stall_ack_fn   Handler for FLPR_MSG_RING_STALL_ACK.
 * @param user_data      Opaque pointer passed to each handler.
 */
void flpr_handshake_register_ring_handlers(flpr_handshake_ring_handler_t reset_ack_fn,
					   flpr_handshake_ring_handler_t consumer_fn,
					   flpr_handshake_ring_handler_t report_fn,
					   flpr_handshake_ring_handler_t stall_ack_fn,
					   void *user_data);

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

/**
 * @brief Send a fault-hang request to FLPR and wait for ACK.
 *
 * Blocks up to @p timeout_ms for FAULT_HANG_ACK from FLPR.
 * After ACK, FLPR disables interrupts and spins forever —
 * ring and heartbeat stop, health transitions to unhealthy.
 *
 * @param timeout_ms  Maximum wait for ACK (typically 500 ms).
 * @return 0 on ACK received, -ETIMEDOUT on timeout, -EIO on send failure.
 */
int flpr_handshake_send_fault_hang(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_HANDSHAKE_H_ */
