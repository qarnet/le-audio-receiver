/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared wire protocol between CPUAPP (ARM) and FLPR (RISC-V VPR).
 * Included from both images. Single source of truth for message layout,
 * type constants, version, and compile-time size/endianness checks.
 *
 * RV32E has no atomic extension — all fields are plain loads/stores.
 * Both cores are little-endian (ARMv8-M, RISC-V RV32E) — no byteswap.
 */

#ifndef FLPR_PROTOCOL_H_
#define FLPR_PROTOCOL_H_

#include <stdint.h>
#include <stddef.h>
#include <zephyr/sys/__assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Protocol version ────────────────────────────────────────────
 * Increment when message layout or semantics change incompatibly.
 * Mismatch = CPUAPP rejects FLPR with actionable log, keeps BLE/audio up.
 */
#define FLPR_PROTOCOL_VERSION 2U

/* ── Message types ─────────────────────────────────────────────── */

#define FLPR_MSG_READY     0x01U /* FLPR → CPUAPP: boot complete, version, epoch */
#define FLPR_MSG_ACK       0x02U /* CPUAPP → FLPR: acknowledged; FLPR→CPUAPP: echo */
#define FLPR_MSG_HEARTBEAT 0x03U /* Bidirectional: seq, uptime, liveness */

/* ── Heartbeat policy ──────────────────────────────────────────── */

#define FLPR_HEARTBEAT_INTERVAL_MS  1000U   /* 1 Hz */
#define FLPR_HEARTBEAT_MISS_MAX     5U      /* unhealthy after 5 missed */
#define FLPR_HEARTBEAT_STRESS_BATCH 100000U /* stress test target */

/* ── Wire message (fixed size, packed) ─────────────────────────── */

struct flpr_msg {
	uint8_t type;    /* FLPR_MSG_* */
	uint8_t version; /* FLPR_PROTOCOL_VERSION */
	uint16_t seq;    /* monotonic per-sender, wraps at 16 bits */
	uint32_t data;   /* heartbeat: uptime_ms; READY/ACK: epoch */
} __attribute__((packed));

/* Compile-time safety checks. */
BUILD_ASSERT(sizeof(struct flpr_msg) == 8, "flpr_msg must be exactly 8 bytes");
BUILD_ASSERT(offsetof(struct flpr_msg, type) == 0, "type must be at offset 0");
BUILD_ASSERT(offsetof(struct flpr_msg, version) == 1, "version must be at offset 1");
BUILD_ASSERT(offsetof(struct flpr_msg, seq) == 2, "seq must be at offset 2");
BUILD_ASSERT(offsetof(struct flpr_msg, data) == 4, "data must be at offset 4");

/* ── Sequence number helpers ───────────────────────────────────── */

/** True if a comes after b with 16-bit wrap (maximum gap 32767). */
static inline bool flpr_seq_after(uint16_t a, uint16_t b)
{
	return (int16_t)(a - b) > 0;
}

/** Gap between two sequence numbers (a after b), handles wrap. */
static inline uint16_t flpr_seq_gap(uint16_t a, uint16_t b)
{
	return (uint16_t)(a - b);
}

#ifdef __cplusplus
}
#endif

#endif /* FLPR_PROTOCOL_H_ */
