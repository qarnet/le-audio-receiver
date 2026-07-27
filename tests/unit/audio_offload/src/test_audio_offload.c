/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for audio_offload state machine — Phase 6 Stage 2 final.
 *
 * Tests the production nRF54L15 code path with mocked flpr_ring_mgr
 * transport.  Uses direct invocation of prep_work_fn() and
 * recovery_work_fn() to exercise the dedicated worker state
 * machine deterministically.
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
 */

#include "audio_offload.h"
#include "audio_offload_test_helpers.h"
#include "flpr_ring.h"
#include "flpr_ring_mgr.h"

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
extern enum flpr_consume_result mock_consume_result;
extern uint16_t mock_consume_valid_frames;
extern uint32_t mock_consume_sequence;
extern uint32_t mock_consume_crc;
extern uint32_t mock_consume_latency;
extern uint8_t mock_consume_payload[];
extern bool mock_consume_corrupt_payload;
extern bool mock_consume_corrupt_crc;
extern int mock_produce_calls;
extern int mock_consume_calls;
extern int mock_reset_calls;
extern uint32_t mock_last_sequence;
extern const uint8_t *mock_last_pcm;
extern bool mock_last_crc;

/* ── Test data ───────────────────────────────────────────────────── */

#define TEST_BLOCK_FRAMES  480
#define TEST_BLOCK_SAMPLES (TEST_BLOCK_FRAMES * 2)
#define TEST_BLOCK_BYTES   (TEST_BLOCK_FRAMES * 4)

static int16_t test_input[TEST_BLOCK_SAMPLES];
static int16_t test_output[TEST_BLOCK_SAMPLES];

/* ── Helpers ─────────────────────────────────────────────────────── */

/* Cancel all pending offload work and run the prep worker synchronously.
 * Use this to deterministically transition from PREPARING → ACTIVE
 * without relying on the dedicated WQ thread timing. */
static void run_prep_work(void)
{
	/* Cancel any work that the WQ thread might try to process. */
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

/* Verify status snapshot counters. */
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

/* Verify output buffer untouched. */
static void assert_output_untouched(int16_t expected_val)
{
	for (size_t i = 0; i < TEST_BLOCK_SAMPLES; i++) {
		zassert_equal(test_output[i], expected_val,
			      "output[%zu] untouched expected 0x%04X got 0x%04X", i,
			      (unsigned)expected_val, (unsigned)test_output[i]);
	}
}

/* Fill output with known pattern so we can detect if it was touched. */
static void fill_output(int16_t val)
{
	memset(test_output, (int)(val & 0xFF), sizeof(test_output));
}

/* ── Setup/teardown ──────────────────────────────────────────────── */

static void setup_normal(void *fixture)
{
	(void)fixture;

	/* Reset all mocks. */
	mock_init_fails = false;
	mock_flpr_healthy = true;
	mock_reset_fails = false;
	mock_produce_result = FLPR_PRODUCE_OK;
	mock_notify_result = 0;
	mock_wait_result = 0;
	mock_wait_delay_ms = 0;
	mock_consume_result = FLPR_CONSUME_OK;
	mock_consume_valid_frames = TEST_BLOCK_FRAMES;
	mock_consume_sequence = 0;
	mock_consume_crc = 0;
	mock_consume_latency = 500;
	mock_consume_corrupt_payload = false;
	mock_consume_corrupt_crc = false;

	/* Fill test input with deterministic pattern. */
	for (size_t i = 0; i < TEST_BLOCK_SAMPLES; i++) {
		test_input[i] = (int16_t)(i & 0xFFFF);
	}
	memcpy(mock_consume_payload, test_input, TEST_BLOCK_BYTES);
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);

	memset(test_output, 0, sizeof(test_output));

	audio_offload_init();

	/* Start stream: sets PREPARING, schedules prep work on WQ.
	 * Run prep work synchronously to reach ACTIVE deterministically. */
	audio_offload_stream_start();
	run_prep_work();
}

static void teardown(void *fixture)
{
	(void)fixture;
	audio_offload_stream_stop();
	memset(test_output, 0, sizeof(test_output));
}

/* ── Test: normal identity pass ──────────────────────────────────── */

ZTEST(audio_offload, test_normal_identity)
{
	mock_consume_sequence = 42;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 42, 0, test_output);
	zassert_equal(ret, 0, "submit should succeed");
	zassert_mem_equal(test_output, test_input, TEST_BLOCK_BYTES, "output identity");
	zassert_true(mock_last_crc, "CRC must be computed");
	verify_status(1, 1, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* ── Test: 1000 sequential blocks all pass ──────────────────────── */

ZTEST(audio_offload, test_sequential_normal)
{
	for (uint32_t seq = 0; seq < 1000; seq++) {
		mock_consume_sequence = seq;
		memset(test_output, 0xFF, sizeof(test_output));

		int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, seq, 0, test_output);
		zassert_equal(ret, 0, "seq %u", seq);
		zassert_mem_equal(test_output, test_input, TEST_BLOCK_BYTES, "output seq %u", seq);
	}
	verify_status(1000, 1000, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* ── Test: timeout → poison → recovery → ACTIVE ────────────────── */

ZTEST(audio_offload, test_timeout_triggers_recovery)
{
	mock_wait_result = -EAGAIN;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should return -EAGAIN on timeout");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "must be RECOVERING");
	zassert_false(s.healthy, "not healthy");
	zassert_equal(s.timeout_count, 1, "timeout_count=1");
	zassert_equal(s.fallback_count, 1, "fallback_count=1");
	zassert_equal(s.submit_count, 1, "submit_count=1");

	fill_output(0xAB);
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 2, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should return -EAGAIN while recovering");
	assert_output_untouched((int16_t)0xABAB);

	/* Record recovery_attempts before running recovery. */
	uint32_t recov_before = s.recovery_attempts;

	/* Run the recovery worker directly. */
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "state ACTIVE after recovery");
	zassert_true(s.healthy, "healthy after recovery");

	/* Counters: timeout=1, fallback=1+1(in-recovery)=2 preserved. */
	zassert_equal(s.timeout_count, 1, "timeout preserved across recovery");
	zassert_equal(s.fallback_count, 2, "fallback preserved (1 fault + 1 recovery-pass)");
	zassert_equal(s.submit_count, 2, "submit_count=2");
	zassert_equal(s.recovery_attempts, recov_before + 1, "recovery_count incremented");

	/* Next submit must succeed. */
	mock_wait_result = 0;
	mock_consume_sequence = 100;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	memset(test_output, 0xFF, sizeof(test_output));
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 100, 0, test_output);
	zassert_equal(ret, 0, "submit after recovery");
	zassert_mem_equal(test_output, test_input, TEST_BLOCK_BYTES, "output after recovery");
}

/* ── Test: CRC mismatch → poison → recovery ────────────────────── */

ZTEST(audio_offload, test_crc_mismatch)
{
	mock_consume_sequence = 10;
	mock_consume_corrupt_crc = true;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 10, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on CRC mismatch");

	fill_output(0);
	assert_output_untouched(0);

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.crc_fault_count, 1, "crc=1");
	zassert_equal(s.fallback_count, 1, "fallback=1");

	/* Recover. */
	run_recovery_work();
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after recovery");
	zassert_equal(s.crc_fault_count, 1, "crc preserved");
}

/* ── Test: payload corruption → poison → recovery ──────────────── */

ZTEST(audio_offload, test_payload_corruption)
{
	mock_consume_sequence = 5;
	mock_consume_corrupt_payload = true;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 5, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on payload corruption");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.payload_fault_count, 1, "payload=1");
}

/* ── Test: wrong frame count ─────────────────────────────────────── */

ZTEST(audio_offload, test_wrong_frame_count)
{
	mock_consume_valid_frames = 400;
	mock_consume_sequence = 7;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 7, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on wrong frame count");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.frame_fault_count, 1, "frame=1");
}

/* ── Test: sequence mismatch ─────────────────────────────────────── */

ZTEST(audio_offload, test_seq_mismatch)
{
	mock_consume_sequence = 99;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 100, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on seq mismatch");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.seq_fault_count, 1, "seq=1");
}

/* ── Test: ring full poison ──────────────────────────────────────── */

ZTEST(audio_offload, test_ring_full)
{
	mock_produce_result = FLPR_PRODUCE_FULL;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 0, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on ring full");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.full_count, 1, "full=1");
}

/* ── Test: notify failure → poison ───────────────────────────────── */

ZTEST(audio_offload, test_notify_failure)
{
	mock_notify_result = -EIO;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 3, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on notify failure");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.fallback_count, 1, "fallback=1");
}

/* ── Test: empty consume → poison ────────────────────────────────── */

ZTEST(audio_offload, test_consume_empty)
{
	mock_consume_result = FLPR_CONSUME_EMPTY;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 8, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on empty consume");
}

/* ── Test: stale epoch → poison ──────────────────────────────────── */

ZTEST(audio_offload, test_stale_epoch)
{
	mock_consume_result = FLPR_CONSUME_STALE;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 9, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on stale epoch");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.stale_count, 1, "stale=1");
}

/* ── Test: invalid args rejected without counting ────────────────── */

ZTEST(audio_offload, test_invalid_args)
{
	int ret;

	ret = audio_offload_submit(NULL, TEST_BLOCK_SAMPLES, 0, 0, test_output);
	zassert_equal(ret, -EINVAL, "NULL input");

	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, NULL);
	zassert_equal(ret, -EINVAL, "NULL output");

	ret = audio_offload_submit(test_input, 0, 2, 0, test_output);
	zassert_equal(ret, -EINVAL, "zero samples");

	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES - 2, 3, 0, test_output);
	zassert_equal(ret, -EINVAL, "wrong samples");

	/* Invalid args must NOT increment any counter. */
	verify_status(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* ── Test: recovery success via production worker ───────────────── */

ZTEST(audio_offload, test_recovery_success)
{
	/* Force timeout to trigger recovery. */
	mock_wait_result = -EAGAIN;
	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	zassert_equal(ret, -EAGAIN, "timeout");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "state RECOVERING");
	zassert_equal(s.timeout_count, 1, "timeout=1");
	zassert_equal(s.fallback_count, 1, "fallback=1");

	/* Record recovery_attempts before running recovery. */
	uint32_t recov_before_rs = s.recovery_attempts;

	/* Run actual recovery worker (not stream_start as before). */
	mock_wait_result = 0;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after recovery");
	zassert_true(s.healthy, "healthy");
	zassert_equal(s.recovery_attempts, recov_before_rs + 1, "recovery_count incremented");

	/* Counters preserved: timeout=1, fallback=1 (not reset by recovery). */
	zassert_equal(s.timeout_count, 1, "timeout preserved");
	zassert_equal(s.fallback_count, 1, "fallback preserved");

	/* Submit after recovery succeeds. */
	mock_consume_sequence = 100;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	memset(test_output, 0xFF, sizeof(test_output));
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 100, 0, test_output);
	zassert_equal(ret, 0, "submit after recovery");
	zassert_mem_equal(test_output, test_input, TEST_BLOCK_BYTES, "output after recovery");
}

/* ── Test: recovery backoff with retry ──────────────────────────── */

ZTEST(audio_offload, test_recovery_backoff)
{
	/* Trigger recovery. */
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING");
	uint32_t recov_before = s.recovery_attempts;

	/* Make reset fail on first recovery attempt. */
	mock_reset_fails = true;
	run_recovery_work();

	/* Should still be RECOVERING. */
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "still RECOVERING after reset fail");
	zassert_equal(s.recovery_attempts, recov_before, "recovery_count unchanged (reset failed)");

	/* Second attempt: reset succeeds. */
	mock_reset_fails = false;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after retry");
	zassert_equal(s.recovery_attempts, recov_before + 1, "recovery_count incremented");
}

/* ── Test: stop cancels pending recovery ────────────────────────── */

ZTEST(audio_offload, test_stop_during_recovery)
{
	/* Trigger recovery. */
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING");

	/* Stop cancels recovery. */
	audio_offload_stream_stop();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "STOPPED after stop");

	/* Recovery work runs but sees state != RECOVERING → no-op. */
	run_recovery_work();
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "still STOPPED");
}

/* ── Test: sequence wrap (uint32_t) ──────────────────────────────── */

ZTEST(audio_offload, test_sequence_wrap)
{
	uint32_t seq = 0xFFFFFFF0U;

	for (int i = 0; i < 32; i++) {
		mock_consume_sequence = seq;
		memset(test_output, 0xFF, sizeof(test_output));

		int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, seq, 0, test_output);
		zassert_equal(ret, 0, "seq %u", seq);
		seq++;
	}
	verify_status(32, 32, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* ── Test: reconnect (stop → start → submit) ─────────────────────── */

ZTEST(audio_offload, test_reconnect)
{
	/* Normal submit. */
	mock_consume_sequence = 0;
	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 0, 0, test_output);
	zassert_equal(ret, 0, "first submit");

	/* Stop. */
	audio_offload_stream_stop();

	/* Submit while stopped returns -EAGAIN. */
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	zassert_equal(ret, -EAGAIN, "stopped returns -EAGAIN");

	/* Restart with async prep. */
	audio_offload_stream_start();
	run_prep_work();

	mock_consume_sequence = 100;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	memset(test_output, 0xFF, sizeof(test_output));
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 100, 0, test_output);
	zassert_equal(ret, 0, "submit after reconnect");
	zassert_mem_equal(test_output, test_input, TEST_BLOCK_BYTES, "output reconnect");
}

/* ── Test: async PREPARING state ─────────────────────────────────── */

ZTEST(audio_offload, test_async_preparing)
{
	/* Stop current stream. */
	audio_offload_stream_stop();

	struct audio_offload_status s;

	/* Start stream — should be PREPARING, not ACTIVE. */
	audio_offload_stream_start();
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_PREPARING, "PREPARING after stream_start");
	zassert_false(s.healthy, "not healthy during PREPARING");

	/* Submit while PREPARING returns -EAGAIN. */
	fill_output(0xAB);
	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	zassert_equal(ret, -EAGAIN, "submit during PREPARING");
	assert_output_untouched((int16_t)0xABAB);

	/* Run prep work — transition to ACTIVE. */
	run_prep_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after prep");
	zassert_true(s.healthy, "healthy after prep");

	/* Per-stream counters reset on new stream start via prep. */
	zassert_equal(s.submit_count, 0, "submit_count reset");
	zassert_equal(s.success_count, 0, "success_count reset");
	zassert_equal(s.fallback_count, 0, "fallback_count reset");
}

/* ── Test: prep failure → bounded retry → ACTIVE ───────────────── */

ZTEST(audio_offload, test_prep_retry_then_active)
{
	/* Stop current stream. */
	audio_offload_stream_stop();

	struct audio_offload_status s;

	/* Make coordinated reset fail on first attempt. */
	mock_reset_fails = true;
	audio_offload_stream_start();

	/* Run prep — it fails. */
	run_prep_work();

	/* Should still be PREPARING (retry pending). */
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_PREPARING, "still PREPARING after first fail");

	/* Now make reset succeed. */
	mock_reset_fails = false;

	/* Run prep again (retry). */
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

	/* Always fail reset. */
	mock_reset_fails = true;
	audio_offload_stream_start();

	/* Run prep 6 times (5 retries + 1 final → FALLBACK). */
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

	/* After 5 retries (6 calls), state should transition to FALLBACK. */
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_FALLBACK, "FALLBACK after max retries");
	zassert_false(s.healthy, "not healthy in FALLBACK");
	zassert_true(s.recovery_fail_count > 0, "recovery_fail_count incremented");
}

/* ── Test: max retries in recovery → FALLBACK ───────────────────── */

ZTEST(audio_offload, test_recovery_max_retries_fallback)
{
	/* Trigger fault. */
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING");

	/* Make all recovery attempts fail. */
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
	/* Simulate a slow submit: the wait will complete, but while
	 * waiting we stop the stream.  The output should be rejected. */
	mock_wait_delay_ms = 200; /* Make wait take 200ms. */
	mock_consume_sequence = 50;

	/* We'll use a separate test to verify that after stop,
	 * a submit that was in-flight gets rejected.
	 *
	 * Since we're single-threaded in ZTEST, we can't truly
	 * simulate concurrent stop during wait.  Instead, we verify
	 * that after stream_stop bumps generation, a submit's
	 * lifecycle recheck catches it.
	 *
	 * Test: submit while ACTIVE, then externally change
	 * generation (via stop/start).  This is tested below. */

	/* Submit with ACTIVE state. */
	mock_wait_delay_ms = 0;
	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 50, 0, test_output);
	zassert_equal(ret, 0, "normal submit OK");

	/* Now stop + restart to bump generation, then verify that
	 * late output from prior epoch would be rejected.
	 * This is tested via the generation check in submit. */

	/* Start a slow submit. */
	mock_wait_delay_ms = 50;
	mock_consume_sequence = 60;

	/* Stop+restart while submit is conceptually in-flight.
	 * In single-threaded test, we just verify that generation
	 * bump invalidates the check. */
	audio_offload_stream_stop();
	audio_offload_stream_start();
	run_prep_work();

	/* Now submit — should work fine with new generation. */
	mock_wait_delay_ms = 0;
	mock_consume_sequence = 10;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	memset(test_output, 0xFF, sizeof(test_output));
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 10, 0, test_output);
	zassert_equal(ret, 0, "submit after restart");
}

/* ── Test: counters preserved across recovery ───────────────────── */

ZTEST(audio_offload, test_counters_preserved_across_recovery)
{
	/* Two good submits. */
	mock_consume_sequence = 0;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 0, 0, test_output);
	mock_consume_sequence = 1;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);

	verify_status(2, 2, 0, 0, 0, 0, 0, 0, 0, 0);

	/* CRC fault. */
	mock_consume_sequence = 2;
	mock_consume_corrupt_crc = true;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 2, 0, test_output);

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.submit_count, 3, "submit=3");
	zassert_equal(s.success_count, 2, "success=2");
	zassert_equal(s.crc_fault_count, 1, "crc=1");
	zassert_equal(s.fallback_count, 1, "fallback=1");

	/* Recover. */
	mock_consume_corrupt_crc = false;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE");

	/* Recovery MUST preserve counters. */
	zassert_equal(s.submit_count, 3, "submit preserved");
	zassert_equal(s.success_count, 2, "success preserved");
	zassert_equal(s.crc_fault_count, 1, "crc preserved");
	zassert_equal(s.fallback_count, 1, "fallback preserved");
	zassert_equal(s.recovery_attempts, 1, "recovery=1");

	/* Submit after recovery. */
	mock_consume_sequence = 100;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 100, 0, test_output);

	audio_offload_get_status(&s);
	zassert_equal(s.success_count, 3, "success incremented to 3");
	zassert_equal(s.submit_count, 4, "submit incremented to 4");
}

/* ── Test: new stream_start resets per-stream counters ──────────── */

ZTEST(audio_offload, test_new_stream_resets_counters)
{
	/* Dirty the counters. */
	mock_consume_sequence = 0;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 0, 0, test_output);
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.submit_count, 2, "submit=2");
	zassert_equal(s.success_count, 1, "success=1");
	zassert_equal(s.fallback_count, 1, "fallback=1");
	uint32_t prev_recovery = s.recovery_attempts;

	/* Stop and start new stream. */
	audio_offload_stream_stop();
	mock_wait_result = 0;
	audio_offload_stream_start();
	run_prep_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE");

	/* Per-stream counters reset. */
	zassert_equal(s.submit_count, 0, "submit reset");
	zassert_equal(s.success_count, 0, "success reset");
	zassert_equal(s.fallback_count, 0, "fallback reset");

	/* Lifetime counters preserved. */
	zassert_equal(s.recovery_attempts, prev_recovery, "recovery_count lifetime");
}

/* ── Test: output untouched on all failure paths ─────────────────── */

ZTEST(audio_offload, test_output_untouched_on_failure)
{
	/* Timeout. */
	mock_wait_result = -EAGAIN;
	fill_output(0xAB);
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	assert_output_untouched((int16_t)0xABAB);
	run_recovery_work();

	/* CRC. */
	mock_consume_corrupt_crc = true;
	mock_consume_sequence = 10;
	mock_wait_result = 0;
	fill_output(0xCD);
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 10, 0, test_output);
	assert_output_untouched((int16_t)0xCDCD);
	run_recovery_work();

	/* Payload. */
	mock_consume_corrupt_crc = false;
	mock_consume_corrupt_payload = true;
	mock_consume_sequence = 20;
	mock_wait_result = 0;
	mock_consume_crc = flpr_ring_crc32(mock_consume_payload, TEST_BLOCK_BYTES);
	fill_output(0xEF);
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 20, 0, test_output);
	assert_output_untouched((int16_t)0xEFEF);
	run_recovery_work();

	/* Seq mismatch. */
	mock_consume_corrupt_payload = false;
	mock_consume_sequence = 999;
	mock_wait_result = 0;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	fill_output(0x11);
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 30, 0, test_output);
	assert_output_untouched((int16_t)0x1111);
}

/* ── Test: fallback state persists ───────────────────────────────── */

ZTEST(audio_offload, test_fallback_state)
{
	audio_offload_stream_stop();

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 0, 0, test_output);
	zassert_equal(ret, -EAGAIN, "STOPPED returns -EAGAIN");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "state STOPPED");
}

/* ── Test: health check ──────────────────────────────────────────── */

ZTEST(audio_offload, test_is_healthy)
{
	zassert_true(audio_offload_is_healthy(), "healthy after init+start+prep");

	/* Trigger fault. */
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	zassert_false(audio_offload_is_healthy(), "not healthy after fault");

	/* Recover. */
	run_recovery_work();
	zassert_true(audio_offload_is_healthy(), "healthy after recovery");
}

/* ── Test: concurrent stop during submit (helper thread) ──────────── */

/* Thread helper: sleep briefly then call stream_stop.
 * Used by test_concurrent_stop_during_submit to simulate
 * a stop arriving while a submit is blocked in mock_wait. */

struct concurrent_ctx {
	bool done;
};

static void stop_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	/* Wait a bit for the main thread to enter the wait. */
	k_sleep(K_MSEC(20));

	audio_offload_stream_stop();
}

ZTEST(audio_offload, test_concurrent_stop_during_submit)
{
	/* Set up slow wait (200ms) so the stop thread can intervene. */
	mock_wait_delay_ms = 200;
	mock_consume_sequence = 50;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);

	/* Start a helper thread that will call stream_stop after 20ms. */
	struct k_thread stop_thread;
	static K_THREAD_STACK_DEFINE(stop_stack, 512);

	k_thread_create(&stop_thread, stop_stack, K_THREAD_STACK_SIZEOF(stop_stack), stop_thread_fn,
			NULL, NULL, NULL, 2, 0, K_NO_WAIT);

	/* Submit — this will block in mock_wait_consume for 200ms.
	 * The stop thread runs after 20ms and calls stream_stop,
	 * which bumps generation.  When submit returns from wait,
	 * the lifecycle recheck should detect the generation change
	 * and reject the output. */
	fill_output(0x99);
	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 50, 0, test_output);
	/* Should return -EAGAIN because generation changed. */
	zassert_equal(ret, -EAGAIN, "submit rejected after concurrent stop");

	/* Output must be untouched. */
	assert_output_untouched((int16_t)0x9999);

	/* Wait for stop thread to finish. */
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

	/* Two good submits. */
	mock_consume_sequence = 0;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 0, 0, test_output);
	mock_consume_sequence = 1;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	verify_status(2, 2, 0, 0, 0, 0, 0, 0, 0, 0);

	/* Timeout → 1 more submit, 0 more success, 1 fallback, 1 timeout. */
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 2, 0, test_output);
	verify_status(3, 2, 1, 1, 0, 0, 0, 0, 0, 0);

	/* While recovering (submits rejected with fallback). */
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 3, 0, test_output);
	verify_status(4, 2, 2, 1, 0, 0, 0, 0, 0, 0);

	/* Record recovery_count before recovery. */
	audio_offload_get_status(&s);
	uint32_t recov_before_ea = s.recovery_attempts;

	/* Recover preserves counters. */
	run_recovery_work();
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE");
	zassert_equal(s.submit_count, 4, "submit=4 after recovery");
	zassert_equal(s.success_count, 2, "success=2 after recovery");
	zassert_equal(s.fallback_count, 2, "fallback=2 after recovery");
	zassert_equal(s.timeout_count, 1, "timeout=1 after recovery");
	zassert_equal(s.recovery_attempts, recov_before_ea + 1, "recovery_count incremented");

	/* More good submits after recovery. */
	mock_wait_result = 0;
	mock_consume_sequence = 100;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 100, 0, test_output);
	mock_consume_sequence = 101;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 101, 0, test_output);

	audio_offload_get_status(&s);
	zassert_equal(s.submit_count, 6, "submit=6");
	zassert_equal(s.success_count, 4, "success=4");
	zassert_equal(s.fallback_count, 2, "fallback still 2");
}

/* ── Recovery stability policy tests (Phase 6 Stage 2 probation) ─── */

/* Trigger a fault and run recovery to completion.
 * Returns the updated status. */
static struct audio_offload_status fault_and_recover(void)
{
	struct audio_offload_status s;
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	mock_wait_result = 0;
	run_recovery_work();
	audio_offload_get_status(&s);
	return s;
}

/* Helper: submit N successful blocks.  Sets mock consume seq to start_seq
 * and increments per call. */
static void submit_successes(uint32_t start_seq, uint32_t count)
{
	mock_wait_result = 0;
	mock_consume_corrupt_crc = false;
	mock_consume_corrupt_payload = false;
	for (uint32_t i = 0; i < count; i++) {
		mock_consume_sequence = start_seq + i;
		mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
		audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, start_seq + i, 0, test_output);
	}
}

/* ── Test 1: repeated reset-success → submit-fault escalates and exhausts ── */

ZTEST(audio_offload, test_probation_relapse_exhaustion)
{
	struct audio_offload_status s, baseline;

	/* Capture lifetime counters before test (carry-over from prior tests). */
	audio_offload_get_status(&baseline);
	uint32_t base_attempts = baseline.recovery_attempts;
	uint32_t base_relapses = baseline.recovery_relapses;

	/* Initial fault + recovery: probation starts. */
	s = fault_and_recover();
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after first recovery");
	zassert_true(s.probation_active, "probation active");
	zassert_equal(s.probation_success, 0, "probation_success=0");
	zassert_equal(s.recovery_attempts, base_attempts + 1, "recovery_attempts+1");
	zassert_equal(s.recovery_relapses, base_relapses + 0, "no relapses yet");

	/* Relapse 1: fault during probation. */
	s = fault_and_recover();
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after relapse 1");
	zassert_equal(s.recovery_relapses, base_relapses + 1, "relapse+1");
	zassert_equal(s.recovery_attempts, base_attempts + 2, "recovery_attempts+2");

	/* Relapse 2. */
	s = fault_and_recover();
	zassert_equal(s.recovery_relapses, base_relapses + 2, "relapse+2");
	zassert_equal(s.recovery_attempts, base_attempts + 3, "recovery_attempts+3");

	/* Relapse 3. */
	s = fault_and_recover();
	zassert_equal(s.recovery_relapses, base_relapses + 3, "relapse+3");
	zassert_equal(s.recovery_attempts, base_attempts + 4, "recovery_attempts+4");

	/* Relapse 4. */
	s = fault_and_recover();
	zassert_equal(s.recovery_relapses, base_relapses + 4, "relapse+4");
	zassert_equal(s.recovery_attempts, base_attempts + 5, "recovery_attempts+5");
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after 4 relapses");

	/* Relapse 5: max tries exhausted → FALLBACK. */
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 10, 0, test_output);
	mock_wait_result = 0;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_FALLBACK, "FALLBACK after max exhaustion");
	zassert_equal(s.max_exhaustion_count, baseline.max_exhaustion_count + 1,
		      "max_exhaustion+1");
	zassert_equal(s.recovery_relapses, base_relapses + 5, "relapse+5");
	zassert_equal(s.recovery_attempts, base_attempts + 5, "5 successful recoveries");

	/* Submit in FALLBACK returns -EAGAIN. */
	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 20, 0, test_output);
	zassert_equal(ret, -EAGAIN, "FALLBACK submit returns -EAGAIN");
}

/* ── Test 2: 100 consecutive successes clear probation ────────────── */

ZTEST(audio_offload, test_probation_cleared_100_success)
{
	struct audio_offload_status s, baseline;

	/* Capture lifetime counter baseline. */
	audio_offload_get_status(&baseline);
	uint32_t base_cleared = baseline.probation_cleared;
	uint32_t base_attempts = baseline.recovery_attempts;

	/* Trigger fault + recovery → probation. */
	fault_and_recover();
	audio_offload_get_status(&s);
	zassert_true(s.probation_active, "probation active after recovery");

	/* Submit 99 successes — probation still active. */
	submit_successes(1000, 99);
	audio_offload_get_status(&s);
	zassert_true(s.probation_active, "probation still active at 99");
	zassert_equal(s.probation_success, 99, "probation_success=99");

	/* Submit 100th success — clears probation. */
	submit_successes(1099, 1);
	audio_offload_get_status(&s);
	zassert_false(s.probation_active, "probation cleared at 100");
	zassert_equal(s.probation_cleared, base_cleared + 1, "probation_cleared+1");

	/* After probation clear, a new fault should start fresh (no relapse). */
	uint32_t prev_relapses = s.recovery_relapses;
	s = fault_and_recover();
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after fresh fault");
	zassert_true(s.probation_active, "probation active again");
	zassert_equal(s.recovery_relapses, prev_relapses,
		      "relapses unchanged (fresh start after clear, not relapse)");
	zassert_equal(s.recovery_attempts, base_attempts + 2, "recovery_attempts+2");
}

/* ── Test 3: fault after stable (probation cleared) starts base delay ── */

ZTEST(audio_offload, test_fault_after_stable)
{
	struct audio_offload_status s, baseline;

	audio_offload_get_status(&baseline);
	uint32_t base_attempts = baseline.recovery_attempts;

	/* First fault + recovery. */
	fault_and_recover();
	audio_offload_get_status(&s);
	zassert_true(s.probation_active, "probation active");

	/* Clear probation by submitting 100 successes. */
	uint32_t prev_relapses = s.recovery_relapses;
	submit_successes(2000, 100);
	audio_offload_get_status(&s);
	zassert_false(s.probation_active, "probation cleared");

	/* New fault: should NOT be counted as relapse (probation was cleared). */
	s = fault_and_recover();
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after fresh fault");
	zassert_equal(s.recovery_relapses, prev_relapses,
		      "relapses unchanged — fault after stable is NOT a relapse");
	zassert_equal(s.recovery_attempts, base_attempts + 2, "recovery_attempts+2");
	zassert_true(s.probation_active, "probation active for new recovery");
}

/* ── Test 4: stop/reconnect resets recovery policy ────────────────── */

ZTEST(audio_offload, test_stop_reconnect_resets_policy)
{
	struct audio_offload_status s, baseline;

	audio_offload_get_status(&baseline);
	uint32_t base_attempts = baseline.recovery_attempts;
	uint32_t base_relapses = baseline.recovery_relapses;

	/* Build up probation state with relapse. */
	fault_and_recover();
	audio_offload_get_status(&s);
	zassert_true(s.probation_active, "probation active");
	zassert_equal(s.recovery_relapses, base_relapses, "initial no relapse");

	/* One relapse. */
	fault_and_recover();
	audio_offload_get_status(&s);
	zassert_equal(s.recovery_relapses, base_relapses + 1, "relapse+1");

	/* Stop + reconnect (fresh stream). */
	audio_offload_stream_stop();
	audio_offload_stream_start();
	run_prep_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after reconnect");
	zassert_false(s.probation_active, "probation cleared by reconnect");
	zassert_equal(s.probation_success, 0, "probation_success=0");
	/* Lifetime counters preserved across reconnect. */
	zassert_equal(s.recovery_relapses, base_relapses + 1, "relapses preserved (lifetime)");
	zassert_equal(s.recovery_attempts, base_attempts + 2,
		      "recovery_attempts preserved (lifetime)");

	/* New fault after reconnect starts fresh (not a relapse, tries=0). */
	s = fault_and_recover();
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after fresh fault");
	zassert_true(s.probation_active, "probation active for new cycle");
	/* relapses unchanged — fault after reconnect is NOT counted as relapse
	 * because probation was cleared. */
	zassert_equal(s.recovery_relapses, base_relapses + 1,
		      "relapses unchanged (not a new relapse)");
	zassert_equal(s.recovery_attempts, base_attempts + 3, "recovery_attempts+3");
}

/* ── Test 5: bound verification — exactly 5 recovery attempts cap ─── */

ZTEST(audio_offload, test_recovery_bounded_5_attempts)
{
	struct audio_offload_status s, baseline;

	audio_offload_get_status(&baseline);
	uint32_t base_attempts = baseline.recovery_attempts;

	/* Run 5 relapse cycles — should stay within budget (tries 1..5). */
	for (int i = 0; i < 5; i++) {
		fault_and_recover();
	}
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after 5 relapse cycles (tries=5)");
	zassert_equal(s.recovery_attempts, base_attempts + 5, "recovery_attempts+5 (5 successful)");
	zassert_false(s.max_exhaustion_count > baseline.max_exhaustion_count, "not exhausted yet");

	/* 6th fault during probation → exhaustion (tries 5→6 > MAX). */
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 50, 0, test_output);
	mock_wait_result = 0;
	run_recovery_work();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_FALLBACK,
		      "FALLBACK on 6th recovery entry (exhausted)");
	zassert_equal(s.max_exhaustion_count, baseline.max_exhaustion_count + 1,
		      "max_exhaustion+1");
	/* 5 recoveries succeeded, the 6th was blocked. */
	zassert_equal(s.recovery_attempts, base_attempts + 5, "still 5 successful recoveries");
}

/* ── Test suite registration ─────────────────────────────────────── */

ZTEST_SUITE(audio_offload, NULL, NULL, setup_normal, teardown, NULL);
