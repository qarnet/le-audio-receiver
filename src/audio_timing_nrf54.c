/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * nRF54L15 audio timing measurement: hardware-timed PCLK clock progress
 * against Bluetooth controller / GRTC time.
 *
 * Design:
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

/* Thread-context control mutex serializing the anchor decision,
 * GRTC first-compare programming, and anchor/active commit in
 * audio_timing_sdu_ref_update() against the inactive/generation
 * transition, compare disable, and anchor/last-state reset in
 * audio_timing_reset().  The GRTC ISR NEVER takes this mutex (single-core
 * nRF54L15: the reset thread cannot run while the ISR itself executes;
 * generation stays the queued-work staleness proof). */
static K_MUTEX_DEFINE(ctl_mutex);

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

/* Deferred diagnostics FIFO.
 * Produced by the GRTC ISR (append) and consumed by the work handler
 * (pop) under diag_lock.  A single work submission may represent many
 * queued payloads: Zephyr may coalesce k_work_submit() calls while the
 * work item is pending/running, so a shared single mailbox could
 * silently lose one-second feedforward measurements when the system
 * workqueue is delayed.  The FIFO guarantees that every non-stale
 * measurement reaches the drift feedforward path.
 */
#define DIAG_FIFO_CAPACITY 16

struct diag_payload {
	uint32_t tick_delta; /* TIMER capture delta (PCLK ticks) */
	uint32_t elapsed_us; /* GRTC elapsed microseconds */
	uint32_t nominal_hz; /* TIMER nominal base frequency */
	uint32_t seq;        /* diagnostic sequence number */
	atomic_val_t gen;    /* generation at capture time */
	bool is_error;       /* true → schedule_err is valid */
	int schedule_err;    /* error code when is_error */
};

static struct diag_payload diag_fifo[DIAG_FIFO_CAPACITY];
static uint8_t diag_fifo_head; /* next payload to pop */
static uint8_t diag_fifo_len;  /* queued payloads */

/* Overflow fault: set by the ISR producer when the FIFO is full.
 * Measurement stops (active cleared) and the work handler reports one
 * LOG_ERR and clears this flag.  Timing evidence is never silently
 * dropped — overflow is an explicit, observable fault transition.
 */
static atomic_t overflow_fault;

static struct k_spinlock diag_lock;

/* Append a payload in FIFO order.  Caller holds diag_lock.
 * @return false when the FIFO is full (payload not stored).
 */
static bool diag_fifo_push(const struct diag_payload *diag)
{
	if (diag_fifo_len >= DIAG_FIFO_CAPACITY) {
		return false;
	}

	uint8_t tail = (uint8_t)((diag_fifo_head + diag_fifo_len) % DIAG_FIFO_CAPACITY);

	diag_fifo[tail] = *diag;
	diag_fifo_len++;
	return true;
}

/* Publish a payload from ISR context: append under diag_lock, then
 * submit the deferred work.  On a full FIFO the producer sets the
 * overflow fault, stops measurement, and still submits work so the
 * fault is reported; the dropped payload belongs to that explicit
 * fault transition, not to normal feedforward loss.  ISR critical
 * section stays bounded (one small copy, no logging).
 */
static void audio_timing_submit_diag_work(void); /* defined with the work item below */

static void diag_publish(const struct diag_payload *diag)
{
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	if (!diag_fifo_push(diag)) {
		atomic_set(&overflow_fault, 1);
		atomic_set(&active, false);
	}

	k_spin_unlock(&diag_lock, key);
	audio_timing_submit_diag_work();
}

/* ── Work handler (deferred from ISR) ─────────────────────────────── */

static void diag_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Drain every queued payload in FIFO order in this one
	 * invocation.  Coalesced submissions must not lose payloads;
	 * the loop terminates because producers stop enqueueing once
	 * the FIFO is full (overflow fault) or the session ends.
	 */
	for (;;) {
		struct diag_payload diag;

		k_spinlock_key_t key = k_spin_lock(&diag_lock);

		if (diag_fifo_len == 0) {
			k_spin_unlock(&diag_lock, key);
			break;
		}

		diag = diag_fifo[diag_fifo_head];
		diag_fifo_head = (uint8_t)((diag_fifo_head + 1) % DIAG_FIFO_CAPACITY);
		diag_fifo_len--;
		k_spin_unlock(&diag_lock, key);

		/* Reject stale payload from a previous session */
		if (diag.gen != atomic_get(&generation)) {
			continue;
		}

		/* Error payload: schedule failure deferred from ISR */
		if (diag.is_error) {
			LOG_ERR("GRTC compare schedule failed: %d (seq %" PRIu32 ")",
				diag.schedule_err, diag.seq);
			continue;
		}

		/* Not an error but no measurement data (initial skip) */
		if (diag.elapsed_us == 0) {
			continue;
		}

		/* Generation is the authoritative staleness guard: reset
		 * bumps it, and queued payloads are never rewritten.  An
		 * inactive flag alone (schedule failure, FIFO overflow)
		 * must not discard already-accepted measurements — the
		 * overflow contract explicitly drains accepted entries
		 * in order.  Later callbacks are stopped at the ISR
		 * entry, not here.
		 */
		/* Nominal ticks expected in this interval at the timer's base
		 * frequency: timer_nominal_hz * elapsed_us / 1,000,000.
		 */
		uint64_t nominal64 = ((uint64_t)diag.nominal_hz * diag.elapsed_us) / 1000000ULL;
		uint32_t nominal = (nominal64 > UINT32_MAX) ? UINT32_MAX : (uint32_t)nominal64;

		int32_t ppm = audio_timing_compute_ppm(diag.tick_delta, nominal);

		/* Every measurement feeds the PCLK frequency
		 * error into the drift controller's feedforward path.
		 * Positive ppm → local PCLK/I2S faster than controller.
		 */
		audio_drift_frequency_error_update(ppm);

		/* Bounded diagnostic logging: sequence 1 and every DIAG_PERIOD_S. */
		if (diag.seq == DIAG_FIRST_SEQ || (diag.seq % DIAG_PERIOD_S) == 0) {
			LOG_INF("PCLK timer diag[%" PRIu32 "]: %" PRIu32 " ticks in %" PRIu32
				" us (nom %" PRIu32 " @ %" PRIu32 " Hz) → %" PRId32 " ppm",
				diag.seq, diag.tick_delta, diag.elapsed_us, nominal,
				diag.nominal_hz, ppm);
		}
	}

	/* Overflow report: one LOG_ERR per fault, cleared after report. */
	if (atomic_get(&overflow_fault)) {
		atomic_set(&overflow_fault, 0);
		LOG_ERR("GRTC diag FIFO overflow: measurement stopped until session reset");
	}
}

#ifdef AUDIO_TIMING_NRF54_TEST
/* GCOVR_EXCL_START — test seams, absent from production builds */
/*
 * Test seams (tests/unit/timing_nrf54):
 *  - audio_timing_submit_diag_work() captures the work item instead of
 *    dispatching it, so tests can run it explicitly;
 *  - audio_timing_test_state_reset() clears file-static module state
 *    between tests without pretending to release hardware;
 *  - audio_timing_test_is_active()/audio_timing_test_generation()
 *    read the minimal state the mocks cannot observe;
 *  - audio_timing_test_overflow_fault() reads the overflow fault flag
 *    (the mocks cannot observe the LOG_ERR that consumes it).
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

bool audio_timing_test_overflow_fault(void)
{
	return atomic_get(&overflow_fault) != 0;
}

void audio_timing_test_state_reset(void)
{
	memset(&ts, 0, sizeof(ts));
	grtc_channel = 0;
	gppi_grtc_to_cap = 0;
	timer_nominal_hz = 0;
	atomic_set(&active, 0);
	atomic_set(&generation, 0);
	atomic_set(&overflow_fault, 0);
	diag_fifo_head = 0;
	diag_fifo_len = 0;
	test_captured_work = NULL;
}
/* GCOVR_EXCL_STOP */
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

	/* Capture the generation exactly once at ISR entry and carry
	 * that captured value in every payload, so a callback accepted
	 * under one session can never be labeled as a later generation. */
	atomic_val_t gen = atomic_get(&generation);

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
		 * defer an error payload via the work handler.
		 * Do NOT log from ISR context.
		 */
		atomic_set(&active, false);

		struct diag_payload diag = {
			.is_error = true,
			.schedule_err = ret,
			.seq = ts.diag_seq,
			.gen = gen,
		};

		diag_publish(&diag);
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

		/* Publish every measurement for the
		 * frequency-error feedforward path.  Diagnostic
		 * logging is gated in the work handler.
		 */
		struct diag_payload diag = {
			.is_error = false,
			.tick_delta = tick_delta,
			.elapsed_us = elapsed_us,
			.nominal_hz = timer_nominal_hz,
			.seq = ts.diag_seq,
			.gen = gen,
		};

		diag_publish(&diag);
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
	k_mutex_lock(&ctl_mutex, K_FOREVER);

	if (!ts.init_done) {
		k_mutex_unlock(&ctl_mutex);
		return;
	}

	if (sdu_ts_us == 0) {
		k_mutex_unlock(&ctl_mutex);
		return;
	}

	if (ts.anchor_set) {
		/* Already anchored — one compare at a time, skip
		 * subsequent SDUs until the anchor changes.
		 */
		k_mutex_unlock(&ctl_mutex);
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
		k_mutex_unlock(&ctl_mutex);
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

	k_mutex_unlock(&ctl_mutex);
}

void audio_timing_reset(void)
{
	k_mutex_lock(&ctl_mutex, K_FOREVER);

	if (!ts.init_done) {
		k_mutex_unlock(&ctl_mutex);
		return;
	}

	/* Mark inactive and bump the generation under the spinlock so
	 * any concurrent ISR sees a consistent state.  Already-queued
	 * payloads are NOT rewritten: deferred work rejects them as
	 * stale by generation, and new-session payloads may follow old
	 * payloads in the FIFO and still deliver.  diag_lock is released
	 * before the nrfx HAL call below.
	 */
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	atomic_set(&active, false);
	atomic_inc(&generation);
	ts.diag_seq = 0;

	k_spin_unlock(&diag_lock, key);

	/* Cancel any pending GRTC compare — disable the CC channel */
	nrfx_grtc_syscounter_cc_disable(grtc_channel);

	ts.anchor_set = false;
	ts.last_compare_us = 0;
	ts.last_cap = 0;

	k_mutex_unlock(&ctl_mutex);
}
