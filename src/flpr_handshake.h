/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP ↔ FLPR handshake protocol.
 * VPR launcher boots FLPR; this module receives READY, sends ACK,
 * maintains bidirectional heartbeat counters. Graceful if FLPR absent.
 */

#ifndef FLPR_HANDSHAKE_H_
#define FLPR_HANDSHAKE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Re-export protocol version from shared header. */
#include "flpr_protocol.h"

/* Handshake + heartbeat status exposed for shell diagnostics.
 * All counters are race-safe (spinlock-protected snapshot). */
struct flpr_status {
	bool ready;           /* FLPR sent READY? */
	bool acked;           /* CPUAPP sent ACK? */
	bool healthy;         /* heartbeat not missed too many */
	uint32_t ready_count; /* total READY received (reset epochs) */
	uint32_t epoch;       /* last FLPR boot nonce (epoch) */
	uint32_t error_count; /* version mismatch / short msg / IPC err */

	/* Heartbeat: CPUAPP → FLPR */
	uint32_t tx_seq;  /* last seq we sent */
	uint32_t tx_lost; /* cumulative ACK gaps (FLPR didn't echo) */

	/* Heartbeat: FLPR → CPUAPP */
	uint32_t rx_seq;     /* last seq received from FLPR */
	uint32_t rx_lost;    /* cumulative gaps */
	uint32_t rx_last_ms; /* last rx uptime (for staleness) */
	uint32_t rx_missed;  /* consecutive missed (resets on rx) */
};

/**
 * @brief Initialize the FLPR handshake subsystem.
 *
 * Must be called after the VPR launcher has released the FLPR from reset.
 * Opens IPC instance, registers endpoint, sends ACK on READY.
 * Gracefully tolerates absent/mismatched FLPR (one error logged, non-fatal).
 *
 * @return 0 on success (IPC endpoint registered), negative errno on failure.
 */
int flpr_handshake_init(void);

/**
 * @brief Get current handshake status (race-safe snapshot).
 */
void flpr_handshake_get_status(struct flpr_status *status);

/**
 * @brief Periodic heartbeat sender — call from application idle loop.
 *
 * Sends heartbeat to FLPR at ~1 Hz if handshake is established.
 * Tracks tx sequence, rx echo gaps, and missed-heartbeat health.
 * Never blocks: returns immediately if IPC not bound or send buffer full.
 */
void flpr_handshake_heartbeat(void);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_HANDSHAKE_H_ */
