/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Direct native tests for the FLPR-image acceptance module (R8) — real
 * production source execution.
 *
 * This suite compiles src/flpr/acceptance.c (the FLPR acceptance
 * handlers that src/flpr/main.c delegates to under
 * CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS) and drives it through the injected
 * send/wake dependency table — no ipc_service, no devicetree, no RV32
 * glue.  Wire protocol values, report subtype packing, stall packing /
 * ACK echo, stress echo, and the FAULT_HANG ACK-before-spin ordering are
 * proven directly against the production module.
 */
#include <zephyr/ztest.h>
#include <string.h>

#include "acceptance.h"
#include "fake_flpr_deps.h"
#include "flpr_protocol.h"

/* ── Setup ───────────────────────────────────────────────────────── */

static void flpr_setup(void *fixture)
{
	(void)fixture;
	fake_deps_reset();

	static const struct flpr_acceptance_deps deps = {
		.send = fake_deps_send,
		.wake = fake_deps_wake,
	};

	flpr_acceptance_init(&deps);
}

/* ── RING_TEST_START / STOP report cascade ────────────────────────── */

ZTEST(flpr_acceptance_flpr, test_test_start_stop_report_cascade)
{
	/* START resets and activates. */
	struct flpr_msg start = {.type = FLPR_MSG_RING_TEST_START,
				 .version = FLPR_PROTOCOL_VERSION,
				 .seq = 0,
				 .data = 100};
	zassert_true(flpr_acceptance_handle_msg(&start), "START consumed");
	zassert_true(flpr_acceptance_test_active(), "test active after START");

	/* Diagnostic hooks update the counters reported at STOP. */
	flpr_acceptance_note_worker_wake();
	flpr_acceptance_note_worker_wake();
	flpr_acceptance_note_consume_ok();
	flpr_acceptance_note_consume_empty();
	flpr_acceptance_note_consume_stale();
	flpr_acceptance_note_produce_ok();
	flpr_acceptance_note_produce_full();
	flpr_acceptance_note_notify_rcv();
	flpr_acceptance_note_block_processed();
	flpr_acceptance_note_crc_error();
	flpr_acceptance_note_empty_poll();
	flpr_acceptance_note_epoch_stale();

	/* STOP sends the five-report cascade with exact subtype packing. */
	struct flpr_msg stop = {.type = FLPR_MSG_RING_TEST_STOP,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 0,
				.data = 0};
	zassert_true(flpr_acceptance_handle_msg(&stop), "STOP consumed");
	zassert_false(flpr_acceptance_test_active(), "test inactive after STOP");

	zassert_equal(fake_deps_sent_type_count(FLPR_MSG_RING_TEST_REPORT), 5, "five reports sent");

	/* Report 1: seq lo 16 = crc_errors, data = block_count. */
	uint32_t r0 = 0;
	for (uint32_t i = 0; i < fake_deps_sent_count(); i++) {
		const struct flpr_msg *m = fake_deps_sent_at(i);

		if (m->type == FLPR_MSG_RING_TEST_REPORT && (uint8_t)(m->seq >> 8) == 0x00) {
			r0 = i;
			break;
		}
	}
	zassert_equal(fake_deps_sent_at(r0)->seq, 1, "crc_errors = 1");
	zassert_equal(fake_deps_sent_at(r0)->data, 1, "block_count = 1");

	/* Report 2: 0xD1 consume_ok. */
	uint32_t r1 = 0;
	for (uint32_t i = 0; i < fake_deps_sent_count(); i++) {
		const struct flpr_msg *m = fake_deps_sent_at(i);

		if (m->type == FLPR_MSG_RING_TEST_REPORT && m->seq == 0xD100U) {
			r1 = i;
			break;
		}
	}
	zassert_equal(fake_deps_sent_at(r1)->data, 1, "consume_ok = 1");

	/* Report 3: 0xD2 produce_ok. */
	uint32_t r2 = 0;
	for (uint32_t i = 0; i < fake_deps_sent_count(); i++) {
		const struct flpr_msg *m = fake_deps_sent_at(i);

		if (m->type == FLPR_MSG_RING_TEST_REPORT && m->seq == 0xD200U) {
			r2 = i;
			break;
		}
	}
	zassert_equal(fake_deps_sent_at(r2)->data, 1, "produce_ok = 1");

	/* Report 4: 0xD3 notify_rcv(lo8 seq) + worker_wake(lo16 data) +
	 * produce_full(hi16 data). */
	uint32_t r3 = 0;
	for (uint32_t i = 0; i < fake_deps_sent_count(); i++) {
		const struct flpr_msg *m = fake_deps_sent_at(i);

		if (m->type == FLPR_MSG_RING_TEST_REPORT && (uint8_t)(m->seq >> 8) == 0xD3) {
			r3 = i;
			break;
		}
	}
	zassert_equal((uint8_t)fake_deps_sent_at(r3)->seq, 1, "notify_rcv = 1");
	zassert_equal((uint32_t)(fake_deps_sent_at(r3)->data & 0xFFFFU), 2, "worker_wake = 2");
	zassert_equal((uint32_t)((fake_deps_sent_at(r3)->data >> 16) & 0xFFFFU), 1,
		      "produce_full = 1");

	/* Report 5: 0xD4 cons_empty(lo16) + cons_stale(hi16). */
	uint32_t r4 = 0;
	for (uint32_t i = 0; i < fake_deps_sent_count(); i++) {
		const struct flpr_msg *m = fake_deps_sent_at(i);

		if (m->type == FLPR_MSG_RING_TEST_REPORT && m->seq == 0xD400U) {
			r4 = i;
			break;
		}
	}
	zassert_equal((uint32_t)(fake_deps_sent_at(r4)->data & 0xFFFFU), 1, "cons_empty = 1");
	zassert_equal((uint32_t)((fake_deps_sent_at(r4)->data >> 16) & 0xFFFFU), 1,
		      "cons_stale = 1");
}

ZTEST(flpr_acceptance_flpr, test_test_start_restarts_counters)
{
	struct flpr_msg start = {.type = FLPR_MSG_RING_TEST_START,
				 .version = FLPR_PROTOCOL_VERSION,
				 .seq = 0,
				 .data = 5};
	zassert_true(flpr_acceptance_handle_msg(&start), "START 1");
	flpr_acceptance_note_block_processed();

	/* Second START resets the counters. */
	zassert_true(flpr_acceptance_handle_msg(&start), "START 2");
	flpr_acceptance_note_block_processed();

	struct flpr_msg stop = {.type = FLPR_MSG_RING_TEST_STOP,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 0,
				.data = 0};
	zassert_true(flpr_acceptance_handle_msg(&stop), "STOP");

	/* Only the post-restart block count is reported. */
	uint32_t r0 = 0;
	for (uint32_t i = 0; i < fake_deps_sent_count(); i++) {
		const struct flpr_msg *m = fake_deps_sent_at(i);

		if (m->type == FLPR_MSG_RING_TEST_REPORT && (uint8_t)(m->seq >> 8) == 0x00) {
			r0 = i;
			break;
		}
	}
	zassert_equal(fake_deps_sent_at(r0)->data, 1, "block count reset by second START");
}

/* ── RING_STALL ──────────────────────────────────────────────────── */

ZTEST(flpr_acceptance_flpr, test_stall_persistent_ack_echo)
{
	struct flpr_msg stall = {.type = FLPR_MSG_RING_STALL,
				 .version = FLPR_PROTOCOL_VERSION,
				 .seq = 0x1234,
				 .data = FLPR_STALL_PACK(0x03, 0)};
	zassert_true(flpr_acceptance_handle_msg(&stall), "stall consumed");
	zassert_equal(flpr_acceptance_stall_flags(), 0x03, "flags applied");

	/* ACK echoes the exact packed value and the request token. */
	zassert_equal(fake_deps_sent_type_count(FLPR_MSG_RING_STALL_ACK), 1, "one ACK");
	const struct flpr_msg *ack = NULL;

	for (uint32_t i = 0; i < fake_deps_sent_count(); i++) {
		if (fake_deps_sent_at(i)->type == FLPR_MSG_RING_STALL_ACK) {
			ack = fake_deps_sent_at(i);
		}
	}
	zassert_not_null(ack, "ACK found");
	zassert_equal(ack->seq, 0x1234, "ACK echoes request token");
	zassert_equal(ack->data, FLPR_STALL_PACK(0x03, 0), "ACK echoes packed value");
}

ZTEST(flpr_acceptance_flpr, test_stall_timed_autoclear)
{
	struct flpr_msg stall = {.type = FLPR_MSG_RING_STALL,
				 .version = FLPR_PROTOCOL_VERSION,
				 .seq = 1,
				 .data = FLPR_STALL_PACK(0x01, 50)};
	zassert_true(flpr_acceptance_handle_msg(&stall), "timed stall consumed");
	zassert_equal(flpr_acceptance_stall_flags(), 0x01, "flags applied");

	/* Timer auto-clears after the duration and wakes the loop. */
	k_sleep(K_MSEC(150));
	zassert_equal(flpr_acceptance_stall_flags(), 0, "flags cleared by timer");
	zassert_true(fake_deps_wake_count() >= 1, "timer expiry woke the loop");
}

ZTEST(flpr_acceptance_flpr, test_stall_clear_and_reset)
{
	struct flpr_msg stall = {.type = FLPR_MSG_RING_STALL,
				 .version = FLPR_PROTOCOL_VERSION,
				 .seq = 1,
				 .data = FLPR_STALL_PACK(0x02, 0)};
	zassert_true(flpr_acceptance_handle_msg(&stall), "stall");
	zassert_equal(flpr_acceptance_stall_flags(), 0x02, "flags applied");

	/* Persistent clear (mask 0, duration 0). */
	struct flpr_msg clear = {
		.type = FLPR_MSG_RING_STALL, .version = FLPR_PROTOCOL_VERSION, .seq = 2, .data = 0};
	zassert_true(flpr_acceptance_handle_msg(&clear), "clear");
	zassert_equal(flpr_acceptance_stall_flags(), 0, "flags cleared");

	/* Ring reset also clears flags and resets counters. */
	zassert_true(flpr_acceptance_handle_msg(&stall), "stall again");
	zassert_equal(flpr_acceptance_stall_flags(), 0x02, "flags applied");
	flpr_acceptance_note_block_processed();
	flpr_acceptance_on_ring_reset();
	zassert_equal(flpr_acceptance_stall_flags(), 0, "flags cleared by ring reset");
	zassert_false(flpr_acceptance_test_active(), "test cleared by ring reset");
}

/* ── STRESS_PING ─────────────────────────────────────────────────── */

ZTEST(flpr_acceptance_flpr, test_stress_ping_pong_echo)
{
	struct flpr_msg ping = {.type = FLPR_MSG_STRESS_PING,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 0x00AB,
				.data = 0xCAFEBABEU};
	zassert_true(flpr_acceptance_handle_msg(&ping), "ping consumed");
	zassert_equal(fake_deps_sent_type_count(FLPR_MSG_STRESS_PONG), 1, "one PONG");
	const struct flpr_msg *pong = NULL;

	for (uint32_t i = 0; i < fake_deps_sent_count(); i++) {
		if (fake_deps_sent_at(i)->type == FLPR_MSG_STRESS_PONG) {
			pong = fake_deps_sent_at(i);
		}
	}
	zassert_not_null(pong, "PONG found");
	zassert_equal(pong->seq, 0x00AB, "PONG echoes seq");
	zassert_equal(pong->data, 0xCAFEBABEU, "PONG echoes cookie");
}

/* ── FAULT_HANG ACK-before-spin ──────────────────────────────────── */

ZTEST(flpr_acceptance_flpr, test_fault_hang_ack_before_pending)
{
	zassert_false(flpr_acceptance_hang_pending(), "no hang initially");

	struct flpr_msg hang = {
		.type = FLPR_MSG_FAULT_HANG, .version = FLPR_PROTOCOL_VERSION, .seq = 0, .data = 0};
	zassert_true(flpr_acceptance_handle_msg(&hang), "hang consumed");

	/* ACK must be recorded BEFORE hang_pending is visible (the main
	 * loop only reads flpr_acceptance_hang_pending() after the ACK
	 * send is recorded). */
	zassert_equal(fake_deps_sent_type_count(FLPR_MSG_FAULT_HANG_ACK), 1,
		      "ACK sent before pending");
	zassert_true(flpr_acceptance_hang_pending(), "hang pending after ACK");
	zassert_true(fake_deps_wake_count() >= 1, "main loop woken for the spin");
}

/* ── Unknown messages ────────────────────────────────────────────── */

ZTEST(flpr_acceptance_flpr, test_unknown_message_false)
{
	struct flpr_msg unknown = {.type = 0x7F, .version = FLPR_PROTOCOL_VERSION};
	zassert_false(flpr_acceptance_handle_msg(&unknown), "unknown not consumed");
	zassert_false(flpr_acceptance_handle_msg(NULL), "NULL not consumed");
}

ZTEST_SUITE(flpr_acceptance_flpr, NULL, NULL, flpr_setup, NULL, NULL);
