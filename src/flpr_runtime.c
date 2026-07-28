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
 * Exact restart sequence (matches handoff):
 *   1. Snapshot previous handshake epoch; mark manager busy.
 *   2. Deregister CPUAPP IPC endpoint.
 *   3. nrf_vpr_cpurun_set(vpr, false).
 *   4. Pulse NDMRESET via debugif DMCONTROL mask (sQSPI pattern).
 *   5. Copy exec-size bytes from source to execution SRAM.
 *   6. Flush copied range (sys_cache_data_flush_range) + full barrier.
 *   7. CRC-32 source + execution over copied bytes; require equality.
 *   8. Set INITPC to execution base (128-byte aligned).
 *   9. Re-register CPUAPP IPC endpoint before releasing core.
 *  10. nrf_vpr_cpurun_set(vpr, true).
 *  11. Wait bound, then READY+ACK with epoch different from snapshot.
 *  12. Return success; on failure leave FLPR unavailable/stopped,
 *      report exact stage/error. Never reboot CPUAPP.
 *
 * No raw register writes — all through nrfx HAL.
 * No heap — all structs are static.
 * Mutex-serialised sync API. Shell/dedicated thread only.
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

#if defined(CONFIG_SOC_NRF54L15)

#include "flpr_handshake.h"
#include "flpr_ring.h" /* for flpr_ring_crc32 */
#include "audio_offload.h"

#include <hal/nrf_vpr.h>

LOG_MODULE_REGISTER(flpr_rt, LOG_LEVEL_INF);

/* ── DT-derived constants ───────────────────────────────────────── */

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

/* DEBUGIF DMCONTROL mask values for NDMRESET pulse.
 * Matches nrfxlib sQSPI reset sequence:
 *   1. Set NDMRESET active, DMACTIVE enabled.
 *   2. Set NDMRESET inactive, DMACTIVE disabled. */
#define DMCONTROL_NDMRESET_ACTIVE                                                                  \
	((VPR_DEBUGIF_DMCONTROL_NDMRESET_Active << VPR_DEBUGIF_DMCONTROL_NDMRESET_Pos) |           \
	 (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos))

#define DMCONTROL_NDMRESET_INACTIVE                                                                \
	((VPR_DEBUGIF_DMCONTROL_NDMRESET_Inactive << VPR_DEBUGIF_DMCONTROL_NDMRESET_Pos) |         \
	 (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Disabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos))

/* ── Static state ────────────────────────────────────────────────── */

static NRF_VPR_Type *const vpr_reg = (NRF_VPR_Type *)(uintptr_t)VPR_BASE;

static K_MUTEX_DEFINE(runtime_lock);
static struct flpr_runtime_status g_rt_status;
static bool g_initialized;

/* ── Public API ──────────────────────────────────────────────────── */

int flpr_runtime_init(void)
{
	if (g_initialized) {
		return 0;
	}

	/* Verify VPR peripheral base accessible. */
	if (VPR_BASE == 0 || EXEC_BASE == 0 || SRC_BASE == 0) {
		LOG_ERR("FLPR runtime: invalid DT addresses");
		return -ENODEV;
	}

	memset(&g_rt_status, 0, sizeof(g_rt_status));
	g_rt_status.state = FLPR_RUNTIME_IDLE;
	g_initialized = true;

	LOG_INF("FLPR runtime init OK: vpr=0x%lx src=0x%lx(%zu) exec=0x%lx(%zu)",
		(unsigned long)VPR_BASE, (unsigned long)SRC_BASE, (size_t)SRC_SIZE,
		(unsigned long)EXEC_BASE, (size_t)EXEC_SIZE);

	return 0;
}

int flpr_runtime_restart(uint32_t timeout_ms)
{
	if (!g_initialized) {
		return -ENODEV;
	}

	/* ── Guard: reject if offload stream active ─────────── */
	if (audio_offload_is_healthy()) {
		g_rt_status.busy_reject++;
		LOG_WRN("FLPR restart rejected: offload stream active");
		return -EBUSY;
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
	uint32_t start_ms = k_uptime_get_32();

	/* ── Stage 1: Snapshot previous handshake epoch ─────── */
	struct flpr_status hs_before;
	flpr_handshake_get_status(&hs_before);
	g_rt_status.previous_epoch = hs_before.epoch;

	LOG_INF("FLPR restart start: prev_epoch=%u", hs_before.epoch);

	/* ── Stage 1b: Snapshot source CRC BEFORE touching VPR ─ */
	uint32_t expected_crc =
		flpr_ring_crc32((const uint8_t *)(uintptr_t)SRC_BASE, (size_t)EXEC_SIZE);
	g_rt_status.source_crc = expected_crc;

	/* ── Stage 2: Deregister CPUAPP IPC endpoint ───────── */
	ret = flpr_handshake_disconnect();
	if (ret < 0) {
		LOG_ERR("FLPR restart: disconnect failed: %d", ret);
		goto fail;
	}

	/* ── Stage 3: Stop VPR ──────────────────────────────── */
	nrf_vpr_cpurun_set(vpr_reg, false);
	/* Wait for VPR pipeline to drain and bus activity to settle. */
	k_busy_wait(1000);

	/* ── Stage 4: Pulse NDMRESET (sQSPI pattern) ─────────
	 * Resets the VPR core (RISC-V hart + internal state)
	 * while leaving the VPR peripheral registers intact.
	 * First apply reset, then release. */
	nrf_vpr_debugif_dmcontrol_mask_set(vpr_reg, DMCONTROL_NDMRESET_ACTIVE);
	k_busy_wait(1000);
	nrf_vpr_debugif_dmcontrol_mask_set(vpr_reg, DMCONTROL_NDMRESET_INACTIVE);
	/* Allow reset to propagate through the VPR subsystem. */
	k_busy_wait(1000);

	/* ── Stage 5: Copy source → execution ──────────────── */
	memcpy((void *)(uintptr_t)EXEC_BASE, (const void *)(uintptr_t)SRC_BASE, (size_t)EXEC_SIZE);
	g_rt_status.reload_bytes = EXEC_SIZE;

	/* ── Stage 6: Cache flush + barrier ────────────────── */
	sys_cache_data_flush_range((void *)(uintptr_t)EXEC_BASE, (size_t)EXEC_SIZE);
	__DSB(); /* full data synchronisation barrier */
	__ISB(); /* instruction synchronisation barrier */

	/* ── Stage 7: CRC-32 execution, verify against source ─ */
	g_rt_status.execution_crc =
		flpr_ring_crc32((const uint8_t *)(uintptr_t)EXEC_BASE, (size_t)EXEC_SIZE);

	if (expected_crc != g_rt_status.execution_crc) {
		LOG_ERR("FLPR restart: CRC mismatch: src=0x%08lx exec=0x%08lx",
			(unsigned long)expected_crc, (unsigned long)g_rt_status.execution_crc);
		ret = -EIO;
		goto fail;
	}

	/* ── Stage 8: Set INITPC ───────────────────────────── */
	nrf_vpr_initpc_set(vpr_reg, EXEC_BASE);

	/* ── Stage 9: Re-register IPC endpoint ─────────────── */
	ret = flpr_handshake_reconnect();
	if (ret < 0) {
		LOG_ERR("FLPR restart: reconnect failed: %d", ret);
		goto fail;
	}

	/* ── Stage 10: Start VPR ───────────────────────────── */
	nrf_vpr_cpurun_set(vpr_reg, true);

	/* Give FLPR time to boot before waiting for bound. */
	k_msleep(200);

	/* ── Stage 11: Wait bound ──────────────────────────── */
	ret = flpr_handshake_wait_bound(K_MSEC(timeout_ms / 2));
	if (ret != 0) {
		LOG_ERR("FLPR restart: wait bound timeout: %d", ret);
		goto fail_stop;
	}

	/* Wait new READY with different epoch. */
	ret = flpr_handshake_wait_new_ready(hs_before.epoch, K_MSEC(timeout_ms / 2));
	if (ret != 0) {
		LOG_ERR("FLPR restart: wait new ready timeout: %d", ret);
		goto fail_stop;
	}

	/* ── Stage 12: Success ─────────────────────────────── */
	{
		struct flpr_status hs_after;
		flpr_handshake_get_status(&hs_after);
		g_rt_status.new_epoch = hs_after.epoch;
	}

	uint32_t duration = k_uptime_get_32() - start_ms;
	g_rt_status.total_duration_ms += duration;
	if (duration > g_rt_status.max_duration_ms) {
		g_rt_status.max_duration_ms = duration;
	}

	g_rt_status.success_count++;
	g_rt_status.last_errno = 0;
	g_rt_status.state = FLPR_RUNTIME_IDLE;
	LOG_INF("FLPR restart OK: epoch %u→%u crc=0x%08lx duration=%u ms", hs_before.epoch,
		g_rt_status.new_epoch, (unsigned long)expected_crc, duration);

	k_mutex_unlock(&runtime_lock);
	return 0;

fail_stop:
	/* Leave FLPR stopped but available for retry. */
	nrf_vpr_cpurun_set(vpr_reg, false);
fail:
	g_rt_status.last_errno = ret;
	g_rt_status.fail_count++;
	g_rt_status.state = FLPR_RUNTIME_UNAVAILABLE;

	uint32_t f_dur = k_uptime_get_32() - start_ms;
	g_rt_status.total_duration_ms += f_dur;

	LOG_ERR("FLPR restart FAILED: stage err=%d duration=%u ms", ret, f_dur);
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

#else /* !CONFIG_SOC_NRF54L15 */

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
