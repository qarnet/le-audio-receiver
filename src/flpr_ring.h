/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure SPSC ring protocol for CPUAPP ↔ FLPR PCM transport.
 * Cache-correct ordering via platform hooks; no Zephyr deps for unit testing.
 *
 * Memory layout (one direction, 8 KiB):
 *   [ ring_header 128 B ] [ slot_0 2016 B ] [ slot_1 ] [ slot_2 ] [ slot_3 ]
 *
 * The ring at 0x2002C000 is CPUAPP→FLPR; 0x2002E000 is FLPR→CPUAPP.
 * Four slots per direction, each holds 480 stereo PCM frames (1920 bytes)
 * + slot metadata (32 bytes) padded to 32-byte cache-line alignment.
 *
 * Both cores little-endian. RV32E has no atomic extension — indices are
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
#define FLPR_RING_ABI_VERSION 1U

#define FLPR_RING_SLOT_COUNT       4U
#define FLPR_RING_PAYLOAD_FRAMES   480U  /* stereo frames per slot */
#define FLPR_RING_PAYLOAD_BYTES    1920U /* 480 * 2ch * 2B = 1920 */
#define FLPR_RING_SLOT_ALIGN       32U   /* cache-line alignment */
#define FLPR_RING_SLOT_METADATA_SZ 32U
#define FLPR_RING_SLOT_PAYLOAD_OFF 32U
#define FLPR_RING_SLOT_STRIDE      2016U /* padded to multiple of 32 */

/* Total ring size: header (128) + 4 * stride (2016) = 8192 = 8 KiB */
#define FLPR_RING_HEADER_OFFSET 0U
#define FLPR_RING_HEADER_SIZE   128U
#define FLPR_RING_DATA_OFFSET   128U
#define FLPR_RING_TOTAL_SIZE    8192U /* must match DT reservation */

/* ── Ring header (128 bytes, at base of each ring) ──────────────── */

struct flpr_ring_header {
	uint32_t magic;          /* FLPR_RING_MAGIC */
	uint32_t abi_version;    /* FLPR_RING_ABI_VERSION */
	uint32_t flags;          /* reserved */
	uint32_t stream_epoch;   /* non-zero after reset agreement */
	uint32_t slot_count;     /* FLPR_RING_SLOT_COUNT */
	uint32_t slot_stride;    /* FLPR_RING_SLOT_STRIDE */
	uint32_t payload_frames; /* FLPR_RING_PAYLOAD_FRAMES */
	uint32_t payload_bytes;  /* FLPR_RING_PAYLOAD_BYTES */

	/* Producer index: writer increments after publishing a slot.
	 * CPUAPP→FLPR: CPUAPP is producer. FLPR→CPUAPP: FLPR is producer. */
	volatile uint32_t producer_idx;

	/* Consumer index: reader increments after consuming a slot. */
	volatile uint32_t consumer_idx;

	/* Error counters (monotonic). */
	uint32_t err_bad_magic;
	uint32_t err_bad_version;
	uint32_t err_bad_epoch;
	uint32_t err_bad_crc;
	uint32_t err_bad_seq;
	uint32_t err_producer_full;
	uint32_t err_consumer_empty;
	uint32_t err_stale_epoch;

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
	uint16_t valid_frames;  /* 0..FLPR_RING_PAYLOAD_FRAMES */
	uint16_t flags;         /* reserved */
	int32_t correction_ppm; /* drift correction at time of capture */
	uint32_t crc32;         /* CRC-32 of payload (0 if unused) */
	uint32_t _pad[3];       /* 32-byte alignment */
};

_Static_assert(sizeof(struct flpr_ring_slot_meta) == FLPR_RING_SLOT_METADATA_SZ,
	       "slot metadata size");

/* Slot flags. */
#define FLPR_SLOT_FLAG_VALID  0x0001U /* payload contains valid data */
#define FLPR_SLOT_FLAG_CRC_OK 0x0002U /* CRC verified (reader sets) */
#define FLPR_SLOT_FLAG_STALL  0x0004U /* producer injected stall */

/* ── Ring direction ─────────────────────────────────────────────── */

enum flpr_ring_dir {
	FLPR_RING_CPUAPP_TO_FLPR = 0, /* CPUAPP produces, FLPR consumes */
	FLPR_RING_FLPR_TO_CPUAPP = 1, /* FLPR produces, CPUAPP consumes */
};

/* ── Slot index arithmetic (pure helpers, no volatile access) ───── */

static inline uint32_t flpr_ring_space(uint32_t producer, uint32_t consumer)
{
	if (producer >= consumer) {
		return FLPR_RING_SLOT_COUNT - (producer - consumer) - 1;
	}
	return consumer - producer - 1;
}

static inline uint32_t flpr_ring_count(uint32_t producer, uint32_t consumer)
{
	if (producer >= consumer) {
		return producer - consumer;
	}
	return FLPR_RING_SLOT_COUNT + producer - consumer;
}

static inline bool flpr_ring_is_empty(uint32_t producer, uint32_t consumer)
{
	return producer == consumer;
}

static inline bool flpr_ring_is_full(uint32_t producer, uint32_t consumer)
{
	return flpr_ring_space(producer, consumer) == 0;
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

/** Get pointer to the base of a slot given ring base + slot index. */
static inline uint8_t *flpr_ring_slot_base(uint8_t *ring_base, uint32_t index)
{
	return ring_base + FLPR_RING_DATA_OFFSET +
	       (index % FLPR_RING_SLOT_COUNT) * FLPR_RING_SLOT_STRIDE;
}

/* ── Platform cache/barrier hooks ─────────────────────────────────
 * These are defined by the platform-specific cache module.
 * On nRF54L15 CPUAPP: __DSB() barriers only (SRAM is not cached).
 * On FLPR: no-ops (no cache, no barrier needed for in-order pipeline).
 */

/** Flush (write-back) a range. Called by producer AFTER filling a slot
 *  but BEFORE publishing producer index. */
extern void flpr_cache_flush_range(void *addr, size_t size);

/** Invalidate a range. Called by consumer BEFORE reading a slot after
 *  observing a new producer index. */
extern void flpr_cache_invld_range(void *addr, size_t size);

/** Full memory barrier. Ensures ordering of prior stores before
 *  subsequent stores (e.g. slot data before index publish). */
extern void flpr_cache_write_barrier(void);

/** Full memory barrier. Ensures ordering of prior loads before
 *  subsequent loads (e.g. index read before slot read). */
extern void flpr_cache_read_barrier(void);

/* ── Ring lifecycle ─────────────────────────────────────────────── */

/**
 * @brief Initialize a ring at the given base address.
 *
 * Writes magic, version, slot layout fields, zeroes indices, clears
 * error counters. Does NOT set stream_epoch — that requires reset
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
 * indices to 0. Caller must ensure both sides agree on the new epoch
 * before this is called (via IPC handshake).
 *
 * Stale slots from a previous epoch will be rejected by the consumer.
 *
 * @param base       Ring base address.
 * @param new_epoch  New stream epoch (must be != 0 and != current).
 * @return 0 on success, -EINVAL if epoch is 0 or same as current.
 */
int flpr_ring_reset_epoch(uint8_t *base, uint32_t new_epoch);

/**
 * @brief Validate ring header.
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
 * @param base          Ring base address.
 * @param slot_idx_out  Output: allocated slot index.
 * @return 0 on success, -ENOSPC if ring full.
 */
int flpr_ring_produce_begin(uint8_t *base, uint32_t *slot_idx_out);

/**
 * @brief Commit a produced slot (publish producer index).
 *
 * Flushes the slot range and writes the new producer index with barrier
 * ordering. After this call, the consumer may observe and read the slot.
 *
 * @param base      Ring base address.
 * @param slot_idx  Slot index returned by produce_begin.
 */
void flpr_ring_produce_commit(uint8_t *base, uint32_t slot_idx);

/* ── Consumer API ───────────────────────────────────────────────── */
/* Caller is the single consumer for a given ring. */

/**
 * @brief Try to consume a slot (get next available).
 *
 * If a new slot is available, invalidates the slot range, validates
 * epoch/sequence (counts errors on mismatch), and writes slot_base_out.
 *
 * The caller reads slot metadata + payload, then calls
 * flpr_ring_consume_done() to release the slot.
 *
 * @param base           Ring base address.
 * @param current_epoch  Stream epoch expected by consumer.
 * @param slot_base_out  Output: pointer to slot base.
 * @param meta_out       Output: pointer to validated slot metadata.
 * @return 0 on success, -ENOENT if ring empty, -ESTALE if stale epoch.
 */
int flpr_ring_consume_begin(uint8_t *base, uint32_t current_epoch, uint8_t **slot_base_out,
			    struct flpr_ring_slot_meta **meta_out);

/**
 * @brief Release a consumed slot (advance consumer index).
 *
 * Writes the new consumer index with barrier ordering.
 *
 * @param base      Ring base address.
 */
void flpr_ring_consume_done(uint8_t *base);

/* ── CRC ────────────────────────────────────────────────────────── */

/**
 * @brief Compute CRC-32 (Ethernet/gzip polynomial) over payload.
 *
 * Pure function, no global state. Used for test-mode payload validation.
 *
 * @param data   Payload bytes.
 * @param len    Number of bytes.
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
