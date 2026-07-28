/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for flpr_handshake lifecycle — protocol helpers from
 * flpr_protocol.h (header-only, no IPC dependency).
 *
 * The full disconnect/reconnect/bound/READY semaphore lifecycle
 * requires the IPC subsystem and FLPR running; that is tested
 * via hardware acceptance (see phase6-stage4a results).
 */
#include <zephyr/ztest.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "flpr_protocol.h"

/* ── READY new-epoch detection ───────────────────────────────── */

ZTEST(flpr_lifecycle, test_peer_handle_ready_new_epoch)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	bool is_new = flpr_peer_handle_ready(&p, 42);
	zassert_true(is_new, "first READY is new epoch");
	zassert_equal(p.epoch, 42);
	zassert_equal(p.ready_count, 1);
	zassert_equal(p.reboot_count, 1);
	zassert_true(p.ready);
	zassert_true(p.healthy);
}

ZTEST(flpr_lifecycle, test_peer_handle_ready_same_epoch)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_handle_ready(&p, 42);
	bool is_new = flpr_peer_handle_ready(&p, 42);
	zassert_false(is_new, "same epoch is not new");
	zassert_equal(p.ready_count, 2);
	zassert_equal(p.reboot_count, 1);
}

ZTEST(flpr_lifecycle, test_peer_handle_ready_different_epoch)
{
	struct flpr_peer p;
	memset(&p, 0, sizeof(p));

	flpr_peer_handle_ready(&p, 42);
	bool is_new = flpr_peer_handle_ready(&p, 99);
	zassert_true(is_new, "different epoch is new");
	zassert_equal(p.epoch, 99);
	zassert_equal(p.ready_count, 2);
	zassert_equal(p.reboot_count, 2);
}

/* ── Peer reset ─────────────────────────────────────────────── */

ZTEST(flpr_lifecycle, test_peer_reset)
{
	struct flpr_peer p;
	memset(&p, 0xff, sizeof(p)); /* fill with non-zero */
	p.ready = true;
	p.healthy = true;

	flpr_peer_reset(&p);

	zassert_false(p.ready);
	zassert_false(p.healthy);
	zassert_equal(p.epoch, 0);
	zassert_equal(p.ready_count, 0);
	zassert_equal(p.reboot_count, 0);
}

/* ── Partial reset (simulating unbound with preserved counters) ─ */

ZTEST(flpr_lifecycle, test_unbound_preserves_lifetime)
{
	struct flpr_peer p;

	/* Simulate a running peer with some history. */
	memset(&p, 0, sizeof(p));
	p.ready_count = 5;
	p.reboot_count = 3;
	p.err_send = 10;
	p.rx_missed_total = 2;
	p.epoch = 42;
	p.ready = true;
	p.acked = true;
	p.healthy = true;
	p.rx_seq = 100;
	p.tx_seq = 200;

	/* Save lifetime counters (simulating ep_unbound). */
	uint32_t saved_ready_count = p.ready_count;
	uint32_t saved_reboot_count = p.reboot_count;
	uint32_t saved_err_send = p.err_send;
	uint32_t saved_rx_missed_total = p.rx_missed_total;
	uint32_t saved_epoch = p.epoch;

	/* Partial reset: clear volatile, preserve lifetime. */
	memset(&p, 0, sizeof(p));
	p.ready_count = saved_ready_count;
	p.reboot_count = saved_reboot_count;
	p.err_send = saved_err_send;
	p.rx_missed_total = saved_rx_missed_total;
	p.epoch = saved_epoch;

	/* Verify volatile cleared. */
	zassert_false(p.ready);
	zassert_false(p.acked);
	zassert_false(p.healthy);
	zassert_equal(p.rx_seq, 0);
	zassert_equal(p.tx_seq, 0);

	/* Verify lifetime preserved. */
	zassert_equal(p.ready_count, 5);
	zassert_equal(p.reboot_count, 3);
	zassert_equal(p.err_send, 10);
	zassert_equal(p.rx_missed_total, 2);
	zassert_equal(p.epoch, 42);
}

/* ── Stress PONG cookie classification ──────────────────────── */

ZTEST(flpr_lifecycle, test_stress_pong_match)
{
	enum flpr_stress_pong_class c = flpr_classify_stress_pong(5, 5, true);
	zassert_equal(c, FLPR_PONG_MATCH);
}

ZTEST(flpr_lifecycle, test_stress_pong_stale)
{
	enum flpr_stress_pong_class c = flpr_classify_stress_pong(3, 5, true);
	zassert_equal(c, FLPR_PONG_STALE);
}

ZTEST(flpr_lifecycle, test_stress_pong_future)
{
	enum flpr_stress_pong_class c = flpr_classify_stress_pong(7, 5, true);
	zassert_equal(c, FLPR_PONG_FUTURE);
}

ZTEST(flpr_lifecycle, test_stress_pong_inactive)
{
	enum flpr_stress_pong_class c = flpr_classify_stress_pong(5, 5, false);
	zassert_equal(c, FLPR_PONG_INACTIVE);
}

ZTEST_SUITE(flpr_lifecycle, NULL, NULL, NULL, NULL, NULL);
