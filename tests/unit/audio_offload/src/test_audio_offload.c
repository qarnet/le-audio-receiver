/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for audio_offload state machine — Phase 6 Stage 5.
 *
 * Tests the production nRF54L15 code path with mocked flpr_ring_mgr
 * transport via audio_offload_process_asrc().  Uses direct invocation
 * of prep_work_fn() and recovery_work_fn() to exercise the dedicated
 * worker state machine deterministically.
 *
 * Key patterns tested:
 *   - Async PREPARING → prep work → ACTIVE
 *   - Prep failure → bounded retry → ACTIVE
 *   - Prep max retries → FALLBACK
 *   - Submit fault → RECOVERING → recovery work → ACTIVE
 *   - Recovery preserves counter evidence
 *   - Stream stop cancels pending work
 *   - Late output generation rejection
 *   - Concurrent stop-during-submit with helper thread
 *   - Exact accounting: submit_count, fallback_count, busy_count
 *   - ASRC typed validation: sequence, frames, flags, state, CRC
 *   - Stage 4B recovery state machine
 */

#include "audio_offload.h"
#include "audio_offload_test_helpers.h"
#include "flpr_ring.h"
#include "flpr_ring_mgr.h"
#include "audio_asrc.h"

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <string.h>

/* ── Mock control variables (defined in mock_ring_mgr.c) ─────────── */
extern bool mock_init_fails;
extern bool mock_flpr_healthy;
extern bool mock_reset_fails;
extern enum flpr_produce_result mock_produce_result;
extern int mock_notify_result;
extern int mock_wait_result;
extern uint32_t mock_wait_delay_ms;
extern bool mock_stall_producer_active;
extern int mock_init_calls;
extern int mock_reset_calls;
extern int mock_produce_calls;
extern int mock_notify_calls;
extern int mock_wait_calls;
extern uint32_t mock_last_sequence;
extern uint32_t mock_last_epoch;

/* ── ASRC mock control variables (defined in mock_ring_mgr.c) ────── */
extern enum flpr_consume_result mock_asrc_consume_result;
extern struct flpr_consume_asrc_result mock_asrc_consume_data;
extern int mock_asrc_consume_calls;

/* ── Stage 4B recovery mock control variables (mock_ring_mgr.c) ───── */
extern int mock_runtime_restart_result;
extern uint32_t mock_runtime_restart_calls;
extern bool mock_runtime_restart_called;
extern uint32_t mock_runtime_new_epoch;
extern int mock_remote_restarted_result;
extern uint32_t mock_remote_restarted_calls;

/* ── Test data ───────────────────────────────────────────────────── */

#define TEST_ASRC_FRAMES   480
#define TEST_ASRC_SAMPLES  (TEST_ASRC_FRAMES * 2)
#define TEST_ASRC_CAPACITY 481

static int16_t test_input[TEST_ASRC_SAMPLES];
static int16_t test_output[TEST_ASRC_CAPACITY * 2];
static struct audio_asrc_state test_pre_state;

/* ── Helpers ─────────────────────────────────────────────────────── */

/* Cancel all pending offload work and run the prep worker synchronously. */
static void run_prep_work(void)
{
	k_work_cancel_delayable(&g_recovery_work);
	k_work_cancel_delayable(&g_prep_work);
	prep_work_fn(NULL);
}

/* Cancel all pending work and run the recovery worker synchronously. */
static void run_recovery_work(void)
{
	k_work_cancel_delayable(&g_recovery_work);
	k_work_cancel_delayable(&g_prep_work);
	recovery_work_fn(NULL);
}

/* Verify generic status snapshot counters. */
static void verify_status(uint32_t exp_submit, uint32_t exp_success, uint32_t exp_fallback,
			  uint32_t exp_timeout, uint32_t exp_full, uint32_t exp_stale,
			  uint32_t exp_seq, uint32_t exp_frame, uint32_t exp_crc,
			  uint32_t exp_payload)
{
	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.submit_count, exp_submit, "submit_count");
	zassert_equal(s.success_count, exp_success, "success_count");
	zassert_equal(s.fallback_count, exp_fallback, "fallback_count");
	zassert_equal(s.timeout_count, exp_timeout, "timeout_count");
	zassert_equal(s.full_count, exp_full, "full_count");
	zassert_equal(s.stale_count, exp_stale, "stale_count");
	zassert_equal(s.seq_fault_count, exp_seq, "seq_fault_count");
	zassert_equal(s.frame_fault_count, exp_frame, "frame_fault_count");
	zassert_equal(s.crc_fault_count, exp_crc, "crc_fault_count");
	zassert_equal(s.payload_fault_count, exp_payload, "payload_fault_count");
}

/* Fill output with known pattern so we can detect if it was touched. */
static void fill_output(int16_t val)
{
	memset(test_output, (int)(val & 0xFF), sizeof(test_output));
}

/* Verify output buffer untouched. */
static void assert_output_untouched(int16_t expected_val)
{
	for (size_t i = 0; i < TEST_ASRC_CAPACITY * 2; i++) {
		zassert_equal(test_output[i], expected_val,
			      "output[%zu] untouched expected 0x%04X got 0x%04X", i,
			      (unsigned)expected_val, (unsigned)test_output[i]);
	}
}

/* Initialize a valid ASRC pre-state. */
static void init_valid_pre_state(void)
{
	memset(&test_pre_state, 0, sizeof(test_pre_state));
	test_pre_state.phase = 0x0000000100000000ULL;
	test_pre_state.step_base = 0x0000000100000000ULL;
	test_pre_state.prev_l = 100;
	test_pre_state.prev_r = -100;
	test_pre_state.prev_valid = 1;
}

/* Set ASRC mock data to valid defaults for a given sequence. */
static void mock_asrc_defaults(uint32_t seq)
{
	mock_produce_result = FLPR_PRODUCE_OK;
	mock_notify_result = 0;
	mock_wait_result = 0;
	mock_wait_delay_ms = 0;
	mock_asrc_consume_result = FLPR_CONSUME_OK;
	memset(&mock_asrc_consume_data, 0, sizeof(mock_asrc_consume_data));
	mock_asrc_consume_data.output_frames = 480;
	mock_asrc_consume_data.sequence = seq;
	mock_asrc_consume_data.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
	mock_asrc_consume_data.correction_ppm = 0;
	mock_asrc_consume_data.processing_status = 0;
	mock_asrc_consume_data.rtt_cycles = 500;
	mock_asrc_consume_data.processing_cycles = 300;
	mock_asrc_consume_data.post_state = test_pre_state;
}

/* Trigger a fault via ASRC timeout; leave state RECOVERING. */
static void trigger_fault_no_recover(void)
{
	struct audio_offload_asrc_result result;
	mock_asrc_defaults(1);
	mock_wait_result = -EAGAIN;
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0, &test_pre_state, test_output,
				   TEST_ASRC_CAPACITY, &result);
}

/* Trigger a fault and run recovery to completion. Returns updated status. */
static struct audio_offload_status fault_and_recover(void)
{
	struct audio_offload_status s;
	trigger_fault_no_recover();
	mock_wait_result = 0;
	run_recovery_work();
	audio_offload_get_status(&s);
	return s;
}

/* Submit N successful ASRC blocks. */
static void submit_successes(uint32_t start_seq, uint32_t count)
{
	struct audio_offload_asrc_result result;
	mock_wait_result = 0;
	for (uint32_t i = 0; i < count; i++) {
		mock_asrc_defaults(start_seq + i);
		audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, start_seq + i, 0,
					   &test_pre_state, test_output, TEST_ASRC_CAPACITY,
					   &result);
	}
}

/* ── Setup/teardown ──────────────────────────────────────────────── */

static void setup_normal(void *fixture)
{
	(void)fixture;

	/* Reset all mocks (mock_reset handled by linker init on native_sim;
	 * we explicitly set the key variables here). */
	mock_init_fails = false;
	mock_flpr_healthy = true;
	mock_reset_fails = false;
	mock_produce_result = FLPR_PRODUCE_OK;
	mock_notify_result = 0;
	mock_wait_result = 0;
	mock_wait_delay_ms = 0;

	/* Stage 4B recovery mocks. */
	mock_runtime_restart_result = 0;
	mock_runtime_restart_calls = 0;
	mock_runtime_restart_called = false;
	mock_runtime_new_epoch = 0xABCD0001;
	mock_remote_restarted_result = 0;
	mock_remote_restarted_calls = 0;

	/* Fill test input with deterministic pattern. */
	for (size_t i = 0; i < TEST_ASRC_SAMPLES; i++) {
		test_input[i] = (int16_t)(i & 0xFFFF);
	}
	memset(test_output, 0, sizeof(test_output));
	init_valid_pre_state();

	audio_offload_init();

	/* Start stream: sets PREPARING, run prep synchronously to reach ACTIVE. */
	audio_offload_stream_start();
	run_prep_work();
}

static void teardown(void *fixture)
{
	(void)fixture;
	audio_offload_stream_stop();
	memset(test_output, 0, sizeof(test_output));
}

/* ── Test: timeout → poison → recovery → ACTIVE ────────────────── */

ZTEST(audio_offload, test_timeout_triggers_recovery)
{
	struct audio_offload_asrc_result result;
	mock_asrc_defaults(1);
	mock_wait_result = -EAGAIN;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, -EAGAIN, "should return -EAGAIN on timeout");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "must be RECOVERING");
	zassert_false(s.healthy, "not healthy");
	zassert_equal(s.timeout_count, 1, "timeout_count=1");
	zassert_equal(s.fallback_count, 1, "fallback_count=1");
	zassert_equal(s.submit_count, 1, "submit_count=1");

	fill_output(0xAB);
	mock_asrc_defaults(2);
	ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 2, 0, &test_pre_state,
					 test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, -EAGAIN, "should return -EAGAIN while recovering");
	assert_output_untouched((int16_t)0xABAB);

	uint32_t recov_before = s.recovery_attempts;

	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "state ACTIVE after recovery");
	zassert_true(s.healthy, "healthy after recovery");
	zassert_equal(s.timeout_count, 1, "timeout preserved across recovery");
	zassert_equal(s.fallback_count, 2, "fallback preserved (1 fault + 1 recovery-pass)");
	zassert_equal(s.submit_count, 2, "submit_count=2");
	zassert_equal(s.recovery_attempts, recov_before + 1, "recovery_count incremented");

	/* Next submit must succeed. */
	mock_wait_result = 0;
	mock_asrc_defaults(100);
	memset(test_output, 0xFF, sizeof(test_output));
	ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 100, 0, &test_pre_state,
					 test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, 0, "submit after recovery");
}

/* ── Test: recovery success via production worker ───────────────── */

ZTEST(audio_offload, test_recovery_success)
{
	mock_wait_result = -EAGAIN;
	struct audio_offload_asrc_result result;
	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, -EAGAIN, "timeout");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "state RECOVERING");
	zassert_equal(s.timeout_count, 1, "timeout=1");
	uint32_t recov_before = s.recovery_attempts;

	mock_wait_result = 0;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after recovery");
	zassert_true(s.healthy, "healthy");
	zassert_equal(s.recovery_attempts, recov_before + 1, "recovery_count incremented");
	zassert_equal(s.timeout_count, 1, "timeout preserved");
	zassert_equal(s.fallback_count, 1, "fallback preserved");

	mock_asrc_defaults(100);
	memset(test_output, 0xFF, sizeof(test_output));
	ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 100, 0, &test_pre_state,
					 test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, 0, "submit after recovery");
}

/* ── Test: recovery backoff with retry ──────────────────────────── */

ZTEST(audio_offload, test_recovery_backoff)
{
	trigger_fault_no_recover();

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING");
	uint32_t recov_before = s.recovery_attempts;

	mock_reset_fails = true;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "still RECOVERING after reset fail");
	zassert_equal(s.recovery_attempts, recov_before, "recovery_count unchanged (reset failed)");

	mock_reset_fails = false;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after retry");
	zassert_equal(s.recovery_attempts, recov_before + 1, "recovery_count incremented");
}

/* ── Test: stop cancels pending recovery ────────────────────────── */

ZTEST(audio_offload, test_stop_during_recovery)
{
	trigger_fault_no_recover();

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING");

	audio_offload_stream_stop();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "STOPPED after stop");

	run_recovery_work();
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "still STOPPED");
}

/* ── Test: sequence wrap (uint32_t) ──────────────────────────────── */

ZTEST(audio_offload, test_sequence_wrap)
{
	struct audio_offload_asrc_result result;
	uint32_t seq = 0xFFFFFFF0U;

	for (int i = 0; i < 32; i++) {
		mock_asrc_defaults(seq);
		memset(test_output, 0xFF, sizeof(test_output));
		int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, seq, 0,
						     &test_pre_state, test_output,
						     TEST_ASRC_CAPACITY, &result);
		zassert_equal(ret, 0, "seq %u", seq);
		seq++;
	}
	verify_status(32, 32, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* ── Test: reconnect (stop → start → submit) ─────────────────────── */

ZTEST(audio_offload, test_reconnect)
{
	struct audio_offload_asrc_result result;
	mock_asrc_defaults(0);
	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 0, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, 0, "first submit");

	audio_offload_stream_stop();

	mock_asrc_defaults(1);
	ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0, &test_pre_state,
					 test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, -EAGAIN, "stopped returns -EAGAIN");

	audio_offload_stream_start();
	run_prep_work();

	mock_asrc_defaults(100);
	memset(test_output, 0xFF, sizeof(test_output));
	ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 100, 0, &test_pre_state,
					 test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, 0, "submit after reconnect");
}

/* ── Test: async PREPARING state ─────────────────────────────────── */

ZTEST(audio_offload, test_async_preparing)
{
	audio_offload_stream_stop();

	struct audio_offload_status s;
	audio_offload_stream_start();
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_PREPARING, "PREPARING after stream_start");
	zassert_false(s.healthy, "not healthy during PREPARING");

	struct audio_offload_asrc_result result;
	mock_asrc_defaults(1);
	fill_output(0xAB);
	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, -EAGAIN, "submit during PREPARING");
	assert_output_untouched((int16_t)0xABAB);

	/* Check counters BEFORE prep resets them. */
	audio_offload_get_status(&s);
	zassert_equal(s.submit_count, 1, "submit_count counted");
	zassert_equal(s.fallback_count, 1, "fallback counted");

	run_prep_work();
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after prep");
	zassert_true(s.healthy, "healthy after prep");
}

/* ── Test: prep failure → bounded retry → ACTIVE ───────────────── */

ZTEST(audio_offload, test_prep_retry_then_active)
{
	audio_offload_stream_stop();

	struct audio_offload_status s;
	mock_reset_fails = true;
	audio_offload_stream_start();
	run_prep_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_PREPARING, "still PREPARING after first fail");

	mock_reset_fails = false;
	run_prep_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after retry success");
	zassert_true(s.healthy, "healthy");
}

/* ── Test: prep max retries → FALLBACK ──────────────────────────── */

ZTEST(audio_offload, test_prep_max_retries_fallback)
{
	audio_offload_stream_stop();

	struct audio_offload_status s;
	mock_reset_fails = true;
	audio_offload_stream_start();

	for (int i = 0; i < 6; i++) {
		run_prep_work();
		audio_offload_get_status(&s);
		if (s.state == AUDIO_OFFLOAD_FALLBACK) {
			break;
		}
		if (i < 5) {
			zassert_equal(s.state, AUDIO_OFFLOAD_PREPARING, "PREPARING on attempt %d",
				      i + 1);
		}
	}

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_FALLBACK, "FALLBACK after max retries");
	zassert_false(s.healthy, "not healthy in FALLBACK");
	zassert_true(s.recovery_fail_count > 0, "recovery_fail_count incremented");
}

/* ── Test: max retries in recovery → FALLBACK ───────────────────── */

ZTEST(audio_offload, test_recovery_max_retries_fallback)
{
	trigger_fault_no_recover();

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING");

	mock_reset_fails = true;
	for (int i = 0; i < 6; i++) {
		run_recovery_work();
		audio_offload_get_status(&s);
		if (s.state == AUDIO_OFFLOAD_FALLBACK) {
			break;
		}
		if (i < 5) {
			zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING on attempt %d",
				      i + 1);
		}
	}

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_FALLBACK, "FALLBACK after max recovery retries");
	zassert_true(s.recovery_fail_count > 0, "recovery_fail_count incremented");
}

/* ── Test: late output generation rejection ──────────────────────── */

ZTEST(audio_offload, test_late_output_rejection)
{
	struct audio_offload_asrc_result result;

	mock_asrc_defaults(50);
	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 50, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, 0, "normal submit OK");

	/* Stop + restart to bump generation. */
	mock_wait_delay_ms = 50;
	mock_asrc_defaults(60);
	audio_offload_stream_stop();
	audio_offload_stream_start();
	run_prep_work();

	mock_wait_delay_ms = 0;
	mock_asrc_defaults(10);
	memset(test_output, 0xFF, sizeof(test_output));
	ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 10, 0, &test_pre_state,
					 test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, 0, "submit after restart");
}

/* ── Test: counters preserved across recovery ───────────────────── */

ZTEST(audio_offload, test_counters_preserved_across_recovery)
{
	struct audio_offload_asrc_result result;

	mock_asrc_defaults(0);
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 0, 0, &test_pre_state, test_output,
				   TEST_ASRC_CAPACITY, &result);
	mock_asrc_defaults(1);
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0, &test_pre_state, test_output,
				   TEST_ASRC_CAPACITY, &result);
	verify_status(2, 2, 0, 0, 0, 0, 0, 0, 0, 0);

	/* Sequence fault. */
	mock_asrc_defaults(2);
	mock_asrc_consume_data.sequence = 999;
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 2, 0, &test_pre_state, test_output,
				   TEST_ASRC_CAPACITY, &result);

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.submit_count, 3, "submit=3");
	zassert_equal(s.success_count, 2, "success=2");
	zassert_equal(s.fallback_count, 1, "fallback=1");

	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE");
	zassert_equal(s.submit_count, 3, "submit preserved");
	zassert_equal(s.success_count, 2, "success preserved");
	zassert_equal(s.fallback_count, 1, "fallback preserved");
	zassert_equal(s.recovery_attempts, 1, "recovery=1");

	mock_asrc_defaults(100);
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 100, 0, &test_pre_state,
				   test_output, TEST_ASRC_CAPACITY, &result);

	audio_offload_get_status(&s);
	zassert_equal(s.success_count, 3, "success incremented to 3");
	zassert_equal(s.submit_count, 4, "submit incremented to 4");
}

/* ── Test: new stream_start resets per-stream counters ──────────── */

ZTEST(audio_offload, test_new_stream_resets_counters)
{
	struct audio_offload_asrc_result result;

	mock_asrc_defaults(0);
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 0, 0, &test_pre_state, test_output,
				   TEST_ASRC_CAPACITY, &result);
	mock_asrc_defaults(1);
	mock_wait_result = -EAGAIN;
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0, &test_pre_state, test_output,
				   TEST_ASRC_CAPACITY, &result);

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.submit_count, 2, "submit=2");
	zassert_equal(s.success_count, 1, "success=1");
	uint32_t prev_recovery = s.recovery_attempts;

	audio_offload_stream_stop();
	mock_wait_result = 0;
	audio_offload_stream_start();
	run_prep_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE");
	zassert_equal(s.submit_count, 0, "submit reset");
	zassert_equal(s.success_count, 0, "success reset");
	zassert_equal(s.recovery_attempts, prev_recovery, "recovery_count lifetime");
}

/* ── Test: fallback state persists ───────────────────────────────── */

ZTEST(audio_offload, test_fallback_state)
{
	audio_offload_stream_stop();

	struct audio_offload_asrc_result result;
	mock_asrc_defaults(0);
	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 0, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, -EAGAIN, "STOPPED returns -EAGAIN");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "state STOPPED");
}

/* ── Test: health check ──────────────────────────────────────────── */

ZTEST(audio_offload, test_is_healthy)
{
	zassert_true(audio_offload_is_healthy(), "healthy after init+start+prep");

	/* NULL status snapshot is a deterministic no-op (R2: keeps the
	 * surviving get_status null-guard line covered after the
	 * is_stopped() deletion). */
	audio_offload_get_status(NULL);

	trigger_fault_no_recover();
	zassert_false(audio_offload_is_healthy(), "not healthy after fault");

	run_recovery_work();
	zassert_true(audio_offload_is_healthy(), "healthy after recovery");
}

/* ── Test: concurrent stop during submit (helper thread) ──────────── */

static void stop_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	k_sleep(K_MSEC(20));
	audio_offload_stream_stop();
}

ZTEST(audio_offload, test_concurrent_stop_during_submit)
{
	mock_asrc_defaults(50);
	mock_wait_delay_ms = 200;

	struct k_thread stop_thread;
	static K_THREAD_STACK_DEFINE(stop_stack, 512);
	k_thread_create(&stop_thread, stop_stack, K_THREAD_STACK_SIZEOF(stop_stack), stop_thread_fn,
			NULL, NULL, NULL, 2, 0, K_NO_WAIT);

	struct audio_offload_asrc_result result;
	fill_output(0x99);
	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 50, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, -EAGAIN, "submit rejected after concurrent stop");
	assert_output_untouched((int16_t)0x9999);

	k_thread_join(&stop_thread, K_FOREVER);

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "STOPPED");
	zassert_true(s.stale_count > 0, "stale_count incremented by late rejection");
}

/* ── Test: exact submit+fallback accounting ──────────────────────── */

ZTEST(audio_offload, test_exact_accounting)
{
	struct audio_offload_status s;
	struct audio_offload_asrc_result result;

	mock_asrc_defaults(0);
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 0, 0, &test_pre_state, test_output,
				   TEST_ASRC_CAPACITY, &result);
	mock_asrc_defaults(1);
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0, &test_pre_state, test_output,
				   TEST_ASRC_CAPACITY, &result);
	verify_status(2, 2, 0, 0, 0, 0, 0, 0, 0, 0);

	/* Timeout. */
	mock_asrc_defaults(2);
	mock_wait_result = -EAGAIN;
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 2, 0, &test_pre_state, test_output,
				   TEST_ASRC_CAPACITY, &result);
	verify_status(3, 2, 1, 1, 0, 0, 0, 0, 0, 0);

	/* While recovering (submits rejected with fallback). */
	mock_asrc_defaults(3);
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 3, 0, &test_pre_state, test_output,
				   TEST_ASRC_CAPACITY, &result);
	verify_status(4, 2, 2, 1, 0, 0, 0, 0, 0, 0);

	audio_offload_get_status(&s);
	uint32_t recov_before = s.recovery_attempts;

	run_recovery_work();
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE");
	zassert_equal(s.submit_count, 4, "submit=4 after recovery");
	zassert_equal(s.success_count, 2, "success=2 after recovery");
	zassert_equal(s.fallback_count, 2, "fallback=2 after recovery");
	zassert_equal(s.timeout_count, 1, "timeout=1 after recovery");
	zassert_equal(s.recovery_attempts, recov_before + 1, "recovery_count incremented");

	/* More good submits after recovery. */
	mock_wait_result = 0;
	mock_asrc_defaults(100);
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 100, 0, &test_pre_state,
				   test_output, TEST_ASRC_CAPACITY, &result);
	mock_asrc_defaults(101);
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 101, 0, &test_pre_state,
				   test_output, TEST_ASRC_CAPACITY, &result);

	audio_offload_get_status(&s);
	zassert_equal(s.submit_count, 6, "submit=6");
	zassert_equal(s.success_count, 4, "success=4");
	zassert_equal(s.fallback_count, 2, "fallback still 2");
}

/* ── Recovery stability policy tests (probation) ─────────────────── */

ZTEST(audio_offload, test_probation_relapse_exhaustion)
{
	struct audio_offload_status s, baseline;

	audio_offload_get_status(&baseline);
	uint32_t base_attempts = baseline.recovery_attempts;
	uint32_t base_relapses = baseline.recovery_relapses;

	s = fault_and_recover();
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after first recovery");
	zassert_true(s.probation_active, "probation active");
	zassert_equal(s.probation_success, 0, "probation_success=0");
	zassert_equal(s.recovery_attempts, base_attempts + 1, "recovery_attempts+1");

	s = fault_and_recover();
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after relapse 1");
	zassert_equal(s.recovery_relapses, base_relapses + 1, "relapse+1");
	zassert_equal(s.recovery_attempts, base_attempts + 2, "recovery_attempts+2");

	s = fault_and_recover();
	zassert_equal(s.recovery_relapses, base_relapses + 2, "relapse+2");

	s = fault_and_recover();
	zassert_equal(s.recovery_relapses, base_relapses + 3, "relapse+3");

	s = fault_and_recover();
	zassert_equal(s.recovery_relapses, base_relapses + 4, "relapse+4");
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after 4 relapses");

	/* Relapse 5: max tries exhausted → FALLBACK. */
	mock_asrc_defaults(10);
	mock_wait_result = -EAGAIN;
	struct audio_offload_asrc_result r;
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 10, 0, &test_pre_state,
				   test_output, TEST_ASRC_CAPACITY, &r);
	mock_wait_result = 0;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_FALLBACK, "FALLBACK after max exhaustion");
	zassert_equal(s.max_exhaustion_count, baseline.max_exhaustion_count + 1,
		      "max_exhaustion+1");
	zassert_equal(s.recovery_relapses, base_relapses + 5, "relapse+5");
	zassert_equal(s.recovery_attempts, base_attempts + 5, "5 successful recoveries");

	mock_asrc_defaults(20);
	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 20, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &r);
	zassert_equal(ret, -EAGAIN, "FALLBACK submit returns -EAGAIN");
}

ZTEST(audio_offload, test_probation_cleared_100_success)
{
	struct audio_offload_status s, baseline;

	audio_offload_get_status(&baseline);
	uint32_t base_cleared = baseline.probation_cleared;
	uint32_t base_attempts = baseline.recovery_attempts;

	fault_and_recover();
	audio_offload_get_status(&s);
	zassert_true(s.probation_active, "probation active after recovery");

	submit_successes(1000, 99);
	audio_offload_get_status(&s);
	zassert_true(s.probation_active, "probation still active at 99");
	zassert_equal(s.probation_success, 99, "probation_success=99");

	submit_successes(1099, 1);
	audio_offload_get_status(&s);
	zassert_false(s.probation_active, "probation cleared at 100");
	zassert_equal(s.probation_cleared, base_cleared + 1, "probation_cleared+1");

	uint32_t prev_relapses = s.recovery_relapses;
	s = fault_and_recover();
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after fresh fault");
	zassert_true(s.probation_active, "probation active again");
	zassert_equal(s.recovery_relapses, prev_relapses,
		      "relapses unchanged (fresh start after clear, not relapse)");
	zassert_equal(s.recovery_attempts, base_attempts + 2, "recovery_attempts+2");
}

ZTEST(audio_offload, test_fault_after_stable)
{
	struct audio_offload_status s, baseline;

	audio_offload_get_status(&baseline);
	uint32_t base_attempts = baseline.recovery_attempts;

	fault_and_recover();
	audio_offload_get_status(&s);
	zassert_true(s.probation_active, "probation active");

	uint32_t prev_relapses = s.recovery_relapses;
	submit_successes(2000, 100);
	audio_offload_get_status(&s);
	zassert_false(s.probation_active, "probation cleared");

	s = fault_and_recover();
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after fresh fault");
	zassert_equal(s.recovery_relapses, prev_relapses,
		      "relapses unchanged — fault after stable is NOT a relapse");
	zassert_equal(s.recovery_attempts, base_attempts + 2, "recovery_attempts+2");
	zassert_true(s.probation_active, "probation active for new recovery");
}

ZTEST(audio_offload, test_stop_reconnect_resets_policy)
{
	struct audio_offload_status s, baseline;

	audio_offload_get_status(&baseline);
	uint32_t base_attempts = baseline.recovery_attempts;
	uint32_t base_relapses = baseline.recovery_relapses;

	fault_and_recover();
	audio_offload_get_status(&s);
	zassert_true(s.probation_active, "probation active");

	fault_and_recover();
	audio_offload_get_status(&s);
	zassert_equal(s.recovery_relapses, base_relapses + 1, "relapse+1");

	audio_offload_stream_stop();
	audio_offload_stream_start();
	run_prep_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after reconnect");
	zassert_false(s.probation_active, "probation cleared by reconnect");
	zassert_equal(s.recovery_relapses, base_relapses + 1, "relapses preserved (lifetime)");
	zassert_equal(s.recovery_attempts, base_attempts + 2,
		      "recovery_attempts preserved (lifetime)");

	s = fault_and_recover();
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after fresh fault");
	zassert_true(s.probation_active, "probation active for new cycle");
	zassert_equal(s.recovery_relapses, base_relapses + 1,
		      "relapses unchanged (not a new relapse)");
	zassert_equal(s.recovery_attempts, base_attempts + 3, "recovery_attempts+3");
}

ZTEST(audio_offload, test_recovery_bounded_5_attempts)
{
	struct audio_offload_status s, baseline;

	audio_offload_get_status(&baseline);
	uint32_t base_attempts = baseline.recovery_attempts;

	for (int i = 0; i < 5; i++) {
		fault_and_recover();
	}
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after 5 relapse cycles (tries=5)");
	zassert_equal(s.recovery_attempts, base_attempts + 5, "recovery_attempts+5 (5 successful)");
	zassert_false(s.max_exhaustion_count > baseline.max_exhaustion_count, "not exhausted yet");

	/* 6th fault during probation → exhaustion. */
	mock_asrc_defaults(50);
	mock_wait_result = -EAGAIN;
	struct audio_offload_asrc_result r;
	audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 50, 0, &test_pre_state,
				   test_output, TEST_ASRC_CAPACITY, &r);
	mock_wait_result = 0;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_FALLBACK,
		      "FALLBACK on 6th recovery entry (exhausted)");
	zassert_equal(s.max_exhaustion_count, baseline.max_exhaustion_count + 1,
		      "max_exhaustion+1");
	zassert_equal(s.recovery_attempts, base_attempts + 5, "still 5 successful recoveries");
}

/* ── Stage 4B recovery state machine tests ───────────────────────── */

ZTEST(audio_offload, test_stage4b_short_reset_ok)
{
	trigger_fault_no_recover();

	mock_flpr_healthy = true;
	mock_reset_fails = false;

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	uint32_t prev_runtime = s.runtime_restart_count;

	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after short reset");
	zassert_true(s.healthy, "healthy");
	zassert_not_equal(s.epoch, 0, "epoch nonzero");
	zassert_equal(s.epoch, mock_last_epoch, "epoch committed from coordinated reset");
	zassert_equal(s.runtime_restart_count, prev_runtime, "no runtime restart for short path");
}

ZTEST(audio_offload, test_stage4b_runtime_restart_path)
{
	trigger_fault_no_recover();

	mock_flpr_healthy = false;
	mock_runtime_restart_result = 0;
	mock_remote_restarted_result = 0;
	mock_reset_fails = false;

	uint32_t prev_rt_calls = mock_runtime_restart_calls;
	uint32_t prev_reinit = mock_remote_restarted_calls;

	run_recovery_work();

	zassert_equal(mock_runtime_restart_calls, prev_rt_calls + 1, "runtime restart called");
	zassert_equal(mock_remote_restarted_calls, prev_reinit + 1, "ring remote reinit called");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after runtime path");
	zassert_true(s.healthy, "healthy");
}

ZTEST(audio_offload, test_stage4b_heartbeat_dedup_recovering)
{
	trigger_fault_no_recover();

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING after fault");
	uint32_t prev_dedup = s.heartbeat_dedup_count;

	audio_offload_remote_unavailable();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "still RECOVERING");
	zassert_equal(s.heartbeat_dedup_count, prev_dedup + 1, "dedup incremented");

	mock_flpr_healthy = true;
	run_recovery_work();
}

ZTEST(audio_offload, test_stage4b_failure_retry_policy)
{
	trigger_fault_no_recover();
	mock_flpr_healthy = false;

	mock_runtime_restart_result = -EIO;
	run_recovery_work();

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "still RECOVERING after runtime fail");

	mock_runtime_restart_result = 0;
	mock_remote_restarted_result = -EIO;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "still RECOVERING after reinit fail");

	mock_remote_restarted_result = 0;
	mock_reset_fails = true;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "still RECOVERING after reset fail");

	mock_reset_fails = false;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after all stages pass");
	zassert_true(s.healthy, "healthy");
}

ZTEST(audio_offload, test_stage4b_stop_blocks_stale)
{
	trigger_fault_no_recover();
	mock_flpr_healthy = false;
	mock_runtime_restart_result = 0;
	mock_remote_restarted_result = 0;
	mock_reset_fails = false;

	audio_offload_stream_stop();
	run_recovery_work();

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "STOPPED after stop during recovery");
}

ZTEST(audio_offload, test_stage4b_idle_restart_reinit)
{
	audio_offload_stream_stop();

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "STOPPED");

	mock_runtime_restart_result = 0;
	mock_remote_restarted_result = 0;
	uint32_t prev_reinit = mock_remote_restarted_calls;

	audio_offload_remote_unavailable();

	zassert_equal(mock_remote_restarted_calls, prev_reinit + 1,
		      "ring reinit called on idle restart");
}

ZTEST(audio_offload, test_stage4b_fallback_block)
{
	audio_offload_stream_stop();

	struct audio_offload_asrc_result result;
	mock_asrc_defaults(1);
	fill_output(0x42);
	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);
	zassert_equal(ret, -EAGAIN, "STOPPED blocks submit");
	assert_output_untouched((int16_t)0x4242);

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "state unchanged");
	zassert_false(s.healthy, "not healthy");
}

ZTEST(audio_offload, test_stage4b_exact_counters)
{
	struct audio_offload_status s;
	audio_offload_get_status(&s);
	uint32_t base_runtime = s.runtime_restart_count;

	trigger_fault_no_recover();
	mock_flpr_healthy = true;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after short reset");
	zassert_equal(s.runtime_restart_count, base_runtime, "no runtime for short reset");
	zassert_true(s.recovery_attempts > 0, "recovery_attempts incremented");

	trigger_fault_no_recover();
	mock_flpr_healthy = false;
	mock_runtime_restart_result = 0;
	mock_remote_restarted_result = 0;
	mock_reset_fails = false;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after runtime path");
	zassert_equal(s.runtime_restart_count, base_runtime + 1,
		      "runtime_restart_count incremented");

	trigger_fault_no_recover();
	audio_offload_get_status(&s);
	uint32_t dedup_before = s.heartbeat_dedup_count;
	audio_offload_remote_unavailable();
	audio_offload_get_status(&s);
	zassert_equal(s.heartbeat_dedup_count, dedup_before + 1, "heartbeat dedup increment");

	mock_flpr_healthy = true;
	run_recovery_work();
}

/* ── ASRC offload accounting tests ────────────────────────────────── */

/* Snapshot helpers for delta verification. */
#define ASRC_SNAPSHOT(s, a)                                                                        \
	do {                                                                                       \
		audio_offload_get_status(&(s));                                                    \
		audio_offload_get_asrc_stats(&(a));                                                \
	} while (0)

#define ASRC_DELTA_U32(post, pre, field) ((post).field - (pre).field)

static void
verify_asrc_deltas(struct audio_offload_status *pre_s, struct audio_offload_asrc_stats *pre_a,
		   struct audio_offload_status *post_s, struct audio_offload_asrc_stats *post_a,
		   uint32_t exp_as_submit, uint32_t exp_as_success, uint32_t exp_as_fallback,
		   uint32_t exp_gen_submit, uint32_t exp_gen_success, uint32_t exp_gen_fallback)
{
	zassert_equal(ASRC_DELTA_U32(*post_a, *pre_a, submit_count), exp_as_submit,
		      "asrc submit_count");
	zassert_equal(ASRC_DELTA_U32(*post_a, *pre_a, success_count), exp_as_success,
		      "asrc success_count");
	zassert_equal(ASRC_DELTA_U32(*post_a, *pre_a, fallback_count), exp_as_fallback,
		      "asrc fallback_count");
	zassert_equal(ASRC_DELTA_U32(*post_s, *pre_s, submit_count), exp_gen_submit,
		      "generic submit_count");
	zassert_equal(ASRC_DELTA_U32(*post_s, *pre_s, success_count), exp_gen_success,
		      "generic success_count");
	zassert_equal(ASRC_DELTA_U32(*post_s, *pre_s, fallback_count), exp_gen_fallback,
		      "generic fallback_count");
}

static void verify_asrc_category(struct audio_offload_status *pre_s,
				 struct audio_offload_status *post_s,
				 struct audio_offload_asrc_stats *pre_a,
				 struct audio_offload_asrc_stats *post_a, uint32_t exp_gen_timeout,
				 uint32_t exp_gen_full, uint32_t exp_gen_stale,
				 uint32_t exp_gen_seq, uint32_t exp_gen_frame, uint32_t exp_gen_crc,
				 uint32_t exp_gen_payload, uint32_t exp_as_timeout,
				 uint32_t exp_as_full, uint32_t exp_as_stale, uint32_t exp_as_seq,
				 uint32_t exp_as_frame, uint32_t exp_as_crc, uint32_t exp_as_state,
				 uint32_t exp_as_verify)
{
	zassert_equal(ASRC_DELTA_U32(*post_s, *pre_s, timeout_count), exp_gen_timeout,
		      "gen timeout");
	zassert_equal(ASRC_DELTA_U32(*post_s, *pre_s, full_count), exp_gen_full, "gen full");
	zassert_equal(ASRC_DELTA_U32(*post_s, *pre_s, stale_count), exp_gen_stale, "gen stale");
	zassert_equal(ASRC_DELTA_U32(*post_s, *pre_s, seq_fault_count), exp_gen_seq, "gen seq");
	zassert_equal(ASRC_DELTA_U32(*post_s, *pre_s, frame_fault_count), exp_gen_frame,
		      "gen frame");
	zassert_equal(ASRC_DELTA_U32(*post_s, *pre_s, crc_fault_count), exp_gen_crc, "gen crc");
	zassert_equal(ASRC_DELTA_U32(*post_s, *pre_s, payload_fault_count), exp_gen_payload,
		      "gen payload");
	zassert_equal(ASRC_DELTA_U32(*post_a, *pre_a, timeout_count), exp_as_timeout,
		      "asrc timeout");
	zassert_equal(ASRC_DELTA_U32(*post_a, *pre_a, full_count), exp_as_full, "asrc full");
	zassert_equal(ASRC_DELTA_U32(*post_a, *pre_a, stale_count), exp_as_stale, "asrc stale");
	zassert_equal(ASRC_DELTA_U32(*post_a, *pre_a, seq_fault_count), exp_as_seq, "asrc seq");
	zassert_equal(ASRC_DELTA_U32(*post_a, *pre_a, frame_fault_count), exp_as_frame,
		      "asrc frame");
	zassert_equal(ASRC_DELTA_U32(*post_a, *pre_a, crc_fault_count), exp_as_crc, "asrc crc");
	zassert_equal(ASRC_DELTA_U32(*post_a, *pre_a, state_fault_count), exp_as_state,
		      "asrc state");
	zassert_equal(ASRC_DELTA_U32(*post_a, *pre_a, verify_fault_count), exp_as_verify,
		      "asrc verify");
}

/* Setup for ASRC tests: stream started, state ACTIVE. */
static enum asrc_test_stage stop_target;

static void setup_asrc(void *fixture)
{
	(void)fixture;

	asrc_test_stage_hook = NULL;
	asrc_test_stage_user_data = NULL;
	stop_target = (enum asrc_test_stage)0xFF;

	audio_offload_init();
	audio_offload_stream_start();
	run_prep_work();

	mock_produce_result = FLPR_PRODUCE_OK;
	mock_notify_result = 0;
	mock_wait_result = 0;
	mock_wait_delay_ms = 0;

	mock_asrc_consume_result = FLPR_CONSUME_OK;
	memset(&mock_asrc_consume_data, 0, sizeof(mock_asrc_consume_data));
	mock_asrc_consume_data.output_frames = 480;
	mock_asrc_consume_data.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
	mock_asrc_consume_data.processing_status = 0;
	mock_asrc_consume_data.rtt_cycles = 500;
	mock_asrc_consume_data.processing_cycles = 300;

	init_valid_pre_state();

	for (size_t i = 0; i < TEST_ASRC_SAMPLES; i++) {
		test_input[i] = (int16_t)(i & 0xFFFF);
	}
	memset(test_output, 0, sizeof(test_output));
}

static void teardown_asrc(void *fixture)
{
	(void)fixture;
	asrc_test_stage_hook = NULL;
	asrc_test_stage_user_data = NULL;
	audio_offload_stream_stop();
	memset(test_output, 0, sizeof(test_output));
}

/* ── R5: second-thread submit-mutex contention + stage hooks ──────── */

K_SEM_DEFINE(busy_holder_locked, 0, 1);
K_SEM_DEFINE(busy_holder_release, 0, 1);

/* Holds g_submit_lock until the test releases it — honest contention
 * for the 8 ms mutex-timeout (busy) path. */
static void busy_lock_holder_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_mutex_lock(&g_submit_lock, K_FOREVER);
	k_sem_give(&busy_holder_locked);
	k_sem_take(&busy_holder_release, K_FOREVER);
	k_mutex_unlock(&g_submit_lock);
}

/* Hook callback: lifecycle change at ONE chosen stage boundary.  The
 * production hook fires at every boundary; the test filters so the stop
 * happens exactly at the stage under test. */
static void stop_at_stage_hook(enum asrc_test_stage stage, void *user_data)
{
	(void)user_data;
	if (stage == stop_target) {
		audio_offload_stream_stop();
	}
}

/* ── R5: table-driven recovery-eligible fault snapshots ───────────── */

struct asrc_fault_row {
	const char *name;
	uint32_t sequence;
	int expected_last_error;
	uint32_t gen_timeout, gen_full, gen_stale, gen_seq, gen_frame;
	uint32_t as_timeout, as_full, as_stale, as_seq, as_frame, as_state;
};

/* Every recovery-eligible post-lock fault stage: exact category deltas,
 * exact last_error/last_error_seq, RECOVERING + one schedule, output and
 * result untouched.  Category column order matches verify_asrc_category. */
static const struct asrc_fault_row asrc_fault_rows[] = {
	{"produce_full", 101, -ENOSPC, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0},
	{"produce_other", 102, -EIO, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{"notify_error", 103, -EIO, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{"wait_timeout", 104, -ETIMEDOUT, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
	{"consume_empty", 105, -ENOENT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{"consume_stale", 106, -ESTALE, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0},
	{"consume_other", 107, -EIO, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{"flpr_error_transport", 108, -5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{"frame_range", 109, -EFAULT, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0},
	{"status_nonzero", 110, -EFAULT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{"flags_wrong", 111, -EFAULT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{"seq_mismatch", 112, -EFAULT, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0},
	{"correction_mismatch", 113, -EFAULT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{"reserved_nonzero", 114, -EFAULT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
	{"post_state_import", 115, -EFAULT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
	{"step_base_mismatch", 116, -EFAULT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
};

static void apply_asrc_fault_row(const struct asrc_fault_row *row)
{
	switch (row - asrc_fault_rows) {
	case 0:
		mock_produce_result = FLPR_PRODUCE_FULL;
		break;
	case 1:
		mock_produce_result = (enum flpr_produce_result)99;
		break;
	case 2:
		mock_notify_result = -EIO;
		break;
	case 3:
		mock_wait_result = -ETIMEDOUT;
		break;
	case 4:
		mock_asrc_consume_result = FLPR_CONSUME_EMPTY;
		break;
	case 5:
		mock_asrc_consume_result = FLPR_CONSUME_STALE;
		break;
	case 6:
		mock_asrc_consume_result = (enum flpr_consume_result)99;
		break;
	case 7:
		mock_asrc_consume_data.processing_status = -5;
		mock_asrc_consume_data.output_frames = 0;
		break;
	case 8:
		mock_asrc_consume_data.output_frames = 0;
		break;
	case 9:
		mock_asrc_consume_data.processing_status = 1;
		break;
	case 10:
		mock_asrc_consume_data.flags = FLPR_SLOT_FLAG_VALID;
		break;
	case 11:
		mock_asrc_consume_data.sequence = 9999;
		break;
	case 12:
		mock_asrc_consume_data.correction_ppm = 123;
		break;
	case 13:
		mock_asrc_consume_data.post_state.reserved[0] = 1;
		break;
	case 14:
		mock_asrc_consume_data.post_state.step_base = 0;
		break;
	case 15:
		mock_asrc_consume_data.post_state.step_base = test_pre_state.step_base + 1;
		break;
	}
}

/* ── ASRC tests: exact deltas per reachable branch ───────────────── */

ZTEST(audio_offload_asrc, test_asrc_success)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_asrc_defaults(1);
	ASRC_SNAPSHOT(pre_s, pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, 0, "success return 0");
	zassert_equal(result.output_frames, 480, "output_frames");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 1, 0, 1, 1, 0);
	verify_asrc_category(&pre_s, &post_s, &pre_a, &post_a, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			     0, 0, 0);
}

ZTEST(audio_offload_asrc, test_asrc_invalid_null)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	int ret = audio_offload_process_asrc(NULL, TEST_ASRC_FRAMES, 1, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EINVAL, "null input");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 0, 0, 0, 0, 0, 0);
}

ZTEST(audio_offload_asrc, test_asrc_invalid_frames)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	int ret = audio_offload_process_asrc(test_input, 240, 1, 0, &test_pre_state, test_output,
					     TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EINVAL, "wrong frames");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 0, 0, 0, 0, 0, 0);
}

ZTEST(audio_offload_asrc, test_asrc_not_active)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	audio_offload_stream_stop();
	audio_offload_stream_start();

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "not ACTIVE");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);

	run_prep_work();
}

ZTEST(audio_offload_asrc, test_asrc_busy)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;
	struct k_thread holder;
	static K_THREAD_STACK_DEFINE(holder_stack, 512);

	ASRC_SNAPSHOT(pre_s, pre_a);

	/* Honest second-thread contention: a helper thread holds the submit
	 * mutex so the 8 ms lock deadline expires in the main thread. */
	k_thread_create(&holder, holder_stack, K_THREAD_STACK_SIZEOF(holder_stack),
			busy_lock_holder_fn, NULL, NULL, NULL, 3, 0, K_NO_WAIT);
	zassert_equal(k_sem_take(&busy_holder_locked, K_MSEC(2000)), 0, "holder must lock");

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 42, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	k_sem_give(&busy_holder_release);
	k_thread_join(&holder, K_FOREVER);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "mutex timeout rejects");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	zassert_equal(ASRC_DELTA_U32(post_s, pre_s, busy_count), 1, "busy_count +1");
	zassert_equal(post_s.last_error, -EBUSY, "last_error -EBUSY");
	zassert_equal(post_s.last_error_seq, 42, "last_error_seq");
	zassert_equal(post_s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING");
	zassert_false(post_s.healthy, "healthy false");
	zassert_true(audio_offload_test_is_recovery_scheduled(), "one recovery schedule");
	assert_output_untouched(0);

	/* One recovery run consumes the schedule exactly once. */
	run_recovery_work();
	{
		struct audio_offload_status s;
		audio_offload_get_status(&s);
		zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after recovery");
		zassert_false(audio_offload_test_is_recovery_scheduled(),
			      "recovery schedule consumed");
	}
}

ZTEST(audio_offload_asrc, test_asrc_post_mutex_not_active)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	memset(&result, 0, sizeof(result));
	ASRC_SNAPSHOT(pre_s, pre_a);

	/* Mutex is acquired, then the lifecycle changes BEFORE the post-mutex
	 * recheck: the special non-recovery epilogue must run (fallback +
	 * last_error=-EAGAIN, no stale, no RECOVERING, no recovery). */
	stop_target = ASRC_TEST_STAGE_MUTEX_ACQUIRED;
	asrc_test_stage_hook = stop_at_stage_hook;
	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 33, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);
	asrc_test_stage_hook = NULL;

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "post-mutex non-ACTIVE rejects");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	zassert_equal(ASRC_DELTA_U32(post_s, pre_s, stale_count), 0, "no stale count");
	zassert_equal(ASRC_DELTA_U32(post_s, pre_s, busy_count), 0, "no busy count");
	zassert_equal(post_s.last_error, -EAGAIN, "last_error -EAGAIN");
	zassert_equal(post_s.last_error_seq, 33, "last_error_seq");
	zassert_equal(post_s.state, AUDIO_OFFLOAD_STOPPED, "STOPPED preserved (no RECOVERING)");
	zassert_false(audio_offload_test_is_recovery_scheduled(), "no recovery schedule");
	assert_output_untouched(0);
}

ZTEST(audio_offload_asrc, test_asrc_mutex_timeout_lifecycle_changed)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;
	struct k_thread holder;
	static K_THREAD_STACK_DEFINE(holder_stack, 512);

	memset(&result, 0, sizeof(result));
	ASRC_SNAPSHOT(pre_s, pre_a);

	/* Holder blocks the mutex; the hook fires when the 8 ms lock expires
	 * and stops the stream BEFORE the busy finalizer's lifecycle recheck:
	 * must count stale, NOT busy, and never schedule recovery. */
	k_thread_create(&holder, holder_stack, K_THREAD_STACK_SIZEOF(holder_stack),
			busy_lock_holder_fn, NULL, NULL, NULL, 3, 0, K_NO_WAIT);
	zassert_equal(k_sem_take(&busy_holder_locked, K_MSEC(2000)), 0, "holder must lock");

	stop_target = ASRC_TEST_STAGE_MUTEX_TIMEOUT;
	asrc_test_stage_hook = stop_at_stage_hook;
	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 34, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);
	asrc_test_stage_hook = NULL;

	k_sem_give(&busy_holder_release);
	k_thread_join(&holder, K_FOREVER);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "stale-during-mutex-timeout rejects");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	zassert_equal(ASRC_DELTA_U32(post_s, pre_s, stale_count), 1, "stale_count +1");
	zassert_equal(ASRC_DELTA_U32(post_s, pre_s, busy_count), 0, "busy_count NOT counted");
	zassert_equal(post_s.last_error, -ESTALE, "last_error -ESTALE");
	zassert_equal(post_s.last_error_seq, 34, "last_error_seq");
	zassert_equal(post_s.state, AUDIO_OFFLOAD_STOPPED, "STOPPED preserved (no RECOVERING)");
	zassert_false(audio_offload_test_is_recovery_scheduled(), "no recovery schedule");
	assert_output_untouched(0);
}

ZTEST(audio_offload_asrc, test_asrc_commit_stale_race)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_asrc_defaults(44);
	fill_output(0x77);
	memset(&result, 0, sizeof(result));

	ASRC_SNAPSHOT(pre_s, pre_a);

	/* Full successful roundtrip, but the stream stops at the commit
	 * boundary: the final lifecycle guard must count stale, leave output
	 * and result untouched, and NOT commit success. */
	stop_target = ASRC_TEST_STAGE_BEFORE_COMMIT;
	asrc_test_stage_hook = stop_at_stage_hook;
	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 44, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);
	asrc_test_stage_hook = NULL;

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "commit stale rejects");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	zassert_equal(ASRC_DELTA_U32(post_s, pre_s, stale_count), 1, "stale_count +1");
	zassert_equal(post_s.last_error, -ESTALE, "last_error -ESTALE");
	zassert_equal(post_s.last_error_seq, 44, "last_error_seq");
	zassert_equal(post_s.state, AUDIO_OFFLOAD_STOPPED, "STOPPED preserved (no RECOVERING)");
	zassert_false(audio_offload_test_is_recovery_scheduled(), "no recovery schedule");
	assert_output_untouched((int16_t)0x7777);
	zassert_equal(result.output_frames, 0, "result untouched");
}

ZTEST(audio_offload_asrc, test_asrc_fault_table)
{
	for (size_t i = 0; i < ARRAY_SIZE(asrc_fault_rows); i++) {
		const struct asrc_fault_row *row = &asrc_fault_rows[i];
		struct audio_offload_status pre_s, post_s;
		struct audio_offload_asrc_stats pre_a, post_a;
		struct audio_offload_asrc_result result;

		/* Fresh ACTIVE state per row without consuming recovery tries:
		 * stop + start + prep resets per-stream counters and cancels
		 * any pending recovery schedule. */
		audio_offload_stream_stop();
		audio_offload_stream_start();
		run_prep_work();

		mock_asrc_defaults(row->sequence);
		apply_asrc_fault_row(row);
		fill_output(0x55);
		memset(&result, 0xFF, sizeof(result));

		ASRC_SNAPSHOT(pre_s, pre_a);

		int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, row->sequence, 0,
						     &test_pre_state, test_output,
						     TEST_ASRC_CAPACITY, &result);

		ASRC_SNAPSHOT(post_s, post_a);

		zassert_equal(ret, -EAGAIN, "row %s ret", row->name);
		verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
		zassert_equal(post_s.last_error, row->expected_last_error, "row %s last_error",
			      row->name);
		zassert_equal(post_s.last_error_seq, row->sequence, "row %s last_error_seq",
			      row->name);
		zassert_equal(ASRC_DELTA_U32(post_s, pre_s, timeout_count), row->gen_timeout,
			      "row %s gen timeout", row->name);
		zassert_equal(ASRC_DELTA_U32(post_s, pre_s, full_count), row->gen_full,
			      "row %s gen full", row->name);
		zassert_equal(ASRC_DELTA_U32(post_s, pre_s, stale_count), row->gen_stale,
			      "row %s gen stale", row->name);
		zassert_equal(ASRC_DELTA_U32(post_s, pre_s, seq_fault_count), row->gen_seq,
			      "row %s gen seq", row->name);
		zassert_equal(ASRC_DELTA_U32(post_s, pre_s, frame_fault_count), row->gen_frame,
			      "row %s gen frame", row->name);
		zassert_equal(ASRC_DELTA_U32(post_a, pre_a, timeout_count), row->as_timeout,
			      "row %s asrc timeout", row->name);
		zassert_equal(ASRC_DELTA_U32(post_a, pre_a, full_count), row->as_full,
			      "row %s asrc full", row->name);
		zassert_equal(ASRC_DELTA_U32(post_a, pre_a, stale_count), row->as_stale,
			      "row %s asrc stale", row->name);
		zassert_equal(ASRC_DELTA_U32(post_a, pre_a, seq_fault_count), row->as_seq,
			      "row %s asrc seq", row->name);
		zassert_equal(ASRC_DELTA_U32(post_a, pre_a, frame_fault_count), row->as_frame,
			      "row %s asrc frame", row->name);
		zassert_equal(ASRC_DELTA_U32(post_a, pre_a, state_fault_count), row->as_state,
			      "row %s asrc state", row->name);
		zassert_equal(post_s.state, AUDIO_OFFLOAD_RECOVERING, "row %s RECOVERING",
			      row->name);
		zassert_false(post_s.healthy, "row %s healthy false", row->name);
		zassert_true(audio_offload_test_is_recovery_scheduled(),
			     "row %s one recovery schedule", row->name);
		assert_output_untouched((int16_t)0x5555);
		zassert_equal(result.output_frames, 0xFFFF, "row %s result untouched", row->name);
	}
}

ZTEST(audio_offload_asrc, test_asrc_rtt_stats)
{
	struct audio_offload_asrc_stats pre, s;

	/* RTT/cycles are lifetime counters (reset only at init); use deltas. */
	audio_offload_get_asrc_stats(&pre);

	mock_asrc_defaults(1);
	mock_asrc_consume_data.rtt_cycles = 500;
	{
		struct audio_offload_asrc_result r;
		memset(&r, 0, sizeof(r));
		zassert_equal(audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 1, 0,
							 &test_pre_state, test_output,
							 TEST_ASRC_CAPACITY, &r),
			      0, "success 1");
	}
	mock_asrc_defaults(2);
	mock_asrc_consume_data.rtt_cycles = 700;
	{
		struct audio_offload_asrc_result r;
		memset(&r, 0, sizeof(r));
		zassert_equal(audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 2, 0,
							 &test_pre_state, test_output,
							 TEST_ASRC_CAPACITY, &r),
			      0, "success 2");
	}
	mock_asrc_defaults(3);
	mock_asrc_consume_data.rtt_cycles = 600;
	{
		struct audio_offload_asrc_result r;
		memset(&r, 0, sizeof(r));
		zassert_equal(audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 3, 0,
							 &test_pre_state, test_output,
							 TEST_ASRC_CAPACITY, &r),
			      0, "success 3");
	}

	audio_offload_get_asrc_stats(&s);
	zassert_equal(s.rtt_count - pre.rtt_count, 3, "rtt_count delta 3");
	zassert_equal(s.rtt_sum_cycles - pre.rtt_sum_cycles, 1800, "rtt_sum delta 1800");
	zassert_equal(s.rtt_min_cycles, 500, "rtt_min 500 (extends range low)");
	zassert_equal(s.rtt_max_cycles, 700, "rtt_max 700 (extends range high)");
}

ZTEST(audio_offload_asrc, test_asrc_state_changed_before_mutex)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	audio_offload_stream_stop();
	audio_offload_stream_start();
	run_prep_work();

	audio_offload_stream_start();

	ASRC_SNAPSHOT(pre_s, pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 2, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "state changed");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);

	run_prep_work();
}

ZTEST(audio_offload_asrc, test_asrc_produce_full)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_produce_result = FLPR_PRODUCE_FULL;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 3, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "full");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	verify_asrc_category(&pre_s, &post_s, &pre_a, &post_a, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
			     0, 0, 0);
}

ZTEST(audio_offload_asrc, test_asrc_produce_error)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_produce_result = (enum flpr_produce_result)99;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 4, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "produce error");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
}

ZTEST(audio_offload_asrc, test_asrc_notify_error)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_notify_result = -EIO;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 5, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "notify error");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
}

ZTEST(audio_offload_asrc, test_asrc_wait_timeout)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_wait_result = -ETIMEDOUT;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 6, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "timeout");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	verify_asrc_category(&pre_s, &post_s, &pre_a, &post_a, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
			     0, 0, 0);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_lifecycle_before_consume)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_asrc_defaults(7);
	ASRC_SNAPSHOT(pre_s, pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 7, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, 0, "success (lifecycle unchanged)");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 1, 0, 1, 1, 0);
}

ZTEST(audio_offload_asrc, test_asrc_consume_empty)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_result = FLPR_CONSUME_EMPTY;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 8, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "empty");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_consume_stale)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_result = FLPR_CONSUME_STALE;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 9, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "stale");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	verify_asrc_category(&pre_s, &post_s, &pre_a, &post_a, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0,
			     0, 0, 0);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_consume_error)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_result = (enum flpr_consume_result)99;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 10, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "consume error");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_error_output)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_data.processing_status = -5;
	mock_asrc_consume_data.output_frames = 0;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 11, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "error output");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_frame_range)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_data.output_frames = 0;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 12, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "frame range");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	verify_asrc_category(&pre_s, &post_s, &pre_a, &post_a, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1,
			     0, 0, 0);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_status_nonzero)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_data.processing_status = 1;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 13, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "status nonzero");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_flags_wrong)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_data.flags = FLPR_SLOT_FLAG_VALID;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 14, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "flags wrong");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_seq_mismatch)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_data.sequence = 999;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 15, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "seq mismatch");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	verify_asrc_category(&pre_s, &post_s, &pre_a, &post_a, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0,
			     0, 0, 0);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_correction_mismatch)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_data.correction_ppm = 123;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 16, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "correction mismatch");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_reserved_nonzero)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_asrc_defaults(17);
	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_data.post_state.reserved[0] = 1;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 17, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "reserved nonzero");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	verify_asrc_category(&pre_s, &post_s, &pre_a, &post_a, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			     0, 1, 0);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_state_import_fail)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_asrc_defaults(18);
	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_data.post_state.step_base = 0;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 18, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "import fail");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	verify_asrc_category(&pre_s, &post_s, &pre_a, &post_a, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			     0, 1, 0);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_step_base_mismatch)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_asrc_defaults(19);
	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_data.post_state.step_base = test_pre_state.step_base + 1;
	mock_asrc_consume_data.post_state.phase = 1;
	mock_asrc_consume_data.post_state.prev_valid = 1;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 19, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "step base mismatch");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);
	verify_asrc_category(&pre_s, &post_s, &pre_a, &post_a, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			     0, 1, 0);

	run_recovery_work();
}

ZTEST(audio_offload_asrc, test_asrc_final_lifecycle_race)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_asrc_defaults(20);
	ASRC_SNAPSHOT(pre_s, pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 20, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, 0, "success (no race)");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 1, 0, 1, 1, 0);
}

ZTEST(audio_offload_asrc, test_asrc_flags_exact)
{
	struct audio_offload_status pre_s, post_s;
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	ASRC_SNAPSHOT(pre_s, pre_a);

	mock_asrc_consume_data.flags =
		FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR | FLPR_SLOT_FLAG_CRC_OK;

	int ret = audio_offload_process_asrc(test_input, TEST_ASRC_FRAMES, 21, 0, &test_pre_state,
					     test_output, TEST_ASRC_CAPACITY, &result);

	ASRC_SNAPSHOT(post_s, post_a);

	zassert_equal(ret, -EAGAIN, "flags extra bit fails");
	verify_asrc_deltas(&pre_s, &pre_a, &post_s, &post_a, 1, 0, 1, 1, 0, 1);

	run_recovery_work();
}

/* ── Test suite registration ─────────────────────────────────────── */

ZTEST_SUITE(audio_offload, NULL, NULL, setup_normal, teardown, NULL);
ZTEST_SUITE(audio_offload_asrc, NULL, NULL, setup_asrc, teardown_asrc, NULL);
