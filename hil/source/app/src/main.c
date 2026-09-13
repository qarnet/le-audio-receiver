/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dedicated LE Audio source fixture entry point.
 *
 * Initializes output, raises CPUAPP to 128 MHz, and initializes the
 * controller-clock mirror, then the Bluetooth/BAP backend (bt_enable,
 * settings_load, bt_is_ready, callbacks), then the app coordinator (state and
 * shell command), and returns into Zephyr threads.
 * A fatal init error emits one best-effort HIL1 status when possible and returns
 * nonzero; there is no restart loop.
 */

#include <nrfx_clock.h>

#include "hil_source_app.h"
#include "hil_source_bap.h"
#include "hil_source_controller_time.h"
#include "hil_source_output.h"

int main(void)
{
	int err;

	err = hil_source_output_init();
	if (err != 0) {
		/* No output available; nothing can be emitted. */
		return err;
	}

	/* Mode B encodes two LC3 channels per 10 ms SDU. At the nRF5340's
	 * 64 MHz reset frequency that work misses alternate controller-clock
	 * pins. Nordic's Bluetooth audio implementations use DIV_1 for the same
	 * real-time throughput requirement. RTC-based timing is unaffected. */
	err = nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1);
	if (err != 0) {
		hil_source_app_fatal_status("cpu_clock_init", err);
		return err;
	}

	err = hil_source_controller_time_init();
	if (err != 0) {
		hil_source_app_fatal_status("controller_time_init", err);
		return err;
	}

	err = hil_source_bap_init();
	if (err != 0) {
		hil_source_app_fatal_status("bap_init", err);
		return err;
	}

	err = hil_source_app_init();
	if (err != 0) {
		hil_source_app_fatal_status("app_init", err);
		return err;
	}

	return 0;
}
