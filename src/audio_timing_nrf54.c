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
 * No direct RADIO access.  All hardware resources allocated through
 * nrfx APIs.  SDC/MPSL owns the RADIO peripheral.
 */

#include "audio_timing.h"

#include <nrfx_grtc.h>
#include <nrfx_timer.h>
#include <nrfx_i2s.h>
#include <helpers/nrfx_gppi.h>
#include <hal/nrf_grtc.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(audio_timing, LOG_LEVEL_INF);

/* ── Hardware identities ─────────────────────────────────────────── */
#define TIMER20_INST NRF_TIMER_INST_GET(20)

/* I2S20 peripheral (nRF54L15 cpuapp) */
#define I2S20_PERIPH NRF_I2S20

/* ── Diagnostic pacing ────────────────────────────────────────────── */
#define DIAG_PERIOD_S    5U /* log at most every 5 seconds */
#define DIAG_FIRST_COUNT 1  /* always log first measurement */

/* ── Static hardware resources ────────────────────────────────────── */
static nrfx_timer_t timer_inst = NRFX_TIMER_INSTANCE(TIMER20_INST);
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
	uint32_t diag_count;      /* number of compare events processed */
};

static struct timing_state ts;

/* Work item for deferred logging (ISR must not log) */
static struct k_work diag_work;

/* Saved diagnostics for the work handler */
struct diag_payload {
	uint32_t frame_delta;
	uint32_t elapsed_us;
	uint32_t sample_rate_hz;
};

static struct diag_payload pending_diag;

/* ── Pure helpers (unit-testable) ─────────────────────────────────── */

/**
 * Expand a 32-bit ISO timestamp to a full 64-bit GRTC time,
 * following Nordic iso_time_sync wrap logic.
 *
 * @param ts_us    32-bit ISO timestamp in microseconds.
 * @param now_us   Current 64-bit GRTC system counter value.
 * @return         Expanded 64-bit future GRTC time.
 */
static uint64_t iso_ts_to_grtc64(uint32_t ts_us, uint64_t now_us)
{
	uint64_t upper = now_us & 0xFFFFFFFF00000000ULL;
	uint64_t full = upper | ts_us;

	if (ts_us < (now_us & UINT32_MAX)) {
		/* Timestamp is after UINT32 wrap */
		full += 0x100000000ULL;
	}

	return full;
}

/**
 * Unsigned 32-bit counter delta (handles wrap).
 */
static uint32_t counter_delta_u32(uint32_t current, uint32_t previous)
{
	return current - previous; /* unsigned arithmetic handles wrap */
}

/**
 * Integer ppm calculation: (measured - nominal) * 1e6 / nominal.
 *
 * @param measured         Measured frame count in the interval.
 * @param nominal_expected Nominal frame count for the given elapsed time.
 * @return                 Integer ppm (positive = LRCK faster than nominal).
 */
static int32_t compute_ppm(uint32_t measured, uint32_t nominal_expected)
{
	if (nominal_expected == 0) {
		return 0;
	}

	/* Use int64 to avoid overflow in intermediate */
	int64_t diff = (int64_t)measured - (int64_t)nominal_expected;

	return (int32_t)((diff * 1000000LL) / (int64_t)nominal_expected);
}

/* ── Work handler (deferred from ISR) ─────────────────────────────── */

static void diag_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int32_t ppm;
	uint32_t nominal;

	if (pending_diag.elapsed_us == 0) {
		return;
	}

	/* Nominal frames in this interval */
	nominal = (uint32_t)(((uint64_t)pending_diag.elapsed_us * pending_diag.sample_rate_hz) /
			     1000000ULL);

	ppm = compute_ppm(pending_diag.frame_delta, nominal);

	LOG_INF("LRCK diag[%" PRIu32 "]: %" PRIu32 " frames in %" PRIu32 " us (nom %" PRIu32
		") → %" PRId32 " ppm",
		ts.diag_count, pending_diag.frame_delta, pending_diag.elapsed_us, nominal, ppm);
}

/* ── GRTC compare callback (ISR context) ──────────────────────────── */

static void grtc_cc_handler(int32_t id, uint64_t cc_value, void *p_context)
{
	ARG_UNUSED(id);
	ARG_UNUSED(p_context);

	/* Read captured TIMER20 count — hardware snapshotted at
	 * the compare instant via GPPI, so this value is jitter-free.
	 */
	uint32_t cap = nrfx_timer_capture_get(&timer_inst, NRF_TIMER_CC_CHANNEL0);

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
	(void)nrfx_grtc_syscounter_cc_absolute_set(&chan_data, next_cmp, true);

	/* Accumulate diagnostics */
	if (ts.last_compare_us != 0) {
		uint32_t frame_delta = counter_delta_u32(cap, ts.last_cap);
		uint32_t elapsed_us = (uint32_t)(cc_value - ts.last_compare_us);

		ts.diag_count++;

		/* Log first measurement, then every DIAG_PERIOD_S */
		if (ts.diag_count == DIAG_FIRST_COUNT || (ts.diag_count % DIAG_PERIOD_S) == 0) {
			pending_diag.frame_delta = frame_delta;
			pending_diag.elapsed_us = elapsed_us;
			pending_diag.sample_rate_hz =
				(uint32_t)CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ;
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

	/* --- TIMER20: 32-bit COUNTER mode --- */
	const nrfx_timer_config_t tcfg = {
		.frequency = NRFX_MHZ_TO_HZ(1UL),
		.mode = NRF_TIMER_MODE_COUNTER,
		.bit_width = NRF_TIMER_BIT_WIDTH_32,
		.interrupt_priority = NRFX_TIMER_DEFAULT_CONFIG_IRQ_PRIORITY,
		.p_context = NULL,
	};

	ret = nrfx_timer_init(&timer_inst, &tcfg, NULL);
	if (ret < 0) {
		LOG_ERR("TIMER20 init failed: %d", ret);
		nrfx_grtc_channel_free(grtc_channel);
		return ret;
	}

	nrfx_timer_enable(&timer_inst);

	/* --- GPPI: I2S20 FRAMESTART → TIMER20 COUNT --- */
	uint32_t fs_evt = nrf_i2s_event_address_get(I2S20_PERIPH, NRF_I2S_EVENT_FRAMESTART);
	uint32_t cnt_tsk = nrfx_timer_task_address_get(&timer_inst, NRF_TIMER_TASK_COUNT);

	ret = nrfx_gppi_conn_alloc(fs_evt, cnt_tsk, &gppi_fs_to_count);
	if (ret < 0) {
		LOG_ERR("GPPI FRAMESTART→COUNT alloc failed: %d", ret);
		nrfx_timer_uninit(&timer_inst);
		nrfx_grtc_channel_free(grtc_channel);
		return ret;
	}
	nrfx_gppi_conn_enable(gppi_fs_to_count);

	/* --- GPPI: GRTC COMPARE → TIMER20 CAPTURE[0] --- */
	uint32_t grtc_evt = nrf_grtc_event_address_get(
		NRF_GRTC, nrf_grtc_sys_counter_compare_event_get(grtc_channel));
	uint32_t cap_tsk = nrfx_timer_capture_task_address_get(&timer_inst, NRF_TIMER_CC_CHANNEL0);

	ret = nrfx_gppi_conn_alloc(grtc_evt, cap_tsk, &gppi_grtc_to_cap);
	if (ret < 0) {
		LOG_ERR("GPPI GRTC→CAPTURE alloc failed: %d", ret);
		nrfx_gppi_conn_free(fs_evt, cnt_tsk, gppi_fs_to_count);
		nrfx_timer_uninit(&timer_inst);
		nrfx_grtc_channel_free(grtc_channel);
		return ret;
	}
	nrfx_gppi_conn_enable(gppi_grtc_to_cap);

	/* Work item for deferred logging */
	k_work_init(&diag_work, diag_work_handler);

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
	uint64_t anchor = iso_ts_to_grtc64(sdu_ts_us, now) + pd_us;

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

	ts.anchor_us = anchor;
	ts.anchor_set = true;
	ts.last_compare_us = 0; /* force first delta skip */
	ts.diag_count = 0;

	LOG_INF("Timing anchor: ts=%" PRIu32 " pd=%" PRIu32 " anchor_grtc=%" PRIu64
		" first_cmp=%" PRIu64,
		sdu_ts_us, pd_us, anchor, first_cmp);
}

void audio_timing_reset(void)
{
	if (!ts.init_done) {
		return;
	}

	/* Cancel any pending GRTC compare — disable the CC channel */
	nrfx_grtc_syscounter_cc_disable(grtc_channel);

	ts.anchor_set = false;
	ts.last_compare_us = 0;
	ts.last_cap = 0;
	ts.diag_count = 0;
}
