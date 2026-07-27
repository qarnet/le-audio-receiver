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

#ifdef __cplusplus
}
#endif

#endif /* FLPR_HANDSHAKE_H_ */
