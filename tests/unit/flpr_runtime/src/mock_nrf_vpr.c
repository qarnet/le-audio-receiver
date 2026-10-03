/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shadow nRF VPR HAL for the FLPR runtime restart suite (native_sim).
 *
 * Mirrors the register-file layout and field constants of the real
 * nrfx hal/nrf_vpr.h + mdk nrf_vpr.h so the production restart body
 * compiles unchanged.  HAL calls write THROUGH to the passed register
 * struct (the test-owned NRF_VPR_Type storage) so production readbacks
 * (DMCONTROL, CPURUN, INITPC) observe consistent state, and record
 * call counts/write history for verification.
 */
#include "mock_nrf_vpr.h"

#include <string.h>
#include "mock_state.h"
#include "flpr_runtime_hooks.h"
#include <hal/nrf_memconf.h>

/* ── Public mock state ──────────────────────────────────────── */

mock_vpr_state_t mock_vpr;
NRF_MEMCONF_Type flpr_rt_test_memconf;
static bool restoring_context;
static bool rtperiph_up;
static bool fail_interrupt_rearm;
static bool fail_context_selection;

void mock_reset(void)
{
	memset(&mock_vpr, 0, sizeof(mock_vpr));
	memset(&flpr_rt_test_memconf, 0xff, sizeof(flpr_rt_test_memconf));
	flpr_rt_test_vpr.INTENSET = 1U << 20;
	restoring_context = false;
	rtperiph_up = true;
	fail_interrupt_rearm = false;
	fail_context_selection = false;
}

void nrf_vpr_cpurun_set(NRF_VPR_Type *p_reg, bool enable)
{
	if (enable) {
		p_reg->CPURUN |= VPR_CPURUN_EN_Msk;
	} else {
		p_reg->CPURUN &= ~VPR_CPURUN_EN_Msk;
	}
	mock_vpr.cpurun = enable;
	mock_vpr.cpurun_set_count++;
}

bool nrf_vpr_cpurun_get(NRF_VPR_Type const *p_reg)
{
	mock_vpr.cpurun_get_count++;
	return (p_reg->CPURUN & VPR_CPURUN_EN_Msk) != 0;
}

void nrf_vpr_initpc_set(NRF_VPR_Type *p_reg, uint32_t pc)
{
	p_reg->INITPC = pc;
	mock_vpr.initpc = pc;
	mock_vpr.initpc_set_count++;
}

uint32_t nrf_vpr_initpc_get(NRF_VPR_Type const *p_reg)
{
	mock_vpr.initpc_get_count++;
	return p_reg->INITPC;
}

void nrf_vpr_debugif_dmcontrol_set(NRF_VPR_Type *p_reg, int signal, bool enable)
{
	(void)p_reg;
	(void)signal;
	(void)enable;
	mock_vpr.dmcontrol_set_count++;
}

void nrf_vpr_debugif_dmcontrol_mask_set(NRF_VPR_Type *p_reg, uint32_t mask)
{
	/* Write through to the register file: readbacks in the production
	 * restart body observe exactly the mask written. */
	p_reg->DEBUGIF.DMCONTROL = mask;
	if (mask & VPR_DEBUGIF_DMCONTROL_NDMRESET_Msk) {
		restoring_context = (flpr_rt_test_memconf.POWER[1].RET & 1U) != 0U;
		rtperiph_up = false;
		p_reg->INTENSET = 0U;
	} else if ((p_reg->CPURUN & VPR_CPURUN_EN_Msk) && !restoring_context) {
		rtperiph_up = true;
	}

	/* Record sequential DMCONTROL writes for order verification. */
	if (mock_vpr.dmcontrol_write_count < MOCK_MAX_DMCONTROL_WRITES) {
		mock_vpr.dmcontrol_writes[mock_vpr.dmcontrol_write_count] = mask;
		mock_vpr.dmcontrol_write_count++;
	}
}

void nrf_vpr_int_enable(NRF_VPR_Type *p_reg, uint32_t mask)
{
	if (!fail_interrupt_rearm) {
		p_reg->INTENSET |= mask;
	}
}

uint32_t nrf_vpr_int_enable_check(NRF_VPR_Type const *p_reg, uint32_t mask)
{
	return rtperiph_up ? p_reg->INTENSET & mask : 0U;
}

bool mock_vpr_fresh_boot_running(void)
{
	return rtperiph_up && (flpr_rt_test_vpr.CPURUN & VPR_CPURUN_EN_Msk) &&
	       !(flpr_rt_test_vpr.DEBUGIF.DMCONTROL & VPR_DEBUGIF_DMCONTROL_NDMRESET_Msk);
}

void mock_vpr_fail_interrupt_rearm(bool fail)
{
	fail_interrupt_rearm = fail;
}

void mock_vpr_fail_context_selection(bool fail)
{
	fail_context_selection = fail;
}

void nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF_Type *reg, uint8_t power, uint8_t block,
					 bool enable)
{
	if (!enable && fail_context_selection) {
		return;
	}
	if (enable) {
		reg->POWER[power].RET |= 1U << block;
	} else {
		reg->POWER[power].RET &= ~(1U << block);
	}
}

bool nrf_memconf_ramblock_ret_enable_check(NRF_MEMCONF_Type const *reg, uint8_t power,
					   uint8_t block)
{
	return (reg->POWER[power].RET & (1U << block)) != 0U;
}

bool nrf_vpr_debugif_dmcontrol_get(NRF_VPR_Type const *p_reg, int signal)
{
	(void)p_reg;
	(void)signal;
	mock_vpr.dmcontrol_get_count++;
	return false;
}
