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

/* ── Suite entry ─────────────────────────────────────────────────── */

ZTEST_SUITE(lifecycle, NULL, NULL, NULL, NULL, NULL);
