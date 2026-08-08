/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned shadow of the installed NCS v3.3.0 <hal/nrf_timer.h>.
 * Declares exactly the API surface used by src/audio_timing_nrf54.c;
 * the mock implementation lives in src/mock_hal.c.  Never part of a
 * production build.
 */

#ifndef NRF_TIMER_H__
#define NRF_TIMER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t _reserved;
} NRF_TIMER_Type;

typedef enum {
	NRF_TIMER_MODE_TIMER = 0,
} nrf_timer_mode_t;

typedef enum {
	NRF_TIMER_BIT_WIDTH_32 = 0,
} nrf_timer_bit_width_t;

typedef enum {
	NRF_TIMER_TASK_CLEAR = 0,
	NRF_TIMER_TASK_START = 1,
} nrf_timer_task_t;

typedef enum {
	NRF_TIMER_CC_CHANNEL0 = 0,
} nrf_timer_cc_channel_t;

/* Test-owned timer register object backing TIMER20_BASE under
 * AUDIO_TIMING_NRF54_TEST (declared extern here, defined in mock_hal.c). */
extern NRF_TIMER_Type test_timer_reg;

void nrf_timer_mode_set(NRF_TIMER_Type *p_reg, nrf_timer_mode_t mode);
void nrf_timer_bit_width_set(NRF_TIMER_Type *p_reg, nrf_timer_bit_width_t bit_width);
void nrf_timer_prescaler_set(NRF_TIMER_Type *p_reg, uint32_t prescaler_factor);
void nrf_timer_task_trigger(NRF_TIMER_Type *p_reg, nrf_timer_task_t task);
uint32_t nrf_timer_cc_get(NRF_TIMER_Type const *p_reg, nrf_timer_cc_channel_t cc_channel);
uint32_t nrf_timer_task_address_get(NRF_TIMER_Type const *p_reg, nrf_timer_task_t task);
nrf_timer_task_t nrf_timer_capture_task_get(uint8_t channel);

/* Mock-controlled base frequency (production uses the HAL macro to
 * cache the nominal timer frequency at init). */
#define NRF_TIMER_BASE_FREQUENCY_GET(p_reg) (mock_nrf_timer_base_frequency_hz)
extern uint32_t mock_nrf_timer_base_frequency_hz;

#ifdef __cplusplus
}
#endif

#endif /* NRF_TIMER_H__ */
