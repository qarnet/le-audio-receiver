/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned hook interface for the FLPR runtime restart suite.
 *
 * Compiled into src/flpr_runtime.c ONLY under FLPR_RUNTIME_NATIVE_TEST
 * (native_sim).  Production builds never include this header and contain
 * no test symbols.
 *
 * native_sim has no MMU/address translation, so the fixed devicetree reg
 * addresses (VPR 0x21000000-ish, execution SRAM 0x20030000) cannot be
 * dereferenced.  This hook header replaces them with host storage:
 *   - an NRF_VPR_Type register file (shadow-HAL pattern),
 *   - source and execution byte arrays of equal fixed size,
 *   - hooks for busy wait, sleep, uptime, cache flush, barriers, and
 *     after-copy fault injection,
 *   - an ordered event log emitted by the production restart body.
 */
#ifndef FLPR_RUNTIME_HOOKS_H_
#define FLPR_RUNTIME_HOOKS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <hal/nrf_vpr.h> /* shadow mock HAL type */

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed image size shared by the source and execution arrays. */
#define FLPR_RT_TEST_IMAGE_SIZE 4096U

/* Test-provided VPR register storage. */
extern NRF_VPR_Type flpr_rt_test_vpr;

/* Test-provided source and execution byte arrays (equal fixed size). */
extern uint8_t flpr_rt_test_source[FLPR_RT_TEST_IMAGE_SIZE];
extern uint8_t flpr_rt_test_exec[FLPR_RT_TEST_IMAGE_SIZE];

/* ── Ordered restart events ─────────────────────────────────────────
 * Emitted by the actual production restart function (test mode only).
 * Events observe real control flow; they never drive it. */

enum flpr_rt_test_event {
	FLPR_RT_EV_SNAPSHOT = 0,         /* 1: snapshot status/epoch */
	FLPR_RT_EV_SRC_CRC,              /* 2: source CRC */
	FLPR_RT_EV_DISCONNECT,           /* 3: disconnect (stage reached) */
	FLPR_RT_EV_STOP_CPURUN,          /* 4: stop CPURUN */
	FLPR_RT_EV_ASSERT_RESET,         /* 5: assert reset */
	FLPR_RT_EV_COPY,                 /* 6: copy */
	FLPR_RT_EV_CACHE_FLUSH_BARRIERS, /* 7: cache flush + barriers */
	FLPR_RT_EV_EXEC_CRC,             /* 8: execution CRC */
	FLPR_RT_EV_INITPC,               /* 9: INITPC */
	FLPR_RT_EV_RECONNECT,            /* 10: reconnect (stage reached) */
	FLPR_RT_EV_SET_CPURUN,           /* 11: set CPURUN */
	FLPR_RT_EV_RELEASE_RESET,        /* 12: release reset */
	FLPR_RT_EV_WAIT_BOUND,           /* 13: wait bound (stage reached) */
	FLPR_RT_EV_WAIT_READY,           /* 14: wait new READY (stage reached) */
	FLPR_RT_EV_SUCCESS,              /* 15: success */
	FLPR_RT_EV_FAILURE_STOP,         /* 16: failure stop */
};

/* Barrier kinds routed through the hook. */
enum flpr_rt_test_barrier {
	FLPR_RT_BARRIER_DSB = 0,
	FLPR_RT_BARRIER_ISB,
};

/* ── Hooks invoked by the production restart body (test mode) ────── */

void flpr_rt_emit_event(enum flpr_rt_test_event ev);
void flpr_rt_hook_busy_wait(uint32_t usec);
void flpr_rt_hook_sleep(uint32_t ms);
uint32_t flpr_rt_hook_uptime_ms(void);
void flpr_rt_hook_cache_flush(const void *addr, size_t size);
void flpr_rt_hook_barrier(enum flpr_rt_test_barrier kind);
void flpr_rt_hook_after_copy(void);

/* ── Test controls ───────────────────────────────────────────────── */

/* Full cleanup: event log, hook counters, VPR storage, arrays, and
 * production-side state (via flpr_runtime_test_reset()). */
void flpr_rt_test_reset_all(void);

/* Production-side reset + mutex-busy simulation.  Implemented inside
 * src/flpr_runtime.c under FLPR_RUNTIME_NATIVE_TEST (module statics). */
void flpr_runtime_test_reset(void);
void flpr_runtime_test_hold_mutex(void);
void flpr_runtime_test_release_mutex(void);

/* ── Observation accessors ───────────────────────────────────────── */

uint32_t flpr_rt_test_event_count(void);
enum flpr_rt_test_event flpr_rt_test_event_at(uint32_t i);
uint32_t flpr_rt_test_cache_flush_count(void);
uint32_t flpr_rt_test_barrier_count(enum flpr_rt_test_barrier kind);
uint32_t flpr_rt_test_busy_wait_count(void);
uint32_t flpr_rt_test_sleep_count(void);

/* Uptime control: sets the current test uptime.  The sleep hook advances
 * it by its (possibly overridden) amount. */
void flpr_rt_test_set_uptime(uint32_t ms);
void flpr_rt_test_set_sleep_advance(uint32_t ms); /* 0 = advance by requested amount */

/* After-copy fault injection: when enabled the after-copy hook corrupts
 * the execution image so the execution CRC mismatches. */
void flpr_rt_test_set_corrupt_after_copy(bool corrupt);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_RUNTIME_HOOKS_H_ */
