/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Platform-specific cache/barrier hooks for shared-memory SPSC rings.
 *
 * On nRF54L15 CPUAPP: SRAM (0x20000000+) is on the S-AHB system bus,
 * NOT cached by the ICACHE/CACHEDATA peripheral (which only caches the
 * C-AHB code bus for NVM/RRAM). Explicit D-cache maintenance is NOT
 * needed. Only __DSB() data synchronization barriers are required for
 * ordering between the producer and consumer cores.
 *
 * On FLPR: no cache, no barriers needed (RV32E in-order pipeline with
 * no speculative loads across device-type memory boundaries).
 *
 * See AGENTS.md §"SDC/MPSL owns RADIO" and nRF54L15 datasheet:
 * "Both instruction and data accesses towards NVM memory are cached."
 * SRAM is not NVM — the system bus (S-AHB) bypasses the cache.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include "flpr_ring.h"

/* ── CPUAPP (ARM Cortex-M33, S-AHB SRAM: no D-cache) ────────────── */
#if defined(CONFIG_SOC_NRF54L15) && !defined(CONFIG_SOC_NRF54L15_CPUFLPR)

void flpr_cache_flush_range(void *addr, size_t size)
{
	/* SRAM is uncached — no flush needed.
	 * Only a barrier to ensure prior CPU stores are observable
	 * by other bus masters. */
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
	barrier_dsync_fence_full();
}

void flpr_cache_invld_range(void *addr, size_t size)
{
	/* SRAM is uncached — no invalidate needed.
	 * Barrier ensures subsequent loads see current memory state. */
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
	barrier_dsync_fence_full();
}

void flpr_cache_write_barrier(void)
{
	barrier_dsync_fence_full();
}

void flpr_cache_read_barrier(void)
{
	barrier_dsync_fence_full();
}

/* ── FLPR (RISC-V VPR, no cache at all) ─────────────────────────── */
#elif defined(CONFIG_SOC_NRF54L15_CPUFLPR)

void flpr_cache_flush_range(void *addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
	/* No cache, no barriers needed. */
}

void flpr_cache_invld_range(void *addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
}

void flpr_cache_write_barrier(void)
{
}

void flpr_cache_read_barrier(void)
{
}

/* ── Host / unit test (native_sim) ──────────────────────────────── */
#else

/* Generic: memory barriers via compiler fence + host-appropriate
 * barrier. On a simulated single-threaded host, compiler barrier
 * is sufficient for unit tests. */
#include <stdatomic.h>

void flpr_cache_flush_range(void *addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
	atomic_thread_fence(memory_order_release);
}

void flpr_cache_invld_range(void *addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
	atomic_thread_fence(memory_order_acquire);
}

void flpr_cache_write_barrier(void)
{
	atomic_thread_fence(memory_order_release);
}

void flpr_cache_read_barrier(void)
{
	atomic_thread_fence(memory_order_acquire);
}

#endif
