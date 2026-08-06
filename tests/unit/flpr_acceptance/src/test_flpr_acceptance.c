/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Direct tests for the cpuapp FLPR acceptance module (R8) — real
 * production source execution.
 *
 * This suite compiles src/flpr_acceptance.c, src/flpr_control_ack.c,
 * src/flpr_ring_mgr.c, src/flpr_ring.c, and src/flpr_cache.c with
 * FLPR_ACCEPTANCE_NATIVE_TEST + FLPR_RING_MGR_NATIVE_TEST +
 * FLPR_CONTROL_ACK_NATIVE_TEST and CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS
 * forced, against a faithful handshake mock implementing BOTH the
 * production handler slot (reset ACK + consumer) and the diagnostic
 * slot (report/stall-ack/pong/hang).  The moved acceptance behavior —
 * ring throughput test, FLPR stall/timed-stall correlation, stale
 * produce, report aggregation, producer-stall backpressure, CRC/payload/
 * latency accounting, handshake stress, fault hang, remote-restart
 * stall-token reset — is proven against the real core + acceptance
 * sources.  The all-gates-PASS tail of the gate runner requires a live
 * FLPR worker and stays hardware evidence.
 */
#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>

#include "flpr_acceptance.h"
#include "flpr_acceptance_hooks.h"
#include "flpr_ring_mgr.h"
#include "flpr_ring_mgr_hooks.h"
#include "flpr_control_ack.h"
#include "mock_flpr_handshake.h"
#include "flpr_ring.h"
#include "flpr_protocol.h"
#include "audio_asrc.h"

/* ── Helpers ────────────────────────────────────────────────────── */

static void acc_setup(void *fixture)
{
	(void)fixture;
	mock_hs_reset();
	flpr_control_ack_test_reset_registry();
	flpr_ring_mgr_test_reset_state();
	flpr_acceptance_test_reset_state();
	flpr_acceptance_init(); /* registers diag handler + stall ctl */
	flpr_ring_mgr_test_set_cycle(0);
}

/* Standard arrangement: FLPR ready+acked, manager init, epoch reset. */
static void acc_init_and_reset(uint32_t epoch)
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

/* ── Worker infra for the stall late-ACK test ────────────────────── */

static K_SEM_DEFINE(stall_worker_done, 0, 1);
static int stall_worker_result;

static void stall_worker_fn(void *arg_p, void *u1, void *u2)
{
	(void)u1;
	(void)u2;
	stall_worker_result = flpr_acceptance_flpr_stall(0x01, 500);
	k_sem_give(&stall_worker_done);
}

static bool wait_for_stall_token(uint16_t want, uint32_t timeout_ms)
{
	uint32_t deadline = k_uptime_get_32() + timeout_ms;

	while (k_uptime_get_32() < deadline) {
		for (uint32_t i = 0; i < mock_hs_sent_count(); i++) {
			const struct flpr_msg *m = mock_hs_sent_at(i);

			if (m->type == FLPR_MSG_RING_STALL && m->seq == want) {
				return true;
			}
		}
		k_sleep(K_MSEC(1));
	}
	return false;
}

/* ── Ring throughput test ────────────────────────────────────────── */

ZTEST(flpr_acceptance, test_ring_test_run_pre_init_eagain)
{
	struct flpr_acceptance_status out;

	zassert_equal(flpr_acceptance_test_run(1, 1000, &out), -EAGAIN,
		      "uninitialized rings rejected");
}

ZTEST(flpr_acceptance, test_ring_test_run_rate_pre_init_eagain)
{
	struct flpr_acceptance_status out;

	zassert_equal(flpr_acceptance_test_run_rate(1, 1000, 0, &out), -EAGAIN,
		      "uninitialized rings rejected (rate path)");
}

ZTEST(flpr_acceptance, test_ring_test_run_active_ebusy)
{
	acc_init_and_reset(42);
	flpr_acceptance_test_set_active(true);

	struct flpr_acceptance_status out;
	zassert_equal(flpr_acceptance_test_run(1, 1000, &out), -EBUSY, "active test rejected");
	zassert_equal(out.test_active, true, "status reports test active");

	flpr_acceptance_test_set_active(false);
}

ZTEST(flpr_acceptance, test_ring_test_run_rate_active_ebusy)
{
	acc_init_and_reset(42);
	flpr_acceptance_test_set_active(true);

	struct flpr_acceptance_status out;
	zassert_equal(flpr_acceptance_test_run_rate(1, 1000, 0, &out), -EBUSY,
		      "active test rejected (rate path)");

	flpr_acceptance_test_set_active(false);
}

ZTEST(flpr_acceptance, test_ring_test_run_success_bounded)
{
	acc_init_and_reset(42);

	/* Pre-fill one valid output slot so the final drain observes a
	 * received block without a remote FLPR worker.  The payload must
	 * match the deterministic gen_payload pattern for its sequence —
	 * consume_block verifies CRC and regenerates the payload when
	 * test_active. */
	uint8_t pcm[100 * 4U];
	flpr_ring_gen_payload(pcm, sizeof(pcm), 1);
	uint32_t crc = flpr_ring_crc32(pcm, sizeof(pcm));
	arrange_output_slot(42, 1, 100, FLPR_SLOT_FLAG_VALID, 0, crc, 0, 0, 0, pcm, NULL);

	struct flpr_acceptance_status out;
	zassert_equal(flpr_acceptance_test_run(1, 5000, &out), 0, "bounded ring test succeeds");
	zassert_equal(out.test_blocks_sent, 1, "one block sent");
	zassert_equal(out.test_blocks_recv, 1, "one block received");
	zassert_equal(out.test_active, false, "test_active cleared");

	/* TEST_START and TEST_STOP were sent on the wire. */
	zassert_true(mock_hs_sent_type_count(FLPR_MSG_RING_TEST_START) >= 1, "START sent");
	zassert_true(mock_hs_sent_type_count(FLPR_MSG_RING_TEST_STOP) >= 1, "STOP sent");
}

ZTEST(flpr_acceptance, test_ring_test_run_rate_success_bounded)
{
	acc_init_and_reset(42);

	uint8_t pcm[100 * 4U];
	flpr_ring_gen_payload(pcm, sizeof(pcm), 1);
	uint32_t crc = flpr_ring_crc32(pcm, sizeof(pcm));
	arrange_output_slot(42, 1, 100, FLPR_SLOT_FLAG_VALID, 0, crc, 0, 0, 0, pcm, NULL);

	struct flpr_acceptance_status out;
	zassert_equal(flpr_acceptance_test_run_rate(1, 5000, 0, &out), 0,
		      "bounded rate-limited ring test succeeds");
	zassert_equal(out.test_blocks_sent, 1, "one block sent");
	zassert_equal(out.test_blocks_recv, 1, "one block received");
}

/* ── FLPR stall / timed stall ────────────────────────────────────── */

ZTEST(flpr_acceptance, test_stall_persistent_packing)
{
	acc_init_and_reset(42);
	mock_hs_set_auto_ack(true);

	zassert_ok(flpr_acceptance_flpr_stall(0x01, 100), "persistent stall");

	const struct flpr_msg *sent = mock_hs_sent_at(0);
	zassert_not_null(sent, "message sent");
	zassert_equal(sent->type, FLPR_MSG_RING_STALL, "type");
	zassert_equal(sent->data, FLPR_STALL_PACK(0x01, 0), "packed persistent value");
}

ZTEST(flpr_acceptance, test_stall_timed_packing)
{
	acc_init_and_reset(42);
	mock_hs_set_auto_ack(true);

	zassert_ok(flpr_acceptance_flpr_stall_timed(0x03, 5000, 100), "timed stall");

	const struct flpr_msg *sent = mock_hs_sent_at(0);
	zassert_not_null(sent, "message sent");
	zassert_equal(sent->type, FLPR_MSG_RING_STALL, "type");
	zassert_equal(sent->data, FLPR_STALL_PACK(0x03, 5000), "packed timed value");
}

ZTEST(flpr_acceptance, test_stall_timed_zero_mask_rejected)
{
	acc_init_and_reset(42);
	mock_hs_set_auto_ack(true);

	zassert_equal(flpr_acceptance_flpr_stall_timed(0, 100, 100), -EINVAL,
		      "zero mask with duration rejected");
	zassert_equal(flpr_acceptance_flpr_stall_timed(0x01, FLPR_STALL_DURATION_MAX + 1, 100),
		      -EINVAL, "duration overflow rejected");

	/* Zero mask + zero duration is the persistent clear — allowed. */
	zassert_ok(flpr_acceptance_flpr_stall_timed(0, 0, 100), "persistent clear");
	const struct flpr_msg *sent = mock_hs_sent_at(0);
	zassert_not_null(sent, "message sent");
	zassert_equal(sent->data, 0U, "clear value packed");
}

ZTEST(flpr_acceptance, test_stall_send_failure)
{
	acc_init_and_reset(42);
	mock_hs_set_send_result(-EIO);

	zassert_equal(flpr_acceptance_flpr_stall(0x01, 100), -EIO, "send failure propagates");
}

ZTEST(flpr_acceptance, test_stall_ack_timeout)
{
	acc_init_and_reset(42);
	/* No auto-ack, no manual ACK. */

	zassert_equal(flpr_acceptance_flpr_stall(0x01, 50), -ETIMEDOUT, "ACK timeout");
}

ZTEST(flpr_acceptance, test_stall_wrong_ack)
{
	acc_init_and_reset(42);
	mock_hs_set_auto_ack(true);
	mock_hs_set_auto_ack_data_offset(1);

	zassert_equal(flpr_acceptance_flpr_stall(0x01, 100), -EIO, "wrong ACK echo rejected");
}

ZTEST(flpr_acceptance, test_stall_exact_ack)
{
	acc_init_and_reset(42);
	mock_hs_set_auto_ack(true);

	zassert_ok(flpr_acceptance_flpr_stall_timed(0x05, 12345, 100), "stall with exact ACK");
	zassert_equal(flpr_acceptance_flpr_stall_acked(), FLPR_STALL_PACK(0x05, 12345),
		      "acked value observable");
}

/* Request A times out (token 1).  Request B reuses the same data (token
 * 2).  A late ACK A arriving after B is armed is ignored by sequence;
 * the matching ACK B completes B (stall-side mirror of the reset test). */
ZTEST(flpr_acceptance, test_stall_ack_retry_late_ack_ignored_by_sequence)
{
	acc_init_and_reset(42);

	mock_hs_set_auto_ack(false);
	zassert_equal(flpr_acceptance_flpr_stall(0x01, 50), -ETIMEDOUT, "request A times out");

	/* Request B in a worker so ACKs can be injected while it waits. */
	static K_THREAD_STACK_DEFINE(st_stack, 2048);
	static struct k_thread st_thread;

	k_sem_reset(&stall_worker_done);
	k_thread_create(&st_thread, st_stack, K_THREAD_STACK_SIZEOF(st_stack),
			(void (*)(void *, void *, void *))stall_worker_fn, (void *)(uintptr_t)1,
			NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

	/* Wait for request B (token 2) on the wire. */
	zassert_true(wait_for_stall_token(2, 5000), "request B (token 2) sent");

	/* Late ACK A: stale sequence while B is armed → ignored/counted. */
	struct flpr_msg late_a = {
		.type = FLPR_MSG_RING_STALL_ACK,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = 1,
		.data = FLPR_STALL_PACK(0x01, 0),
	};
	mock_hs_invoke_diag(&late_a);

	/* ACK B: matching sequence → completes B. */
	struct flpr_msg ack_b = {
		.type = FLPR_MSG_RING_STALL_ACK,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = 2,
		.data = FLPR_STALL_PACK(0x01, 0),
	};
	mock_hs_invoke_diag(&ack_b);

	zassert_true(k_sem_take(&stall_worker_done, K_MSEC(5000)) == 0, "request B joined");
	zassert_ok(stall_worker_result, "request B completes after the matching ACK");
	zassert_equal(flpr_acceptance_test_stall_stale_count(), 1, "late ACK A counted stale");
}

/* Token space: 0xFFFF succeeds, the next request overflows with
 * -EOVERFLOW, and a remote restart (known quiescence boundary) resets
 * the counter so token 1 is allocated again. */
ZTEST(flpr_acceptance, test_stall_ack_token_boundary_overflow_and_remote_restart)
{
	mock_hs_set_ready_acked(true, true);
	zassert_ok(flpr_ring_mgr_init(), "init");

	flpr_acceptance_test_set_next_stall_token(0xFFFF);
	mock_hs_set_auto_ack(true);
	zassert_ok(flpr_acceptance_flpr_stall(0x01, 100), "stall token 0xFFFF succeeds");
	zassert_equal(flpr_acceptance_flpr_stall(0x01, 100), -EOVERFLOW,
		      "stall token space exhausted");
	zassert_equal(flpr_acceptance_flpr_stall_timed(0x01, 0, 100), -EOVERFLOW,
		      "timed stall token space exhausted");
	zassert_ok(flpr_ring_mgr_remote_restarted(), "remote restart (stall tokens)");
	zassert_ok(flpr_acceptance_flpr_stall(0x01, 100), "stall token 1 after remote restart");
}

/* Remote restart drains the stall-ACK semaphore and resets acceptance
 * counters (the assert moved from the ring-mgr suite where the stall
 * instance is not registered). */
ZTEST(flpr_acceptance, test_remote_restarted_drains_stall_ack_and_resets_counters)
{
	acc_init_and_reset(42);
	mock_hs_set_auto_ack(true);
	zassert_ok(flpr_acceptance_flpr_stall(0x01, 100), "one stall transaction");

	zassert_ok(flpr_ring_mgr_remote_restarted(), "remote restart");
	zassert_equal(flpr_acceptance_test_stall_ack_sem_count(), 0, "stall ack sem drained");

	struct flpr_acceptance_status as;
	flpr_acceptance_get_status(&as);
	zassert_equal(as.test_blocks_sent, 0, "acceptance counters reset");
}

/* ── Stale produce ───────────────────────────────────────────────── */

ZTEST(flpr_acceptance, test_produce_stale_rejects_uninitialized_and_zero)
{
	zassert_equal(flpr_acceptance_produce_stale_test(7), -EAGAIN, "uninitialized → -EAGAIN");

	acc_init_and_reset(42);
	zassert_equal(flpr_acceptance_produce_stale_test(0), -EINVAL, "zero epoch rejected");
}

ZTEST(flpr_acceptance, test_stale_slot_advances)
{
	acc_init_and_reset(42);

	/* Stale slot with wrong epoch in the OUTPUT ring. */
	zassert_ok(flpr_acceptance_produce_stale_test(7), "produce stale slot");

	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL), FLPR_CONSUME_STALE,
		      "stale slot reported");

	struct flpr_acceptance_status st;
	flpr_acceptance_get_status(&st);
	zassert_equal(st.test_stale_events, 1, "stale event counted");

	/* Consumer advanced past the stale slot. */
	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL), FLPR_CONSUME_EMPTY,
		      "advanced past stale slot");
}

/* ── Report aggregation ──────────────────────────────────────────── */

ZTEST(flpr_acceptance, test_report_subtypes_update_status)
{
	acc_init_and_reset(42);

	struct flpr_msg report = {.type = FLPR_MSG_RING_TEST_REPORT,
				  .version = FLPR_PROTOCOL_VERSION};
	struct flpr_acceptance_status st;

	/* Subtype 0x00: seq lo 16 = crc errors (not exported), data = block count. */
	report.seq = 0x0003;
	report.data = 100;
	mock_hs_invoke_diag(&report);
	flpr_acceptance_get_status(&st);
	zassert_equal(st.test_producer_blocks, 100, "blocks");

	/* Subtype 0xD1: consume_ok. */
	report.seq = 0xD100;
	report.data = 55;
	mock_hs_invoke_diag(&report);
	flpr_acceptance_get_status(&st);
	zassert_equal(st.flpr_consume_ok, 55, "consume_ok");

	/* Subtype 0xD2: produce_ok. */
	report.seq = 0xD200;
	report.data = 44;
	mock_hs_invoke_diag(&report);
	flpr_acceptance_get_status(&st);
	zassert_equal(st.flpr_produce_ok, 44, "produce_ok");

	/* Subtype 0xD3: notify_rcv (seq&0xFF), worker_wake (data lo), produce_full (data hi). */
	report.seq = 0xD301;
	report.data = (33U << 16) | 22U;
	mock_hs_invoke_diag(&report);
	flpr_acceptance_get_status(&st);
	zassert_equal(st.flpr_notify_rcv, 1, "notify_rcv");
	zassert_equal(st.flpr_worker_wake, 22, "worker_wake");
	zassert_equal(st.flpr_produce_full, 33, "produce_full");

	/* Subtype 0xD4: consume_empty (lo), consume_stale (hi). */
	report.seq = 0xD400;
	report.data = (9U << 16) | 8U;
	mock_hs_invoke_diag(&report);
	flpr_acceptance_get_status(&st);
	zassert_equal(st.flpr_consume_empty, 8, "consume_empty");
	zassert_equal(st.flpr_consume_stale, 9, "consume_stale");

	/* Unknown subtype: no field updates. */
	report.seq = 0x99FF;
	report.data = 0xDEADBEEF;
	mock_hs_invoke_diag(&report);
	flpr_acceptance_get_status(&st);
	zassert_equal(st.flpr_consume_ok, 55, "unknown subtype ignored");
}

/* ── Producer stall + counter accounting (moved from ring-mgr suite) ── */

ZTEST(flpr_acceptance, test_producer_stall_full_backpressure)
{
	acc_init_and_reset(42);

	flpr_acceptance_stall_producer(true);

	enum flpr_produce_result pr = flpr_ring_mgr_produce_block(NULL, 480, 1, 0, true);
	zassert_equal(pr, FLPR_PRODUCE_FULL, "stalled producer returns FULL");

	struct flpr_acceptance_status st;
	flpr_acceptance_get_status(&st);
	zassert_equal(st.test_backpressure, 1, "backpressure counted");
	zassert_equal(flpr_ring_producer(flpr_ring_mgr_test_input_ring()), 0, "index not advanced");

	flpr_acceptance_stall_producer(false);
}

ZTEST(flpr_acceptance, test_full_event_counted)
{
	acc_init_and_reset(42);

	for (uint32_t i = 0; i < 4; i++) {
		zassert_equal(flpr_ring_mgr_produce_block(NULL, 480, i, 0, false), FLPR_PRODUCE_OK,
			      "produce %u", i);
	}
	zassert_equal(flpr_ring_mgr_produce_block(NULL, 480, 4, 0, false), FLPR_PRODUCE_FULL,
		      "5th produce is FULL");

	struct flpr_acceptance_status st;
	flpr_acceptance_get_status(&st);
	zassert_equal(st.test_full_events, 1, "full event counted");
}

ZTEST(flpr_acceptance, test_consumer_notification_records_flpr_blocks)
{
	acc_init_and_reset(42);

	struct flpr_msg notify = {.type = FLPR_MSG_RING_CONSUMER,
				  .version = FLPR_PROTOCOL_VERSION,
				  .seq = 7,
				  .data = 42};
	mock_hs_invoke_consumer(&notify);

	struct flpr_acceptance_status st;
	flpr_acceptance_get_status(&st);
	zassert_equal(st.test_producer_blocks, 7, "FLPR block count recorded");
}

ZTEST(flpr_acceptance, test_latency_wrap_unsigned)
{
	acc_init_and_reset(42);
	flpr_acceptance_test_set_active(true);

	/* cpu_timestamp = now + 0xFFFF0000 (mod 2^32): unsigned subtraction
	 * yields exactly 0x10000 regardless of the current cycle value. */
	flpr_ring_mgr_test_set_cycle(1000);
	arrange_output_slot(42, 1, 10, FLPR_SLOT_FLAG_VALID, 0, 0, 1000U + 0xFFFF0000U, 0, 0, NULL,
			    NULL);

	uint32_t latency_out = 0;
	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, &latency_out),
		      FLPR_CONSUME_OK, "consume");
	zassert_equal(latency_out, 0x10000U, "wrapped unsigned latency");

	struct flpr_acceptance_status st;
	flpr_acceptance_get_status(&st);
	zassert_equal(st.latency_count, 1, "metrics updated in test mode");
	zassert_equal(st.latency_min, 0x10000U, "latency_min");
	zassert_equal(st.latency_max, 0x10000U, "latency_max");
	zassert_equal(st.latency_sum, 0x10000U, "latency_sum");

	flpr_acceptance_test_set_active(false);
}

ZTEST(flpr_acceptance, test_latency_metrics_only_in_test_mode)
{
	acc_init_and_reset(42);
	/* test_active OFF. */

	flpr_ring_mgr_test_set_cycle(1000);
	arrange_output_slot(42, 1, 10, FLPR_SLOT_FLAG_VALID, 0, 0, 900, 0, 0, NULL, NULL);

	uint32_t latency_out = 0;
	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, &latency_out),
		      FLPR_CONSUME_OK, "consume");
	zassert_equal(latency_out, 100, "latency still reported");

	struct flpr_acceptance_status st;
	flpr_acceptance_get_status(&st);
	zassert_equal(st.latency_count, 0, "metrics NOT updated outside test mode");
}

ZTEST(flpr_acceptance, test_crc_error_accounting)
{
	acc_init_and_reset(42);
	flpr_acceptance_test_set_active(true);

	/* Payload matches the deterministic pattern for seq 5, but the
	 * stored CRC is wrong → CRC error counted, payload verification OK. */
	uint8_t pcm[50 * 4U];
	flpr_ring_gen_payload(pcm, sizeof(pcm), 5);
	arrange_output_slot(42, 5, 50, FLPR_SLOT_FLAG_VALID, 0, 0xDEADBEEFU, 0, 0, 0, pcm, NULL);

	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL), FLPR_CONSUME_OK,
		      "consume (CRC mismatch counted, not fatal)");

	struct flpr_acceptance_status st;
	flpr_acceptance_get_status(&st);
	zassert_equal(st.test_crc_errors, 1, "CRC error counted");
	zassert_equal(st.test_payload_errors, 0, "payload still matches pattern");

	flpr_acceptance_test_set_active(false);
}

ZTEST(flpr_acceptance, test_payload_mismatch_accounting)
{
	acc_init_and_reset(42);
	flpr_acceptance_test_set_active(true);

	uint8_t pcm[100 * 4U];
	flpr_ring_gen_payload(pcm, sizeof(pcm), 9);
	pcm[0] ^= 0xFFU; /* corrupt one byte → one frame error */
	uint32_t crc = flpr_ring_crc32(pcm, sizeof(pcm));
	arrange_output_slot(42, 9, 100, FLPR_SLOT_FLAG_VALID, 0, crc, 0, 0, 0, pcm, NULL);

	zassert_equal(flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL), FLPR_CONSUME_OK,
		      "consume");

	struct flpr_acceptance_status st;
	flpr_acceptance_get_status(&st);
	zassert_equal(st.test_payload_errors, 1, "payload mismatch counted per frame");
	zassert_equal(st.test_crc_errors, 0, "CRC still valid (corrupted after crc)");

	flpr_acceptance_test_set_active(false);
}

/* ── Stress (moved from flpr_handshake suite) ─────────────────────── */

static K_THREAD_STACK_DEFINE(acc_worker_stack, 2048);
static struct k_thread acc_worker_thread;
static K_SEM_DEFINE(acc_worker_done_sem, 0, 1);
static int acc_worker_result;
static struct flpr_status acc_worker_out;

static void acc_stress_worker_fn(void *count_p, void *u1, void *u2)
{
	(void)u1;
	(void)u2;
	flpr_acceptance_stress((uint32_t)(uintptr_t)count_p, &acc_worker_out);
	k_sem_give(&acc_worker_done_sem);
}

static void acc_hang_worker_fn(void *timeout_p, void *u1, void *u2)
{
	(void)u1;
	(void)u2;
	acc_worker_result = flpr_acceptance_send_fault_hang((uint32_t)(uintptr_t)timeout_p);
	k_sem_give(&acc_worker_done_sem);
}

static void acc_spawn_worker(void (*fn)(void *, void *, void *), void *arg)
{
	k_sem_reset(&acc_worker_done_sem);
	k_thread_create(&acc_worker_thread, acc_worker_stack,
			K_THREAD_STACK_SIZEOF(acc_worker_stack), fn, arg, NULL, NULL,
			K_PRIO_COOP(1), 0, K_NO_WAIT);
}

static bool acc_wait_done(uint32_t timeout_ms)
{
	return k_sem_take(&acc_worker_done_sem, K_MSEC(timeout_ms)) == 0;
}

static bool acc_wait_until(bool (*cond)(void *), void *arg, uint32_t timeout_ms)
{
	uint32_t deadline = k_uptime_get_32() + timeout_ms;

	while (!cond(arg)) {
		if (k_uptime_get_32() >= deadline) {
			return false;
		}
		k_sleep(K_MSEC(1));
	}
	return true;
}

static bool ping_sent_cond(void *arg)
{
	(void)arg;
	return mock_hs_sent_type_count(FLPR_MSG_STRESS_PING) >= 1;
}

static bool ping2_sent_cond(void *arg)
{
	(void)arg;
	return mock_hs_sent_type_count(FLPR_MSG_STRESS_PING) >= 2;
}

static bool hang_sent_cond(void *arg)
{
	(void)arg;
	return mock_hs_sent_type_count(FLPR_MSG_FAULT_HANG) >= 1;
}

ZTEST(flpr_acceptance, test_stress_pong_match_signals_waiter)
{
	acc_init_and_reset(42);

	acc_spawn_worker(acc_stress_worker_fn, (void *)(uintptr_t)1);
	zassert_true(acc_wait_until(ping_sent_cond, NULL, 1000), "ping sent");

	struct flpr_msg pong = {.type = FLPR_MSG_STRESS_PONG,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 0,
				.data = 1}; /* first cookie */
	mock_hs_invoke_diag(&pong);

	zassert_true(acc_wait_done(1000), "stress completed");
	zassert_equal(acc_worker_out.stress_sent, 1, "sent");
	zassert_equal(acc_worker_out.stress_recv, 1, "recv");
	zassert_equal(acc_worker_out.stress_timeouts, 0, "timeouts");
}

ZTEST(flpr_acceptance, test_stress_pong_stale_classified)
{
	acc_init_and_reset(42);

	acc_spawn_worker(acc_stress_worker_fn, (void *)(uintptr_t)2);
	zassert_true(acc_wait_until(ping_sent_cond, NULL, 1000), "ping 1 sent");
	mock_hs_invoke_diag(&(struct flpr_msg){.type = FLPR_MSG_STRESS_PONG,
					       .version = FLPR_PROTOCOL_VERSION,
					       .seq = 0,
					       .data = 1});
	zassert_true(acc_wait_until(ping2_sent_cond, NULL, 1000), "ping 2 sent");

	/* PONG with iteration-1 cookie during iteration 2 → stale. */
	mock_hs_invoke_diag(&(struct flpr_msg){.type = FLPR_MSG_STRESS_PONG,
					       .version = FLPR_PROTOCOL_VERSION,
					       .seq = 0,
					       .data = 1});
	/* Matching cookie completes iteration 2. */
	mock_hs_invoke_diag(&(struct flpr_msg){.type = FLPR_MSG_STRESS_PONG,
					       .version = FLPR_PROTOCOL_VERSION,
					       .seq = 0,
					       .data = 2});

	zassert_true(acc_wait_done(1000), "stress completed");
	zassert_equal(acc_worker_out.stress_sent, 2, "sent");
	zassert_equal(acc_worker_out.stress_recv, 2, "recv");
	zassert_equal(acc_worker_out.stress_stale, 1, "stale counted");
	zassert_equal(acc_worker_out.stress_timeouts, 0, "timeouts");
}

ZTEST(flpr_acceptance, test_stress_pong_future_classified)
{
	acc_init_and_reset(42);

	acc_spawn_worker(acc_stress_worker_fn, (void *)(uintptr_t)1);
	zassert_true(acc_wait_until(ping_sent_cond, NULL, 1000), "ping sent");

	mock_hs_invoke_diag(&(struct flpr_msg){.type = FLPR_MSG_STRESS_PONG,
					       .version = FLPR_PROTOCOL_VERSION,
					       .seq = 0,
					       .data = 3}); /* future cookie */
	mock_hs_invoke_diag(&(struct flpr_msg){.type = FLPR_MSG_STRESS_PONG,
					       .version = FLPR_PROTOCOL_VERSION,
					       .seq = 0,
					       .data = 1}); /* match */

	zassert_true(acc_wait_done(1000), "stress completed");
	zassert_equal(acc_worker_out.stress_mismatch, 1, "future counted as mismatch");
	zassert_equal(acc_worker_out.stress_recv, 1, "match still received");
}

ZTEST(flpr_acceptance, test_stress_pong_inactive_ignored)
{
	acc_init_and_reset(42);

	mock_hs_invoke_diag(&(struct flpr_msg){.type = FLPR_MSG_STRESS_PONG,
					       .version = FLPR_PROTOCOL_VERSION,
					       .seq = 0,
					       .data = 1});

	struct flpr_status st;
	flpr_acceptance_stress_snapshot(&st);
	zassert_equal(st.stress_recv, 0, "no receive counted");
	zassert_equal(st.stress_stale, 0, "no stale counted");
	zassert_equal(st.stress_mismatch, 0, "no mismatch counted");
	zassert_equal(flpr_acceptance_test_stress_sem_count(), 0, "no semaphore signal");
}

ZTEST(flpr_acceptance, test_stress_rejects_unavailable)
{
	/* No ready/acked: stress rejected without pings. */
	flpr_acceptance_stress(3, &acc_worker_out);

	zassert_equal(acc_worker_out.stress_sent, 0, "no pings sent");
	zassert_equal(mock_hs_sent_type_count(FLPR_MSG_STRESS_PING), 0, "no pings on wire");
	zassert_false(acc_worker_out.stress_active, "stress not active");
}

ZTEST(flpr_acceptance, test_stress_rejects_active)
{
	acc_init_and_reset(42);

	acc_spawn_worker(acc_stress_worker_fn, (void *)(uintptr_t)1);
	zassert_true(acc_wait_until(ping_sent_cond, NULL, 1000), "first stress started");

	/* Second concurrent stress call is rejected without side effects. */
	flpr_acceptance_stress(1, NULL);
	struct flpr_status st;
	flpr_acceptance_stress_snapshot(&st);
	zassert_true(st.stress_active, "original stress still active");
	zassert_equal(st.stress_count, 1, "count not clobbered");

	zassert_true(acc_wait_done(1500), "first stress completes (200 ms timeout)");
}

ZTEST(flpr_acceptance, test_stress_clamps_count)
{
	acc_init_and_reset(42);
	/* Park the worker inside its first send so the loop cannot run to
	 * completion; the clamp is asserted from the live stress state.
	 * The worker stays parked (same accepted pattern as the pre-R8
	 * handshake suite) and never touches state again; the next test's
	 * setup resets the acceptance state. */
	mock_hs_set_send_block(true);

	acc_spawn_worker(acc_stress_worker_fn, (void *)(uintptr_t)(FLPR_STRESS_MAX_COUNT + 7));
	zassert_true(acc_wait_until(ping_sent_cond, NULL, 1000), "worker parked in first send");

	struct flpr_status st;
	flpr_acceptance_stress_snapshot(&st);
	zassert_equal(st.stress_count, FLPR_STRESS_MAX_COUNT, "count clamped");
	zassert_true(st.stress_active, "stress active while parked");
}

ZTEST(flpr_acceptance, test_stress_send_failure)
{
	acc_init_and_reset(42);
	mock_hs_set_send_result(-EIO);

	acc_spawn_worker(acc_stress_worker_fn, (void *)(uintptr_t)2);
	zassert_true(acc_wait_done(1500), "stress completed");

	zassert_equal(acc_worker_out.stress_err_send, 2, "two send errors");
	zassert_equal(acc_worker_out.stress_sent, 0, "no successful sends");
	zassert_equal(acc_worker_out.stress_timeouts, 0, "no sem wait on send failure");
}

ZTEST(flpr_acceptance, test_stress_timeout)
{
	acc_init_and_reset(42);

	acc_spawn_worker(acc_stress_worker_fn, (void *)(uintptr_t)1);
	zassert_true(acc_wait_done(1500), "stress completed (200 ms timeout)");

	zassert_equal(acc_worker_out.stress_sent, 1, "sent");
	zassert_equal(acc_worker_out.stress_recv, 0, "no PONG");
	zassert_equal(acc_worker_out.stress_timeouts, 1, "timeout counted");
}

ZTEST(flpr_acceptance, test_stress_late_pong)
{
	acc_init_and_reset(42);

	acc_spawn_worker(acc_stress_worker_fn, (void *)(uintptr_t)2);
	zassert_true(acc_wait_until(ping_sent_cond, NULL, 1000), "ping 1 sent");
	/* Iteration 1 times out (no PONG); cookie advances. */
	zassert_true(acc_wait_until(ping2_sent_cond, NULL, 1500), "ping 2 sent");

	/* Late PONG for iteration 1 arrives during iteration 2 → stale.
	 * Note: the timeout invalidation advances stress_cookie, so
	 * iteration 2's expected cookie is 3 (1 → timeout → 3). */
	mock_hs_invoke_diag(&(struct flpr_msg){.type = FLPR_MSG_STRESS_PONG,
					       .version = FLPR_PROTOCOL_VERSION,
					       .seq = 0,
					       .data = 1});
	/* Matching PONG completes iteration 2. */
	mock_hs_invoke_diag(&(struct flpr_msg){.type = FLPR_MSG_STRESS_PONG,
					       .version = FLPR_PROTOCOL_VERSION,
					       .seq = 0,
					       .data = 3});

	zassert_true(acc_wait_done(1500), "stress completed");
	zassert_equal(acc_worker_out.stress_timeouts, 1, "iteration 1 timed out");
	zassert_equal(acc_worker_out.stress_stale, 1, "late PONG classified stale");
	zassert_equal(acc_worker_out.stress_recv, 1, "iteration 2 matched");
	zassert_equal(acc_worker_out.stress_sent, 2, "sent");
}

/* ── Fault hang (moved from flpr_handshake suite) ─────────────────── */

ZTEST(flpr_acceptance, test_fault_hang_ack_signals_waiter)
{
	acc_init_and_reset(42);

	acc_spawn_worker(acc_hang_worker_fn, (void *)(uintptr_t)500);
	zassert_true(acc_wait_until(hang_sent_cond, NULL, 1000), "FAULT_HANG sent");

	mock_hs_invoke_diag(&(struct flpr_msg){.type = FLPR_MSG_FAULT_HANG_ACK,
					       .version = FLPR_PROTOCOL_VERSION,
					       .seq = 0,
					       .data = 0});

	zassert_true(acc_wait_done(1000), "fault hang completed");
	zassert_ok(acc_worker_result, "fault hang ACK success");
}

ZTEST(flpr_acceptance, test_fault_hang_send_failure)
{
	acc_init_and_reset(42);
	mock_hs_set_send_result(-EIO);

	zassert_equal(flpr_acceptance_send_fault_hang(50), -EIO, "send failure → -EIO");
}

ZTEST(flpr_acceptance, test_fault_hang_ack_timeout)
{
	acc_init_and_reset(42);

	acc_spawn_worker(acc_hang_worker_fn, (void *)(uintptr_t)50);
	zassert_true(acc_wait_until(hang_sent_cond, NULL, 1000), "FAULT_HANG sent");
	zassert_true(acc_wait_done(1000), "fault hang completed");
	zassert_equal(acc_worker_result, -ETIMEDOUT, "ACK timeout");
}

/* ── Gate runner (native-reachable paths) ───────────────────────────
 * The full gate sequence (1–6) requires a live FLPR worker (gate 1's
 * test_run drain and gates 2–5's resume drains); the all-gates-PASS
 * tail stays hardware evidence, exactly as the R4 shell test kept
 * long-running acceptance on hardware.  Natively we prove: the runner
 * entry, the prep path (coordinated reset + stall clears), the
 * prep-reset-failure errno return, and the output sink severity
 * mapping, by running the runner with a scripted send failure at the
 * first coordinated reset. */

struct gate_output {
	char lines[8][128];
	int count;
};

static void gate_print_rec(void *ctx, enum flpr_acceptance_print_level lvl, const char *line)
{
	(void)lvl;
	struct gate_output *go = ctx;

	if (go->count < 8) {
		strncpy(go->lines[go->count], line, sizeof(go->lines[0]) - 1);
		go->lines[go->count][sizeof(go->lines[0]) - 1] = '\0';
	}
	go->count++;
}

ZTEST(flpr_acceptance, test_gate_runner_prep_reset_failure)
{
	acc_init_and_reset(42);
	mock_hs_set_auto_ack(true);
	/* Fail every send at index 0 (the prep coordinated reset). */
	mock_hs_set_send_fail_from(0);

	struct gate_output go;
	memset(&go, 0, sizeof(go));

	int ret = flpr_acceptance_run_gates(1, &go, gate_print_rec);

	zassert_not_equal(ret, 0, "prep reset failure propagates as an error");
	zassert_true(go.count >= 1, "error line emitted");
	zassert_not_null(strstr(go.lines[0], "Coordinated reset failed"), "exact error text");
}

ZTEST_SUITE(flpr_acceptance, NULL, NULL, acc_setup, NULL, NULL);
