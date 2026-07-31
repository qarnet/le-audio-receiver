/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned hook implementation for the FLPR ring manager suite:
 * aligned host ring arrays and the test-controlled cycle counter.
 */
#include "flpr_ring_mgr_hooks.h"
/* Two aligned host arrays of FLPR_RING_TOTAL_SIZE each, matching the
 * production layout (input at base, output at base + one ring size). */
uint8_t flpr_ring_mgr_test_ring_mem[2U * FLPR_RING_TOTAL_SIZE]
	__attribute__((aligned(FLPR_RING_SLOT_ALIGN)));

uint8_t *flpr_ring_mgr_test_input_ring(void)
{
	return &flpr_ring_mgr_test_ring_mem[0];
}

uint8_t *flpr_ring_mgr_test_output_ring(void)
{
	return &flpr_ring_mgr_test_ring_mem[FLPR_RING_TOTAL_SIZE];
}

static uint32_t test_cycle;

uint32_t flpr_ring_mgr_test_cycle_get(void)
{
	return test_cycle;
}

void flpr_ring_mgr_test_set_cycle(uint32_t cycles)
{
	test_cycle = cycles;
}
