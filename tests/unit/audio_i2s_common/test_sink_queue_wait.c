/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Native proof of target-specific I2S TX queue-full behavior.  The ASRC
 * variant models nRF54L15's finite configured wait; the identity variant
 * preserves nRF5340's nonblocking queue-full error boundary.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include "audio_i2s_test_helpers.h"

#define QUEUE_WAIT_TEST_TIMEOUT_MS 1000

#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
static K_THREAD_STACK_DEFINE(queue_wait_push_stack, 4096);
static struct k_thread queue_wait_push_thread;
static K_SEM_DEFINE(queue_wait_push_done, 0, 1);

struct queue_wait_push_ctx {
	int ret;
};

static void queue_wait_push_worker(void *arg, void *unused1, void *unused2)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	struct queue_wait_push_ctx *ctx = arg;
	ctx->ret = audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2);
	k_sem_give(&queue_wait_push_done);
}
#endif

static K_SEM_DEFINE(slab_alloc_entered, 0, 1);

static void test_fill_post_start_slab(void)
{
	test_start_stream();

	/* The first started push consumes the one block reserved by startup. */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0,
		      "first post-start push");
	zassert_equal(test_slab_free(), 0, "all sixteen slab blocks are driver-owned");
	zassert_equal(fake_i2s_queued_count(), TEST_SLAB_BLOCKS, "sixteen driver-owned blocks");
	zassert_equal(fake_i2s_write_calls(), STARTUP_FIRST_STEADY_WRITE_IDX + 1,
		      "one post-start write");
}

#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
static K_THREAD_STACK_DEFINE(slab_push_stack, 4096);
static struct k_thread slab_push_thread;
static K_SEM_DEFINE(slab_push_done, 0, 1);

struct slab_push_ctx {
	int ret;
};

static void slab_push_worker(void *arg, void *unused1, void *unused2)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	struct slab_push_ctx *ctx = arg;
	ctx->ret = audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2);
	k_sem_give(&slab_push_done);
}
#endif

ZTEST(audio_i2s, test_full_tx_queue_uses_variant_timeout)
{
	test_start_stream();
	zassert_equal(fake_i2s_force_queue_full(), 0, "force TX queue full");
	zassert_equal(fake_i2s_queued_count(), FAKE_I2S_QUEUE_CAPACITY, "TX queue full");

#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
	k_sem_reset(&queue_wait_push_done);
	struct queue_wait_push_ctx ctx = {.ret = -1};

	k_thread_create(&queue_wait_push_thread, queue_wait_push_stack,
			K_THREAD_STACK_SIZEOF(queue_wait_push_stack), queue_wait_push_worker, &ctx,
			NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

	zassert_equal(fake_i2s_wait_until_queue_full_write(K_MSEC(QUEUE_WAIT_TEST_TIMEOUT_MS)), 0,
		      "second push blocked on full TX queue");

	/* Simulate one DMA completion before the configured 20 ms deadline. */
	fake_i2s_release_one_for_queue_wait();
	zassert_equal(k_sem_take(&queue_wait_push_done, K_MSEC(QUEUE_WAIT_TEST_TIMEOUT_MS)), 0,
		      "second push completed after queue space");
	zassert_equal(ctx.ret, 0, "finite wait push succeeds");

	zassert_equal(fake_i2s_write_calls(), STARTUP_FIRST_STEADY_WRITE_IDX + 1,
		      "one steady write transferred");
	zassert_equal(fake_i2s_queued_count(), FAKE_I2S_QUEUE_CAPACITY,
		      "queue remains full after transfer");
	void *data_ptr = fake_i2s_write_rec(STARTUP_FIRST_STEADY_WRITE_IDX)->ptr;
	zassert_equal(fake_i2s_queued_ptr(fake_i2s_queued_count() - 1), data_ptr,
		      "new block is queued");
	for (int i = 0; i < fake_i2s_queued_count() - 1; i++) {
		zassert_not_equal(fake_i2s_queued_ptr(i), data_ptr,
				  "new block distinct from every queued block");
	}
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - FAKE_I2S_QUEUE_CAPACITY,
		      "slab ownership matches queue");
	zassert_equal(mock_perf_i2s_write_failure_calls, 0, "no I2S write failure telemetry");
	zassert_equal(mock_stats_underrun_calls, 0, "no underrun");
	zassert_equal(mock_stats_stream_reset_calls, 0, "no stream reset");
	zassert_equal(mock_perf_i2s_dma_restart_calls, 0, "no DMA restart");
	zassert_true(audio_i2s_test_is_started(), "sink stays started");
	zassert_equal(audio_i2s_test_active_pushes(), 0, "push admission drained");
	test_assert_no_duplicate_writes();
#else
	/* nRF5340 keeps timeout 0: nrfx returns -ENOMSG immediately and the
	 * caller retains ownership of the failed write block. */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -ENOMSG,
		      "full TX queue returns ENOMSG without waiting");
	zassert_equal(fake_i2s_write_calls(), STARTUP_FIRST_STEADY_WRITE_IDX + 1,
		      "full-queue write recorded");
	zassert_equal(fake_i2s_queued_count(), FAKE_I2S_QUEUE_CAPACITY,
		      "failed write does not change queue");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS - FAKE_I2S_QUEUE_CAPACITY,
		      "failed write block reclaimed");
	zassert_equal(mock_perf_i2s_write_failure_calls, 1, "I2S write failure telemetry");
	zassert_equal(mock_perf_i2s_last_write_errno, -ENOMSG, "ENOMSG telemetry");
	zassert_equal(mock_stats_underrun_calls, 0, "no underrun");
	zassert_equal(mock_stats_stream_reset_calls, 0, "no stream reset");
	zassert_true(audio_i2s_test_is_started(), "sink stays started");
	test_assert_no_duplicate_writes();
#endif
}

ZTEST(audio_i2s, test_post_start_slab_backpressure_respects_variant_timeout)
{
	test_fill_post_start_slab();

#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
	k_sem_reset(&slab_alloc_entered);
	k_sem_reset(&slab_push_done);
	struct slab_push_ctx ctx = {.ret = -1};

	audio_i2s_test_arm_main_slab_alloc(&slab_alloc_entered);
	k_thread_create(&slab_push_thread, slab_push_stack, K_THREAD_STACK_SIZEOF(slab_push_stack),
			slab_push_worker, &ctx, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

	zassert_equal(k_sem_take(&slab_alloc_entered, K_MSEC(QUEUE_WAIT_TEST_TIMEOUT_MS)), 0,
		      "post-start allocation entered");
	zassert_equal(test_slab_free(), 0, "slab remains exhausted while waiting");
	zassert_true(audio_i2s_test_last_main_slab_alloc_waited(),
		     "ASRC selects positive slab wait");

	/* A DMA completion releases one startup-owned block and wakes the
	 * bounded primary allocation. */
	fake_i2s_release(fake_i2s_queued_ptr(0));
	zassert_equal(k_sem_take(&slab_push_done, K_MSEC(QUEUE_WAIT_TEST_TIMEOUT_MS)), 0,
		      "post-start push completes after one DMA release");
	zassert_equal(k_thread_join(&slab_push_thread, K_FOREVER), 0,
		      "post-start push worker joined");
	zassert_equal(ctx.ret, 0, "finite slab wait push succeeds");

	zassert_equal(test_slab_free(), 0, "sixteen blocks driver-owned again");
	zassert_equal(fake_i2s_queued_count(), TEST_SLAB_BLOCKS,
		      "sixteen driver-owned blocks again");
	zassert_equal(fake_i2s_write_calls(), STARTUP_FIRST_STEADY_WRITE_IDX + 2,
		      "second push adds one write");
	zassert_equal(mock_stats_underrun_calls, 0, "no I2S underrun");
	zassert_equal(mock_perf_push_failure_calls, 0, "no push failure telemetry");
	zassert_equal(mock_perf_i2s_write_failure_calls, 0, "no I2S write failure telemetry");
	zassert_true(audio_i2s_test_is_started(), "started retained");
	zassert_true(audio_i2s_test_is_configured(), "configured retained");
	test_assert_no_duplicate_writes();
#else
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -ENOMEM,
		      "identity full slab returns immediately");
	zassert_false(audio_i2s_test_last_main_slab_alloc_waited(),
		      "identity selects no slab wait");
	zassert_equal(test_slab_free(), 0, "slab remains exhausted");
	zassert_equal(fake_i2s_queued_count(), TEST_SLAB_BLOCKS, "driver-owned blocks unchanged");
	zassert_equal(fake_i2s_write_calls(), STARTUP_FIRST_STEADY_WRITE_IDX + 1,
		      "no extra I2S write");
	zassert_equal(mock_stats_underrun_calls, 1, "one I2S underrun");
	zassert_equal(mock_perf_push_failure_calls, 0, "no push failure telemetry");
	zassert_equal(mock_perf_i2s_write_failure_calls, 0, "no I2S write failure telemetry");
	zassert_true(audio_i2s_test_is_started(), "started retained");
	zassert_true(audio_i2s_test_is_configured(), "configured retained");
	test_assert_no_duplicate_writes();
#endif
}

ZTEST(audio_i2s, test_post_start_slab_backpressure_timeout_is_bounded)
{
	test_fill_post_start_slab();

#if defined(AUDIO_I2S_TEST_MARKER_ASRC)
	k_sem_reset(&slab_alloc_entered);
	k_sem_reset(&slab_push_done);
	struct slab_push_ctx ctx = {.ret = -1};

	audio_i2s_test_arm_main_slab_alloc(&slab_alloc_entered);
	k_thread_create(&slab_push_thread, slab_push_stack, K_THREAD_STACK_SIZEOF(slab_push_stack),
			slab_push_worker, &ctx, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

	zassert_equal(k_sem_take(&slab_alloc_entered, K_MSEC(QUEUE_WAIT_TEST_TIMEOUT_MS)), 0,
		      "post-start allocation entered");
	zassert_equal(test_slab_free(), 0, "slab remains exhausted while waiting");
	zassert_true(audio_i2s_test_last_main_slab_alloc_waited(),
		     "ASRC selects positive slab wait");
	zassert_equal(k_sem_take(&slab_push_done, K_MSEC(QUEUE_WAIT_TEST_TIMEOUT_MS)), 0,
		      "bounded slab wait completes");
	zassert_equal(k_thread_join(&slab_push_thread, K_FOREVER), 0,
		      "bounded slab wait worker joined");
	zassert_equal(ctx.ret, -EAGAIN, "finite slab wait returns EAGAIN");

	zassert_equal(test_slab_free(), 0, "slab remains exhausted");
	zassert_equal(fake_i2s_queued_count(), TEST_SLAB_BLOCKS, "driver-owned blocks unchanged");
	zassert_equal(fake_i2s_write_calls(), STARTUP_FIRST_STEADY_WRITE_IDX + 1,
		      "no extra I2S write");
	zassert_equal(mock_stats_underrun_calls, 1, "one I2S underrun");
	zassert_equal(mock_perf_push_failure_calls, 0, "no push failure telemetry");
	zassert_equal(mock_perf_i2s_write_failure_calls, 0, "no I2S write failure telemetry");
	zassert_true(audio_i2s_test_is_started(), "started retained");
	zassert_true(audio_i2s_test_is_configured(), "configured retained");
	test_assert_no_duplicate_writes();
#else
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -ENOMEM,
		      "identity full slab returns immediately");
	zassert_false(audio_i2s_test_last_main_slab_alloc_waited(),
		      "identity selects no slab wait");
	zassert_equal(test_slab_free(), 0, "slab remains exhausted");
	zassert_equal(fake_i2s_queued_count(), TEST_SLAB_BLOCKS, "driver-owned blocks unchanged");
	zassert_equal(fake_i2s_write_calls(), STARTUP_FIRST_STEADY_WRITE_IDX + 1,
		      "no extra I2S write");
	zassert_equal(mock_stats_underrun_calls, 1, "one I2S underrun");
	zassert_equal(mock_perf_push_failure_calls, 0, "no push failure telemetry");
	zassert_equal(mock_perf_i2s_write_failure_calls, 0, "no I2S write failure telemetry");
	zassert_true(audio_i2s_test_is_started(), "started retained");
	zassert_true(audio_i2s_test_is_configured(), "configured retained");
	test_assert_no_duplicate_writes();
#endif
}
