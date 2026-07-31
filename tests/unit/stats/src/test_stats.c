/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * T2D — production statistics suite.
 *
 * Compiles and executes the REAL src/audio_stats.c.  Counter semantics:
 * frame_decoded -> total only; frame_plc -> plc + total; decode_error,
 * i2s_underrun, stream_reset -> own counter only.  See
 * docs/testing/t2-audio-pipeline-tests.md.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include "audio_stats.h"

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	audio_stats_reset();
}

ZTEST_SUITE(stats, NULL, NULL, reset_before_each, NULL, NULL);

/* ── single-threaded semantics ───────────────────────────────────── */

ZTEST(stats, test_initial_snapshot_zero)
{
	struct audio_stats st = audio_stats_get();

	zassert_equal(st.total_frames, 0, "total");
	zassert_equal(st.plc_frames, 0, "plc");
	zassert_equal(st.decode_errors, 0, "errors");
	zassert_equal(st.i2s_underruns, 0, "underruns");
	zassert_equal(st.stream_resets, 0, "resets");
}

ZTEST(stats, test_frame_decoded_increments_total_only)
{
	audio_stats_frame_decoded();
	audio_stats_frame_decoded();

	struct audio_stats st = audio_stats_get();

	zassert_equal(st.total_frames, 2, "total 2");
	zassert_equal(st.plc_frames, 0, "no plc");
	zassert_equal(st.decode_errors, 0, "no errors");
	zassert_equal(st.i2s_underruns, 0, "no underruns");
	zassert_equal(st.stream_resets, 0, "no resets");
}

ZTEST(stats, test_plc_increments_plc_and_total_once)
{
	audio_stats_frame_plc();

	struct audio_stats st = audio_stats_get();

	zassert_equal(st.plc_frames, 1, "plc 1");
	zassert_equal(st.total_frames, 1, "total 1");
}

ZTEST(stats, test_decode_error_does_not_increment_total)
{
	audio_stats_decode_error();
	audio_stats_decode_error();

	struct audio_stats st = audio_stats_get();

	zassert_equal(st.decode_errors, 2, "errors 2");
	zassert_equal(st.total_frames, 0, "total untouched");
	zassert_equal(st.plc_frames, 0, "plc untouched");
}

ZTEST(stats, test_underrun_and_reset_increment_own_counters)
{
	audio_stats_i2s_underrun();
	audio_stats_i2s_underrun();
	audio_stats_i2s_underrun();
	audio_stats_stream_reset();
	audio_stats_stream_reset();

	struct audio_stats st = audio_stats_get();

	zassert_equal(st.i2s_underruns, 3, "underruns 3");
	zassert_equal(st.stream_resets, 2, "resets 2");
	zassert_equal(st.total_frames, 0, "total untouched");
}

ZTEST(stats, test_mixed_sequence_exact_snapshot)
{
	audio_stats_frame_decoded();
	audio_stats_frame_decoded();
	audio_stats_frame_decoded(); /* 3 decoded */
	audio_stats_frame_plc();     /* plc 1, total 4 */
	audio_stats_decode_error();  /* error 1 */
	audio_stats_i2s_underrun();  /* underrun 1 */
	audio_stats_stream_reset();  /* reset 1 */
	audio_stats_frame_decoded(); /* 4 decoded, total 5 */

	struct audio_stats st = audio_stats_get();

	zassert_equal(st.total_frames, 5, "total 5");
	zassert_equal(st.plc_frames, 1, "plc 1");
	zassert_equal(st.decode_errors, 1, "errors 1");
	zassert_equal(st.i2s_underruns, 1, "underruns 1");
	zassert_equal(st.stream_resets, 1, "resets 1");
}

ZTEST(stats, test_reset_after_nonzero_clears_every_counter)
{
	audio_stats_frame_decoded();
	audio_stats_frame_plc();
	audio_stats_decode_error();
	audio_stats_i2s_underrun();
	audio_stats_stream_reset();

	audio_stats_reset();

	struct audio_stats st = audio_stats_get();

	zassert_equal(st.total_frames, 0, "total");
	zassert_equal(st.plc_frames, 0, "plc");
	zassert_equal(st.decode_errors, 0, "errors");
	zassert_equal(st.i2s_underruns, 0, "underruns");
	zassert_equal(st.stream_resets, 0, "resets");
}

ZTEST(stats, test_snapshot_by_value_no_mutation)
{
	audio_stats_frame_decoded();

	struct audio_stats st = audio_stats_get();
	struct audio_stats again = audio_stats_get();

	zassert_equal(st.total_frames, 1, "first");
	zassert_equal(again.total_frames, 1, "second identical");

	/* Modifying a returned snapshot must not affect future state. */
	st.total_frames = 999;
	st.plc_frames = 999;

	struct audio_stats third = audio_stats_get();

	zassert_equal(third.total_frames, 1, "state not mutated");
	zassert_equal(third.plc_frames, 0, "state not mutated");
}

ZTEST(stats, test_repeated_reset_get_deterministic)
{
	for (int i = 0; i < 5; i++) {
		audio_stats_reset();
		struct audio_stats st = audio_stats_get();

		zassert_equal(st.total_frames, 0, "deterministic total %d", i);
		zassert_equal(st.plc_frames, 0, "deterministic plc %d", i);
		zassert_equal(st.decode_errors, 0, "deterministic errors %d", i);
		zassert_equal(st.i2s_underruns, 0, "deterministic underruns %d", i);
		zassert_equal(st.stream_resets, 0, "deterministic resets %d", i);
	}
}

/* ── concurrency ─────────────────────────────────────────────────── */

#define CONCURRENT_COUNT 10000

K_THREAD_STACK_DEFINE(t_decoded_stack, 1024);
K_THREAD_STACK_DEFINE(t_plc_stack, 1024);
K_THREAD_STACK_DEFINE(t_error_stack, 1024);
K_THREAD_STACK_DEFINE(t_io_stack, 1024);

static struct k_thread t_decoded;
static struct k_thread t_plc;
static struct k_thread t_error;
static struct k_thread t_io;

static void inc_decoded_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (int i = 0; i < CONCURRENT_COUNT; i++) {
		audio_stats_frame_decoded();
	}
}

static void inc_plc_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (int i = 0; i < CONCURRENT_COUNT; i++) {
		audio_stats_frame_plc();
	}
}

static void inc_error_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (int i = 0; i < CONCURRENT_COUNT; i++) {
		audio_stats_decode_error();
	}
}

static void inc_io_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (int i = 0; i < CONCURRENT_COUNT; i++) {
		audio_stats_i2s_underrun();
		audio_stats_stream_reset();
	}
}

ZTEST(stats, test_concurrent_threads_exact_atomic_counts)
{
	/* Four threads incrementing the production atomics concurrently.
	 * Final counts must be exact, including total = decoded + plc.
	 */
	k_thread_create(&t_decoded, t_decoded_stack, K_THREAD_STACK_SIZEOF(t_decoded_stack),
			inc_decoded_fn, NULL, NULL, NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);
	k_thread_create(&t_plc, t_plc_stack, K_THREAD_STACK_SIZEOF(t_plc_stack), inc_plc_fn, NULL,
			NULL, NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);
	k_thread_create(&t_error, t_error_stack, K_THREAD_STACK_SIZEOF(t_error_stack), inc_error_fn,
			NULL, NULL, NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);
	k_thread_create(&t_io, t_io_stack, K_THREAD_STACK_SIZEOF(t_io_stack), inc_io_fn, NULL, NULL,
			NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);

	k_thread_join(&t_decoded, K_FOREVER);
	k_thread_join(&t_plc, K_FOREVER);
	k_thread_join(&t_error, K_FOREVER);
	k_thread_join(&t_io, K_FOREVER);

	struct audio_stats st = audio_stats_get();

	zassert_equal(st.total_frames, 2 * CONCURRENT_COUNT, "total = decoded + plc");
	zassert_equal(st.plc_frames, CONCURRENT_COUNT, "plc exact");
	zassert_equal(st.decode_errors, CONCURRENT_COUNT, "errors exact");
	zassert_equal(st.i2s_underruns, CONCURRENT_COUNT, "underruns exact");
	zassert_equal(st.stream_resets, CONCURRENT_COUNT, "resets exact");
}
