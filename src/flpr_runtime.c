/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLPR runtime restart manager — nRF54L15 only.
 *
 * CPUAPP-owned synchronous restart of SRAM-executing FLPR VPR
 * using public nrfx VPR HAL + public IPC service lifecycle.
 *
 * DT-derived from cpuflpr_vpr node:
 *   - VPR register base
 *   - source-memory phandle (FLPR code in RRAM at 0x165000)
 *   - execution-memory phandle (FLPR execution SRAM at 0x20030000)
 *
 * Stage 4A reset-order fix: one-variable DMCONTROL mask.
 * DMACTIVE stays Enabled through entire restart — never Disabled.
 * Only NDMRESET toggles: assert for preparation, release as final launch edge.
 *
 * Exact restart sequence:
 *   1. Snapshot previous handshake epoch; mark manager busy.
 *   2. Deregister CPUAPP IPC endpoint.
 *   3. nrf_vpr_cpurun_set(vpr, false) — stop VPR.
 *   4. Assert NDMRESET+DMACTIVE (single mask, held).
 *   5. Copy exec-size bytes from source to execution SRAM (reset held).
 *   6. Flush copied range + full barrier (reset held).
 *   7. CRC-32 execution vs source, require equality (reset held).
 *   8. Set INITPC to execution base (reset held).
 *   9. Re-register CPUAPP IPC endpoint (reset held).
 *  10. Set CPURUN true (reset held).
 *  11. Final launch: release NDMRESET (DMACTIVE still Enabled).
 *  12. Wait bound, then READY+ACK with epoch different from snapshot.
 *  13. Return success; on failure leave reset asserted/core stopped,
 *      report exact stage/error. Never reboot CPUAPP.
 *
 * No raw register writes — all through nrfx HAL.
 * No heap — all structs are static.
 * Mutex-serialised sync API. Shell/dedicated thread only.
 *
 * Automatic recovery may call restart() while offload is RECOVERING; the
 * shell owns its separate active-stream guard.  restart() itself does NOT
 * reject an active offload stream.
 */

#include "flpr_runtime.h"

#include <errno.h>
#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/cache.h>

#if defined(CONFIG_SOC_NRF54L15) || defined(FLPR_RUNTIME_NATIVE_TEST)

#include "flpr_handshake.h"
#include "flpr_ring.h" /* for flpr_ring_crc32 */

#if defined(FLPR_RUNTIME_NATIVE_TEST)
/* Test-owned hook header: provides VPR register storage, source/execution
 * byte arrays, ordered restart events, and time/cache/barrier hooks.
 * Included BEFORE the hardware constants so test storage can replace the
 * DT-derived addresses below.  Only compiled into the native_sim suite. */
#include "flpr_runtime_hooks.h"
#else
#include <hal/nrf_vpr.h>
#endif

LOG_MODULE_REGISTER(flpr_rt, LOG_LEVEL_INF);

/* ── Memory constants ──────────────────────────────────────────────
 * Production: DT-derived from the cpuflpr_vpr node.  Test mode: host
 * arrays provided by the test hooks (native_sim cannot dereference the
 * fixed physical addresses).  All fixed-address assertions are retained
 * outside test mode. */

#if defined(FLPR_RUNTIME_NATIVE_TEST)

#define VPR_REG_PTR   (&flpr_rt_test_vpr)
#define SRC_BASE_PTR  (flpr_rt_test_source)
#define EXEC_BASE_PTR (flpr_rt_test_exec)
#define SRC_SIZE      (FLPR_RT_TEST_IMAGE_SIZE)
#define EXEC_SIZE     (FLPR_RT_TEST_IMAGE_SIZE)

#else

#define VPR_NODE  DT_NODELABEL(cpuflpr_vpr)
#define SRC_NODE  DT_PHANDLE(VPR_NODE, source_memory)
#define EXEC_NODE DT_PHANDLE(VPR_NODE, execution_memory)

/* Build assertions. */
BUILD_ASSERT(DT_NODE_EXISTS(VPR_NODE), "cpuflpr_vpr node must exist");
BUILD_ASSERT(DT_NODE_HAS_PROP(VPR_NODE, source_memory), "source-memory required");
BUILD_ASSERT(DT_NODE_HAS_PROP(VPR_NODE, execution_memory), "execution-memory required");

#define VPR_BASE  DT_REG_ADDR(VPR_NODE)
#define SRC_BASE  DT_REG_ADDR(SRC_NODE)
#define SRC_SIZE  DT_REG_SIZE(SRC_NODE)
#define EXEC_BASE DT_REG_ADDR(EXEC_NODE)
#define EXEC_SIZE DT_REG_SIZE(EXEC_NODE)

BUILD_ASSERT(EXEC_SIZE <= SRC_SIZE, "execution-memory size must be <= source-memory size");
BUILD_ASSERT(EXEC_BASE == 0x20030000 && EXEC_SIZE == 0x10000,
	     "execution range must be exactly 0x20030000..0x20040000");
BUILD_ASSERT((EXEC_BASE & 0x7F) == 0,
	     "execution base must be 128-byte aligned (INITPC requirement)");

#define VPR_REG_PTR   ((NRF_VPR_Type *)(uintptr_t)VPR_BASE)
#define SRC_BASE_PTR  ((const uint8_t *)(uintptr_t)SRC_BASE)
#define EXEC_BASE_PTR ((uint8_t *)(uintptr_t)EXEC_BASE)

#endif /* FLPR_RUNTIME_NATIVE_TEST */

/* Stage 4A reset-order fix: one-variable DMCONTROL mask.
 * DMACTIVE stays Enabled through entire restart — never Disabled.
 * Only NDMRESET toggles: assert for preparation, release as final launch edge. */
#define DMCONTROL_RESET_ASSERT                                                                     \
	((VPR_DEBUGIF_DMCONTROL_NDMRESET_Active << VPR_DEBUGIF_DMCONTROL_NDMRESET_Pos) |           \
	 (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos))

#define DMCONTROL_RESET_RELEASE                                                                    \
	((VPR_DEBUGIF_DMCONTROL_NDMRESET_Inactive << VPR_DEBUGIF_DMCONTROL_NDMRESET_Pos) |         \
	 (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos))

/* ── Static state ────────────────────────────────────────────────── */

static NRF_VPR_Type *const vpr_reg = VPR_REG_PTR;

static K_MUTEX_DEFINE(runtime_lock);
static struct flpr_runtime_status g_rt_status;
static bool g_initialized;

/* ── Platform hooks ────────────────────────────────────────────────
 * Test mode routes busy wait, sleep, uptime, cache flush, and barrier
 * operations through test hooks so the restart body is deterministic on
 * native_sim and every side effect is observable.  Production builds
 * expand to the exact original calls. */

#if defined(FLPR_RUNTIME_NATIVE_TEST)

#define RT_BUSY_WAIT(us)     flpr_rt_hook_busy_wait(us)
#define RT_SLEEP(ms)         flpr_rt_hook_sleep(ms)
#define RT_UPTIME()          flpr_rt_hook_uptime_ms()
#define RT_CACHE_FLUSH(a, s) flpr_rt_hook_cache_flush(a, s)
#define RT_BARRIER_DSB()     flpr_rt_hook_barrier(FLPR_RT_BARRIER_DSB)
#define RT_BARRIER_ISB()     flpr_rt_hook_barrier(FLPR_RT_BARRIER_ISB)
#define RT_EMIT(ev)          flpr_rt_emit_event(ev)
#define RT_AFTER_COPY()      flpr_rt_hook_after_copy()

#else

#define RT_BUSY_WAIT(us)     k_busy_wait(us)
#define RT_SLEEP(ms)         k_msleep(ms)
#define RT_UPTIME()          k_uptime_get_32()
#define RT_CACHE_FLUSH(a, s) sys_cache_data_flush_range(a, s)
#define RT_BARRIER_DSB()     __DSB()
#define RT_BARRIER_ISB()     __ISB()
#define RT_EMIT(ev)                                                                                \
	do {                                                                                       \
	} while (0)
#define RT_AFTER_COPY()                                                                            \
	do {                                                                                       \
	} while (0)

#endif /* FLPR_RUNTIME_NATIVE_TEST */

/* ── Public API ──────────────────────────────────────────────────── */

int flpr_runtime_init(void)
{
	if (g_initialized) {
		return 0;
	}

	/* Verify VPR peripheral base accessible. */
#if defined(FLPR_RUNTIME_NATIVE_TEST)
	/* Test storage is a fixed host struct/array — never null. */
#else
	if (VPR_BASE == 0 || EXEC_BASE == 0 || SRC_BASE == 0) {
		LOG_ERR("FLPR runtime: invalid DT addresses");
		return -ENODEV;
	}
#endif

	memset(&g_rt_status, 0, sizeof(g_rt_status));
	g_rt_status.state = FLPR_RUNTIME_IDLE;
	g_initialized = true;

#if defined(FLPR_RUNTIME_NATIVE_TEST)
	LOG_INF("FLPR runtime init OK: vpr=%p src=%p(%zu) exec=%p(%zu)", (void *)VPR_REG_PTR,
		(const void *)SRC_BASE_PTR, (size_t)SRC_SIZE, (const void *)EXEC_BASE_PTR,
		(size_t)EXEC_SIZE);
#else
	LOG_INF("FLPR runtime init OK: vpr=0x%lx src=0x%lx(%zu) exec=0x%lx(%zu)",
		(unsigned long)VPR_BASE, (unsigned long)SRC_BASE, (size_t)SRC_SIZE,
		(unsigned long)EXEC_BASE, (size_t)EXEC_SIZE);
#endif

	return 0;
}

int flpr_runtime_restart(uint32_t timeout_ms)
{
	if (!g_initialized) {
		return -ENODEV;
	}

	/* ── Serialise via mutex ────────────────────────────── */
	if (k_mutex_lock(&runtime_lock, K_MSEC(timeout_ms)) != 0) {
		g_rt_status.busy_reject++;
		LOG_WRN("FLPR restart rejected: mutex busy");
		return -EBUSY;
	}

	g_rt_status.state = FLPR_RUNTIME_BUSY;
	g_rt_status.requests++;
	int ret = 0;
	uint32_t start_ms = RT_UPTIME();

	/* ── Stage 1: Snapshot previous handshake epoch ─────── */
	struct flpr_status hs_before;
	flpr_handshake_get_status(&hs_before);
	g_rt_status.previous_epoch = hs_before.epoch;
	RT_EMIT(FLPR_RT_EV_SNAPSHOT);

	LOG_INF("FLPR restart start: prev_epoch=%u", hs_before.epoch);

	/* ── Stage 1b: Snapshot source CRC BEFORE touching VPR ─ */
	uint32_t expected_crc = flpr_ring_crc32(SRC_BASE_PTR, (size_t)EXEC_SIZE);
	g_rt_status.source_crc = expected_crc;
	RT_EMIT(FLPR_RT_EV_SRC_CRC);

	/* ── Stage 2: Deregister CPUAPP IPC endpoint ───────── */
	g_rt_status.failed_stage = FLPR_STAGE_DISCONNECT;
	RT_EMIT(FLPR_RT_EV_DISCONNECT);
	ret = flpr_handshake_disconnect();
	if (ret < 0) {
		LOG_ERR("FLPR restart: disconnect failed: %d", ret);
		goto fail;
	}

	/* ── Stage 3: Stop VPR ──────────────────────────────── */
	g_rt_status.failed_stage = FLPR_STAGE_STOP;
	nrf_vpr_cpurun_set(vpr_reg, false);
	/* Wait for VPR pipeline to drain and bus activity to settle. */
	RT_BUSY_WAIT(1000);
	RT_EMIT(FLPR_RT_EV_STOP_CPURUN);

	/* ── Stage 4: Assert NDMRESET+DMACTIVE (held) ─────────
	 * One-variable mask: NDMRESET=Active, DMACTIVE=Enabled.
	 * Reset is HELD through preparation — not yet released. */
	g_rt_status.failed_stage = FLPR_STAGE_ASSERT_RESET;
	nrf_vpr_debugif_dmcontrol_mask_set(vpr_reg, DMCONTROL_RESET_ASSERT);
	RT_BUSY_WAIT(1000);
	RT_EMIT(FLPR_RT_EV_ASSERT_RESET);

	/* Readback after assert: record DMCONTROL and CPURUN state. */
	g_rt_status.readbacks.dmcontrol_after_assert = vpr_reg->DEBUGIF.DMCONTROL;
	g_rt_status.readbacks.cpurun_after_assert = nrf_vpr_cpurun_get(vpr_reg);

	/* ── Stage 5: Copy source → execution (reset held) ─── */
	g_rt_status.failed_stage = FLPR_STAGE_COPY;
	memcpy(EXEC_BASE_PTR, SRC_BASE_PTR, (size_t)EXEC_SIZE);
	g_rt_status.reload_bytes = EXEC_SIZE;
	RT_EMIT(FLPR_RT_EV_COPY);
	RT_AFTER_COPY();

	/* ── Stage 6: Cache flush + barrier (reset held) ───── */
	g_rt_status.failed_stage = FLPR_STAGE_FLUSH_BARRIER;
	RT_CACHE_FLUSH(EXEC_BASE_PTR, (size_t)EXEC_SIZE);
	RT_BARRIER_DSB(); /* full data synchronisation barrier */
	RT_BARRIER_ISB(); /* instruction synchronisation barrier */
	RT_EMIT(FLPR_RT_EV_CACHE_FLUSH_BARRIERS);

	/* ── Stage 7: CRC-32 execution, verify against source ─ */
	g_rt_status.failed_stage = FLPR_STAGE_CRC_VERIFY;
	g_rt_status.execution_crc = flpr_ring_crc32(EXEC_BASE_PTR, (size_t)EXEC_SIZE);
	RT_EMIT(FLPR_RT_EV_EXEC_CRC);

	if (expected_crc != g_rt_status.execution_crc) {
		LOG_ERR("FLPR restart: CRC mismatch: src=0x%08lx exec=0x%08lx",
			(unsigned long)expected_crc, (unsigned long)g_rt_status.execution_crc);
		ret = -EIO;
		goto fail;
	}

	/* ── Stage 8: Set INITPC (reset held) ──────────────── */
	g_rt_status.failed_stage = FLPR_STAGE_INITPC;
	nrf_vpr_initpc_set(vpr_reg, (uint32_t)(uintptr_t)EXEC_BASE_PTR);
	g_rt_status.readbacks.initpc_after_set = nrf_vpr_initpc_get(vpr_reg);
	RT_EMIT(FLPR_RT_EV_INITPC);

	/* ── Stage 9: Re-register IPC endpoint (reset held) ── */
	g_rt_status.failed_stage = FLPR_STAGE_RECONNECT;
	RT_EMIT(FLPR_RT_EV_RECONNECT);
	ret = flpr_handshake_reconnect();
	if (ret < 0) {
		LOG_ERR("FLPR restart: reconnect failed: %d", ret);
		goto fail;
	}

	/* ── Stage 10: Set CPURUN true (reset held) ──────────── */
	g_rt_status.failed_stage = FLPR_STAGE_START_CPURUN;
	nrf_vpr_cpurun_set(vpr_reg, true);
	g_rt_status.readbacks.cpurun_after_set = nrf_vpr_cpurun_get(vpr_reg);
	RT_EMIT(FLPR_RT_EV_SET_CPURUN);

	/* Readback before release: DMCONTROL with reset still asserted. */
	g_rt_status.readbacks.dmcontrol_before_release = vpr_reg->DEBUGIF.DMCONTROL;

	/* ── Stage 11: Release NDMRESET (DMACTIVE still Enabled) ─
	 * This is the final launch edge.  DMACTIVE never written Disabled. */
	g_rt_status.failed_stage = FLPR_STAGE_RELEASE_RESET;
	nrf_vpr_debugif_dmcontrol_mask_set(vpr_reg, DMCONTROL_RESET_RELEASE);
	RT_BUSY_WAIT(1000);
	g_rt_status.readbacks.dmcontrol_after_release = vpr_reg->DEBUGIF.DMCONTROL;
	RT_EMIT(FLPR_RT_EV_RELEASE_RESET);

	/* Give FLPR time to boot before waiting for bound. */
	RT_SLEEP(200);

	/* ── Stage 12: Wait bound ────────────────────────────── */
	g_rt_status.failed_stage = FLPR_STAGE_WAIT_BOUND;
	RT_EMIT(FLPR_RT_EV_WAIT_BOUND);
	ret = flpr_handshake_wait_bound(K_MSEC(timeout_ms / 2));
	if (ret != 0) {
		LOG_ERR("FLPR restart: wait bound timeout: %d", ret);
		goto fail_stop;
	}

	/* Wait new READY with different epoch. */
	g_rt_status.failed_stage = FLPR_STAGE_WAIT_READY;
	RT_EMIT(FLPR_RT_EV_WAIT_READY);
	ret = flpr_handshake_wait_new_ready(hs_before.epoch, K_MSEC(timeout_ms / 2));
	if (ret != 0) {
		LOG_ERR("FLPR restart: wait new ready timeout: %d", ret);
		goto fail_stop;
	}

	/* ── Stage 13: Success ─────────────────────────────── */
	g_rt_status.failed_stage = FLPR_STAGE_SUCCESS;
	{
		struct flpr_status hs_after;
		flpr_handshake_get_status(&hs_after);
		g_rt_status.new_epoch = hs_after.epoch;
	}

	uint32_t duration = RT_UPTIME() - start_ms;
	g_rt_status.total_duration_ms += duration;
	if (duration > g_rt_status.max_duration_ms) {
		g_rt_status.max_duration_ms = duration;
	}

	g_rt_status.success_count++;
	g_rt_status.last_errno = 0;
	g_rt_status.state = FLPR_RUNTIME_IDLE;
	RT_EMIT(FLPR_RT_EV_SUCCESS);
	LOG_INF("FLPR restart OK: epoch %u→%u crc=0x%08lx duration=%u ms", hs_before.epoch,
		g_rt_status.new_epoch, (unsigned long)expected_crc, duration);

	k_mutex_unlock(&runtime_lock);
	return 0;

fail_stop:
	/* Leave FLPR stopped but available for retry. */
	nrf_vpr_cpurun_set(vpr_reg, false);
	RT_EMIT(FLPR_RT_EV_FAILURE_STOP);
fail:
	g_rt_status.last_errno = ret;
	g_rt_status.fail_count++;
	g_rt_status.state = FLPR_RUNTIME_UNAVAILABLE;

	/* Failed attempts count toward cumulative and max duration —
	 * max_duration_ms documents the longest restart, failed or not. */
	uint32_t f_dur = RT_UPTIME() - start_ms;
	g_rt_status.total_duration_ms += f_dur;
	if (f_dur > g_rt_status.max_duration_ms) {
		g_rt_status.max_duration_ms = f_dur;
	}

	LOG_ERR("FLPR restart FAILED: stage=%d err=%d duration=%u ms",
		(int)g_rt_status.failed_stage, ret, f_dur);
	k_mutex_unlock(&runtime_lock);
	return ret;
}

void flpr_runtime_get_status(struct flpr_runtime_status *out)
{
	if (!out) {
		return;
	}
	k_mutex_lock(&runtime_lock, K_FOREVER);
	memcpy(out, &g_rt_status, sizeof(*out));
	k_mutex_unlock(&runtime_lock);
}

#if defined(FLPR_RUNTIME_NATIVE_TEST)

/* ── Test-only helpers ─────────────────────────────────────────────
 * Compiled only under FLPR_RUNTIME_NATIVE_TEST (native_sim suite).
 * Production builds contain none of these symbols. */

void flpr_runtime_test_reset(void)
{
	/* Release any mutex ownership left behind by a previous test. */
	while (k_mutex_unlock(&runtime_lock) == 0) {
	}

	g_initialized = false;
	memset(&g_rt_status, 0, sizeof(g_rt_status));
	g_rt_status.state = FLPR_RUNTIME_IDLE;
}

/* Mutex-busy simulation: a dedicated thread holds runtime_lock so the
 * calling (test) thread observes a genuine -EBUSY from restart(). */
static K_THREAD_STACK_DEFINE(rt_busy_stack, 1024);
static struct k_thread rt_busy_thread;
static K_SEM_DEFINE(rt_busy_held_sem, 0, 1);
static K_SEM_DEFINE(rt_busy_release_sem, 0, 1);

static void rt_busy_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_mutex_lock(&runtime_lock, K_FOREVER);
	k_sem_give(&rt_busy_held_sem);
	k_sem_take(&rt_busy_release_sem, K_FOREVER);
	k_mutex_unlock(&runtime_lock);
}

void flpr_runtime_test_hold_mutex(void)
{
	k_sem_reset(&rt_busy_release_sem);
	k_thread_create(&rt_busy_thread, rt_busy_stack, K_THREAD_STACK_SIZEOF(rt_busy_stack),
			rt_busy_thread_fn, NULL, NULL, NULL, K_PRIO_COOP(4), 0, K_NO_WAIT);
	k_sem_take(&rt_busy_held_sem, K_FOREVER);
}

void flpr_runtime_test_release_mutex(void)
{
	k_sem_give(&rt_busy_release_sem);
	k_thread_join(&rt_busy_thread, K_FOREVER);
}

#endif /* FLPR_RUNTIME_NATIVE_TEST */

#else /* !CONFIG_SOC_NRF54L15 && !FLPR_RUNTIME_NATIVE_TEST */

/* ── nRF5340 / other targets: compile-time stub ────────────────────── */

int flpr_runtime_init(void)
{
	return -ENOSYS;
}

int flpr_runtime_restart(uint32_t timeout_ms)
{
	(void)timeout_ms;
	return -ENOSYS;
}

void flpr_runtime_get_status(struct flpr_runtime_status *out)
{
	if (out) {
		memset(out, 0, sizeof(*out));
	}
}

#endif /* CONFIG_SOC_NRF54L15 */
