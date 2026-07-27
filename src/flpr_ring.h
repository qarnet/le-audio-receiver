/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure SPSC ring protocol for CPUAPP ↔ FLPR PCM transport.
 * Cache-correct ordering via platform hooks; no Zephyr deps for unit testing.
 *
 * Index discipline: producer_idx and consumer_idx are MONOTONIC uint32_t
 * counters — they grow unbounded and only wrap at 2³².  Slot index is
 * counter % slot_count.  used = producer - consumer is always correct
 * (producer only advances when consumer cannot overtake it).  Full when
 * used >= slot_count - 1 (sentinel slot kept empty).  Empty when used == 0.
 *
 * Memory layout (one direction, 8 KiB):
 *   [ ring_header 128 B ] [ slot_0 2016 B ] [ slot_1 ] [ slot_2 ] [ slot_3 ]
 *
 * The ring at DT pcm_ring base is CPUAPP→FLPR; base + 8 KiB is FLPR→CPUAPP.
 * Four slots per direction, each holds up to 481 stereo PCM frames (1924 bytes)
 * + slot metadata (32 bytes), padded to 32-byte cache-line alignment.
 * Input restricts valid_frames ≤ 480; output permits ≤ 481 (forward-compat).
 *
 * Both cores little-endian.  RV32E has no atomic extension — indices are
 * volatile naturally-aligned uint32_t, single-writer per index.
 */

#ifndef FLPR_RING_H_
#define FLPR_RING_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ─────────────────────────────────────────────────── */

#define FLPR_RING_MAGIC       0x52494E47U /* "RING" */
#define FLPR_RING_ABI_VERSION 2U          /* v2: monotonic counters, 481-frame capacity */

#define FLPR_RING_SLOT_COUNT 4U

/* Slot capacity: 481 stereo frames (forward-compatible ceiling).
 * Input metadata restricts valid_frames ≤ 480.
 * Output permits valid_frames ≤ 481. */
#define FLPR_RING_PAYLOAD_CAPACITY_FRAMES 481U
#define FLPR_RING_PAYLOAD_CAPACITY_BYTES  1924U /* 481 × 2ch × 16-bit */

/* Production input ceiling (reject valid_frames > this). */
#define FLPR_RING_PAYLOAD_MAX_INPUT 480U

/* Slot layout (all 32-byte cache-line aligned). */
#define FLPR_RING_SLOT_ALIGN       32U
#define FLPR_RING_SLOT_METADATA_SZ 32U
#define FLPR_RING_SLOT_PAYLOAD_OFF 32U
#define FLPR_RING_SLOT_STRIDE      2016U /* padded to multiple of 32 */

/* Total ring size: header (128) + 4 × stride (2016) = 8192 = 8 KiB */
#define FLPR_RING_HEADER_OFFSET 0U
#define FLPR_RING_HEADER_SIZE   128U
#define FLPR_RING_DATA_OFFSET   128U
#define FLPR_RING_TOTAL_SIZE    8192U /* must match DT reservation */

/* Maximum slot fill level (sentinel: one slot always kept empty). */
#define FLPR_RING_MAX_USED (FLPR_RING_SLOT_COUNT - 1U)

/* ── Ring header (128 bytes, at base of each ring) ──────────────── */

struct flpr_ring_header {
	uint32_t magic;              /* FLPR_RING_MAGIC */
	uint32_t abi_version;        /* FLPR_RING_ABI_VERSION */
	uint32_t flags;              /* reserved */
	uint32_t stream_epoch;       /* non-zero after reset agreement */
	uint32_t slot_count;         /* FLPR_RING_SLOT_COUNT */
	uint32_t slot_stride;        /* FLPR_RING_SLOT_STRIDE */
	uint32_t payload_cap_frames; /* FLPR_RING_PAYLOAD_CAPACITY_FRAMES */
	uint32_t payload_cap_bytes;  /* FLPR_RING_PAYLOAD_CAPACITY_BYTES */

	/* Producer index: monotonic, writer increments after publishing.
	 * CPUAPP→FLPR: CPUAPP is producer.  FLPR→CPUAPP: FLPR is producer. */
	volatile uint32_t producer_idx;

	/* Consumer index: monotonic, reader increments after consuming. */
	volatile uint32_t consumer_idx;

	/* Error counters — each has exactly ONE writer (see comment block).
	 * Writes are not synchronized; counters may be stale on remote core. */
	uint32_t err_bad_magic;      /* writer: consumer (first validate) */
	uint32_t err_bad_version;    /* writer: consumer */
	uint32_t err_bad_epoch;      /* writer: consumer */
	uint32_t err_bad_crc;        /* writer: consumer */
	uint32_t err_bad_seq;        /* writer: consumer */
	uint32_t err_producer_full;  /* writer: producer */
	uint32_t err_consumer_empty; /* writer: consumer */
	uint32_t err_stale_epoch;    /* writer: consumer */

	/* Padding to 128 bytes. */
	uint32_t _pad[14];
};

/* Compile-time size check. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(struct flpr_ring_header) == 128, "ring header size");
_Static_assert(sizeof(struct flpr_ring_header) <= FLPR_RING_HEADER_SIZE,
	       "ring header exceeds reserved size");
#endif

/* ── Slot metadata (32 bytes, before payload in each slot) ──────── */

struct flpr_ring_slot_meta {
	uint32_t sequence;      /* monotonic frame sequence from stream */
	uint32_t epoch;         /* must match ring stream_epoch */
	uint16_t valid_frames;  /* valid stereo frames: input ≤ 480, output ≤ 481 */
	uint16_t flags;         /* FLPR_SLOT_FLAG_* */
	int32_t correction_ppm; /* drift correction at time of capture */
	uint32_t crc32;         /* CRC-32 of valid payload bytes (0 = unused) */
	uint32_t _pad[3];       /* 32-byte alignment */
};

_Static_assert(sizeof(struct flpr_ring_slot_meta) == FLPR_RING_SLOT_METADATA_SZ,
	       "slot metadata size");

/* Slot flags. */
#define FLPR_SLOT_FLAG_VALID  0x0001U /* payload contains valid data */
#define FLPR_SLOT_FLAG_CRC_OK 0x0002U /* CRC verified (consumer sets) */
#define FLPR_SLOT_FLAG_STALL  0x0004U /* producer injected stall */

/* ── Ring direction ─────────────────────────────────────────────── */

enum flpr_ring_dir {
	FLPR_RING_CPUAPP_TO_FLPR = 0, /* CPUAPP produces, FLPR consumes */
	FLPR_RING_FLPR_TO_CPUAPP = 1, /* FLPR produces, CPUAPP consumes */
};

/* ── Slot index arithmetic (pure helpers, no volatile access) ─────
 *
 * Producer and consumer are UNBOUNDED monotonic uint32_t counters.
 * Slot index = counter % slot_count.
 * Used slots  = producer - consumer  (correct even across uint32 wrap
 *               as long as distance ≤ slot_count).
 * Empty       = (producer == consumer).
 * Full        = (producer - consumer) >= (slot_count - 1).
 */

/** Number of slots currently occupied (consumer has not yet processed). */
static inline uint32_t flpr_ring_used(uint32_t producer, uint32_t consumer)
{
	return producer - consumer;
}

/** Number of free slots for the producer to fill.
 *  Always ≤ slot_count - 1 (sentinel discipline). */
static inline uint32_t flpr_ring_space(uint32_t producer, uint32_t consumer)
{
	uint32_t used = producer - consumer;

	if (used >= FLPR_RING_MAX_USED) {
		return 0U;
	}
	return FLPR_RING_MAX_USED - used;
}

/** Number of slots available to the consumer. Same as flpr_ring_used. */
static inline uint32_t flpr_ring_count(uint32_t producer, uint32_t consumer)
{
	return producer - consumer;
}

static inline bool flpr_ring_is_empty(uint32_t producer, uint32_t consumer)
{
	return producer == consumer;
}

static inline bool flpr_ring_is_full(uint32_t producer, uint32_t consumer)
{
	return (producer - consumer) >= FLPR_RING_MAX_USED;
}

/* ── Payload pointer helpers ────────────────────────────────────── */

/** Get pointer to payload bytes within a slot. */
static inline uint8_t *flpr_ring_slot_payload(uint8_t *slot_base)
{
	return slot_base + FLPR_RING_SLOT_PAYLOAD_OFF;
}

/** Get pointer to slot metadata. */
static inline struct flpr_ring_slot_meta *flpr_ring_slot_meta_ptr(uint8_t *slot_base)
{
	return (struct flpr_ring_slot_meta *)slot_base;
}

/** Get pointer to the base of a slot given ring base + monotonic index.
 *  Slot index = monotonic_counter % slot_count (wrap-safe for uint32). */
static inline uint8_t *flpr_ring_slot_base(uint8_t *ring_base, uint32_t monotonic_index)
{
	return ring_base + FLPR_RING_DATA_OFFSET +
	       (monotonic_index % FLPR_RING_SLOT_COUNT) * FLPR_RING_SLOT_STRIDE;
}

/* ── Platform cache/barrier hooks ─────────────────────────────────
 * Defined by the platform-specific cache module (src/flpr_cache.c).
 *
 * nRF54L15 CPUAPP (ARM Cortex-M33): SRAM on S-AHB system bus —
 *   NOT cached by ICACHE/CACHEDATA (NVM-only).  CONFIG_DCACHE unset.
 *   barrier_dsync_fence_full() → __DSB() data-sync barrier for
 *   multi-core ordering.
 *
 * nRF54L15 FLPR (RISC-V RV32E): no hardware cache.
 *   BARRIER_OPERATIONS_BUILTIN → __atomic_thread_fence(__ATOMIC_SEQ_CST).
 *
 * Ordering contract:
 *   Producer: fill slot payload + metadata → release fence →
 *             increment producer_idx.
 *   Consumer: observe producer_idx → acquire fence →
 *             read slot payload + metadata.
 */

/** Release fence (store ordering).  Call BEFORE publishing index write. */
extern void flpr_cache_write_barrier(void);

/** Acquire fence (load ordering).  Call AFTER observing index but BEFORE
 *  reading slot data. */
extern void flpr_cache_read_barrier(void);

/** Full bidirectional fence.  Used after ring init / reset. */
extern void flpr_cache_full_barrier(void);

/* ── Ring lifecycle ─────────────────────────────────────────────── */

/**
 * @brief Initialize a ring at the given base address.
 *
 * Writes magic, version, layout fields, zeroes indices, clears
 * error counters.  Does NOT set stream_epoch — that requires reset
 * agreement (see flpr_ring_reset_epoch).
 *
 * @param base  Ring base address (must be at aligned physical address).
 * @param dir   Direction hint (not stored, used for cache hooks).
 */
void flpr_ring_init(uint8_t *base, enum flpr_ring_dir dir);

/**
 * @brief Reset the ring stream epoch.
 *
 * Sets stream_epoch to a new non-zero value, resets producer/consumer
 * indices to 0.  Caller must ensure both sides agree on the new epoch
 * before this is called (via IPC handshake).
 *
 * Stale slots from a previous epoch will be rejected by the consumer.
 *
 * @param base       Ring base address.
 * @param new_epoch  New stream epoch (must be != 0 and != current).
 * @return 0 on success, -1 if epoch is 0 or same as current.
 */
int flpr_ring_reset_epoch(uint8_t *base, uint32_t new_epoch);

/**
 * @brief Validate ring header layout.
 * @return true if magic + version + field consistency is valid.
 */
bool flpr_ring_validate(const uint8_t *base);

/* ── Producer API ───────────────────────────────────────────────── */
/* Caller is the single producer for a given ring. */

/**
 * @brief Try to produce a slot (allocate space).
 *
 * If successful, the slot index is written to *slot_idx_out and the
 * caller fills the slot metadata + payload, then calls
 * flpr_ring_produce_commit() to publish.
 *
 * Restrictions: valid_frames must be ≤ FLPR_RING_PAYLOAD_MAX_INPUT.
 * CRC must be computed ONLY over valid_frames × 4 bytes.
 *
 * @param base          Ring base address.
 * @param slot_idx_out  Output: allocated monotonic index.
 * @return 0 on success, -1 if ring full.
 */
int flpr_ring_produce_begin(uint8_t *base, uint32_t *slot_idx_out);

/**
 * @brief Commit a produced slot (publish producer index).
 *
 * Release fence BEFORE incrementing producer_idx.  After this call
 * the consumer may observe and read the slot.
 *
 * @param base       Ring base address.
 * @param slot_idx   Monotonic index returned by produce_begin.
 */
void flpr_ring_produce_commit(uint8_t *base, uint32_t slot_idx);

/* ── Consumer API ───────────────────────────────────────────────── */
/* Caller is the single consumer for a given ring. */

/**
 * @brief Try to consume a slot (get next available).
 *
 * Acquire fence after observing producer_idx.  Validates epoch and
 * returns pointer to slot.  On stale epoch the consumer advances
 * past the slot (no deadlock) and returns -2.
 *
 * @param base           Ring base address.
 * @param current_epoch  Stream epoch expected by consumer (0 = no check).
 * @param slot_base_out  Output: pointer to slot base.
 * @param meta_out       Output: pointer to validated slot metadata.
 * @return 0 on success, -1 if ring empty, -2 if stale epoch.
 */
int flpr_ring_consume_begin(uint8_t *base, uint32_t current_epoch, uint8_t **slot_base_out,
			    struct flpr_ring_slot_meta **meta_out);

/**
 * @brief Release a consumed slot (advance consumer index).
 *
 * @param base  Ring base address.
 */
void flpr_ring_consume_done(uint8_t *base);

/* ── CRC ────────────────────────────────────────────────────────── */

/**
 * @brief Compute CRC-32 (Ethernet/gzip polynomial, init=0xFFFFFFFF)
 *        over arbitrary byte range.
 *
 * Pure function, no global state.  Used for test-mode payload validation.
 *
 * @param data   Byte buffer (can be NULL if len == 0).
 * @param len    Number of bytes (0 returns 0).
 * @return CRC-32 value.
 */
uint32_t flpr_ring_crc32(const uint8_t *data, size_t len);

/* ── Header field accessors (read-only from ring memory) ────────── */

/** Get current producer index (volatile read). */
static inline uint32_t flpr_ring_producer(const uint8_t *base)
{
	const struct flpr_ring_header *hdr = (const struct flpr_ring_header *)base;
	return hdr->producer_idx;
}

/** Get current consumer index (volatile read). */
static inline uint32_t flpr_ring_consumer(const uint8_t *base)
{
	const struct flpr_ring_header *hdr = (const struct flpr_ring_header *)base;
	return hdr->consumer_idx;
}

/** Get stream epoch. */
static inline uint32_t flpr_ring_epoch(const uint8_t *base)
{
	const struct flpr_ring_header *hdr = (const struct flpr_ring_header *)base;
	return hdr->stream_epoch;
}

#ifdef __cplusplus
}
#endif

#endif /* FLPR_RING_H_ */
