/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the FLPR ring manager — real production source
 * execution (T1B).
 *
 * This suite compiles src/flpr_ring_mgr.c, src/flpr_ring.c, and
 * src/flpr_cache.c with FLPR_RING_MGR_NATIVE_TEST: DT ring pointers are
 * replaced by two aligned host arrays and the cycle counter is
 * test-controlled.  The production static IPC handlers (on_ring_reset_ack,
 * on_ring_consumer, on_ring_test_report, on_ring_stall_ack) are invoked
 * through the captured handlers of the handshake mock, and the real
 * flpr_ring producer/consumer APIs are used to arrange ring contents.
 * No copied reset/produce/consume/callback algorithm is used.
 */
#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>

#include "flpr_ring_mgr.h"
#include "flpr_ring_mgr_hooks.h"
#include "mock_flpr_handshake.h"
#include "flpr_ring.h"
#include "flpr_protocol.h"
#include "audio_asrc.h"

/* ── Helpers ────────────────────────────────────────────────────── */

static void rm_setup(void *fixture)
{
	(void)fixture;
	flpr_ring_mgr_test_reset_state();
	mock_hs_reset();
	flpr_ring_mgr_test_set_cycle(0);
}

/* Standard arrangement: FLPR ready+acked, manager init, epoch reset. */
static void rm_init_and_reset(uint32_t epoch)
{
	mock_hs_set_ready_acked(true, true);
	zassert_ok(flpr_ring_mgr_init(), "init");
	zassert_ok(flpr_ring_mgr_reset(epoch), "reset");
}

/* Arrange a slot in the OUTPUT ring using the real flpr_ring APIs. */
static void arrange_output_slot(uint32_t epoch, uint32_t seq, uint16_t vf, uint16_t flags,
				int32_t ppm, uint32_t crc32, uint32_t cpu_ts, int32_t status,
				uint32_t cycles, const uint8_t *payload,
				const struct audio_asrc_state *st)
{
	uint8_t *ring = flpr_ring_mgr_test_output_ring();
	uint32_t idx;

	zassert_ok(flpr_ring_produce_begin(ring, &idx), "output produce begin");

	uint8_t *slot = flpr_ring_slot_base(ring, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);

	memset(slot, 0, FLPR_RING_SLOT_STRIDE);
	meta->epoch = epoch;
	meta->sequence = seq;
	meta->valid_frames = vf;
	meta->flags = flags;
	meta->correction_ppm = ppm;
	meta->crc32 = crc32;
	meta->cpu_timestamp = cpu_ts;
	meta->processing_cycles = cycles;
	meta->processing_status = status;
	if (st) {
		memcpy(&meta->asrc_state, st, sizeof(*st));
	}
	if (payload && vf > 0) {
		memcpy(flpr_ring_slot_payload(slot), payload, (size_t)vf * 4U);
	}

	flpr_ring_produce_commit(ring, idx);
}

/* Metadata of the most recently produced INPUT ring slot. */
static struct flpr_ring_slot_meta *last_input_slot(void)
{
	uint8_t *ring = flpr_ring_mgr_test_input_ring();

	zassert_true(flpr_ring_producer(ring) > flpr_ring_consumer(ring),
		     "at least one produced slot");
	return flpr_ring_slot_meta_ptr(flpr_ring_slot_base(ring, flpr_ring_producer(ring) - 1));
}

/* ── Initialization and status ──────────────────────────────────── */

ZTEST(flpr_ring_mgr, test_init_rejects_remote_not_ready)
{
	mock_hs_set_ready_acked(false, true);
	zassert_equal(flpr_ring_mgr_init(), -EAGAIN, "not ready → -EAGAIN");

	mock_hs_set_ready_acked(true, false);
	zassert_equal(flpr_ring_mgr_init(), -EAGAIN, "not acked → -EAGAIN");
}

ZTEST(flpr_ring_mgr, test_init_registers_and_inits_rings)
{
	mock_hs_set_ready_acked(true, true);
	zassert_ok(flpr_ring_mgr_init(), "init");

	/* Both ring headers must be valid after init. */
	zassert_true(flpr_ring_validate(flpr_ring_mgr_test_input_ring()), "input ring valid");
	zassert_true(flpr_ring_validate(flpr_ring_mgr_test_output_ring()), "output ring valid");

	/* Handlers must be captured by the mock (registered by init). */
	zassert_true(mock_hs_handlers_registered(), "ring handlers registered");
}

ZTEST(flpr_ring_mgr, test_status_before_after_init)
{
	struct flpr_ring_status st;

	flpr_ring_mgr_get_status(NULL); /* null output harmless */

	flpr_ring_mgr_get_status(&st);
	zassert_false(st.initialized, "not initialized before init");

	mock_hs_set_ready_acked(true, true);
	zassert_ok(flpr_ring_mgr_init(), "init");

	flpr_ring_mgr_get_status(&st);
	zassert_true(st.initialized, "initialized after init");
	zassert_equal(st.epoch, 0, "epoch not yet agreed");
	zassert_equal(st.in_epoch, 0, "input ring epoch 0");
	zassert_equal(st.out_epoch, 0, "output ring epoch 0");
}

ZTEST(flpr_ring_mgr, test_repeated_init_safe)
{
	mock_hs_set_ready_acked(true, true);
	zassert_ok(flpr_ring_mgr_init(), "init 1");
	zassert_ok(flpr_ring_mgr_init(), "init 2");

	zassert_true(flpr_ring_validate(flpr_ring_mgr_test_input_ring()), "input ring valid");
	zassert_true(flpr_ring_validate(flpr_ring_mgr_test_output_ring()), "output ring valid");
}

/* ── Notification and reset ──────────────────────────────────────── */

ZTEST(flpr_ring_mgr, test_notify_message_fields_and_counters)
{
	rm_init_and_reset(42);

	zassert_ok(flpr_ring_mgr_notify_producer(), "notify");

	const struct flpr_msg *sent = mock_hs_sent_at(0);
	zassert_not_null(sent, "message sent");
	zassert_equal(sent->type, FLPR_MSG_RING_PRODUCER, "type");
	zassert_equal(sent->version, FLPR_PROTOCOL_VERSION, "version");
	zassert_equal(sent->seq, 0, "seq");
	zassert_equal(sent->data, 0, "data");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.notify_sent, 1, "notify_sent");
	zassert_equal(st.notify_err, 0, "notify_err");
}

ZTEST(flpr_ring_mgr, test_notify_send_failure_counter)
{
	rm_init_and_reset(42);
	mock_hs_set_send_result(-EIO);

	zassert_equal(flpr_ring_mgr_notify_producer(), -EIO, "send failure propagates");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.notify_sent, 1, "notify_sent counted");
	zassert_equal(st.notify_err, 1, "notify_err counted");
}

ZTEST(flpr_ring_mgr, test_local_reset_rejects_zero_epoch)
{
	mock_hs_set_ready_acked(true, true);
	zassert_ok(flpr_ring_mgr_init(), "init");

	zassert_equal(flpr_ring_mgr_reset(0), -EINVAL, "zero epoch rejected");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.epoch, 0, "epoch stays invalid (0)");
}

ZTEST(flpr_ring_mgr, test_coordinated_reset_rejects_unhealthy_peer)
{
	mock_hs_set_ready_acked(false, false);
	zassert_equal(flpr_ring_mgr_init(), -EAGAIN, "init deferred when not ready");

	zassert_equal(flpr_ring_mgr_coordinated_reset(42, 100), -EAGAIN, "not ready → -EAGAIN");

	mock_hs_set_ready_acked(true, false);
	zassert_equal(flpr_ring_mgr_coordinated_reset(42, 100), -EAGAIN, "not acked → -EAGAIN");
}

ZTEST(flpr_ring_mgr, test_generated_epoch_zero_fails)
{
	mock_hs_set_ready_acked(true, true);
	zassert_ok(flpr_ring_mgr_init(), "init");

	flpr_ring_mgr_test_set_cycle(0);
	zassert_equal(flpr_ring_mgr_coordinated_reset(0, 100), -EINVAL,
		      "cycle source returning 0 → -EINVAL");
}

ZTEST(flpr_ring_mgr, test_reset_send_failure)
{
	mock_hs_set_ready_acked(true, true);
	zassert_ok(flpr_ring_mgr_init(), "init");
	mock_hs_set_send_result(-EIO);

	zassert_equal(flpr_ring_mgr_coordinated_reset(42, 100), -EIO, "send failure → -EIO");
}

ZTEST(flpr_ring_mgr, test_reset_ack_timeout)
{
	mock_hs_set_ready_acked(true, true);
	zassert_ok(flpr_ring_mgr_init(), "init");
	/* No ACK injected. */

	zassert_equal(flpr_ring_mgr_coordinated_reset(42, 50), -ETIMEDOUT, "ACK timeout");
}

ZTEST(flpr_ring_mgr, test_reset_ack_mismatch)
{
	mock_hs_set_ready_acked(true, true);
	zassert_ok(flpr_ring_mgr_init(), "init");
	mock_hs_set_auto_ack(true);
	mock_hs_set_auto_ack_data_offset(1); /* ACK echoes 43, expected 42 */

	zassert_equal(flpr_ring_mgr_coordinated_reset(42, 100), -EIO, "epoch mismatch → -EIO");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.epoch, 0, "epoch not published on mismatch");
}

ZTEST(flpr_ring_mgr, test_reset_ack_exact_match)
{
	mock_hs_set_ready_acked(true, true);
	zassert_ok(flpr_ring_mgr_init(), "init");
	mock_hs_set_auto_ack(true);

	zassert_ok(flpr_ring_mgr_coordinated_reset(42, 100), "coordinated reset");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.epoch, 42, "epoch published");
	zassert_equal(st.in_epoch, 42, "input ring epoch");
	zassert_equal(st.out_epoch, 42, "output ring epoch");
	zassert_equal(st.in_producer, 0, "input indices cleared");
	zassert_equal(st.out_producer, 0, "output indices cleared");
}

ZTEST(flpr_ring_mgr, test_old_epoch_notification_rejected)
{
	rm_init_and_reset(42);

	struct flpr_msg notify = {.type = FLPR_MSG_RING_CONSUMER,
				  .version = FLPR_PROTOCOL_VERSION,
				  .seq = 5,
				  .data = 41};
	mock_hs_invoke_consumer(&notify);

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.stale_notify, 1, "stale_notify counted");
	zassert_equal(flpr_ring_mgr_test_consume_sem_count(), 0, "no semaphore give");
}

ZTEST(flpr_ring_mgr, test_matching_epoch_notification_gives)
{
	rm_init_and_reset(42);

	struct flpr_msg notify = {.type = FLPR_MSG_RING_CONSUMER,
				  .version = FLPR_PROTOCOL_VERSION,
				  .seq = 7,
				  .data = 42};
	mock_hs_invoke_consumer(&notify);

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(flpr_ring_mgr_test_consume_sem_count(), 1, "semaphore given");
	zassert_equal(st.sem_gives, 1, "sem_gives counted");
	zassert_equal(st.test_producer_blocks, 7, "FLPR block count updated");
	zassert_equal(st.stale_notify, 0, "not stale");
}

ZTEST(flpr_ring_mgr, test_token_present_before_reset_drained)
{
	rm_init_and_reset(42);

	struct flpr_msg notify = {.type = FLPR_MSG_RING_CONSUMER,
				  .version = FLPR_PROTOCOL_VERSION,
				  .seq = 1,
				  .data = 42};
	mock_hs_invoke_consumer(&notify);
	zassert_equal(flpr_ring_mgr_test_consume_sem_count(), 1, "token present");

	zassert_ok(flpr_ring_mgr_reset(43), "reset to 43");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.sem_drained, 1, "one token drained and observable");
	zassert_equal(flpr_ring_mgr_test_consume_sem_count(), 0, "semaphore empty");
	zassert_equal(st.epoch, 43, "new epoch published");
}

ZTEST(flpr_ring_mgr, test_notification_during_epoch_zero_window_stale)
{
	rm_init_and_reset(42);
	zassert_ok(flpr_ring_mgr_remote_restarted(), "remote restart → epoch 0");

	struct flpr_msg notify = {.type = FLPR_MSG_RING_CONSUMER,
				  .version = FLPR_PROTOCOL_VERSION,
				  .seq = 3,
				  .data = 42};
	mock_hs_invoke_consumer(&notify);

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.epoch, 0, "epoch invalidated");
	zassert_equal(st.stale_notify, 1, "notification during invalidation window is stale");
	zassert_equal(flpr_ring_mgr_test_consume_sem_count(), 0, "no semaphore give");
}

/* ── Produce / consume ───────────────────────────────────────────── */

ZTEST(flpr_ring_mgr, test_produce_frames_over_max_rejected)
{
	rm_init_and_reset(42);

	enum flpr_produce_result pr =
		flpr_ring_mgr_produce_block(NULL, FLPR_RING_PAYLOAD_MAX_INPUT + 1, 0, 0, true);
	zassert_equal(pr, FLPR_PRODUCE_INVALID, "frames over input max rejected");
}

ZTEST(flpr_ring_mgr, test_produce_null_pcm_zero_filled)
{
	rm_init_and_reset(42);

	enum flpr_produce_result pr = flpr_ring_mgr_produce_block(NULL, 480, 7, -3, true);
	zassert_equal(pr, FLPR_PRODUCE_OK, "null PCM produces");

	struct flpr_ring_slot_meta *meta = last_input_slot();
	zassert_equal(meta->sequence, 7, "sequence");
	zassert_equal(meta->epoch, 42, "epoch");
	zassert_equal(meta->valid_frames, 480, "valid_frames");
	zassert_equal(meta->flags, FLPR_SLOT_FLAG_VALID, "flags");
	zassert_equal(meta->correction_ppm, -3, "ppm");

	const uint8_t *payload = flpr_ring_slot_payload((uint8_t *)meta);
	for (uint32_t i = 0; i < FLPR_RING_PAYLOAD_CAPACITY_BYTES; i++) {
		zassert_equal(payload[i], 0, "payload byte %u zero-filled", i);
	}
	zassert_equal(meta->crc32, flpr_ring_crc32(payload, 480 * 4U), "CRC over zeros");
}

ZTEST(flpr_ring_mgr, test_produce_exact_metadata_crc_remainder)
{
	rm_init_and_reset(42);

	uint8_t pcm[480 * 4U];
	for (uint32_t i = 0; i < sizeof(pcm); i++) {
		pcm[i] = (uint8_t)(i * 13U + 1U);
	}

	flpr_ring_mgr_test_set_cycle(1000);
	enum flpr_produce_result pr = flpr_ring_mgr_produce_block(pcm, 480, 99, 12, true);
	zassert_equal(pr, FLPR_PRODUCE_OK, "produce");

	struct flpr_ring_slot_meta *meta = last_input_slot();
	zassert_equal(meta->sequence, 99, "sequence");
	zassert_equal(meta->epoch, 42, "epoch");
	zassert_equal(meta->valid_frames, 480, "valid_frames");
	zassert_equal(meta->flags, FLPR_SLOT_FLAG_VALID, "flags");
	zassert_equal(meta->correction_ppm, 12, "ppm");
	zassert_equal(meta->cpu_timestamp, 1000, "cpu_timestamp");

	const uint8_t *payload = flpr_ring_slot_payload((uint8_t *)meta);
	zassert_equal(memcmp(payload, pcm, sizeof(pcm)), 0, "payload copied");
	for (uint32_t i = sizeof(pcm); i < FLPR_RING_PAYLOAD_CAPACITY_BYTES; i++) {
		zassert_equal(payload[i], 0, "remainder byte %u zero", i);
	}
	zassert_equal(meta->crc32, flpr_ring_crc32(payload, sizeof(pcm)), "CRC over valid bytes");

	/* compute_crc = false → crc32 left 0. */
	zassert_equal(flpr_ring_mgr_produce_block(pcm, 480, 100, 0, false), FLPR_PRODUCE_OK,
		      "produce without crc");
	meta = last_input_slot();
	zassert_equal(meta->crc32, 0, "crc32 zero when disabled");
}

ZTEST(flpr_ring_mgr, test_producer_stall_full_backpressure)
{
	rm_init_and_reset(42);

	flpr_ring_mgr_stall_producer(true);

	enum flpr_produce_result pr = flpr_ring_mgr_produce_block(NULL, 480, 1, 0, true);
	zassert_equal(pr, FLPR_PRODUCE_FULL, "stalled producer returns FULL");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.test_backpressure, 1, "backpressure counted");
	zassert_equal(flpr_ring_producer(flpr_ring_mgr_test_input_ring()), 0, "index not advanced");

	flpr_ring_mgr_stall_producer(false);
}

ZTEST(flpr_ring_mgr, test_physical_four_slot_full)
{
	rm_init_and_reset(42);

	for (uint32_t i = 0; i < 4; i++) {
		zassert_equal(flpr_ring_mgr_produce_block(NULL, 480, i, 0, false), FLPR_PRODUCE_OK,
			      "produce %u", i);
	}

	enum flpr_produce_result pr = flpr_ring_mgr_produce_block(NULL, 480, 4, 0, false);
	zassert_equal(pr, FLPR_PRODUCE_FULL, "5th produce on 4-slot ring is FULL");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.test_full_events, 1, "full event counted");
}

ZTEST(flpr_ring_mgr, test_index_wrap_preserves_slot_order)
{
	rm_init_and_reset(42);

	for (uint32_t i = 0; i < 4; i++) {
		zassert_equal(flpr_ring_mgr_produce_block(NULL, 480, i, 0, false), FLPR_PRODUCE_OK,
			      "produce %u", i);
	}

	/* FLPR consumes the input ring (real ring APIs, epoch check off). */
	uint8_t *in_ring = flpr_ring_mgr_test_input_ring();
	for (uint32_t i = 0; i < 4; i++) {
		uint8_t *slot_base;
		struct flpr_ring_slot_meta *meta;
		zassert_ok(flpr_ring_consume_begin(in_ring, 0, &slot_base, &meta),
			   "consume input %u", i);
		zassert_equal(meta->sequence, i, "input order %u", i);
		flpr_ring_consume_done(in_ring);
	}

	/* Produce four more: slot indices wrap to 0..3 again. */
	for (uint32_t i = 4; i < 8; i++) {
		zassert_equal(flpr_ring_mgr_produce_block(NULL, 480, i, 0, false), FLPR_PRODUCE_OK,
			      "produce %u", i);
	}

	/* Slot 0 (producer index 4) holds sequence 4. */
	struct flpr_ring_slot_meta *slot0 =
		flpr_ring_slot_meta_ptr(flpr_ring_slot_base(flpr_ring_mgr_test_input_ring(), 0));
	zassert_equal(slot0->sequence, 4, "wrapped slot 0 holds sequence 4");

	/* Wrap again: first four slots refilled in order. */
	for (uint32_t i = 0; i < 4; i++) {
		uint8_t *slot_base;
		struct flpr_ring_slot_meta *meta;
		zassert_ok(flpr_ring_consume_begin(in_ring, 0, &slot_base, &meta),
			   "consume input (wrap) %u", i);
		zassert_equal(meta->sequence, i + 4, "wrapped input order %u", i);
		flpr_ring_consume_done(in_ring);
	}
}

ZTEST(flpr_ring_mgr, test_consume_empty)
{
	rm_init_and_reset(42);

	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL), FLPR_CONSUME_EMPTY,
		      "empty output ring");
}

ZTEST(flpr_ring_mgr, test_stale_slot_advances)
{
	rm_init_and_reset(42);

	/* Stale slot with wrong epoch in the OUTPUT ring. */
	zassert_ok(flpr_ring_mgr_produce_stale_test(7), "produce stale slot");

	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL), FLPR_CONSUME_STALE,
		      "stale slot reported");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.test_stale_events, 1, "stale event counted");

	/* Consumer advanced past the stale slot. */
	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL), FLPR_CONSUME_EMPTY,
		      "advanced past stale slot");
}

ZTEST(flpr_ring_mgr, test_consume_invalid_frame_count)
{
	rm_init_and_reset(42);

	arrange_output_slot(42, 1, FLPR_RING_PAYLOAD_CAPACITY_FRAMES + 1, FLPR_SLOT_FLAG_VALID, 0,
			    0, 0, 0, 0, NULL, NULL);

	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL),
		      FLPR_CONSUME_INVALID, "invalid frame count rejected");

	/* Slot consumed (rejected safely, no stuck state). */
	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL), FLPR_CONSUME_EMPTY,
		      "invalid slot consumed");
}

ZTEST(flpr_ring_mgr, test_consume_valid_fills_outputs)
{
	rm_init_and_reset(42);
	flpr_ring_mgr_test_set_test_active(true);

	uint8_t pcm[100 * 4U];
	for (uint32_t i = 0; i < sizeof(pcm); i++) {
		pcm[i] = (uint8_t)(i * 5U + 2U);
	}
	uint32_t crc = flpr_ring_crc32(pcm, sizeof(pcm));
	arrange_output_slot(42, 77, 100, FLPR_SLOT_FLAG_VALID, -9, crc, 900, 0, 0, pcm, NULL);

	flpr_ring_mgr_test_set_cycle(1000);

	uint8_t out[sizeof(pcm)];
	uint16_t vf = 0;
	uint32_t seq_out = 0;
	uint32_t crc_out = 0;
	uint32_t latency_out = 0;
	zassert_equal(flpr_ring_mgr_consume_block(out, &vf, &seq_out, &crc_out, &latency_out),
		      FLPR_CONSUME_OK, "consume");
	zassert_equal(vf, 100, "valid_frames");
	zassert_equal(seq_out, 77, "sequence");
	zassert_equal(crc_out, crc, "crc");
	zassert_equal(latency_out, 100, "latency = now - cpu_timestamp");
	zassert_equal(memcmp(out, pcm, sizeof(pcm)), 0, "payload");
}

ZTEST(flpr_ring_mgr, test_consume_null_outputs_safe)
{
	rm_init_and_reset(42);

	arrange_output_slot(42, 3, 10, FLPR_SLOT_FLAG_VALID, 0, 0, 0, 0, 0, NULL, NULL);

	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL), FLPR_CONSUME_OK,
		      "all-null outputs safe");
}

ZTEST(flpr_ring_mgr, test_latency_wrap_unsigned)
{
	rm_init_and_reset(42);
	flpr_ring_mgr_test_set_test_active(true);

	/* cpu_timestamp = now + 0xFFFF0000 (mod 2^32): unsigned subtraction
	 * yields exactly 0x10000 regardless of the current cycle value. */
	flpr_ring_mgr_test_set_cycle(1000);
	arrange_output_slot(42, 1, 10, FLPR_SLOT_FLAG_VALID, 0, 0, 1000U + 0xFFFF0000U, 0, 0, NULL,
			    NULL);

	uint32_t latency_out = 0;
	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, &latency_out),
		      FLPR_CONSUME_OK, "consume");
	zassert_equal(latency_out, 0x10000U, "wrapped unsigned latency");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.latency_count, 1, "metrics updated in test mode");
	zassert_equal(st.latency_min, 0x10000U, "latency_min");
	zassert_equal(st.latency_max, 0x10000U, "latency_max");
	zassert_equal(st.latency_sum, 0x10000U, "latency_sum");
}

ZTEST(flpr_ring_mgr, test_latency_metrics_only_in_test_mode)
{
	rm_init_and_reset(42);
	/* test_active OFF. */

	flpr_ring_mgr_test_set_cycle(1000);
	arrange_output_slot(42, 1, 10, FLPR_SLOT_FLAG_VALID, 0, 0, 900, 0, 0, NULL, NULL);

	uint32_t latency_out = 0;
	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, &latency_out),
		      FLPR_CONSUME_OK, "consume");
	zassert_equal(latency_out, 100, "latency still reported");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.latency_count, 0, "metrics NOT updated outside test mode");
}

ZTEST(flpr_ring_mgr, test_crc_error_accounting)
{
	rm_init_and_reset(42);
	flpr_ring_mgr_test_set_test_active(true);

	/* Payload matches the deterministic pattern for seq 5, but the
	 * stored CRC is wrong → CRC error counted, payload verification OK. */
	uint8_t pcm[50 * 4U];
	flpr_ring_gen_payload(pcm, sizeof(pcm), 5);
	arrange_output_slot(42, 5, 50, FLPR_SLOT_FLAG_VALID, 0, 0xDEADBEEFU, 0, 0, 0, pcm, NULL);

	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL), FLPR_CONSUME_OK,
		      "consume (CRC mismatch counted, not fatal)");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.test_crc_errors, 1, "CRC error counted");
	zassert_equal(st.test_payload_errors, 0, "payload still matches pattern");
}

ZTEST(flpr_ring_mgr, test_payload_mismatch_accounting)
{
	rm_init_and_reset(42);
	flpr_ring_mgr_test_set_test_active(true);

	uint8_t pcm[100 * 4U];
	flpr_ring_gen_payload(pcm, sizeof(pcm), 9);
	pcm[0] ^= 0xFFU; /* corrupt one byte → one frame error */
	uint32_t crc = flpr_ring_crc32(pcm, sizeof(pcm));
	arrange_output_slot(42, 9, 100, FLPR_SLOT_FLAG_VALID, 0, crc, 0, 0, 0, pcm, NULL);

	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL), FLPR_CONSUME_OK,
		      "consume");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.test_payload_errors, 1, "payload mismatch counted per frame");
	zassert_equal(st.test_crc_errors, 0, "CRC still valid (corrupted after crc)");
}

ZTEST(flpr_ring_mgr, test_set_consume_cb_noop)
{
	/* Documented no-op (refactor-review candidate); must be safe. */
	flpr_ring_mgr_set_consume_cb(NULL, NULL);
	flpr_ring_mgr_set_consume_cb((flpr_ring_consume_cb_t)1, (void *)0x1);
}

/* ── Typed ASRC ──────────────────────────────────────────────────── */

ZTEST(flpr_ring_mgr, test_produce_asrc_invalid_args)
{
	rm_init_and_reset(42);

	struct audio_asrc_state st;
	memset(&st, 0, sizeof(st));

	zassert_equal(flpr_ring_mgr_produce_asrc(NULL, FLPR_RING_PAYLOAD_MAX_INPUT - 1, 1, 0, &st),
		      FLPR_PRODUCE_INVALID, "wrong frame count rejected");
	zassert_equal(flpr_ring_mgr_produce_asrc(NULL, FLPR_RING_PAYLOAD_MAX_INPUT, 1, 0, NULL),
		      FLPR_PRODUCE_INVALID, "null state rejected");
}

ZTEST(flpr_ring_mgr, test_produce_asrc_metadata_prestate_crc)
{
	rm_init_and_reset(42);

	int16_t pcm[480 * 2U];
	for (uint32_t i = 0; i < sizeof(pcm) / sizeof(pcm[0]); i++) {
		pcm[i] = (int16_t)(i * 3U - 100);
	}

	struct audio_asrc_state pre;
	memset(&pre, 0, sizeof(pre));
	pre.phase = 0x12345678ULL;
	pre.step_base = 0x100000000ULL;
	pre.prev_l = -100;
	pre.prev_r = 200;
	pre.prev_valid = 1;

	zassert_equal(flpr_ring_mgr_produce_asrc(pcm, 480, 11, -7, &pre), FLPR_PRODUCE_OK,
		      "produce_asrc");

	struct flpr_ring_slot_meta *meta = last_input_slot();
	zassert_equal(meta->flags, FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR, "flags");
	zassert_equal(meta->sequence, 11, "sequence");
	zassert_equal(meta->epoch, 42, "epoch");
	zassert_equal(meta->correction_ppm, -7, "ppm");
	zassert_equal(memcmp(&meta->asrc_state, &pre, sizeof(pre)), 0, "pre-state copied");

	const uint8_t *payload = flpr_ring_slot_payload((uint8_t *)meta);
	zassert_equal(memcmp(payload, pcm, sizeof(pcm)), 0, "payload copied");
	zassert_equal(meta->crc32, flpr_ring_crc32(payload, 480 * 4U), "CRC always computed");
}

ZTEST(flpr_ring_mgr, test_consume_asrc_empty_and_stale)
{
	rm_init_and_reset(42);

	int16_t out[FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2U];
	struct flpr_consume_asrc_result res;

	zassert_equal(
		flpr_ring_mgr_consume_asrc_result(out, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, &res),
		FLPR_CONSUME_EMPTY, "empty");

	zassert_ok(flpr_ring_mgr_produce_stale_test(7), "stale slot");
	zassert_equal(
		flpr_ring_mgr_consume_asrc_result(out, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, &res),
		FLPR_CONSUME_STALE, "stale");
}

ZTEST(flpr_ring_mgr, test_consume_asrc_bad_status)
{
	rm_init_and_reset(42);

	/* vf == 0 with non-negative processing_status is invalid. */
	arrange_output_slot(42, 1, 0, FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR, 0, 0, 0, 0,
			    0, NULL, NULL);

	int16_t out[FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2U];
	struct flpr_consume_asrc_result res;
	zassert_equal(
		flpr_ring_mgr_consume_asrc_result(out, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, &res),
		FLPR_CONSUME_INVALID, "vf=0 with status>=0 invalid");
	zassert_equal(res.output_frames, 0, "output_frames zeroed");
}

ZTEST(flpr_ring_mgr, test_consume_asrc_error_output_ok)
{
	rm_init_and_reset(42);

	/* FLPR error output: vf == 0, processing_status < 0 → valid response. */
	arrange_output_slot(42, 2, 0, FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR, 0, 0, 0,
			    -5, 0, NULL, NULL);

	int16_t out[FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2U];
	struct flpr_consume_asrc_result res;
	zassert_equal(
		flpr_ring_mgr_consume_asrc_result(out, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, &res),
		FLPR_CONSUME_OK, "error output is a valid transport response");
	zassert_equal(res.output_frames, 0, "output_frames 0");
	zassert_equal(res.processing_status, -5, "status echoed");
}

ZTEST(flpr_ring_mgr, test_consume_asrc_bad_flags)
{
	rm_init_and_reset(42);

	/* Missing ASRC_LINEAR flag. */
	arrange_output_slot(42, 1, 100, FLPR_SLOT_FLAG_VALID, 0, 0, 0, 0, 0, NULL, NULL);

	int16_t out[FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2U];
	memset(out, 0x5A, sizeof(out));
	struct flpr_consume_asrc_result res;
	zassert_equal(
		flpr_ring_mgr_consume_asrc_result(out, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, &res),
		FLPR_CONSUME_INVALID, "bad flags rejected");
	zassert_equal(res.output_frames, 0, "output_frames zeroed");
}

ZTEST(flpr_ring_mgr, test_consume_asrc_state_corruption)
{
	rm_init_and_reset(42);

	struct audio_asrc_state bad;
	memset(&bad, 0, sizeof(bad));
	bad.reserved[0] = 1; /* reserved bytes must be zero */

	int16_t out[FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2U];
	struct flpr_consume_asrc_result res;
	arrange_output_slot(42, 1, 100, FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR, 0, 0, 0,
			    0, 0, NULL, &bad);
	zassert_equal(
		flpr_ring_mgr_consume_asrc_result(out, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, &res),
		FLPR_CONSUME_INVALID, "corrupt state rejected");
	zassert_equal(res.output_frames, 0, "output_frames zeroed");
}

ZTEST(flpr_ring_mgr, test_consume_asrc_bad_frame_range)
{
	rm_init_and_reset(42);

	arrange_output_slot(42, 1, FLPR_RING_PAYLOAD_CAPACITY_FRAMES + 1,
			    FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR, 0, 0, 0, 0, 0, NULL,
			    NULL);

	int16_t out[FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2U];
	struct flpr_consume_asrc_result res;
	zassert_equal(
		flpr_ring_mgr_consume_asrc_result(out, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, &res),
		FLPR_CONSUME_INVALID, "frame range rejected");
	zassert_equal(res.output_frames, 0, "output_frames zeroed");
}

ZTEST(flpr_ring_mgr, test_consume_asrc_bad_crc)
{
	rm_init_and_reset(42);

	uint8_t pcm[100 * 4U];
	memset(pcm, 0x11, sizeof(pcm));
	arrange_output_slot(42, 1, 100, FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR, 0,
			    0xDEADBEEFU, 0, 0, 0, pcm, NULL);

	int16_t out[FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2U];
	struct flpr_consume_asrc_result res;
	zassert_equal(
		flpr_ring_mgr_consume_asrc_result(out, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, &res),
		FLPR_CONSUME_INVALID, "bad CRC rejected");
	zassert_equal(res.output_frames, 0, "output_frames zeroed");
	zassert_equal(res.sequence, 0, "result fields untouched");
}

ZTEST(flpr_ring_mgr, test_consume_asrc_transactional_output)
{
	rm_init_and_reset(42);

	/* Bad flags: fails before any output write. */
	arrange_output_slot(42, 1, 100, FLPR_SLOT_FLAG_VALID, 0, 0, 0, 0, 0, NULL, NULL);

	int16_t out[FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2U];
	memset(out, 0x5A, sizeof(out));
	struct flpr_consume_asrc_result res;
	zassert_equal(
		flpr_ring_mgr_consume_asrc_result(out, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, &res),
		FLPR_CONSUME_INVALID, "rejected");
	zassert_equal(res.output_frames, 0, "output_frames zeroed");
	for (uint32_t i = 0; i < sizeof(out) / sizeof(out[0]); i++) {
		zassert_equal((uint16_t)out[i], 0x5A5A, "pcm_out untouched on pre-copy failure");
	}
}

ZTEST(flpr_ring_mgr, test_consume_asrc_success)
{
	rm_init_and_reset(42);

	uint8_t pcm[100 * 4U];
	for (uint32_t i = 0; i < sizeof(pcm); i++) {
		pcm[i] = (uint8_t)(i * 7U + 3U);
	}
	uint32_t crc = flpr_ring_crc32(pcm, sizeof(pcm));

	struct audio_asrc_state post;
	memset(&post, 0, sizeof(post));
	post.phase = 0x0F0F0F0FULL;
	post.step_base = 0x100000000ULL;
	post.prev_l = 5;
	post.prev_r = -6;
	post.prev_valid = 1;

	arrange_output_slot(42, 66, 100, FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR, -4, crc,
			    1800, 0, 4321, pcm, &post);

	flpr_ring_mgr_test_set_cycle(2000);

	int16_t out[FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2U];
	struct flpr_consume_asrc_result res;
	zassert_equal(
		flpr_ring_mgr_consume_asrc_result(out, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, &res),
		FLPR_CONSUME_OK, "consume_asrc success");
	zassert_equal(res.output_frames, 100, "output_frames");
	zassert_equal(res.sequence, 66, "sequence");
	zassert_equal(res.flags, FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR, "flags");
	zassert_equal(res.correction_ppm, -4, "ppm");
	zassert_equal(res.payload_crc, flpr_ring_crc32(pcm, sizeof(pcm)), "payload_crc recomputed");
	zassert_equal(memcmp(&res.post_state, &post, sizeof(post)), 0, "post_state");
	zassert_equal(res.processing_cycles, 4321, "processing_cycles");
	zassert_equal(res.processing_status, 0, "processing_status");
	zassert_equal(res.rtt_cycles, 200, "rtt_cycles = now - cpu_timestamp");
	zassert_equal(memcmp(out, pcm, sizeof(pcm)), 0, "payload");
}

/* ── Stall / report / restart ────────────────────────────────────── */

ZTEST(flpr_ring_mgr, test_stall_persistent_packing)
{
	rm_init_and_reset(42);
	mock_hs_set_auto_ack(true);

	zassert_ok(flpr_ring_mgr_flpr_stall(0x01, 100), "persistent stall");

	const struct flpr_msg *sent = mock_hs_sent_at(0);
	zassert_not_null(sent, "message sent");
	zassert_equal(sent->type, FLPR_MSG_RING_STALL, "type");
	zassert_equal(sent->data, FLPR_STALL_PACK(0x01, 0), "packed persistent value");
}

ZTEST(flpr_ring_mgr, test_stall_timed_packing)
{
	rm_init_and_reset(42);
	mock_hs_set_auto_ack(true);

	zassert_ok(flpr_ring_mgr_flpr_stall_timed(0x03, 5000, 100), "timed stall");

	const struct flpr_msg *sent = mock_hs_sent_at(0);
	zassert_not_null(sent, "message sent");
	zassert_equal(sent->type, FLPR_MSG_RING_STALL, "type");
	zassert_equal(sent->data, FLPR_STALL_PACK(0x03, 5000), "packed timed value");
}

ZTEST(flpr_ring_mgr, test_stall_timed_zero_mask_rejected)
{
	rm_init_and_reset(42);
	mock_hs_set_auto_ack(true);

	zassert_equal(flpr_ring_mgr_flpr_stall_timed(0, 100, 100), -EINVAL,
		      "zero mask with duration rejected");
	zassert_equal(flpr_ring_mgr_flpr_stall_timed(0x01, FLPR_STALL_DURATION_MAX + 1, 100),
		      -EINVAL, "duration overflow rejected");

	/* Zero mask + zero duration is the persistent clear — allowed. */
	zassert_ok(flpr_ring_mgr_flpr_stall_timed(0, 0, 100), "persistent clear");
	const struct flpr_msg *sent = mock_hs_sent_at(0);
	zassert_not_null(sent, "message sent");
	zassert_equal(sent->data, 0U, "clear value packed");
}

ZTEST(flpr_ring_mgr, test_stall_send_failure)
{
	rm_init_and_reset(42);
	mock_hs_set_send_result(-EIO);

	zassert_equal(flpr_ring_mgr_flpr_stall(0x01, 100), -EIO, "send failure propagates");
}

ZTEST(flpr_ring_mgr, test_stall_ack_timeout)
{
	rm_init_and_reset(42);
	/* No auto-ack, no manual ACK. */

	zassert_equal(flpr_ring_mgr_flpr_stall(0x01, 50), -ETIMEDOUT, "ACK timeout");
}

ZTEST(flpr_ring_mgr, test_stall_wrong_ack)
{
	rm_init_and_reset(42);
	mock_hs_set_auto_ack(true);
	mock_hs_set_auto_ack_data_offset(1);

	zassert_equal(flpr_ring_mgr_flpr_stall(0x01, 100), -EIO, "wrong ACK echo rejected");
}

ZTEST(flpr_ring_mgr, test_stall_exact_ack)
{
	rm_init_and_reset(42);
	mock_hs_set_auto_ack(true);

	zassert_ok(flpr_ring_mgr_flpr_stall_timed(0x05, 12345, 100), "stall with exact ACK");
	zassert_equal(flpr_ring_mgr_flpr_stall_acked(), FLPR_STALL_PACK(0x05, 12345),
		      "acked value observable");
}

ZTEST(flpr_ring_mgr, test_report_subtypes_update_status)
{
	rm_init_and_reset(42);

	struct flpr_msg report = {.type = FLPR_MSG_RING_TEST_REPORT,
				  .version = FLPR_PROTOCOL_VERSION};
	struct flpr_ring_status st;

	/* Subtype 0x00: seq lo 16 = crc errors (not exported), data = block count. */
	report.seq = 0x0003;
	report.data = 100;
	mock_hs_invoke_report(&report);
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.test_producer_blocks, 100, "blocks");

	/* Subtype 0xD1: consume_ok. */
	report.seq = 0xD100;
	report.data = 55;
	mock_hs_invoke_report(&report);
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.flpr_consume_ok, 55, "consume_ok");

	/* Subtype 0xD2: produce_ok. */
	report.seq = 0xD200;
	report.data = 44;
	mock_hs_invoke_report(&report);
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.flpr_produce_ok, 44, "produce_ok");

	/* Subtype 0xD3: notify_rcv (seq&0xFF), worker_wake (data lo), produce_full (data hi). */
	report.seq = 0xD301;
	report.data = (33U << 16) | 22U;
	mock_hs_invoke_report(&report);
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.flpr_notify_rcv, 1, "notify_rcv");
	zassert_equal(st.flpr_worker_wake, 22, "worker_wake");
	zassert_equal(st.flpr_produce_full, 33, "produce_full");

	/* Subtype 0xD4: consume_empty (lo), consume_stale (hi). */
	report.seq = 0xD400;
	report.data = (9U << 16) | 8U;
	mock_hs_invoke_report(&report);
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.flpr_consume_empty, 8, "consume_empty");
	zassert_equal(st.flpr_consume_stale, 9, "consume_stale");

	/* Unknown subtype: no field updates. */
	report.seq = 0x99FF;
	report.data = 0xDEADBEEF;
	mock_hs_invoke_report(&report);
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.flpr_consume_ok, 55, "unknown subtype ignored");
}

ZTEST(flpr_ring_mgr, test_remote_restarted_invalidates_and_requires_reset)
{
	rm_init_and_reset(42);

	/* Live traffic first. */
	zassert_equal(flpr_ring_mgr_produce_block(NULL, 480, 1, 0, false), FLPR_PRODUCE_OK,
		      "produce");
	struct flpr_msg notify = {.type = FLPR_MSG_RING_CONSUMER,
				  .version = FLPR_PROTOCOL_VERSION,
				  .seq = 1,
				  .data = 42};
	mock_hs_invoke_consumer(&notify);
	zassert_equal(flpr_ring_mgr_test_consume_sem_count(), 1, "token present");

	zassert_ok(flpr_ring_mgr_remote_restarted(), "remote restart");

	struct flpr_ring_status st;
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.epoch, 0, "epoch invalidated");
	zassert_equal(flpr_ring_mgr_test_consume_sem_count(), 0, "consume sem drained");
	zassert_equal(flpr_ring_mgr_test_reset_ack_sem_count(), 0, "reset ack sem drained");
	zassert_equal(flpr_ring_mgr_test_stall_ack_sem_count(), 0, "stall ack sem drained");
	zassert_true(flpr_ring_validate(flpr_ring_mgr_test_input_ring()), "input ring re-inited");
	zassert_true(flpr_ring_validate(flpr_ring_mgr_test_output_ring()), "output ring re-inited");
	zassert_equal(flpr_ring_producer(flpr_ring_mgr_test_input_ring()), 0,
		      "input indices cleared");
	zassert_equal(st.test_blocks_sent, 0, "test counters reset");

	/* Subsequent coordinated reset required and works. */
	mock_hs_set_auto_ack(true);
	zassert_ok(flpr_ring_mgr_coordinated_reset(43, 100), "coordinated reset after restart");
	flpr_ring_mgr_get_status(&st);
	zassert_equal(st.epoch, 43, "epoch re-established");
}

ZTEST_SUITE(flpr_ring_mgr, NULL, NULL, rm_setup, NULL, NULL);
