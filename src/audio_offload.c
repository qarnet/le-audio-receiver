/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Audio offload — nRF54L15: FLPR identity loopback transport.
 *
 * Wires decoded PCM through the Stage 1 SPSC rings:
 *   CPUAPP input ring → FLPR identity-copies → CPUAPP output ring
 *
 * Submit is mutex-serialised.  Scratch output buffer is module-static
 * (no stack allocation in BT callback).  CRC-32 is computed on produce
 * and independently recomputed on consume; payload is memcmp'd against
 * original input for bit-exact identity verification.
 *
 * Fault state machine: ANY fault poisons healthy immediately.  Bounded
 * recovery work (k_work_delayable) runs outside BT callback with
 * exponential backoff.  While recovering, submits return -EAGAIN.
 * Stream stop cancels recovery and increments generation so late
 * output from a prior epoch is never accepted.
 *
 * Never waits/sends under spinlock.  No wait in ISR context.
 *
 * nRF5340 bypass: identity no-ops, compiled out.
 */

#include "audio_offload.h"

#include <errno.h>
#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

LOG_MODULE_REGISTER(audio_offload, LOG_LEVEL_INF);

/* ── nRF54L15: FLPR transport ───────────────────────────────────── */

#if defined(CONFIG_SOC_NRF54L15)

#include "flpr_ring.h"
#include "flpr_ring_mgr.h"
#include "flpr_handshake.h"

/* Synchronous wait deadline.
 * Stage 1 measured max RTT ~5.3 ms → 8 ms leaves 2.7 ms margin.
 * Mode B (two LC3 decodes) stays well under 10 ms SDU interval. */
#define OFFLOAD_DEADLINE_MS 8U

/* 480 stereo frames = 960 samples = 1920 bytes. */
#define OFFLOAD_EXPECTED_FRAMES  FLPR_RING_PAYLOAD_MAX_INPUT
#define OFFLOAD_EXPECTED_SAMPLES ((size_t)OFFLOAD_EXPECTED_FRAMES * 2U)
#define OFFLOAD_EXPECTED_BYTES   ((size_t)OFFLOAD_EXPECTED_FRAMES * 4U)

/* Recovery backoff: starts at 100 ms, doubles each attempt, caps at 5 s. */
#define OFFLOAD_RECOVERY_BASE_MS   100U
#define OFFLOAD_RECOVERY_MAX_MS    5000U
#define OFFLOAD_RECOVERY_MAX_TRIES 5U

BUILD_ASSERT(OFFLOAD_DEADLINE_MS > 0 && OFFLOAD_DEADLINE_MS < 10,
	     "deadline must be in range (1..9) ms");
BUILD_ASSERT(OFFLOAD_EXPECTED_BYTES <= 2048, "scratch output buffer size");

/* ── Module-static scratch output ──────────────────────────────────
 *
 * Serialised by submit_lock — only one block in-flight.
 * No stack allocation in BT callback path.
 * 32-byte aligned for cache-line cleanliness on shared memory access. */
static int16_t g_scratch_output[OFFLOAD_EXPECTED_SAMPLES] __attribute__((aligned(32)));

BUILD_ASSERT(sizeof(g_scratch_output) == OFFLOAD_EXPECTED_BYTES, "scratch output size mismatch");

/* ── Internal state ──────────────────────────────────────────────── */

static struct k_spinlock g_lock;

/* Submit serialisation — only one block in-flight at a time.
 * Never wait on mutex from spinlock or ISR. */
K_MUTEX_DEFINE(g_submit_lock);

/* Recovery work — runs in system workqueue, never in BT callback. */
static struct k_work_delayable g_recovery_work;

static struct audio_offload_status g_status;
static bool g_initialized;
static bool g_rings_ready;
static uint32_t g_stream_epoch;
static uint32_t g_generation; /* bumped on start + recovery */
static enum audio_offload_state g_state;
static bool g_healthy; /* shadow: true when ACTIVE */

/* Recovery state (only accessed from recovery work or under lock). */
static uint32_t g_recovery_backoff_ms;
static uint32_t g_recovery_tries;

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

/*
 * Centralised fault accounting.
 * Must be called under g_lock spinlock.
 * Increments fallback_count once, category_counter once.
 * Sets state to RECOVERING, poisons healthy.
 */
static void record_fault(uint32_t *category_counter, int error, uint32_t seq)
{
	g_status.fallback_count++;
	if (category_counter) {
		(*category_counter)++;
	}
	g_status.last_error = error;
	g_status.last_error_seq = seq;
	g_state = AUDIO_OFFLOAD_RECOVERING;
	g_status.state = g_state;
	g_healthy = false;
	g_status.healthy = false;
}

/*
 * Schedule recovery work — call under g_lock spinlock.
 * Cancels any in-flight recovery, reschedules with current backoff.
 */
static void schedule_recovery_locked(void)
{
	(void)k_work_cancel_delayable(&g_recovery_work);
	k_work_schedule(&g_recovery_work, K_MSEC(g_recovery_backoff_ms));
}

/* ── Recovery work ─────────────────────────────────────────────────
 * Runs in system workqueue context — NEVER in BT callback.
 * Confirms FLPR healthy, performs coordinated epoch reset, then
 * re-enables offload.  On failure, schedules retry with backoff. */

static void recovery_work_fn(struct k_work *work)
{
	(void)work;

	k_spinlock_key_t key = k_spin_lock(&g_lock);

	/* State may have changed while work was queued. */
	if (g_state != AUDIO_OFFLOAD_RECOVERING) {
		k_spin_unlock(&g_lock, key);
		return;
	}

	if (g_recovery_tries >= OFFLOAD_RECOVERY_MAX_TRIES) {
		LOG_ERR("offload recovery: max tries (%u) exhausted, staying in FALLBACK",
			OFFLOAD_RECOVERY_MAX_TRIES);
		g_status.recovery_fail_count++;
		g_state = AUDIO_OFFLOAD_FALLBACK;
		g_status.state = g_state;
		k_spin_unlock(&g_lock, key);
		return;
	}

	k_spin_unlock(&g_lock, key);

	/* ── Step 1: Confirm FLPR is healthy ──────────────────────── */
	if (!check_flpr_healthy()) {
		g_recovery_backoff_ms *= 2U;
		if (g_recovery_backoff_ms > OFFLOAD_RECOVERY_MAX_MS) {
			g_recovery_backoff_ms = OFFLOAD_RECOVERY_MAX_MS;
		}
		LOG_WRN("offload recovery: FLPR not healthy, retry in %u ms",
			g_recovery_backoff_ms);

		key = k_spin_lock(&g_lock);
		if (g_state == AUDIO_OFFLOAD_RECOVERING) {
			g_recovery_tries++;
			schedule_recovery_locked();
		}
		k_spin_unlock(&g_lock, key);
		return;
	}

	/* ── Step 2: Coordinated new epoch reset ──────────────────── */
	uint32_t new_epoch = k_cycle_get_32();
	if (new_epoch == 0) {
		new_epoch = 1;
	}
	new_epoch &= 0x7FFFFFFFU;

	int ret = flpr_ring_mgr_coordinated_reset(new_epoch, 5000);
	if (ret < 0) {
		g_recovery_backoff_ms *= 2U;
		if (g_recovery_backoff_ms > OFFLOAD_RECOVERY_MAX_MS) {
			g_recovery_backoff_ms = OFFLOAD_RECOVERY_MAX_MS;
		}
		LOG_WRN("offload recovery: coordinated reset failed: %d, retry in %u ms", ret,
			g_recovery_backoff_ms);

		key = k_spin_lock(&g_lock);
		if (g_state == AUDIO_OFFLOAD_RECOVERING) {
			g_recovery_tries++;
			schedule_recovery_locked();
		}
		k_spin_unlock(&g_lock, key);
		return;
	}

	/* ── Step 3: Bump generation, re-enable ────────────────────── */
	key = k_spin_lock(&g_lock);

	/* Re-verify state wasn't cancelled while we were doing IPC. */
	if (g_state != AUDIO_OFFLOAD_RECOVERING) {
		k_spin_unlock(&g_lock, key);
		return;
	}

	g_stream_epoch = new_epoch;
	g_generation++;
	g_status.epoch = new_epoch;
	g_status.generation = g_generation;
	g_status.recovery_count++;
	g_status.healthy = true;
	g_healthy = true;

	/* Reset per-stream fault counters on recovery. */
	g_status.timeout_count = 0;
	g_status.full_count = 0;
	g_status.stale_count = 0;
	g_status.seq_fault_count = 0;
	g_status.frame_fault_count = 0;
	g_status.crc_fault_count = 0;
	g_status.payload_fault_count = 0;
	g_status.fallback_count = 0;
	g_status.rtt_min_cycles = 0;
	g_status.rtt_max_cycles = 0;
	g_status.rtt_sum_cycles = 0;
	g_status.rtt_count = 0;
	g_status.last_error = 0;
	g_status.last_error_seq = 0;

	g_state = AUDIO_OFFLOAD_ACTIVE;
	g_status.state = g_state;
	g_recovery_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
	g_recovery_tries = 0;

	k_spin_unlock(&g_lock, key);

	LOG_INF("offload recovery OK: epoch=%u gen=%u", new_epoch, g_generation);
}

/* ── Public API ──────────────────────────────────────────────────── */

int audio_offload_init(void)
{
	if (g_initialized) {
		return 0;
	}

	memset(&g_status, 0, sizeof(g_status));

	k_work_init_delayable(&g_recovery_work, recovery_work_fn);

	/* Try to init rings early.  FLPR may not be ready yet —
	 * that's fine, stream_start will retry. */
	int ret = flpr_ring_mgr_init();
	if (ret == 0) {
		g_rings_ready = true;
		LOG_INF("offload init OK (rings ready)");
	} else {
		LOG_INF("offload init OK (rings deferred, FLPR not yet ready: %d)", ret);
	}

	g_state = AUDIO_OFFLOAD_STOPPED;
	g_status.state = g_state;
	g_status.healthy = false;
	g_healthy = false;
	g_status.initialized = true;
	g_initialized = true;
	g_stream_epoch = 0;
	g_generation = 0;

	return 0;
}

void audio_offload_stream_start(void)
{
	if (!g_initialized) {
		LOG_WRN("stream_start: offload not initialized");
		return;
	}

	/* Cancel any in-flight recovery. */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		(void)k_work_cancel_delayable(&g_recovery_work);
		g_state = AUDIO_OFFLOAD_PREPARING;
		g_status.state = g_state;
		g_status.healthy = false;
		g_healthy = false;
		k_spin_unlock(&g_lock, key);
	}

	/* Retry ring init if deferred (FLPR was not ready at boot). */
	if (!g_rings_ready) {
		int ret = flpr_ring_mgr_init();
		if (ret == 0) {
			g_rings_ready = true;
			LOG_INF("offload rings initialized (retry OK)");
		} else {
			LOG_WRN("offload rings still not ready: %d — bypassing", ret);
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			g_state = AUDIO_OFFLOAD_FALLBACK;
			g_status.state = g_state;
			k_spin_unlock(&g_lock, key);
			return;
		}
	}

	/* Generate new epoch and perform coordinated reset.
	 * This blocks up to 5 s — but stream_start is called
	 * from the enable/start callback, NOT the ISO receive
	 * callback.  The BT spec allows up to several seconds
	 * for stream setup. */
	uint32_t epoch = k_cycle_get_32();
	if (epoch == 0) {
		epoch = 1;
	}
	epoch &= 0x7FFFFFFFU;

	int ret = flpr_ring_mgr_coordinated_reset(epoch, 5000);
	if (ret < 0) {
		LOG_WRN("coordinated reset failed: %d — falling back", ret);
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		g_state = AUDIO_OFFLOAD_FALLBACK;
		g_status.state = g_state;
		k_spin_unlock(&g_lock, key);
		return;
	}

	/* ── Transition to ACTIVE ─────────────────────────────────── */
	k_spinlock_key_t key = k_spin_lock(&g_lock);

	g_stream_epoch = epoch;
	g_generation++;
	g_status.epoch = epoch;
	g_status.generation = g_generation;
	g_status.healthy = true;
	g_healthy = true;

	/* Reset per-stream fault counters (NOT lifetime counters). */
	g_status.submit_count = 0;
	g_status.success_count = 0;
	g_status.timeout_count = 0;
	g_status.full_count = 0;
	g_status.stale_count = 0;
	g_status.seq_fault_count = 0;
	g_status.frame_fault_count = 0;
	g_status.crc_fault_count = 0;
	g_status.payload_fault_count = 0;
	g_status.fallback_count = 0;
	/* Keep: recovery_count, recovery_fail_count (lifetime) */
	g_status.rtt_min_cycles = 0;
	g_status.rtt_max_cycles = 0;
	g_status.rtt_sum_cycles = 0;
	g_status.rtt_count = 0;
	g_status.last_error = 0;
	g_status.last_error_seq = 0;

	g_state = AUDIO_OFFLOAD_ACTIVE;
	g_status.state = g_state;
	g_recovery_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
	g_recovery_tries = 0;

	k_spin_unlock(&g_lock, key);

	LOG_INF("offload stream start: epoch=%u gen=%u state=ACTIVE", epoch, g_generation);
}

void audio_offload_stream_stop(void)
{
	if (!g_initialized) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&g_lock);

	/* Cancel recovery work if pending. */
	(void)k_work_cancel_delayable(&g_recovery_work);

	/* Increment generation so any late output from prior epoch
	 * is rejected by the generation check in submit. */
	g_generation++;
	g_status.generation = g_generation;

	g_state = AUDIO_OFFLOAD_STOPPED;
	g_status.state = g_state;
	g_status.healthy = false;
	g_healthy = false;
	g_stream_epoch = 0;
	g_status.epoch = 0;

	k_spin_unlock(&g_lock, key);

	LOG_INF("offload stream stop: gen=%u", g_generation);
}

bool audio_offload_is_healthy(void)
{
	if (!g_initialized) {
		return false;
	}
	return g_state == AUDIO_OFFLOAD_ACTIVE;
}

int audio_offload_submit(const int16_t *input, size_t samples, uint32_t sequence,
			 int32_t correction_ppm, int16_t *output)
{
	/* ── Validate args BEFORE any state/counter access ────── */
	if (!input || !output || samples == 0) {
		return -EINVAL;
	}

	if (samples != OFFLOAD_EXPECTED_SAMPLES) {
		LOG_WRN("offload: bad sample count %zu (expect %u)", samples,
			(unsigned)OFFLOAD_EXPECTED_SAMPLES);
		return -EINVAL;
	}

	/* ── Quick pre-check under spinlock ──────────────────────── */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (!g_initialized || g_state == AUDIO_OFFLOAD_STOPPED) {
			k_spin_unlock(&g_lock, key);
			return -EAGAIN;
		}

		if (g_state != AUDIO_OFFLOAD_ACTIVE) {
			/* PREPARING, FALLBACK, RECOVERING → caller uses ASRC. */
			g_status.fallback_count++;
			k_spin_unlock(&g_lock, key);
			return -EAGAIN;
		}

		/* Increment submit_count only after ALL validations pass. */
		g_status.submit_count++;
		k_spin_unlock(&g_lock, key);
	}

	/* ── Serialise submit — one block in-flight at a time ─────── */
	if (k_mutex_lock(&g_submit_lock, K_MSEC(OFFLOAD_DEADLINE_MS)) != 0) {
		/* Mutex timeout — another submit is stuck.  Don't block
		 * the BT callback; fallback immediately. */
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		record_fault(NULL, -EBUSY, sequence);
		schedule_recovery_locked();
		k_spin_unlock(&g_lock, key);
		return -EAGAIN;
	}

	/* ── Re-check state under mutex (race: state change between
	 *     spinlock release and mutex acquire) ─────────────────── */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (g_state != AUDIO_OFFLOAD_ACTIVE) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		k_spin_unlock(&g_lock, key);
	}

	/* ── Produce: write input PCM to input ring WITH CRC ──────── */
	enum flpr_produce_result pr =
		flpr_ring_mgr_produce_block((const uint8_t *)input, OFFLOAD_EXPECTED_FRAMES,
					    sequence, correction_ppm, true /* compute_crc */);

	if (pr == FLPR_PRODUCE_FULL) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		record_fault(&g_status.full_count, -ENOSPC, sequence);
		/* Ring full without prior inflight = poisoned ring
		 * (synchronous design permits only one in-flight). */
		schedule_recovery_locked();
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		return -EAGAIN;
	}
	if (pr != FLPR_PRODUCE_OK) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		record_fault(NULL, -EIO, sequence);
		schedule_recovery_locked();
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		return -EAGAIN;
	}

	/* ── Notify FLPR ──────────────────────────────────────────── */
	{
		int notify_ret = flpr_ring_mgr_notify_producer();
		if (notify_ret < 0) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			record_fault(NULL, notify_ret, sequence);
			/* Notify failed → FLPR may still process, but we
			 * can't proceed safely.  Poison ring. */
			schedule_recovery_locked();
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
	}

	/* ── Wait for FLPR to produce output ──────────────────────── */
	{
		int wait_ret = flpr_ring_mgr_wait_consume(OFFLOAD_DEADLINE_MS);
		if (wait_ret != 0) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			record_fault(&g_status.timeout_count, -ETIMEDOUT, sequence);
			schedule_recovery_locked();
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
	}

	/* ── Consume: read FLPR output into scratch buffer ─────────── */
	uint16_t vf = 0;
	uint32_t out_seq = 0;
	uint32_t crc_metadata = 0;
	uint32_t latency_cycles = 0;

	enum flpr_consume_result cr = flpr_ring_mgr_consume_block(
		(uint8_t *)g_scratch_output, &vf, &out_seq, &crc_metadata, &latency_cycles);

	if (cr == FLPR_CONSUME_EMPTY) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		record_fault(NULL, -ENOENT, sequence);
		schedule_recovery_locked();
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		return -EAGAIN;
	}
	if (cr == FLPR_CONSUME_STALE) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		record_fault(&g_status.stale_count, -ESTALE, sequence);
		schedule_recovery_locked();
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		return -EAGAIN;
	}
	if (cr != FLPR_CONSUME_OK) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		record_fault(NULL, -EIO, sequence);
		schedule_recovery_locked();
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		return -EAGAIN;
	}

	/* ── Validate output identity ────────────────────────────────
	 * All checks happen BEFORE output is touched.
	 * Any fault here → output buffer stays UNTOUCHED. */

	/* Frame count check. */
	if (vf != OFFLOAD_EXPECTED_FRAMES) {
		LOG_WRN("offload: wrong frames: got %u expect %u", vf, OFFLOAD_EXPECTED_FRAMES);
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		record_fault(&g_status.frame_fault_count, -EFAULT, sequence);
		schedule_recovery_locked();
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		return -EAGAIN;
	}

	/* Sequence check. */
	if (out_seq != sequence) {
		LOG_WRN("offload: seq mismatch: got %u expect %u", out_seq, sequence);
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		record_fault(&g_status.seq_fault_count, -EFAULT, sequence);
		schedule_recovery_locked();
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		return -EAGAIN;
	}

	/* CRC check: recompute over received payload, compare to metadata. */
	{
		uint32_t computed_crc =
			flpr_ring_crc32((const uint8_t *)g_scratch_output, OFFLOAD_EXPECTED_BYTES);
		if (computed_crc != crc_metadata) {
			LOG_WRN("offload: CRC mismatch: comp=0x%08x meta=0x%08x", computed_crc,
				crc_metadata);
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			record_fault(&g_status.crc_fault_count, -EFAULT, sequence);
			schedule_recovery_locked();
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
	}

	/* Payload identity check: memcmp output against original input. */
	{
		int cmp = memcmp(g_scratch_output, input, OFFLOAD_EXPECTED_BYTES);
		if (cmp != 0) {
			/* Count mismatched frames (one 4-byte stereo frame at a time). */
			const int16_t *scr = g_scratch_output;
			const int16_t *inp = input;
			uint32_t bad_frames = 0;
			for (size_t i = 0; i < OFFLOAD_EXPECTED_FRAMES; i++) {
				if (scr[2 * i] != inp[2 * i] || scr[2 * i + 1] != inp[2 * i + 1]) {
					bad_frames++;
				}
			}
			LOG_WRN("offload: payload mismatch: %u/%u frames bad", bad_frames,
				OFFLOAD_EXPECTED_FRAMES);
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			record_fault(&g_status.payload_fault_count, -EFAULT, sequence);
			schedule_recovery_locked();
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
	}

	/* ── All checks passed — copy output, record latency ───────── */
	memcpy(output, g_scratch_output, OFFLOAD_EXPECTED_BYTES);
	record_latency(latency_cycles);

	k_spinlock_key_t key = k_spin_lock(&g_lock);
	g_status.success_count++;
	g_status.last_error = 0;
	k_spin_unlock(&g_lock, key);

	k_mutex_unlock(&g_submit_lock);
	return 0;
}

void audio_offload_get_status(struct audio_offload_status *status)
{
	if (!status) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&g_lock);
	memcpy(status, &g_status, sizeof(*status));
	status->initialized = g_initialized;
	status->healthy = (g_state == AUDIO_OFFLOAD_ACTIVE);
	status->epoch = g_stream_epoch;
	status->generation = g_generation;
	status->state = g_state;
	k_spin_unlock(&g_lock, key);
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
	return true;
}

int audio_offload_submit(const int16_t *input, size_t samples, uint32_t sequence,
			 int32_t correction_ppm, int16_t *output)
{
	(void)sequence;
	(void)correction_ppm;

	if (!input || !output || samples == 0) {
		return -EINVAL;
	}

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
		status->state = AUDIO_OFFLOAD_ACTIVE;
	}
}

#endif /* CONFIG_SOC_NRF54L15 */
