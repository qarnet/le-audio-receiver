/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mirror the nRF5340 network-core controller RTC on application-core RTC0.
 * MPSL publishes its RTC-start event over IPC channel 4; PPI clears RTC0 on
 * that event. Only current controller time is needed here, so no high-frequency
 * TIMER or hardware presentation trigger is configured.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <hal/nrf_ipc.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_rtc.h>
#include <zephyr/device.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>

#include "hil_source_controller_time.h"

#define HIL_SOURCE_CONTROLLER_IPC_CHANNEL      4U
#define HIL_SOURCE_CONTROLLER_RTC_COUNTER_BITS 24U

static nrfx_rtc_t controller_rtc = NRFX_RTC_INSTANCE(NRF_RTC0);
static nrfx_gppi_handle_t rtc_start_connection;
static volatile uint32_t rtc_overflows;
static volatile bool controller_time_synchronized;
static volatile bool controller_time_fault;
static volatile bool controller_start_seen;
static bool controller_time_initialized;

static void controller_start_handler(const struct device *dev, mbox_channel_id_t channel_id,
				     void *user_data, struct mbox_msg *data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);
	ARG_UNUSED(data);

	/* One APP boot supports one controller clock epoch. A second pulse means
	 * CPUNET restarted while APP remained alive. PPI already cleared RTC0, so
	 * fail closed instead of combining epochs. */
	if (controller_start_seen) {
		controller_time_synchronized = false;
		controller_time_fault = true;
		return;
	}
	nrf_rtc_event_clear(controller_rtc.p_reg, NRF_RTC_EVENT_OVERFLOW);
	rtc_overflows = 0U;
	controller_start_seen = true;
	controller_time_synchronized = true;
}

static void controller_rtc_handler(nrf_rtc_event_t event_type, void *context)
{
	ARG_UNUSED(context);

	if (event_type == NRF_RTC_EVENT_OVERFLOW && controller_time_synchronized &&
	    !controller_time_fault) {
		rtc_overflows++;
	}
}

int hil_source_controller_time_init(void)
{
	const nrfx_rtc_config_t config = NRFX_RTC_DEFAULT_CONFIG;
	const struct device *mbox = DEVICE_DT_GET(DT_NODELABEL(mbox));
	uint32_t event_endpoint;
	uint32_t task_endpoint;
	int ret;

	if (controller_time_initialized) {
		return 0;
	}
	if (!device_is_ready(mbox)) {
		return -ENODEV;
	}
	controller_time_synchronized = false;
	controller_time_fault = false;
	controller_start_seen = false;
	rtc_overflows = 0U;

	ret = nrfx_rtc_init(&controller_rtc, &config, controller_rtc_handler);
	if (ret != 0) {
		return ret;
	}

	IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_RTC_INST_GET(0)), IRQ_PRIO_LOWEST, nrfx_rtc_irq_handler,
		    &controller_rtc, 0);
	nrfx_rtc_overflow_enable(&controller_rtc, true);
	nrfx_rtc_counter_clear(&controller_rtc);

	ret = mbox_register_callback(mbox, HIL_SOURCE_CONTROLLER_IPC_CHANNEL,
				     controller_start_handler, NULL);
	if (ret != 0) {
		goto fail_rtc;
	}

	/* Mbox driver maps receive event 4 to channel 4. Arm its event-to-RTC0
	 * route before Bluetooth can start CPUNET. */
	nrf_ipc_event_clear(NRF_IPC, NRF_IPC_EVENT_RECEIVE_4);
	event_endpoint = nrf_ipc_event_address_get(NRF_IPC, NRF_IPC_EVENT_RECEIVE_4);
	task_endpoint = nrfx_rtc_task_address_get(&controller_rtc, NRF_RTC_TASK_CLEAR);
	ret = nrfx_gppi_conn_alloc(event_endpoint, task_endpoint, &rtc_start_connection);
	if (ret < 0) {
		goto fail_callback;
	}
	nrfx_gppi_conn_enable(rtc_start_connection);

	ret = mbox_set_enabled(mbox, HIL_SOURCE_CONTROLLER_IPC_CHANNEL, true);
	if (ret != 0) {
		goto fail_connection;
	}
	nrfx_rtc_enable(&controller_rtc);

	controller_time_initialized = true;
	return 0;

fail_connection:
	nrfx_gppi_conn_disable(rtc_start_connection);
	nrfx_gppi_conn_free(event_endpoint, task_endpoint, rtc_start_connection);
fail_callback:
	(void)mbox_register_callback(mbox, HIL_SOURCE_CONTROLLER_IPC_CHANNEL, NULL, NULL);
fail_rtc:
	nrfx_rtc_uninit(&controller_rtc);
	return ret;
}

int hil_source_controller_time_get(uint32_t *time_us)
{
	uint64_t ticks;
	uint32_t overflows;
	uint32_t counter;
	unsigned int key;

	if (time_us == NULL) {
		return -EINVAL;
	}
	/* Keep overflow accounting coherent if wrap occurs while IRQ delivery is
	 * masked. One RTC tick is 1 / 32768 s; 15625 / 512 is the exact
	 * microsecond conversion ratio. */
	key = irq_lock();
	if (controller_time_fault) {
		irq_unlock(key);
		return -EIO;
	}
	if (!controller_time_synchronized ||
	    nrf_ipc_event_check(NRF_IPC, NRF_IPC_EVENT_RECEIVE_4)) {
		irq_unlock(key);
		return -EAGAIN;
	}
	overflows = rtc_overflows;
	counter = nrf_rtc_counter_get(controller_rtc.p_reg);
	if (nrf_rtc_event_check(controller_rtc.p_reg, NRF_RTC_EVENT_OVERFLOW)) {
		counter = nrf_rtc_counter_get(controller_rtc.p_reg);
		overflows++;
	}
	if (nrf_ipc_event_check(NRF_IPC, NRF_IPC_EVENT_RECEIVE_4)) {
		irq_unlock(key);
		return -EAGAIN;
	}
	irq_unlock(key);

	ticks = ((uint64_t)overflows << HIL_SOURCE_CONTROLLER_RTC_COUNTER_BITS) + counter;
	*time_us = (uint32_t)((ticks * 15625ULL) / 512ULL);
	return 0;
}

static int hil_source_controller_time_sys_init(void)
{
	return hil_source_controller_time_init();
}

/* Arm as early as Nordic's reference implementation. main() calls the same
 * idempotent initializer and checks its result, so init failure is observable. */
SYS_INIT(hil_source_controller_time_sys_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
