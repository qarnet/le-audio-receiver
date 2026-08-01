/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Narrow boot coordinator — see app_lifecycle.h.  No hardware access
 * here; all side effects come from the operations supplied by main.c.
 */

#include <errno.h>
#include <stddef.h>

#include <zephyr/logging/log.h>

#include "app_lifecycle.h"

LOG_MODULE_REGISTER(app_lifecycle, LOG_LEVEL_INF);

/** Validate ops and the required callbacks (everything but platform_init). */
static bool ops_valid(const struct app_lifecycle_ops *ops)
{
	if (ops == NULL || ops->watchdog_init == NULL || ops->bluetooth_init == NULL ||
	    ops->settings_init == NULL || ops->volume_init == NULL || ops->bap_init == NULL ||
	    ops->sink_init == NULL || ops->advertising_start == NULL || ops->cold_reboot == NULL) {
		return false;
	}
	return true;
}

static int run_fatal_step(const struct app_lifecycle_ops *ops, const char *step_name,
			  int (*step)(void))
{
	int err = step();

	if (err) {
		LOG_ERR("%s failed: %d", step_name, err);
		ops->cold_reboot();
	}
	return err;
}

int app_lifecycle_boot(const struct app_lifecycle_ops *ops)
{
	int err;

	if (!ops_valid(ops)) {
		return -EINVAL;
	}

	err = run_fatal_step(ops, "Watchdog init", ops->watchdog_init);
	if (err) {
		return err;
	}

	err = run_fatal_step(ops, "Bluetooth init", ops->bluetooth_init);
	if (err) {
		return err;
	}

	err = run_fatal_step(ops, "settings_load()", ops->settings_init);
	if (err) {
		return err;
	}

	err = run_fatal_step(ops, "Volume init", ops->volume_init);
	if (err) {
		return err;
	}

	err = run_fatal_step(ops, "BAP init", ops->bap_init);
	if (err) {
		return err;
	}

	err = run_fatal_step(ops, "I2S init", ops->sink_init);
	if (err) {
		return err;
	}

	/* Nonfatal nRF54-only platform wiring (FLPR handshake/offload/runtime). */
	if (ops->platform_init != NULL) {
		ops->platform_init();
	}

	err = run_fatal_step(ops, "Adv start", ops->advertising_start);
	if (err) {
		return err;
	}

	return 0;
}

int app_lifecycle_restart_advertising(const struct app_lifecycle_ops *ops)
{
	if (ops == NULL || ops->advertising_start == NULL || ops->cold_reboot == NULL) {
		return -EINVAL;
	}

	int err = ops->advertising_start();

	if (err) {
		LOG_ERR("Adv restart failed: %d", err);
		ops->cold_reboot();
	}
	return err;
}
