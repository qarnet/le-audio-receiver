/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP ↔ FLPR handshake protocol.
 * VPR launcher boots FLPR; this module receives READY, sends ACK,
 * and maintains heartbeat counters. Graceful if FLPR absent.
 */

#ifndef FLPR_HANDSHAKE_H_
#define FLPR_HANDSHAKE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol version — must match src/flpr/main.c FLPR_PROTOCOL_VERSION. */
#define FLPR_PROTOCOL_VERSION 1U

/* Message types. */
#define FLPR_MSG_READY     0x01U
#define FLPR_MSG_ACK       0x02U
#define FLPR_MSG_HEARTBEAT 0x03U

/* Fixed-size message — keep in sync with src/flpr/main.c. */
struct flpr_msg {
	uint8_t type;    /* FLPR_MSG_* */
	uint8_t version; /* protocol version */
	uint16_t seq;    /* sequence number (wrapping) */
	uint32_t data;   /* heartbeat counter (uptime ms) */
};

/* Handshake status exposed for shell diagnostics.
 * All counters are safe to read from any context (atomic).
 */
struct flpr_status {
	bool ready;            /* FLPR sent READY? */
	bool acked;            /* CPUAPP sent ACK? */
	uint32_t rx_heartbeat; /* last FLPR heartbeat counter */
	uint32_t tx_heartbeat; /* last CPUAPP heartbeat counter */
	uint32_t ready_count;  /* total READY messages received (epochs) */
	uint32_t error_count;  /* version mismatch / IPC errors */
};

/**
 * @brief Initialize the FLPR handshake subsystem.
 *
 * Must be called after the VPR launcher has released the FLPR from reset.
 * Opens IPC instance, registers endpoint, sends ACK on READY.
 * Gracefully tolerates absent/mismatched FLPR.
 *
 * @return 0 on success (IPC endpoint registered).
 */
int flpr_handshake_init(void);

/**
 * @brief Get current handshake status (snapshot of atomic counters).
 *
 * @param status  Output structure to fill.
 */
void flpr_handshake_get_status(struct flpr_status *status);

/**
 * @brief Periodic heartbeat sender — call from application idle loop.
 *
 * Sends heartbeat to FLPR every ~1 s if handshake is established.
 * Returns immediately if no IPC bound yet.
 */
void flpr_handshake_heartbeat(void);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_HANDSHAKE_H_ */
