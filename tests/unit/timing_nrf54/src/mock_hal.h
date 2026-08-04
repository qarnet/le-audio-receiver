/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock HAL state and ordered call log for tests/unit/timing_nrf54.
 * Test-owned; never part of a production build.
 */

#ifndef MOCK_HAL_H
#define MOCK_HAL_H

#include <helpers/nrfx_gppi.h>
#include <nrfx_grtc.h>
#include <stdbool.h>
#include <stdint.h>

struct k_sem;

#ifdef __cplusplus
extern "C" {
#endif

/* ── Ordered call log ────────────────────────────────────────────── */

enum mock_event {
	EV_GRTC_ALLOC,
	EV_GRTC_COMPARE_EVENT_ENABLE,
	EV_GRTC_CALLBACK_SET,
	EV_GRTC_FREE,
	EV_GRTC_CC_ABSOLUTE_SET,
	EV_GRTC_CC_DISABLE,
	EV_GRTC_SYSCOUNTER_GET,
	EV_GRTC_EVENT_ADDRESS_GET,
	EV_GRTC_COMPARE_EVENT_GET,
	EV_TIMER_MODE_SET,
	EV_TIMER_BIT_WIDTH_SET,
	EV_TIMER_PRESCALER_SET,
	EV_TIMER_TASK_TRIGGER,
	EV_TIMER_CC_GET,
	EV_TIMER_TASK_ADDRESS_GET,
	EV_TIMER_CAPTURE_TASK_GET,
	EV_GPPI_CONN_ALLOC,
	EV_GPPI_CONN_ENABLE,
};

int mock_hal_log_len(void);
enum mock_event mock_hal_log_ev(int i);
uint64_t mock_hal_log_a(int i);
uint64_t mock_hal_log_b(int i);
void mock_hal_reset(void);

/* ── GRTC ────────────────────────────────────────────────────────── */

extern int mock_grtc_alloc_ret;                     /* <0 → alloc fails without writing */
extern uint8_t mock_grtc_alloc_channel;             /* channel written on success */
extern uint8_t mock_grtc_free_channel;              /* last freed channel */
extern int mock_grtc_cc_abs_ret;                    /* <0 → compare set fails */
extern uint8_t mock_grtc_cb_channel;                /* channel of last callback_set */
extern nrfx_grtc_cc_handler_t mock_grtc_cb_handler; /* captured handler */
extern uint64_t mock_grtc_now;                      /* SYSCOUNTER now */
extern int mock_grtc_cc_abs_calls;
extern uint64_t mock_grtc_cc_abs_last_value;
extern uint8_t mock_grtc_cc_abs_last_channel;
extern bool mock_grtc_cc_abs_last_irq;
extern int mock_grtc_cc_disable_calls;
extern uint8_t mock_grtc_cc_disable_last_channel;
extern uint32_t mock_grtc_event_addr; /* fake GRTC event address base */

/* R1: deterministic one-shot gate on the FIRST compare programming after
 * arming (entered/release semaphores).  The update thread blocks inside
 * nrfx_grtc_syscounter_cc_absolute_set while holding the production
 * control mutex; the reset thread then proves it cannot disable/reset
 * until the compare is released.  Cleared by mock_hal_reset(). */
void mock_grtc_block_first_cc_abs(struct k_sem *entered, struct k_sem *release);

/* ── TIMER ───────────────────────────────────────────────────────── */

extern uint32_t mock_timer_cc_value; /* value returned by cc_get */
extern uint32_t mock_nrf_timer_base_frequency_hz;
extern int mock_timer_mode_last;
extern int mock_timer_bw_last;
extern uint32_t mock_timer_prescaler_last;
extern int mock_timer_task_last;
extern uint32_t mock_timer_task_addr; /* fake task address base */

/* ── GPPI ────────────────────────────────────────────────────────── */

extern int mock_gppi_conn_alloc_ret; /* <0 → alloc fails without handle */
extern nrfx_gppi_handle_t mock_gppi_next_handle;
extern nrfx_gppi_handle_t mock_gppi_last_handle;
extern uint32_t mock_gppi_last_eep;
extern uint32_t mock_gppi_last_tep;
extern int mock_gppi_conn_enable_calls;

#ifdef __cplusplus
}
#endif

#endif /* MOCK_HAL_H */
