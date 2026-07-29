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
#include <string.h>
#include <zephyr/sys/__assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Protocol version ──────────────────────────────────────────── */

#define FLPR_PROTOCOL_VERSION 4U

/* ── Message types ─────────────────────────────────────────────── */

#define FLPR_MSG_READY         0x01U /* FLPR → CPUAPP: boot complete */
#define FLPR_MSG_READY_ACK     0x02U /* CPUAPP → FLPR: acknowledged */
#define FLPR_MSG_HEARTBEAT     0x03U /* Bidirectional: liveness */
#define FLPR_MSG_HEARTBEAT_ACK 0x04U /* Echo of heartbeat */
#define FLPR_MSG_STRESS_PING   0x05U /* CPUAPP → FLPR: stress test */
#define FLPR_MSG_STRESS_PONG   0x06U /* FLPR → CPUAPP: stress response */

/* Stage 1: PCM ring control */
#define FLPR_MSG_RING_RESET       0x10U /* CPUAPP → FLPR: reset ring epoch */
#define FLPR_MSG_RING_RESET_ACK   0x11U /* FLPR → CPUAPP: ack reset */
#define FLPR_MSG_RING_TEST_START  0x12U /* CPUAPP → FLPR: start ring test (count in data) */
#define FLPR_MSG_RING_TEST_STOP   0x13U /* CPUAPP → FLPR: abort test */
#define FLPR_MSG_RING_TEST_REPORT 0x14U /* FLPR → CPUAPP: test results (count so far) */
#define FLPR_MSG_RING_PRODUCER    0x15U /* CPUAPP → FLPR: input data available */
#define FLPR_MSG_RING_CONSUMER    0x16U /* FLPR → CPUAPP: output data available */

/* Stage 1: stall controls */
#define FLPR_MSG_RING_STALL     0x17U /* CPUAPP → FLPR: stall config (data: packed mask+duration) */
#define FLPR_MSG_RING_STALL_ACK 0x18U /* FLPR → CPUAPP: stall config applied */

/* Stage 4B: fault injection and hang recovery */
#define FLPR_MSG_FAULT_HANG     0x20U /* CPUAPP → FLPR: request FLPR to hang (halt ring+heartbeat) */
#define FLPR_MSG_FAULT_HANG_ACK 0x21U /* FLPR → CPUAPP: ACK received, hang imminent */

/* ── Stall data packing (Stage 2) ───────────────────────────────────
 * data[7:0]   = stall mask (FLPR_STALL_CONSUMER_INPUT, etc.)
 * data[31:8]  = duration milliseconds (0 = persistent)
 *
 * Maximum duration 0x00FFFFFF ms (~4.7 hours).  Zero bits in mask
 * with nonzero duration is rejected by pack macro (zero-bits mask is
 * a clear command, which must always be persistent).
 */
#define FLPR_STALL_DURATION_MAX 0x00FFFFFFU

/* Pack mask + duration: mask in low 8 bits, duration in upper 24 bits. */
#define FLPR_STALL_PACK(mask, duration_ms)                                                         \
	(((uint32_t)(duration_ms) << 8) | ((uint32_t)(mask) & 0xFFU))

/* Unpack mask: low 8 bits. */
#define FLPR_STALL_MASK(data) ((uint8_t)((data) & 0xFFU))

/* Unpack duration: upper 24 bits. */
#define FLPR_STALL_DURATION(data) ((uint32_t)(((data) >> 8) & 0x00FFFFFFU))

/* Compile-time assertions. */
BUILD_ASSERT(FLPR_STALL_PACK(0x01, 0) == 0x00000001U,
	     "FLPR_STALL_PACK(0x01,0) must equal 0x00000001 (persistent)");
BUILD_ASSERT(FLPR_STALL_PACK(0x02, 60) == ((60U << 8) | 0x02U),
	     "FLPR_STALL_PACK(0x02,60) must pack duration at bits 31:8");
BUILD_ASSERT(FLPR_STALL_PACK(0xFF, 0x00FFFFFFU) == ((0xFFFFFF00U) | 0x000000FFU),
	     "FLPR_STALL_PACK(0xFF, max duration) boundary check");

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
	bool ready;            /* remote sent READY? */
	bool acked;            /* we sent READY_ACK? (CPUAPP) / received READY_ACK? (FLPR) */
	bool healthy;          /* heartbeat not missed too many? */
	uint32_t epoch;        /* remote boot nonce */
	uint32_t ready_count;  /* total READY received */
	uint32_t reboot_count; /* distinct epochs = reboots detected */

	/* Remote → local sequence tracking (heartbeats we receive). */
	uint32_t rx_seq;     /* last IN-ORDER seq received (32-bit, wraps at 16-bit) */
	uint32_t rx_lost;    /* cumulative gaps counted */
	uint32_t rx_dup;     /* duplicate heartbeats */
	uint32_t rx_ooo;     /* out-of-order (backward delta, counted but baseline NOT moved) */
	uint32_t rx_last_ms; /* last IN-ORDER rx uptime for staleness */
	uint32_t rx_missed_total; /* cumulative healthy→unhealthy transitions */

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

/* ── Peer reset ───────────────────────────────────────────────────
 * Zeroes all state. Use on IPC unbound / restart / initialisation.
 */
static inline void flpr_peer_reset(struct flpr_peer *peer)
{
	memset(peer, 0, sizeof(*peer));
}

/* ── READY transition (peer side, pure) ───────────────────────────
 * Process a READY message.  new epoch  → resets heartbeat sequence,
 * ack and health state, increments reboot_count.  duplicate same
 * epoch → increments ready_count only, does NOT reset counters.
 * Returns true if this was a new epoch (= reboot detected).
 *
 * Caller must still send READY_ACK (not part of this helper because
 * that requires IPC outside any lock).
 */
static inline bool flpr_peer_handle_ready(struct flpr_peer *peer, uint32_t epoch)
{
	bool new_epoch = (peer->ready_count == 0) || (epoch != peer->epoch);

	peer->ready = true;
	peer->ready_count++;

	if (new_epoch) {
		peer->epoch = epoch;
		peer->reboot_count++;
		peer->healthy = true;

		/* Reset rx-side tracking for the new boot. */
		peer->rx_seq = 0;
		peer->rx_lost = 0;
		peer->rx_dup = 0;
		peer->rx_ooo = 0;
		peer->rx_last_ms = 0;
		peer->rx_missed_total = 0;

		/* Reset ack tracking. */
		peer->tx_acked_seq = 0;
	} else {
		/* Same epoch: duplicate READY.  Preserve all state,
		 * only update epoch field (idempotent). */
		peer->epoch = epoch;
	}

	return new_epoch;
}

/* ── READY_ACK handler (FLPR side) ────────────────────────────────
 * Called when FLPR receives READY_ACK from CPUAPP.
 * Sets acked + healthy, stores CPUAPP uptime for diagnostics.
 */
static inline void flpr_peer_handle_ready_ack(struct flpr_peer *peer, uint32_t data)
{
	peer->acked = true;
	peer->healthy = true;
	peer->epoch = data; /* CPUAPP uptime at ACK — diagnostic */
}

/* ── Heartbeat ACK validation ────────────────────────────────────
 * Process a HEARTBEAT_ACK: update last acked sequence.
 * Only advances tx_acked_seq (never regresses); stale seq no-op.
 */
static inline void flpr_peer_handle_heartbeat_ack(struct flpr_peer *peer, uint16_t acked_seq)
{
	/* Accept if acked_seq >= current in-order (including wrap). */
	if (!flpr_seq_after((uint16_t)peer->tx_acked_seq, acked_seq)) {
		peer->tx_acked_seq = acked_seq;
	}
}

/* ── Sequence tracking ───────────────────────────────────────────
 * Process a received HEARTBEAT sequence number from the remote peer.
 * Updates rx_seq (in-order only), rx_lost, rx_dup, rx_ooo.
 * Out-of-order packets are COUNTED but do NOT move the in-order
 * rx_seq baseline or rx_last_ms — so a stale packet followed by
 * the next in-order produces NO fake huge gap.
 * Natural 16-bit wrap (diff > 0 after wrap) remains valid.
 * Call AFTER flpr_msg_validate().
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
		/* Normal in-order advance (including natural wrap). */
		if (diff > 1) {
			peer->rx_lost += (uint16_t)(diff - 1);
		}
		peer->rx_seq = seq;
		peer->rx_last_ms = now_ms;
	} else if (diff == 0) {
		/* Duplicate.  Update timestamp for health. */
		peer->rx_dup++;
		peer->rx_last_ms = now_ms;
	} else {
		/* Out of order (backward delta — delayed/reordered).
		 * Count but do NOT move in-order baseline or timestamp. */
		peer->rx_ooo++;
	}
}

/* ── Health check ────────────────────────────────────────────────
 * Call periodically.  Returns current healthy state.
 * Increments rx_missed_total ONLY on healthy→unhealthy transition
 * (once per miss episode, not on repeated polling).
 * A new in-order heartbeat arrival resets rx_last_ms so the next
 * check_health call restores healthy automatically.
 */
static inline bool flpr_peer_check_health(struct flpr_peer *peer, uint32_t now_ms)
{
	if (peer->rx_last_ms == 0) {
		/* No heartbeat received yet — cannot determine health. */
		return peer->healthy;
	}

	uint32_t elapsed = now_ms - peer->rx_last_ms;

	if (elapsed > FLPR_HEARTBEAT_MISS_MAX * FLPR_HEARTBEAT_INTERVAL_MS) {
		/* Stale: transition from healthy → unhealthy. */
		if (peer->healthy) {
			peer->rx_missed_total++;
			peer->healthy = false;
		}
		return false;
	}

	/* Recent heartbeat: restore healthy. */
	peer->healthy = true;
	return true;
}

/* ── Stress PONG cookie classification (pure) ────────────────────
 * Classifies a received stress-PONG cookie against the expected
 * value.  Late previous-cookie and future-cookie never signal.
 * Caller takes action (semaphore give, counter increment).
 */
enum flpr_stress_pong_class {
	FLPR_PONG_MATCH = 0, /* exact cookie match — signal caller */
	FLPR_PONG_STALE,     /* cookie < expected (late from previous run/iter) */
	FLPR_PONG_FUTURE,    /* cookie > expected (unknown/future) */
	FLPR_PONG_INACTIVE,  /* stress not active — ignore */
};

static inline enum flpr_stress_pong_class
flpr_classify_stress_pong(uint32_t pong_cookie, uint32_t expected_cookie, bool stress_active)
{
	if (!stress_active) {
		return FLPR_PONG_INACTIVE;
	}
	if (pong_cookie == expected_cookie) {
		return FLPR_PONG_MATCH;
	}
	if (pong_cookie < expected_cookie) {
		return FLPR_PONG_STALE;
	}
	return FLPR_PONG_FUTURE;
}

#ifdef __cplusplus
}
#endif

#endif /* FLPR_PROTOCOL_H_ */
