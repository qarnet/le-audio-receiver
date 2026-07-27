/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared wire protocol between CPUAPP (ARM) and FLPR (RISC-V VPR).
 * Single source of truth for message layout, type constants, version,
 * sequence tracking, and state machine helpers.
 *
 * Both cores little-endian, no byteswap needed.
 * RV32E has no atomic extension — struct is naturally aligned, no packed.
 */

#ifndef FLPR_PROTOCOL_H_
#define FLPR_PROTOCOL_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <zephyr/sys/__assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Protocol version ──────────────────────────────────────────── */

#define FLPR_PROTOCOL_VERSION 2U

/* ── Message types ─────────────────────────────────────────────── */

#define FLPR_MSG_READY         0x01U /* FLPR → CPUAPP: boot complete */
#define FLPR_MSG_READY_ACK     0x02U /* CPUAPP → FLPR: acknowledged */
#define FLPR_MSG_HEARTBEAT     0x03U /* Bidirectional: liveness */
#define FLPR_MSG_HEARTBEAT_ACK 0x04U /* Echo of heartbeat */
#define FLPR_MSG_STRESS_PING   0x05U /* CPUAPP → FLPR: stress test */
#define FLPR_MSG_STRESS_PONG   0x06U /* FLPR → CPUAPP: stress response */

/* ── Timing ────────────────────────────────────────────────────── */

#define FLPR_HEARTBEAT_INTERVAL_MS 1000U
#define FLPR_HEARTBEAT_MISS_MAX    5U
#define FLPR_STRESS_DEFAULT_COUNT  100000U
#define FLPR_STRESS_MAX_COUNT      1000000U

/* ── Wire message ────────────────────────────────────────────────
 * 8 bytes, naturally aligned on both ARM and RV32E.
 * No __packed — both cores access fields at natural alignment.
 */
struct flpr_msg {
	uint8_t type;    /* FLPR_MSG_* */
	uint8_t version; /* FLPR_PROTOCOL_VERSION */
	uint16_t seq;    /* monotonic per-sender, wraps at 16 bits */
	uint32_t data;   /* READY: epoch, HB: uptime, STRESS: cookie */
};

/* Compile-time safety checks. */
BUILD_ASSERT(sizeof(struct flpr_msg) == 8, "flpr_msg must be exactly 8 bytes");
BUILD_ASSERT(offsetof(struct flpr_msg, type) == 0, "type offset");
BUILD_ASSERT(offsetof(struct flpr_msg, version) == 1, "version offset");
BUILD_ASSERT(offsetof(struct flpr_msg, seq) == 2, "seq offset");
BUILD_ASSERT(offsetof(struct flpr_msg, data) == 4, "data offset");

/* Endianness: both ARMv8-M and RV32E are little-endian. */
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Protocol requires little-endian CPU"
#endif

/* ── Sequence number helpers (pure, no atomics needed) ──────────── */

/** True if a comes after b with 16-bit wrap (gap <= 32767). */
static inline bool flpr_seq_after(uint16_t a, uint16_t b)
{
	return (int16_t)(a - b) > 0;
}

/** Signed gap between two sequence numbers. */
static inline int16_t flpr_seq_diff(uint16_t a, uint16_t b)
{
	return (int16_t)(a - b);
}

/** Unsigned gap (positive, 0 if equal, wraps at 16 bits). */
static inline uint16_t flpr_seq_gap(uint16_t after, uint16_t before)
{
	return (uint16_t)(after - before);
}

/* ── Per-peer state tracking ─────────────────────────────────────
 * One instance tracks the remote peer. NOT thread-safe — caller
 * must serialize access (spinlock on CPUAPP, single-threaded on FLPR).
 */
struct flpr_peer {
	bool bound; /* IPC endpoint bound? */

	/* Remote peer state (what we know about them). */
	bool ready;           /* remote sent READY? */
	bool acked;           /* we sent READY_ACK? */
	bool healthy;         /* heartbeat not missed too many? */
	uint32_t epoch;       /* remote boot nonce */
	uint32_t ready_count; /* total READY received */

	/* Remote → local sequence tracking (heartbeats we receive). */
	uint32_t rx_seq;          /* last seq received (32-bit, wraps at 16-bit) */
	uint32_t rx_lost;         /* cumulative gaps counted */
	uint32_t rx_dup;          /* duplicate heartbeats */
	uint32_t rx_ooo;          /* out-of-order (backward delta, counted) */
	uint32_t rx_last_ms;      /* last rx uptime for staleness */
	uint32_t rx_missed_total; /* cumulative heartbeat check failures */

	/* Local → remote tracking (heartbeats we send). */
	uint32_t tx_seq;       /* last seq we sent */
	uint32_t tx_acked_seq; /* last seq remote acknowledged */

	/* Error counters. */
	uint32_t err_len;     /* short/oversize messages */
	uint32_t err_version; /* version mismatch */
	uint32_t err_unknown; /* unknown message type */
	uint32_t err_send;    /* IPC send failures */
};

/* ── Message validation ───────────────────────────────────────────
 * Returns true if msg is valid for processing.
 * Updates peer error counters on rejection.
 */
static inline bool flpr_msg_validate(const struct flpr_msg *msg, size_t len, struct flpr_peer *peer)
{
	if (len != sizeof(struct flpr_msg)) {
		peer->err_len++;
		return false;
	}
	if (msg->version != FLPR_PROTOCOL_VERSION) {
		peer->err_version++;
		return false;
	}
	return true;
}

/* ── Sequence tracking ───────────────────────────────────────────
 * Process a received sequence number from the remote peer.
 * Updates rx_seq, rx_lost, rx_dup, rx_ooo.
 * Call AFTER msg_validate().
 */
static inline void flpr_peer_rx_seq(struct flpr_peer *peer, uint16_t seq, uint32_t now_ms)
{
	if (peer->rx_seq == 0 && peer->rx_last_ms == 0) {
		/* First heartbeat ever received. */
		peer->rx_seq = seq;
		peer->rx_last_ms = now_ms;
		return;
	}

	int16_t diff = flpr_seq_diff(seq, (uint16_t)peer->rx_seq);

	if (diff > 0) {
		/* Normal in-order advance. */
		if (diff > 1) {
			peer->rx_lost += (uint16_t)(diff - 1);
		}
		peer->rx_seq = seq;
	} else if (diff == 0) {
		/* Duplicate. */
		peer->rx_dup++;
	} else {
		/* Out of order (backward delta — delayed/reordered). */
		peer->rx_ooo++;
		peer->rx_seq = seq;
	}
	peer->rx_last_ms = now_ms;
}

/* ── Health check ────────────────────────────────────────────────
 * Call periodically. Returns true if heartbeat is current.
 * Updates rx_missed_total when check fails.
 */
static inline bool flpr_peer_check_health(struct flpr_peer *peer, uint32_t now_ms)
{
	if (peer->rx_last_ms == 0) {
		return peer->healthy; /* no heartbeats yet */
	}
	uint32_t elapsed = now_ms - peer->rx_last_ms;
	if (elapsed > FLPR_HEARTBEAT_MISS_MAX * FLPR_HEARTBEAT_INTERVAL_MS) {
		peer->rx_missed_total++;
		return false;
	}
	return true;
}

#ifdef __cplusplus
}
#endif

#endif /* FLPR_PROTOCOL_H_ */
