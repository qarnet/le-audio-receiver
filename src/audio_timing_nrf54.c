/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * nRF54L15 audio timing measurement: hardware-timed PCLK clock progress
 * against Bluetooth controller / GRTC time.
 *
 * Design (Phase 4b.1, revised 2026-07-26):
 *  1. TIMER20 runs in TIMER mode (free-running PCLK-derived ticks).
 *  2. GRTC compare at 1 s intervals → GPPI → TIMER20 CAPTURE[0]
 *     (hardware snapshots timer count; zero callback latency).
 *  3. GRTC ISR reads captured count, computes unsigned delta and
 *     elapsed GRTC microseconds, then logs bounded diagnostics via
 *     a work item.  Reported ppm is local PCLK frequency error
 *     relative to controller/GRTC time.
 *
 * Historical note: original design counted I2S20 FRAMESTART edges via
 * GPPI → TIMER20 COUNT.  Hardware validation on 2026-07-26 showed
 * FRAMESTART fires at DMA audio-buffer boundaries (~100 Hz in this
 * configuration), not every physical LRCK edge (~47,619 Hz).
 * Counting FRAMESTART cannot measure sample-clock frequency.
 * The production path is PCLK-derived TIMER captured at GRTC
 * presentation references.
 *
 * No direct RADIO access.  SDC/MPSL owns the RADIO peripheral.
 *
 * TIMER20 register base derived from devicetree (&timer20), not a
 * raw address.  HAL functions (nrf_timer_*) are used instead of
 * nrfx_timer so no CONFIG_NRFX_TIMER is required.
 */

#include "audio_timing.h"
#include "audio_timing_math.h"
#include "audio_drift.h"

#include <nrfx_grtc.h>
#include <helpers/nrfx_gppi.h>
#include <hal/nrf_grtc.h>
#include <hal/nrf_timer.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#ifdef AUDIO_TIMING_NRF54_TEST
#include <string.h>
#endif

LOG_MODULE_REGISTER(audio_timing, LOG_LEVEL_INF);

/* ── Hardware identities ─────────────────────────────────────────── */

/* Derive TIMER20 register base from devicetree.
 * On nRF54L15 cpuapp: &timer20 { reg = <0xca000 0x1000>; }
 * resolved to absolute address by DT_REG_ADDR.
 * Under AUDIO_TIMING_NRF54_TEST a test-owned NRF_TIMER_Type object is
 * used instead of devicetree parsing (see tests/unit/timing_nrf54).
 */
#ifdef AUDIO_TIMING_NRF54_TEST
#define TIMER20_BASE ((uintptr_t)&test_timer_reg)
#else
#define TIMER20_NODE DT_NODELABEL(timer20)
#define TIMER20_BASE DT_REG_ADDR(TIMER20_NODE)
#endif

/* ── Diagnostic pacing ────────────────────────────────────────────── */
#define DIAG_PERIOD_S  5U /* log at most every 5 seconds */
#define DIAG_FIRST_SEQ 1  /* always log first measurement */

/* ── Static hardware resources ────────────────────────────────────── */
static NRF_TIMER_Type *const timer_reg = (NRF_TIMER_Type *)TIMER20_BASE;
static uint32_t timer_nominal_hz;           /* cached at init time */
static uint8_t grtc_channel;                /* allocated GRTC CC channel */
static nrfx_gppi_handle_t gppi_grtc_to_cap; /* GRTC COMPARE → CAPTURE */

/* ── Timing state ─────────────────────────────────────────────────── */
struct timing_state {
	bool init_done;           /* init() succeeded */
	bool anchor_set;          /* first valid SDU has arrived */
	uint64_t anchor_us;       /* GRTC anchor time (= ts + pd, converted) */
	uint32_t last_cap;        /* previous TIMER20 capture value */
	uint64_t last_compare_us; /* GRTC time of last compare (cc_value) */
	uint32_t diag_seq;        /* diagnostic sequence (reset per session) */
};

static struct timing_state ts;

/* Active flag: cleared before hardware is disabled in reset,
 * checked in ISR and work handler to prevent stale callbacks.
 */
static atomic_t active;

/* Generation counter — incremented on each reset, carried by
 * the diagnostic payload so the work handler can reject stale data.
 */
static atomic_t generation;

/* Work item for deferred logging (ISR must not log) */
static struct k_work diag_work;

/* Saved diagnostics for the work handler.
 * Published under diag_lock; consumed by work handler under the
 * same lock to prevent races with ISR and reset.
 */
struct diag_payload {
	uint32_t tick_delta; /* TIMER capture delta (PCLK ticks) */
	uint32_t elapsed_us; /* GRTC elapsed microseconds */
	uint32_t nominal_hz; /* TIMER nominal base frequency */
	uint32_t seq;        /* diagnostic sequence number */
	atomic_val_t gen;    /* generation at capture time */
	bool is_error;       /* true → schedule_err is valid */
	int schedule_err;    /* error code when is_error */
};

static struct diag_payload pending_diag;
static struct k_spinlock diag_lock;

/* ── Work handler (deferred from ISR) ─────────────────────────────── */

static void diag_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Snapshot the payload under lock so ISR/reset cannot
	 * mutate it while we inspect it.  Only the snapshot is
	 * validated against the current session generation.
	 */
	struct diag_payload diag;
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	diag = pending_diag;
	k_spin_unlock(&diag_lock, key);

	/* Reject stale payload from a previous session */
	if (diag.gen != atomic_get(&generation)) {
		return;
	}

	/* Error payload: schedule failure deferred from ISR */
	if (diag.is_error) {
		LOG_ERR("GRTC compare schedule failed: %d (seq %" PRIu32 ")", diag.schedule_err,
			diag.seq);
		return;
	}

	/* Not an error but no measurement data (initial skip, or
	 * cleared by reset / overwritten).
	 */
	if (diag.elapsed_us == 0) {
		return;
	}

	/* Measurement is no longer active — discard */
	if (!atomic_get(&active)) {
		return;
	}

	/* Nominal ticks expected in this interval at the timer's base
	 * frequency: timer_nominal_hz * elapsed_us / 1,000,000.
	 */
	uint64_t nominal64 = ((uint64_t)diag.nominal_hz * diag.elapsed_us) / 1000000ULL;
	uint32_t nominal = (nominal64 > UINT32_MAX) ? UINT32_MAX : (uint32_t)nominal64;

	int32_t ppm = audio_timing_compute_ppm(diag.tick_delta, nominal);

	/* Phase 4b.2: every measurement feeds the PCLK frequency
	 * error into the drift controller's feedforward path.
	 * Positive ppm → local PCLK/I2S faster than controller.
	 */
	audio_drift_frequency_error_update(ppm);

	/* Bounded diagnostic logging: sequence 1 and every DIAG_PERIOD_S. */
	if (diag.seq == DIAG_FIRST_SEQ || (diag.seq % DIAG_PERIOD_S) == 0) {
		LOG_INF("PCLK timer diag[%" PRIu32 "]: %" PRIu32 " ticks in %" PRIu32
			" us (nom %" PRIu32 " @ %" PRIu32 " Hz) → %" PRId32 " ppm",
			diag.seq, diag.tick_delta, diag.elapsed_us, nominal, diag.nominal_hz, ppm);
	}
}

#ifdef AUDIO_TIMING_NRF54_TEST
/*
 * Test seams (tests/unit/timing_nrf54):
 *  - audio_timing_submit_diag_work() captures the work item instead of
 *    dispatching it, so tests can run it explicitly;
 *  - audio_timing_test_state_reset() clears file-static module state
 *    between tests without pretending to release hardware;
 *  - audio_timing_test_is_active()/audio_timing_test_generation()
 *    read the minimal state the mocks cannot observe.
 * None of these enter production firmware builds.
 */
static struct k_work *test_captured_work;

static void audio_timing_submit_diag_work(void)
{
	test_captured_work = &diag_work;
}

struct k_work *audio_timing_test_take_captured_work(void)
{
	struct k_work *work = test_captured_work;

	test_captured_work = NULL;
	return work;
}

bool audio_timing_test_is_active(void)
{
	return atomic_get(&active) != 0;
}

uint32_t audio_timing_test_generation(void)
{
	return atomic_get(&generation);
}

void audio_timing_test_state_reset(void)
{
	memset(&ts, 0, sizeof(ts));
	grtc_channel = 0;
	gppi_grtc_to_cap = 0;
	timer_nominal_hz = 0;
	atomic_set(&active, 0);
	atomic_set(&generation, 0);
	memset(&pending_diag, 0, sizeof(pending_diag));
	test_captured_work = NULL;
}
#else
static void audio_timing_submit_diag_work(void)
{
	k_work_submit(&diag_work);
}
#endif /* AUDIO_TIMING_NRF54_TEST */

/* ── GRTC compare callback (ISR context) ──────────────────────────── */

static void grtc_cc_handler(int32_t id, uint64_t cc_value, void *p_context)
{
	ARG_UNUSED(id);
	ARG_UNUSED(p_context);

	/* If measurement is inactive, do not reschedule or publish */
	if (!atomic_get(&active)) {
		return;
	}

	/* Read captured TIMER20 count — hardware snapshotted at
	 * the compare instant via GPPI, so this value is jitter-free.
	 */
	uint32_t cap = nrf_timer_cc_get(timer_reg, NRF_TIMER_CC_CHANNEL0);

	/* Schedule next absolute compare at previous compare + 1 s */
	uint64_t next_cmp = cc_value + 1000000ULL;

	/* Check that next cmp is safely in the future */
	uint64_t now = nrfx_grtc_syscounter_get();

	if (next_cmp <= now) {
		/* We're late — skip a cycle, schedule 1 s from now */
		next_cmp = now + 1000000ULL;
	}

	nrfx_grtc_channel_t chan_data = {
		.channel = grtc_channel,
		.handler = grtc_cc_handler,
		.p_context = NULL,
	};
	int ret = nrfx_grtc_syscounter_cc_absolute_set(&chan_data, next_cmp, true);
	if (ret < 0) {
		/* Schedule failure: stop active measurement and
		 * defer a single error log via the work handler.
		 * Do NOT log from ISR context.
		 */
		atomic_set(&active, false);

		k_spinlock_key_t key = k_spin_lock(&diag_lock);
		pending_diag.is_error = true;
		pending_diag.schedule_err = ret;
		pending_diag.seq = ts.diag_seq;
		pending_diag.gen = atomic_get(&generation);
		k_spin_unlock(&diag_lock, key);

		audio_timing_submit_diag_work();
		return;
	}

	/* Accumulate diagnostics */
	if (ts.last_compare_us != 0) {
		uint32_t tick_delta = audio_timing_counter_delta_u32(cap, ts.last_cap);
		uint32_t elapsed_us = (uint32_t)(cc_value - ts.last_compare_us);

		/* Diagnostic sequence is owned by ISR and reset;
		 * carried in the payload so the work handler never
		 * reads mutable sequence from global state.
		 */
		ts.diag_seq++;

		/* Phase 4b.2: publish every measurement for the
		 * frequency-error feedforward path.  Diagnostic
		 * logging is gated in the work handler.
		 */
		k_spinlock_key_t key = k_spin_lock(&diag_lock);
		pending_diag.is_error = false;
		pending_diag.tick_delta = tick_delta;
		pending_diag.elapsed_us = elapsed_us;
		pending_diag.nominal_hz = timer_nominal_hz;
		pending_diag.seq = ts.diag_seq;
		pending_diag.gen = atomic_get(&generation);
		k_spin_unlock(&diag_lock, key);

		audio_timing_submit_diag_work();
	}

	/* Update state for next interval */
	ts.last_cap = cap;
	ts.last_compare_us = cc_value;
}

/* ── Public API ───────────────────────────────────────────────────── */

int audio_timing_init(void)
{
	int ret;

	if (ts.init_done) {
		return 0;
	}

	/* --- GRTC channel allocation --- */
	ret = nrfx_grtc_channel_alloc(&grtc_channel);
	if (ret < 0) {
		LOG_ERR("GRTC channel alloc failed: %d", ret);
		return ret;
	}

	nrf_grtc_sys_counter_compare_event_enable(NRF_GRTC, grtc_channel);

	/* Register the callback that the compare absolute_set will use */
	nrfx_grtc_channel_callback_set(grtc_channel, grtc_cc_handler, NULL);

	/* --- TIMER20: 32-bit TIMER mode, prescaler 0, free-running ---
	 * TIMER mode (not COUNTER) counts PCLK-derived ticks at
	 * the peripheral's base frequency.  The nominal frequency
	 * is determined by the HAL macro NRF_TIMER_BASE_FREQUENCY_GET.
	 * On nRF54L15 cpuapp, TIMER20 falls back to the 16 MHz path.
	 */
	nrf_timer_mode_set(timer_reg, NRF_TIMER_MODE_TIMER);
	nrf_timer_bit_width_set(timer_reg, NRF_TIMER_BIT_WIDTH_32);
	nrf_timer_prescaler_set(timer_reg, 0);
	nrf_timer_task_trigger(timer_reg, NRF_TIMER_TASK_CLEAR);
	nrf_timer_task_trigger(timer_reg, NRF_TIMER_TASK_START);

	timer_nominal_hz = NRF_TIMER_BASE_FREQUENCY_GET(timer_reg);

	/* --- GPPI: GRTC COMPARE → TIMER20 CAPTURE[0] ---
	 * Hardware snapshots the timer count at the GRTC compare
	 * instant, eliminating ISR-latency jitter.
	 */
	uint32_t grtc_evt = nrf_grtc_event_address_get(
		NRF_GRTC, nrf_grtc_sys_counter_compare_event_get(grtc_channel));
	uint32_t cap_tsk = nrf_timer_task_address_get(
		timer_reg, nrf_timer_capture_task_get(NRF_TIMER_CC_CHANNEL0));

	ret = nrfx_gppi_conn_alloc(grtc_evt, cap_tsk, &gppi_grtc_to_cap);
	if (ret < 0) {
		LOG_ERR("GPPI GRTC→CAPTURE alloc failed: %d", ret);
		/* The GRTC compare event and its interrupt were enabled
		 * above; undo that state through
		 * nrfx_grtc_syscounter_cc_disable() before freeing the
		 * channel.  No GPPI free applies: the allocation never
		 * succeeded (nrfx_gppi_conn_alloc() returns before
		 * writing a handle on failure).
		 */
		nrfx_grtc_syscounter_cc_disable(grtc_channel);
		nrfx_grtc_channel_free(grtc_channel);
		return ret;
	}
	nrfx_gppi_conn_enable(gppi_grtc_to_cap);

	/* Work item for deferred logging */
	k_work_init(&diag_work, diag_work_handler);

	atomic_set(&active, false);
	atomic_set(&generation, 0);

	ts.init_done = true;
	ts.anchor_set = false;

	LOG_INF("Audio timing: GRTC+TIMER20+GPPI ready (timer %" PRIu32 " Hz)", timer_nominal_hz);
	return 0;
}

void audio_timing_sdu_ref_update(uint32_t sdu_ts_us, uint32_t pd_us)
{
	if (!ts.init_done) {
		return;
	}

	if (sdu_ts_us == 0) {
		return;
	}

	if (ts.anchor_set) {
		/* Already anchored — one compare at a time, skip
		 * subsequent SDUs until Phase 4b.2.
		 */
		return;
	}

	/* First valid SDU with TS: compute presentation anchor and
	 * schedule first compare.  The presentation delay is added
	 * before 64-bit expansion per Nordic iso_time_sync pattern
	 * (iso_rx.c → timed_led_toggle.c) so that a raw SDU timestamp
	 * behind the current GRTC is not expanded ~71.6 minutes ahead.
	 */
	uint64_t now = nrfx_grtc_syscounter_get();
	uint64_t anchor = audio_timing_anchor_to_grtc64(sdu_ts_us, pd_us, now);

	/* First compare = anchor (presentation time) + 1 second */
	uint64_t first_cmp = anchor + 1000000ULL;

	/* Ensure first compare is in the future */
	if (first_cmp <= now) {
		first_cmp = now + 1000000ULL;
	}

	nrfx_grtc_channel_t chan_data = {
		.channel = grtc_channel,
		.handler = grtc_cc_handler,
		.p_context = NULL,
	};
	int ret = nrfx_grtc_syscounter_cc_absolute_set(&chan_data, first_cmp, true);
	if (ret < 0) {
		LOG_ERR("First GRTC compare set failed: %d", ret);
		return;
	}

	/* Measurement is now active */
	atomic_set(&active, true);

	ts.anchor_us = anchor;
	ts.anchor_set = true;
	ts.last_compare_us = 0; /* force first delta skip */
	ts.diag_seq = 0;        /* start fresh diagnostic sequence */

	LOG_INF("Timing anchor: ts=%" PRIu32 " pd=%" PRIu32 " anchor_grtc=%" PRIu64
		" first_cmp=%" PRIu64,
		sdu_ts_us, pd_us, anchor, first_cmp);
}

void audio_timing_reset(void)
{
	if (!ts.init_done) {
		return;
	}

	/* Invalidate payload, mark inactive, and bump generation
	 * under the spinlock so any concurrent ISR sees a consistent
	 * state and any already-queued work payload is rejected.
	 */
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	atomic_set(&active, false);
	atomic_inc(&generation);
	pending_diag.is_error = false;
	pending_diag.elapsed_us = 0;
	ts.diag_seq = 0;

	k_spin_unlock(&diag_lock, key);

	/* Cancel any pending GRTC compare — disable the CC channel */
	nrfx_grtc_syscounter_cc_disable(grtc_channel);

	ts.anchor_set = false;
	ts.last_compare_us = 0;
	ts.last_cap = 0;
}
