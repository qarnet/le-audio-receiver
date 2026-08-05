/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Verify-enabled unit tests for audio_offload_process_asrc() — the
 * CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY shadow path.
 *
 * Compiles PRODUCTION src/audio_offload.c + src/audio_asrc.c +
 * src/flpr_ring.c with CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=1.  The mock
 * transport computes the EXACT real CPU ASRC output + exported
 * post-state (identical to what the shadow computes), so the
 * exact-match path succeeds bit-for-bit; corruption knobs inject
 * controlled damage at the process_asrc validation boundaries.
 *
 * CRC note: payload CRC is validated inside the real
 * flpr_ring_mgr_consume_asrc_result() (transport layer) and never
 * reaches audio_offload_process_asrc(); this suite covers the
 * process_asrc-layer boundaries (seq/frame/status/flags/ppm/state/
 * shadow) and documents CRC as transport-owned.
 */

#include "audio_offload.h"
#include "audio_offload_test_helpers.h"
#include "audio_asrc.h"
#include "flpr_ring.h"
#include "flpr_ring_mgr.h"

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <string.h>

/* ── Mock control variables (mock_ring_mgr_verify.c) ────────────────── */

extern bool mock_init_fails;
extern bool mock_flpr_healthy;
extern bool mock_reset_fails;
extern enum flpr_produce_result mock_produce_result;
extern int mock_notify_result;
extern int mock_wait_result;
extern enum flpr_consume_result mock_consume_result;
extern int mock_produce_asrc_calls;
extern const int16_t *mock_produce_asrc_data;
extern uint32_t mock_produce_asrc_seq;
extern int32_t mock_produce_asrc_ppm;
extern struct audio_asrc_state mock_produce_asrc_pre_state;
extern int mock_consume_asrc_calls;
extern bool mock_seq_wrong;
extern uint32_t mock_corrupt_seq;
extern bool mock_corrupt_sample;
extern bool mock_output_frames_override;
extern uint16_t mock_override_frames;
extern bool mock_corrupt_post_state_phase;
extern bool mock_corrupt_post_state_prev_l;
extern bool mock_corrupt_post_state_prev_valid;
extern bool mock_corrupt_post_state_reserved;
extern bool mock_corrupt_post_state_step_base;
extern bool mock_post_state_step_base_zero;
extern bool mock_corrupt_correction_oob;
extern int32_t mock_processing_status;
extern uint32_t mock_processing_cycles;
extern uint32_t mock_rtt_cycles;

/* ── Test data ────────────────────────────────────────────────────── */

#define TEST_BLOCK_FRAMES  480
#define TEST_BLOCK_SAMPLES (TEST_BLOCK_FRAMES * 2)
#define MAX_OUT_FRAMES     481

static int16_t test_input[TEST_BLOCK_SAMPLES];
static int16_t test_output[MAX_OUT_FRAMES * 2];
static struct audio_asrc_state test_pre_state;

/* ── Helpers ──────────────────────────────────────────────────────── */

static void run_prep_work(void)
{
	k_work_cancel_delayable(&g_recovery_work);
	k_work_cancel_delayable(&g_prep_work);
	prep_work_fn(NULL);
}

static void run_recovery_work(void)
{
	k_work_cancel_delayable(&g_recovery_work);
	k_work_cancel_delayable(&g_prep_work);
	recovery_work_fn(NULL);
}

/* Build a valid cpuapp-exported ASRC pre-state (48000/47619 ratio). */
static void build_valid_pre_state(struct audio_asrc_state *state)
{
	struct audio_asrc ctx;
	audio_asrc_init(&ctx, 48000, 47619);

	int16_t prev_l = 0, prev_r = 0;
	bool prev_valid = false;
	size_t consumed, produced;
	int16_t nl, nr;
	int16_t out_buf[MAX_OUT_FRAMES * 2];

	audio_asrc_process(&ctx, test_input, TEST_BLOCK_FRAMES, out_buf, MAX_OUT_FRAMES, 0, prev_l,
			   prev_r, prev_valid, &consumed, &produced, &nl, &nr);

	audio_asrc_state_export(&ctx, nl, nr, true, state);
}

static void fill_output(int16_t val)
{
	memset(test_output, (int)(val & 0xFF), sizeof(test_output));
}

static void assert_output_untouched(int16_t expected_val)
{
	for (size_t i = 0; i < MAX_OUT_FRAMES * 2; i++) {
		zassert_equal(test_output[i], expected_val,
			      "output[%zu] untouched expected 0x%04X got 0x%04X", i,
			      (unsigned)expected_val, (unsigned)test_output[i]);
	}
}

/* ── Setup/teardown ───────────────────────────────────────────────── */

static void setup(void *fixture)
{
	(void)fixture;

	mock_init_fails = false;
	mock_flpr_healthy = true;
	mock_reset_fails = false;
	mock_produce_result = FLPR_PRODUCE_OK;
	mock_notify_result = 0;
	mock_wait_result = 0;
	mock_consume_result = FLPR_CONSUME_OK;
	mock_produce_asrc_calls = 0;
	mock_consume_asrc_calls = 0;
	mock_seq_wrong = false;
	mock_corrupt_seq = 0;
	mock_corrupt_sample = false;
	mock_output_frames_override = false;
	mock_override_frames = 0;
	mock_corrupt_post_state_phase = false;
	mock_corrupt_post_state_prev_l = false;
	mock_corrupt_post_state_prev_valid = false;
	mock_corrupt_post_state_reserved = false;
	mock_corrupt_post_state_step_base = false;
	mock_post_state_step_base_zero = false;
	mock_corrupt_correction_oob = false;
	mock_processing_status = 0;
	mock_processing_cycles = 12000;
	mock_rtt_cycles = 500;

	for (size_t i = 0; i < TEST_BLOCK_SAMPLES; i++) {
		test_input[i] = (int16_t)(i & 0xFFFF);
	}

	build_valid_pre_state(&test_pre_state);

	memset(test_output, 0, sizeof(test_output));

	audio_offload_init();
	audio_offload_stream_start();
	run_prep_work();
}

static void teardown(void *fixture)
{
	(void)fixture;
	audio_offload_stream_stop();
	memset(test_output, 0, sizeof(test_output));
}

/* ── Tests ────────────────────────────────────────────────────────── */

/* Exact match: shadow == real CPU ASRC result, bit-for-bit. */
ZTEST(offload_asrc_verify, test_verify_exact_match)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	memset(&result, 0, sizeof(result));
	audio_offload_get_asrc_stats(&pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, 42, 150,
					     &test_pre_state, test_output, MAX_OUT_FRAMES, &result);

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(ret, 0, "exact match succeeds");
	zassert_equal(mock_produce_asrc_ppm, 150, "ppm captured");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 0, "no verify fault");
	zassert_equal(post_a.success_count - pre_a.success_count, 1, "success +1");

	/* Output must equal the real CPU ASRC computation sample-for-sample. */
	struct audio_asrc exp_ctx;
	int16_t el = 0, er = 0;
	bool ev = false;
	size_t ec, ep;
	int16_t enl, enr;
	int16_t exp_out[MAX_OUT_FRAMES * 2];

	zassert_equal(audio_asrc_state_import(&exp_ctx, &test_pre_state, &el, &er, &ev), 0,
		      "pre-state import");
	zassert_equal(audio_asrc_process(&exp_ctx, test_input, TEST_BLOCK_FRAMES, exp_out,
					 MAX_OUT_FRAMES, 150, el, er, ev, &ec, &ep, &enl, &enr),
		      0, "reference process");
	zassert_equal(result.output_frames, ep, "output_frames matches reference");
	zassert_mem_equal(test_output, exp_out, (size_t)ep * 4U, "output matches reference");
}

/* Sample mismatch: shadow sample compare fails → verify fault. */
ZTEST(offload_asrc_verify, test_verify_sample_mismatch)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_corrupt_sample = true;
	fill_output(0x11);
	memset(&result, 0xFF, sizeof(result));
	audio_offload_get_asrc_stats(&pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, 1, 0, &test_pre_state,
					     test_output, MAX_OUT_FRAMES, &result);

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(ret, -EAGAIN, "sample mismatch fails");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 1,
		      "verify_fault_count +1");
	zassert_equal(post_a.fallback_count - pre_a.fallback_count, 1, "asrc fallback +1");
	assert_output_untouched((int16_t)0x1111);
	zassert_equal(result.output_frames, 0xFFFF, "result untouched");

	/* Shared finalizer: RECOVERING + one recovery schedule, consumed once. */
	{
		struct audio_offload_status s;
		audio_offload_get_status(&s);
		zassert_equal(s.state, AUDIO_OFFLOAD_RECOVERING, "RECOVERING");
		zassert_false(s.healthy, "healthy false");
		zassert_equal(s.last_error, -EFAULT, "last_error -EFAULT");
		zassert_equal(s.last_error_seq, 1, "last_error_seq");
		zassert_true(audio_offload_test_is_recovery_scheduled(), "one recovery schedule");
	}

	run_recovery_work();
	{
		struct audio_offload_status s;
		audio_offload_get_status(&s);
		zassert_equal(s.state, AUDIO_OFFLOAD_ACTIVE, "ACTIVE after one recovery");
		zassert_false(audio_offload_test_is_recovery_scheduled(), "schedule consumed");
	}
}

/* Frame-count mismatch: shadow produced != FLPR output_frames → verify fault. */
ZTEST(offload_asrc_verify, test_verify_frame_count_mismatch)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	/* produced is ~476 at this ratio; override within 1..481 so metadata
	 * passes and the shadow frame-count compare catches it. */
	mock_output_frames_override = true;
	mock_override_frames = 470;
	fill_output(0x22);
	memset(&result, 0xFF, sizeof(result));
	audio_offload_get_asrc_stats(&pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, 2, 0, &test_pre_state,
					     test_output, MAX_OUT_FRAMES, &result);

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(ret, -EAGAIN, "frame-count mismatch fails");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 1,
		      "verify_fault_count +1");
	assert_output_untouched((int16_t)0x2222);
	zassert_equal(result.output_frames, 0xFFFF, "result untouched");

	run_recovery_work();
}

/* Post-state phase mismatch: shadow post-state compare fails → verify fault. */
ZTEST(offload_asrc_verify, test_verify_post_state_phase_mismatch)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_corrupt_post_state_phase = true;
	memset(&result, 0xFF, sizeof(result));
	audio_offload_get_asrc_stats(&pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, 3, 0, &test_pre_state,
					     test_output, MAX_OUT_FRAMES, &result);

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(ret, -EAGAIN, "post-state phase mismatch fails");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 1,
		      "verify_fault_count +1");
	zassert_equal(result.output_frames, 0xFFFF, "result untouched");

	run_recovery_work();
}

/* Post-state prev_l mismatch → verify fault. */
ZTEST(offload_asrc_verify, test_verify_post_state_prev_l_mismatch)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_corrupt_post_state_prev_l = true;
	memset(&result, 0xFF, sizeof(result));
	audio_offload_get_asrc_stats(&pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, 4, 0, &test_pre_state,
					     test_output, MAX_OUT_FRAMES, &result);

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(ret, -EAGAIN, "post-state prev_l mismatch fails");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 1,
		      "verify_fault_count +1");

	run_recovery_work();
}

/* Post-state prev_valid mismatch → verify fault. */
ZTEST(offload_asrc_verify, test_verify_post_state_prev_valid_mismatch)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_corrupt_post_state_prev_valid = true;
	memset(&result, 0xFF, sizeof(result));
	audio_offload_get_asrc_stats(&pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, 5, 0, &test_pre_state,
					     test_output, MAX_OUT_FRAMES, &result);

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(ret, -EAGAIN, "post-state prev_valid mismatch fails");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 1,
		      "verify_fault_count +1");

	run_recovery_work();
}

/* Sequence corruption: caught at the METADATA boundary (seq fault),
 * before the shadow compare. */
ZTEST(offload_asrc_verify, test_verify_sequence_corruption)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_seq_wrong = true;
	mock_corrupt_seq = 9999;
	memset(&result, 0xFF, sizeof(result));
	audio_offload_get_asrc_stats(&pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, 6, 0, &test_pre_state,
					     test_output, MAX_OUT_FRAMES, &result);

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(ret, -EAGAIN, "sequence corruption fails");
	zassert_equal(post_a.seq_fault_count - pre_a.seq_fault_count, 1,
		      "seq_fault_count +1 (metadata boundary)");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 0, "no verify fault");

	run_recovery_work();
}

/* Out-of-range ppm: echo passes (5000 == 5000), but the shadow ASRC
 * process call fails (-EINVAL) → verify fault. */
ZTEST(offload_asrc_verify, test_verify_ppm_out_of_range)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_corrupt_correction_oob = true;
	fill_output(0x33);
	memset(&result, 0xFF, sizeof(result));
	audio_offload_get_asrc_stats(&pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, 7, 5000,
					     &test_pre_state, test_output, MAX_OUT_FRAMES, &result);

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(ret, -EAGAIN, "OOB ppm shadow failure fails");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 1,
		      "verify_fault_count +1 (shadow process failure)");
	assert_output_untouched((int16_t)0x3333);
	zassert_equal(result.output_frames, 0xFFFF, "result untouched");

	run_recovery_work();
}

/* Reachable cpuapp/post-state import failure BEFORE shadow: step_base=0
 * is rejected by metadata post-state import → state fault. */
ZTEST(offload_asrc_verify, test_verify_post_state_import_fail)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_post_state_step_base_zero = true;
	memset(&result, 0xFF, sizeof(result));
	audio_offload_get_asrc_stats(&pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, 8, 0, &test_pre_state,
					     test_output, MAX_OUT_FRAMES, &result);

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(ret, -EAGAIN, "post-state import fail");
	zassert_equal(post_a.state_fault_count - pre_a.state_fault_count, 1,
		      "state_fault_count +1 (metadata import)");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 0, "no verify fault");
	zassert_equal(result.output_frames, 0xFFFF, "result untouched");

	run_recovery_work();
}

/* Reserved byte corruption → metadata state fault before shadow. */
ZTEST(offload_asrc_verify, test_verify_post_state_reserved)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_corrupt_post_state_reserved = true;
	memset(&result, 0xFF, sizeof(result));
	audio_offload_get_asrc_stats(&pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, 9, 0, &test_pre_state,
					     test_output, MAX_OUT_FRAMES, &result);

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(ret, -EAGAIN, "reserved corruption fails");
	zassert_equal(post_a.state_fault_count - pre_a.state_fault_count, 1,
		      "state_fault_count +1");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 0, "no verify fault");

	run_recovery_work();
}

/* step_base mismatch → metadata state fault before shadow. */
ZTEST(offload_asrc_verify, test_verify_post_state_step_base_mismatch)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result result;

	mock_corrupt_post_state_step_base = true;
	memset(&result, 0xFF, sizeof(result));
	audio_offload_get_asrc_stats(&pre_a);

	int ret = audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, 10, 0, &test_pre_state,
					     test_output, MAX_OUT_FRAMES, &result);

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(ret, -EAGAIN, "step_base mismatch fails");
	zassert_equal(post_a.state_fault_count - pre_a.state_fault_count, 1,
		      "state_fault_count +1");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 0, "no verify fault");

	run_recovery_work();
}

/* Stats across verify-enabled successes: RTT/cycles tracked, zero faults. */
ZTEST(offload_asrc_verify, test_verify_stats_across_successes)
{
	struct audio_offload_asrc_stats pre_a, post_a;
	struct audio_offload_asrc_result r;

	audio_offload_get_asrc_stats(&pre_a);

	for (uint32_t seq = 20; seq < 30; seq++) {
		mock_rtt_cycles = 400 + seq;
		memset(&r, 0, sizeof(r));
		zassert_equal(audio_offload_process_asrc(test_input, TEST_BLOCK_FRAMES, seq, 0,
							 &test_pre_state, test_output,
							 MAX_OUT_FRAMES, &r),
			      0, "success seq %u", seq);
	}

	audio_offload_get_asrc_stats(&post_a);

	zassert_equal(post_a.success_count - pre_a.success_count, 10, "success +10");
	zassert_equal(post_a.rtt_count - pre_a.rtt_count, 10, "rtt_count +10");
	zassert_equal(post_a.verify_fault_count - pre_a.verify_fault_count, 0,
		      "zero verify faults");
	zassert_equal(post_a.state_fault_count - pre_a.state_fault_count, 0, "zero state faults");
	zassert_equal(post_a.seq_fault_count - pre_a.seq_fault_count, 0, "zero seq faults");
}

/* ── Test suite registration ──────────────────────────────────────── */

ZTEST_SUITE(offload_asrc_verify, NULL, NULL, setup, teardown, NULL);
