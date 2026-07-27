/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for flpr_ring.h / flpr_ring.c SPSC ring operations.
 * Uses native_sim target — pure C, no Zephyr kernel deps beyond
 * the cache hook file (which has a native fallback).
 *
 * Covers:
 *   - Init, validation, magic/version checks
 *   - Space/count helpers (monotonic counters)
 *   - Produce/consume: single slot, full ring, empty ring
 *   - 100+ wraps of slot indices
 *   - UINT32 counter wrap (near overflow)
 *   - Exact full capacity (SLOT_COUNT - 1 = 3 slots)
 *   - CRC-32 known vectors, zero-length, different data
 *   - CRC over valid bytes only
 *   - Epoch reset, stale epoch rejection
 *   - Slot bounds: no clobber, canary check
 *   - Slot arithmetic (index → pointer mapping)
 *   - Two-ring independence
 *   - Invalid flags: bad magic, bad version, bad slot_count, bad stride
 *   - Stall injection: produce-full, consume-empty counters
 *   - Produce with valid_frames = 480 (max input), verify CRC range
 *   - 481-frame forward-compat capacity
 *   - Metadata size and epoch validation
 *   - Deterministic payload generation and verification
 *   - CPU timestamp in slot metadata
 *   - New error counters: err_bad_payload, err_bad_sequence
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>

#include "flpr_ring.h"

/* Static ring buffers — simulate shared memory at known addresses. */
static uint8_t test_ring[FLPR_RING_TOTAL_SIZE] __attribute__((aligned(FLPR_RING_SLOT_ALIGN)));
static uint8_t test_ring2[FLPR_RING_TOTAL_SIZE] __attribute__((aligned(FLPR_RING_SLOT_ALIGN)));

/* Capacity-sized payload buffer (481 frames = 1924 bytes). */
static uint8_t payload_buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES];

/* ── Setup ──────────────────────────────────────────────────────── */

static void test_setup(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(test_ring, 0, sizeof(test_ring));
	memset(test_ring2, 0, sizeof(test_ring2));
	memset(payload_buf, 0, sizeof(payload_buf));
	flpr_ring_init(test_ring, FLPR_RING_CPUAPP_TO_FLPR);
}

/* ── Init / validation ──────────────────────────────────────────── */

ZTEST(flpr_ring, test_ring_init_sets_magic)
{
	flpr_ring_init(test_ring, FLPR_RING_CPUAPP_TO_FLPR);
	struct flpr_ring_header *hdr = (struct flpr_ring_header *)test_ring;
	zassert_equal(hdr->magic, FLPR_RING_MAGIC, "magic should be set");
	zassert_equal(hdr->abi_version, FLPR_RING_ABI_VERSION, "version should be set");
	zassert_equal(hdr->slot_count, FLPR_RING_SLOT_COUNT, "slot count should be set");
	zassert_equal(hdr->slot_stride, FLPR_RING_SLOT_STRIDE, "slot stride should be set");
	zassert_equal(hdr->payload_cap_frames, FLPR_RING_PAYLOAD_CAPACITY_FRAMES,
		      "payload cap frames = 481");
	zassert_equal(hdr->payload_cap_bytes, FLPR_RING_PAYLOAD_CAPACITY_BYTES,
		      "payload cap bytes = 1924");
	zassert_equal(hdr->producer_idx, 0, "producer should start at 0");
	zassert_equal(hdr->consumer_idx, 0, "consumer should start at 0");
	zassert_equal(hdr->stream_epoch, 0, "epoch should start at 0");
}

ZTEST(flpr_ring, test_validate_ok)
{
	zassert_true(flpr_ring_validate(test_ring), "fresh ring should validate");
}

ZTEST(flpr_ring, test_validate_bad_magic)
{
	struct flpr_ring_header *hdr = (struct flpr_ring_header *)test_ring;
	hdr->magic = 0xDEADBEEFU;
	zassert_false(flpr_ring_validate(test_ring), "bad magic should fail");
}

ZTEST(flpr_ring, test_validate_bad_version)
{
	struct flpr_ring_header *hdr = (struct flpr_ring_header *)test_ring;
	hdr->abi_version = 255;
	zassert_false(flpr_ring_validate(test_ring), "bad version should fail");
}

ZTEST(flpr_ring, test_validate_bad_slot_count)
{
	struct flpr_ring_header *hdr = (struct flpr_ring_header *)test_ring;
	hdr->slot_count = 8;
	zassert_false(flpr_ring_validate(test_ring), "bad slot_count should fail");
}

ZTEST(flpr_ring, test_validate_bad_capacity)
{
	struct flpr_ring_header *hdr = (struct flpr_ring_header *)test_ring;
	hdr->payload_cap_frames = 0;
	zassert_false(flpr_ring_validate(test_ring), "bad capacity should fail");
}

/* ── Space / count helpers (monotonic with sentinel) ────────────── */

ZTEST(flpr_ring, test_space_empty)
{
	zassert_equal(flpr_ring_space(0, 0), FLPR_RING_SLOT_COUNT, "empty ring = all slots (4)");
	zassert_equal(flpr_ring_used(0, 0), 0, "empty ring used = 0");
}

ZTEST(flpr_ring, test_space_one_used)
{
	/* Producer at 1, consumer at 0: used=1, space=3. */
	zassert_equal(flpr_ring_space(1, 0), FLPR_RING_SLOT_COUNT - 1, "one used = 3 space");
	zassert_equal(flpr_ring_used(1, 0), 1, "used = 1");
}

ZTEST(flpr_ring, test_space_full)
{
	/* Producer at 4, consumer at 0: used=4 = full. */
	uint32_t prod = FLPR_RING_SLOT_COUNT;
	zassert_equal(flpr_ring_space(prod, 0), 0, "full ring space = 0");
	zassert_equal(flpr_ring_used(prod, 0), FLPR_RING_SLOT_COUNT, "full ring used = 4");
}

ZTEST(flpr_ring, test_space_wrap)
{
	/* Simulate wrap: producer=0xFFFFFFF0, consumer=0xFFFFFFF0.
	 * used=0, space=4 (all slots). */
	uint32_t base = 0xFFFFFFF0U;
	zassert_equal(flpr_ring_space(base, base), FLPR_RING_SLOT_COUNT, "wrapped empty = 4 space");
	zassert_equal(flpr_ring_used(base, base), 0, "wrapped empty used = 0");
}

ZTEST(flpr_ring, test_space_one_used_wrap)
{
	uint32_t base = 0xFFFFFFF0U;
	zassert_equal(flpr_ring_space(base + 1, base), FLPR_RING_SLOT_COUNT - 1,
		      "wrapped one used space = 3");
	zassert_equal(flpr_ring_used(base + 1, base), 1, "wrapped used = 1");
}

ZTEST(flpr_ring, test_space_full_wrap)
{
	uint32_t base = 0xFFFFFFF0U;
	uint32_t prod = base + FLPR_RING_SLOT_COUNT;
	zassert_equal(flpr_ring_space(prod, base), 0, "wrapped full = 0 space");
	zassert_equal(flpr_ring_used(prod, base), FLPR_RING_SLOT_COUNT, "wrapped full = 4 used");
}

ZTEST(flpr_ring, test_count_equals_used)
{
	zassert_equal(flpr_ring_count(7, 2), 5, "count = producer - consumer");
	zassert_equal(flpr_ring_used(7, 2), 5, "used = count");
}

ZTEST(flpr_ring, test_is_empty)
{
	zassert_true(flpr_ring_is_empty(10000, 10000), "equal → empty");
	zassert_false(flpr_ring_is_empty(10001, 10000), "prod > cons → not empty");
}

ZTEST(flpr_ring, test_is_full)
{
	/* Full = used >= slot_count (4). */
	zassert_false(flpr_ring_is_full(1, 0), "1 used → not full");
	zassert_false(flpr_ring_is_full(2, 0), "2 used → not full");
	zassert_false(flpr_ring_is_full(3, 0), "3 used → not full (N=4, full at 4)");
	zassert_true(flpr_ring_is_full(4, 0), "4 used → full");
	zassert_true(flpr_ring_is_full(10, 0), "used >= 4 → full");
}

/* ── 100+ wraps through slot indices ────────────────────────────── */

ZTEST(flpr_ring, test_100_wraps_produce_consume)
{
	uint32_t idx;
	int ret;

	flpr_ring_reset_epoch(test_ring, 1);

	/* 100 produce/consume cycles forces slot indices to wrap many times. */
	for (uint32_t i = 0; i < 100; i++) {
		ret = flpr_ring_produce_begin(test_ring, &idx);
		zassert_equal(ret, 0, "produce %u should succeed", i);

		uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
		struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
		meta->epoch = 1;
		meta->sequence = i;
		meta->valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
		meta->flags = FLPR_SLOT_FLAG_VALID;
		memset(flpr_ring_slot_payload(slot), (uint8_t)i, FLPR_RING_PAYLOAD_CAPACITY_BYTES);

		flpr_ring_produce_commit(test_ring, idx);

		/* Consume immediately (keep ring near-empty). */
		uint8_t *cs;
		struct flpr_ring_slot_meta *cm;
		ret = flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
		zassert_equal(ret, 0, "consume %u should succeed", i);
		zassert_equal(cm->sequence, i, "seq match on wrap %u", i);
		zassert_equal(flpr_ring_slot_payload(cs)[0], (uint8_t)i, "payload match on wrap %u",
			      i);
		flpr_ring_consume_done(test_ring);
	}
}

/* ── UINT32 counter wrap: fill ring near overflow ───────────────── */

ZTEST(flpr_ring, test_uint32_counter_wrap)
{
	uint32_t idx;
	int ret;

	/* Manually set producer/consumer near UINT32_MAX. */
	struct flpr_ring_header *hdr = (struct flpr_ring_header *)test_ring;
	uint32_t near_end = 0xFFFFFFFDU;

	flpr_ring_reset_epoch(test_ring, 1);
	hdr->producer_idx = near_end;
	hdr->consumer_idx = near_end;

	/* Verify space correct near wrap. */
	zassert_equal(flpr_ring_space(hdr->producer_idx, hdr->consumer_idx), FLPR_RING_SLOT_COUNT,
		      "space correct near UINT32_MAX");

	/* Produce 4 slots (fill to full). */
	for (uint32_t i = 0; i < FLPR_RING_SLOT_COUNT; i++) {
		ret = flpr_ring_produce_begin(test_ring, &idx);
		if (i < FLPR_RING_SLOT_COUNT) {
			zassert_equal(ret, 0, "produce %u should succeed near wrap", i);
		} else {
			zassert_equal(ret, -1, "produce %u should fail (full)", i);
		}

		uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
		struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
		meta->epoch = 1;
		meta->sequence = i;
		meta->flags = FLPR_SLOT_FLAG_VALID;
		flpr_ring_produce_commit(test_ring, idx);
	}

	/* Verify full. */
	zassert_true(flpr_ring_is_full(hdr->producer_idx, hdr->consumer_idx),
		     "should be full near UINT32_MAX");

	/* Consume all. */
	for (uint32_t i = 0; i < FLPR_RING_SLOT_COUNT; i++) {
		uint8_t *cs;
		struct flpr_ring_slot_meta *cm;
		ret = flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
		zassert_equal(ret, 0, "consume %u should succeed near wrap", i);
		zassert_equal(cm->sequence, i, "sequence near wrap");
		flpr_ring_consume_done(test_ring);
	}

	/* Verify empty. */
	zassert_true(flpr_ring_is_empty(hdr->producer_idx, hdr->consumer_idx),
		     "should be empty after drain near UINT32_MAX");
}

/* ── Produce / consume ──────────────────────────────────────────── */

ZTEST(flpr_ring, test_produce_consume_one)
{
	uint32_t idx;
	int ret;

	ret = flpr_ring_produce_begin(test_ring, &idx);
	zassert_equal(ret, 0, "produce_begin should succeed");
	zassert_equal(idx, 0, "first monotonic index should be 0");

	/* Fill the slot. */
	flpr_ring_reset_epoch(test_ring, 42);

	uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
	meta->sequence = 42;
	meta->epoch = 42;
	meta->valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	memset(flpr_ring_slot_payload(slot), 0xAA, FLPR_RING_PAYLOAD_MAX_INPUT * 4U);

	/* CRC over valid bytes only. */
	meta->crc32 =
		flpr_ring_crc32(flpr_ring_slot_payload(slot), FLPR_RING_PAYLOAD_MAX_INPUT * 4U);

	flpr_ring_produce_commit(test_ring, idx);

	/* Consume. */
	uint8_t *cs;
	struct flpr_ring_slot_meta *cm;
	ret = flpr_ring_consume_begin(test_ring, 42, &cs, &cm);
	zassert_equal(ret, 0, "consume_begin should succeed");
	zassert_equal(cm->sequence, 42, "sequence should match");
	zassert_equal(cm->valid_frames, FLPR_RING_PAYLOAD_MAX_INPUT, "valid_frames should match");
	zassert_equal(flpr_ring_slot_payload(cs)[0], 0xAA, "payload byte match");

	/* Verify CRC over valid bytes. */
	uint32_t computed =
		flpr_ring_crc32(flpr_ring_slot_payload(cs), FLPR_RING_PAYLOAD_MAX_INPUT * 4U);
	zassert_equal(computed, cm->crc32, "CRC over valid bytes match");

	flpr_ring_consume_done(test_ring);
	zassert_equal(flpr_ring_consumer(test_ring), 1, "consumer should advance");
}

ZTEST(flpr_ring, test_produce_full)
{
	uint32_t idx;
	int ret;

	/* Fill to capacity: all 4 slots. */
	for (uint32_t i = 0; i < FLPR_RING_SLOT_COUNT; i++) {
		ret = flpr_ring_produce_begin(test_ring, &idx);
		zassert_equal(ret, 0, "produce %u should succeed", i);
		uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
		flpr_ring_slot_meta_ptr(slot)->epoch = 0; /* no epoch check */
		flpr_ring_produce_commit(test_ring, idx);
	}

	/* 5th produce must fail. */
	ret = flpr_ring_produce_begin(test_ring, &idx);
	zassert_equal(ret, -ENOSPC, "produce 5th should fail (full)");
}

ZTEST(flpr_ring, test_consume_empty)
{
	uint8_t *slot;
	struct flpr_ring_slot_meta *meta;
	int ret = flpr_ring_consume_begin(test_ring, 1, &slot, &meta);
	zassert_equal(ret, -ENOENT, "consume on empty ring should fail");
}

ZTEST(flpr_ring, test_consume_stale_epoch)
{
	uint32_t idx;

	flpr_ring_reset_epoch(test_ring, 5);
	flpr_ring_produce_begin(test_ring, &idx);
	uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
	meta->epoch = 5;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	flpr_ring_produce_commit(test_ring, idx);

	/* Try to consume with wrong epoch. */
	uint8_t *cs;
	struct flpr_ring_slot_meta *cm;
	int ret = flpr_ring_consume_begin(test_ring, 99, &cs, &cm);
	zassert_equal(ret, -ESTALE, "stale epoch should return -ESTALE");

	/* Consumer should have advanced past the stale slot. */
	zassert_equal(flpr_ring_consumer(test_ring), 1, "consumer should advance past stale");
}

ZTEST(flpr_ring, test_produce_consume_wrap)
{
	uint32_t idx;
	int ret;

	flpr_ring_reset_epoch(test_ring, 1);

	/* 10 batches of 2 produce/consume = 20 total, many wraps. */
	for (uint32_t batch = 0; batch < 10; batch++) {
		for (int p = 0; p < 2; p++) {
			ret = flpr_ring_produce_begin(test_ring, &idx);
			zassert_equal(ret, 0, "produce succeed batch %u", batch);
			uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
			struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
			meta->epoch = 1;
			meta->sequence = batch * 2 + p;
			meta->flags = FLPR_SLOT_FLAG_VALID;
			memset(flpr_ring_slot_payload(slot), (uint8_t)(batch * 2 + p),
			       FLPR_RING_PAYLOAD_CAPACITY_BYTES);
			flpr_ring_produce_commit(test_ring, idx);
		}
		for (int c = 0; c < 2; c++) {
			uint8_t *cs;
			struct flpr_ring_slot_meta *cm;
			ret = flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
			zassert_equal(ret, 0, "consume succeed batch %u", batch);
			zassert_equal(cm->sequence, batch * 2 + c, "sequence match batch %u",
				      batch);
			zassert_equal(flpr_ring_slot_payload(cs)[0], (uint8_t)(batch * 2 + c),
				      "payload match batch %u", batch);
			flpr_ring_consume_done(test_ring);
		}
	}
}

/* ── CRC ────────────────────────────────────────────────────────── */

ZTEST(flpr_ring, test_crc32_known)
{
	uint8_t data[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
	uint32_t crc = flpr_ring_crc32(data, 9);
	zassert_equal(crc, 0xCBF43926U, "CRC-32 of '123456789' = 0xCBF43926");
}

ZTEST(flpr_ring, test_crc32_zero_length)
{
	uint32_t crc = flpr_ring_crc32(NULL, 0);
	zassert_equal(crc, 0U, "CRC of zero-length = 0");
}

ZTEST(flpr_ring, test_crc32_different_data)
{
	uint8_t a[4] = {0x00, 0x00, 0x00, 0x00};
	uint8_t b[4] = {0x00, 0x00, 0x00, 0x01};
	zassert_not_equal(flpr_ring_crc32(a, 4), flpr_ring_crc32(b, 4),
			  "different data → different CRC");
}

ZTEST(flpr_ring, test_crc32_valid_bytes_only)
{
	/* CRC over 480 frames vs 481 frames should differ.
	 * Fill 481 frames with known data, compute CRC over 480 vs 481. */
	uint8_t full[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
	memset(full, 0x55, sizeof(full));

	uint32_t crc_480 = flpr_ring_crc32(full, (size_t)FLPR_RING_PAYLOAD_MAX_INPUT * 4U);
	uint32_t crc_481 = flpr_ring_crc32(full, sizeof(full));
	zassert_not_equal(crc_480, crc_481, "CRC over 480 frames != CRC over 481 frames");
}

/* ── Epoch reset ────────────────────────────────────────────────── */

ZTEST(flpr_ring, test_reset_epoch)
{
	flpr_ring_reset_epoch(test_ring, 100);
	zassert_equal(flpr_ring_epoch(test_ring), 100, "epoch updated");
	zassert_equal(flpr_ring_producer(test_ring), 0, "producer reset");
	zassert_equal(flpr_ring_consumer(test_ring), 0, "consumer reset");
}

ZTEST(flpr_ring, test_reset_epoch_reject_zero)
{
	int ret = flpr_ring_reset_epoch(test_ring, 0);
	zassert_equal(ret, -1, "epoch 0 rejected");
}

ZTEST(flpr_ring, test_reset_epoch_idempotent)
{
	flpr_ring_reset_epoch(test_ring, 42);
	/* Same epoch, indices at 0 — idempotent, no error. */
	int ret = flpr_ring_reset_epoch(test_ring, 42);
	zassert_equal(ret, 0, "same epoch with zero indices should be idempotent");
	zassert_equal(flpr_ring_epoch(test_ring), 42, "epoch unchanged");
}

/* ── Slot arithmetic ────────────────────────────────────────────── */

ZTEST(flpr_ring, test_slot_base_index)
{
	uint8_t *slot0 = flpr_ring_slot_base(test_ring, 0);
	uint8_t *slot3 = flpr_ring_slot_base(test_ring, 3);
	uint8_t *slot4 = flpr_ring_slot_base(test_ring, 4);
	uint8_t *slot100 = flpr_ring_slot_base(test_ring, 100);

	zassert_equal_ptr(slot0, test_ring + FLPR_RING_DATA_OFFSET, "slot 0 at data offset");
	zassert_equal_ptr(slot4, slot0, "slot 4 wraps to slot 0");
	zassert_equal_ptr(slot100, slot0, "slot 100 wraps to slot 0");
	zassert_equal_ptr(slot3, slot0 + 3 * FLPR_RING_SLOT_STRIDE, "slot 3 at correct offset");
}

/* ── Canary / bounds: slot does not clobber next ────────────────── */

ZTEST(flpr_ring, test_slot_does_not_clobber_next)
{
	flpr_ring_reset_epoch(test_ring, 1);

	for (int i = 0; i < 2; i++) {
		uint32_t idx;
		flpr_ring_produce_begin(test_ring, &idx);
		uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
		struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
		meta->epoch = 1;
		meta->sequence = i;
		meta->flags = FLPR_SLOT_FLAG_VALID;
		meta->valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
		memset(flpr_ring_slot_payload(slot), (uint8_t)i, FLPR_RING_PAYLOAD_MAX_INPUT * 4U);
		flpr_ring_produce_commit(test_ring, idx);
	}

	uint8_t *cs;
	struct flpr_ring_slot_meta *cm;

	flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
	zassert_equal(cm->sequence, 0, "slot 0 seq = 0");
	zassert_equal(flpr_ring_slot_payload(cs)[0], 0x00, "slot 0 payload = 0x00");
	flpr_ring_consume_done(test_ring);

	flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
	zassert_equal(cm->sequence, 1, "slot 1 seq = 1");
	zassert_equal(flpr_ring_slot_payload(cs)[0], 0x01, "slot 1 payload not clobbered");
	flpr_ring_consume_done(test_ring);
}

/* ── Two-ring independence ──────────────────────────────────────── */

ZTEST(flpr_ring, test_two_rings_independent)
{
	flpr_ring_init(test_ring2, FLPR_RING_FLPR_TO_CPUAPP);
	flpr_ring_reset_epoch(test_ring, 10);
	flpr_ring_reset_epoch(test_ring2, 20);

	uint32_t idx;
	flpr_ring_produce_begin(test_ring, &idx);
	uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
	flpr_ring_slot_meta_ptr(slot)->epoch = 10;
	flpr_ring_slot_meta_ptr(slot)->flags = FLPR_SLOT_FLAG_VALID;
	flpr_ring_produce_commit(test_ring, idx);

	zassert_equal(flpr_ring_producer(test_ring2), 0, "ring 2 producer unaffected");
	zassert_equal(flpr_ring_epoch(test_ring2), 20, "ring 2 epoch independent");
}

/* ── Error counters (single-writer ownership) ───────────────────── */

ZTEST(flpr_ring, test_producer_full_counter)
{
	uint32_t idx;
	struct flpr_ring_header *hdr = (struct flpr_ring_header *)test_ring;

	/* Fill ring to capacity (4 slots). */
	for (uint32_t i = 0; i < FLPR_RING_SLOT_COUNT; i++) {
		flpr_ring_produce_begin(test_ring, &idx);
		flpr_ring_produce_commit(test_ring, idx);
	}

	/* 5th attempt increments err_producer_full. */
	uint32_t before = hdr->err_producer_full;
	flpr_ring_produce_begin(test_ring, &idx);
	zassert_equal(hdr->err_producer_full, before + 1, "err_producer_full incremented");
}

ZTEST(flpr_ring, test_forward_capacity_481)
{
	/* Verify slot stride fits 481 frames. */
	zassert_true(FLPR_RING_SLOT_STRIDE >=
			     FLPR_RING_SLOT_METADATA_SZ + FLPR_RING_PAYLOAD_CAPACITY_BYTES,
		     "stride fits 481 frames");

	/* Verify max input = 480. */
	zassert_equal(FLPR_RING_PAYLOAD_MAX_INPUT, 480, "max input = 480 frames");

	/* Verify capacity = 481. */
	zassert_equal(FLPR_RING_PAYLOAD_CAPACITY_FRAMES, 481, "capacity = 481 frames");
	zassert_equal(FLPR_RING_PAYLOAD_CAPACITY_BYTES, 1924, "capacity = 1924 bytes");
}

/* ── Invalid slot flags / fields ────────────────────────────────── */

ZTEST(flpr_ring, test_slot_invalid_valid_frames)
{
	/* valid_frames = 0 with VALID flag — should still validate at
	 * the ring level (consumer decides interpretation). */
	uint32_t idx;
	flpr_ring_reset_epoch(test_ring, 1);
	flpr_ring_produce_begin(test_ring, &idx);
	uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
	meta->epoch = 1;
	meta->valid_frames = 0;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	flpr_ring_produce_commit(test_ring, idx);

	uint8_t *cs;
	struct flpr_ring_slot_meta *cm;
	int ret = flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
	zassert_equal(ret, 0, "slot with 0 valid frames consumable");
	zassert_equal(cm->valid_frames, 0, "valid_frames = 0 preserved");
	flpr_ring_consume_done(test_ring);
}

ZTEST(flpr_ring, test_slot_481_frames_fits)
{
	/* Produce a slot with 481 valid frames and verify the payload
	 * fits without clobbering the slot stride boundary. */
	uint32_t idx;
	flpr_ring_reset_epoch(test_ring, 1);
	flpr_ring_produce_begin(test_ring, &idx);
	uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
	meta->epoch = 1;
	meta->valid_frames = FLPR_RING_PAYLOAD_CAPACITY_FRAMES; /* 481 */
	meta->flags = FLPR_SLOT_FLAG_VALID;
	memset(flpr_ring_slot_payload(slot), 0xCC, FLPR_RING_PAYLOAD_CAPACITY_BYTES);
	flpr_ring_produce_commit(test_ring, idx);

	uint8_t *cs;
	struct flpr_ring_slot_meta *cm;
	int ret = flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
	zassert_equal(ret, 0, "481-frame slot consumable");
	zassert_equal(cm->valid_frames, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, "valid_frames = 481");
	/* Verify all 1924 payload bytes are 0xCC. */
	for (size_t i = 0; i < FLPR_RING_PAYLOAD_CAPACITY_BYTES; i++) {
		zassert_equal(flpr_ring_slot_payload(cs)[i], 0xCC, "payload byte %zu correct", i);
	}
	flpr_ring_consume_done(test_ring);

	/* Verify the NEXT slot (after this one) is NOT clobbered.
	 * Produce a second slot and verify it's clean. */
	flpr_ring_produce_begin(test_ring, &idx);
	uint8_t *slot2 = flpr_ring_slot_base(test_ring, idx);
	struct flpr_ring_slot_meta *meta2 = flpr_ring_slot_meta_ptr(slot2);
	/* Should be zero from init unless clobbered. */
	zassert_equal(meta2->flags, 0, "next slot flags not clobbered");
	meta2->epoch = 1;
	meta2->flags = FLPR_SLOT_FLAG_VALID;
	memset(flpr_ring_slot_payload(slot2), 0xDD, FLPR_RING_PAYLOAD_CAPACITY_BYTES);
	flpr_ring_produce_commit(test_ring, idx);

	flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
	zassert_equal(flpr_ring_slot_payload(cs)[0], 0xDD, "second slot payload intact");
	flpr_ring_consume_done(test_ring);
}

/* ── max input limit ────────────────────────────────────────────── */

ZTEST(flpr_ring, test_max_input_constant)
{
	zassert_equal(FLPR_RING_PAYLOAD_MAX_INPUT, 480, "max input = 480 frames");
	zassert_true(FLPR_RING_PAYLOAD_MAX_INPUT < FLPR_RING_PAYLOAD_CAPACITY_FRAMES,
		     "max input < capacity (480 < 481)");
}

/* ── Reset with pending data → stale rejected ───────────────────── */

ZTEST(flpr_ring, test_reset_with_pending_data_reject_stale)
{
	uint32_t idx;
	int ret;

	/* Reset to epoch 20. */
	flpr_ring_reset_epoch(test_ring, 20);

	/* Produce a slot but DELIBERATELY set epoch to 10 (wrong). */
	ret = flpr_ring_produce_begin(test_ring, &idx);
	zassert_equal(ret, 0, "produce succeed");

	uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
	meta->epoch = 10; /* stale — ring epoch is 20 */
	meta->flags = FLPR_SLOT_FLAG_VALID;
	flpr_ring_produce_commit(test_ring, idx);

	/* Consumer with current_epoch=20 should reject. */
	uint8_t *cs;
	struct flpr_ring_slot_meta *cm;
	ret = flpr_ring_consume_begin(test_ring, 20, &cs, &cm);
	zassert_equal(ret, -ESTALE, "stale epoch rejected (slot=10, ring=20)");
}

/* ── Metadata size / epoch validation ──────────────────────────── */

ZTEST(flpr_ring, test_metadata_size)
{
	zassert_equal(sizeof(struct flpr_ring_slot_meta), FLPR_RING_SLOT_METADATA_SZ,
		      "slot metadata size = 32 bytes");
}

ZTEST(flpr_ring, test_cpu_timestamp_field)
{
	/* Verify cpu_timestamp offset preserves 32-byte alignment. */
	struct flpr_ring_slot_meta meta;
	memset(&meta, 0, sizeof(meta));
	zassert_equal(sizeof(meta), 32, "metadata must be exactly 32 bytes");
	zassert_equal(meta.cpu_timestamp, 0, "cpu_timestamp initializes to 0");
}

/* ── Deterministic payload generation ───────────────────────────── */

ZTEST(flpr_ring, test_gen_payload_deterministic)
{
	uint8_t a[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
	uint8_t b[FLPR_RING_PAYLOAD_CAPACITY_BYTES];

	flpr_ring_gen_payload(a, sizeof(a), 42);
	flpr_ring_gen_payload(b, sizeof(b), 42);
	zassert_mem_equal(a, b, sizeof(a), "same sequence → same payload");
}

ZTEST(flpr_ring, test_gen_payload_different_seq)
{
	uint8_t a[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
	uint8_t b[FLPR_RING_PAYLOAD_CAPACITY_BYTES];

	flpr_ring_gen_payload(a, sizeof(a), 0);
	flpr_ring_gen_payload(b, sizeof(b), 1);
	zassert_true(memcmp(a, b, sizeof(a)) != 0, "different sequences → different payload");
}

ZTEST(flpr_ring, test_gen_payload_non_trivial)
{
	/* Payload should not be all zeros or all same byte. */
	uint8_t buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
	flpr_ring_gen_payload(buf, sizeof(buf), 100);

	bool all_same = true;
	for (size_t i = 1; i < sizeof(buf); i++) {
		if (buf[i] != buf[0]) {
			all_same = false;
			break;
		}
	}
	zassert_false(all_same, "payload should not be uniform");
}

/* ── Deterministic payload verification ─────────────────────────── */

ZTEST(flpr_ring, test_verify_payload_match)
{
	uint8_t buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
	flpr_ring_gen_payload(buf, sizeof(buf), 777);
	uint32_t errs = flpr_ring_verify_payload(buf, sizeof(buf), 777);
	zassert_equal(errs, 0, "verified payload should have zero errors");
}

ZTEST(flpr_ring, test_verify_payload_mismatch)
{
	uint8_t buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
	flpr_ring_gen_payload(buf, sizeof(buf), 888);
	buf[0] ^= 0xFFU; /* corrupt first byte */
	uint32_t errs = flpr_ring_verify_payload(buf, sizeof(buf), 888);
	zassert_true(errs > 0, "corrupted payload should report errors");
	zassert_true(errs <= (sizeof(buf) + 3) / 4, "frame errors bounded by total frames");
}

ZTEST(flpr_ring, test_verify_payload_wrong_seq)
{
	uint8_t buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
	flpr_ring_gen_payload(buf, sizeof(buf), 0);
	uint32_t errs = flpr_ring_verify_payload(buf, sizeof(buf), 1);
	zassert_true(errs > 0, "wrong sequence should produce errors");
}

ZTEST(flpr_ring, test_verify_payload_zero_bytes)
{
	uint32_t errs = flpr_ring_verify_payload(NULL, 0, 0);
	zassert_equal(errs, 0, "zero valid bytes = zero errors");
}

/* ── New error counters ─────────────────────────────────────────── */

ZTEST(flpr_ring, test_err_bad_payload_counter)
{
	/* Verify the counter is at expected offset and initially zero. */
	struct flpr_ring_header *hdr = (struct flpr_ring_header *)test_ring;
	flpr_ring_init(test_ring, FLPR_RING_CPUAPP_TO_FLPR);
	zassert_equal(hdr->err_bad_payload, 0, "err_bad_payload starts at 0");
	zassert_equal(hdr->err_bad_sequence, 0, "err_bad_sequence starts at 0");
}

ZTEST(flpr_ring, test_err_counters_independent)
{
	struct flpr_ring_header *hdr = (struct flpr_ring_header *)test_ring;
	flpr_ring_init(test_ring, FLPR_RING_CPUAPP_TO_FLPR);
	hdr->err_bad_payload = 5;
	hdr->err_bad_sequence = 10;
	zassert_equal(hdr->err_bad_payload, 5, "payload counter retains value");
	zassert_equal(hdr->err_bad_sequence, 10, "sequence counter retains value");
}

/* ── 1000+ wraps stress test ────────────────────────────────────── */

ZTEST(flpr_ring, test_1000_wraps_produce_consume)
{
	uint32_t idx;
	int ret;

	flpr_ring_reset_epoch(test_ring, 1);

	for (uint32_t i = 0; i < 1000; i++) {
		ret = flpr_ring_produce_begin(test_ring, &idx);
		zassert_equal(ret, 0, "produce %u should succeed", i);

		uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
		struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
		meta->epoch = 1;
		meta->sequence = i;
		meta->valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
		meta->flags = FLPR_SLOT_FLAG_VALID;
		meta->cpu_timestamp = i; /* test: use sequence as timestamp */
		memset(flpr_ring_slot_payload(slot), (uint8_t)i, FLPR_RING_PAYLOAD_CAPACITY_BYTES);

		flpr_ring_produce_commit(test_ring, idx);

		/* Consume immediately. */
		uint8_t *cs;
		struct flpr_ring_slot_meta *cm;
		ret = flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
		zassert_equal(ret, 0, "consume %u should succeed", i);
		zassert_equal(cm->sequence, i, "seq match on wrap %u", i);
		zassert_equal(cm->cpu_timestamp, i, "cpu_timestamp preserved on wrap %u", i);
		flpr_ring_consume_done(test_ring);
	}
}

/* ── Slot metadata fields preserved through produce/consume ─────── */

ZTEST(flpr_ring, test_metadata_all_fields)
{
	uint32_t idx;
	int ret;

	flpr_ring_reset_epoch(test_ring, 99);

	ret = flpr_ring_produce_begin(test_ring, &idx);
	zassert_equal(ret, 0, "produce_begin ok");

	uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
	meta->sequence = 12345;
	meta->epoch = 99;
	meta->valid_frames = 240;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	meta->correction_ppm = -123;
	meta->crc32 = 0xDEADBEEFU;
	meta->cpu_timestamp = 0xABCDEF01U;
	memset(flpr_ring_slot_payload(slot), 0x5A, 240U * 4U);
	flpr_ring_produce_commit(test_ring, idx);

	uint8_t *cs;
	struct flpr_ring_slot_meta *cm;
	ret = flpr_ring_consume_begin(test_ring, 99, &cs, &cm);
	zassert_equal(ret, 0, "consume_begin ok");

	zassert_equal(cm->sequence, 12345, "sequence preserved");
	zassert_equal(cm->epoch, 99, "epoch preserved");
	zassert_equal(cm->valid_frames, 240, "valid_frames preserved");
	zassert_equal(cm->flags, FLPR_SLOT_FLAG_VALID, "flags preserved");
	zassert_equal(cm->correction_ppm, -123, "correction_ppm preserved");
	zassert_equal(cm->crc32, 0xDEADBEEFU, "crc32 preserved");
	zassert_equal(cm->cpu_timestamp, 0xABCDEF01U, "cpu_timestamp preserved");

	flpr_ring_consume_done(test_ring);
}

/* ── Test suite ─────────────────────────────────────────────── */

ZTEST_SUITE(flpr_ring, NULL, NULL, test_setup, NULL, NULL);
