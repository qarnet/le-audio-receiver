/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5: production nRF54 timing suite.
 *
 * Compiles and executes the real src/audio_timing_nrf54.c and
 * src/audio_timing_math.c against test-owned include shadows of the
 * installed nrfx_grtc / nrfx_gppi / nrf_grtc / nrf_timer HALs (see
 * include/) and a mock of audio_drift_frequency_error_update().
 * No timing algorithm is copied into this suite; the mocks only
 * capture calls, arguments, callbacks, and resource state.
 *
 * The GRTC compare callback is invoked exactly as the driver would:
 * tests fire the handler captured by the mocked
 * nrfx_grtc_channel_callback_set(), then explicitly run the captured
 * deferred work item on the real system workqueue.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <errno.h>

#include <hal/nrf_timer.h>

#include "audio_timing.h"
#include "audio_timing_math.h"
#include "mock_drift.h"
#include "mock_hal.h"

/* Test seams compiled into the production file under
 * AUDIO_TIMING_NRF54_TEST (see src/audio_timing_nrf54.c). */
struct k_work *audio_timing_test_take_captured_work(void);
bool audio_timing_test_is_active(void);
uint32_t audio_timing_test_generation(void);
void audio_timing_test_state_reset(void);

/* Work synchronization object must be cache-coherent and static. */
static struct k_work_sync work_sync;

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	mock_hal_reset();
	mock_drift_reset();
	audio_timing_test_state_reset();
}

ZTEST_SUITE(timing_nrf54, NULL, NULL, reset_before_each, NULL, NULL);

/* ── Helpers ─────────────────────────────────────────────────────── */

static void init_ok(void)
{
	zassert_equal(audio_timing_init(), 0, "init succeeds");
}

static void fire_cc(uint64_t cc_value)
{
	zassert_not_null(mock_grtc_cb_handler, "GRTC cc callback must be registered");
	mock_grtc_cb_handler(0, cc_value, NULL);
}

static void run_captured_work(void)
{
	struct k_work *work = audio_timing_test_take_captured_work();

	zassert_not_null(work, "diag work must have been submitted");
	zassert_equal(k_work_submit(work), 1, "captured work queued");
	k_work_flush(work, &work_sync);
}

static int log_count(enum mock_event ev)
{
	int n = 0;

	for (int i = 0; i < mock_hal_log_len(); i++) {
		if (mock_hal_log_ev(i) == ev) {
			n++;
		}
	}
	return n;
}

static int log_first(enum mock_event ev)
{
	for (int i = 0; i < mock_hal_log_len(); i++) {
		if (mock_hal_log_ev(i) == ev) {
			return i;
		}
	}
	return -1;
}

/* ── 1. GRTC allocation failure ──────────────────────────────────── */

ZTEST(timing_nrf54, test_grtc_alloc_failure_returns_exact_error_no_setup)
{
	mock_grtc_alloc_ret = -ENOMEM;

	zassert_equal(audio_timing_init(), -ENOMEM, "exact error propagates");
	zassert_equal(mock_hal_log_len(), 1, "only the alloc attempt happens");
	zassert_equal(mock_hal_log_ev(0), EV_GRTC_ALLOC, "first event is the alloc");
	zassert_equal(mock_grtc_cc_abs_calls, 0, "no compare programmed");
	zassert_equal(mock_grtc_cc_disable_calls, 0, "nothing to disable");
	zassert_equal(mock_gppi_conn_enable_calls, 0, "no GPPI setup");
	zassert_equal(mock_timer_mode_last, -1, "no TIMER configuration");
}

/* ── 2. GPPI allocation failure cleanup ──────────────────────────── */

ZTEST(timing_nrf54, test_gppi_alloc_failure_disables_cc_then_frees_channel)
{
	mock_gppi_conn_alloc_ret = -ENOMEM;

	zassert_equal(audio_timing_init(), -ENOMEM, "exact error propagates");

	/* The GRTC compare/interrupt state enabled before the GPPI
	 * allocation must be undone through cc_disable, then the
	 * channel freed. */
	zassert_equal(mock_grtc_cc_disable_calls, 1, "cc disabled once");
	zassert_equal(mock_grtc_cc_disable_last_channel, mock_grtc_alloc_channel,
		      "disabled on the allocated channel");
	zassert_equal(mock_grtc_free_channel, mock_grtc_alloc_channel, "channel freed");

	int disable_idx = log_first(EV_GRTC_CC_DISABLE);
	int free_idx = log_first(EV_GRTC_FREE);

	zassert_true(disable_idx >= 0 && free_idx > disable_idx, "disable before free");

	/* No GPPI free occurs because the allocation never succeeded. */
	zassert_equal(mock_gppi_conn_enable_calls, 0, "no GPPI enable");
	zassert_equal(log_count(EV_GPPI_CONN_ALLOC), 1, "single alloc attempt");
}

/* ── 3. Successful init sequence, wiring, idempotence ────────────── */

ZTEST(timing_nrf54, test_init_success_sequence_and_idempotent)
{
	init_ok();

	/* GRTC: alloc → compare event enable → callback set. */
	zassert_equal(mock_hal_log_ev(0), EV_GRTC_ALLOC, "alloc first");
	zassert_equal(mock_hal_log_ev(1), EV_GRTC_COMPARE_EVENT_ENABLE, "event enabled");
	zassert_equal(mock_hal_log_a(1), mock_grtc_alloc_channel, "enabled channel");
	zassert_equal(mock_hal_log_ev(2), EV_GRTC_CALLBACK_SET, "callback registered");
	zassert_equal(mock_hal_log_a(2), mock_grtc_alloc_channel, "callback channel");
	zassert_not_null(mock_grtc_cb_handler, "handler captured");

	/* TIMER: 32-bit TIMER mode, prescaler 0, CLEAR then START. */
	zassert_equal(mock_timer_mode_last, (int)NRF_TIMER_MODE_TIMER, "TIMER mode");
	zassert_equal(mock_timer_bw_last, (int)NRF_TIMER_BIT_WIDTH_32, "32-bit");
	zassert_equal(mock_timer_prescaler_last, 0, "prescaler 0");
	int clear_idx = -1;
	int start_idx = -1;

	for (int i = 0; i < mock_hal_log_len(); i++) {
		if (mock_hal_log_ev(i) == EV_TIMER_TASK_TRIGGER &&
		    mock_hal_log_a(i) == (uint64_t)NRF_TIMER_TASK_CLEAR) {
			clear_idx = i;
		}
		if (mock_hal_log_ev(i) == EV_TIMER_TASK_TRIGGER &&
		    mock_hal_log_a(i) == (uint64_t)NRF_TIMER_TASK_START) {
			start_idx = i;
		}
	}
	zassert_true(clear_idx >= 0 && start_idx > clear_idx, "CLEAR then START");

	/* GPPI: the GRTC compare event address is routed to the TIMER
	 * CAPTURE[0] task address, then the connection is enabled.
	 * The event address encodes the allocated channel. */
	zassert_equal(mock_gppi_last_eep, mock_grtc_event_addr + mock_grtc_alloc_channel,
		      "GRTC event wired as eep");
	zassert_equal(mock_gppi_last_tep, mock_timer_task_addr, "CAPTURE task wired as tep");
	zassert_equal(mock_gppi_conn_enable_calls, 1, "GPPI enabled once");
	zassert_equal(mock_gppi_last_handle, mock_gppi_next_handle, "enabled handle");

	/* Nominal base frequency captured from the HAL macro. */
	zassert_equal(mock_nrf_timer_base_frequency_hz, 16000000, "mock nominal stays 16 MHz");

	/* Idempotent: a second init does no further hardware setup. */
	zassert_equal(audio_timing_init(), 0, "second init returns 0");
	zassert_equal(log_count(EV_GRTC_ALLOC), 1, "single channel alloc");
	zassert_equal(mock_grtc_cc_abs_calls, 0, "no compare before any SDU");
	zassert_equal(mock_gppi_conn_enable_calls, 1, "no second GPPI enable");
}

/* ── 4. No-ops: update before init, zero SDU timestamp ───────────── */

ZTEST(timing_nrf54, test_update_before_init_and_zero_ts_noop)
{
	audio_timing_sdu_ref_update(100000, 20000); /* before init */
	zassert_equal(mock_grtc_cc_abs_calls, 0, "no compare before init");

	init_ok();
	audio_timing_sdu_ref_update(0, 20000); /* zero SDU timestamp */
	zassert_equal(mock_grtc_cc_abs_calls, 0, "zero ts is a no-op");
	zassert_false(audio_timing_test_is_active(), "not active");
}

/* ── 5. One anchor per session; later SDUs do not reschedule ─────── */

ZTEST(timing_nrf54, test_first_valid_ts_single_anchor_no_reschedule)
{
	init_ok();
	mock_grtc_now = 1000000ULL;

	audio_timing_sdu_ref_update(2000000, 0);
	zassert_equal(mock_grtc_cc_abs_calls, 1, "one compare");
	zassert_equal(mock_grtc_cc_abs_last_channel, mock_grtc_alloc_channel, "channel");
	zassert_equal(mock_grtc_cc_abs_last_value, 3000000ULL, "anchor + 1 s");
	zassert_true(mock_grtc_cc_abs_last_irq, "interrupt enabled");
	zassert_true(audio_timing_test_is_active(), "measurement active");

	/* Later SDUs in the same session must not reschedule. */
	audio_timing_sdu_ref_update(9000000, 0);
	zassert_equal(mock_grtc_cc_abs_calls, 1, "still one compare");
}

/* ── 6a. Future anchor schedules anchor + 1 s ────────────────────── */

ZTEST(timing_nrf54, test_first_compare_future_anchor)
{
	init_ok();
	mock_grtc_now = 1000000ULL;

	/* ts in the current epoch, ahead of now → anchor = ts,
	 * first compare = anchor + 1 s. */
	audio_timing_sdu_ref_update(3000000, 0);
	zassert_equal(mock_grtc_cc_abs_last_value, 4000000ULL, "anchor + 1 s");
}

/* ── 6b. Past first compare schedules now + 1 s ──────────────────── */

ZTEST(timing_nrf54, test_first_compare_past_schedules_now_plus_1s)
{
	init_ok();

	/* Near the 64-bit GRTC wrap the anchor + 1 s arithmetic wraps,
	 * landing in the past; the fallback must schedule now + 1 s. */
	mock_grtc_now = 0xFFFFFFFFFFFFFF00ULL;
	audio_timing_sdu_ref_update(0xFFFFFF10, 0);
	zassert_equal(mock_grtc_cc_abs_last_value, mock_grtc_now + 1000000ULL, "now + 1 s");
}

/* ── 6c. 32-bit timestamp wrap ───────────────────────────────────── */

ZTEST(timing_nrf54, test_first_compare_32bit_ts_wrap)
{
	init_ok();

	/* ts + pd wraps the 32-bit timestamp into the next epoch;
	 * the anchor expands one epoch ahead and the first compare is
	 * anchor + 1 s. */
	mock_grtc_now = 0x0000000500000030ULL;
	audio_timing_sdu_ref_update(0xFFFFFFF0, 0x20);
	zassert_equal(mock_grtc_cc_abs_last_value, 0x00000006000F4250ULL,
		      "anchor + 1 s across the epoch boundary");
}

/* ── 7. First compare callback: baseline, no feedforward ─────────── */

ZTEST(timing_nrf54, test_first_cc_callback_baseline_no_feedforward)
{
	init_ok();
	mock_grtc_now = 1000000ULL;
	audio_timing_sdu_ref_update(2000000, 0); /* first_cmp = 3 s */

	/* Fire on time; TIMER captured 16,000,000 ticks. */
	mock_grtc_now = 3000000ULL;
	mock_timer_cc_value = 16000000;
	fire_cc(3000000ULL);

	zassert_equal(mock_grtc_cc_abs_calls, 2, "next compare scheduled");
	zassert_equal(mock_grtc_cc_abs_last_value, 4000000ULL, "cc + 1 s");
	zassert_equal(audio_timing_test_take_captured_work(), NULL, "baseline publishes no work");
	zassert_equal(mock_drift_ppm_count(), 0, "no feedforward on the first compare");
	zassert_true(audio_timing_test_is_active(), "measurement stays active");
}

/* ── 8. Second callback delivers exact ppm incl. TIMER32 wrap ────── */

ZTEST(timing_nrf54, test_second_cc_callback_delivers_exact_ppm_timer_wrap)
{
	init_ok();
	mock_grtc_now = 1000000ULL;
	audio_timing_sdu_ref_update(2000000, 0); /* first_cmp = 3 s */

	/* Baseline capture just before the 32-bit TIMER wrap. */
	mock_grtc_now = 3000000ULL;
	mock_timer_cc_value = 0xFFF00000U;
	fire_cc(3000000ULL);
	zassert_equal(mock_drift_ppm_count(), 0, "baseline still silent");

	/* One second later the counter wrapped; it gained
	 * 16,000,032 ticks → exactly 2 ppm. */
	mock_grtc_now = 4000000ULL;
	mock_timer_cc_value = 0x00E42420U; /* 0xFFF00000 + 16000032 mod 2^32 */
	fire_cc(4000000ULL);

	zassert_equal(mock_grtc_cc_abs_calls, 3, "rescheduled after measurement");
	run_captured_work();
	zassert_equal(mock_drift_ppm_count(), 1, "one delivery");
	zassert_equal(mock_drift_ppm_at(0), 2, "32 ticks @ 16 MHz = 2 ppm");
}

/* ── 9. Late callback reschedules at now + 1 s ───────────────────── */

ZTEST(timing_nrf54, test_late_cc_callback_reschedules_now_plus_1s)
{
	init_ok();
	mock_grtc_now = 1000000ULL;
	audio_timing_sdu_ref_update(2000000, 0); /* first_cmp = 3 s */

	/* The callback fires 2 s late: cc + 1 s is already in the
	 * past, so the next compare must be now + 1 s. */
	mock_grtc_now = 5000000ULL;
	mock_timer_cc_value = 16000000;
	fire_cc(3000000ULL);

	zassert_equal(mock_grtc_cc_abs_calls, 2, "rescheduled");
	zassert_equal(mock_grtc_cc_abs_last_value, 6000000ULL, "now + 1 s");
}

/* ── 10. Reschedule failure: inactive, one deferred error, silence ─ */

ZTEST(timing_nrf54, test_reschedule_failure_clears_active_deferred_error_no_later_measurement)
{
	init_ok();
	mock_grtc_now = 1000000ULL;
	audio_timing_sdu_ref_update(2000000, 0); /* first_cmp = 3 s */

	mock_grtc_now = 3000000ULL;
	mock_timer_cc_value = 16000000;
	mock_grtc_cc_abs_ret = -ECANCELED;
	fire_cc(3000000ULL);

	zassert_false(audio_timing_test_is_active(), "active cleared on schedule failure");
	zassert_equal(mock_grtc_cc_abs_calls, 2, "anchor + one failed reschedule");

	/* One deferred error payload; running it delivers no
	 * measurement. */
	run_captured_work();
	zassert_equal(mock_drift_ppm_count(), 0, "error payload delivers no ppm");

	/* Later callbacks and SDUs stay silent: inactive, and the
	 * anchor remains set. */
	zassert_equal(audio_timing_test_take_captured_work(), NULL, "no further work");
	int abs_after_failure = mock_grtc_cc_abs_calls;

	fire_cc(4000000ULL);
	zassert_equal(mock_grtc_cc_abs_calls, abs_after_failure, "no reschedule while inactive");

	audio_timing_sdu_ref_update(9000000, 0);
	zassert_equal(mock_grtc_cc_abs_calls, abs_after_failure, "no re-anchor after failure");
	zassert_equal(mock_drift_ppm_count(), 0, "no later measurement delivered");
}

/* ── 11. Reset: session cleared, compare disabled, generation bump ─ */

ZTEST(timing_nrf54, test_reset_clears_session_disables_cc_generation_permits_new_anchor)
{
	init_ok();
	mock_grtc_now = 1000000ULL;
	audio_timing_sdu_ref_update(2000000, 0);
	mock_grtc_now = 3000000ULL;
	mock_timer_cc_value = 16000000;
	fire_cc(3000000ULL);
	zassert_true(audio_timing_test_is_active(), "active during session");

	uint32_t gen_before = audio_timing_test_generation();

	audio_timing_reset();
	zassert_false(audio_timing_test_is_active(), "inactive after reset");
	zassert_equal(audio_timing_test_generation(), gen_before + 1, "generation incremented");
	zassert_equal(mock_grtc_cc_disable_calls, 1, "compare disabled");
	zassert_equal(mock_grtc_cc_disable_last_channel, mock_grtc_alloc_channel,
		      "disabled on the allocated channel");

	/* One new anchor is permitted after reset. */
	int abs_before = mock_grtc_cc_abs_calls;

	mock_grtc_now = 1000000ULL;
	audio_timing_sdu_ref_update(2000000, 0);
	zassert_equal(mock_grtc_cc_abs_calls, abs_before + 1, "new anchor scheduled");
	zassert_true(audio_timing_test_is_active(), "new session active");
}

/* ── 12. Work captured before reset is rejected as stale ─────────── */

ZTEST(timing_nrf54, test_work_captured_before_reset_rejected_stale)
{
	init_ok();
	mock_grtc_now = 1000000ULL;
	audio_timing_sdu_ref_update(2000000, 0); /* first_cmp = 3 s */

	/* Baseline compare. */
	mock_grtc_now = 3000000ULL;
	mock_timer_cc_value = 16000000;
	fire_cc(3000000ULL);

	/* Measurement compare captures work with the old generation. */
	mock_grtc_now = 4000000ULL;
	mock_timer_cc_value = 16000016;
	fire_cc(4000000ULL);
	struct k_work *stale = audio_timing_test_take_captured_work();

	zassert_not_null(stale, "work captured before reset");

	audio_timing_reset(); /* generation increments */

	zassert_equal(k_work_submit(stale), 1, "stale work queued");
	k_work_flush(stale, &work_sync);
	zassert_equal(mock_drift_ppm_count(), 0, "stale work delivers nothing");

	/* Control: a fresh session delivers again. */
	mock_grtc_now = 1000000ULL;
	audio_timing_sdu_ref_update(2000000, 0);
	mock_grtc_now = 3000000ULL;
	mock_timer_cc_value = 16000000;
	fire_cc(3000000ULL); /* baseline */
	mock_grtc_now = 4000000ULL;
	mock_timer_cc_value = 32000016; /* +16,000,016 ticks → 1 ppm */
	fire_cc(4000000ULL);
	run_captured_work();
	zassert_equal(mock_drift_ppm_count(), 1, "fresh session delivers");
	zassert_equal(mock_drift_ppm_at(0), 1, "1 ppm");
}

/* ── 13. Every measurement reaches drift despite log pacing ──────── */

ZTEST(timing_nrf54, test_every_measurement_reaches_drift_despite_log_pacing)
{
	init_ok();
	mock_grtc_now = 1000000ULL;
	audio_timing_sdu_ref_update(2000000, 0); /* first_cmp = 3 s */

	/* Baseline compare: no delivery. */
	mock_grtc_now = 3000000ULL;
	mock_timer_cc_value = 16000000;
	fire_cc(3000000ULL);
	zassert_equal(mock_drift_ppm_count(), 0, "baseline silent");

	/* Ten one-second cycles, each gaining 16,000,016 ticks (1 ppm
	 * over the 16 MHz nominal).  The diagnostic log is paced (seq 1
	 * and every 5 s), but the feedforward must be delivered for
	 * every measurement. */
	for (int i = 1; i <= 10; i++) {
		mock_grtc_now = 3000000ULL + (uint64_t)i * 1000000ULL;
		mock_timer_cc_value = 16000000U + (uint32_t)i * 16000016U;
		fire_cc(mock_grtc_now);
		run_captured_work();
		zassert_equal(mock_drift_ppm_count(), i, "delivery %d", i);
		zassert_equal(mock_drift_ppm_at(i - 1), 1, "exact ppm for cycle %d", i);
	}
}
