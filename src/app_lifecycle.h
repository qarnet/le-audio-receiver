/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Narrow boot coordinator — the only boot logic that is unit-testable.
 *
 * main.c retains all hardware wiring (watchdog device/thread, real
 * subsystem calls, nRF54-only FLPR platform initializer, advertising
 * restart loop) and adapts it to this operations structure.  The fatal
 * init order is fixed: watchdog, Bluetooth, settings, volume, BAP, I2S,
 * optional platform init, initial advertising.  settings_load() stays
 * after Bluetooth enable and before BAP/PACS registration — do not
 * reorder.
 */

#ifndef APP_LIFECYCLE_H
#define APP_LIFECYCLE_H

/** Boot/advertising operations supplied by main.c. */
struct app_lifecycle_ops {
	/** Fatal step 1: watchdog setup. */
	int (*watchdog_init)(void);
	/** Fatal step 2: Bluetooth enable (bt_enable(NULL)). */
	int (*bluetooth_init)(void);
	/** Fatal step 3: settings_load() (after BT, before BAP/PACS). */
	int (*settings_init)(void);
	/** Fatal step 4: volume init. */
	int (*volume_init)(void);
	/** Fatal step 5: BAP init (includes PACS registration). */
	int (*bap_init)(void);
	/** Fatal step 6: audio sink init. */
	int (*sink_init)(void);
	/** Optional, nonfatal: nRF54-only FLPR wiring. May be NULL. */
	void (*platform_init)(void);
	/** Fatal final step: initial advertising start. */
	int (*advertising_start)(void);
	/** Cold reboot (sys_reboot(SYS_REBOOT_COLD) in production). */
	void (*cold_reboot)(void);
};

/**
 * @brief Run the fatal boot sequence exactly once.
 *
 * Runs the six subsystem steps in the documented order, then the
 * optional nonfatal platform init, then the initial advertising start
 * (final fatal step).  On any fatal callback error: logs the step and
 * error, calls cold_reboot() exactly once, stops immediately, and
 * returns the original error only if the reboot callback returned
 * (unit-test behavior — production sys_reboot() never returns).
 *
 * @param ops  Non-NULL operations; all callbacks except platform_init
 *             must be non-NULL (validated before any call, no reboot
 *             for malformed wiring).
 * @retval 0 on success
 * @retval -EINVAL for NULL ops or a missing required callback
 * @retval negative errno of the first fatal step that failed
 */
int app_lifecycle_boot(const struct app_lifecycle_ops *ops);

/**
 * @brief Restart advertising after a disconnect.
 *
 * Calls only advertising_start.  On failure: logs, cold-reboots exactly
 * once, and returns the original error if the reboot callback returned.
 *
 * @param ops  Non-NULL operations; advertising_start and cold_reboot
 *             must be non-NULL.
 * @retval 0 on success
 * @retval -EINVAL for NULL ops or a missing required callback
 * @retval negative errno of advertising_start on failure
 */
int app_lifecycle_restart_advertising(const struct app_lifecycle_ops *ops);

#endif /* APP_LIFECYCLE_H */
