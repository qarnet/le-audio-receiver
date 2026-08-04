/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock implementation of the nrfx_grtc / nrfx_gppi / nrf_grtc /
 * nrf_timer HAL surface used by src/audio_timing_nrf54.c.  Records
 * every call (with arguments) in an ordered log and exposes
 * test-controllable return values.  Test-owned; never part of a
 * production build.
 */

#include "mock_hal.h"

#include <stddef.h>

#include <zephyr/kernel.h>
#include <hal/nrf_grtc.h>
#include <hal/nrf_timer.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_grtc.h>

/* ── Ordered call log ────────────────────────────────────────────── */

#define MOCK_LOG_MAX 256

struct mock_log_entry {
	enum mock_event ev;
	uint64_t a;
	uint64_t b;
};

static struct mock_log_entry mock_log[MOCK_LOG_MAX];
static int mock_log_len;

int mock_hal_log_len(void)
{
	return mock_log_len;
}

enum mock_event mock_hal_log_ev(int i)
{
	return mock_log[i].ev;
}

uint64_t mock_hal_log_a(int i)
{
	return mock_log[i].a;
}

uint64_t mock_hal_log_b(int i)
{
	return mock_log[i].b;
}

static void log_ev(enum mock_event ev, uint64_t a, uint64_t b)
{
	if (mock_log_len < MOCK_LOG_MAX) {
		mock_log[mock_log_len].ev = ev;
		mock_log[mock_log_len].a = a;
		mock_log[mock_log_len].b = b;
	}
	mock_log_len++;
}

/* ── GRTC mock state ─────────────────────────────────────────────── */

int mock_grtc_alloc_ret = 0;
uint8_t mock_grtc_alloc_channel = 3;
uint8_t mock_grtc_free_channel = 0xFF;
int mock_grtc_cc_abs_ret = 0;
uint8_t mock_grtc_cb_channel = 0xFF;
nrfx_grtc_cc_handler_t mock_grtc_cb_handler = NULL;
uint64_t mock_grtc_now = 0;
int mock_grtc_cc_abs_calls = 0;
uint64_t mock_grtc_cc_abs_last_value = 0;
uint8_t mock_grtc_cc_abs_last_channel = 0xFF;
bool mock_grtc_cc_abs_last_irq = false;
int mock_grtc_cc_disable_calls = 0;
uint8_t mock_grtc_cc_disable_last_channel = 0xFF;
uint32_t mock_grtc_event_addr = 0x1000;

/* ── TIMER mock state ────────────────────────────────────────────── */

uint32_t mock_timer_cc_value = 0;
uint32_t mock_nrf_timer_base_frequency_hz = 16000000;
int mock_timer_mode_last = -1;
int mock_timer_bw_last = -1;
uint32_t mock_timer_prescaler_last = 0;
int mock_timer_task_last = -1;
uint32_t mock_timer_task_addr = 0x2000;

/* ── GPPI mock state ─────────────────────────────────────────────── */

int mock_gppi_conn_alloc_ret = 0;
nrfx_gppi_handle_t mock_gppi_next_handle = 7;
nrfx_gppi_handle_t mock_gppi_last_handle = 0xFF;
uint32_t mock_gppi_last_eep = 0;
uint32_t mock_gppi_last_tep = 0;
int mock_gppi_conn_enable_calls = 0;

/* ── R1 compare-programming gate ─────────────────────────────────── */

static struct k_sem *cc_abs_entered;
static struct k_sem *cc_abs_release;
static bool cc_abs_gate_armed;

void mock_grtc_block_first_cc_abs(struct k_sem *entered, struct k_sem *release)
{
	cc_abs_gate_armed = (entered != NULL && release != NULL);
	cc_abs_entered = entered;
	cc_abs_release = release;
}

void mock_hal_reset(void)
{
	mock_log_len = 0;
	mock_grtc_alloc_ret = 0;
	mock_grtc_alloc_channel = 3;
	mock_grtc_free_channel = 0xFF;
	mock_grtc_cc_abs_ret = 0;
	mock_grtc_cb_channel = 0xFF;
	mock_grtc_cb_handler = NULL;
	mock_grtc_now = 0;
	mock_grtc_cc_abs_calls = 0;
	mock_grtc_cc_abs_last_value = 0;
	mock_grtc_cc_abs_last_channel = 0xFF;
	mock_grtc_cc_abs_last_irq = false;
	mock_grtc_cc_disable_calls = 0;
	mock_grtc_cc_disable_last_channel = 0xFF;
	mock_grtc_event_addr = 0x1000;
	mock_timer_cc_value = 0;
	mock_nrf_timer_base_frequency_hz = 16000000;
	mock_timer_mode_last = -1;
	mock_timer_bw_last = -1;
	mock_timer_prescaler_last = 0;
	mock_timer_task_last = -1;
	mock_timer_task_addr = 0x2000;
	mock_gppi_conn_alloc_ret = 0;
	mock_gppi_next_handle = 7;
	mock_gppi_last_handle = 0xFF;
	mock_gppi_last_eep = 0;
	mock_gppi_last_tep = 0;
	mock_gppi_conn_enable_calls = 0;
	cc_abs_entered = NULL;
	cc_abs_release = NULL;
	cc_abs_gate_armed = false;
}

/* ── nrfx_grtc ───────────────────────────────────────────────────── */

int nrfx_grtc_channel_alloc(uint8_t *p_channel)
{
	log_ev(EV_GRTC_ALLOC, 0, 0);
	if (mock_grtc_alloc_ret < 0) {
		/* Allocation failure returns before writing a handle. */
		return mock_grtc_alloc_ret;
	}
	*p_channel = mock_grtc_alloc_channel;
	return 0;
}

void nrfx_grtc_channel_callback_set(uint8_t channel, nrfx_grtc_cc_handler_t handler,
				    void *p_context)
{
	log_ev(EV_GRTC_CALLBACK_SET, channel, 0);
	mock_grtc_cb_channel = channel;
	mock_grtc_cb_handler = handler;
}

int nrfx_grtc_channel_free(uint8_t channel)
{
	log_ev(EV_GRTC_FREE, channel, 0);
	mock_grtc_free_channel = channel;
	return 0;
}

int nrfx_grtc_syscounter_cc_absolute_set(nrfx_grtc_channel_t *p_chan_data, uint64_t val,
					 bool enable_irq)
{
	log_ev(EV_GRTC_CC_ABSOLUTE_SET, p_chan_data->channel, val);
	mock_grtc_cc_abs_calls++;
	mock_grtc_cc_abs_last_value = val;
	mock_grtc_cc_abs_last_channel = p_chan_data->channel;
	mock_grtc_cc_abs_last_irq = enable_irq;

	/* R1: one-shot deterministic block on the first compare after
	 * arming (the production update thread holds the control mutex
	 * across this call). */
	if (cc_abs_gate_armed) {
		cc_abs_gate_armed = false;
		if (cc_abs_entered != NULL && cc_abs_release != NULL) {
			k_sem_give(cc_abs_entered);
			k_sem_take(cc_abs_release, K_FOREVER);
		}
	}
	return mock_grtc_cc_abs_ret;
}

int nrfx_grtc_syscounter_cc_disable(uint8_t channel)
{
	log_ev(EV_GRTC_CC_DISABLE, channel, 0);
	mock_grtc_cc_disable_calls++;
	mock_grtc_cc_disable_last_channel = channel;
	return 0;
}

uint64_t nrfx_grtc_syscounter_get(void)
{
	log_ev(EV_GRTC_SYSCOUNTER_GET, 0, 0);
	return mock_grtc_now;
}

/* ── hal nrf_grtc ────────────────────────────────────────────────── */

void nrf_grtc_sys_counter_compare_event_enable(NRF_GRTC_Type *p_reg, uint8_t cc_channel)
{
	log_ev(EV_GRTC_COMPARE_EVENT_ENABLE, cc_channel, 0);
}

nrf_grtc_event_t nrf_grtc_sys_counter_compare_event_get(uint8_t cc_channel)
{
	log_ev(EV_GRTC_COMPARE_EVENT_GET, cc_channel, 0);
	return (nrf_grtc_event_t)(int)cc_channel;
}

uint32_t nrf_grtc_event_address_get(NRF_GRTC_Type const *p_reg, nrf_grtc_event_t event)
{
	log_ev(EV_GRTC_EVENT_ADDRESS_GET, (uint64_t)event, 0);
	return mock_grtc_event_addr + (uint32_t)event;
}

/* ── hal nrf_timer ───────────────────────────────────────────────── */

void nrf_timer_mode_set(NRF_TIMER_Type *p_reg, nrf_timer_mode_t mode)
{
	log_ev(EV_TIMER_MODE_SET, (uint64_t)mode, 0);
	mock_timer_mode_last = (int)mode;
}

void nrf_timer_bit_width_set(NRF_TIMER_Type *p_reg, nrf_timer_bit_width_t bit_width)
{
	log_ev(EV_TIMER_BIT_WIDTH_SET, (uint64_t)bit_width, 0);
	mock_timer_bw_last = (int)bit_width;
}

void nrf_timer_prescaler_set(NRF_TIMER_Type *p_reg, uint32_t prescaler_factor)
{
	log_ev(EV_TIMER_PRESCALER_SET, prescaler_factor, 0);
	mock_timer_prescaler_last = prescaler_factor;
}

void nrf_timer_task_trigger(NRF_TIMER_Type *p_reg, nrf_timer_task_t task)
{
	log_ev(EV_TIMER_TASK_TRIGGER, (uint64_t)task, 0);
	mock_timer_task_last = (int)task;
}

uint32_t nrf_timer_cc_get(NRF_TIMER_Type const *p_reg, nrf_timer_cc_channel_t cc_channel)
{
	log_ev(EV_TIMER_CC_GET, (uint64_t)cc_channel, 0);
	return mock_timer_cc_value;
}

uint32_t nrf_timer_task_address_get(NRF_TIMER_Type const *p_reg, nrf_timer_task_t task)
{
	log_ev(EV_TIMER_TASK_ADDRESS_GET, (uint64_t)task, 0);
	return mock_timer_task_addr + (uint32_t)task;
}

nrf_timer_task_t nrf_timer_capture_task_get(uint8_t channel)
{
	log_ev(EV_TIMER_CAPTURE_TASK_GET, channel, 0);
	return (nrf_timer_task_t)(int)channel;
}

/* ── nrfx_gppi ───────────────────────────────────────────────────── */

int nrfx_gppi_conn_alloc(uint32_t eep, uint32_t tep, nrfx_gppi_handle_t *p_handle)
{
	log_ev(EV_GPPI_CONN_ALLOC, eep, tep);
	mock_gppi_last_eep = eep;
	mock_gppi_last_tep = tep;
	if (mock_gppi_conn_alloc_ret < 0) {
		/* Allocation failure returns before writing a handle. */
		return mock_gppi_conn_alloc_ret;
	}
	*p_handle = mock_gppi_next_handle;
	return 0;
}

void nrfx_gppi_conn_enable(nrfx_gppi_handle_t handle)
{
	log_ev(EV_GPPI_CONN_ENABLE, handle, 0);
	mock_gppi_conn_enable_calls++;
	mock_gppi_last_handle = handle;
}

/* ── Test-owned timer register object ────────────────────────────── */

NRF_TIMER_Type test_timer_reg;
