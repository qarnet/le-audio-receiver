/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the FLPR handshake — real production source execution
 * (T1C).
 *
 * This suite compiles src/flpr_handshake.c with FLPR_HANDSHAKE_NATIVE_TEST
 * against a fake IPC service backend (see fake_ipc_backend.c, patterned
 * on the NCS v3.3.0 ipc_service test backend).  The real Zephyr
 * ipc_service_open_instance/register/deregister/send APIs are used; the
 * backend captures the endpoint config, invokes the production callbacks,
 * records sent messages, and exposes test controls.  Tests drive the
 * production receive path by injecting messages through the backend.
 *
 * Pure flpr_protocol.h helper coverage lives in the flpr_protocol suite;
 * the copied lifecycle simulations previously in this suite are removed.
 */
#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>

#include "flpr_handshake.h"
#include "flpr_handshake_hooks.h"
#include "fake_ipc_backend.h"
#include "flpr_protocol.h"

/* ── Setup / teardown ────────────────────────────────────────────── */

static void hs_setup(void *fixture)
{
	(void)fixture;
	flpr_handshake_test_reset();
	fake_ipc_reset();
}

static void hs_teardown(void *fixture)
{
	(void)fixture;
	/* Cancel any heartbeat reschedule left pending by the test. */
	flpr_handshake_test_reset();
}

/* READY flow helper: init + inject READY with the given epoch. */
static void hs_ready_flow(uint32_t epoch)
{
	zassert_ok(flpr_handshake_init(), "init");
	struct flpr_msg ready = {
		.type = FLPR_MSG_READY, .version = FLPR_PROTOCOL_VERSION, .seq = 0, .data = epoch};
	fake_ipc_receive(&ready, sizeof(ready));
}

/* ── Initialization ──────────────────────────────────────────────── */

ZTEST(flpr_handshake, test_init_open_register_success)
{
	zassert_ok(flpr_handshake_init(), "init");
	zassert_true(fake_ipc_endpoint_registered(), "endpoint registered");
	zassert_equal(flpr_handshake_test_bound_sem_count(), 1, "bound callback gave sem");
	zassert_ok(flpr_handshake_wait_bound(K_NO_WAIT), "bound wait succeeds");
}

ZTEST(flpr_handshake, test_init_open_ealready_accepted)
{
	fake_ipc_set_open_result(-EALREADY);
	zassert_ok(flpr_handshake_init(), "-EALREADY accepted");
}

ZTEST(flpr_handshake, test_init_open_failure_propagated)
{
	fake_ipc_set_open_result(-EIO);
	zassert_equal(flpr_handshake_init(), -EIO, "open failure propagated");
}

ZTEST(flpr_handshake, test_init_register_failure_propagated)
{
	fake_ipc_set_register_result(-ENOMEM);
	zassert_equal(flpr_handshake_init(), -ENOMEM, "register failure propagated");
}

ZTEST(flpr_handshake, test_endpoint_bound_callback)
{
	fake_ipc_set_auto_bound(false);
	zassert_ok(flpr_handshake_init(), "init without auto bound");
	zassert_equal(flpr_handshake_test_bound_sem_count(), 0, "no bound yet");
	zassert_equal(flpr_handshake_wait_bound(K_NO_WAIT), -EBUSY, "wait times out");
}

ZTEST(flpr_handshake, test_init_reset_idempotent_cleanup)
{
	zassert_ok(flpr_handshake_init(), "init 1");
	zassert_ok(flpr_handshake_init(), "init 2 (idempotent)");

	flpr_handshake_test_reset();
	fake_ipc_reset();
	zassert_ok(flpr_handshake_init(), "init after test cleanup");
}

/* ── Receive path ────────────────────────────────────────────────── */

ZTEST(flpr_handshake, test_short_oversized_wrong_version_counters)
{
	zassert_ok(flpr_handshake_init(), "init");

	uint8_t short_msg[4] = {0};
	fake_ipc_receive(short_msg, sizeof(short_msg));
	fake_ipc_receive(short_msg, sizeof(short_msg));

	uint8_t big_msg[16] = {0};
	fake_ipc_receive(big_msg, sizeof(big_msg));

	struct flpr_msg bad_ver = {.type = FLPR_MSG_READY, .version = FLPR_PROTOCOL_VERSION - 1};
	fake_ipc_receive(&bad_ver, sizeof(bad_ver));

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.err_len, 3, "err_len (2 short + 1 oversized)");
	zassert_equal(st.err_version, 1, "err_version");

	/* Valid message changes nothing. */
	struct flpr_msg good = {
		.type = FLPR_MSG_READY, .version = FLPR_PROTOCOL_VERSION, .seq = 0, .data = 1};
	fake_ipc_receive(&good, sizeof(good));
	flpr_handshake_get_status(&st);
	zassert_equal(st.err_len, 3, "err_len unchanged");
	zassert_equal(st.err_version, 1, "err_version unchanged");
}

ZTEST(flpr_handshake, test_unknown_type_increments_unknown)
{
	zassert_ok(flpr_handshake_init(), "init");

	struct flpr_msg unknown = {.type = 0x7F, .version = FLPR_PROTOCOL_VERSION};
	fake_ipc_receive(&unknown, sizeof(unknown));

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.err_unknown, 1, "err_unknown");
}

ZTEST(flpr_handshake, test_first_ready_new_epoch_exact_ack)
{
	zassert_ok(flpr_handshake_init(), "init");

	struct flpr_msg ready = {
		.type = FLPR_MSG_READY, .version = FLPR_PROTOCOL_VERSION, .seq = 0, .data = 42};
	fake_ipc_receive(&ready, sizeof(ready));

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_true(st.ready, "ready");
	zassert_true(st.acked, "acked after ACK send");
	zassert_equal(st.epoch, 42, "epoch");
	zassert_equal(st.ready_count, 1, "ready_count");
	zassert_equal(st.reboot_count, 1, "reboot_count");

	const struct flpr_msg *ack = fake_ipc_sent_at(0);
	zassert_not_null(ack, "READY_ACK sent");
	zassert_equal(ack->type, FLPR_MSG_READY_ACK, "type");
	zassert_equal(ack->version, FLPR_PROTOCOL_VERSION, "version");
	zassert_equal(ack->seq, 0, "seq");
	zassert_true(ack->data <= k_uptime_get_32(), "data is a plausible uptime");

	zassert_equal(flpr_handshake_test_new_ready_sem_count(), 1, "new-ready signalled");
	zassert_equal(flpr_handshake_test_hb_start_requests(), 1,
		      "heartbeat start recorded (not submitted)");
}

ZTEST(flpr_handshake, test_duplicate_ready_no_reboot_no_signal)
{
	hs_ready_flow(42);
	/* Drain the first new-ready token. */
	zassert_ok(flpr_handshake_wait_new_ready(41, K_NO_WAIT), "drain first signal");

	struct flpr_msg ready = {
		.type = FLPR_MSG_READY, .version = FLPR_PROTOCOL_VERSION, .seq = 0, .data = 42};
	fake_ipc_receive(&ready, sizeof(ready));

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.ready_count, 2, "ready_count");
	zassert_equal(st.reboot_count, 1, "reboot_count unchanged (same epoch)");
	zassert_equal(flpr_handshake_test_new_ready_sem_count(), 0,
		      "duplicate READY gives no new-ready signal");
}

ZTEST(flpr_handshake, test_changed_epoch_ready_counts_reboot)
{
	hs_ready_flow(42);
	zassert_ok(flpr_handshake_wait_new_ready(41, K_NO_WAIT), "drain first signal");

	struct flpr_msg ready = {
		.type = FLPR_MSG_READY, .version = FLPR_PROTOCOL_VERSION, .seq = 0, .data = 99};
	fake_ipc_receive(&ready, sizeof(ready));

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.epoch, 99, "epoch updated");
	zassert_equal(st.ready_count, 2, "ready_count");
	zassert_equal(st.reboot_count, 2, "reboot_count (changed epoch)");
	zassert_equal(flpr_handshake_test_new_ready_sem_count(), 1, "new-ready signalled");
}

ZTEST(flpr_handshake, test_ready_ack_send_failure_no_ack_no_signal)
{
	zassert_ok(flpr_handshake_init(), "init");
	fake_ipc_set_send_result(-EIO);

	struct flpr_msg ready = {
		.type = FLPR_MSG_READY, .version = FLPR_PROTOCOL_VERSION, .seq = 0, .data = 42};
	fake_ipc_receive(&ready, sizeof(ready));

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_false(st.acked, "not acked on send failure");
	zassert_equal(st.err_send, 1, "err_send incremented");
	zassert_equal(flpr_handshake_test_new_ready_sem_count(), 0,
		      "no new-ready signal without ACK");

	/* Same epoch: no token (K_NO_WAIT → -EBUSY). */
	zassert_equal(flpr_handshake_wait_new_ready(42, K_NO_WAIT), -EBUSY,
		      "same epoch, no signal token (K_NO_WAIT) → -EBUSY");

	/* CHANGED epoch with failed ACK: the fast path must NOT succeed
	 * (it requires acked); the semaphore path has no token → timeout. */
	zassert_equal(flpr_handshake_wait_new_ready(41, K_NO_WAIT), -EBUSY,
		      "changed epoch without ACK: fast path blocked (K_NO_WAIT) → -EBUSY");
	zassert_equal(flpr_handshake_wait_new_ready(41, K_MSEC(50)), -EAGAIN,
		      "changed epoch without ACK: times out");

	/* A later successful duplicate READY ACKs the epoch; only then
	 * does the fast path succeed. */
	fake_ipc_set_send_result(0);
	fake_ipc_receive(&ready, sizeof(ready)); /* duplicate READY, ACK now succeeds */
	flpr_handshake_get_status(&st);
	zassert_true(st.acked, "acked after successful duplicate READY ACK");
	zassert_ok(flpr_handshake_wait_new_ready(41, K_NO_WAIT),
		   "changed epoch succeeds only after successful ACK");
}

ZTEST(flpr_handshake, test_changed_epoch_ack_success_fast_path)
{
	hs_ready_flow(42);

	/* Changed epoch + successful ACK: fast path succeeds without
	 * waiting and drains the posted signal. */
	zassert_ok(flpr_handshake_wait_new_ready(41, K_NO_WAIT), "fast path success");
	zassert_equal(flpr_handshake_test_new_ready_sem_count(), 0, "signal drained");

	/* Same epoch still times out. */
	zassert_equal(flpr_handshake_wait_new_ready(42, K_MSEC(50)), -EAGAIN,
		      "same epoch times out");
}

/* ── R1 repair: changed epoch clears stale ACK state ────────────────
 * A changed/new epoch invalidates the previous session's ACK state: if
 * the READY_ACK send for the new epoch fails, wait/status must not
 * report success off the prior epoch's acked=true.  Previously the stale
 * ACK let flpr_handshake_wait_new_ready() take its fast path against an
 * un-acked epoch (flpr_protocol.h flpr_peer_handle_ready now clears
 * peer->acked on new epoch). */

ZTEST(flpr_handshake, test_changed_epoch_send_failure_clears_ack)
{
	/* Complete epoch 42 READY/ACK so acked=true. */
	hs_ready_flow(42);
	zassert_ok(flpr_handshake_wait_new_ready(41, K_NO_WAIT), "drain first signal");

	struct flpr_status st;

	flpr_handshake_get_status(&st);
	zassert_true(st.acked, "acked after successful epoch 42");
	zassert_equal(st.epoch, 42, "epoch 42 established");

	/* Make the IPC send fail, then inject changed epoch 99 READY. */
	fake_ipc_set_send_result(-EIO);
	struct flpr_msg ready = {
		.type = FLPR_MSG_READY, .version = FLPR_PROTOCOL_VERSION, .seq = 0, .data = 99};
	fake_ipc_receive(&ready, sizeof(ready));

	flpr_handshake_get_status(&st);
	zassert_equal(st.epoch, 99, "status epoch 99");
	zassert_equal(st.ready_count, 2, "ready_count incremented");
	zassert_equal(st.reboot_count, 2, "reboot count incremented");
	zassert_false(st.acked, "acked cleared on changed epoch");
	zassert_equal(st.err_send, 1, "send error incremented");
	zassert_equal(flpr_handshake_test_new_ready_sem_count(), 0, "no new-ready token");

	/* The stale fast path must NOT succeed: epoch differs from 42 but
	 * acked=false (K_NO_WAIT → -EBUSY, bounded wait → -EAGAIN). */
	zassert_equal(flpr_handshake_wait_new_ready(42, K_NO_WAIT), -EBUSY,
		      "no stale fast path without ack (K_NO_WAIT)");
	zassert_equal(flpr_handshake_wait_new_ready(42, K_MSEC(50)), -EAGAIN,
		      "no stale fast path without ack (bounded wait times out)");

	/* A successful duplicate READY for epoch 99 restores ACK and
	 * permits the expected fast-path result. */
	fake_ipc_set_send_result(0);
	fake_ipc_receive(&ready, sizeof(ready)); /* duplicate READY epoch 99, ACK succeeds */

	flpr_handshake_get_status(&st);
	zassert_true(st.acked, "acked restored after successful duplicate READY");
	zassert_equal(st.epoch, 99, "epoch still 99");
	zassert_ok(flpr_handshake_wait_new_ready(42, K_NO_WAIT),
		   "fast path succeeds only after epoch 99 ACK");
}

ZTEST(flpr_handshake, test_heartbeat_rx_sequence_and_echo)
{
	/* Not acked: rx tracked, no echo. */
	zassert_ok(flpr_handshake_init(), "init");
	struct flpr_msg hb = {.type = FLPR_MSG_HEARTBEAT,
			      .version = FLPR_PROTOCOL_VERSION,
			      .seq = 7,
			      .data = 100};
	fake_ipc_receive(&hb, sizeof(hb));
	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.rx_seq, 7, "rx_seq tracked without ack");
	zassert_equal(fake_ipc_sent_type_count(FLPR_MSG_HEARTBEAT_ACK), 0, "no echo unacked");

	/* Acked: exact echo. */
	flpr_handshake_test_reset();
	fake_ipc_reset();
	hs_ready_flow(42);

	fake_ipc_receive(&hb, sizeof(hb));
	flpr_handshake_get_status(&st);
	zassert_equal(st.rx_seq, 7, "rx_seq");
	zassert_true(st.rx_last_ms > 0, "rx timestamp updated");

	const struct flpr_msg *echo = fake_ipc_sent_at(1); /* [0]=READY_ACK */
	zassert_not_null(echo, "echo sent");
	zassert_equal(echo->type, FLPR_MSG_HEARTBEAT_ACK, "echo type");
	zassert_equal(echo->seq, 7, "echo seq matches");
	zassert_equal(echo->version, FLPR_PROTOCOL_VERSION, "echo version");
}

ZTEST(flpr_handshake, test_heartbeat_ack_tracking)
{
	hs_ready_flow(42);

	struct flpr_msg ack3 = {.type = FLPR_MSG_HEARTBEAT_ACK,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 3,
				.data = 0};
	fake_ipc_receive(&ack3, sizeof(ack3));
	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.tx_acked_seq, 3, "ack advanced");

	/* Stale ack never regresses. */
	struct flpr_msg ack1 = {.type = FLPR_MSG_HEARTBEAT_ACK,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 1,
				.data = 0};
	fake_ipc_receive(&ack1, sizeof(ack1));
	flpr_handshake_get_status(&st);
	zassert_equal(st.tx_acked_seq, 3, "stale ack ignored");

	struct flpr_msg ack5 = {.type = FLPR_MSG_HEARTBEAT_ACK,
				.version = FLPR_PROTOCOL_VERSION,
				.seq = 5,
				.data = 0};
	fake_ipc_receive(&ack5, sizeof(ack5));
	flpr_handshake_get_status(&st);
	zassert_equal(st.tx_acked_seq, 5, "ack advanced again");
}

/* Recording ring handlers at file scope; each also probes the module
 * spinlock by calling get_status() (a nested spinlock would assert under
 * CONFIG_SPIN_VALIDATE, proving dispatch runs without flpr_lock).
 * R8: slots [0]=reset ACK, [1]=consumer (production slot);
 * [2]=report, [3]=stall ACK, [4]=stress PONG, [5]=fault-hang ACK
 * (diagnostic slot). */
struct ring_rec {
	const struct flpr_msg *msg;
	void *ud;
	int calls;
};

static struct ring_rec ring_records[6];
static int ring_dispatch_marker;

static void reset_rec(const struct flpr_msg *m, void *ud)
{
	ring_records[0].msg = m;
	ring_records[0].ud = ud;
	ring_records[0].calls++;
	struct flpr_status st;
	flpr_handshake_get_status(&st);
}

static void consumer_rec(const struct flpr_msg *m, void *ud)
{
	ring_records[1].msg = m;
	ring_records[1].ud = ud;
	ring_records[1].calls++;
	struct flpr_status st;
	flpr_handshake_get_status(&st);
}

static void report_rec(const struct flpr_msg *m, void *ud)
{
	ring_records[2].msg = m;
	ring_records[2].ud = ud;
	ring_records[2].calls++;
	struct flpr_status st;
	flpr_handshake_get_status(&st);
}

static void stall_rec(const struct flpr_msg *m, void *ud)
{
	ring_records[3].msg = m;
	ring_records[3].ud = ud;
	ring_records[3].calls++;
	struct flpr_status st;
	flpr_handshake_get_status(&st);
}

static void pong_rec(const struct flpr_msg *m, void *ud)
{
	ring_records[4].msg = m;
	ring_records[4].ud = ud;
	ring_records[4].calls++;
	struct flpr_status st;
	flpr_handshake_get_status(&st);
}

static void hang_ack_rec(const struct flpr_msg *m, void *ud)
{
	ring_records[5].msg = m;
	ring_records[5].ud = ud;
	ring_records[5].calls++;
	struct flpr_status st;
	flpr_handshake_get_status(&st);
}

/* The diagnostic slot is ONE handler receiving all four diagnostic
 * types; it routes by message type exactly like the acceptance module's
 * handler (flpr_acceptance_diag_handler). */
static void diag_router(const struct flpr_msg *m, void *ud)
{
	switch (m->type) {
	case FLPR_MSG_RING_TEST_REPORT:
		report_rec(m, ud);
		break;
	case FLPR_MSG_RING_STALL_ACK:
		stall_rec(m, ud);
		break;
	case FLPR_MSG_STRESS_PONG:
		pong_rec(m, ud);
		break;
	case FLPR_MSG_FAULT_HANG_ACK:
		hang_ack_rec(m, ud);
		break;
	default:
		break;
	}
}

ZTEST(flpr_handshake, test_ring_control_dispatch_exact)
{
	zassert_ok(flpr_handshake_init(), "init");

	memset(ring_records, 0, sizeof(ring_records));

	flpr_handshake_register_ring_handlers(reset_rec, consumer_rec, &ring_dispatch_marker);
	flpr_handshake_register_diag_handlers(diag_router, &ring_dispatch_marker);

	struct flpr_msg m1 = {.type = FLPR_MSG_RING_RESET_ACK,
			      .version = FLPR_PROTOCOL_VERSION,
			      .seq = 1,
			      .data = 42};
	struct flpr_msg m2 = {.type = FLPR_MSG_RING_CONSUMER,
			      .version = FLPR_PROTOCOL_VERSION,
			      .seq = 2,
			      .data = 43};
	struct flpr_msg m3 = {.type = FLPR_MSG_RING_TEST_REPORT,
			      .version = FLPR_PROTOCOL_VERSION,
			      .seq = 3,
			      .data = 44};
	struct flpr_msg m4 = {.type = FLPR_MSG_RING_STALL_ACK,
			      .version = FLPR_PROTOCOL_VERSION,
			      .seq = 4,
			      .data = 45};
	struct flpr_msg m5 = {.type = FLPR_MSG_STRESS_PONG,
			      .version = FLPR_PROTOCOL_VERSION,
			      .seq = 5,
			      .data = 46};
	struct flpr_msg m6 = {.type = FLPR_MSG_FAULT_HANG_ACK,
			      .version = FLPR_PROTOCOL_VERSION,
			      .seq = 0,
			      .data = 47};

	fake_ipc_receive(&m1, sizeof(m1));
	fake_ipc_receive(&m2, sizeof(m2));
	fake_ipc_receive(&m3, sizeof(m3));
	fake_ipc_receive(&m4, sizeof(m4));
	fake_ipc_receive(&m5, sizeof(m5));
	fake_ipc_receive(&m6, sizeof(m6));

	zassert_equal(ring_records[0].calls, 1, "reset_ack dispatched");
	zassert_equal(ring_records[1].calls, 1, "consumer dispatched");
	zassert_equal(ring_records[2].calls, 1, "report dispatched");
	zassert_equal(ring_records[3].calls, 1, "stall_ack dispatched");
	zassert_equal(ring_records[4].calls, 1, "stress pong dispatched");
	zassert_equal(ring_records[5].calls, 1, "fault-hang ack dispatched");

	zassert_equal(ring_records[0].msg->type, FLPR_MSG_RING_RESET_ACK, "reset_ack msg type");
	zassert_equal(ring_records[0].msg->data, 42, "reset_ack msg data");
	zassert_equal(ring_records[1].msg->type, FLPR_MSG_RING_CONSUMER, "consumer msg type");
	zassert_equal(ring_records[1].msg->data, 43, "consumer msg data");
	zassert_equal(ring_records[2].msg->type, FLPR_MSG_RING_TEST_REPORT, "report msg type");
	zassert_equal(ring_records[2].msg->seq, 3, "report msg seq");
	zassert_equal(ring_records[3].msg->type, FLPR_MSG_RING_STALL_ACK, "stall_ack msg type");
	zassert_equal(ring_records[3].msg->data, 45, "stall_ack msg data");
	zassert_equal(ring_records[4].msg->type, FLPR_MSG_STRESS_PONG, "pong msg type");
	zassert_equal(ring_records[4].msg->data, 46, "pong msg data");
	zassert_equal(ring_records[5].msg->type, FLPR_MSG_FAULT_HANG_ACK, "hang ack msg type");
	for (int i = 0; i < 4; i++) {
		zassert_equal(ring_records[i].ud, &ring_dispatch_marker,
			      "user_data passed through [%d]", i);
	}
}

/* Diagnostic messages with NO registered diag handler are silently
 * dropped (identical to the pre-R8 inert acceptance state — not
 * counted as unknown). */
ZTEST(flpr_handshake, test_diag_messages_dropped_when_unregistered)
{
	zassert_ok(flpr_handshake_init(), "init");

	/* No diag handler registered (register_diag_handlers never called). */
	fake_ipc_receive(&(struct flpr_msg){.type = FLPR_MSG_STRESS_PONG,
					    .version = FLPR_PROTOCOL_VERSION,
					    .seq = 0,
					    .data = 1},
			 sizeof(struct flpr_msg));
	fake_ipc_receive(&(struct flpr_msg){.type = FLPR_MSG_RING_TEST_REPORT,
					    .version = FLPR_PROTOCOL_VERSION,
					    .seq = 0xD100,
					    .data = 1},
			 sizeof(struct flpr_msg));
	fake_ipc_receive(&(struct flpr_msg){.type = FLPR_MSG_RING_STALL_ACK,
					    .version = FLPR_PROTOCOL_VERSION,
					    .seq = 0,
					    .data = 1},
			 sizeof(struct flpr_msg));
	fake_ipc_receive(&(struct flpr_msg){.type = FLPR_MSG_FAULT_HANG_ACK,
					    .version = FLPR_PROTOCOL_VERSION,
					    .seq = 0,
					    .data = 1},
			 sizeof(struct flpr_msg));

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.err_unknown, 0, "diag messages with no handler are not unknown");
}

ZTEST(flpr_handshake, test_unexpected_allowed_types_no_unknown)
{
	hs_ready_flow(42);

	fake_ipc_receive(&(struct flpr_msg){.type = FLPR_MSG_STRESS_PING,
					    .version = FLPR_PROTOCOL_VERSION,
					    .seq = 0,
					    .data = 0},
			 sizeof(struct flpr_msg));
	fake_ipc_receive(&(struct flpr_msg){.type = FLPR_MSG_READY_ACK,
					    .version = FLPR_PROTOCOL_VERSION,
					    .seq = 0,
					    .data = 0},
			 sizeof(struct flpr_msg));

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.err_unknown, 0, "allowed types are not unknown");
}

/* ── Heartbeat and health ────────────────────────────────────────── */

ZTEST(flpr_handshake, test_no_send_before_ready_acked)
{
	zassert_ok(flpr_handshake_init(), "init");
	/* session available, but acked=false. */
	flpr_handshake_test_heartbeat_once();
	zassert_equal(fake_ipc_sent_type_count(FLPR_MSG_HEARTBEAT), 0, "no heartbeat sent");

	/* No session at all. */
	flpr_handshake_test_reset();
	fake_ipc_reset();
	flpr_handshake_test_heartbeat_once();
	zassert_equal(fake_ipc_sent_type_count(FLPR_MSG_HEARTBEAT), 0, "no heartbeat sent");
}

ZTEST(flpr_handshake, test_heartbeat_success_increments_tx_seq)
{
	hs_ready_flow(42);

	flpr_handshake_test_heartbeat_once();

	const struct flpr_msg *hb = NULL;
	for (int i = (int)fake_ipc_sent_count() - 1; i >= 0; i--) {
		if (fake_ipc_sent_at(i)->type == FLPR_MSG_HEARTBEAT) {
			hb = fake_ipc_sent_at(i);
			break;
		}
	}
	zassert_not_null(hb, "heartbeat sent");
	zassert_equal(hb->seq, 0, "first heartbeat carries tx_seq 0");

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.tx_seq, 1, "tx_seq incremented after successful send");
}

ZTEST(flpr_handshake, test_heartbeat_send_failure_no_tx_increment)
{
	hs_ready_flow(42);
	fake_ipc_set_send_result(-EIO);

	flpr_handshake_test_heartbeat_once();

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.tx_seq, 0, "tx_seq NOT incremented on send failure");
	zassert_equal(st.err_send, 1, "err_send incremented");
}

static int health_cb_calls;

static void health_cb(void *ud)
{
	(void)ud;
	health_cb_calls++;
	/* Lock probe: a nested spinlock would assert. */
	struct flpr_status st;
	flpr_handshake_get_status(&st);
}

ZTEST(flpr_handshake, test_health_transition_callback_once_outside_lock)
{
	hs_ready_flow(42);

	health_cb_calls = 0;
	flpr_handshake_register_health_cb(health_cb, NULL);

	/* Peer healthy but stale: last heartbeat 6 s ago (> 5 s miss bound). */
	uint32_t stale_ms = k_uptime_get_32() - 6U * FLPR_HEARTBEAT_INTERVAL_MS;
	flpr_handshake_test_set_peer_state(true, true, true, true, stale_ms);

	flpr_handshake_test_heartbeat_once();

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_false(st.healthy, "transitioned to unhealthy");
	zassert_equal(st.rx_missed_total, 1, "one transition counted");
	zassert_equal(health_cb_calls, 1, "callback invoked exactly once");

	/* Repeated unhealthy checks do not duplicate the callback. */
	flpr_handshake_test_heartbeat_once();
	flpr_handshake_get_status(&st);
	zassert_equal(st.rx_missed_total, 1, "no second transition");
	zassert_equal(health_cb_calls, 1, "callback not duplicated");
}

static int health_cb2_calls;

static void health_cb2(void *ud)
{
	(void)ud;
	health_cb2_calls++;
}

ZTEST(flpr_handshake, test_health_cb_unregister)
{
	hs_ready_flow(42);

	health_cb2_calls = 0;
	flpr_handshake_register_health_cb(health_cb2, NULL);
	flpr_handshake_register_health_cb(NULL, NULL); /* unregister */

	uint32_t stale_ms = k_uptime_get_32() - 6U * FLPR_HEARTBEAT_INTERVAL_MS;
	flpr_handshake_test_set_peer_state(true, true, true, true, stale_ms);
	flpr_handshake_test_heartbeat_once();

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_false(st.healthy, "still transitions");
	zassert_equal(health_cb2_calls, 0, "unregistered callback not invoked");
}

ZTEST(flpr_handshake, test_work_reschedules_and_teardown_cancels)
{
	hs_ready_flow(42);
	zassert_equal(flpr_handshake_test_hb_start_requests(), 1, "start recorded");

	flpr_handshake_test_heartbeat_once();
	zassert_equal(flpr_handshake_test_hb_reschedules(), 1, "one reschedule recorded");
	zassert_true(flpr_handshake_test_work_pending(),
		     "reschedule pending at documented interval");

	/* Teardown cancels the pending reschedule (verified by reset). */
	flpr_handshake_test_reset();
	zassert_false(flpr_handshake_test_work_pending(), "teardown cancelled the work");
}

/* ── Lifecycle / public API ──────────────────────────────────────── */

ZTEST(flpr_handshake, test_unbound_preserves_lifetime_fields)
{
	hs_ready_flow(42);
	flpr_handshake_test_heartbeat_once(); /* tx_seq 1 */
	fake_ipc_receive(&(struct flpr_msg){.type = FLPR_MSG_HEARTBEAT,
					    .version = FLPR_PROTOCOL_VERSION,
					    .seq = 9,
					    .data = 0},
			 sizeof(struct flpr_msg)); /* rx_seq 9 */

	struct flpr_status before;
	flpr_handshake_get_status(&before);
	zassert_equal(before.tx_seq, 1, "tx_seq set up");
	zassert_equal(before.rx_seq, 9, "rx_seq set up");

	fake_ipc_unbind();

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	/* Volatile state cleared. */
	zassert_false(st.ready, "ready cleared");
	zassert_false(st.acked, "acked cleared");
	zassert_false(st.healthy, "healthy cleared");
	zassert_equal(st.tx_seq, 0, "tx_seq cleared");
	zassert_equal(st.rx_seq, 0, "rx_seq cleared");
	zassert_equal(st.rx_last_ms, 0, "rx_last_ms cleared");
	/* Lifetime counters and last epoch preserved. */
	zassert_equal(st.ready_count, before.ready_count, "ready_count preserved");
	zassert_equal(st.reboot_count, before.reboot_count, "reboot_count preserved");
	zassert_equal(st.err_send, before.err_send, "err_send preserved");
	zassert_equal(st.rx_missed_total, before.rx_missed_total, "rx_missed_total preserved");
	zassert_equal(st.epoch, before.epoch, "last epoch preserved");
	/* Session unavailable. */
	zassert_equal(flpr_handshake_wait_new_ready(41, K_NO_WAIT), -ECANCELED,
		      "session unavailable after unbind");
}

ZTEST(flpr_handshake, test_disconnect_drains_and_propagates)
{
	hs_ready_flow(42);
	zassert_equal(flpr_handshake_test_bound_sem_count(), 1, "bound token");
	zassert_equal(flpr_handshake_test_new_ready_sem_count(), 1, "new-ready token");

	zassert_ok(flpr_handshake_disconnect(), "disconnect");

	zassert_equal(flpr_handshake_test_bound_sem_count(), 0, "bound drained");
	zassert_equal(flpr_handshake_test_new_ready_sem_count(), 0, "new-ready drained");
	zassert_equal(flpr_handshake_wait_bound(K_NO_WAIT), -EBUSY, "bound wait fails");
	zassert_equal(flpr_handshake_wait_new_ready(41, K_NO_WAIT), -ECANCELED,
		      "session unavailable");
	zassert_false(fake_ipc_endpoint_registered(), "endpoint deregistered");

	/* Deregister failure propagates. */
	hs_ready_flow(42);
	fake_ipc_set_deregister_result(-EIO);
	zassert_equal(flpr_handshake_disconnect(), -EIO, "deregister failure propagated");
}

ZTEST(flpr_handshake, test_reconnect_drains_and_marks_available)
{
	hs_ready_flow(42);
	zassert_ok(flpr_handshake_disconnect(), "disconnect");

	zassert_ok(flpr_handshake_reconnect(), "reconnect");
	zassert_equal(flpr_handshake_test_bound_sem_count(), 1, "bound after re-register");
	zassert_ok(flpr_handshake_wait_bound(K_NO_WAIT), "bound wait succeeds");
	/* Session available again: wait_new_ready no longer -ECANCELED;
	 * no new-ready token exists (same epoch, ACK path not re-run). */
	zassert_equal(flpr_handshake_wait_new_ready(42, K_NO_WAIT), -EBUSY,
		      "available, no token (K_NO_WAIT) → -EBUSY");

	/* Register failure: session stays unavailable. */
	zassert_ok(flpr_handshake_disconnect(), "disconnect 2");
	fake_ipc_set_register_result(-ENOMEM);
	zassert_equal(flpr_handshake_reconnect(), -ENOMEM, "register failure propagated");
	zassert_equal(flpr_handshake_wait_new_ready(41, K_NO_WAIT), -ECANCELED,
		      "session unavailable after failed reconnect");
}

ZTEST(flpr_handshake, test_wait_bound_semantics)
{
	hs_ready_flow(42);

	zassert_ok(flpr_handshake_wait_bound(K_MSEC(100)), "first wait");
	zassert_ok(flpr_handshake_wait_bound(K_MSEC(100)), "reposted token");

	/* Timeout after drain. */
	zassert_ok(flpr_handshake_disconnect(), "disconnect drains");
	zassert_equal(flpr_handshake_wait_bound(K_MSEC(50)), -EAGAIN, "timeout");
}

ZTEST(flpr_handshake, test_wait_new_ready_semantics)
{
	/* Unavailable → -ECANCELED. */
	zassert_equal(flpr_handshake_wait_new_ready(0, K_MSEC(10)), -ECANCELED,
		      "unavailable session");

	/* Already-new epoch succeeds without waiting. */
	hs_ready_flow(42);
	zassert_ok(flpr_handshake_wait_new_ready(41, K_NO_WAIT), "already-new epoch");

	/* Same epoch times out (token drained above). */
	zassert_equal(flpr_handshake_wait_new_ready(42, K_MSEC(100)), -EAGAIN,
		      "same epoch times out");
}

ZTEST(flpr_handshake, test_send_msg_null_and_error_accounting)
{
	zassert_ok(flpr_handshake_init(), "init");

	zassert_equal(flpr_handshake_send_msg(NULL), -EINVAL, "null rejected");
	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.err_send, 0, "null rejection counts no send error");

	struct flpr_msg msg = {.type = FLPR_MSG_RING_PRODUCER,
			       .version = FLPR_PROTOCOL_VERSION,
			       .seq = 0,
			       .data = 0};
	zassert_ok(flpr_handshake_send_msg(&msg), "send ok");

	fake_ipc_set_send_result(-EIO);
	zassert_equal(flpr_handshake_send_msg(&msg), -EIO, "send failure propagates");
	flpr_handshake_get_status(&st);
	zassert_equal(st.err_send, 1, "exactly one send error counted");
}

static int unreg_calls;

static void unreg_rec(const struct flpr_msg *m, void *ud)
{
	(void)m;
	(void)ud;
	unreg_calls++;
}

ZTEST(flpr_handshake, test_ring_handler_unregistration)
{
	zassert_ok(flpr_handshake_init(), "init");

	unreg_calls = 0;
	flpr_handshake_register_ring_handlers(unreg_rec, NULL, NULL);

	fake_ipc_receive(&(struct flpr_msg){.type = FLPR_MSG_RING_RESET_ACK,
					    .version = FLPR_PROTOCOL_VERSION,
					    .seq = 0,
					    .data = 1},
			 sizeof(struct flpr_msg));
	zassert_equal(unreg_calls, 1, "handler invoked");

	flpr_handshake_register_ring_handlers(NULL, NULL, NULL);
	fake_ipc_receive(&(struct flpr_msg){.type = FLPR_MSG_RING_RESET_ACK,
					    .version = FLPR_PROTOCOL_VERSION,
					    .seq = 0,
					    .data = 2},
			 sizeof(struct flpr_msg));
	zassert_equal(unreg_calls, 1, "handler not invoked after unregister");

	/* Diagnostic slot unregisters independently. */
	flpr_handshake_register_diag_handlers(unreg_rec, NULL);
	fake_ipc_receive(&(struct flpr_msg){.type = FLPR_MSG_RING_TEST_REPORT,
					    .version = FLPR_PROTOCOL_VERSION,
					    .seq = 0,
					    .data = 3},
			 sizeof(struct flpr_msg));
	zassert_equal(unreg_calls, 2, "diag handler invoked");

	flpr_handshake_register_diag_handlers(NULL, NULL);
	fake_ipc_receive(&(struct flpr_msg){.type = FLPR_MSG_RING_TEST_REPORT,
					    .version = FLPR_PROTOCOL_VERSION,
					    .seq = 0,
					    .data = 4},
			 sizeof(struct flpr_msg));
	zassert_equal(unreg_calls, 2, "diag handler not invoked after unregister");
}

ZTEST(flpr_handshake, test_endpoint_error_callback_safe)
{
	zassert_ok(flpr_handshake_init(), "init");

	fake_ipc_error(NULL);
	fake_ipc_error("simulated backend error");
}

/* ── Stress / fault hang ─────────────────────────────────────────── */

ZTEST_SUITE(flpr_handshake, NULL, NULL, hs_setup, hs_teardown, NULL);

/* ── R1: validation counters under concurrent status reads ──────────
 * ep_received now validates under flpr_lock (it mutates err_len /
 * err_version that get_status reads).  A reader thread polls status
 * while the main thread injects invalid messages; the counter pair must
 * stay monotonic and end exact. */

static K_THREAD_STACK_DEFINE(rd_stack, 2048);
static struct k_thread rd_thread;
static K_SEM_DEFINE(rd_done_sem, 0, 1);
static bool rd_stop;
static bool rd_monotonic_violation;
static uint32_t rd_last_sum;

static void status_reader_worker(void *u1, void *u2, void *u3)
{
	(void)u1;
	(void)u2;
	(void)u3;

	while (!rd_stop) {
		struct flpr_status st;

		flpr_handshake_get_status(&st);
		uint32_t sum = st.err_len + st.err_version;

		if (sum < rd_last_sum) {
			rd_monotonic_violation = true;
		}
		rd_last_sum = sum;
	}
	k_sem_give(&rd_done_sem);
}

ZTEST(flpr_handshake, test_validation_counters_consistent_under_concurrent_status_reads)
{
	zassert_ok(flpr_handshake_init(), "init");

	k_sem_reset(&rd_done_sem);
	rd_stop = false;
	rd_monotonic_violation = false;
	rd_last_sum = 0;
	k_thread_create(&rd_thread, rd_stack, K_THREAD_STACK_SIZEOF(rd_stack),
			status_reader_worker, NULL, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

	/* Inject a bounded mix of short/oversized/wrong-version messages
	 * while the reader polls. */
	uint8_t short_msg[4] = {0};
	uint8_t big_msg[16] = {0};
	struct flpr_msg bad_ver = {.type = FLPR_MSG_READY, .version = FLPR_PROTOCOL_VERSION - 1};

	for (int i = 0; i < 50; i++) {
		fake_ipc_receive(short_msg, sizeof(short_msg));
		fake_ipc_receive(big_msg, sizeof(big_msg));
		fake_ipc_receive(&bad_ver, sizeof(bad_ver));
	}

	rd_stop = true;
	zassert_true(k_sem_take(&rd_done_sem, K_MSEC(5000)) == 0, "reader joined");
	zassert_false(rd_monotonic_violation, "counter pair never torn/decreasing");

	struct flpr_status st;
	flpr_handshake_get_status(&st);
	zassert_equal(st.err_len, 100, "err_len exact (50 short + 50 oversized)");
	zassert_equal(st.err_version, 50, "err_version exact");
}
