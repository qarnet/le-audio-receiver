/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * nRF54L15 audio timing measurement: hardware-timed I2S LRCK frame-clock
 * progress against Bluetooth controller / GRTC time.
 *
 * Design (Phase 4b.1):
 *  1. I2S20 FRAMESTART → GPPI → TIMER20 COUNT (counts every LRCK edge).
 *  2. GRTC compare at 1 s intervals → GPPI → TIMER20 CAPTURE[0]
 *     (hardware snapshots frame count; zero callback latency).
 *  3. GRTC ISR reads captured count, computes unsigned delta and
 *     elapsed GRTC microseconds, then logs bounded diagnostics via
 *     a work item.
 *
 * No direct RADIO access.  SDC/MPSL owns the RADIO peripheral.
 *
 * TIMER20 register base derived from devicetree (&timer20), not a
 * raw address.  HAL functions (nrf_timer_*) are used instead of
 * nrfx_timer so no CONFIG_NRFX_TIMER is required.
 */

#include "audio_timing.h"
#include "audio_timing_math.h"

#include <nrfx_grtc.h>
#include <nrfx_i2s.h>
#include <helpers/nrfx_gppi.h>
#include <hal/nrf_grtc.h>
#include <hal/nrf_timer.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(audio_timing, LOG_LEVEL_INF);

/* ── Hardware identities ─────────────────────────────────────────── */

/* Derive TIMER20 register base from devicetree.
 * On nRF54L15 cpuapp: &timer20 { reg = <0xca000 0x1000>; }
 * resolved to absolute address by DT_REG_ADDR.
 */
#define TIMER20_NODE DT_NODELABEL(timer20)
#define TIMER20_BASE DT_REG_ADDR(TIMER20_NODE)

/* I2S20 peripheral (nRF54L15 cpuapp) */
#define I2S20_PERIPH NRF_I2S20

/* ── Diagnostic pacing ────────────────────────────────────────────── */
#define DIAG_PERIOD_S  5U /* log at most every 5 seconds */
#define DIAG_FIRST_SEQ 1  /* always log first measurement */

/* ── Static hardware resources ────────────────────────────────────── */
static NRF_TIMER_Type *const timer_reg = (NRF_TIMER_Type *)TIMER20_BASE;
static uint8_t grtc_channel;                /* allocated GRTC CC channel */
static nrfx_gppi_handle_t gppi_fs_to_count; /* FRAMESTART → COUNT */
static nrfx_gppi_handle_t gppi_grtc_to_cap; /* GRTC COMPARE → CAPTURE */

/* ── Timing state ─────────────────────────────────────────────────── */
struct timing_state {
	bool init_done;           /* init() succeeded */
	bool anchor_set;          /* first valid SDU has arrived */
	uint64_t anchor_us;       /* GRTC anchor time (= ts + pd, converted) */
	uint32_t last_cap;        /* previous TIMER20 capture value */
	uint64_t last_compare_us; /* GRTC time of last compare (cc_value) */
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

/* Saved diagnostics for the work handler */
struct diag_payload {
	uint32_t frame_delta;
	uint32_t elapsed_us;
	uint32_t sample_rate_hz;
	uint32_t seq;     /* diagnostic sequence number */
	atomic_val_t gen; /* generation at capture time */
};

static struct diag_payload pending_diag;

/* ── Work handler (deferred from ISR) ─────────────────────────────── */

static void diag_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Reject stale payload from a previous session */
	if (pending_diag.gen != atomic_get(&generation)) {
		return;
	}

	if (pending_diag.elapsed_us == 0) {
		return;
	}

	/* Nominal frames in this interval */
	uint32_t nominal =
		(uint32_t)(((uint64_t)pending_diag.elapsed_us * pending_diag.sample_rate_hz) /
			   1000000ULL);

	int32_t ppm = audio_timing_compute_ppm(pending_diag.frame_delta, nominal);

	LOG_INF("LRCK diag[%" PRIu32 "]: %" PRIu32 " frames in %" PRIu32 " us (nom %" PRIu32
		") → %" PRId32 " ppm",
		pending_diag.seq, pending_diag.frame_delta, pending_diag.elapsed_us, nominal, ppm);
}

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
		 */
		atomic_set(&active, false);
		pending_diag.elapsed_us = 0; /* signal error path */
		k_work_submit(&diag_work);
		LOG_ERR("GRTC compare schedule failed: %d", ret);
		return;
	}

	/* Accumulate diagnostics */
	if (ts.last_compare_us != 0) {
		uint32_t frame_delta = audio_timing_counter_delta_u32(cap, ts.last_cap);
		uint32_t elapsed_us = (uint32_t)(cc_value - ts.last_compare_us);

		/* Carry sequence number and generation in payload so
		 * work handler can reject stale data from old sessions.
		 */
		static uint32_t diag_seq;

		diag_seq++;

		/* Log first measurement, then every DIAG_PERIOD_S */
		if (diag_seq == DIAG_FIRST_SEQ || (diag_seq % DIAG_PERIOD_S) == 0) {
			pending_diag.frame_delta = frame_delta;
			pending_diag.elapsed_us = elapsed_us;
			pending_diag.sample_rate_hz =
				(uint32_t)CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ;
			pending_diag.seq = diag_seq;
			pending_diag.gen = atomic_get(&generation);
			k_work_submit(&diag_work);
		}
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

	/* --- TIMER20: 32-bit COUNTER mode (HAL, no nrfx_timer) --- */
	nrf_timer_mode_set(timer_reg, NRF_TIMER_MODE_COUNTER);
	nrf_timer_bit_width_set(timer_reg, NRF_TIMER_BIT_WIDTH_32);
	nrf_timer_task_trigger(timer_reg, NRF_TIMER_TASK_CLEAR);
	nrf_timer_task_trigger(timer_reg, NRF_TIMER_TASK_START);

	/* --- GPPI: I2S20 FRAMESTART → TIMER20 COUNT --- */
	uint32_t fs_evt = nrf_i2s_event_address_get(I2S20_PERIPH, NRF_I2S_EVENT_FRAMESTART);
	uint32_t cnt_tsk = nrf_timer_task_address_get(timer_reg, NRF_TIMER_TASK_COUNT);

	ret = nrfx_gppi_conn_alloc(fs_evt, cnt_tsk, &gppi_fs_to_count);
	if (ret < 0) {
		LOG_ERR("GPPI FRAMESTART→COUNT alloc failed: %d", ret);
		nrfx_grtc_channel_free(grtc_channel);
		return ret;
	}
	nrfx_gppi_conn_enable(gppi_fs_to_count);

	/* --- GPPI: GRTC COMPARE → TIMER20 CAPTURE[0] --- */
	uint32_t grtc_evt = nrf_grtc_event_address_get(
		NRF_GRTC, nrf_grtc_sys_counter_compare_event_get(grtc_channel));
	uint32_t cap_tsk = nrf_timer_task_address_get(
		timer_reg, nrf_timer_capture_task_get(NRF_TIMER_CC_CHANNEL0));

	ret = nrfx_gppi_conn_alloc(grtc_evt, cap_tsk, &gppi_grtc_to_cap);
	if (ret < 0) {
		LOG_ERR("GPPI GRTC→CAPTURE alloc failed: %d", ret);
		nrfx_gppi_conn_free(fs_evt, cnt_tsk, gppi_fs_to_count);
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

	LOG_INF("Audio timing: GRTC+TIMER20+GPPI ready");
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

	/* First valid SDU with TS: anchor and schedule first compare */
	uint64_t now = nrfx_grtc_syscounter_get();
	uint64_t anchor = audio_timing_iso_ts_to_grtc64(sdu_ts_us, now) + pd_us;

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

	LOG_INF("Timing anchor: ts=%" PRIu32 " pd=%" PRIu32 " anchor_grtc=%" PRIu64
		" first_cmp=%" PRIu64,
		sdu_ts_us, pd_us, anchor, first_cmp);
}

void audio_timing_reset(void)
{
	if (!ts.init_done) {
		return;
	}

	/* Mark inactive BEFORE disabling hardware so the ISR
	 * and work handler can reject in-flight events.
	 */
	atomic_set(&active, false);

	/* Bump generation so any already-queued work payload
	 * from this session is rejected by diag_work_handler.
	 */
	atomic_inc(&generation);

	/* Cancel any pending GRTC compare — disable the CC channel */
	nrfx_grtc_syscounter_cc_disable(grtc_channel);

	ts.anchor_set = false;
	ts.last_compare_us = 0;
	ts.last_cap = 0;
}
