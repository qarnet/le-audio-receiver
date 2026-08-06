/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reusable user-button / user-LED pairing-control hardware adapter (P2).
 * See user_pairing_io.h and docs/development/user-pairing-control-plan.md.
 *
 * Hardware is selected ONLY through the `user-button` and `user-led`
 * devicetree aliases; there are no board-number conditionals.  The module
 * subscribes to the selected gpio-keys input device, translates one
 * debounced button into the already-accepted pairing_mode requests
 * (BONDING at USER_PAIRING_BOND_HOLD_MS, RESET at
 * USER_PAIRING_RESET_HOLD_MS), and drives the user LED with logical levels
 * (polarity lives in DT).  It contains no Bluetooth knowledge.
 *
 * Debounce ownership
 * ------------------
 * The gpio-keys driver owns electrical edge handling and debounce (it
 * configures GPIO_INT_EDGE_BOTH and reports one input event per stable
 * transition after debounce-interval-ms).  This module adds NO second
 * debounce timer: it only schedules/cancels the two hold-threshold works
 * from the debounced press/release events.  The selected gpio-keys node's
 * resolved debounce-interval-ms is BUILD_ASSERTed equal to
 * CONFIG_USER_PAIRING_DEBOUNCE_MS below.
 *
 * Concurrency
 * -----------
 * The input callback can be built in synchronous or thread context (and a
 * synchronous build may report from an ISR), so all shared state is
 * guarded by a short spinlock and the callback only schedules/cancels
 * non-blocking delayed work and submits P1 request events.  The spinlock
 * is never held across GPIO, P1 request, logging, or work-cancel calls.
 *
 * Hold semantics (one observable action per threshold/release race)
 * -----------------------------------------------------------------
 * - Press: one hold generation (nonzero, wraps zero to one); both
 *   thresholds are armed with the captured generation.
 * - Bonding threshold: rechecks initialized/pressed/armed/generation,
 *   clears only the bonding armed flag, then calls
 *   pairing_mode_request_bonding() exactly once.
 * - Reset threshold: rechecks initialized/pressed/armed/generation,
 *   clears both armed flags, cancels a still-armed bonding work, then
 *   calls pairing_mode_request_reset() exactly once (valid even when
 *   BONDING already fired; P1 RESET priority supersedes it).
 * - Release: clears pressed, bumps the generation (invalidating every
 *   captured generation), clears both armed flags, and cancels both
 *   works; it never requests a mode.  k_work_cancel_delayable() returns a
 *   work busy-state bitmask, not errno, so its nonzero return is
 *   deliberately ignored (cancellation is best-effort generation
 *   invalidation).
 * - Stale work from a prior hold can never call P1: generation + pressed
 *   + armed must all match at fire time.
 */

#include "user_pairing_io.h"

#include "pairing_mode.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(user_pairing_io, LOG_LEVEL_INF);

/* ── Devicetree contract (compile-time, useful errors) ────────────── */

#if !defined(DT_N_ALIAS_user_button)
#error "CONFIG_USER_PAIRING_INPUT requires a 'user-button' devicetree alias pointing at a child of a gpio-keys node"
#endif
#if !defined(DT_N_ALIAS_user_led)
#error "CONFIG_USER_PAIRING_INPUT requires a 'user-led' devicetree alias pointing at a gpio-leds child"
#endif

#define USER_BUTTON_NODE DT_ALIAS(user_button)
#define USER_LED_NODE    DT_ALIAS(user_led)
#define USER_INPUT_NODE  DT_PARENT(USER_BUTTON_NODE)

BUILD_ASSERT(DT_NODE_HAS_COMPAT(USER_INPUT_NODE, gpio_keys),
	     "user-button must be a child of a gpio-keys node");
BUILD_ASSERT(DT_NODE_HAS_PROP(USER_BUTTON_NODE, gpios), "user-button must have a gpios property");
BUILD_ASSERT(DT_NODE_HAS_PROP(USER_BUTTON_NODE, zephyr_code),
	     "user-button must have a zephyr,code property");
BUILD_ASSERT(DT_NODE_HAS_PROP(USER_LED_NODE, gpios), "user-led must have a gpios property");
BUILD_ASSERT(DT_PROP(USER_INPUT_NODE, debounce_interval_ms) == CONFIG_USER_PAIRING_DEBOUNCE_MS,
	     "the selected gpio-keys node's debounce-interval-ms must equal "
	     "CONFIG_USER_PAIRING_DEBOUNCE_MS (board/devicetree contract)");

/* The module's own status fields are readable only through get_status();
 * the work-item generation fields are private (never exposed publicly). */

/* ── Hardware bindings (all from DT; no board conditionals) ──────── */

static const struct device *const g_input_dev = DEVICE_DT_GET(USER_INPUT_NODE);
static const struct gpio_dt_spec g_button_spec = GPIO_DT_SPEC_GET(USER_BUTTON_NODE, gpios);
static const struct gpio_dt_spec g_led_spec = GPIO_DT_SPEC_GET(USER_LED_NODE, gpios);
static const uint32_t g_button_code = DT_PROP(USER_BUTTON_NODE, zephyr_code);

/* ── Module state ────────────────────────────────────────────────── */

static struct user_pairing_io_status g_status;
static struct k_spinlock g_lock;
static atomic_t g_initialized;

/* One hold-threshold work item per threshold; each carries the hold
 * generation captured at arm time so stale work can never call P1. */
struct threshold_work {
	struct k_work_delayable dwork;
	uint32_t generation;
};

static struct threshold_work g_bond_work;
static struct threshold_work g_reset_work;

/* ── Forward declarations ────────────────────────────────────────── */

static void handle_press(void);
static void handle_release(void);
static void cancel_and_clear(int err);
static void bond_threshold_handler(struct k_work *work);
static void reset_threshold_handler(struct k_work *work);

/* ── LED write (with test-only fault injection seam) ─────────────── */

#ifdef USER_PAIRING_IO_TEST
static int g_test_led_error;
static int g_test_schedule_error;
#endif

static int led_write(bool active)
{
#ifdef USER_PAIRING_IO_TEST
	/* GCOVR_EXCL_START — test-only fault injection, absent from production */
	if (g_test_led_error != 0) {
		return -g_test_led_error;
	}
	/* GCOVR_EXCL_STOP */
#endif
	return gpio_pin_set_dt(&g_led_spec, active ? 1 : 0);
}

/* Schedule one hold-threshold work on the system work queue.  Split out so
 * the (otherwise unreachable-on-native_sim) scheduling-failure cleanup can
 * be exercised through the test-only fault-injection seam. */
static int schedule_threshold(struct k_work_delayable *dwork, k_timeout_t delay)
{
#ifdef USER_PAIRING_IO_TEST
	/* GCOVR_EXCL_START — test-only fault injection, absent from production */
	if (g_test_schedule_error != 0) {
		return -g_test_schedule_error;
	}
	/* GCOVR_EXCL_STOP */
#endif
	return k_work_schedule(dwork, delay);
}

/* ── Button event handling ───────────────────────────────────────── */

/*
 * Debounced press edge (also used directly by init for a button already
 * active at boot).  Duplicate presses are no-ops.  Schedules both hold
 * thresholds; a negative scheduling result cancels both, clears
 * pressed/armed state, records last_error, and logs — the module never
 * invents a second fatal owner (P1 owns fatal recovery).
 */
static void handle_press(void)
{
	uint32_t gen;
	int ret;

	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (!g_status.initialized || g_status.pressed) {
			k_spin_unlock(&g_lock, key);
			return; /* pre-init or duplicate press: no-op */
		}

		g_status.hold_generation++;
		if (g_status.hold_generation == 0) {
			g_status.hold_generation = 1; /* wrap zero to one */
		}
		g_status.pressed = true;
		g_status.bonding_threshold_armed = true;
		g_status.reset_threshold_armed = true;
		g_status.last_error = 0;
		g_bond_work.generation = g_status.hold_generation;
		g_reset_work.generation = g_status.hold_generation;
		gen = g_status.hold_generation;
		k_spin_unlock(&g_lock, key);
	}

	/* k_work_schedule() returns 0/1 on success, negative on failure. */
	ret = schedule_threshold(&g_bond_work.dwork, K_MSEC(CONFIG_USER_PAIRING_BOND_HOLD_MS));
	if (ret < 0) {
		LOG_ERR("user pairing: bonding threshold schedule failed: %d", ret);
		cancel_and_clear(ret);
		return;
	}

	ret = schedule_threshold(&g_reset_work.dwork, K_MSEC(CONFIG_USER_PAIRING_RESET_HOLD_MS));
	if (ret < 0) {
		LOG_ERR("user pairing: reset threshold schedule failed: %d", ret);
		cancel_and_clear(ret);
		return;
	}
}

/* Shared scheduling-failure cleanup: cancel both thresholds, clear
 * pressed/armed state, and record the errno in status.  Never retried. */
static void cancel_and_clear(int err)
{
	(void)k_work_cancel_delayable(&g_bond_work.dwork);
	(void)k_work_cancel_delayable(&g_reset_work.dwork);

	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		g_status.pressed = false;
		g_status.bonding_threshold_armed = false;
		g_status.reset_threshold_armed = false;
		g_status.last_error = err;
		k_spin_unlock(&g_lock, key);
	}
}

/* Debounced release edge.  Duplicate releases are no-ops.  Clears
 * pressed, bumps the generation (invalidating both captured
 * generations), clears the armed flags, and cancels both works
 * best-effort.  Never requests a mode on release. */
static void handle_release(void)
{
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		if (!g_status.initialized || !g_status.pressed) {
			k_spin_unlock(&g_lock, key);
			return; /* pre-init or duplicate release: no-op */
		}

		g_status.pressed = false;
		g_status.hold_generation++; /* invalidate captured generations */
		g_status.bonding_threshold_armed = false;
		g_status.reset_threshold_armed = false;
		k_spin_unlock(&g_lock, key);
	}

	/* k_work_cancel_delayable() returns a busy-state bitmask, not errno;
	 * cancellation is best-effort generation invalidation, so the return
	 * is deliberately ignored (never logged as a failure, never blocking). */
	(void)k_work_cancel_delayable(&g_bond_work.dwork);
	(void)k_work_cancel_delayable(&g_reset_work.dwork);
}

/* ── Hold-threshold handlers (system work-queue context) ─────────── */

/*
 * BONDING threshold: recheck initialized/pressed/armed/captured
 * generation, clear only the bonding armed flag, then call
 * pairing_mode_request_bonding() exactly once.  A negative return
 * (including -ECANCELED after a P1 fatal) is logged and surfaced in
 * status; it is never retried and never drives transition operations
 * directly.
 */
static void bond_threshold_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
	struct threshold_work *tw = CONTAINER_OF(dwork, struct threshold_work, dwork);
	bool fire;
	int ret;

	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		fire = g_status.initialized && g_status.pressed &&
		       g_status.bonding_threshold_armed &&
		       tw->generation == g_status.hold_generation;
		if (fire) {
			g_status.bonding_threshold_armed = false;
		}
		k_spin_unlock(&g_lock, key);
	}

	if (!fire) {
		return;
	}

	ret = pairing_mode_request_bonding();
	if (ret < 0) {
		LOG_ERR("user pairing: pairing_mode_request_bonding failed: %d", ret);
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		g_status.last_error = ret;
		k_spin_unlock(&g_lock, key);
	}
}

/*
 * RESET threshold: recheck initialized/pressed/armed/captured generation,
 * clear both armed flags, cancel a still-armed bonding work, then call
 * pairing_mode_request_reset() exactly once.  This stays valid when
 * BONDING already fired at the shorter threshold; P1 RESET priority
 * supersedes it.  A negative return is logged and surfaced in status and
 * never retried.
 */
static void reset_threshold_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
	struct threshold_work *tw = CONTAINER_OF(dwork, struct threshold_work, dwork);
	bool fire;
	bool cancel_bonding;
	int ret;

	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		fire = g_status.initialized && g_status.pressed && g_status.reset_threshold_armed &&
		       tw->generation == g_status.hold_generation;
		if (fire) {
			cancel_bonding = g_status.bonding_threshold_armed;
			g_status.reset_threshold_armed = false;
			g_status.bonding_threshold_armed = false;
		}
		k_spin_unlock(&g_lock, key);
	}

	if (!fire) {
		return;
	}

	if (cancel_bonding) {
		(void)k_work_cancel_delayable(&g_bond_work.dwork);
	}

	ret = pairing_mode_request_reset();
	if (ret < 0) {
		LOG_ERR("user pairing: pairing_mode_request_reset failed: %d", ret);
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		g_status.last_error = ret;
		k_spin_unlock(&g_lock, key);
	}
}

/* ── Input callback (registered statically against the gpio-keys device) ── */

/*
 * Filters INPUT_EV_KEY, the selected zephyr,code, and the sync batch-end
 * flag.  (struct input_event carries only a sync flag, not a separate
 * "complete" concept; gpio-keys reports every key event with sync=true.)
 * The INPUT_CALLBACK_DEFINE registration below already restricts the
 * callback to the gpio-keys device.
 */
static void user_button_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY || evt->code != g_button_code || !evt->sync) {
		return;
	}

	if (evt->value != 0) {
		handle_press();
	} else {
		handle_release();
	}
}

INPUT_CALLBACK_DEFINE(g_input_dev, user_button_cb, NULL);

/* ── Public API ──────────────────────────────────────────────────── */

int user_pairing_io_init(void)
{
	int ret;
	bool pressed_at_init;

	if (atomic_get(&g_initialized)) {
		return -EALREADY;
	}

	if (!device_is_ready(g_input_dev)) {
		LOG_ERR("user pairing: input device %s not ready", g_input_dev->name);
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&g_button_spec)) {
		LOG_ERR("user pairing: button GPIO controller not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&g_led_spec)) {
		LOG_ERR("user pairing: LED GPIO controller not ready");
		return -ENODEV;
	}

	k_work_init_delayable(&g_bond_work.dwork, bond_threshold_handler);
	k_work_init_delayable(&g_reset_work.dwork, reset_threshold_handler);

	/* NORMAL's physical default is off; DT supplies the polarity. */
	ret = gpio_pin_configure_dt(&g_led_spec, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("user pairing: LED GPIO configure failed: %d", ret);
		return ret;
	}

	/* Current LOGICAL button state (DT handles active-low/high). */
	ret = gpio_pin_get_dt(&g_button_spec);
	if (ret < 0) {
		LOG_ERR("user pairing: button GPIO read failed: %d", ret);
		return ret;
	}
	pressed_at_init = (ret == 1);

	/* Publish initialized state only after all mandatory setup succeeds. */
	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		g_status.initialized = true;
		g_status.pressed = false;
		g_status.led_active = false;
		g_status.hold_generation = 0;
		g_status.bonding_threshold_armed = false;
		g_status.reset_threshold_armed = false;
		g_status.last_error = 0;
		k_spin_unlock(&g_lock, key);
	}

	atomic_set(&g_initialized, 1);

	if (pressed_at_init) {
		/* Button already active at boot: initialization time is the
		 * debounced press edge; arm both hold thresholds. */
		handle_press();
	}

	return 0;
}

int user_pairing_io_led_set(bool active, void *ctx)
{
	int ret;

	ARG_UNUSED(ctx); /* P1 has one shared operation context */

	ret = led_write(active);
	if (ret < 0) {
		return ret; /* exact GPIO errno; cached state untouched */
	}

	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		g_status.led_active = active;
		k_spin_unlock(&g_lock, key);
	}

	return 0;
}

void user_pairing_io_get_status(struct user_pairing_io_status *status)
{
	if (status == NULL) {
		return; /* documented NULL-safe contract */
	}

	k_spinlock_key_t key = k_spin_lock(&g_lock);

	*status = g_status;
	k_spin_unlock(&g_lock, key);
}

#ifdef USER_PAIRING_IO_TEST
/* GCOVR_EXCL_START — test seams, absent from production builds */
/*
 * Test seams (tests/unit/user_pairing_io): reset every file-static module
 * state between tests and cancel all queued threshold work synchronously
 * so no stale event can leak into the next test.  The LED fault-injection
 * hook forces the exact-errno propagation path that the never-failing
 * native_sim emulated controller cannot produce.  None of this enters
 * production firmware.
 */
static struct k_work_sync g_test_work_sync;

void user_pairing_io_test_reset(void)
{
	(void)k_work_cancel_delayable_sync(&g_bond_work.dwork, &g_test_work_sync);
	(void)k_work_cancel_delayable_sync(&g_reset_work.dwork, &g_test_work_sync);

	{
		k_spinlock_key_t key = k_spin_lock(&g_lock);

		memset(&g_status, 0, sizeof(g_status));
		k_spin_unlock(&g_lock, key);
	}

	atomic_set(&g_initialized, 0);
	g_test_led_error = 0;
	g_test_schedule_error = 0;
}

void user_pairing_io_test_inject_led_error(int err)
{
	g_test_led_error = (err > 0) ? err : 0;
}

void user_pairing_io_test_inject_schedule_error(int err)
{
	g_test_schedule_error = (err > 0) ? err : 0;
}
/* GCOVR_EXCL_STOP */
#endif /* USER_PAIRING_IO_TEST */
