/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Platform-specific cache/barrier hooks for shared-memory SPSC rings.
 *
 * Cache analysis (nRF54L15):
 *
 *   The ICACHE/CACHEDATA peripheral at 0x52004000 is documented as:
 *   "Both instruction and data accesses towards NVM memory are cached."
 *   NVM = flash / RRAM, accessed over the C-AHB (code) bus.
 *
 *   SRAM at 0x20000000..0x20040000 lives on the S-AHB (system) bus
 *   and is NOT cached by this peripheral.  Memory-type documentation:
 *   - S-AHB Cortex-M33 MPU region attributes: Shareable Normal (not
 *     Device).  Generated MPU config from build/zephyr/.../build_info
 *     confirms SRAM regions use Normal Write-Back No-Allocate for
 *     the S-AHB bus (ATTR=0x3, SH=1).  However the CACHEDATA
 *     accelerator only operates on C-AHB addresses; it does not
 *     snoop the S-AHB bus.  SRAM writes are write-through in
 *     practice (no dirty lines to evict).
 *   - CONFIG_DCACHE is NOT set in nRF54L15 cpuapp .config.  The
 *     Zephyr cache driver only provides I-cache functions.
 *   - nRF54L15 FLPR (RISC-V RV32E) has NO data or instruction cache.
 *
 *   Conclusion: no explicit D-cache maintenance needed.  The ordering
 *   requirement is satisfied by architecture fences:
 *   - CPUAPP (ARM): barrier_dsync_fence_full() → __DSB() data-sync.
 *     barrrier_dmem_fence_full() → __DMB() for pure memory-ordering.
 *   - FLPR (RISC-V): BARRIER_OPERATIONS_BUILTIN is selected, so
 *     barrier_dmem_fence_full() → __atomic_thread_fence(__ATOMIC_SEQ_CST).
 *     This emits a full hw fence (fence rw,rw on RV32) plus compiler
 *     barrier — sufficient even for the in-order VPR pipeline.
 *
 * References:
 *   - Nordic nRF54L15 OPS v1.0: §5.6.1 CACHEDATA — C-AHB only.
 *   - Zephyr include/zephyr/sys/barrier.h — BARRIER_OPERATIONS_*
 *     derivation for ARM (arch) and RISC-V (builtin).
 *   - Generated build/zephyr/.config — CONFIG_DCACHE=n confirmed.
 *   - Generated build/zephyr/arch/arm/core/aarch32/cortex_m/mpu/arm_mpu_regions.c
 *     for the runtime MPU configuration.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include "flpr_ring.h"

/* ── CPUAPP (ARM Cortex-M33, S-AHB SRAM: no D-cache) ────────────── */
#if defined(CONFIG_SOC_NRF54L15) && !defined(CONFIG_SOC_NRF54L15_CPUFLPR)

void flpr_cache_write_barrier(void)
{
	/* ARM DMB: data memory barrier — ensures prior stores complete
	 * before subsequent stores are visible to other observers. */
	barrier_dmem_fence_full();
}

void flpr_cache_read_barrier(void)
{
	/* ARM DMB: data memory barrier — ensures prior loads complete
	 * before subsequent loads start. */
	barrier_dmem_fence_full();
}

void flpr_cache_full_barrier(void)
{
	/* ARM DSB: data synchronization barrier — all prior data accesses
	 * complete before any subsequent instruction executes. */
	barrier_dsync_fence_full();
}

/* ── FLPR (RISC-V VPR, no cache at all) ─────────────────────────── */
#elif defined(CONFIG_SOC_NRF54L15_CPUFLPR)

void flpr_cache_write_barrier(void)
{
	/* RISC-V: BARRIER_OPERATIONS_BUILTIN →
	 * __atomic_thread_fence(__ATOMIC_SEQ_CST) →
	 * on RV32: fence rw,rw + compiler barrier.
	 * Ensures store ordering even on in-order pipeline. */
	barrier_dmem_fence_full();
}

void flpr_cache_read_barrier(void)
{
	/* Same as write: seq-cst fence covers both. */
	barrier_dmem_fence_full();
}

void flpr_cache_full_barrier(void)
{
	/* Seq-cst fence covers load+store ordering. */
	barrier_dmem_fence_full();
}

/* ── Host / unit test (native_sim) ──────────────────────────────── */
#else

#include <stdatomic.h>

void flpr_cache_write_barrier(void)
{
	atomic_thread_fence(memory_order_release);
}

void flpr_cache_read_barrier(void)
{
	atomic_thread_fence(memory_order_acquire);
}

void flpr_cache_full_barrier(void)
{
	atomic_thread_fence(memory_order_seq_cst);
}

#endif
