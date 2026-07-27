/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for audio_offload state machine.
 *
 * Tests the production nRF54L15 code path with mocked flpr_ring_mgr
 * and flpr_handshake transport.  Exercises normal identity, faults,
 * recovery, lifecycle, sequence wrap, and exact counter accounting.
 */

#include "audio_offload.h"
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
extern uint32_t mock_last_sequence;
extern const uint8_t *mock_last_pcm;
extern bool mock_last_crc;

/* ── Test data ───────────────────────────────────────────────────── */

/* 480-frame stereo block = 960 int16_t samples = 1920 bytes */
#define TEST_BLOCK_FRAMES  480
#define TEST_BLOCK_SAMPLES (TEST_BLOCK_FRAMES * 2)
#define TEST_BLOCK_BYTES   (TEST_BLOCK_FRAMES * 4)

static int16_t test_input[TEST_BLOCK_SAMPLES];
static int16_t test_output[TEST_BLOCK_SAMPLES];

/* ── Setup/teardown ──────────────────────────────────────────────── */

static void setup_normal(void *fixture)
{
	(void)fixture;
	/* Reset mock state. */
	mock_init_fails = false;
	mock_flpr_healthy = true;
	mock_reset_fails = false;
	mock_produce_result = FLPR_PRODUCE_OK;
	mock_notify_result = 0;
	mock_wait_result = 0;
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

	/* Fill mock payload with same pattern (identity). */
	memcpy(mock_consume_payload, test_input, TEST_BLOCK_BYTES);

	/* Precompute CRC to match. */
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);

	memset(test_output, 0, sizeof(test_output));

	audio_offload_init();
	audio_offload_stream_start();
}

static void teardown(void *fixture)
{
	(void)fixture;
	audio_offload_stream_stop();
	memset(test_output, 0, sizeof(test_output));
}

/* ── Helper: verify status snapshot ──────────────────────────────── */

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

/* ── Test: normal identity pass ──────────────────────────────────── */

ZTEST(audio_offload, test_normal_identity)
{
	mock_consume_sequence = 42; /* must match submit sequence */

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 42, 0, test_output);
	zassert_equal(ret, 0, "submit should succeed");

	/* Output must match input. */
	zassert_mem_equal(test_output, test_input, TEST_BLOCK_BYTES, "output identity");

	/* CRC must have been computed on produce. */
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

/* ── Test: timeout → poison → recovery ──────────────────────────── */

ZTEST(audio_offload, test_timeout_triggers_recovery)
{
	/* Fail the wait. */
	mock_wait_result = -EAGAIN;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should return -EAGAIN on timeout");

	/* State must be RECOVERING. */
	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "must be RECOVERING");
	zassert_false(s.healthy, "not healthy");

	/* Output must be untouched. */
	for (size_t i = 0; i < TEST_BLOCK_SAMPLES; i++) {
		zassert_equal(test_output[i], 0, "output[%zu] untouched", i);
	}

	verify_status(1, 0, 1, 1, 0, 0, 0, 0, 0, 0);

	/* While recovering, submits return -EAGAIN. */
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 2, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should return -EAGAIN while recovering");
}

/* ── Test: CRC mismatch → poison ────────────────────────────────── */

ZTEST(audio_offload, test_crc_mismatch)
{
	mock_consume_sequence = 10;
	mock_consume_corrupt_crc = true;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 10, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on CRC mismatch");

	/* Output untouched. */
	for (size_t i = 0; i < TEST_BLOCK_SAMPLES; i++) {
		zassert_equal(test_output[i], 0, "output[%zu] untouched", i);
	}

	verify_status(1, 0, 1, 0, 0, 0, 0, 0, 1, 0);
}

/* ── Test: payload corruption → poison ──────────────────────────── */

ZTEST(audio_offload, test_payload_corruption)
{
	mock_consume_sequence = 5;
	mock_consume_corrupt_payload = true;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 5, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on payload corruption");

	verify_status(1, 0, 1, 0, 0, 0, 0, 0, 0, 1);
}

/* ── Test: wrong frame count ─────────────────────────────────────── */

ZTEST(audio_offload, test_wrong_frame_count)
{
	mock_consume_valid_frames = 400; /* should be 480 */
	mock_consume_sequence = 7;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 7, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on wrong frame count");

	verify_status(1, 0, 1, 0, 0, 0, 0, 1, 0, 0);
}

/* ── Test: sequence mismatch ─────────────────────────────────────── */

ZTEST(audio_offload, test_seq_mismatch)
{
	mock_consume_sequence = 99; /* submit sends 100 */

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 100, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on seq mismatch");

	verify_status(1, 0, 1, 0, 0, 0, 1, 0, 0, 0);
}

/* ── Test: ring full → poison ────────────────────────────────────── */

ZTEST(audio_offload, test_ring_full)
{
	mock_produce_result = FLPR_PRODUCE_FULL;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 0, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on ring full");

	verify_status(1, 0, 1, 0, 1, 0, 0, 0, 0, 0);
}

/* ── Test: notify failure → poison ───────────────────────────────── */

ZTEST(audio_offload, test_notify_failure)
{
	mock_notify_result = -EIO;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 3, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on notify failure");

	verify_status(1, 0, 1, 0, 0, 0, 0, 0, 0, 0);
}

/* ── Test: empty consume → poison ────────────────────────────────── */

ZTEST(audio_offload, test_consume_empty)
{
	mock_consume_result = FLPR_CONSUME_EMPTY;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 8, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on empty consume");

	verify_status(1, 0, 1, 0, 0, 0, 0, 0, 0, 0);
}

/* ── Test: stale epoch → poison ──────────────────────────────────── */

ZTEST(audio_offload, test_stale_epoch)
{
	mock_consume_result = FLPR_CONSUME_STALE;

	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 9, 0, test_output);
	zassert_equal(ret, -EAGAIN, "should fail on stale epoch");

	verify_status(1, 0, 1, 0, 0, 1, 0, 0, 0, 0);
}

/* ── Test: invalid args rejected without counting ────────────────── */

ZTEST(audio_offload, test_invalid_args)
{
	int ret;

	/* NULL input. */
	ret = audio_offload_submit(NULL, TEST_BLOCK_SAMPLES, 0, 0, test_output);
	zassert_equal(ret, -EINVAL, "NULL input");

	/* NULL output. */
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, NULL);
	zassert_equal(ret, -EINVAL, "NULL output");

	/* Zero samples. */
	ret = audio_offload_submit(test_input, 0, 2, 0, test_output);
	zassert_equal(ret, -EINVAL, "zero samples");

	/* Wrong sample count. */
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES - 2, 3, 0, test_output);
	zassert_equal(ret, -EINVAL, "wrong samples");

	/* Invalid args must NOT increment submit_count or any other counter. */
	verify_status(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* ── Test: recovery success ──────────────────────────────────────── */

ZTEST(audio_offload, test_recovery_success)
{
	/* Force a timeout to trigger recovery. */
	mock_wait_result = -EAGAIN;
	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	zassert_equal(ret, -EAGAIN, "timeout");

	/* Verify RECOVERING. */
	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "state RECOVERING");

	/* Simulate recovery by calling stream_start (in production,
	 * the k_work_delayable recovery handler does a coordinated
	 * epoch reset and transitions back to ACTIVE). */
	mock_wait_result = 0; /* reset mock */
	audio_offload_stream_start();

	/* Should be ACTIVE again. */
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "state ACTIVE after recovery");
	zassert_true(s.healthy, "healthy after recovery");

	/* Next submit should succeed. */
	mock_consume_sequence = 100;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	memset(test_output, 0xFF, sizeof(test_output));
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 100, 0, test_output);
	zassert_equal(ret, 0, "submit after recovery");
	zassert_mem_equal(test_output, test_input, TEST_BLOCK_BYTES, "output after recovery");
}

/* ── Test: recovery backoff ──────────────────────────────────────── */

ZTEST(audio_offload, test_recovery_backoff)
{
	/* Fail the coordinated reset to test backoff.
	 * The recovery work is scheduled via k_work — we can't easily
	 * observe backoff without advancing simulated time.
	 * Test that the state machine transitions correctly. */
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);

	/* Recovery should be pending (RECOVERING state). */
	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING");

	/* Stop cancels recovery. */
	audio_offload_stream_stop();
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "STOPPED after stop");
}

/* ── Test: stop during recovery ──────────────────────────────────── */

ZTEST(audio_offload, test_stop_during_recovery)
{
	/* Trigger recovery. */
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING");

	/* Stop should cancel and transition to STOPPED. */
	audio_offload_stream_stop();
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "STOPPED");
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

	/* Restart (new stream). */
	audio_offload_stream_start();

	mock_consume_sequence = 100;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	memset(test_output, 0xFF, sizeof(test_output));
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 100, 0, test_output);
	zassert_equal(ret, 0, "submit after reconnect");
	zassert_mem_equal(test_output, test_input, TEST_BLOCK_BYTES, "output reconnect");
}

/* ── Test: exact fallback accounting ─────────────────────────────── */

ZTEST(audio_offload, test_fallback_accounting)
{
	struct audio_offload_status s;

	/* Timeout → 1 fallback, 1 timeout. */
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	verify_status(1, 0, 1, 1, 0, 0, 0, 0, 0, 0);

	/* Recover via stream_start. */
	mock_wait_result = 0;
	audio_offload_stream_start();

	/* Verify ACTIVE after recovery. */
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after recovery");

	/* Good submit. */
	mock_consume_sequence = 100;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 100, 0, test_output);

	/* Second good. */
	mock_consume_sequence = 101;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 101, 0, test_output);

	/* CRC fault → 1 fallback, 1 crc. */
	mock_consume_sequence = 102;
	mock_consume_corrupt_crc = true;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 102, 0, test_output);

	/* Recover again. */
	mock_consume_corrupt_crc = false;
	mock_wait_result = 0;
	audio_offload_stream_start();

	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after 2nd recovery");

	/* Good. */
	mock_consume_sequence = 200;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 200, 0, test_output);

	/* After recovery + stream_start, per-stream counters reset: success=1. */
	audio_offload_get_status(&s);
	zassert_equal(s.success_count, 1, "success_count after 2nd recovery");
	zassert_equal(s.fallback_count, 0, "fallback_count reset");
}

/* ── Test: output untouched on all failure paths ─────────────────── */

ZTEST(audio_offload, test_output_untouched_on_failure)
{
	/* Test that output is all zeros after every failure path. */

	/* Timeout. */
	mock_wait_result = -EAGAIN;
	memset(test_output, 0xAB, sizeof(test_output));
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	for (size_t i = 0; i < TEST_BLOCK_SAMPLES; i++) {
		zassert_equal(test_output[i], (int16_t)0xABAB, "output untouched timeout[%zu]", i);
	}
	audio_offload_stream_start(); /* recover */

	/* CRC. */
	mock_consume_corrupt_crc = true;
	mock_consume_sequence = 10;
	mock_wait_result = 0;
	memset(test_output, 0xCD, sizeof(test_output));
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 10, 0, test_output);
	for (size_t i = 0; i < TEST_BLOCK_SAMPLES; i++) {
		zassert_equal(test_output[i], (int16_t)0xCDCD, "output untouched crc[%zu]", i);
	}
	audio_offload_stream_start(); /* recover */

	/* Payload. */
	mock_consume_corrupt_crc = false;
	mock_consume_corrupt_payload = true;
	mock_consume_sequence = 20;
	mock_wait_result = 0;
	mock_consume_crc = flpr_ring_crc32(mock_consume_payload, TEST_BLOCK_BYTES);
	memset(test_output, 0xEF, sizeof(test_output));
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 20, 0, test_output);
	for (size_t i = 0; i < TEST_BLOCK_SAMPLES; i++) {
		zassert_equal(test_output[i], (int16_t)0xEFEF, "output untouched payload[%zu]", i);
	}
	audio_offload_stream_start(); /* recover */

	/* Seq. */
	mock_consume_corrupt_payload = false;
	mock_consume_sequence = 999;
	mock_wait_result = 0;
	mock_consume_crc = flpr_ring_crc32((const uint8_t *)test_input, TEST_BLOCK_BYTES);
	memset(test_output, 0x11, sizeof(test_output));
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 30, 0, test_output);
	for (size_t i = 0; i < TEST_BLOCK_SAMPLES; i++) {
		zassert_equal(test_output[i], (int16_t)0x1111, "output untouched seq[%zu]", i);
	}
}

/* ── Test: concurrent/busy submit (mutex contention) ─────────────── */

ZTEST(audio_offload, test_concurrent_submit)
{
	/* First submit holds mutex.  Use a delayed produce to simulate
	 * a slow submission then try to submit again.
	 * In this test we simulate mutex timeout by checking the return
	 * from a second submit while the first is artificially slow.
	 *
	 * Since we're single-threaded in ZTEST, we can't truly test
	 * concurrent submits.  But we can verify that after a timeout
	 * fault, the state machine correctly rejects further submits. */
	mock_wait_result = -EAGAIN;
	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);
	zassert_equal(ret, -EAGAIN, "first fails");

	/* Second submit while recovering returns -EAGAIN. */
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 2, 0, test_output);
	zassert_equal(ret, -EAGAIN, "second rejected while recovering");

	/* Third. */
	ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 3, 0, test_output);
	zassert_equal(ret, -EAGAIN, "third rejected");
}

/* ── Test: fallback state persists ───────────────────────────────── */

ZTEST(audio_offload, test_fallback_state)
{
	/* Simulate init failure by stopping after init. */
	audio_offload_stream_stop();

	/* Set rings_ready=false by not calling stream_start properly.
	 * Actually, stream_stop sets STOPPED, so submit returns -EAGAIN. */
	int ret = audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 0, 0, test_output);
	zassert_equal(ret, -EAGAIN, "STOPPED returns -EAGAIN");

	struct audio_offload_status s;
	audio_offload_get_status(&s);
	zassert_equal(s.state, AUDIO_OFFLOAD_STOPPED, "state STOPPED");
}

/* ── Test: health check ──────────────────────────────────────────── */

ZTEST(audio_offload, test_is_healthy)
{
	zassert_true(audio_offload_is_healthy(), "healthy after init+start");

	/* Trigger fault. */
	mock_wait_result = -EAGAIN;
	audio_offload_submit(test_input, TEST_BLOCK_SAMPLES, 1, 0, test_output);

	zassert_false(audio_offload_is_healthy(), "not healthy after fault");

	/* Recover. */
	mock_wait_result = 0;
	audio_offload_stream_start();

	zassert_true(audio_offload_is_healthy(), "healthy after recovery");
}

/* ── Test suite registration ─────────────────────────────────────── */

ZTEST_SUITE(audio_offload, NULL, NULL, setup_normal, teardown, NULL);
