/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dedicated LE Audio source fixture (RH1B) entry point.
 *
 * Initializes output, then the Bluetooth/BAP backend (bt_enable,
 * settings_load, bt_is_ready, callbacks), then the app coordinator (state
 * and shell command), and returns into Zephyr threads.  A fatal init
 * error emits one best-effort HIL1 status when possible and returns
 * nonzero; there is no restart loop.
 */

#include "hil_source_app.h"
#include "hil_source_bap.h"
#include "hil_source_output.h"

int main(void)
{
	int err;

	err = hil_source_output_init();
	if (err != 0) {
		/* No output available; nothing can be emitted. */
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
