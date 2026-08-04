/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * R1 concurrency tests for the audio sink (shared by the ASRC and
 * identity suites).  Real src/audio_i2s.c runs against the fake I2S
 * driver; the fake write gate pauses one selected successful write with
 * entered/release semaphores while ownership semantics stay unchanged.
 *
 * Covers: stop drains an admitted push before finalizing; two
 * overlapping stops finalize once; closed rejection and reconnect;
 * failure exits release admission; open waits for the full stop cohort;
 * close is nonblocking.  All synchronization uses entered/release
 * semaphores and bounded waits/joins — a broken drain fails the suite
 * instead of hanging it.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2s.h>

#include "audio_i2s_test_helpers.h"

#define CONCUR_TIMEOUT_MS 5000

/* ── worker infrastructure ─────────────────────────────────────────
 * Each role owns its own k_thread/stack: workers run CONCURRENTLY
 * (a stop is spawned while the pushed worker is still paused in the
 * write gate), so a single reusable thread object would be re-created
 * while still alive. */

static K_THREAD_STACK_DEFINE(push_stack, 4096);
static struct k_thread push_thread;
static K_THREAD_STACK_DEFINE(stop_stack_a, 4096);
static struct k_thread stop_thread_a;
static K_THREAD_STACK_DEFINE(stop_stack_b, 4096);
static struct k_thread stop_thread_b;
static K_THREAD_STACK_DEFINE(open_stack, 4096);
static struct k_thread open_thread;

static K_SEM_DEFINE(conc_done_sem, 0, 8);
static bool open_started_flag;
static bool open_returned_flag;

/* Write-gate semaphores (file scope: K_SEM_DEFINE requires iterable
 * section membership that block scope cannot provide).  Tests reset
 * them before arming the gate. */
static K_SEM_DEFINE(w_entered, 0, 1);
static K_SEM_DEFINE(w_release, 0, 1);

static void spawn_push(void (*fn)(void *, void *, void *), void *arg)
{
	k_thread_create(&push_thread, push_stack, K_THREAD_STACK_SIZEOF(push_stack), fn, arg, NULL,
			NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
}

static void spawn_stop_a(void (*fn)(void *, void *, void *), void *arg)
{
	k_thread_create(&stop_thread_a, stop_stack_a, K_THREAD_STACK_SIZEOF(stop_stack_a), fn, arg,
			NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
}

static void spawn_stop_b(void (*fn)(void *, void *, void *), void *arg)
{
	k_thread_create(&stop_thread_b, stop_stack_b, K_THREAD_STACK_SIZEOF(stop_stack_b), fn, arg,
			NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
}

static void spawn_open(void (*fn)(void *, void *, void *), void *arg)
{
	open_started_flag = false;
	open_returned_flag = false;
	k_thread_create(&open_thread, open_stack, K_THREAD_STACK_SIZEOF(open_stack), fn, arg, NULL,
			NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
}

static bool conc_wait_done(uint32_t timeout_ms)
{
	return k_sem_take(&conc_done_sem, K_MSEC(timeout_ms)) == 0;
}

static bool conc_wait_until(bool (*cond)(void *), void *arg, uint32_t timeout_ms)
{
	uint32_t deadline = k_uptime_get_32() + timeout_ms;

	while (!cond(arg)) {
		if (k_uptime_get_32() >= deadline) {
			return false;
		}
		k_sleep(K_MSEC(1));
	}
	return true;
}

struct push_ctx {
	const int16_t *in;
	size_t samples;
	int ret;
};

static void push_worker_fn(void *ctx_p, void *u1, void *u2)
{
	(void)u1;
	(void)u2;
	struct push_ctx *c = ctx_p;

	c->ret = audio_sink_push(c->in, c->samples);
	k_sem_give(&conc_done_sem);
}

static void stop_worker_fn(void *ctx_p, void *u1, void *u2)
{
	(void)ctx_p;
	(void)u1;
	(void)u2;
	audio_sink_stop();
	k_sem_give(&conc_done_sem);
}

struct open_ctx {
	int ret;
};

static void open_worker_fn(void *ctx_p, void *u1, void *u2)
{
	(void)u1;
	(void)u2;
	struct open_ctx *c = ctx_p;

	open_started_flag = true;
	c->ret = audio_sink_stream_open();
	open_returned_flag = true;
	k_sem_give(&conc_done_sem);
}

/* ── conditions ──────────────────────────────────────────────────── */

static bool stop_in_drain_cond(void *arg)
{
	(void)arg;
	return audio_i2s_test_stop_callers() == 1 && audio_i2s_test_stop_finalizing();
}

static bool two_stop_cohort_cond(void *arg)
{
	(void)arg;
	return audio_i2s_test_stop_callers() == 2 && audio_i2s_test_stop_finalizing();
}

static bool open_started_cond(void *arg)
{
	(void)arg;
	return open_started_flag;
}

/* ── 1. stop drains admitted push ────────────────────────────────── */

ZTEST(audio_i2s, test_stop_drains_admitted_push)
{
	test_start_stream();

	k_sem_reset(&w_entered);
	k_sem_reset(&w_release);
	fake_i2s_block_write_at(7, &w_entered, &w_release);

	struct push_ctx pc = {
		.in = test_input_480(),
		.samples = TEST_FRAMES_480 * 2,
		.ret = -1,
	};

	spawn_push(push_worker_fn, &pc);
	zassert_true(k_sem_take(&w_entered, K_MSEC(CONCUR_TIMEOUT_MS)) == 0,
		     "push entered write gate");
	zassert_equal(audio_i2s_test_active_pushes(), 1, "one admitted push");
	zassert_true(audio_i2s_test_is_started(), "still started");

	/* Stop must not return and PREPARE/DROP must not have run while
	 * the admitted push is blocked. */
	spawn_stop_a(stop_worker_fn, NULL);
	zassert_true(conc_wait_until(stop_in_drain_cond, NULL, CONCUR_TIMEOUT_MS),
		     "stop reached the drain");
	zassert_equal(fake_i2s_trigger_calls(), 1, "only START so far");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_START, "START only");
	zassert_equal(mock_drift_reset_calls, 0, "no software reset before drain");

	/* Release the admitted write: the push completes first, then stop
	 * finalizes with PREPARE-then-DROP. */
	k_sem_give(&w_release);

	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "push joined");
	zassert_equal(pc.ret, 0, "admitted push returns 0");
	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "stop joined");

	zassert_equal(fake_i2s_trigger_calls(), 3, "START + PREPARE + DROP");
	zassert_equal(fake_i2s_trigger_rec(1)->cmd, I2S_TRIGGER_PREPARE, "PREPARE first");
	zassert_equal(fake_i2s_trigger_rec(2)->cmd, I2S_TRIGGER_DROP, "DROP second");
	zassert_equal(fake_i2s_queued_count(), 0, "queue purged");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "slab 16/16");
	zassert_false(audio_i2s_test_is_started(), "not started");
	zassert_true(audio_i2s_test_is_configured(), "configured retained");
	zassert_false(audio_i2s_test_is_accepting(), "admission closed");
	zassert_equal(audio_i2s_test_active_pushes(), 0, "zero active pushes");
	zassert_equal(audio_i2s_test_stop_callers(), 0, "caller count zero");
	zassert_false(audio_i2s_test_stop_finalizing(), "finalizing cleared");
	test_assert_no_duplicate_writes();
}

/* ── 2. two overlapping stops finalize once ──────────────────────── */

ZTEST(audio_i2s, test_two_overlapping_stops_finalize_once)
{
	test_start_stream();

	k_sem_reset(&w_entered);
	k_sem_reset(&w_release);
	fake_i2s_block_write_at(7, &w_entered, &w_release);

	struct push_ctx pc = {
		.in = test_input_480(),
		.samples = TEST_FRAMES_480 * 2,
		.ret = -1,
	};

	spawn_push(push_worker_fn, &pc);
	zassert_true(k_sem_take(&w_entered, K_MSEC(CONCUR_TIMEOUT_MS)) == 0,
		     "push entered write gate");

	spawn_stop_a(stop_worker_fn, NULL);
	spawn_stop_b(stop_worker_fn, NULL);

	zassert_true(conc_wait_until(two_stop_cohort_cond, NULL, CONCUR_TIMEOUT_MS),
		     "both stops in one cohort with a claimed finalizer");
	zassert_equal(audio_i2s_test_stop_callers(), 2, "two stop callers");
	zassert_true(audio_i2s_test_stop_finalizing(), "one finalizer claimed");
	zassert_equal(mock_drift_reset_calls, 0, "no reset before drain");
	zassert_equal(fake_i2s_trigger_calls(), 1, "no PREPARE/DROP before drain");

	k_sem_give(&w_release);

	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "push joined");
	zassert_equal(pc.ret, 0, "admitted push returns 0");
	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "stop A joined");
	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "stop B joined");

	zassert_equal(mock_drift_reset_calls, 1, "exactly one software reset");
	zassert_equal(mock_timing_reset_calls, 1, "exactly one timing reset");
	zassert_equal(fake_i2s_trigger_calls(), 3, "START + one PREPARE/DROP pair");
	zassert_equal(fake_i2s_trigger_rec(1)->cmd, I2S_TRIGGER_PREPARE, "PREPARE");
	zassert_equal(fake_i2s_trigger_rec(2)->cmd, I2S_TRIGGER_DROP, "DROP");
	zassert_equal(audio_i2s_test_stop_callers(), 0, "caller count returns zero");
	zassert_false(audio_i2s_test_stop_finalizing(), "finalizing cleared");
	zassert_equal(audio_i2s_test_active_pushes(), 0, "zero active pushes");
	zassert_equal(fake_i2s_queued_count(), 0, "queue purged");
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "no leak / double free");
	test_assert_no_duplicate_writes();
}

/* ── 3. closed rejection and reconnect ───────────────────────────── */

ZTEST(audio_i2s, test_closed_rejection_and_reconnect)
{
	test_start_stream();
	audio_sink_stop();

	/* Closed: valid push is rejected with zero mutation. */
	int writes_before = fake_i2s_write_calls();
	int triggers_before = fake_i2s_trigger_calls();
	uint32_t seq_before = audio_i2s_test_offload_sequence();

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EBUSY,
		      "closed push rejected");
	zassert_equal(fake_i2s_write_calls(), writes_before, "no writes while closed");
	zassert_equal(fake_i2s_trigger_calls(), triggers_before, "no triggers while closed");
	zassert_equal(audio_i2s_test_active_pushes(), 0, "no admitted push");
	zassert_equal(audio_i2s_test_offload_sequence(), seq_before, "sequence untouched");
	zassert_equal(mock_drift_update_calls, 0, "no drift while closed");
	zassert_false(audio_i2s_test_is_started(), "still stopped");

	/* Explicit stream open then a push performs a fresh seven-block
	 * prefill and START without reconfigure. */
	zassert_equal(audio_sink_stream_open(), 0, "stream open");
	zassert_true(audio_i2s_test_is_accepting(), "admission open");

	fake_i2s_reset();
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "reconnect push");
	zassert_equal(fake_i2s_write_calls(), 7, "fresh seven-block prefill");
	zassert_equal(fake_i2s_trigger_calls(), 1, "fresh START");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_START, "START");
	zassert_true(audio_i2s_test_is_started(), "started again");
	zassert_equal(fake_i2s_configure_calls(), 0, "no re-configure");
	test_assert_no_duplicate_writes();
}

/* ── 4. failure exits release admission ──────────────────────────── */

ZTEST(audio_i2s, test_failure_exits_release_admission)
{
	/* Startup failure row: active count returns to zero and a
	 * following stop returns promptly. */
	test_init_ok();
	fake_i2s_set_write_fail_errno(-EFAULT);
	fake_i2s_fail_write_at(2);

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EFAULT,
		      "startup write failure");
	zassert_equal(audio_i2s_test_active_pushes(), 0, "startup failure releases admission");
	zassert_true(audio_i2s_test_is_accepting(), "admission stays open");

	spawn_stop_a(stop_worker_fn, NULL);
	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "stop after startup failure returns");

	/* Steady failure row: same contract. */
	test_reset_all();
	test_start_stream();
	fake_i2s_set_write_fail_errno(-EBUSY);
	fake_i2s_fail_write_at(7);

	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EBUSY,
		      "steady write failure");
	zassert_equal(audio_i2s_test_active_pushes(), 0, "steady failure releases admission");
	zassert_true(audio_i2s_test_is_accepting(), "admission stays open");

	spawn_stop_a(stop_worker_fn, NULL);
	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "stop after steady failure returns");
}

/* ── 5. open waits for the full stop cohort ──────────────────────── */

ZTEST(audio_i2s, test_open_waits_for_full_stop_cohort)
{
	test_start_stream();

	k_sem_reset(&w_entered);
	k_sem_reset(&w_release);
	fake_i2s_block_write_at(7, &w_entered, &w_release);

	struct push_ctx pc = {
		.in = test_input_480(),
		.samples = TEST_FRAMES_480 * 2,
		.ret = -1,
	};

	spawn_push(push_worker_fn, &pc);
	zassert_true(k_sem_take(&w_entered, K_MSEC(CONCUR_TIMEOUT_MS)) == 0,
		     "push entered write gate");

	spawn_stop_a(stop_worker_fn, NULL);
	spawn_stop_b(stop_worker_fn, NULL);
	zassert_true(conc_wait_until(two_stop_cohort_cond, NULL, CONCUR_TIMEOUT_MS),
		     "cohort ready");

	/* Open waiter must not return before the push is released and
	 * both stop callers exit. */
	struct open_ctx oc = {.ret = -1};

	spawn_open(open_worker_fn, &oc);
	zassert_true(conc_wait_until(open_started_cond, NULL, CONCUR_TIMEOUT_MS),
		     "open waiter started");
	zassert_false(open_returned_flag, "open cannot return before cohort completion");

	k_sem_give(&w_release);

	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "push joined");
	zassert_equal(pc.ret, 0, "admitted push returns 0");
	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "stop A joined");
	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "stop B joined");
	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "open joined");

	zassert_equal(oc.ret, 0, "open returns 0 after the cohort");
	zassert_true(open_returned_flag, "open returned");
	zassert_true(audio_i2s_test_is_accepting(), "admission open");
	zassert_equal(audio_i2s_test_stop_callers(), 0, "caller count zero");

	/* A push is admitted after the open completes. */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0,
		      "push admitted after open");
}

/* ── 6. close is nonblocking ─────────────────────────────────────── */

ZTEST(audio_i2s, test_close_is_nonblocking)
{
	test_start_stream();

	k_sem_reset(&w_entered);
	k_sem_reset(&w_release);
	fake_i2s_block_write_at(7, &w_entered, &w_release);

	struct push_ctx pc = {
		.in = test_input_480(),
		.samples = TEST_FRAMES_480 * 2,
		.ret = -1,
	};

	spawn_push(push_worker_fn, &pc);
	zassert_true(k_sem_take(&w_entered, K_MSEC(CONCUR_TIMEOUT_MS)) == 0,
		     "push entered write gate");
	zassert_equal(audio_i2s_test_active_pushes(), 1, "one admitted push");

	/* Close returns immediately while the admitted push stays blocked:
	 * no reset, no PREPARE/DROP, DMA untouched. */
	audio_sink_stream_close();
	zassert_false(audio_i2s_test_is_accepting(), "admission closed");
	zassert_equal(mock_drift_reset_calls, 0, "no reset from close");
	zassert_equal(fake_i2s_trigger_calls(), 1, "no PREPARE/DROP from close");
	zassert_true(audio_i2s_test_is_started(), "DMA not touched by close");
	zassert_equal(audio_i2s_test_active_pushes(), 1, "admitted push still active");

	/* New push rejected while closed. */
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), -EBUSY,
		      "push while closed rejected");
	zassert_equal(audio_i2s_test_active_pushes(), 1, "no new admission");

	/* Release the admitted push: it completes normally. */
	k_sem_give(&w_release);
	zassert_true(conc_wait_done(CONCUR_TIMEOUT_MS), "push joined");
	zassert_equal(pc.ret, 0, "admitted push returns 0");
	zassert_equal(audio_i2s_test_active_pushes(), 0, "drained");

	/* Explicit open restores admission. */
	zassert_equal(audio_sink_stream_open(), 0, "open after close");
	zassert_true(audio_i2s_test_is_accepting(), "accepting again");
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0,
		      "push admitted after reopen");
}
