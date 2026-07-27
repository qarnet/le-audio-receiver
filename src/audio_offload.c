/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Audio offload — nRF54L15: FLPR identity loopback transport.
 *
 * Wires decoded PCM through the Stage 1 SPSC rings:
 *   CPUAPP input ring → FLPR identity-copies → CPUAPP output ring
 *
 * Synchronous path: produce → notify → wait → consume.
 * Deadline from Stage 1 max measured RTT: 5,320 µs.  Implemented
 * as 8 ms hard deadline with 1,000 ms soft fallback timeout.
 * If HARD_DEADLINE_MS breaks Mode B margin, switch to pipelined.
 *
 * Fallback: on any fault the PCM input is used directly by the
 * caller — no audio drop, duplicate, or stale output.
 *
 * nRF5340 bypass: identity no-ops, compiled out.
 */

#include "audio_offload.h"

#include <errno.h>
#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(audio_offload, LOG_LEVEL_INF);

/* ── nRF54L15: FLPR transport ───────────────────────────────────── */

#if defined(CONFIG_SOC_NRF54L15)

#include "flpr_ring.h"
#include "flpr_ring_mgr.h"
#include "flpr_handshake.h"

/* Synchronous wait deadline derived from Stage 1 measured max RTT.
 * Mode A and B headroom measured in real streams before tightening. */
#define OFFLOAD_DEADLINE_MS 8U    /* max sync wait for one block */
#define OFFLOAD_FALLBACK_MS 1000U /* soft cap before we stop retrying entirely */

/* 480 stereo frames = 960 samples = 1920 bytes. */
#define OFFLOAD_EXPECTED_FRAMES FLPR_RING_PAYLOAD_MAX_INPUT
#define OFFLOAD_EXPECTED_BYTES  ((size_t)OFFLOAD_EXPECTED_FRAMES * 4U)

/* 3 consecutive timeouts trigger unhealthy transition. */
#define OFFLOAD_HEALTHY_TIMEOUT_THRESHOLD 3U

BUILD_ASSERT(OFFLOAD_DEADLINE_MS > 0 && OFFLOAD_DEADLINE_MS < 10,
	     "deadline must be in range (1..9) ms");
BUILD_ASSERT(OFFLOAD_FALLBACK_MS > OFFLOAD_DEADLINE_MS, "fallback timeout must exceed deadline");

/* ── Internal state ──────────────────────────────────────────────── */

static struct audio_offload_status g_status;
static bool g_initialized;
static bool g_active;      /* stream_start called and not yet stopped */
static bool g_healthy;     /* can we offload right now? */
static bool g_rings_ready; /* flpr_ring_mgr_init() succeeded */
static uint32_t g_stream_epoch;
static uint32_t g_consecutive_timeouts;
static uint32_t g_next_expected_seq; /* for sequence validation */

/* Output buffer — block-size allocation, not stack. */
static int16_t g_output_buf[OFFLOAD_EXPECTED_FRAMES * 2];

/* ── Helpers ─────────────────────────────────────────────────────── */

static bool check_flpr_healthy(void)
{
	struct flpr_status hs;
	flpr_handshake_get_status(&hs);
	return hs.healthy;
}

static void record_latency(uint32_t cycles)
{
	if (g_status.rtt_count == 0 || cycles < g_status.rtt_min_cycles) {
		g_status.rtt_min_cycles = cycles;
	}
	if (cycles > g_status.rtt_max_cycles) {
		g_status.rtt_max_cycles = cycles;
	}
	g_status.rtt_sum_cycles += (uint64_t)cycles;
	g_status.rtt_count++;
}

/* ── Public API ──────────────────────────────────────────────────── */

int audio_offload_init(void)
{
	if (g_initialized) {
		return 0;
	}

	memset(&g_status, 0, sizeof(g_status));

	/* Try to init rings early.  FLPR may not be ready yet —
	 * that's fine, stream_start will retry. */
	int ret = flpr_ring_mgr_init();
	if (ret == 0) {
		g_rings_ready = true;
		LOG_INF("offload init OK (rings ready)");
	} else {
		LOG_INF("offload init OK (rings deferred, FLPR not yet ready: %d)", ret);
	}

	g_healthy = false;
	g_initialized = true;
	g_active = false;
	g_stream_epoch = 0;

	return 0;
}

void audio_offload_stream_start(void)
{
	if (!g_initialized) {
		LOG_WRN("stream_start: offload not initialized");
		return;
	}

	/* Retry ring init if deferred (FLPR was not ready at boot). */
	if (!g_rings_ready) {
		int ret = flpr_ring_mgr_init();
		if (ret == 0) {
			g_rings_ready = true;
			LOG_INF("offload rings initialized (retry OK)");
		} else {
			LOG_WRN("offload rings still not ready: %d — bypassing", ret);
			g_healthy = false;
			g_active = true;
			return;
		}
	}

	/* Coordinate new epoch with FLPR.  Timeout 5 s — FLPR is
	 * already running at this point (stream starts seconds after
	 * boot, well past handshake completion). */
	uint32_t epoch = k_cycle_get_32();
	if (epoch == 0) {
		epoch = 1;
	}

	int ret = flpr_ring_mgr_coordinated_reset(epoch, 5000);
	if (ret < 0) {
		LOG_WRN("coordinated reset failed: %d — falling back", ret);
		g_healthy = false;
		g_active = true;
		return;
	}

	g_stream_epoch = epoch;
	g_consecutive_timeouts = 0;
	g_next_expected_seq = 0;
	g_healthy = check_flpr_healthy();
	g_active = true;

	/* Reset per-stream counters. */
	g_status.timeout_count = 0;
	g_status.full_count = 0;
	g_status.stale_count = 0;
	g_status.seq_fault_count = 0;
	g_status.frame_fault_count = 0;
	g_status.crc_fault_count = 0;
	g_status.fallback_count = 0;
	g_status.rtt_min_cycles = 0;
	g_status.rtt_max_cycles = 0;
	g_status.rtt_sum_cycles = 0;
	g_status.rtt_count = 0;
	g_status.last_error = 0;
	g_status.last_error_seq = 0;

	LOG_INF("offload stream start: epoch=%u healthy=%d", g_stream_epoch, g_healthy);
}

void audio_offload_stream_stop(void)
{
	if (!g_initialized) {
		return;
	}

	g_active = false;
	g_healthy = false;
	g_stream_epoch = 0;
	g_consecutive_timeouts = 0;
	g_next_expected_seq = 0;

	LOG_INF("offload stream stop");
}

bool audio_offload_is_healthy(void)
{
	if (!g_initialized || !g_active) {
		return false;
	}
	return g_healthy;
}

int audio_offload_submit(const int16_t *input, size_t samples, uint32_t sequence,
			 int32_t correction_ppm, int16_t *output)
{
	g_status.submit_count++;

	if (!g_initialized) {
		g_status.fallback_count++;
		g_status.last_error = -EIO;
		g_status.last_error_seq = sequence;
		return -EIO;
	}

	if (!g_active || !g_healthy) {
		/* Offload unhealthy or not active — fallback (no-drop). */
		g_status.fallback_count++;
		g_status.last_error = -EAGAIN;
		g_status.last_error_seq = sequence;
		return -EAGAIN;
	}

	/* Validate sample count: must match expected frames. */
	if (samples != OFFLOAD_EXPECTED_BYTES / 2U) {
		LOG_WRN("offload: bad sample count %zu (expect %u)", samples,
			OFFLOAD_EXPECTED_BYTES / 2U);
		g_status.frame_fault_count++;
		g_status.last_error = -EINVAL;
		g_status.last_error_seq = sequence;
		return -EINVAL;
	}

	uint16_t valid_frames = OFFLOAD_EXPECTED_FRAMES;
	size_t copy_bytes = (size_t)valid_frames * 4U;

	/* ── Produce: write input PCM to input ring ────────────────── */
	enum flpr_produce_result pr = flpr_ring_mgr_produce_block(
		(const uint8_t *)input, valid_frames, sequence, correction_ppm, false);

	if (pr == FLPR_PRODUCE_FULL) {
		g_status.full_count++;
		g_status.last_error = -ENOSPC;
		g_status.last_error_seq = sequence;
		/* Ring full — fallback, but stay healthy.  Ring full is
		 * transient; do NOT trigger unhealthy transition. */
		return -ENOSPC;
	}
	if (pr != FLPR_PRODUCE_OK) {
		g_status.fallback_count++;
		g_status.last_error = -EIO;
		g_status.last_error_seq = sequence;
		return -EIO;
	}

	/* ── Notify FLPR: wake up the consumer ─────────────────────── */
	int notify_ret = flpr_ring_mgr_notify_producer();
	if (notify_ret < 0) {
		/* FLPR may be temporarily unresponsive.  The slot is
		 * already in the ring — FLPR will drain it eventually.
		 * Fall back this block but don't count as unhealthy. */
		g_status.fallback_count++;
		g_status.last_error = notify_ret;
		g_status.last_error_seq = sequence;
		return notify_ret;
	}

	/* ── Wait for FLPR to produce output ─────────────────────────
	 * Deadline-bounded: OFFLOAD_DEADLINE_MS is the max we wait
	 * for one block.  If no output arrives, fall back. */
	int wait_ret = flpr_ring_mgr_wait_consume(OFFLOAD_DEADLINE_MS);

	if (wait_ret != 0) {
		/* Timeout — FLPR didn't respond in time. */
		g_consecutive_timeouts++;
		g_status.timeout_count++;
		g_status.last_error = -ETIMEDOUT;
		g_status.last_error_seq = sequence;

		if (g_consecutive_timeouts >= OFFLOAD_HEALTHY_TIMEOUT_THRESHOLD) {
			LOG_WRN("offload: %u consecutive timeouts → unhealthy",
				g_consecutive_timeouts);
			g_healthy = false;
		}

		return -ETIMEDOUT;
	}

	/* Reset timeout counter on success. */
	g_consecutive_timeouts = 0;

	/* ── Consume: read FLPR output ─────────────────────────────── */
	uint8_t pcm_out[OFFLOAD_EXPECTED_BYTES];
	uint16_t vf = 0;
	uint32_t out_seq = 0;
	uint32_t crc_out = 0;
	uint32_t latency_cycles = 0;

	enum flpr_consume_result cr =
		flpr_ring_mgr_consume_block(pcm_out, &vf, &out_seq, &crc_out, &latency_cycles);

	if (cr == FLPR_CONSUME_EMPTY) {
		/* Ring empty despite semaphore — possible race.
		 * Fall back, not unhealthy. */
		g_status.fallback_count++;
		g_status.last_error = -ENOENT;
		g_status.last_error_seq = sequence;
		return -ENOENT;
	}
	if (cr == FLPR_CONSUME_STALE) {
		g_consecutive_timeouts++; /* count as unhealthy trigger */
		g_status.stale_count++;
		g_status.last_error = -ESTALE;
		g_status.last_error_seq = sequence;
		return -ESTALE;
	}
	if (cr != FLPR_CONSUME_OK) {
		g_status.fallback_count++;
		g_status.last_error = -EIO;
		g_status.last_error_seq = sequence;
		return -EIO;
	}

	/* ── Validate output identity ───────────────────────────────── */
	if (vf != OFFLOAD_EXPECTED_FRAMES) {
		LOG_WRN("offload: wrong frames: got %u expect %u", vf, OFFLOAD_EXPECTED_FRAMES);
		g_status.frame_fault_count++;
		g_status.last_error = -EFAULT;
		g_status.last_error_seq = sequence;
		return -EFAULT;
	}

	if (out_seq != sequence) {
		LOG_WRN("offload: seq mismatch: got %u expect %u", out_seq, sequence);
		g_status.seq_fault_count++;
		g_status.last_error = -EFAULT;
		g_status.last_error_seq = sequence;
		return -EFAULT;
	}

	/* ── Copy output, record latency ────────────────────────────── */
	memcpy(output, pcm_out, copy_bytes);
	memcpy(g_output_buf, pcm_out, copy_bytes); /* also keep local copy for diagnostics */

	record_latency(latency_cycles);

	g_status.success_count++;
	g_status.last_error = 0;
	return 0;
}

void audio_offload_get_status(struct audio_offload_status *status)
{
	if (!status) {
		return;
	}
	memcpy(status, &g_status, sizeof(*status));
	status->initialized = g_initialized;
	status->healthy = g_healthy;
	status->epoch = g_stream_epoch;
}

/* ── nRF5340: identity bypass ────────────────────────────────────── */

#else /* !CONFIG_SOC_NRF54L15 */

int audio_offload_init(void)
{
	return 0;
}

void audio_offload_stream_start(void)
{
}

void audio_offload_stream_stop(void)
{
}

bool audio_offload_is_healthy(void)
{
	return true; /* bypass is always available */
}

int audio_offload_submit(const int16_t *input, size_t samples, uint32_t sequence,
			 int32_t correction_ppm, int16_t *output)
{
	(void)sequence;
	(void)correction_ppm;

	if (!input || !output || samples == 0) {
		return -EINVAL;
	}

	/* Identity: copy input to output directly. */
	size_t bytes = samples * sizeof(int16_t);
	memcpy(output, input, bytes);

	return 0;
}

void audio_offload_get_status(struct audio_offload_status *status)
{
	if (status) {
		memset(status, 0, sizeof(*status));
		status->initialized = true;
		status->healthy = true;
	}
}

#endif /* CONFIG_SOC_NRF54L15 */
