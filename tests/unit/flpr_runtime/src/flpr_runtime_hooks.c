/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned hook implementation for the FLPR runtime restart suite.
 * Provides host arrays, the event log, and time/cache/barrier hooks used
 * by the production restart body under FLPR_RUNTIME_NATIVE_TEST.
 */
#include "flpr_runtime_hooks.h"

#include <string.h>

/* ── Test storage ─────────────────────────────────────────────── */

NRF_VPR_Type flpr_rt_test_vpr;
uint8_t flpr_rt_test_source[FLPR_RT_TEST_IMAGE_SIZE];
uint8_t flpr_rt_test_exec[FLPR_RT_TEST_IMAGE_SIZE];

/* ── Event log ────────────────────────────────────────────────── */

#define FLPR_RT_TEST_MAX_EVENTS 64U

static uint32_t ev_count;
static enum flpr_rt_test_event ev_log[FLPR_RT_TEST_MAX_EVENTS];

void flpr_rt_emit_event(enum flpr_rt_test_event ev)
{
	if (ev_count < FLPR_RT_TEST_MAX_EVENTS) {
		ev_log[ev_count] = ev;
	}
	ev_count++;
}

/* ── Hook counters and time state ─────────────────────────────── */

static uint32_t cache_flush_count;
static uint32_t barrier_counts[2];
static uint32_t busy_wait_count;
static uint32_t sleep_count;
static uint32_t test_uptime_ms;
static uint32_t sleep_advance_override;
static bool corrupt_after_copy;

void flpr_rt_hook_busy_wait(uint32_t usec)
{
	(void)usec;
	busy_wait_count++;
}

void flpr_rt_hook_sleep(uint32_t ms)
{
	sleep_count++;
	test_uptime_ms += (sleep_advance_override != 0U) ? sleep_advance_override : ms;
}

uint32_t flpr_rt_hook_uptime_ms(void)
{
	return test_uptime_ms;
}

void flpr_rt_hook_cache_flush(const void *addr, size_t size)
{
	(void)addr;
	(void)size;
	cache_flush_count++;
}

void flpr_rt_hook_barrier(enum flpr_rt_test_barrier kind)
{
	if (kind == FLPR_RT_BARRIER_DSB || kind == FLPR_RT_BARRIER_ISB) {
		barrier_counts[kind]++;
	}
}

void flpr_rt_hook_after_copy(void)
{
	if (corrupt_after_copy) {
		flpr_rt_test_exec[0] ^= 0xFFU;
	}
}

/* ── Test controls ────────────────────────────────────────────── */

void flpr_rt_test_reset_all(void)
{
	ev_count = 0;
	cache_flush_count = 0;
	barrier_counts[0] = 0;
	barrier_counts[1] = 0;
	busy_wait_count = 0;
	sleep_count = 0;
	test_uptime_ms = 0;
	sleep_advance_override = 0;
	corrupt_after_copy = false;

	memset(&flpr_rt_test_vpr, 0, sizeof(flpr_rt_test_vpr));
	memset(flpr_rt_test_source, 0, sizeof(flpr_rt_test_source));
	memset(flpr_rt_test_exec, 0, sizeof(flpr_rt_test_exec));

	flpr_runtime_test_reset();
}

/* ── Observation accessors ────────────────────────────────────── */

uint32_t flpr_rt_test_event_count(void)
{
	return ev_count;
}

enum flpr_rt_test_event flpr_rt_test_event_at(uint32_t i)
{
	return (i < ev_count) ? ev_log[i] : (enum flpr_rt_test_event) - 1;
}

uint32_t flpr_rt_test_cache_flush_count(void)
{
	return cache_flush_count;
}

uint32_t flpr_rt_test_barrier_count(enum flpr_rt_test_barrier kind)
{
	return (kind == FLPR_RT_BARRIER_DSB || kind == FLPR_RT_BARRIER_ISB) ? barrier_counts[kind]
									    : 0U;
}

uint32_t flpr_rt_test_busy_wait_count(void)
{
	return busy_wait_count;
}

uint32_t flpr_rt_test_sleep_count(void)
{
	return sleep_count;
}

void flpr_rt_test_set_uptime(uint32_t ms)
{
	test_uptime_ms = ms;
}

void flpr_rt_test_set_sleep_advance(uint32_t ms)
{
	sleep_advance_override = ms;
}

void flpr_rt_test_set_corrupt_after_copy(bool corrupt)
{
	corrupt_after_copy = corrupt;
}
