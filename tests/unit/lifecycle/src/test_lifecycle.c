/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for stream_lifecycle.c — audio-path gate decision logic.
 * Compiles the production module directly; no code duplication.
 */

#include <zephyr/ztest.h>

#include "stream_lifecycle.h"

/* ── Mode B: single ASE (stereo) ─────────────────────────────────── */

ZTEST(lifecycle, test_mode_b_single_ase_gate_opens)
{
	stream_lifecycle_reset();

	/* One ASE with chan_count=2: gate opens when that ASE starts. */
	stream_lifecycle_sink_configured(0, 2);
	zassert_false(stream_lifecycle_audio_path_is_open(), "gate closed before start");

	bool ret = stream_lifecycle_sink_started(0);
	zassert_true(ret, "started 0 should return true for single-ASE");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate open after single ASE starts");
}

ZTEST(lifecycle, test_mode_b_gate_close_idempotent)
{
	stream_lifecycle_reset();

	stream_lifecycle_sink_configured(0, 2);
	stream_lifecycle_sink_started(0);

	/* First close: returns true (was open). */
	bool first = stream_lifecycle_audio_path_close();
	zassert_true(first, "first close should return true");
	zassert_false(stream_lifecycle_audio_path_is_open(), "gate closed after first close");

	/* Second close: returns false (already closed). */
	bool second = stream_lifecycle_audio_path_close();
	zassert_false(second, "second close should return false");
	zassert_false(stream_lifecycle_audio_path_is_open(), "gate still closed");
}

/* ── Mode A: two mono ASEs ───────────────────────────────────────── */

ZTEST(lifecycle, test_mode_a_first_ase_does_not_open_gate)
{
	stream_lifecycle_reset();

	/* Two mono ASEs: gate stays closed after first starts. */
	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);

	bool ret0 = stream_lifecycle_sink_started(0);
	zassert_false(ret0, "first ASE started should NOT open gate");
	zassert_false(stream_lifecycle_audio_path_is_open(),
		      "gate closed after only one ASE started");
}

ZTEST(lifecycle, test_mode_a_both_ase_open_gate)
{
	stream_lifecycle_reset();

	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);

	stream_lifecycle_sink_started(0);
	bool ret1 = stream_lifecycle_sink_started(1);
	zassert_true(ret1, "second ASE started should open gate");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate open after both ASEs");
}

ZTEST(lifecycle, test_mode_a_order_1_then_0)
{
	stream_lifecycle_reset();

	/* Reverse order: ASE 1 starts first, then ASE 0. */
	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);

	stream_lifecycle_sink_started(1); /* ASE 1 first */
	zassert_false(stream_lifecycle_audio_path_is_open(), "still closed");

	bool ret = stream_lifecycle_sink_started(0); /* ASE 0 second */
	zassert_true(ret, "second ASE completes the pair");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate open");
}

/* ── Mono single-ASE ──────────────────────────────────────────────── */

ZTEST(lifecycle, test_mono_single_ase_opens)
{
	stream_lifecycle_reset();

	/* One ASE with chan_count=1: same decision as Mode B. */
	stream_lifecycle_sink_configured(0, 1);
	bool ret = stream_lifecycle_sink_started(0);
	zassert_true(ret, "mono single-ASE should open gate");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate open");
}

/* ── Reconfigure/teardown ────────────────────────────────────────── */

ZTEST(lifecycle, test_close_clears_l_received_equivalent)
{
	stream_lifecycle_reset();

	/* After gate closes, l_received/r_received must be cleared
	 * so stale halves can't pair.  This test verifies that
	 * audio_path_close() transitions the gate from open→closed;
	 * the caller in bt_bap.c clears l_received/r_received
	 * on the same transition.
	 */
	stream_lifecycle_sink_configured(0, 2);
	stream_lifecycle_sink_started(0);
	zassert_true(stream_lifecycle_audio_path_is_open(), "open");

	stream_lifecycle_audio_path_close();
	zassert_false(stream_lifecycle_audio_path_is_open(), "closed");

	/* Re-start after close: same run. */
	stream_lifecycle_sink_started(0);
	zassert_true(stream_lifecycle_audio_path_is_open(), "re-opened");
}

ZTEST(lifecycle, test_reset_clears_all_state)
{
	stream_lifecycle_reset();

	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);
	stream_lifecycle_sink_started(0);
	stream_lifecycle_sink_started(1);
	zassert_true(stream_lifecycle_audio_path_is_open(), "open");

	stream_lifecycle_reset();
	zassert_false(stream_lifecycle_audio_path_is_open(), "closed after reset");

	/* After reset, re-configure and start: should work again.
	 * This tests reconnect without re-running audio_sink_init().
	 */
	stream_lifecycle_sink_configured(0, 2);
	bool ret = stream_lifecycle_sink_started(0);
	zassert_true(ret, "reconnected single-ASE opens gate");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate open after reconnect");
}

ZTEST(lifecycle, test_reset_erases_previous_config)
{
	stream_lifecycle_reset();

	/* Mode A configured, then reset, then Mode B:
	 * should open with one ASE, not wait for a second. */
	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);
	stream_lifecycle_sink_started(0);
	stream_lifecycle_sink_started(1);

	stream_lifecycle_reset();

	stream_lifecycle_sink_configured(0, 2);
	bool ret = stream_lifecycle_sink_started(0);
	zassert_true(ret, "Mode B after reset should open on one ASE");
	zassert_true(stream_lifecycle_audio_path_is_open());
}

ZTEST(lifecycle, test_reconfigure_same_run_zeroes_state)
{
	stream_lifecycle_reset();

	/* Same run, reconfigure ASE 0: old started state cleared. */
	stream_lifecycle_sink_configured(0, 2);
	stream_lifecycle_sink_started(0);

	/* Reconfigure without reset — should clear started flag. */
	stream_lifecycle_sink_configured(0, 2);
	stream_lifecycle_sink_started(0);
	zassert_true(stream_lifecycle_audio_path_is_open(), "re-open");
}

/* ── Edge: started without configure ─────────────────────────────── */

ZTEST(lifecycle, test_started_without_configure_noop)
{
	stream_lifecycle_reset();

	/* Calling started on an unconfigured sink should not open gate. */
	bool ret = stream_lifecycle_sink_started(0);
	zassert_false(ret, "unconfigured sink should not open gate");
	zassert_false(stream_lifecycle_audio_path_is_open());
}

/* ── Edge: zero chan_count treated as absent ─────────────────────── */

ZTEST(lifecycle, test_zero_chan_count_sink_absent)
{
	stream_lifecycle_reset();

	/* Explicit zero chan_count: gate must NOT open. */
	stream_lifecycle_sink_configured(0, 0);
	bool ret = stream_lifecycle_sink_started(0);
	zassert_false(ret, "zero chan_count should be treated as absent");
	zassert_false(stream_lifecycle_audio_path_is_open(), "gate stays closed");

	/* Reconfigure with nonzero: gate opens normally. */
	stream_lifecycle_sink_configured(0, 2);
	ret = stream_lifecycle_sink_started(0);
	zassert_true(ret, "nonzero chan_count opens gate");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate open");
}

ZTEST(lifecycle, test_out_of_bounds_idx_noop)
{
	stream_lifecycle_reset();

	bool ret = stream_lifecycle_sink_started(99);
	zassert_false(ret, "out-of-bounds index should return false");
	zassert_false(stream_lifecycle_audio_path_is_open());
}

/* ── T5: closed-to-open edge semantics ─────────────────────────────
 * stream_lifecycle_sink_started() returns true only for a
 * closed-to-open transition; duplicate starts must not repeat the
 * one-time open work in the caller (LIFE-003).
 */

ZTEST(lifecycle, test_duplicate_start_single_ase_no_new_edge)
{
	stream_lifecycle_reset();

	stream_lifecycle_sink_configured(0, 2);
	zassert_true(stream_lifecycle_sink_started(0), "first start opens gate");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate open");

	zassert_false(stream_lifecycle_sink_started(0), "duplicate start is not a new edge");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate remains open");
	zassert_false(stream_lifecycle_sink_started(0), "third start still not an edge");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate still open");
}

ZTEST(lifecycle, test_mode_a_duplicate_starts_single_edge)
{
	stream_lifecycle_reset();

	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);

	zassert_false(stream_lifecycle_sink_started(0), "first ASE alone is not an edge");
	zassert_true(stream_lifecycle_sink_started(1), "completing ASE is the edge");
	zassert_false(stream_lifecycle_sink_started(0), "duplicate first ASE not an edge");
	zassert_false(stream_lifecycle_sink_started(1), "duplicate second ASE not an edge");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate open after the pair");
}

ZTEST(lifecycle, test_close_then_start_one_new_edge_then_duplicates_false)
{
	stream_lifecycle_reset();

	stream_lifecycle_sink_configured(0, 2);
	zassert_true(stream_lifecycle_sink_started(0), "first start opens gate");
	zassert_true(stream_lifecycle_audio_path_close(), "close returns was-open");

	zassert_true(stream_lifecycle_sink_started(0), "start after close is one new edge");
	zassert_true(stream_lifecycle_audio_path_is_open(), "reopened");
	zassert_false(stream_lifecycle_sink_started(0), "duplicate after reopen not an edge");
	zassert_false(stream_lifecycle_sink_started(0), "second duplicate still not an edge");
}

ZTEST(lifecycle, test_configure_start_close_reconfigure_start)
{
	stream_lifecycle_reset();

	stream_lifecycle_sink_configured(0, 2);
	zassert_true(stream_lifecycle_sink_started(0), "open");
	zassert_true(stream_lifecycle_audio_path_close(), "close");

	/* Reconfigure clears the started flag; a fresh start must be a
	 * new closed-to-open edge. */
	stream_lifecycle_sink_configured(0, 2);
	zassert_true(stream_lifecycle_sink_started(0), "reconfigured start re-opens");
	zassert_true(stream_lifecycle_audio_path_is_open(), "open again");
	zassert_false(stream_lifecycle_sink_started(0), "duplicate after reconfigure not edge");
}

ZTEST(lifecycle, test_mode_a_close_reconfigure_both_start_pair_edges)
{
	stream_lifecycle_reset();

	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);
	zassert_false(stream_lifecycle_sink_started(0), "partial");
	zassert_true(stream_lifecycle_sink_started(1), "pair completes");
	zassert_true(stream_lifecycle_audio_path_close(), "close");

	/* Both slots were reconfigured (started cleared), so the gate
	 * must again require both starts. */
	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);
	zassert_false(stream_lifecycle_sink_started(0), "partial after reconfigure");
	zassert_true(stream_lifecycle_sink_started(1), "pair completes again");
}

ZTEST(lifecycle, test_release_then_slot_reuse)
{
	stream_lifecycle_reset();

	stream_lifecycle_sink_configured(0, 2);
	zassert_true(stream_lifecycle_sink_started(0), "open");
	zassert_true(stream_lifecycle_audio_path_close(), "close");

	/* Release clears the slot; a start without configure is inert. */
	stream_lifecycle_sink_release(0);
	zassert_false(stream_lifecycle_sink_started(0), "released slot is not configured");
	zassert_false(stream_lifecycle_audio_path_is_open(), "gate stays closed");

	/* Reconfigure reuses the slot with a fresh edge. */
	stream_lifecycle_sink_configured(0, 2);
	zassert_true(stream_lifecycle_sink_started(0), "slot reuse opens again");
}

ZTEST(lifecycle, test_reset_from_closed_and_partial_states)
{
	/* Reset from closed: nothing configured. */
	stream_lifecycle_reset();
	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);
	stream_lifecycle_reset();
	zassert_false(stream_lifecycle_sink_started(0), "nothing configured after reset");
	zassert_false(stream_lifecycle_audio_path_is_open(), "closed");

	/* Reset from partially started: the half-started state must not
	 * survive. */
	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);
	stream_lifecycle_sink_started(0);
	stream_lifecycle_reset();
	zassert_false(stream_lifecycle_sink_started(1), "partial start erased");
	zassert_false(stream_lifecycle_audio_path_is_open(), "still closed");
	stream_lifecycle_sink_configured(0, 2);
	zassert_true(stream_lifecycle_sink_started(0), "fresh single-ASE after reset");
}

ZTEST(lifecycle, test_repeated_close_and_open_cycles)
{
	stream_lifecycle_reset();
	stream_lifecycle_sink_configured(0, 2);

	for (int i = 0; i < 10; i++) {
		zassert_true(stream_lifecycle_sink_started(0), "cycle %d open edge", i);
		zassert_false(stream_lifecycle_sink_started(0), "cycle %d duplicate", i);
		zassert_true(stream_lifecycle_audio_path_close(), "cycle %d close", i);
		zassert_false(stream_lifecycle_audio_path_is_open(), "cycle %d closed", i);
	}
}

/* ── Edge: negative chan_count treated as absent ─────────────────── */

ZTEST(lifecycle, test_negative_chan_count_inert)
{
	stream_lifecycle_reset();

	stream_lifecycle_sink_configured(0, -1);
	zassert_false(stream_lifecycle_sink_started(0), "negative chan_count inert");
	zassert_false(stream_lifecycle_audio_path_is_open(), "gate stays closed");
}

/* ── R1: forced close (shell stop) ───────────────────────────────── */

ZTEST(lifecycle, test_force_close_returns_was_open)
{
	stream_lifecycle_reset();
	stream_lifecycle_sink_configured(0, 2);
	stream_lifecycle_sink_started(0);
	zassert_true(stream_lifecycle_audio_path_is_open(), "open");

	zassert_true(stream_lifecycle_force_close(), "first force close reports was-open");
	zassert_false(stream_lifecycle_audio_path_is_open(), "gate closed");

	zassert_false(stream_lifecycle_force_close(), "second force close reports already closed");
}

ZTEST(lifecycle, test_force_close_blocks_later_starts_for_slot_set)
{
	stream_lifecycle_reset();
	stream_lifecycle_sink_configured(0, 2);
	zassert_true(stream_lifecycle_sink_started(0), "first start opens gate");
	zassert_true(stream_lifecycle_force_close(), "forced close");

	/* Later starts (duplicates / stream-start callbacks) stay closed. */
	zassert_false(stream_lifecycle_sink_started(0), "duplicate start stays closed");
	zassert_false(stream_lifecycle_audio_path_is_open(), "gate stays closed");
	zassert_false(stream_lifecycle_sink_started(0), "third start still closed");
}

ZTEST(lifecycle, test_force_close_mode_a_second_ase_stays_closed)
{
	stream_lifecycle_reset();
	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);
	zassert_false(stream_lifecycle_sink_started(0), "first Mode A ASE alone not open");
	zassert_true(stream_lifecycle_sink_started(1), "pair completes");
	zassert_true(stream_lifecycle_force_close(), "forced close");

	/* start(slot0) -> force_close -> start(slot1) stays closed. */
	stream_lifecycle_reset();
	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);
	zassert_false(stream_lifecycle_sink_started(0), "partial");
	zassert_false(stream_lifecycle_force_close(),
		      "force close while gate already closed reports false");
	zassert_false(stream_lifecycle_sink_started(1),
		      "second ASE start after forced close stays closed");
	zassert_false(stream_lifecycle_audio_path_is_open(), "gate never opens");
}

ZTEST(lifecycle, test_force_close_one_slot_release_does_not_unblock)
{
	stream_lifecycle_reset();
	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);
	stream_lifecycle_sink_started(0);
	zassert_true(stream_lifecycle_sink_started(1), "pair opens");
	zassert_true(stream_lifecycle_force_close(), "forced close");

	/* Releasing only one Mode A slot must not clear the latch while
	 * another slot remains configured. */
	stream_lifecycle_sink_release(0);
	zassert_false(stream_lifecycle_sink_started(1), "other slot start still blocked");
	zassert_false(stream_lifecycle_audio_path_is_open(), "gate stays closed");
}

ZTEST(lifecycle, test_force_close_final_release_and_reconfigure_permits_open)
{
	stream_lifecycle_reset();
	stream_lifecycle_sink_configured(0, 1);
	stream_lifecycle_sink_configured(1, 1);
	stream_lifecycle_sink_started(0);
	stream_lifecycle_sink_started(1);
	zassert_true(stream_lifecycle_force_close(), "forced close");

	/* Release both slots: the latch clears. */
	stream_lifecycle_sink_release(0);
	zassert_false(stream_lifecycle_sink_started(1), "still blocked (slot 1 configured)");
	stream_lifecycle_sink_release(1);

	/* Later reconfigure/start lifecycle can open. */
	stream_lifecycle_sink_configured(0, 2);
	zassert_true(stream_lifecycle_sink_started(0), "fresh lifecycle opens after final release");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate open");
}

ZTEST(lifecycle, test_reset_clears_force_close_latch)
{
	stream_lifecycle_reset();
	stream_lifecycle_sink_configured(0, 2);
	stream_lifecycle_sink_started(0);
	zassert_true(stream_lifecycle_force_close(), "forced close");

	stream_lifecycle_reset();
	zassert_false(stream_lifecycle_audio_path_is_open(), "closed after reset");

	stream_lifecycle_sink_configured(0, 2);
	zassert_true(stream_lifecycle_sink_started(0), "reset permits fresh open");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate open after reset");
}

ZTEST(lifecycle, test_force_close_does_not_affect_ordinary_close)
{
	stream_lifecycle_reset();
	stream_lifecycle_sink_configured(0, 2);
	stream_lifecycle_sink_started(0);

	/* Ordinary close does NOT latch: a later start reopens (LIFE-003
	 * close-then-start edge is preserved). */
	zassert_true(stream_lifecycle_audio_path_close(), "ordinary close");
	zassert_true(stream_lifecycle_sink_started(0), "ordinary close allows reopen");
	zassert_true(stream_lifecycle_audio_path_is_open(), "reopened");
}

/* ── R1 repair: idle force close must not latch a nonexistent set ────
 * stream_lifecycle_force_close() with NO configured slot closes the gate
 * but must leave force_closed=false, so a future first configure/start
 * lifecycle can open.  (Without this, the latch persisted forever — no
 * release/reset boundary would ever clear it.) */

ZTEST(lifecycle, test_idle_force_close_does_not_latch_future_configure)
{
	stream_lifecycle_reset();
	zassert_false(stream_lifecycle_audio_path_is_open(), "idle gate closed");

	zassert_false(stream_lifecycle_force_close(),
		      "idle force close reports already-closed gate");

	/* A future first configured lifecycle must open normally. */
	stream_lifecycle_sink_configured(0, 2);
	zassert_true(stream_lifecycle_sink_started(0), "first configure/start opens");
	zassert_true(stream_lifecycle_audio_path_is_open(), "gate open");

	/* And a configured-slot force close still latches (existing
	 * configured-slot latch semantics retained). */
	zassert_true(stream_lifecycle_force_close(), "configured force close reports was-open");
	zassert_false(stream_lifecycle_sink_started(0), "later start stays closed");
	zassert_false(stream_lifecycle_audio_path_is_open(), "gate stays closed");
}

/* ── Suite entry ─────────────────────────────────────────────────── */

ZTEST_SUITE(lifecycle, NULL, NULL, NULL, NULL, NULL);
