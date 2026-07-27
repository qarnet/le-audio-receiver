/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for flpr_ring.h / flpr_ring.c SPSC ring operations.
 * Uses native_sim target — pure C, no Zephyr kernel deps beyond
 * the cache hook file (which has a native fallback).
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "flpr_ring.h"

/* Static ring buffers for test — simulate shared memory at known addresses. */
static uint8_t test_ring[FLPR_RING_TOTAL_SIZE] __attribute__((aligned(FLPR_RING_SLOT_ALIGN)));

static uint8_t test_ring2[FLPR_RING_TOTAL_SIZE] __attribute__((aligned(FLPR_RING_SLOT_ALIGN)));

static uint8_t payload_buf[FLPR_RING_PAYLOAD_BYTES];

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
	zassert_equal(hdr->producer_idx, 0, "producer should start at 0");
	zassert_equal(hdr->consumer_idx, 0, "consumer should start at 0");
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

/* ── Space / count helpers ──────────────────────────────────────── */

ZTEST(flpr_ring, test_space_empty)
{
	/* Producer == consumer: 0 slots consumed, N-1 space. */
	uint32_t space = flpr_ring_space(0, 0);
	zassert_equal(space, FLPR_RING_SLOT_COUNT - 1, "empty ring should have N-1 space");
}

ZTEST(flpr_ring, test_space_full)
{
	/* Producer one ahead of consumer: 0 space. */
	uint32_t space = flpr_ring_space(FLPR_RING_SLOT_COUNT - 1, 0);
	zassert_equal(space, 0, "full ring should have 0 space");
}

ZTEST(flpr_ring, test_count_zero)
{
	zassert_equal(flpr_ring_count(0, 0), 0, "empty ring count = 0");
}

ZTEST(flpr_ring, test_count_wrap)
{
	/* Consumer at slot 3, producer wrapped to slot 1 = 2 slots. */
	uint32_t count = flpr_ring_count(1, 3);
	zassert_equal(count, 2, "wrap count should be 2 (slots 3,0→2 total)");
}

ZTEST(flpr_ring, test_is_empty)
{
	zassert_true(flpr_ring_is_empty(0, 0), "0,0 should be empty");
	zassert_false(flpr_ring_is_empty(1, 0), "1,0 should not be empty");
}

ZTEST(flpr_ring, test_is_full)
{
	zassert_true(flpr_ring_is_full(FLPR_RING_SLOT_COUNT - 1, 0), "N-1,0 should be full");
}

/* ── Produce / consume ──────────────────────────────────────────── */

ZTEST(flpr_ring, test_produce_consume_one)
{
	uint32_t idx;
	int ret;

	ret = flpr_ring_produce_begin(test_ring, &idx);
	zassert_equal(ret, 0, "produce_begin should succeed");
	zassert_equal(idx, 0, "first slot should be 0");

	/* Fill payload with pattern. */
	uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
	meta->sequence = 42;
	meta->epoch = 1;
	meta->valid_frames = FLPR_RING_PAYLOAD_FRAMES;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	memset(flpr_ring_slot_payload(slot), 0xAA, FLPR_RING_PAYLOAD_BYTES);

	flpr_ring_produce_commit(test_ring, idx);

	/* Now: producer=1, consumer=0. Test external reads. */

	/* Reset epoch so consumer can read. */
	flpr_ring_reset_epoch(test_ring, 42);

	/* Produce again after reset so slot metadata uses new epoch. */
	flpr_ring_produce_begin(test_ring, &idx);
	slot = flpr_ring_slot_base(test_ring, idx);
	meta = flpr_ring_slot_meta_ptr(slot);
	meta->sequence = 42;
	meta->epoch = 42; /* match ring epoch */
	meta->valid_frames = FLPR_RING_PAYLOAD_FRAMES;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	memset(flpr_ring_slot_payload(slot), 0xAA, FLPR_RING_PAYLOAD_BYTES);
	flpr_ring_produce_commit(test_ring, idx);

	/* Consume. */
	uint8_t *cs;
	struct flpr_ring_slot_meta *cm;
	ret = flpr_ring_consume_begin(test_ring, 42, &cs, &cm);
	zassert_equal(ret, 0, "consume_begin should succeed");
	zassert_equal(cm->sequence, 42, "sequence should match");
	zassert_equal(cm->epoch, 42, "epoch should match ring epoch");
	zassert_equal(flpr_ring_slot_payload(cs)[0], 0xAA, "payload should match");

	flpr_ring_consume_done(test_ring);
	zassert_equal(flpr_ring_consumer(test_ring), 1, "consumer should advance");
}

ZTEST(flpr_ring, test_produce_full)
{
	uint32_t idx;
	int ret;

	/* Fill all slots. */
	for (uint32_t i = 0; i < FLPR_RING_SLOT_COUNT; i++) {
		ret = flpr_ring_produce_begin(test_ring, &idx);
		if (i < FLPR_RING_SLOT_COUNT - 1) {
			zassert_equal(ret, 0, "produce %u should succeed", i);
		} else {
			zassert_equal(ret, -1, "produce %u should fail (full)", i);
			break;
		}
		flpr_ring_produce_commit(test_ring, idx);
	}
}

ZTEST(flpr_ring, test_consume_empty)
{
	uint8_t *slot;
	struct flpr_ring_slot_meta *meta;
	int ret = flpr_ring_consume_begin(test_ring, 1, &slot, &meta);
	zassert_equal(ret, -1, "consume on empty ring should fail");
}

ZTEST(flpr_ring, test_consume_stale_epoch)
{
	uint32_t idx;

	/* Produce one slot with epoch 5. */
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
	zassert_equal(ret, -2, "stale epoch should fail");
}

ZTEST(flpr_ring, test_produce_consume_wrap)
{
	/* Produce and consume in a pattern that wraps around. */
	uint32_t idx;
	int ret;

	flpr_ring_reset_epoch(test_ring, 1);

	for (uint32_t batch = 0; batch < 10; batch++) {
		/* Produce 2, consume 2. */
		for (int p = 0; p < 2; p++) {
			ret = flpr_ring_produce_begin(test_ring, &idx);
			zassert_equal(ret, 0, "produce should succeed");
			uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
			struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
			meta->epoch = 1;
			meta->sequence = batch * 2 + p;
			meta->flags = FLPR_SLOT_FLAG_VALID;
			memset(flpr_ring_slot_payload(slot), (uint8_t)(batch * 2 + p),
			       FLPR_RING_PAYLOAD_BYTES);
			flpr_ring_produce_commit(test_ring, idx);
		}
		for (int c = 0; c < 2; c++) {
			uint8_t *cs;
			struct flpr_ring_slot_meta *cm;
			ret = flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
			zassert_equal(ret, 0, "consume should succeed (batch %u)", batch);
			zassert_equal(cm->sequence, batch * 2 + c,
				      "sequence should match (batch %u)", batch);
			zassert_equal(flpr_ring_slot_payload(cs)[0], (uint8_t)(batch * 2 + c),
				      "payload pattern should match");
			flpr_ring_consume_done(test_ring);
		}
	}
}

/* ── CRC ────────────────────────────────────────────────────────── */

ZTEST(flpr_ring, test_crc32_known)
{
	/* "123456789" → 0xCBF43926 (standard test vector). */
	uint8_t data[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
	uint32_t crc = flpr_ring_crc32(data, 9);
	zassert_equal(crc, 0xCBF43926U, "CRC-32 of '123456789' should match known vector");
}

ZTEST(flpr_ring, test_crc32_zero_length)
{
	uint32_t crc = flpr_ring_crc32(NULL, 0);
	zassert_equal(crc, 0U, "CRC of zero-length data should be 0");
}

ZTEST(flpr_ring, test_crc32_different_data)
{
	uint8_t a[4] = {0x00, 0x00, 0x00, 0x00};
	uint8_t b[4] = {0x00, 0x00, 0x00, 0x01};
	uint32_t ca = flpr_ring_crc32(a, 4);
	uint32_t cb = flpr_ring_crc32(b, 4);
	zassert_not_equal(ca, cb, "different data should produce different CRC");
}

/* ── Reset epoch ────────────────────────────────────────────────── */

ZTEST(flpr_ring, test_reset_epoch)
{
	flpr_ring_reset_epoch(test_ring, 100);
	zassert_equal(flpr_ring_epoch(test_ring), 100, "epoch should be updated");
	zassert_equal(flpr_ring_producer(test_ring), 0, "producer reset to 0");
	zassert_equal(flpr_ring_consumer(test_ring), 0, "consumer reset to 0");
}

ZTEST(flpr_ring, test_reset_epoch_reject_zero)
{
	int ret = flpr_ring_reset_epoch(test_ring, 0);
	zassert_equal(ret, -1, "epoch 0 should be rejected");
}

ZTEST(flpr_ring, test_reset_epoch_reject_same)
{
	flpr_ring_reset_epoch(test_ring, 42);
	int ret = flpr_ring_reset_epoch(test_ring, 42);
	zassert_equal(ret, -1, "same epoch should be rejected");
}

/* ── Slot arithmetic ────────────────────────────────────────────── */

ZTEST(flpr_ring, test_slot_base_index)
{
	uint8_t *slot0 = flpr_ring_slot_base(test_ring, 0);
	uint8_t *slot3 = flpr_ring_slot_base(test_ring, 3);
	uint8_t *slot4 = flpr_ring_slot_base(test_ring, 4);

	/* Slot 0 should be at data offset from ring base. */
	zassert_equal_ptr(slot0, test_ring + FLPR_RING_DATA_OFFSET, "slot 0 at data offset");

	/* Slot 4 should wrap to slot 0. */
	zassert_equal_ptr(slot4, slot0, "slot 4 wraps to slot 0");

	/* Slot 3 should be 3 strides from data offset. */
	zassert_equal_ptr(slot3, slot0 + 3 * FLPR_RING_SLOT_STRIDE, "slot 3 at correct offset");
}

/* ── Canary / bounds check ──────────────────────────────────────── */

ZTEST(flpr_ring, test_slot_does_not_clobber_next)
{
	flpr_ring_reset_epoch(test_ring, 1);

	/* Produce two slots with different patterns. */
	for (int i = 0; i < 2; i++) {
		uint32_t idx;
		flpr_ring_produce_begin(test_ring, &idx);
		uint8_t *slot = flpr_ring_slot_base(test_ring, idx);
		struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);
		meta->epoch = 1;
		meta->sequence = i;
		meta->flags = FLPR_SLOT_FLAG_VALID;
		memset(flpr_ring_slot_payload(slot), (uint8_t)i, FLPR_RING_PAYLOAD_BYTES);
		flpr_ring_produce_commit(test_ring, idx);
	}

	/* Consume slot 0. */
	uint8_t *cs;
	struct flpr_ring_slot_meta *cm;
	flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
	zassert_equal(cm->sequence, 0, "slot 0 seq should be 0");
	zassert_equal(flpr_ring_slot_payload(cs)[0], 0x00, "slot 0 payload should be 0x00");
	flpr_ring_consume_done(test_ring);

	/* Consume slot 1 — payload should NOT be clobbered. */
	flpr_ring_consume_begin(test_ring, 1, &cs, &cm);
	zassert_equal(cm->sequence, 1, "slot 1 seq should be 1");
	zassert_equal(flpr_ring_slot_payload(cs)[0], 0x01,
		      "slot 1 payload should be 0x01, NOT clobbered");
	flpr_ring_consume_done(test_ring);
}

/* ── Two-ring independence ──────────────────────────────────────── */

ZTEST(flpr_ring, test_two_rings_independent)
{
	flpr_ring_init(test_ring2, FLPR_RING_FLPR_TO_CPUAPP);
	flpr_ring_reset_epoch(test_ring, 10);
	flpr_ring_reset_epoch(test_ring2, 20);

	/* Produce into ring 1. */
	uint32_t idx;
	flpr_ring_produce_begin(test_ring, &idx);
	flpr_ring_slot_meta_ptr(flpr_ring_slot_base(test_ring, idx))->epoch = 10;
	flpr_ring_slot_meta_ptr(flpr_ring_slot_base(test_ring, idx))->flags = FLPR_SLOT_FLAG_VALID;
	flpr_ring_produce_commit(test_ring, idx);

	/* Ring 2 should still be empty. */
	zassert_equal(flpr_ring_producer(test_ring2), 0, "ring 2 producer unaffected");
	zassert_equal(flpr_ring_epoch(test_ring2), 20, "ring 2 epoch independent");
}

/* ── Test suite ─────────────────────────────────────────────── */

ZTEST_SUITE(flpr_ring, NULL, NULL, test_setup, NULL, NULL);
