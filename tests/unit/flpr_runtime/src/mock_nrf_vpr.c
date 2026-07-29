/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock nRF VPR HAL for testing flpr_runtime restart sequence.
 * Records DMCONTROL writes, tracks CPURUN/INITPC state for verification.
 */
#include "mock_nrf_vpr.h"

#include <string.h>
#include "mock_state.h"

/* ── Public mock state ──────────────────────────────────────── */

mock_vpr_state_t mock_vpr;

void mock_reset(void)
{
	memset(&mock_vpr, 0, sizeof(mock_vpr));
}

void nrf_vpr_cpurun_set(NRF_VPR_Type *p_reg, bool enable)
{
	(void)p_reg;
	mock_vpr.cpurun = enable;
	mock_vpr.cpurun_set_count++;
}

bool nrf_vpr_cpurun_get(NRF_VPR_Type const *p_reg)
{
	(void)p_reg;
	mock_vpr.cpurun_get_count++;
	return mock_vpr.cpurun;
}

void nrf_vpr_initpc_set(NRF_VPR_Type *p_reg, uint32_t pc)
{
	(void)p_reg;
	mock_vpr.initpc = pc;
	mock_vpr.initpc_set_count++;
}

uint32_t nrf_vpr_initpc_get(NRF_VPR_Type const *p_reg)
{
	(void)p_reg;
	mock_vpr.initpc_get_count++;
	return mock_vpr.initpc;
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
	(void)p_reg;
	/* Record sequential DMCONTROL writes for order verification. */
	if (mock_vpr.dmcontrol_write_count < MOCK_MAX_DMCONTROL_WRITES) {
		mock_vpr.dmcontrol_writes[mock_vpr.dmcontrol_write_count] = mask;
		mock_vpr.dmcontrol_write_count++;
	}
}

bool nrf_vpr_debugif_dmcontrol_get(NRF_VPR_Type const *p_reg, int signal)
{
	(void)p_reg;
	(void)signal;
	mock_vpr.dmcontrol_get_count++;
	return false;
}
