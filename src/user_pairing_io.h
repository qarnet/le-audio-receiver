/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reusable user-button / user-LED pairing-control hardware adapter (P2 of
 * the user pairing control plan, docs/development/user-pairing-control-plan.md).
 *
 * One singleton matching the one physical control pair.  Hardware is
 * selected ONLY through the `user-button` and `user-led` devicetree
 * aliases (no board-number conditionals anywhere):
 *
 *   / {
 *       aliases {
 *           user-button = &user_button_node;   (child of a gpio-keys node)
 *           user-led    = &user_led_node;      (gpio-leds child)
 *       };
 *   };
 *
 * The gpio-keys driver owns electrical edge handling and debounce; this
 * module schedules/cancels the BONDING/RESET hold thresholds and forwards
 * them to pairing_mode through its public request APIs.  The LED is
 * driven with logical levels; active-low/high polarity comes exclusively
 * from DT.
 *
 * The public header contains no GPIO, input-event, or devicetree types.
 */

#ifndef USER_PAIRING_IO_H
#define USER_PAIRING_IO_H

#include <stdbool.h>
#include <stdint.h>

/* Read-only status snapshot (thread-safe; NULL-safe). */
struct user_pairing_io_status {
	bool initialized;
	bool pressed;
	bool led_active;
	uint32_t hold_generation;
	bool bonding_threshold_armed;
	bool reset_threshold_armed;
	int last_error;
};

/*
 * Initialize the adapter: verify the input device and both GPIO
 * controllers, configure the LED inactive, read the current logical button
 * state, and publish initialized state only after every mandatory step
 * succeeds.  A button already active at initialization time is treated as
 * the debounced press edge and arms both hold thresholds (holding the
 * button during boot needs no board-specific code).  Returns 0, -EALREADY
 * for a second init, or the exact errno of the failing step with
 * initialized=false, the LED inactive where possible, and no threshold
 * work armed.  Call from the application thread after Zephyr POST_KERNEL
 * input initialization (gpio-keys configures its GPIO at
 * CONFIG_INPUT_INIT_PRIORITY); do not add an earlier SYS_INIT.
 */
int user_pairing_io_init(void);

/*
 * Drive the user LED with a logical value; DT supplies the polarity.
 * Signature deliberately matches pairing_mode_ops.led_set; the ctx value
 * is ignored (P1 has one shared operation context, so both NULL and
 * non-NULL must be accepted).  The cached led_active status is updated
 * only after the GPIO call succeeds.  Returns 0 or the exact GPIO errno so
 * the P1 controller can invoke its fatal reboot path.
 */
int user_pairing_io_led_set(bool active, void *ctx);

/*
 * Copy the status snapshot.  NULL is a safe no-op (documented contract,
 * matching pairing_mode_get_status).
 */
void user_pairing_io_get_status(struct user_pairing_io_status *status);

#ifdef USER_PAIRING_IO_TEST
/* Test-only seams (never compiled into production firmware): reset every
 * file-static module state between tests, and inject a GPIO errno into the
 * LED write path or a scheduling failure into the hold-threshold path so
 * the module's exact-errno propagation can be proven on native_sim (where
 * the emulated controller never fails and the system work queue never
 * rejects).  Declared here so the direct test suite can call them. */
void user_pairing_io_test_reset(void);
void user_pairing_io_test_inject_led_error(int err);
void user_pairing_io_test_inject_schedule_error(int err);
#endif

#endif /* USER_PAIRING_IO_H */
