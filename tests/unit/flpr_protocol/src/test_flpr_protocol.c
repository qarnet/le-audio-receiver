/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for flpr_protocol.h — wire protocol struct, sequence
 * arithmetic, message validation, peer state machine, health,
 * READY/ACK transitions, heartbeat ACK validation, stress PONG
 * cookie classification, peer reset.
 *
 * ALL tests call production helpers (flpr_peer_handle_ready, etc.)
 * — never assign peer fields directly except for setup.
 * Runs on native_sim.
 */

#include <zephyr/ztest.h>
#include <zephyr/sys/__assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "flpr_protocol.h"
#include "flpr_ring_mgr.h" /* for flpr_rate_limit_target_ms */

/* ── Wire message struct ────────────────────────────────────────── */

ZTEST(flpr_protocol, test_msg_size)
{
	zassert_equal(sizeof(struct flpr_msg), 8, "msg size must be 8");
}

ZTEST(flpr_protocol, test_msg_offsets)
{
	struct flpr_msg m;
	zassert_equal((uintptr_t)&m.type - (uintptr_t)&m, 0, "type offset 0");
	zassert_equal((uintptr_t)&m.version - (uintptr_t)&m, 1, "version offset 1");
	zassert_equal((uintptr_t)&m.seq - (uintptr_t)&m, 2, "seq offset 2");
	zassert_equal((uintptr_t)&m.data - (uintptr_t)&m, 4, "data offset 4");
}

/* ── Message validation ─────────────────────────────────────────── */

ZTEST(flpr_protocol, test_validate_ok)
{
	struct flpr_msg m = {.type = 1, .version = FLPR_PROTOCOL_VERSION, .seq = 0, .data = 0};
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));
	zassert_true(flpr_msg_validate(&m, sizeof(m), &p), "valid msg");
	zassert_equal(p.err_len, 0);
	zassert_equal(p.err_version, 0);
}

ZTEST(flpr_protocol, test_validate_short)
{
	struct flpr_msg m = {.type = 1};
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));
	zassert_false(flpr_msg_validate(&m, 3, &p), "short len rejected");
	zassert_equal(p.err_len, 1);
}

ZTEST(flpr_protocol, test_validate_oversize)
{
	struct flpr_msg m = {.type = 1};
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));
	zassert_false(flpr_msg_validate(&m, 12, &p), "oversize rejected");
	zassert_equal(p.err_len, 1);
}

ZTEST(flpr_protocol, test_validate_wrong_version)
{
	struct flpr_msg m = {
		.type = 1,
		.version = FLPR_PROTOCOL_VERSION + 1,
		.seq = 0,
		.data = 0,
	};
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));
	zassert_false(flpr_msg_validate(&m, sizeof(m), &p), "wrong version");
	zassert_equal(p.err_version, 1);
}

ZTEST(flpr_protocol, test_validate_unknown_type_passes)
{
	/* Unknown types pass validate (type check is caller's job). */
	struct flpr_msg m = {.type = 0xFF, .version = FLPR_PROTOCOL_VERSION};
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));
	zassert_true(flpr_msg_validate(&m, sizeof(m), &p), "unknown type OK at validate");
}

/* ── Sequence number arithmetic ─────────────────────────────────── */

ZTEST(flpr_protocol, test_seq_after_monotonic)
{
	zassert_true(flpr_seq_after(5, 4), "5 after 4");
	zassert_false(flpr_seq_after(4, 5), "4 not after 5");
	zassert_false(flpr_seq_after(5, 5), "same not after");
}

ZTEST(flpr_protocol, test_seq_after_wrap)
{
	/* 16-bit wrap: 0 comes after 65535. */
	zassert_true(flpr_seq_after(0, 65535), "0 after 65535 (wrap)");
	zassert_false(flpr_seq_after(65535, 0), "65535 not after 0 (non-wrap)");
	zassert_true(flpr_seq_after(32768, 32767), "32768 after 32767");
	zassert_false(flpr_seq_after(32767, 32768), "32767 before 32768");
}

ZTEST(flpr_protocol, test_seq_diff)
{
	zassert_equal(flpr_seq_diff(5, 4), 1, "diff 5-4");
	zassert_equal(flpr_seq_diff(4, 5), -1, "diff 4-5");
	zassert_equal(flpr_seq_diff(5, 5), 0, "diff same");
}

ZTEST(flpr_protocol, test_seq_diff_wrap)
{
	/* 0 after 65535 → diff = 1. */
	zassert_equal(flpr_seq_diff(0, 65535), 1, "diff wrap 0-65535 = 1");
	/* 65535 after 0 → diff = -1. */
	zassert_equal(flpr_seq_diff(65535, 0), -1, "diff 65535-0 = -1 non-wrap");
}

ZTEST(flpr_protocol, test_seq_gap)
{
	zassert_equal(flpr_seq_gap(5, 4), 1, "gap 5-4");
	zassert_equal(flpr_seq_gap(0, 65535), 1, "gap wrap 0-65535 = 1");
	zassert_equal(flpr_seq_gap(65535, 0), 65535, "gap 65535-0 = 65535");
}

/* ── Sequence tracking: first heartbeat ─────────────────────────── */

ZTEST(flpr_protocol, test_rx_seq_first)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_rx_seq(&p, 42, 1000);

	zassert_equal(p.rx_seq, 42);
	zassert_equal(p.rx_last_ms, 1000);
	zassert_equal(p.rx_lost, 0);
	zassert_equal(p.rx_dup, 0);
	zassert_equal(p.rx_ooo, 0);
}

/* ── Sequence tracking: monotonic advance ───────────────────────── */

ZTEST(flpr_protocol, test_rx_seq_monotonic)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_rx_seq(&p, 10, 1000);
	flpr_peer_rx_seq(&p, 11, 2000);
	flpr_peer_rx_seq(&p, 12, 3000);

	zassert_equal(p.rx_seq, 12);
	zassert_equal(p.rx_lost, 0);
	zassert_equal(p.rx_dup, 0);
	zassert_equal(p.rx_ooo, 0);
	zassert_equal(p.rx_last_ms, 3000);
}

/* ── Sequence tracking: gap ─────────────────────────────────────── */

ZTEST(flpr_protocol, test_rx_seq_gap)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_rx_seq(&p, 10, 1000);
	flpr_peer_rx_seq(&p, 15, 2000); /* gap: 11,12,13,14 missed */

	zassert_equal(p.rx_seq, 15);
	zassert_equal(p.rx_lost, 4); /* 15-10-1 = 4 */
	zassert_equal(p.rx_dup, 0);
	zassert_equal(p.rx_ooo, 0);
}

/* ── Sequence tracking: duplicate ───────────────────────────────── */

ZTEST(flpr_protocol, test_rx_seq_duplicate)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_rx_seq(&p, 10, 1000);
	flpr_peer_rx_seq(&p, 10, 1500); /* duplicate */

	zassert_equal(p.rx_seq, 10); /* baseline unchanged */
	zassert_equal(p.rx_lost, 0);
	zassert_equal(p.rx_dup, 1);
	zassert_equal(p.rx_ooo, 0);
	zassert_equal(p.rx_last_ms, 1500); /* timestamp updated */
}

/* ── Sequence tracking: out-of-order — counted but baseline NOT moved */

ZTEST(flpr_protocol, test_rx_seq_out_of_order)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_rx_seq(&p, 10, 1000);
	/* 5 < 10 → ooo.  Counted but rx_seq baseline stays 10. */
	flpr_peer_rx_seq(&p, 5, 1500);

	zassert_equal(p.rx_seq, 10); /* baseline NOT moved */
	zassert_equal(p.rx_ooo, 1);  /* counted */
	zassert_equal(p.rx_lost, 0);
	zassert_equal(p.rx_dup, 0);
	zassert_equal(p.rx_last_ms, 1000); /* timestamp NOT updated on OOO */
}

/* ── Sequence tracking: OOO then next in-order — no fake huge gap ─ */

ZTEST(flpr_protocol, test_rx_seq_ooo_then_inorder)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_rx_seq(&p, 10, 1000);
	/* Stale packet. */
	flpr_peer_rx_seq(&p, 5, 1500);
	zassert_equal(p.rx_ooo, 1);
	zassert_equal(p.rx_seq, 10); /* baseline preserved */

	/* Next in-order: 11 after 10 → diff=1, no gap. */
	flpr_peer_rx_seq(&p, 11, 3000);

	zassert_equal(p.rx_seq, 11);
	zassert_equal(p.rx_lost, 0); /* no fake huge gap */
	zassert_equal(p.rx_dup, 0);
	zassert_equal(p.rx_ooo, 1); /* OOO count preserved */
	zassert_equal(p.rx_last_ms, 3000);
}

/* ── Sequence tracking: wrap ────────────────────────────────────── */

ZTEST(flpr_protocol, test_rx_seq_wrap)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_rx_seq(&p, 65535, 1000);
	/* 0 after 65535 via wrap → diff=1, in-order advance, no gap. */
	flpr_peer_rx_seq(&p, 0, 2000);

	zassert_equal(p.rx_seq, 0);
	zassert_equal(p.rx_lost, 0);
	zassert_equal(p.rx_dup, 0);
	zassert_equal(p.rx_ooo, 0);
}

/* ── Sequence tracking: gap across wrap ─────────────────────────── */

ZTEST(flpr_protocol, test_rx_seq_gap_across_wrap)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_rx_seq(&p, 65534, 1000);
	/* 2 after 65534 → wrap, diff = 2 - 65534 = 4 (as int16, after wrap). */
	flpr_peer_rx_seq(&p, 2, 2000);

	zassert_equal(p.rx_seq, 2);
	zassert_equal(p.rx_lost, 3); /* gap: 65535,0,1 → 3 missed */
	zassert_equal(p.rx_dup, 0);
	zassert_equal(p.rx_ooo, 0);
}

/* ── Sequence tracking: multiple operations ─────────────────────── */

ZTEST(flpr_protocol, test_rx_seq_complex)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_rx_seq(&p, 10, 1000);
	flpr_peer_rx_seq(&p, 11, 2000); /* monotonic */
	flpr_peer_rx_seq(&p, 11, 2500); /* dup */
	flpr_peer_rx_seq(&p, 15, 3000); /* gap 12,13,14 */
	flpr_peer_rx_seq(&p, 17, 4000); /* gap 16 */
	flpr_peer_rx_seq(&p, 5, 5000);  /* ooo — baseline stays 17 */

	zassert_equal(p.rx_seq, 17); /* baseline NOT moved by OOO */
	zassert_equal(p.rx_lost, 4); /* seq 11→15 gap=3 + 15→17 gap=1 → lost=4 */
	zassert_equal(p.rx_dup, 1);
	zassert_equal(p.rx_ooo, 1);
	zassert_equal(p.rx_last_ms, 4000); /* last IN-ORDER timestamp */
}

/* ── Health check ────────────────────────────────────────────────── */

ZTEST(flpr_protocol, test_health_no_heartbeat_yet)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));
	p.healthy = true;
	/* rx_last_ms = 0 → no heartbeats → keep current state. */
	zassert_true(flpr_peer_check_health(&p, 10000));
	zassert_true(p.healthy);
}

ZTEST(flpr_protocol, test_health_recent)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));
	p.rx_last_ms = 5000;
	p.healthy = true;

	/* Now = 5500: 500 ms since last hb, well within 5 s. */
	zassert_true(flpr_peer_check_health(&p, 5500));
	zassert_true(p.healthy);
	zassert_equal(p.rx_missed_total, 0);
}

ZTEST(flpr_protocol, test_health_stale)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));
	p.rx_last_ms = 5000;
	p.healthy = true;

	/* Now = 11000: 6000 ms since last hb, exceeds 5*1000=5000. */
	zassert_false(flpr_peer_check_health(&p, 11000));
	zassert_false(p.healthy);
	zassert_equal(p.rx_missed_total, 1); /* counted once on transition */
}

/* ── Health: repeated stale polls do NOT inflate rx_missed_total ── */

ZTEST(flpr_protocol, test_health_repeated_stale_poll)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));
	p.rx_last_ms = 5000;
	p.healthy = true;

	/* First stale check: transition healthy→unhealthy, counter +1. */
	zassert_false(flpr_peer_check_health(&p, 11000));
	zassert_equal(p.rx_missed_total, 1);
	zassert_false(p.healthy);

	/* Second stale check: already unhealthy, NO extra increment. */
	zassert_false(flpr_peer_check_health(&p, 17000));
	zassert_equal(p.rx_missed_total, 1);

	/* Third stale check: still no increment. */
	zassert_false(flpr_peer_check_health(&p, 23000));
	zassert_equal(p.rx_missed_total, 1);
}

/* ── Health: new heartbeat restores healthy ─────────────────────── */

ZTEST(flpr_protocol, test_health_heartbeat_restores)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));
	p.rx_last_ms = 5000;
	p.healthy = true;

	/* Go stale. */
	(void)flpr_peer_check_health(&p, 11000);
	zassert_false(p.healthy);
	zassert_equal(p.rx_missed_total, 1);

	/* New heartbeat arrives — updates rx_last_ms via rx_seq. */
	flpr_peer_rx_seq(&p, 20, 12000);

	/* Now check: elapsed = 0 (12000-12000) → healthy restored. */
	zassert_true(flpr_peer_check_health(&p, 12000));
	zassert_true(p.healthy);
	zassert_equal(p.rx_missed_total, 1); /* unchanged on recovery */
}

/* ── READY: new epoch resets heartbeat, seq, ack, health state ──── */

ZTEST(flpr_protocol, test_ready_new_epoch_resets_state)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	/* Prime with some rx/tx state from a previous epoch. */
	p.rx_seq = 100;
	p.rx_lost = 5;
	p.rx_dup = 2;
	p.rx_ooo = 1;
	p.rx_last_ms = 9999;
	p.rx_missed_total = 3;
	p.tx_acked_seq = 50;
	p.healthy = false;

	bool is_new = flpr_peer_handle_ready(&p, 0xBEEF);
	zassert_true(is_new, "first READY is new epoch");
	zassert_equal(p.ready_count, 1);
	zassert_equal(p.reboot_count, 1);
	zassert_equal(p.epoch, 0xBEEF);
	zassert_true(p.ready);
	zassert_true(p.healthy);

	/* All rx/tx tracking reset. */
	zassert_equal(p.rx_seq, 0);
	zassert_equal(p.rx_lost, 0);
	zassert_equal(p.rx_dup, 0);
	zassert_equal(p.rx_ooo, 0);
	zassert_equal(p.rx_last_ms, 0);
	zassert_equal(p.rx_missed_total, 0);
	zassert_equal(p.tx_acked_seq, 0);
}

/* ── READY: same epoch does NOT reset seq/health state ──────────── */

ZTEST(flpr_protocol, test_ready_same_epoch_no_reset)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	/* First READY with epoch 42. */
	flpr_peer_handle_ready(&p, 42);
	zassert_equal(p.ready_count, 1);
	zassert_equal(p.reboot_count, 1);

	/* Build up state. */
	flpr_peer_rx_seq(&p, 10, 5000);
	flpr_peer_rx_seq(&p, 12, 6000);
	/* elapsed = 11001 - 6000 = 5001 > 5000 → stale, rx_missed_total=1 */
	(void)flpr_peer_check_health(&p, 11001);
	p.tx_acked_seq = 8;

	/* Second READY with SAME epoch — duplicate, not a reboot. */
	bool is_new = flpr_peer_handle_ready(&p, 42);
	zassert_false(is_new, "same epoch not new");
	zassert_equal(p.ready_count, 2);
	zassert_equal(p.reboot_count, 1);    /* unchanged */
	zassert_equal(p.rx_seq, 12);         /* preserved */
	zassert_equal(p.rx_lost, 1);         /* preserved (10→12 gap=1) */
	zassert_equal(p.rx_missed_total, 1); /* preserved */
	zassert_equal(p.tx_acked_seq, 8);    /* preserved */
}

/* ── READY: new epoch increments reboot_count ───────────────────── */

ZTEST(flpr_protocol, test_ready_new_epoch_increments_reboot)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_handle_ready(&p, 100);
	zassert_equal(p.reboot_count, 1);
	zassert_equal(p.ready_count, 1);

	flpr_peer_handle_ready(&p, 200); /* new epoch */
	zassert_equal(p.reboot_count, 2);
	zassert_equal(p.ready_count, 2);

	flpr_peer_handle_ready(&p, 300); /* another new epoch */
	zassert_equal(p.reboot_count, 3);
	zassert_equal(p.ready_count, 3);
}

/* ── READY_ACK handler ──────────────────────────────────────────── */

ZTEST(flpr_protocol, test_ready_ack_sets_acked)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_handle_ready_ack(&p, 12345);

	zassert_true(p.acked);
	zassert_true(p.healthy);
	zassert_equal(p.epoch, 12345); /* stores CPUAPP uptime for diagnostics */
}

/* ── Heartbeat ACK: advances in-order ───────────────────────────── */

ZTEST(flpr_protocol, test_heartbeat_ack_advances)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_handle_heartbeat_ack(&p, 5);
	zassert_equal(p.tx_acked_seq, 5);

	flpr_peer_handle_heartbeat_ack(&p, 10);
	zassert_equal(p.tx_acked_seq, 10); /* advances */

	flpr_peer_handle_heartbeat_ack(&p, 20);
	zassert_equal(p.tx_acked_seq, 20); /* advances */
}

/* ── Heartbeat ACK: stale rejected ─────────────────────────────── */

ZTEST(flpr_protocol, test_heartbeat_ack_stale_rejected)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_handle_heartbeat_ack(&p, 20);
	zassert_equal(p.tx_acked_seq, 20);

	/* Stale: 5 < 20 — rejected. */
	flpr_peer_handle_heartbeat_ack(&p, 5);
	zassert_equal(p.tx_acked_seq, 20); /* unchanged */

	/* Same: 20 == 20 — accepted (idempotent). */
	flpr_peer_handle_heartbeat_ack(&p, 20);
	zassert_equal(p.tx_acked_seq, 20); /* unchanged (same value) */
}

/* ── Stress PONG classification ─────────────────────────────────── */

ZTEST(flpr_protocol, test_stress_pong_match)
{
	zassert_equal(flpr_classify_stress_pong(42, 42, true), FLPR_PONG_MATCH);
}

ZTEST(flpr_protocol, test_stress_pong_stale)
{
	/* cookie 41 < expected 42 → stale */
	zassert_equal(flpr_classify_stress_pong(41, 42, true), FLPR_PONG_STALE);
}

ZTEST(flpr_protocol, test_stress_pong_future)
{
	/* cookie 43 > expected 42 → future/unknown */
	zassert_equal(flpr_classify_stress_pong(43, 42, true), FLPR_PONG_FUTURE);

	/* Even with large gap. */
	zassert_equal(flpr_classify_stress_pong(999, 50, true), FLPR_PONG_FUTURE);
}

ZTEST(flpr_protocol, test_stress_pong_inactive)
{
	/* Stress not active → all cookies are INACTIVE. */
	zassert_equal(flpr_classify_stress_pong(42, 42, false), FLPR_PONG_INACTIVE);
	zassert_equal(flpr_classify_stress_pong(0, 0, false), FLPR_PONG_INACTIVE);
}

/* ── Peer reset ─────────────────────────────────────────────────── */

ZTEST(flpr_protocol, test_peer_reset_clears_all)
{
	struct flpr_peer p;

	/* Prime all fields. */
	p.bound = true;
	p.ready = true;
	p.acked = true;
	p.healthy = true;
	p.epoch = 0xDEADBEEF;
	p.ready_count = 5;
	p.reboot_count = 3;
	p.rx_seq = 100;
	p.rx_lost = 7;
	p.rx_dup = 4;
	p.rx_ooo = 2;
	p.rx_last_ms = 55555;
	p.rx_missed_total = 9;
	p.tx_seq = 200;
	p.tx_acked_seq = 150;
	p.err_len = 1;
	p.err_version = 2;
	p.err_unknown = 3;
	p.err_send = 4;

	flpr_peer_reset(&p);

	zassert_false(p.bound);
	zassert_false(p.ready);
	zassert_false(p.acked);
	zassert_false(p.healthy);
	zassert_equal(p.epoch, 0);
	zassert_equal(p.ready_count, 0);
	zassert_equal(p.reboot_count, 0);
	zassert_equal(p.rx_seq, 0);
	zassert_equal(p.rx_lost, 0);
	zassert_equal(p.rx_dup, 0);
	zassert_equal(p.rx_ooo, 0);
	zassert_equal(p.rx_last_ms, 0);
	zassert_equal(p.rx_missed_total, 0);
	zassert_equal(p.tx_seq, 0);
	zassert_equal(p.tx_acked_seq, 0);
	zassert_equal(p.err_len, 0);
	zassert_equal(p.err_version, 0);
	zassert_equal(p.err_unknown, 0);
	zassert_equal(p.err_send, 0);
}

/* ── Error counter accumulation ──────────────────────────────────── */

ZTEST(flpr_protocol, test_error_counters_independent)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	p.err_len = 1;
	p.err_version = 2;
	p.err_unknown = 3;
	p.err_send = 4;

	zassert_equal(p.err_len, 1);
	zassert_equal(p.err_version, 2);
	zassert_equal(p.err_unknown, 3);
	zassert_equal(p.err_send, 4);

	/* Valid msg should not change error counters. */
	struct flpr_msg m = {.type = 1, .version = FLPR_PROTOCOL_VERSION};
	bool ok = flpr_msg_validate(&m, sizeof(m), &p);
	zassert_true(ok);
	zassert_equal(p.err_len, 1);     /* unchanged */
	zassert_equal(p.err_version, 2); /* unchanged */
}

/* ── Protocol version constant ───────────────────────────────────── */

ZTEST(flpr_protocol, test_protocol_version_constant)
{
	zassert_equal(FLPR_PROTOCOL_VERSION, 4U);
}

/* ── Message type constants ──────────────────────────────────────── */

ZTEST(flpr_protocol, test_message_type_values)
{
	zassert_equal(FLPR_MSG_READY, 0x01U);
	zassert_equal(FLPR_MSG_READY_ACK, 0x02U);
	zassert_equal(FLPR_MSG_HEARTBEAT, 0x03U);
	zassert_equal(FLPR_MSG_HEARTBEAT_ACK, 0x04U);
	zassert_equal(FLPR_MSG_STRESS_PING, 0x05U);
	zassert_equal(FLPR_MSG_STRESS_PONG, 0x06U);
}

/* ── Rate limit pacing calculation ───────────────────────────────── */
/* Pure function, zero hardware/kernel deps.  Tests the calculation
 * used by flpr_ring_mgr_test_run_rate to pace production from the
 * test start time.  No per-second windows, no periodic reset. */

ZTEST(flpr_protocol, test_rate_target_first_block)
{
	/* First block at 100 blk/s → target 10 ms. */
	zassert_equal(flpr_rate_limit_target_ms(1, 100), 10ULL);
}

ZTEST(flpr_protocol, test_rate_target_10ms_per_block)
{
	/* 100 blk/s = 10 ms per block. */
	zassert_equal(flpr_rate_limit_target_ms(10, 100), 100ULL);
	zassert_equal(flpr_rate_limit_target_ms(50, 100), 500ULL);
}

ZTEST(flpr_protocol, test_rate_target_100_blocks)
{
	/* 100 blocks at 100/s → 1000 ms = 1 second. */
	zassert_equal(flpr_rate_limit_target_ms(100, 100), 1000ULL);
}

ZTEST(flpr_protocol, test_rate_target_6000_blocks)
{
	/* 6000 blocks at 100/s → 60000 ms = 60 seconds. */
	zassert_equal(flpr_rate_limit_target_ms(6000, 100), 60000ULL);
}

ZTEST(flpr_protocol, test_rate_target_100k_blocks)
{
	/* High count: 100000 blocks at 100/s → 1,000,000 ms (~16.7 min).
	 * Must NOT overflow 32-bit. */
	zassert_equal(flpr_rate_limit_target_ms(100000, 100), 1000000ULL);
}

ZTEST(flpr_protocol, test_rate_target_high_count_overflow_safe)
{
	/* Near 32-bit max: 4,000,000 blocks at 100/s.
	 * sent * 1000 = 4e9 fits in 64-bit. */
	zassert_equal(flpr_rate_limit_target_ms(4000000, 100), 40000000ULL);
}

ZTEST(flpr_protocol, test_rate_target_different_rates)
{
	/* 200 blk/s → 5 ms per block. */
	zassert_equal(flpr_rate_limit_target_ms(200, 200), 1000ULL);
	/* 50 blk/s → 20 ms per block. */
	zassert_equal(flpr_rate_limit_target_ms(50, 50), 1000ULL);
	/* 10 blk/s → 100 ms per block. */
	zassert_equal(flpr_rate_limit_target_ms(10, 10), 1000ULL);
}

ZTEST(flpr_protocol, test_rate_target_zero_rate)
{
	/* rate_per_sec == 0 → no pacing, returns 0. */
	zassert_equal(flpr_rate_limit_target_ms(100, 0), 0ULL);
}

ZTEST(flpr_protocol, test_rate_target_zero_blocks)
{
	/* No blocks sent → 0 ms target. */
	zassert_equal(flpr_rate_limit_target_ms(0, 100), 0ULL);
}

ZTEST(flpr_protocol, test_rate_target_integer_truncation)
{
	/* 1 block at 3 blk/s → floor(1*1000/3) = 333 ms (not 334). */
	zassert_equal(flpr_rate_limit_target_ms(1, 3), 333ULL);
	/* 2 blocks at 3 blk/s → floor(2*1000/3) = 666 ms. */
	zassert_equal(flpr_rate_limit_target_ms(2, 3), 666ULL);
	/* 3 blocks at 3 blk/s → floor(3*1000/3) = 1000 ms exactly. */
	zassert_equal(flpr_rate_limit_target_ms(3, 3), 1000ULL);
}

/* ── Stage 2: stall pack/unpack ──────────────────────────────────── */

ZTEST(flpr_protocol, test_stall_pack_persistent)
{
	/* Duration zero = persistent. */
	uint32_t packed = FLPR_STALL_PACK(0x01, 0);
	zassert_equal(packed, 0x00000001U, "persistent stall: mask in lo 8 bits");
	zassert_equal(FLPR_STALL_MASK(packed), 0x01);
	zassert_equal(FLPR_STALL_DURATION(packed), 0U);
}

ZTEST(flpr_protocol, test_stall_pack_timed)
{
	uint32_t packed = FLPR_STALL_PACK(0x02, 60);
	zassert_equal(FLPR_STALL_MASK(packed), 0x02);
	zassert_equal(FLPR_STALL_DURATION(packed), 60U);
	/* Verify packing: duration<<8 | mask. */
	zassert_equal(packed, (60U << 8) | 0x02U);
}

ZTEST(flpr_protocol, test_stall_pack_max_duration)
{
	uint32_t packed = FLPR_STALL_PACK(0xFF, 0x00FFFFFFU);
	zassert_equal(FLPR_STALL_MASK(packed), 0xFF);
	zassert_equal(FLPR_STALL_DURATION(packed), 0x00FFFFFFU);
}

ZTEST(flpr_protocol, test_stall_pack_multiple_bits)
{
	/* Both consumer and producer stall with timed duration. */
	uint32_t packed = FLPR_STALL_PACK(0x03, 5000);
	zassert_equal(FLPR_STALL_MASK(packed), 0x03);
	zassert_equal(FLPR_STALL_DURATION(packed), 5000U);
}

ZTEST(flpr_protocol, test_stall_pack_clear)
{
	/* Clear all stalls (persistent). */
	uint32_t packed = FLPR_STALL_PACK(0x00, 0);
	zassert_equal(FLPR_STALL_MASK(packed), 0x00);
	zassert_equal(FLPR_STALL_DURATION(packed), 0U);
	zassert_equal(packed, 0x00000000U);
}

ZTEST(flpr_protocol, test_stall_pack_mask_capped)
{
	/* Mask beyond 8 bits is capped to low 8. */
	uint32_t packed = FLPR_STALL_PACK(0x1FF, 0);
	zassert_equal(FLPR_STALL_MASK(packed), 0xFF);
	zassert_equal(FLPR_STALL_DURATION(packed), 0U);
}

ZTEST(flpr_protocol, test_stall_pack_duration_max_constant)
{
	zassert_equal(FLPR_STALL_DURATION_MAX, 0x00FFFFFFU);
}

ZTEST(flpr_protocol, test_stall_unpack_boundary)
{
	/* Verify mask and duration don't overlap. */
	uint32_t packed = FLPR_STALL_PACK(0xAA, 0xBBCCDD);
	uint32_t extracted_mask = FLPR_STALL_MASK(packed);
	uint32_t extracted_dur = FLPR_STALL_DURATION(packed);

	/* Mask: low 8, no overlap into duration bits. */
	zassert_equal(extracted_mask, 0xAA);
	/* Duration: upper 24 bits, masked to 0x00FFFFFF. */
	uint32_t expected_dur = 0xBBCCDD & 0x00FFFFFFU;
	zassert_equal(extracted_dur, expected_dur);
}

ZTEST(flpr_protocol, test_stall_pack_roundtrip)
{
	/* Pack then unpack to verify no data loss. */
	uint32_t packed = FLPR_STALL_PACK(0x55, 12345);
	zassert_equal(FLPR_STALL_MASK(packed), 0x55);
	zassert_equal(FLPR_STALL_DURATION(packed), 12345U);
	zassert_equal(FLPR_STALL_MASK(packed), (uint8_t)(packed & 0xFFU));
	zassert_equal(FLPR_STALL_DURATION(packed), (packed >> 8) & 0x00FFFFFFU);
}

ZTEST_SUITE(flpr_protocol, NULL, NULL, NULL, NULL, NULL);
