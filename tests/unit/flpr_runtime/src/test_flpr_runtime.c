/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the FLPR runtime restart manager — real production
 * source execution (T1A).
 *
 * This suite compiles src/flpr_runtime.c with FLPR_RUNTIME_NATIVE_TEST,
 * executing the actual nRF54 restart body against test-provided VPR
 * register storage, source/execution arrays, a mock handshake transport,
 * and time/cache/barrier hooks.  No copied restart algorithm is used.
 *
 * The mock handshake models FLPR reboot semantics: a successful
 * disconnect arms a reboot and wait_new_ready() only succeeds once the
 * mock FLPR epoch differs from the snapshot epoch.
 */
#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>

#include "flpr_runtime.h"
#include "flpr_runtime_hooks.h"
#include "mock_flpr_handshake.h"
#include "mock_nrf_vpr.h"
#include "mock_state.h"
#include "flpr_ring.h" /* production CRC for expected values */

/* ── Expected DMCONTROL masks (same field constants as production) ── */

#define EXPECTED_RESET_ASSERT                                                                      \
	((VPR_DEBUGIF_DMCONTROL_NDMRESET_Active << VPR_DEBUGIF_DMCONTROL_NDMRESET_Pos) |           \
	 (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos))

#define EXPECTED_RESET_RELEASE                                                                     \
	((VPR_DEBUGIF_DMCONTROL_NDMRESET_Inactive << VPR_DEBUGIF_DMCONTROL_NDMRESET_Pos) |         \
	 (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos))

#define SUCCESS_EVENTS                                                                             \
	FLPR_RT_EV_SNAPSHOT, FLPR_RT_EV_SRC_CRC, FLPR_RT_EV_DISCONNECT, FLPR_RT_EV_STOP_CPURUN,    \
		FLPR_RT_EV_ASSERT_RESET, FLPR_RT_EV_COPY, FLPR_RT_EV_CACHE_FLUSH_BARRIERS,         \
		FLPR_RT_EV_EXEC_CRC, FLPR_RT_EV_INITPC, FLPR_RT_EV_RECONNECT,                      \
		FLPR_RT_EV_SET_CPURUN, FLPR_RT_EV_RELEASE_RESET, FLPR_RT_EV_WAIT_BOUND,            \
		FLPR_RT_EV_WAIT_READY, FLPR_RT_EV_SUCCESS

/* ── Helpers ────────────────────────────────────────────────────── */

static void rt_setup(void *fixture)
{
	(void)fixture;
	flpr_rt_test_reset_all();
	mock_hs_reset();
	mock_reset();
}

/* Deterministic source image. */
static void rt_fill_source(void)
{
	for (uint32_t i = 0; i < FLPR_RT_TEST_IMAGE_SIZE; i++) {
		flpr_rt_test_source[i] = (uint8_t)(i * 31U + 7U);
	}
}

/* Standard success arrangement: epoch 42, reboot armed by disconnect. */
static void rt_success_arrange(void)
{
	rt_fill_source();
	mock_hs_set_epoch(42);
	zassert_ok(flpr_runtime_init(), "init");
}

static void rt_assert_success_events(void)
{
	static const enum flpr_rt_test_event expected[] = {SUCCESS_EVENTS};
	const uint32_t n = sizeof(expected) / sizeof(expected[0]);

	zassert_equal(flpr_rt_test_event_count(), n, "event count");
	for (uint32_t i = 0; i < n; i++) {
		zassert_equal(flpr_rt_test_event_at(i), expected[i], "event[%u]", i);
	}
}

/* ── Init ──────────────────────────────────────────────────────── */

ZTEST(flpr_runtime, test_init_success)
{
	zassert_ok(flpr_runtime_init(), "init");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.state, FLPR_RUNTIME_IDLE, "state");
	zassert_equal(s.requests, 0, "requests");
}

ZTEST(flpr_runtime, test_init_idempotent)
{
	zassert_ok(flpr_runtime_init(), "first init");
	zassert_ok(flpr_runtime_init(), "second init");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.state, FLPR_RUNTIME_IDLE, "state");
	zassert_equal(s.requests, 0, "requests unchanged");
}

ZTEST(flpr_runtime, test_restart_before_init_enodev)
{
	int ret = flpr_runtime_restart(1000);
	zassert_equal(ret, -ENODEV, "restart before init must be -ENODEV");
}

/* ── Full success path ──────────────────────────────────────────── */

ZTEST(flpr_runtime, test_full_success_event_order)
{
	rt_success_arrange();

	zassert_ok(flpr_runtime_restart(1000), "restart");
	rt_assert_success_events();
}

ZTEST(flpr_runtime, test_exactly_two_dmcontrol_writes)
{
	rt_success_arrange();

	zassert_ok(flpr_runtime_restart(1000), "restart");

	zassert_equal(mock_vpr.dmcontrol_write_count, 2, "exactly 2 DMCONTROL writes");
	zassert_equal(mock_vpr.dmcontrol_writes[0], EXPECTED_RESET_ASSERT, "write 0 = assert");
	zassert_equal(mock_vpr.dmcontrol_writes[1], EXPECTED_RESET_RELEASE, "write 1 = release");
}

ZTEST(flpr_runtime, test_dmactive_enabled_in_both_masks)
{
	rt_success_arrange();

	zassert_ok(flpr_runtime_restart(1000), "restart");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);

	uint32_t after_assert =
		(s.readbacks.dmcontrol_after_assert & VPR_DEBUGIF_DMCONTROL_DMACTIVE_Msk) >>
		VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos;
	uint32_t after_release =
		(s.readbacks.dmcontrol_after_release & VPR_DEBUGIF_DMCONTROL_DMACTIVE_Msk) >>
		VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos;

	zassert_equal(after_assert, VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled,
		      "DMACTIVE enabled after assert");
	zassert_equal(after_release, VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled,
		      "DMACTIVE enabled after release");
}

ZTEST(flpr_runtime, test_cpurun_readbacks)
{
	rt_success_arrange();

	zassert_ok(flpr_runtime_restart(1000), "restart");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);

	zassert_false(s.readbacks.cpurun_after_assert, "CPURUN false after stop+assert");
	zassert_true(s.readbacks.cpurun_after_set, "CPURUN true before release");
	zassert_equal(mock_vpr.cpurun_set_count, 2, "cpurun set false then true");
}

ZTEST(flpr_runtime, test_source_copied_exactly)
{
	rt_success_arrange();

	zassert_ok(flpr_runtime_restart(1000), "restart");

	zassert_equal(memcmp(flpr_rt_test_source, flpr_rt_test_exec, FLPR_RT_TEST_IMAGE_SIZE), 0,
		      "execution image must equal source image");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.reload_bytes, FLPR_RT_TEST_IMAGE_SIZE, "reload_bytes");
}

ZTEST(flpr_runtime, test_cache_flush_and_barriers_after_copy)
{
	rt_success_arrange();

	zassert_ok(flpr_runtime_restart(1000), "restart");

	/* Hook observability. */
	zassert_equal(flpr_rt_test_cache_flush_count(), 1, "one cache flush");
	zassert_equal(flpr_rt_test_barrier_count(FLPR_RT_BARRIER_DSB), 1, "one DSB");
	zassert_equal(flpr_rt_test_barrier_count(FLPR_RT_BARRIER_ISB), 1, "one ISB");
	zassert_equal(flpr_rt_test_busy_wait_count(), 3, "three busy waits");
	zassert_equal(flpr_rt_test_sleep_count(), 1, "one sleep");

	/* Order: copy → flush+barriers → execution CRC. */
	const uint32_t n = flpr_rt_test_event_count();
	uint32_t copy_pos = 0;
	uint32_t flush_pos = 0;
	uint32_t crc_pos = 0;
	for (uint32_t i = 0; i < n; i++) {
		enum flpr_rt_test_event ev = flpr_rt_test_event_at(i);
		if (ev == FLPR_RT_EV_COPY) {
			copy_pos = i;
		}
		if (ev == FLPR_RT_EV_CACHE_FLUSH_BARRIERS) {
			flush_pos = i;
		}
		if (ev == FLPR_RT_EV_EXEC_CRC) {
			crc_pos = i;
		}
	}
	zassert_true(copy_pos < flush_pos && flush_pos < crc_pos,
		     "flush+barriers must occur after copy and before execution CRC");
}

ZTEST(flpr_runtime, test_matching_crc_success)
{
	rt_success_arrange();

	zassert_ok(flpr_runtime_restart(1000), "restart");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);

	uint32_t expected = flpr_ring_crc32(flpr_rt_test_source, FLPR_RT_TEST_IMAGE_SIZE);
	zassert_equal(s.source_crc, expected, "source_crc is real CRC of source image");
	zassert_equal(s.execution_crc, expected, "execution_crc equals source CRC");
	zassert_equal(s.last_errno, 0, "last_errno");
	zassert_equal(s.state, FLPR_RUNTIME_IDLE, "state IDLE after success");
}

ZTEST(flpr_runtime, test_injected_corruption_crc_eio)
{
	rt_success_arrange();
	flpr_rt_test_set_corrupt_after_copy(true);

	int ret = flpr_runtime_restart(1000);
	zassert_equal(ret, -EIO, "CRC mismatch must produce -EIO");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.failed_stage, FLPR_STAGE_CRC_VERIFY, "stage CRC_VERIFY");
	zassert_equal(s.fail_count, 1, "fail_count");
	zassert_equal(s.last_errno, -EIO, "last_errno");
	zassert_equal(s.state, FLPR_RUNTIME_UNAVAILABLE, "state UNAVAILABLE");
	zassert_not_equal(s.source_crc, s.execution_crc, "execution CRC must differ");

	/* Copy happened, flush happened, but no launch edges after CRC. */
	zassert_equal(mock_vpr.dmcontrol_write_count, 1, "only assert write");
	zassert_false(mock_vpr.cpurun, "CPURUN stays stopped");
}

/* ── Failure stages ──────────────────────────────────────────────── */

ZTEST(flpr_runtime, test_disconnect_failure_no_vpr_touch)
{
	rt_success_arrange();
	mock_hs_set_disconnect_result(-EIO);

	int ret = flpr_runtime_restart(1000);
	zassert_equal(ret, -EIO, "disconnect failure propagates");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.failed_stage, FLPR_STAGE_DISCONNECT, "stage DISCONNECT");
	zassert_equal(s.state, FLPR_RUNTIME_UNAVAILABLE, "state UNAVAILABLE");
	zassert_equal(s.fail_count, 1, "fail_count");

	/* VPR must be untouched: no cpurun, no dmcontrol, no initpc. */
	zassert_equal(mock_vpr.cpurun_set_count, 0, "no cpurun writes");
	zassert_equal(mock_vpr.dmcontrol_write_count, 0, "no dmcontrol writes");
	zassert_equal(mock_vpr.initpc_set_count, 0, "no initpc writes");

	/* Events: snapshot, source CRC, disconnect stage reached — no failure stop. */
	zassert_equal(flpr_rt_test_event_count(), 3, "event count");
	zassert_equal(flpr_rt_test_event_at(0), FLPR_RT_EV_SNAPSHOT, "ev 0");
	zassert_equal(flpr_rt_test_event_at(1), FLPR_RT_EV_SRC_CRC, "ev 1");
	zassert_equal(flpr_rt_test_event_at(2), FLPR_RT_EV_DISCONNECT, "ev 2");
}

ZTEST(flpr_runtime, test_reconnect_failure_reset_held)
{
	rt_success_arrange();
	mock_hs_set_reconnect_result(-EIO);

	int ret = flpr_runtime_restart(1000);
	zassert_equal(ret, -EIO, "reconnect failure propagates");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.failed_stage, FLPR_STAGE_RECONNECT, "stage RECONNECT");
	zassert_equal(s.state, FLPR_RUNTIME_UNAVAILABLE, "state UNAVAILABLE");

	/* CPURUN stopped (1 write: false); reset still asserted. */
	zassert_equal(mock_vpr.cpurun_set_count, 1, "only the stop write");
	zassert_false(mock_vpr.cpurun, "CPURUN false");
	zassert_equal(mock_vpr.dmcontrol_write_count, 1, "only the assert write");
	zassert_equal(mock_vpr.dmcontrol_writes[0], EXPECTED_RESET_ASSERT, "reset held");
	zassert_equal(s.readbacks.dmcontrol_after_assert, EXPECTED_RESET_ASSERT,
		      "readback shows held reset");
}

ZTEST(flpr_runtime, test_wait_bound_failure_stops_cpurun)
{
	rt_success_arrange();
	mock_hs_set_wait_bound_result(-EAGAIN);

	int ret = flpr_runtime_restart(1000);
	zassert_equal(ret, -EAGAIN, "wait bound failure propagates");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.failed_stage, FLPR_STAGE_WAIT_BOUND, "stage WAIT_BOUND");
	zassert_equal(s.state, FLPR_RUNTIME_UNAVAILABLE, "state UNAVAILABLE");

	/* stop(false) + start(true) + failure stop(false). */
	zassert_equal(mock_vpr.cpurun_set_count, 3, "cpurun set false,true,false");
	zassert_false(mock_vpr.cpurun, "CPURUN stopped on failure");

	/* Release happened before waiting. */
	zassert_equal(mock_vpr.dmcontrol_write_count, 2, "assert + release");
	zassert_equal(mock_vpr.dmcontrol_writes[1], EXPECTED_RESET_RELEASE, "released");

	/* Events end with failure stop. */
	static const enum flpr_rt_test_event expected[] = {
		FLPR_RT_EV_SNAPSHOT,
		FLPR_RT_EV_SRC_CRC,
		FLPR_RT_EV_DISCONNECT,
		FLPR_RT_EV_STOP_CPURUN,
		FLPR_RT_EV_ASSERT_RESET,
		FLPR_RT_EV_COPY,
		FLPR_RT_EV_CACHE_FLUSH_BARRIERS,
		FLPR_RT_EV_EXEC_CRC,
		FLPR_RT_EV_INITPC,
		FLPR_RT_EV_RECONNECT,
		FLPR_RT_EV_SET_CPURUN,
		FLPR_RT_EV_RELEASE_RESET,
		FLPR_RT_EV_WAIT_BOUND,
		FLPR_RT_EV_FAILURE_STOP,
	};
	const uint32_t n = sizeof(expected) / sizeof(expected[0]);
	zassert_equal(flpr_rt_test_event_count(), n, "event count");
	for (uint32_t i = 0; i < n; i++) {
		zassert_equal(flpr_rt_test_event_at(i), expected[i], "event[%u]", i);
	}
}

ZTEST(flpr_runtime, test_wait_ready_failure_stops_cpurun)
{
	rt_success_arrange();
	mock_hs_set_reboot_on_disconnect(false); /* same epoch → wait fails */

	int ret = flpr_runtime_restart(1000);
	zassert_equal(ret, -EAGAIN, "wait ready failure propagates");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.failed_stage, FLPR_STAGE_WAIT_READY, "stage WAIT_READY");
	zassert_equal(s.state, FLPR_RUNTIME_UNAVAILABLE, "state UNAVAILABLE");
	zassert_equal(mock_vpr.cpurun_set_count, 3, "cpurun stopped on failure");
	zassert_false(mock_vpr.cpurun, "CPURUN stopped");
	zassert_equal(flpr_rt_test_event_at(flpr_rt_test_event_count() - 1),
		      FLPR_RT_EV_FAILURE_STOP, "last event is failure stop");
}

ZTEST(flpr_runtime, test_changed_epoch_required_for_wait_ready)
{
	rt_success_arrange();
	mock_hs_set_reboot_on_disconnect(false);

	/* No reboot → same epoch → wait_new_ready must fail. */
	int ret = flpr_runtime_restart(1000);
	zassert_equal(ret, -EAGAIN, "same-epoch wait must fail");
	zassert_equal(mock_hs_last_prev_epoch(), 42, "waited on snapshot epoch 42");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.failed_stage, FLPR_STAGE_WAIT_READY, "stage WAIT_READY");

	/* Reboot armed by disconnect → changed epoch → success. */
	mock_hs_set_reboot_on_disconnect(true);
	zassert_ok(flpr_runtime_restart(1000), "restart with changed epoch");

	flpr_runtime_get_status(&s);
	zassert_equal(s.previous_epoch, 42, "snapshot epoch");
	zassert_equal(s.new_epoch, 43, "mock FLPR rebooted to 43");
	zassert_equal(mock_hs_last_prev_epoch(), 42, "waited on snapshot epoch again");
}

/* ── Mutex busy ──────────────────────────────────────────────────── */

ZTEST(flpr_runtime, test_mutex_busy_ebusy)
{
	rt_success_arrange();

	flpr_runtime_test_hold_mutex();

	int ret = flpr_runtime_restart(0);
	zassert_equal(ret, -EBUSY, "restart while busy must be -EBUSY");

	/* Release before reading status: get_status() takes the same mutex. */
	flpr_runtime_test_release_mutex();

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.busy_reject, 1, "busy_reject incremented");
	zassert_equal(s.requests, 0, "requests NOT incremented");

	zassert_ok(flpr_runtime_restart(1000), "restart after release");
	flpr_runtime_get_status(&s);
	zassert_equal(s.requests, 1, "request counted after busy window");
	zassert_equal(s.busy_reject, 1, "busy_reject unchanged");
}

/* ── Counters and accounting ─────────────────────────────────────── */

ZTEST(flpr_runtime, test_counters_exact)
{
	rt_success_arrange();

	zassert_ok(flpr_runtime_restart(1000), "restart 1");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.requests, 1, "requests");
	zassert_equal(s.success_count, 1, "successes");
	zassert_equal(s.fail_count, 0, "fails");
	zassert_equal(s.last_errno, 0, "last_errno");
	zassert_equal(s.reload_bytes, FLPR_RT_TEST_IMAGE_SIZE, "reload_bytes");
	zassert_equal(s.source_crc, flpr_ring_crc32(flpr_rt_test_source, FLPR_RT_TEST_IMAGE_SIZE),
		      "source_crc");
	zassert_equal(s.execution_crc, s.source_crc, "execution_crc");
	zassert_equal(s.readbacks.initpc_after_set, (uint32_t)(uintptr_t)flpr_rt_test_exec,
		      "INITPC = execution base");
	zassert_equal(s.readbacks.dmcontrol_before_release, EXPECTED_RESET_ASSERT,
		      "DMCONTROL before release = assert mask");

	/* Failure resets the success markers. */
	mock_hs_set_disconnect_result(-EIO);
	zassert_equal(flpr_runtime_restart(1000), -EIO, "restart 2 fails");
	flpr_runtime_get_status(&s);
	zassert_equal(s.requests, 2, "requests");
	zassert_equal(s.success_count, 1, "successes");
	zassert_equal(s.fail_count, 1, "fails");
	zassert_equal(s.last_errno, -EIO, "last_errno");

	mock_hs_set_disconnect_result(0);
	zassert_ok(flpr_runtime_restart(1000), "restart 3");
	flpr_runtime_get_status(&s);
	zassert_equal(s.requests, 3, "requests");
	zassert_equal(s.success_count, 2, "successes");
	zassert_equal(s.fail_count, 1, "fails");
	zassert_equal(s.last_errno, 0, "last_errno");
}

ZTEST(flpr_runtime, test_duration_accounting_includes_failures)
{
	rt_success_arrange();

	/* Success: one 200 ms sleep. */
	zassert_ok(flpr_runtime_restart(1000), "restart 1");
	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.total_duration_ms, 200, "total after success");
	zassert_equal(s.max_duration_ms, 200, "max after success");

	/* Wait-bound failure also reaches the 200 ms sleep. */
	mock_hs_set_wait_bound_result(-EAGAIN);
	zassert_equal(flpr_runtime_restart(1000), -EAGAIN, "restart 2 fails");
	flpr_runtime_get_status(&s);
	zassert_equal(s.total_duration_ms, 400, "failed attempt counted");
	zassert_equal(s.max_duration_ms, 200, "max unchanged");

	/* Disconnect failure adds zero duration. */
	mock_hs_set_wait_bound_result(0);
	mock_hs_clear_wait_bound_result();
	mock_hs_set_disconnect_result(-EIO);
	zassert_equal(flpr_runtime_restart(1000), -EIO, "restart 3 fails early");
	flpr_runtime_get_status(&s);
	zassert_equal(s.total_duration_ms, 400, "early failure adds nothing");
	zassert_equal(s.max_duration_ms, 200, "max unchanged");

	/* Longer failed attempt must extend max_duration_ms. */
	mock_hs_set_disconnect_result(0);
	flpr_rt_test_set_sleep_advance(500);
	mock_hs_set_wait_bound_result(-EAGAIN);
	zassert_equal(flpr_runtime_restart(1000), -EAGAIN, "restart 4 fails late");
	flpr_runtime_get_status(&s);
	zassert_equal(s.total_duration_ms, 900, "total includes 500 ms failure");
	zassert_equal(s.max_duration_ms, 500, "max includes failed attempt");

	/* Success afterwards still tracks correctly. */
	mock_hs_set_wait_bound_result(0);
	mock_hs_clear_wait_bound_result();
	zassert_ok(flpr_runtime_restart(1000), "restart 5 succeeds");
	flpr_runtime_get_status(&s);
	zassert_equal(s.total_duration_ms, 1400, "total after long success");
	zassert_equal(s.max_duration_ms, 500, "max unchanged");

	flpr_rt_test_set_sleep_advance(0);
	zassert_ok(flpr_runtime_restart(1000), "restart 6 succeeds");
	flpr_runtime_get_status(&s);
	zassert_equal(s.total_duration_ms, 1600, "total after normal success");
	zassert_equal(s.max_duration_ms, 500, "max still longest attempt");
}

ZTEST(flpr_runtime, test_second_run_early_failure_no_success_stage)
{
	rt_success_arrange();

	zassert_ok(flpr_runtime_restart(1000), "restart 1 succeeds");

	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);
	zassert_equal(s.failed_stage, FLPR_STAGE_SUCCESS, "success stage recorded");

	/* Second restart fails at disconnect: must NOT retain SUCCESS stage. */
	mock_hs_set_disconnect_result(-EIO);
	zassert_equal(flpr_runtime_restart(1000), -EIO, "restart 2 fails");
	flpr_runtime_get_status(&s);
	zassert_equal(s.failed_stage, FLPR_STAGE_DISCONNECT,
		      "failed_stage reset to DISCONNECT, not stale SUCCESS");
}

/* ── Status API ──────────────────────────────────────────────────── */

ZTEST(flpr_runtime, test_null_status_output_harmless)
{
	flpr_runtime_get_status(NULL);

	struct flpr_runtime_status s;
	memset(&s, 0xAA, sizeof(s));
	flpr_runtime_get_status(&s);
	zassert_equal(s.state, FLPR_RUNTIME_IDLE, "status filled");
}

ZTEST_SUITE(flpr_runtime, NULL, NULL, rt_setup, NULL, NULL);
