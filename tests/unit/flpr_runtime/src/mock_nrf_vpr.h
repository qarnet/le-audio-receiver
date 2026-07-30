/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock nRF VPR HAL for unit testing flpr_runtime restart sequence.
 * Provides minimal types and inline functions matching nrfx hal/nrf_vpr.h.
 */
#ifndef MOCK_NRF_VPR_H_
#define MOCK_NRF_VPR_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Mock VPR register structure ──────────────────────────── */

typedef struct {
	volatile uint32_t DMCONTROL;
} VPR_DEBUGIF_Type;

typedef struct {
	volatile uint32_t CPURUN;
	volatile uint32_t INITPC;
	volatile uint32_t EVENTS_TRIGGERED[32];
	volatile uint32_t TASKS_TRIGGER[32];
	volatile uint32_t INTENSET;
	volatile uint32_t INTENCLR;
	VPR_DEBUGIF_Type DEBUGIF;
} NRF_VPR_Type;

/* ── CPURUN values ─────────────────────────────────────────── */

#define VPR_CPURUN_EN_Msk     (1UL)
#define VPR_CPURUN_EN_Pos     0
#define VPR_CPURUN_EN_Running (1UL)
#define VPR_CPURUN_EN_Stopped (0UL)

/* ── DMCONTROL field positions and values ──────────────────── */

#define VPR_DEBUGIF_DMCONTROL_DMACTIVE_Msk      (1UL << 0)
#define VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos      0
#define VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled  (1UL)
#define VPR_DEBUGIF_DMCONTROL_DMACTIVE_Disabled (0UL)

#define VPR_DEBUGIF_DMCONTROL_NDMRESET_Msk      (1UL << 1)
#define VPR_DEBUGIF_DMCONTROL_NDMRESET_Pos      1
#define VPR_DEBUGIF_DMCONTROL_NDMRESET_Active   (1UL)
#define VPR_DEBUGIF_DMCONTROL_NDMRESET_Inactive (0UL)

/* ── VPR HAL prototypes ────────────────────────────────────── */

void nrf_vpr_cpurun_set(NRF_VPR_Type *p_reg, bool enable);
bool nrf_vpr_cpurun_get(NRF_VPR_Type const *p_reg);
void nrf_vpr_initpc_set(NRF_VPR_Type *p_reg, uint32_t pc);
uint32_t nrf_vpr_initpc_get(NRF_VPR_Type const *p_reg);
void nrf_vpr_debugif_dmcontrol_set(NRF_VPR_Type *p_reg, int signal, bool enable);
void nrf_vpr_debugif_dmcontrol_mask_set(NRF_VPR_Type *p_reg, uint32_t mask);
bool nrf_vpr_debugif_dmcontrol_get(NRF_VPR_Type const *p_reg, int signal);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_NRF_VPR_H_ */
