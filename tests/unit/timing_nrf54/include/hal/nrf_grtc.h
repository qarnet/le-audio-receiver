/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned shadow of the installed NCS v3.3.0 <hal/nrf_grtc.h>.
 * Declares exactly the API surface used by src/audio_timing_nrf54.c;
 * the mock implementation lives in src/mock_hal.c.  Never part of a
 * production build.
 */

#ifndef NRF_GRTC_H__
#define NRF_GRTC_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t _reserved;
} NRF_GRTC_Type;

typedef enum {
	NRF_GRTC_EVENT_COMPARE_0 = 0,
} nrf_grtc_event_t;

#define NRF_GRTC ((NRF_GRTC_Type *)0x4000F000U)

void nrf_grtc_sys_counter_compare_event_enable(NRF_GRTC_Type *p_reg, uint8_t cc_channel);
nrf_grtc_event_t nrf_grtc_sys_counter_compare_event_get(uint8_t cc_channel);
uint32_t nrf_grtc_event_address_get(NRF_GRTC_Type const *p_reg, nrf_grtc_event_t event);

#ifdef __cplusplus
}
#endif

#endif /* NRF_GRTC_H__ */
