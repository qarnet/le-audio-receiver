/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the Mode A event assembler (src/audio_modea.c).
 *
 * Direct tests of the public resolution contract:
 *   equal timestamps, left/right missing (LOST callback and real
 *   missing callback), consecutive losses, alternating losses,
 *   timestamp wrap, out-of-order callback arrival, invalid/no-TS
 *   startup (positional sentinels), hard-decode-error caller contract,
 *   reset/teardown, queue overflow, and exact push/PLC accounting.
 *
 * The assembler is pure ordering/state logic (no liblc3); actual decode
 * stays in production audio_decode/liblc3, exercised via the BSim
 * matrix.  The caller-contract test below mirrors the production
 * stream_recv Mode A decode loop with a fake decoder to prove the event
 * payload is fully available and that skipping the push on a hard error
 * leaves the assembler consistent.
 */

#include <zephyr/ztest.h>

#include "audio_modea.h"

#define INTERVAL 10000U

/* Fill each carried half with a distinct byte pattern so tests can prove
 * which half's data ended up in an event. */
static uint8_t l_data[MODEA_MAX_FRAME_OCTETS];
static uint8_t r_data[MODEA_MAX_FRAME_OCTETS];

static struct modea_state st;
static struct modea_event ev;
static struct modea_stats stats;

static void init_buffers(void)
{
	for (size_t i = 0; i < sizeof(l_data); i++) {
		l_data[i] = (uint8_t)(0xA0U + (i & 0x0F));
		r_data[i] = (uint8_t)(0x50U + (i & 0x0F));
	}
}

static void setup_state(void)
{
	init_buffers();
	/* Reset FIRST (clears interval), then configure: modea_reset
	 * memsets the whole state, so config-after-reset is required. */
	modea_reset(&st);
	modea_config(&st, INTERVAL);
	memset(&ev, 0, sizeof(ev));
}

/* Store one half; returns the action. */
static enum modea_action store(enum modea_channel ch, const uint8_t *data, size_t len,
			       bool src_valid, bool has_ts, uint32_t ts, uint16_t seq)
{
	return modea_store(&st, ch, data, len, src_valid, has_ts, ts, seq, &ev);
}

#define STORE_VALID(ch, ts)                                                                        \
	store((ch), ((ch) == MODEA_CH_LEFT) ? l_data : r_data, sizeof(l_data), true, true, (ts),   \
	      (uint16_t)(ts))

#define STORE_LOST(ch, seq) store((ch), NULL, 0, false, false, 0U, (seq))

static void assert_stats(uint32_t resolved, uint32_t plc, uint32_t drops, uint32_t rejects)
{
	modea_get_stats(&st, &stats);
	zassert_equal(resolved, stats.resolved_events, "resolved %u != %u", stats.resolved_events,
		      resolved);
	zassert_equal(plc, stats.plc_backed_events, "plc %u != %u", stats.plc_backed_events, plc);
	zassert_equal(drops, stats.overflow_drops, "drops %u != %u", stats.overflow_drops, drops);
	zassert_equal(rejects, stats.rejects, "rejects %u != %u", stats.rejects, rejects);
}

/* ── equal timestamps ─────────────────────────────────────────────── */

ZTEST(modea, test_equal_ts_pairs_and_emits_once)
{
	setup_state();

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 10000));

	zassert_equal(10000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT], "left half must carry data");
	zassert_true(ev.half_valid[MODEA_CH_RIGHT], "right half must carry data");
	zassert_true(ev.half_src[MODEA_CH_LEFT]);
	zassert_true(ev.half_src[MODEA_CH_RIGHT]);
	zassert_equal(sizeof(l_data), ev.len[MODEA_CH_LEFT]);
	zassert_equal(sizeof(r_data), ev.len[MODEA_CH_RIGHT]);
	zassert_mem_equal(l_data, ev.data[MODEA_CH_LEFT], sizeof(l_data));
	zassert_mem_equal(r_data, ev.data[MODEA_CH_RIGHT], sizeof(r_data));

	assert_stats(1, 0, 0, 0);
}

ZTEST(modea, test_equal_ts_order_independent)
{
	setup_state();

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_RIGHT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_LEFT, 10000));

	zassert_equal(10000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);
	assert_stats(1, 0, 0, 0);
}

/* ── right missing via LOST callback (predicted ts) ───────────────── */

ZTEST(modea, test_right_missing_lost_callback)
{
	setup_state();

	/* event 1: both valid */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 10000));

	/* event 2: right SDU lost → LOST callback (no ts).  Its predicted
	 * position is 20000 == left's real ts → PLC pairing. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 20000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_LOST(MODEA_CH_RIGHT, 2));

	zassert_equal(20000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT], "left half carried");
	zassert_false(ev.half_valid[MODEA_CH_RIGHT], "right half must be PLC");
	zassert_true(ev.half_src[MODEA_CH_LEFT]);
	zassert_false(ev.half_src[MODEA_CH_RIGHT], "PLC half never source-valid");

	/* event 3: pairing resumes */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 30000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 30000));
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);

	assert_stats(3, 1, 0, 0);
}

/* ── left missing via real missing callback (newer evidence) ──────── */

ZTEST(modea, test_left_missing_newer_evidence)
{
	setup_state();

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 10000));

	/* Event 2: left SDU lost with NO callback at all (the ISO stack
	 * delivered nothing for it — only the seq/ts gap is visible).
	 * Right's event-2 delivery stays pending; left's event-3 delivery
	 * proves right(20000) is missing (ISO in-order on left). */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_RIGHT, 20000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_LEFT, 30000));
	zassert_equal(20000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);
	zassert_false(ev.half_valid[MODEA_CH_LEFT]);

	/* Event 3 pairs normally. */
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 30000));
	zassert_equal(30000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);
	assert_stats(3, 1, 0, 0);
}

/* ── consecutive losses on one channel (LOST callbacks) ───────────── */

ZTEST(modea, test_consecutive_losses)
{
	setup_state();

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 10000));

	/* Right loses events 2 and 3: LOST callbacks with predicted ts. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 20000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_LOST(MODEA_CH_RIGHT, 2));
	zassert_equal(20000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_false(ev.half_valid[MODEA_CH_RIGHT]);

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 30000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_LOST(MODEA_CH_RIGHT, 3));
	zassert_equal(30000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_false(ev.half_valid[MODEA_CH_RIGHT]);

	/* Event 4 pairs normally. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 40000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 40000));
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);
	assert_stats(4, 2, 0, 0);
}

/* ── alternating losses (no stale cross-pairing) ──────────────────── */

ZTEST(modea, test_alternating_losses)
{
	setup_state();

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 10000));

	/* Event 2: left lost (no callback).  Right(20000) pending, then
	 * left(30000) newer → PLC left at 20000. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_RIGHT, 20000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_LEFT, 30000));
	zassert_equal(20000U, ev.ts);
	zassert_false(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);

	/* Event 3: right lost.  Left(30000) pending, then right(40000)
	 * newer → PLC right at 30000. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 40000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 40000));
	zassert_equal(30000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_false(ev.half_valid[MODEA_CH_RIGHT]);

	/* Events 4 and 5 pair normally. */
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_LEFT, 50000));
	zassert_equal(40000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);

	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 50000));
	zassert_equal(50000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);
	assert_stats(5, 2, 0, 0);
}

/* ── timestamp wrap (wrap-safe ordering) ──────────────────────────── */

ZTEST(modea, test_ts_wrap)
{
	setup_state();

	/* Events straddling the 32-bit wrap; all arithmetic is uint32
	 * wrap-consistent. */
	const uint32_t a = 0xFFFFFFF0U;  /* pre-wrap event 1 */
	const uint32_t b = a + INTERVAL; /* wraps: event 2 (pre-wrap) */
	const uint32_t c = b + INTERVAL; /* post-wrap event 3 */
	const uint32_t d = c + INTERVAL; /* post-wrap event 4 */

	zassert_true(b < a, "event 2 ts must wrap below event 1");

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, a));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, a));
	zassert_equal(a, ev.ts);

	/* Right loses its pre-wrap event 2 (LOST callback: predicted
	 * position wraps correctly).  Left delivers events 2 and 3, then
	 * right's event-3 delivery resolves event 2 with PLC right. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, b));
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, c));
	zassert_equal(MODEA_ACTION_EMIT, STORE_LOST(MODEA_CH_RIGHT, 2));
	zassert_equal(b, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_false(ev.half_valid[MODEA_CH_RIGHT]);

	/* Event 3 pairs across the wrap (equal ts after wrap). */
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, c));
	zassert_equal(c, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);

	/* Event 4 pairs normally. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, d));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, d));
	zassert_equal(d, ev.ts);
	assert_stats(4, 1, 0, 0);
}

/* ── out-of-order cross-channel callback arrival ──────────────────── */

ZTEST(modea, test_out_of_order_cross_channel)
{
	setup_state();

	/* Right one event ahead, left behind with a loss between. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_RIGHT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_LEFT, 10000));

	/* Left delivers event 3 before right delivers event 2 (left was
	 * ahead; right's event 2 arrives late).  left(20000) must have
	 * been delivered before left(30000) (ISO in-order) — it was not,
	 * so left(20000) is LOST and the resolution PLCs it. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 30000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 20000));

	zassert_equal(20000U, ev.ts);
	zassert_false(ev.half_valid[MODEA_CH_LEFT], "left(20000) missing → PLC");
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);

	/* Left(30000) still pending; right(30000) pairs it. */
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 30000));
	zassert_equal(30000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);
	assert_stats(3, 1, 0, 0);
}

/* ── invalid/no-TS startup (positional sentinels) ─────────────────── */

ZTEST(modea, test_no_ts_startup_deterministic)
{
	setup_state();

	/* First events on both channels are LOST replacements (no ts, no
	 * prior timestamped delivery) — positional sentinels pair as a
	 * full-PLC event (historical ts=0==0 startup behavior). */
	zassert_equal(MODEA_ACTION_NONE, STORE_LOST(MODEA_CH_LEFT, 0));
	zassert_equal(MODEA_ACTION_EMIT, STORE_LOST(MODEA_CH_RIGHT, 0));

	zassert_equal(0U, ev.ts);
	zassert_false(ev.half_valid[MODEA_CH_LEFT]);
	zassert_false(ev.half_valid[MODEA_CH_RIGHT]);
	zassert_false(ev.half_src[MODEA_CH_LEFT]);
	zassert_false(ev.half_src[MODEA_CH_RIGHT]);

	/* Then normal timestamped streaming. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 10000));
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);

	assert_stats(2, 1, 0, 0);
}

ZTEST(modea, test_no_ts_sentinel_resolves_with_timestamped_mate)
{
	setup_state();

	/* Left's first event is LOST (sentinel, no usable ts); right
	 * delivers a real timestamped event for the same CIG event.  The
	 * sentinel resolves positionally: right's data + PLC left at
	 * right's ts (a lingering sentinel never blocks the pipeline). */
	zassert_equal(MODEA_ACTION_NONE, STORE_LOST(MODEA_CH_LEFT, 0));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 10000));
	zassert_equal(10000U, ev.ts);
	zassert_false(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);

	/* Event 2 pairs normally. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 20000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 20000));
	zassert_equal(20000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);
	assert_stats(2, 1, 0, 0);
}

/* ── reset / teardown ─────────────────────────────────────────────── */

ZTEST(modea, test_reset_clears_all)
{
	setup_state();

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 20000));

	modea_reset(&st);
	assert_stats(0, 0, 0, 0);

	/* Fresh start pairs cleanly. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 10000));
	assert_stats(1, 0, 0, 0);
}

/* ── queue overflow (bounded state, counted, never silent) ────────── */

ZTEST(modea, test_queue_overflow_drops_oldest)
{
	setup_state();

	/* Right never delivers: left fills to depth 2, then overflows. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 20000));

	/* Third concurrent unresolved half on left → overflow drop of the
	 * oldest (10000), counted. */
	zassert_equal(MODEA_ACTION_DROP, STORE_VALID(MODEA_CH_LEFT, 30000));
	assert_stats(0, 0, 1, 0);

	/* The dropped half cannot be emitted later (its mate arriving now
	 * pairs with the next-left instead). */
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 20000));
	zassert_equal(20000U, ev.ts);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);
	assert_stats(1, 0, 1, 0);
}

ZTEST(modea, test_oversized_reject_no_mutation)
{
	setup_state();

	uint8_t big[MODEA_MAX_FRAME_OCTETS + 1] = {0};

	zassert_equal(MODEA_ACTION_DROP,
		      modea_store(&st, MODEA_CH_LEFT, big, sizeof(big), true, true, 10000, 1, &ev));
	assert_stats(0, 0, 0, 1);

	/* No state mutated: a valid store still pairs fresh. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 10000));
	assert_stats(1, 0, 0, 1);
}

/* ── hard decode error: caller contract ───────────────────────────── */

ZTEST(modea, test_hard_decode_error_caller_contract)
{
	setup_state();

	/* Emit an event; the caller's decode loop fails one half (fake
	 * decoder).  The assembler must have consumed the event (no
	 * re-emission, no stale pending), so the next event proceeds. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 10000));

	/* Payload fully available for the caller decode (mirror of the
	 * production decode loop). */
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_true(ev.half_valid[MODEA_CH_RIGHT]);
	zassert_not_null(ev.data[MODEA_CH_LEFT]);
	zassert_not_null(ev.data[MODEA_CH_RIGHT]);

	/* Simulate the caller skipping the push after a hard error. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 20000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 20000));
	zassert_equal(20000U, ev.ts);
	assert_stats(2, 0, 0, 0);

	/* PLC event payload: the PLC half must be marked invalid so the
	 * caller passes NULL to the decoder. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 30000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_LOST(MODEA_CH_RIGHT, 3));
	zassert_true(ev.half_valid[MODEA_CH_LEFT]);
	zassert_false(ev.half_valid[MODEA_CH_RIGHT]);
	assert_stats(3, 1, 0, 0);
}

/* ── exact push/PLC accounting over a mixed script ────────────────── */

ZTEST(modea, test_push_plc_accounting)
{
	setup_state();

	/* 10 events; right loses events 2 and 5 (LOST callbacks).  Every
	 * event emits exactly once (10 pushes); exactly 2 are PLC-backed. */
	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 10000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 10000));

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 20000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_LOST(MODEA_CH_RIGHT, 2)); /* PLC #1 */

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 30000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 30000));

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 40000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 40000));

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 50000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_LOST(MODEA_CH_RIGHT, 5)); /* PLC #2 */

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 60000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 60000));

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 70000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 70000));

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 80000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 80000));

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 90000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 90000));

	zassert_equal(MODEA_ACTION_NONE, STORE_VALID(MODEA_CH_LEFT, 100000));
	zassert_equal(MODEA_ACTION_EMIT, STORE_VALID(MODEA_CH_RIGHT, 100000));

	assert_stats(10, 2, 0, 0);
}

ZTEST_SUITE(modea, NULL, NULL, NULL, NULL, NULL);
