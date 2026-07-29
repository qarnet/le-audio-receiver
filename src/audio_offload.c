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
 * Dedicated offload work queue (own stack via k_work_queue_start) handles
 * all long-running or blocking operations: initial ring preparation and
 * post-fault recovery.  stream_start() only sets PREPARING + bumps
 * generation + schedules prep work, returns immediately.  Recovery
 * runs on same worker.  Neither can stall system workqueue or BT
 * threads.  The work queue thread is created once in audio_offload_init();
 * no K_THREAD_DEFINE wrapper, no duplicate stack/TCS.
 *
 * Concurrency:
 *   - g_lock spinlock protects all shared state
 *   - g_submit_lock mutex serialises submits
 *   - submit captures gen/epoch/state AFTER mutex; rechecks BEFORE
 *     output copy.  Late output after stop/recovery is rejected
 *     untouched, counting stale+fallback.
 *   - record_latency, audio_offload_is_healthy under spinlock
 *   - No k_work_cancel_delayable under spinlock; update state under
 *     lock, unlock, then cancel pending work.
 *   - No kernel schedule under spinlock; compute action under lock,
 *     capture delay as local, then invoke outside lock.
 *   - All backoff/tries read/write under g_lock only.
 *
 * Accounting:
 *   - submit_count: every valid call (inc PREPARING/RECOVERING/FALLBACK)
 *   - fallback_count: every nonzero valid submit increments exactly once
 *   - Invalid args count nothing
 *   - Recovery NEVER clears fault/fallback/RTT evidence
 *   - New stream_start resets per-stream counters
 *   - recovery_attempts, recovery_fail_count, recovery_relapses,
 *     max_exhaustion_count, probation_cleared are lifetime
 *
 * Lifecycle safety (Stage 2 fix #4):
 *   - After ANY blocking/waiting operation, before calling record_fault,
 *     re-check captured state/generation/epoch.  If stop/restart occurred
 *     during the block, count ONE stale+fallback and NEVER change
 *     STOPPED/PREPARING into RECOVERING.
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

#ifdef CONFIG_SOC_NRF54L15

#include "flpr_ring.h"
#include "flpr_ring_mgr.h"
#include "flpr_handshake.h"
#include "flpr_runtime.h"

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

/* Prep retry: separate from recovery backoff, also max 5 tries. */
#define OFFLOAD_PREP_MAX_TRIES 5U

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

/* ── Dedicated offload work queue ──────────────────────────────────
 *
 * Single thread created in audio_offload_init() via k_work_queue_start.
 * No K_THREAD_DEFINE wrapper — saves one stack + TCB.
 * All prep and recovery work is scheduled ONLY on this queue via
 * k_work_schedule_for_queue().  Never use k_work_schedule (system WQ). */

#define OFFLOAD_THREAD_STACK_SIZE 1536
#define OFFLOAD_THREAD_PRIORITY   5

static K_THREAD_STACK_DEFINE(g_offload_stack, OFFLOAD_THREAD_STACK_SIZE);

/* Work queue and items — exposed for unit test access. */
struct k_work_q g_offload_wq;
struct k_work_delayable g_prep_work;
struct k_work_delayable g_recovery_work;

/* ── Internal state ──────────────────────────────────────────────── */

static struct k_spinlock g_lock;

/* Submit serialisation — only one block in-flight at a time.
 * Never wait on mutex from spinlock or ISR. */
K_MUTEX_DEFINE(g_submit_lock);

static struct audio_offload_status g_status;
static bool g_initialized;
static bool g_rings_ready;
static uint32_t g_stream_epoch;
static uint32_t g_generation; /* bumped on start + recovery */
static enum audio_offload_state g_state;

/* Prep recovery state — only accessed under g_lock. */
static uint32_t g_prep_tries;
static uint32_t g_prep_backoff_ms;

/* Recovery state — only accessed under g_lock. */
static uint32_t g_recovery_backoff_ms;
static uint32_t g_recovery_tries;

/* Probation state — prevent recovery storm across cycles.
 * After coordinated reset success → ACTIVE, probation is active.
 * Faults during probation escalate backoff (doubles) and count
 * as relapses.  Only after PROBATION_SUCCESS_THRESHOLD (100)
 * consecutive successful submits do we clear probation + reset
 * tries/backoff to base. */
#define PROBATION_SUCCESS_THRESHOLD 100U

static bool g_probation_active;
static uint32_t g_probation_success;

/* Stage 4B: runtime restart + heartbeat supervisor state */
static uint32_t g_runtime_restart_count;
static uint32_t g_runtime_restart_fail;
static uint32_t g_runtime_restart_ms;
static uint32_t g_remote_epoch;
static uint32_t g_heartbeat_dedup_count;
static bool g_recovery_scheduled; /* prevent duplicate recovery scheduling */

/* ── Stage 3B: ASRC offload state ────────────────────────────── */

/* Module-static scratch receive buffer — 481 stereo frames (1924 B).
 * Serialised by submit_lock (same mutex as identity submit). */
static int16_t g_asrc_scratch[FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2] __attribute__((aligned(32)));

/* ASRC-specific statistics. */
static struct audio_offload_asrc_stats g_asrc_stats;

/* ── ASRC shadow verification optional buffer ─────────────────── */

#if defined(CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY)
static int16_t g_asrc_shadow[FLPR_RING_PAYLOAD_CAPACITY_FRAMES * 2] __attribute__((aligned(32)));
#endif

/* ── Helpers ─────────────────────────────────────────────────────── */

static bool check_flpr_healthy(void)
{
	struct flpr_status hs;
	flpr_handshake_get_status(&hs);
	return hs.healthy;
}

static void record_latency(uint32_t cycles)
{
	/* Must be called under g_lock spinlock. */
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
 *
 * Note: does NOT schedule recovery here (compute-release pattern).
 * Caller must capture schedule_recov decision under lock, release,
 * then schedule outside lock.
 */
static void record_fault(uint32_t *category_counter, int error, uint32_t seq)
{
	g_status.fallback_count++;
	if (category_counter) {
		(*category_counter)++;
	}
	g_status.last_error = error;
	g_status.last_error_seq = seq;

	/* Probation relapse: a fault during the probation window escalates
	 * backoff exponentially so the recovery storm does not re-arm
	 * at the base delay.  Relapses are countable; max_exhaustion
	 * is logged when the boundary is crossed. */
	if (g_probation_active) {
		g_status.recovery_relapses++;
		g_recovery_backoff_ms *= 2U;
		if (g_recovery_backoff_ms > OFFLOAD_RECOVERY_MAX_MS) {
			g_recovery_backoff_ms = OFFLOAD_RECOVERY_MAX_MS;
		}
	}

	g_state = AUDIO_OFFLOAD_RECOVERING;
	g_status.state = g_state;
	g_status.healthy = false;
}

/*
 * Lifecycle safety: check if state/generation/epoch changed during
 * a blocking operation.  Must be called under g_lock.
 *
 * If changed (stop/restart occurred), count ONE stale + ONE fallback
 * and return false.  Caller MUST NOT call record_fault.
 *
 * If unchanged, return true — caller should proceed with record_fault.
 */
static bool lifecycle_check_before_fault(enum audio_offload_state captured_state,
					 uint32_t captured_gen, uint32_t captured_epoch,
					 uint32_t sequence)
{
	if (g_state != captured_state || g_generation != captured_gen ||
	    g_stream_epoch != captured_epoch) {
		/* Stop/restart during blocking operation.
		 * Count ONE stale + ONE fallback.  Do NOT change state.
		 * Note: ASRC callers must also increment g_asrc_stats.fallback_count
		 * before returning when this returns false. */
		g_status.stale_count++;
		g_status.fallback_count++;
		g_status.last_error = -ESTALE;
		g_status.last_error_seq = sequence;
		return false;
	}
	return true;
}

/*
 * Capture schedule_recov decision + delay under lock for use outside lock.
 * Returns true if recovery should be scheduled; sets *delay_ms if so.
 */
static bool should_schedule_recovery_locked(uint32_t *delay_ms)
{
	if (g_state != AUDIO_OFFLOAD_RECOVERING) {
		return false;
	}
	*delay_ms = g_recovery_backoff_ms;
	return true;
}

/*
 * Schedule the recovery delayable work on the DEDICATED offload work queue.
 * Must be called OUTSIDE spinlock with the delay captured under lock.
 */
static void schedule_recovery(uint32_t delay_ms)
{
	k_work_schedule_for_queue(&g_offload_wq, &g_recovery_work, K_MSEC(delay_ms));
}

/*
 * Schedule the prep work on the DEDICATED offload work queue.
 * Must be called OUTSIDE spinlock.
 */
static void schedule_prep(k_timeout_t delay)
{
	k_work_schedule_for_queue(&g_offload_wq, &g_prep_work, delay);
}

/* ── Heartbeat supervisor (Stage 4B) ───────────────────────────────
 * Invoked from flpr_handshake heartbeat work context (outside spinlock)
 * on healthy→unhealthy transition.  Dedup: only fires once per transition
 * episode.  The first timeout (8ms output deadline) normally detects a
 * live hang before the 5 s heartbeat threshold, so the state is already
 * RECOVERING when this fires — skip duplicate scheduling.
 *
 * If offload is STOPPED, FLPR crash is treated as idle restart.
 */

static void offload_health_transition_cb(void *user_data)
{
	(void)user_data;

	k_spinlock_key_t key = k_spin_lock(&g_lock);

	switch (g_state) {
	case AUDIO_OFFLOAD_ACTIVE:
	case AUDIO_OFFLOAD_PREPARING:
		/* Active or preparing — transition to RECOVERING and schedule. */
		if (g_state != AUDIO_OFFLOAD_RECOVERING) {
			g_state = AUDIO_OFFLOAD_RECOVERING;
			g_status.state = g_state;
			g_status.healthy = false;
			LOG_WRN("offload: heartbeat supervisor → RECOVERING");
		} else {
			/* Already recovering (output timeout got here first) — dedup. */
			g_heartbeat_dedup_count++;
			g_status.heartbeat_dedup_count = g_heartbeat_dedup_count;
		}
		break;
	case AUDIO_OFFLOAD_RECOVERING:
		/* Already in recovery — dedup, do not schedule second restart. */
		g_heartbeat_dedup_count++;
		g_status.heartbeat_dedup_count = g_heartbeat_dedup_count;
		k_spin_unlock(&g_lock, key);
		return; /* no schedule */
	case AUDIO_OFFLOAD_STOPPED:
		/* Stopped: FLPR hung while idle — restart directly.
		 * Blocks heartbeat work (~300ms) — acceptable for rare fault. */
		LOG_WRN("offload: heartbeat supervisor → idle restart");
		k_spin_unlock(&g_lock, key);
		(void)flpr_runtime_restart(1500);
		return;
	case AUDIO_OFFLOAD_FALLBACK:
		/* Stopped/fallback — do nothing, no stream to recover. */
		k_spin_unlock(&g_lock, key);
		return;
	}

	/* Schedule recovery if not already scheduled. */
	bool should_sched = !g_recovery_scheduled;
	if (should_sched) {
		g_recovery_scheduled = true;
	}
	k_spin_unlock(&g_lock, key);

	if (should_sched) {
		schedule_recovery(OFFLOAD_RECOVERY_BASE_MS);
	}
}

void audio_offload_remote_unavailable(void)
{
	offload_health_transition_cb(NULL);
}

/* ── Prep work ──────────────────────────────────────────────────────
 * Runs on dedicated offload work queue.
 * Tries ring_init + coordinated_reset.  Retries with backoff on
 * failure.  Transitions to ACTIVE on success or FALLBACK on max
 * retries exhausted.
 *
 * Captures generation at entry; re-verifies against current generation
 * before transitioning to ACTIVE to guard against stop→start races. */

void prep_work_fn(struct k_work *work)
{
	(void)work;

	uint32_t start_gen;
	bool retry = false;

	/* ── Entry: check state + capture generation ──────────── */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (g_state != AUDIO_OFFLOAD_PREPARING) {
			k_spin_unlock(&g_lock, key);
			return;
		}
		start_gen = g_generation;
		if (g_prep_tries >= OFFLOAD_PREP_MAX_TRIES) {
			LOG_ERR("offload prep: max tries (%u) exhausted, FALLBACK",
				OFFLOAD_PREP_MAX_TRIES);
			g_status.recovery_fail_count++;
			g_state = AUDIO_OFFLOAD_FALLBACK;
			g_status.state = g_state;
			g_status.healthy = false;
			k_spin_unlock(&g_lock, key);
			return;
		}
		k_spin_unlock(&g_lock, key);
	}

	/* ── Step 1: Try ring init if deferred ─────────────────── */
	if (!g_rings_ready) {
		int ret = flpr_ring_mgr_init();
		if (ret == 0) {
			g_rings_ready = true;
			LOG_INF("offload prep: rings init OK (retry)");
		} else {
			LOG_WRN("offload prep: rings still not ready: %d", ret);
			retry = true;
			goto prep_retry;
		}
	}

	/* ── Step 2: Coordinated new epoch reset ───────────────── */
	{
		uint32_t epoch = k_cycle_get_32();
		if (epoch == 0) {
			epoch = 1;
		}
		epoch &= 0x7FFFFFFFU;

		int ret = flpr_ring_mgr_coordinated_reset(epoch, 5000);
		if (ret < 0) {
			LOG_WRN("offload prep: coordinated reset failed: %d", ret);
			retry = true;
			goto prep_retry;
		}

		/* Success — transition to ACTIVE under lock, with
		 * generation guard against stop→start race. */
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (g_state != AUDIO_OFFLOAD_PREPARING || g_generation != start_gen) {
			k_spin_unlock(&g_lock, key);
			return;
		}

		g_stream_epoch = epoch;
		g_generation++;
		g_status.epoch = epoch;
		g_status.generation = g_generation;
		g_status.healthy = true;

		/* Reset per-stream counters for new stream. */
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
		g_status.busy_count = 0;
		/* Lifetime counters preserved: recovery_attempts, recovery_fail_count,
		 * recovery_relapses, max_exhaustion_count, probation_cleared */
		g_status.rtt_min_cycles = 0;
		g_status.rtt_max_cycles = 0;
		g_status.rtt_sum_cycles = 0;
		g_status.rtt_count = 0;
		g_status.last_error = 0;
		g_status.last_error_seq = 0;

		g_state = AUDIO_OFFLOAD_ACTIVE;
		g_status.state = g_state;
		g_prep_tries = 0;
		g_prep_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
		g_recovery_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
		g_recovery_tries = 0;
		g_probation_active = false;
		g_probation_success = 0;
		g_status.probation_active = false;
		g_status.probation_success = 0;

		/* Reset ASRC per-stream counters. */
		g_asrc_stats.submit_count = 0;
		g_asrc_stats.success_count = 0;
		g_asrc_stats.fallback_count = 0;
		g_asrc_stats.timeout_count = 0;
		g_asrc_stats.full_count = 0;
		g_asrc_stats.stale_count = 0;
		g_asrc_stats.seq_fault_count = 0;
		g_asrc_stats.frame_fault_count = 0;
		g_asrc_stats.crc_fault_count = 0;
		g_asrc_stats.state_fault_count = 0;
		g_asrc_stats.verify_fault_count = 0;

		k_spin_unlock(&g_lock, key);

		LOG_INF("offload prep OK: epoch=%u gen=%u state=ACTIVE", epoch, g_generation);
		return;
	}

prep_retry: {
	/* Compute backoff + bump tries under lock, schedule outside. */
	k_spinlock_key_t key = k_spin_lock(&g_lock);
	if (g_state == AUDIO_OFFLOAD_PREPARING && g_generation == start_gen) {
		g_prep_tries++;
		g_prep_backoff_ms *= 2U;
		if (g_prep_backoff_ms > OFFLOAD_RECOVERY_MAX_MS) {
			g_prep_backoff_ms = OFFLOAD_RECOVERY_MAX_MS;
		}
		uint32_t delay_ms = g_prep_backoff_ms;
		k_spin_unlock(&g_lock, key);
		schedule_prep(K_MSEC(delay_ms));
	} else {
		k_spin_unlock(&g_lock, key);
	}
}
}

/* ── Recovery work (Stage 4B: staged approach) ─────────────────────
 * Runs on dedicated offload work queue — NEVER in BT callback.
 *
 * Staged algorithm:
 *   1. If handshake healthy, try coordinated ring reset with 100 ms ACK timeout.
 *   2. On unhealthy handshake or reset timeout/error, call runtime restart with
 *      1500 ms bound/READY budget.
 *   3. After restart success: ring remote-restart reinit, then coordinated reset
 *      with 100 ms timeout.
 *   4. Transition ACTIVE + probation only after all steps pass.
 *   5. Existing cumulative five-attempt/backoff/exhaustion policy remains.
 */

void recovery_work_fn(struct k_work *work)
{
	(void)work;

	uint32_t start_gen;
	bool tried_runtime = false;

	/* ── Entry: check state + capture generation ──────────── */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		/* State may have changed while work was queued. */
		if (g_state != AUDIO_OFFLOAD_RECOVERING) {
			g_recovery_scheduled = false;
			k_spin_unlock(&g_lock, key);
			return;
		}
		start_gen = g_generation;

		/* Bump tries BEFORE the check — each recovery cycle
		 * consumes one attempt. */
		g_recovery_tries++;

		if (g_recovery_tries > OFFLOAD_RECOVERY_MAX_TRIES) {
			LOG_ERR("offload recovery: max tries (%u) exhausted, staying in FALLBACK",
				OFFLOAD_RECOVERY_MAX_TRIES);
			g_status.max_exhaustion_count++;
			g_status.recovery_fail_count++;
			g_state = AUDIO_OFFLOAD_FALLBACK;
			g_status.state = g_state;
			g_recovery_scheduled = false;
			k_spin_unlock(&g_lock, key);
			return;
		}

		k_spin_unlock(&g_lock, key);
	}

	/* ── Stage 1: Try coordinated ring reset if healthy ───── */
	{
		bool handshake_healthy = check_flpr_healthy();

		if (handshake_healthy) {
			uint32_t epoch = k_cycle_get_32();
			if (epoch == 0) {
				epoch = 1;
			}
			epoch &= 0x7FFFFFFFU;

			int ret = flpr_ring_mgr_coordinated_reset(epoch, 100);
			if (ret == 0) {
				/* Short ring reset succeeded — transition to ACTIVE. */
				goto transition_active;
			}

			LOG_WRN("offload recovery: short ring reset failed (%d), "
				"escalating to runtime restart",
				ret);
		} else {
			LOG_WRN("offload recovery: handshake unhealthy, escalating to runtime "
				"restart");
		}
	}

	/* ── Stage 2: Runtime restart ──────────────────────────── */
	{
		/* Re-verify state + generation before runtime restart. */
		{
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (g_state != AUDIO_OFFLOAD_RECOVERING || g_generation != start_gen) {
				g_recovery_scheduled = false;
				k_spin_unlock(&g_lock, key);
				return;
			}
			k_spin_unlock(&g_lock, key);
		}

		uint32_t restart_start = k_uptime_get_32();
		int ret = flpr_runtime_restart(1500);

		if (ret != 0) {
			LOG_ERR("offload recovery: runtime restart failed: %d", ret);

			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (g_state == AUDIO_OFFLOAD_RECOVERING && g_generation == start_gen) {
				g_runtime_restart_fail++;
				g_status.runtime_restart_fail = g_runtime_restart_fail;
				g_recovery_backoff_ms *= 2U;
				if (g_recovery_backoff_ms > OFFLOAD_RECOVERY_MAX_MS) {
					g_recovery_backoff_ms = OFFLOAD_RECOVERY_MAX_MS;
				}
				uint32_t delay_ms = g_recovery_backoff_ms;
				g_recovery_scheduled = false;
				k_spin_unlock(&g_lock, key);
				schedule_recovery(delay_ms);
			} else {
				g_recovery_scheduled = false;
				k_spin_unlock(&g_lock, key);
			}
			return;
		}

		g_runtime_restart_ms = k_uptime_get_32() - restart_start;
		tried_runtime = true;

		/* Get new remote epoch from runtime restart status. */
		{
			struct flpr_runtime_status rs;
			flpr_runtime_get_status(&rs);
			g_remote_epoch = rs.new_epoch;
			g_status.remote_epoch = g_remote_epoch;
		}

		LOG_INF("offload recovery: runtime restart OK in %u ms, epoch=%u",
			g_runtime_restart_ms, g_remote_epoch);
	}

	/* ── Stage 3: Ring remote-restart reinit ──────────────── */
	{
		int ret = flpr_ring_mgr_remote_restarted();
		if (ret != 0) {
			LOG_ERR("offload recovery: ring remote restart failed: %d", ret);
			goto recovery_retry;
		}
	}

	/* ── Stage 4: Coordinated reset with 100 ms timeout ────── */
	{
		uint32_t epoch = k_cycle_get_32();
		if (epoch == 0) {
			epoch = 1;
		}
		epoch &= 0x7FFFFFFFU;

		int ret = flpr_ring_mgr_coordinated_reset(epoch, 100);
		if (ret != 0) {
			LOG_ERR("offload recovery: post-restart ring reset failed: %d", ret);
			goto recovery_retry;
		}

		/* Store the new epoch for transition. */
		g_stream_epoch = epoch;
	}

	/* ── Stage 5: Transition to ACTIVE ─────────────────────── */
transition_active: {
	k_spinlock_key_t key = k_spin_lock(&g_lock);

	/* Re-verify state + generation wasn't changed during IPC. */
	if (g_state != AUDIO_OFFLOAD_RECOVERING || g_generation != start_gen) {
		g_recovery_scheduled = false;
		k_spin_unlock(&g_lock, key);
		return;
	}

	g_generation++;
	g_status.epoch = g_stream_epoch;
	g_status.generation = g_generation;
	g_status.recovery_attempts++;
	g_status.healthy = true;

	if (tried_runtime) {
		g_runtime_restart_count++;
		g_status.runtime_restart_count = g_runtime_restart_count;
		g_status.runtime_restart_ms = g_runtime_restart_ms;
	}

	/* Start probation window. */
	g_probation_active = true;
	g_probation_success = 0;
	g_status.probation_active = true;
	g_status.probation_success = 0;

	g_state = AUDIO_OFFLOAD_ACTIVE;
	g_status.state = g_state;

	g_recovery_scheduled = false;

	k_spin_unlock(&g_lock, key);

	LOG_INF("offload recovery OK: epoch=%u gen=%u tries=%u backoff=%u ms runtime=%u",
		g_stream_epoch, g_generation, g_recovery_tries, g_recovery_backoff_ms,
		tried_runtime ? 1U : 0U);
}
	return;

recovery_retry: {
	k_spinlock_key_t key = k_spin_lock(&g_lock);
	if (g_state == AUDIO_OFFLOAD_RECOVERING && g_generation == start_gen) {
		g_recovery_backoff_ms *= 2U;
		if (g_recovery_backoff_ms > OFFLOAD_RECOVERY_MAX_MS) {
			g_recovery_backoff_ms = OFFLOAD_RECOVERY_MAX_MS;
		}
		uint32_t delay_ms = g_recovery_backoff_ms;
		g_recovery_scheduled = false;
		k_spin_unlock(&g_lock, key);
		schedule_recovery(delay_ms);
	} else {
		g_recovery_scheduled = false;
		k_spin_unlock(&g_lock, key);
	}
}
}

/* ── Public API ──────────────────────────────────────────────────── */

int audio_offload_init(void)
{
	if (g_initialized) {
		return 0;
	}

	memset(&g_status, 0, sizeof(g_status));
	memset(&g_asrc_stats, 0, sizeof(g_asrc_stats));

	/* Initialise work items — they run on the dedicated offload WQ. */
	k_work_init_delayable(&g_prep_work, prep_work_fn);
	k_work_init_delayable(&g_recovery_work, recovery_work_fn);

	/* Start the dedicated work queue thread directly — no K_THREAD_DEFINE
	 * wrapper.  This creates ONE thread with its own stack and TCB. */
	k_work_queue_start(&g_offload_wq, g_offload_stack, K_THREAD_STACK_SIZEOF(g_offload_stack),
			   OFFLOAD_THREAD_PRIORITY, NULL);

	/* Try to init rings early.  FLPR may not be ready yet —
	 * that's fine, prep will retry. */
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
	g_status.initialized = true;
	g_initialized = true;
	g_stream_epoch = 0;
	g_generation = 0;
	g_prep_tries = 0;
	g_prep_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
	g_recovery_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
	g_recovery_tries = 0;
	g_probation_active = false;
	g_probation_success = 0;

	/* Stage 4B: recovery state */
	g_runtime_restart_count = 0;
	g_runtime_restart_fail = 0;
	g_runtime_restart_ms = 0;
	g_remote_epoch = 0;
	g_heartbeat_dedup_count = 0;
	g_recovery_scheduled = false;

	/* Register heartbeat health transition callback. */
	flpr_handshake_register_health_cb(offload_health_transition_cb, NULL);

	return 0;
}

void audio_offload_stream_start(void)
{
	if (!g_initialized) {
		LOG_WRN("stream_start: offload not initialized");
		return;
	}

	/*
	 * Order matters for locking correctness:
	 * 1. Cancel pending work OUTSIDE spinlock (k_work_cancel_delayable can block).
	 * 2. Update state + generation UNDER spinlock.
	 * 3. Schedule new prep work OUTSIDE spinlock (k_work_schedule_for_queue safe).
	 *
	 * The cancel-before-lock order means a just-started work item may
	 * slip through.  The generation guard in prep_work_fn (start_gen
	 * captured at entry, re-verified at ACTIVE transition) handles
	 * this race.
	 */

	/* Cancel any in-flight pending work (may block briefly). */
	(void)k_work_cancel_delayable(&g_recovery_work);
	(void)k_work_cancel_delayable(&g_prep_work);

	k_spinlock_key_t key = k_spin_lock(&g_lock);

	/* Set PREPARING, bump generation, reset prep state. */
	g_state = AUDIO_OFFLOAD_PREPARING;
	g_status.state = g_state;
	g_status.healthy = false;
	g_generation++; /* Invalidate any pending submits from prior epoch */
	g_status.generation = g_generation;
	g_prep_tries = 0;
	g_prep_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
	g_recovery_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
	g_recovery_tries = 0;
	g_probation_active = false;
	g_probation_success = 0;
	g_status.probation_active = false;
	g_status.probation_success = 0;

	g_recovery_scheduled = false;

	k_spin_unlock(&g_lock, key);

	/* Schedule prep work on dedicated offload work queue.
	 * Returns immediately — caller (stream_started callback)
	 * is not blocked. */
	schedule_prep(K_NO_WAIT);

	LOG_INF("offload stream start: gen=%u state=PREPARING (prep scheduled)", g_generation);
}

void audio_offload_stream_stop(void)
{
	if (!g_initialized) {
		return;
	}

	/*
	 * Cancel OUTSIDE spinlock, update state UNDER spinlock.
	 * Same ordering rationale as stream_start.
	 */
	(void)k_work_cancel_delayable(&g_recovery_work);
	(void)k_work_cancel_delayable(&g_prep_work);

	k_spinlock_key_t key = k_spin_lock(&g_lock);

	/* Increment generation so any late output from prior epoch
	 * is rejected by the generation check in submit. */
	g_generation++;
	g_status.generation = g_generation;

	g_state = AUDIO_OFFLOAD_STOPPED;
	g_status.state = g_state;
	g_status.healthy = false;
	g_stream_epoch = 0;
	g_status.epoch = 0;
	g_prep_tries = 0;
	g_prep_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
	g_recovery_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
	g_recovery_tries = 0;
	g_probation_active = false;
	g_probation_success = 0;
	g_status.probation_active = false;
	g_status.probation_success = 0;

	g_recovery_scheduled = false;

	k_spin_unlock(&g_lock, key);

	LOG_INF("offload stream stop: gen=%u", g_generation);
}

bool audio_offload_is_healthy(void)
{
	if (!g_initialized) {
		return false;
	}

	k_spinlock_key_t key = k_spin_lock(&g_lock);
	bool result = (g_state == AUDIO_OFFLOAD_ACTIVE);
	k_spin_unlock(&g_lock, key);

	return result;
}

bool audio_offload_is_stopped(void)
{
	if (!g_initialized) {
		return true;
	}

	k_spinlock_key_t key = k_spin_lock(&g_lock);
	bool result = (g_state == AUDIO_OFFLOAD_STOPPED);
	k_spin_unlock(&g_lock, key);

	return result;
}

int audio_offload_submit(const int16_t *input, size_t samples, uint32_t sequence,
			 int32_t correction_ppm, int16_t *output)
{
	/* ── Validate args BEFORE any state/counter access ────── */
	if (!input || !output || samples == 0) {
		return -EINVAL;
	}

	if (samples != OFFLOAD_EXPECTED_SAMPLES) {
		return -EINVAL;
	}

	uint32_t captured_generation;
	uint32_t captured_epoch;
	enum audio_offload_state captured_state;
	bool need_fallback = false;
	uint32_t recovery_delay_ms;

	/* ── Pre-check under spinlock ──────────────────────────────── */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (!g_initialized || g_state == AUDIO_OFFLOAD_STOPPED) {
			k_spin_unlock(&g_lock, key);
			return -EAGAIN;
		}

		captured_state = g_state;
		captured_generation = g_generation;
		captured_epoch = g_stream_epoch;

		/* Increment submit_count for EVERY valid call. */
		g_status.submit_count++;

		if (g_state != AUDIO_OFFLOAD_ACTIVE) {
			/* PREPARING, FALLBACK, RECOVERING → fallback.
			 * Exactly one fallback increment per valid submit. */
			g_status.fallback_count++;
			need_fallback = true;
		}

		k_spin_unlock(&g_lock, key);
	}

	if (need_fallback) {
		return -EAGAIN;
	}

	/* ── Serialise submit — one block in-flight at a time ─────── */
	if (k_mutex_lock(&g_submit_lock, K_MSEC(OFFLOAD_DEADLINE_MS)) != 0) {
		/* Mutex timeout — another submit is stuck.
		 * Re-check lifecycle first: stop may have happened while waiting. */
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (!lifecycle_check_before_fault(captured_state, captured_generation,
						  captured_epoch, sequence)) {
			/* Stop/restart occurred — already counted stale+fallback.
			 * Do NOT transition to RECOVERING. */
			k_spin_unlock(&g_lock, key);
			return -EAGAIN;
		}

		g_status.busy_count++;
		record_fault(NULL, -EBUSY, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);

		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}

		return -EAGAIN;
	}

	/* ── Re-check state under mutex (race: state change between
	 *     spinlock release and mutex acquire) ─────────────────── */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (g_state != AUDIO_OFFLOAD_ACTIVE) {
			/* State changed before we got the mutex.
			 * Count ONE fallback (not already counted by pre-check
			 * because state WAS ACTIVE at that point). */
			g_status.fallback_count++;
			g_status.last_error = -EAGAIN;
			g_status.last_error_seq = sequence;
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}

		/* Re-capture under mutex protection for late output
		 * invalidation check after wait/consume. */
		captured_state = g_state;
		captured_generation = g_generation;
		captured_epoch = g_stream_epoch;

		k_spin_unlock(&g_lock, key);
	}

	/* ── Produce: write input PCM to input ring WITH CRC ──────── */
	enum flpr_produce_result pr =
		flpr_ring_mgr_produce_block((const uint8_t *)input, OFFLOAD_EXPECTED_FRAMES,
					    sequence, correction_ppm, true /* compute_crc */);

	if (pr == FLPR_PRODUCE_FULL) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!lifecycle_check_before_fault(captured_state, captured_generation,
						  captured_epoch, sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		record_fault(&g_status.full_count, -ENOSPC, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}
	if (pr != FLPR_PRODUCE_OK) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!lifecycle_check_before_fault(captured_state, captured_generation,
						  captured_epoch, sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		record_fault(NULL, -EIO, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}

	/* ── Notify FLPR ──────────────────────────────────────────── */
	{
		int notify_ret = flpr_ring_mgr_notify_producer();
		if (notify_ret < 0) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (!lifecycle_check_before_fault(captured_state, captured_generation,
							  captured_epoch, sequence)) {
				k_spin_unlock(&g_lock, key);
				k_mutex_unlock(&g_submit_lock);
				return -EAGAIN;
			}
			record_fault(NULL, notify_ret, sequence);
			bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			if (sched) {
				schedule_recovery(recovery_delay_ms);
			}
			return -EAGAIN;
		}
	}

	/* ── Wait for FLPR to produce output ────────────────────────
	 * THIS IS A BLOCKING OPERATION (~0-8ms).
	 * Stop/restart may occur during this wait. */
	{
		int wait_ret = flpr_ring_mgr_wait_consume(OFFLOAD_DEADLINE_MS);
		if (wait_ret != 0) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (!lifecycle_check_before_fault(captured_state, captured_generation,
							  captured_epoch, sequence)) {
				/* Stop/restart during wait — already counted stale+fallback. */
				k_spin_unlock(&g_lock, key);
				k_mutex_unlock(&g_submit_lock);
				return -EAGAIN;
			}
			record_fault(&g_status.timeout_count, -ETIMEDOUT, sequence);
			bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			if (sched) {
				schedule_recovery(recovery_delay_ms);
			}
			return -EAGAIN;
		}
	}

	/* ── BEFORE touching output: recheck generation/state/epoch ───
	 * If stop/restart/recovery happened during wait, reject output
	 * untouched and count stale. */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (g_state != captured_state || g_generation != captured_generation ||
		    g_stream_epoch != captured_epoch) {
			/* Lifecycle changed during wait.  Output discarded. */
			g_status.stale_count++;
			g_status.fallback_count++;
			g_status.last_error = -ESTALE;
			g_status.last_error_seq = sequence;
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		k_spin_unlock(&g_lock, key);
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
		if (!lifecycle_check_before_fault(captured_state, captured_generation,
						  captured_epoch, sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		record_fault(NULL, -ENOENT, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}
	if (cr == FLPR_CONSUME_STALE) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!lifecycle_check_before_fault(captured_state, captured_generation,
						  captured_epoch, sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		record_fault(&g_status.stale_count, -ESTALE, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}
	if (cr != FLPR_CONSUME_OK) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!lifecycle_check_before_fault(captured_state, captured_generation,
						  captured_epoch, sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		record_fault(NULL, -EIO, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}

	/* ── Re-check lifecycle before output copy ──────────────────── */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (g_state != captured_state || g_generation != captured_generation ||
		    g_stream_epoch != captured_epoch) {
			g_status.stale_count++;
			g_status.fallback_count++;
			g_status.last_error = -ESTALE;
			g_status.last_error_seq = sequence;
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		k_spin_unlock(&g_lock, key);
	}

	/* ── Validate output identity ────────────────────────────────
	 * All checks happen BEFORE output is touched.
	 * Any fault here → output buffer stays UNTOUCHED. */

	/* Frame count check. */
	if (vf != OFFLOAD_EXPECTED_FRAMES) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!lifecycle_check_before_fault(captured_state, captured_generation,
						  captured_epoch, sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		record_fault(&g_status.frame_fault_count, -EFAULT, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}

	/* Sequence check. */
	if (out_seq != sequence) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!lifecycle_check_before_fault(captured_state, captured_generation,
						  captured_epoch, sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		record_fault(&g_status.seq_fault_count, -EFAULT, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}

	/* CRC check: recompute over received payload, compare to metadata. */
	{
		uint32_t computed_crc =
			flpr_ring_crc32((const uint8_t *)g_scratch_output, OFFLOAD_EXPECTED_BYTES);
		if (computed_crc != crc_metadata) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (!lifecycle_check_before_fault(captured_state, captured_generation,
							  captured_epoch, sequence)) {
				k_spin_unlock(&g_lock, key);
				k_mutex_unlock(&g_submit_lock);
				return -EAGAIN;
			}
			record_fault(&g_status.crc_fault_count, -EFAULT, sequence);
			bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			if (sched) {
				schedule_recovery(recovery_delay_ms);
			}
			return -EAGAIN;
		}
	}

	/* Payload identity check: memcmp output against original input. */
	{
		int cmp = memcmp(g_scratch_output, input, OFFLOAD_EXPECTED_BYTES);
		if (cmp != 0) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (!lifecycle_check_before_fault(captured_state, captured_generation,
							  captured_epoch, sequence)) {
				k_spin_unlock(&g_lock, key);
				k_mutex_unlock(&g_submit_lock);
				return -EAGAIN;
			}
			record_fault(&g_status.payload_fault_count, -EFAULT, sequence);
			bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			if (sched) {
				schedule_recovery(recovery_delay_ms);
			}
			return -EAGAIN;
		}
	}

	/* ── Final lifecycle check before output copy ──────────────── */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (g_state != captured_state || g_generation != captured_generation ||
		    g_stream_epoch != captured_epoch) {
			g_status.stale_count++;
			g_status.fallback_count++;
			g_status.last_error = -ESTALE;
			g_status.last_error_seq = sequence;
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		k_spin_unlock(&g_lock, key);
	}

	/* ── All checks passed — copy output, record latency ───────── */
	memcpy(output, g_scratch_output, OFFLOAD_EXPECTED_BYTES);

	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		/* Final lifecycle recheck AFTER output copy.
		 * If generation changed, the output was already copied
		 * but the caller won't use it because return is -EAGAIN.
		 * This minimises the window where a stop can invalidate
		 * output between copy and count. */
		if (g_state != captured_state || g_generation != captured_generation ||
		    g_stream_epoch != captured_epoch) {
			g_status.stale_count++;
			g_status.fallback_count++;
			g_status.last_error = -ESTALE;
			g_status.last_error_seq = sequence;
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}

		g_status.success_count++;
		g_status.last_error = 0;

		/* Probation success tracking: each consecutive success
		 * during probation window counts toward the clearance
		 * threshold.  Once crossed, escalation state resets to
		 * base so the next fault starts fresh. */
		if (g_probation_active) {
			g_probation_success++;
			g_status.probation_success = g_probation_success;

			if (g_probation_success >= PROBATION_SUCCESS_THRESHOLD) {
				g_probation_active = false;
				g_status.probation_active = false;
				g_recovery_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
				g_recovery_tries = 0;
				g_status.probation_cleared++;
				LOG_INF("offload probation cleared after %u consecutive successes",
					g_probation_success);
			}
		}

		record_latency(latency_cycles);
		k_spin_unlock(&g_lock, key);
	}

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
	status->probation_active = g_probation_active;
	status->probation_success = g_probation_success;

	/* Stage 4B: runtime restart + heartbeat supervisor */
	status->runtime_restart_count = g_runtime_restart_count;
	status->runtime_restart_fail = g_runtime_restart_fail;
	status->runtime_restart_ms = g_runtime_restart_ms;
	status->remote_epoch = g_remote_epoch;
	status->heartbeat_dedup_count = g_heartbeat_dedup_count;

	k_spin_unlock(&g_lock, key);
}

/* ── Stage 3B: ASRC offload ───────────────────────────────────────── */

int audio_offload_process_asrc(const int16_t *input, uint16_t input_frames, uint32_t sequence,
			       int32_t correction_ppm, const struct audio_asrc_state *pre_state,
			       int16_t *output, uint16_t output_capacity,
			       struct audio_offload_asrc_result *result)
{
	/*
	 * Local helper: lifecycle recheck that also counts ASRC fallback
	 * when stop/restart occurred during a blocking operation.
	 * Returns true if lifecycle unchanged; false if changed (already
	 * counted generic stale+fallback via lifecycle_check_before_fault,
	 * plus ASRC fallback here).
	 */
#define ASRC_LIFECYCLE_CHECK(cap_state, cap_gen, cap_epoch, seq)                                   \
	({                                                                                         \
		bool __ok =                                                                        \
			lifecycle_check_before_fault((cap_state), (cap_gen), (cap_epoch), (seq));  \
		if (!__ok) {                                                                       \
			g_asrc_stats.fallback_count++;                                             \
		}                                                                                  \
		__ok;                                                                              \
	})

	/* ── Validate args BEFORE any state/counter access ────── */
	if (!input || !output || !pre_state || !result || input_frames == 0) {
		return -EINVAL;
	}
	if (input_frames != OFFLOAD_EXPECTED_FRAMES) {
		return -EINVAL;
	}
	if (output_capacity < FLPR_RING_PAYLOAD_CAPACITY_FRAMES) {
		return -EINVAL;
	}

	uint32_t captured_generation;
	uint32_t captured_epoch;
	enum audio_offload_state captured_state;
	bool need_fallback = false;
	uint32_t recovery_delay_ms;

	/* ── Pre-check under spinlock ──────────────────────────────── */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (!g_initialized || g_state == AUDIO_OFFLOAD_STOPPED) {
			k_spin_unlock(&g_lock, key);
			return -EAGAIN;
		}

		captured_state = g_state;
		captured_generation = g_generation;
		captured_epoch = g_stream_epoch;

		g_asrc_stats.submit_count++;
		g_status.submit_count++;

		if (g_state != AUDIO_OFFLOAD_ACTIVE) {
			g_asrc_stats.fallback_count++;
			g_status.fallback_count++;
			need_fallback = true;
		}

		k_spin_unlock(&g_lock, key);
	}

	if (need_fallback) {
		return -EAGAIN;
	}

	/* ── Serialise — one block in-flight at a time ──────────── */
	if (k_mutex_lock(&g_submit_lock, K_MSEC(OFFLOAD_DEADLINE_MS)) != 0) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					  sequence)) {
			k_spin_unlock(&g_lock, key);
			return -EAGAIN;
		}

		g_asrc_stats.fallback_count++;
		g_status.busy_count++;
		record_fault(NULL, -EBUSY, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);

		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}

		return -EAGAIN;
	}

	/* Re-check state under mutex. */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (g_state != AUDIO_OFFLOAD_ACTIVE) {
			g_asrc_stats.fallback_count++;
			g_status.fallback_count++;
			g_status.last_error = -EAGAIN;
			g_status.last_error_seq = sequence;
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}

		captured_state = g_state;
		captured_generation = g_generation;
		captured_epoch = g_stream_epoch;

		k_spin_unlock(&g_lock, key);
	}

	/* ── Produce: ASRC-typed block into input ring ─────────── */
	enum flpr_produce_result pr = flpr_ring_mgr_produce_asrc(
		input, OFFLOAD_EXPECTED_FRAMES, sequence, correction_ppm, pre_state);

	if (pr == FLPR_PRODUCE_FULL) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					  sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		g_asrc_stats.fallback_count++;
		g_asrc_stats.full_count++;
		record_fault(&g_status.full_count, -ENOSPC, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}
	if (pr != FLPR_PRODUCE_OK) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					  sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		g_asrc_stats.fallback_count++;
		record_fault(NULL, -EIO, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}

	/* ── Notify FLPR ───────────────────────────────────────── */
	{
		int notify_ret = flpr_ring_mgr_notify_producer();
		if (notify_ret < 0) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation,
						  captured_epoch, sequence)) {
				k_spin_unlock(&g_lock, key);
				k_mutex_unlock(&g_submit_lock);
				return -EAGAIN;
			}
			g_asrc_stats.fallback_count++;
			record_fault(NULL, notify_ret, sequence);
			bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			if (sched) {
				schedule_recovery(recovery_delay_ms);
			}
			return -EAGAIN;
		}
	}

	/* ── Wait for FLPR output ──────────────────────────────── */
	{
		int wait_ret = flpr_ring_mgr_wait_consume(OFFLOAD_DEADLINE_MS);
		if (wait_ret != 0) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation,
						  captured_epoch, sequence)) {
				k_spin_unlock(&g_lock, key);
				k_mutex_unlock(&g_submit_lock);
				return -EAGAIN;
			}
			g_asrc_stats.fallback_count++;
			g_asrc_stats.timeout_count++;
			record_fault(&g_status.timeout_count, -ETIMEDOUT, sequence);
			bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			if (sched) {
				schedule_recovery(recovery_delay_ms);
			}
			return -EAGAIN;
		}
	}

	/* ── Lifecycle recheck before consuming output ─────────── */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		int lc_ok;

		lc_ok = ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					     sequence);
		if (!lc_ok) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		k_spin_unlock(&g_lock, key);
	}

	/* ── Consume: typed ASRC result from output ring ──────── */
	struct flpr_consume_asrc_result cr;

	memset(&cr, 0, sizeof(cr));

	enum flpr_consume_result cresult = flpr_ring_mgr_consume_asrc_result(
		g_asrc_scratch, FLPR_RING_PAYLOAD_CAPACITY_FRAMES, &cr);

	if (cresult == FLPR_CONSUME_EMPTY) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					  sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		g_asrc_stats.fallback_count++;
		record_fault(NULL, -ENOENT, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}
	if (cresult == FLPR_CONSUME_STALE) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					  sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		g_asrc_stats.fallback_count++;
		g_asrc_stats.stale_count++;
		record_fault(&g_status.stale_count, -ESTALE, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}
	if (cresult != FLPR_CONSUME_OK) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					  sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		g_asrc_stats.fallback_count++;
		record_fault(NULL, -EIO, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}

	/* ── Lifecycle recheck after consume ──────────────────── */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		int lc_ok;

		lc_ok = ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					     sequence);
		if (!lc_ok) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		k_spin_unlock(&g_lock, key);
	}

	/* ── Validate FLPR transport metadata ─────────────────────
	 * All validation BEFORE modifying output/result.
	 * Output and result buffers remain UNTOUCHED on any fault. */

	/* Error output from FLPR (status < 0, frames = 0) — valid transport. */
	if (cr.processing_status < 0 && cr.output_frames == 0) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					  sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		g_asrc_stats.fallback_count++;
		record_fault(NULL, cr.processing_status, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}

	/* Validate frame count: 1..481 for normal output. */
	if (cr.output_frames < 1 || cr.output_frames > FLPR_RING_PAYLOAD_CAPACITY_FRAMES) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					  sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		g_asrc_stats.fallback_count++;
		g_asrc_stats.frame_fault_count++;
		record_fault(&g_status.frame_fault_count, -EFAULT, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}

	/* Validate processing_status is zero for normal output. */
	if (cr.processing_status != 0) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					  sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		g_asrc_stats.fallback_count++;
		record_fault(NULL, -EFAULT, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}

	/* Validate flags must equal VALID|ASRC_LINEAR exactly. */
	{
		uint16_t required = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
		if (cr.flags != required) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation,
						  captured_epoch, sequence)) {
				k_spin_unlock(&g_lock, key);
				k_mutex_unlock(&g_submit_lock);
				return -EAGAIN;
			}
			g_asrc_stats.fallback_count++;
			record_fault(NULL, -EFAULT, sequence);
			bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			if (sched) {
				schedule_recovery(recovery_delay_ms);
			}
			return -EAGAIN;
		}
	}

	/* Validate sequence equals request. */
	if (cr.sequence != sequence) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					  sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		g_asrc_stats.fallback_count++;
		g_asrc_stats.seq_fault_count++;
		record_fault(&g_status.seq_fault_count, -EFAULT, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}

	/* Validate correction_ppm echo equals request. */
	if (cr.correction_ppm != correction_ppm) {
		k_spinlock_key_t key = k_spin_lock(&g_lock);
		if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation, captured_epoch,
					  sequence)) {
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}
		g_asrc_stats.fallback_count++;
		record_fault(NULL, -EFAULT, sequence);
		bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
		k_spin_unlock(&g_lock, key);
		k_mutex_unlock(&g_submit_lock);
		if (sched) {
			schedule_recovery(recovery_delay_ms);
		}
		return -EAGAIN;
	}

	/* Validate reserved bytes are zero. */
	{
		const uint8_t *res = cr.post_state.reserved;
		if (res[0] != 0 || res[1] != 0 || res[2] != 0) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation,
						  captured_epoch, sequence)) {
				k_spin_unlock(&g_lock, key);
				k_mutex_unlock(&g_submit_lock);
				return -EAGAIN;
			}
			g_asrc_stats.fallback_count++;
			g_asrc_stats.state_fault_count++;
			record_fault(NULL, -EFAULT, sequence);
			bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			if (sched) {
				schedule_recovery(recovery_delay_ms);
			}
			return -EAGAIN;
		}
	}

	/* Validate post-state importable and step_base unchanged. */
	{
		struct audio_asrc tmp_ctx;
		int16_t dummy_l, dummy_r;
		bool dummy_v;
		int imp_ret = audio_asrc_state_import(&tmp_ctx, &cr.post_state, &dummy_l, &dummy_r,
						      &dummy_v);
		if (imp_ret != 0) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation,
						  captured_epoch, sequence)) {
				k_spin_unlock(&g_lock, key);
				k_mutex_unlock(&g_submit_lock);
				return -EAGAIN;
			}
			g_asrc_stats.fallback_count++;
			g_asrc_stats.state_fault_count++;
			record_fault(NULL, -EFAULT, sequence);
			bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			if (sched) {
				schedule_recovery(recovery_delay_ms);
			}
			return -EAGAIN;
		}

		/* step_base must match pre_state (same ratio). */
		if (cr.post_state.step_base != pre_state->step_base) {
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation,
						  captured_epoch, sequence)) {
				k_spin_unlock(&g_lock, key);
				k_mutex_unlock(&g_submit_lock);
				return -EAGAIN;
			}
			g_asrc_stats.fallback_count++;
			g_asrc_stats.state_fault_count++;
			record_fault(NULL, -EFAULT, sequence);
			bool sched = should_schedule_recovery_locked(&recovery_delay_ms);
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			if (sched) {
				schedule_recovery(recovery_delay_ms);
			}
			return -EAGAIN;
		}
	}

	/* ── Optional shadow verification ──────────────────────── */
#if defined(CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY)
	{
		/* Run cpuapp ASRC from same pre-state + ppm. */
		struct audio_asrc verify_ctx;
		int16_t verify_prev_l = 0, verify_prev_r = 0;
		bool verify_prev_valid = false;
		memset(g_asrc_shadow, 0, sizeof(g_asrc_shadow));

		/* Import pre-state into a local ASRC context. */
		int imp_ret = audio_asrc_state_import(&verify_ctx, pre_state, &verify_prev_l,
						      &verify_prev_r, &verify_prev_valid);
		/* Import must succeed — pre_state was exported by cpuapp.
		 * Import failure is a fault: do NOT fall through as pass. */
		if (imp_ret != 0) {
			goto shadow_mismatch;
		}

		{
			size_t consumed, produced;
			int16_t nl, nr;
			int asrc_ret = audio_asrc_process(
				&verify_ctx, input, OFFLOAD_EXPECTED_FRAMES, g_asrc_shadow,
				FLPR_RING_PAYLOAD_CAPACITY_FRAMES, correction_ppm, verify_prev_l,
				verify_prev_r, verify_prev_valid, &consumed, &produced, &nl, &nr);

			/* Compare return code. */
			if (asrc_ret != 0) {
				goto shadow_mismatch;
			}

			/* Compare frame count. */
			if (produced != cr.output_frames) {
				goto shadow_mismatch;
			}

			/* Compare every sample. */
			size_t sample_count = produced * 2U;
			for (size_t i = 0; i < sample_count; i++) {
				if (g_asrc_shadow[i] != g_asrc_scratch[i]) {
					goto shadow_mismatch;
				}
			}

			/* Compare post-state. */
			struct audio_asrc_state exported;
			audio_asrc_state_export(&verify_ctx, nl, nr, true, &exported);
			if (exported.phase != cr.post_state.phase ||
			    exported.step_base != cr.post_state.step_base ||
			    exported.prev_l != cr.post_state.prev_l ||
			    exported.prev_r != cr.post_state.prev_r ||
			    exported.prev_valid != cr.post_state.prev_valid) {
				goto shadow_mismatch;
			}

			goto shadow_pass;

shadow_mismatch:
			k_spinlock_key_t key = k_spin_lock(&g_lock);
			if (!ASRC_LIFECYCLE_CHECK(captured_state, captured_generation,
						  captured_epoch, sequence)) {
				k_spin_unlock(&g_lock, key);
				k_mutex_unlock(&g_submit_lock);
				return -EAGAIN;
			}
			g_asrc_stats.fallback_count++;
			g_asrc_stats.verify_fault_count++;
			record_fault(NULL, -EFAULT, sequence);
			bool should_sched = should_schedule_recovery_locked(&recovery_delay_ms);
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			if (should_sched) {
				schedule_recovery(recovery_delay_ms);
			}
			return -EAGAIN;
		}
shadow_pass:
		(void)0;
	}
#endif /* CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY */

	/* ── Commit under g_lock — linearization point ────────
	 * All validation passed.  Now commit success/stats/probation
	 * atomically under the spinlock.  After this, stream_stop will
	 * NOT retroactively invalidate this submit. */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		/* Final lifecycle recheck. */
		if (g_state != captured_state || g_generation != captured_generation ||
		    g_stream_epoch != captured_epoch) {
			g_status.stale_count++;
			g_asrc_stats.fallback_count++;
			g_status.fallback_count++;
			g_status.last_error = -ESTALE;
			g_status.last_error_seq = sequence;
			k_spin_unlock(&g_lock, key);
			k_mutex_unlock(&g_submit_lock);
			return -EAGAIN;
		}

		g_asrc_stats.success_count++;
		g_status.success_count++;
		g_status.last_error = 0;

		/* RTT tracking. */
		if (g_asrc_stats.rtt_count == 0 || cr.rtt_cycles < g_asrc_stats.rtt_min_cycles) {
			g_asrc_stats.rtt_min_cycles = cr.rtt_cycles;
		}
		if (cr.rtt_cycles > g_asrc_stats.rtt_max_cycles) {
			g_asrc_stats.rtt_max_cycles = cr.rtt_cycles;
		}
		g_asrc_stats.rtt_sum_cycles += (uint64_t)cr.rtt_cycles;
		g_asrc_stats.rtt_count++;

		/* Processing cycles tracking. */
		if (cr.processing_cycles > 0) {
			if (g_asrc_stats.cycles_count == 0 ||
			    cr.processing_cycles < g_asrc_stats.cycles_min) {
				g_asrc_stats.cycles_min = cr.processing_cycles;
			}
			if (cr.processing_cycles > g_asrc_stats.cycles_max) {
				g_asrc_stats.cycles_max = cr.processing_cycles;
			}
			g_asrc_stats.cycles_sum += (uint64_t)cr.processing_cycles;
			g_asrc_stats.cycles_count++;
		}

		/* Probation success tracking. */
		if (g_probation_active) {
			g_probation_success++;
			g_status.probation_success = g_probation_success;

			if (g_probation_success >= PROBATION_SUCCESS_THRESHOLD) {
				g_probation_active = false;
				g_status.probation_active = false;
				g_recovery_backoff_ms = OFFLOAD_RECOVERY_BASE_MS;
				g_recovery_tries = 0;
				g_status.probation_cleared++;
				LOG_INF("offload probation cleared after %u consecutive successes",
					g_probation_success);
			}
		}

		record_latency(cr.rtt_cycles);
		k_spin_unlock(&g_lock, key);
	}

	/* ── Copy validated output to caller ────────────────────
	 * AFTER g_lock commit — no failure path beyond this point.
	 * Explicit field assignment (not memcpy between different struct types). */
	{
		size_t out_bytes = (size_t)cr.output_frames * 4U;
		memcpy(output, g_asrc_scratch, out_bytes);

		result->output_frames = cr.output_frames;
		memcpy(&result->post_state, &cr.post_state, sizeof(result->post_state));
		result->processing_cycles = cr.processing_cycles;
	}

	k_mutex_unlock(&g_submit_lock);
	return 0;
}

void audio_offload_get_asrc_stats(struct audio_offload_asrc_stats *s)
{
	if (!s) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&g_lock);
	memcpy(s, &g_asrc_stats, sizeof(*s));
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

bool audio_offload_is_stopped(void)
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

int audio_offload_process_asrc(const int16_t *input, uint16_t input_frames, uint32_t sequence,
			       int32_t correction_ppm, const struct audio_asrc_state *pre_state,
			       int16_t *output, uint16_t output_capacity,
			       struct audio_offload_asrc_result *result)
{
	(void)input;
	(void)input_frames;
	(void)sequence;
	(void)correction_ppm;
	(void)pre_state;
	(void)output;
	(void)output_capacity;
	(void)result;
	return -ENOSYS;
}

void audio_offload_get_asrc_stats(struct audio_offload_asrc_stats *s)
{
	if (s) {
		memset(s, 0, sizeof(*s));
	}
}

#endif /* CONFIG_SOC_NRF54L15 */
