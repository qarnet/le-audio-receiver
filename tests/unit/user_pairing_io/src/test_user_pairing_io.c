/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * P2: direct tests of the production user-pairing I/O adapter
 * (src/user_pairing_io.c) against the REAL input subsystem, the REAL
 * gpio-keys driver, and the REAL gpio-emul controller on native_sim.
 *
 * The gpio-keys node (active-low/pull-up button, 30 ms debounce) and the
 * active-low gpio-leds LED are wired through the user-button / user-led
 * aliases in boards/native_sim.overlay.  Logical input is driven with
 * gpio_emul_input_set_dt() (raw physical level: 0 = pressed on an
 * active-low button), physical LED output is read with
 * gpio_emul_output_get_dt().  pairing_mode_request_bonding() /
 * pairing_mode_request_reset() are fake link implementations that record
 * public calls/results.
 *
 * Hold thresholds are shortened to preserve the production ordering
 * (bonding 100 ms, reset 200 ms; debounce 30 ms) and the suite runs at
 * CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000 so native_sim delayed-work
 * deadlines (which land one tick late) stay within 1 ms of nominal.
 * Tests prefer event semaphores over arbitrary long sleeps.
 *
 * Assertions use public state (user_pairing_io_get_status), the fake
 * call counters, and raw emulated GPIO levels — never private fields.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "pairing_mode.h"
#include "user_pairing_io.h"

/* ── hardware under test (from the overlay's aliases) ────────────── */

static const struct gpio_dt_spec g_button_spec = GPIO_DT_SPEC_GET(DT_ALIAS(user_button), gpios);
static const struct gpio_dt_spec g_led_spec = GPIO_DT_SPEC_GET(DT_ALIAS(user_led), gpios);
static const uint16_t g_button_code = DT_PROP(DT_ALIAS(user_button), zephyr_code);
static const struct device *const g_input_dev = DEVICE_DT_GET(DT_PARENT(DT_ALIAS(user_button)));

/* ── fake link implementations of the P1 request APIs ────────────── */

static int fake_bonding_calls;
static int fake_reset_calls;
static int fake_bonding_ret; /* configured return (0 = ok) */
static int fake_reset_ret;
static struct k_sem bond_sem;  /* posted after each recorded bonding call */
static struct k_sem reset_sem; /* posted after each recorded reset call */

int pairing_mode_request_bonding(void)
{
	fake_bonding_calls++;
	k_sem_give(&bond_sem);
	return fake_bonding_ret;
}

int pairing_mode_request_reset(void)
{
	fake_reset_calls++;
	k_sem_give(&reset_sem);
	return fake_reset_ret;
}

/* ── helpers ─────────────────────────────────────────────────────── */

#define WAIT_MS 1000

static void fake_reset(void)
{
	fake_bonding_calls = 0;
	fake_reset_calls = 0;
	fake_bonding_ret = 0;
	fake_reset_ret = 0;
	k_sem_reset(&bond_sem);
	k_sem_reset(&reset_sem);
}

static void suite_before(void *fixture)
{
	ARG_UNUSED(fixture);
	/* Synchronize the module FIRST (an aborted test may leave a
	 * threshold work in flight), then the fake. */
	user_pairing_io_test_reset();
	fake_reset();

	/* Release the button and flush the gpio-keys debounce so no stale
	 * driver work fires into the next test (a no-op edge when the pin
	 * is already released). */
	gpio_emul_input_set_dt(&g_button_spec, 1);
	k_sleep(K_MSEC(80));

	k_sem_init(&bond_sem, 0, 8);
	k_sem_init(&reset_sem, 0, 8);
}

static void init_ok(void)
{
	zassert_ok(user_pairing_io_init(), "init failed");
}

static void press(void)
{
	zassert_ok(gpio_emul_input_set_dt(&g_button_spec, 0), "emul press");
}

static void release(void)
{
	zassert_ok(gpio_emul_input_set_dt(&g_button_spec, 1), "emul release");
}

static void wait_bond(int timeout_ms)
{
	zassert_equal(k_sem_take(&bond_sem, K_MSEC(timeout_ms)), 0, "bonding never requested");
}

static void wait_reset(int timeout_ms)
{
	zassert_equal(k_sem_take(&reset_sem, K_MSEC(timeout_ms)), 0, "reset never requested");
}

static void assert_no_requests(void)
{
	zassert_equal(fake_bonding_calls, 0, "unexpected bonding request");
	zassert_equal(fake_reset_calls, 0, "unexpected reset request");
}

static int led_raw(void)
{
	return gpio_emul_output_get_dt(&g_led_spec);
}

/* ── suite ───────────────────────────────────────────────────────── */

ZTEST_SUITE(user_pairing_io, NULL, NULL, suite_before, NULL, NULL);

/* Init: devices ready, status reports inactive/unpressed, LED raw
 * inactive for the active-low LED (raw high). */
ZTEST(user_pairing_io, test_init_status_inactive_unpressed)
{
	init_ok();

	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_true(st.initialized);
	zassert_false(st.pressed);
	zassert_false(st.led_active);
	zassert_false(st.bonding_threshold_armed);
	zassert_false(st.reset_threshold_armed);
	zassert_equal(st.hold_generation, 0);
	zassert_equal(st.last_error, 0);

	/* Active-low LED, GPIO_OUTPUT_INACTIVE: raw pin high. */
	zassert_equal(led_raw(), 1, "LED raw not inactive after init");
	assert_no_requests();
}

/* Second init is rejected with -EALREADY. */
ZTEST(user_pairing_io, test_second_init_ealready)
{
	init_ok();
	zassert_equal(user_pairing_io_init(), -EALREADY);
}

/* LED logical active/inactive produce the correct active-low raw state;
 * the ctx value is ignored (P1 passes one shared operation context). */
ZTEST(user_pairing_io, test_led_active_low_raw_state)
{
	init_ok();
	zassert_equal(led_raw(), 1, "initial raw");

	int dummy_ctx;

	zassert_ok(user_pairing_io_led_set(true, NULL));
	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_true(st.led_active);
	zassert_equal(led_raw(), 0, "active-low active must be raw low");

	zassert_ok(user_pairing_io_led_set(false, NULL));
	user_pairing_io_get_status(&st);
	zassert_false(st.led_active);
	zassert_equal(led_raw(), 1, "active-low inactive must be raw high");

	/* Non-NULL ctx must be accepted and ignored. */
	zassert_ok(user_pairing_io_led_set(true, &dummy_ctx));
	user_pairing_io_get_status(&st);
	zassert_true(st.led_active);
	zassert_equal(led_raw(), 0);
}

/* LED GPIO failure returns the exact errno and the cached state does not
 * lie (the native_sim emul never fails, so the fault is injected through
 * the test-only seam). */
ZTEST(user_pairing_io, test_led_gpio_failure_errno_cached_state)
{
	init_ok();

	user_pairing_io_test_inject_led_error(EIO);
	zassert_equal(user_pairing_io_led_set(true, NULL), -EIO);

	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_false(st.led_active, "cached led_active lied after failure");
	zassert_equal(led_raw(), 1, "raw pin must be untouched on failure");

	user_pairing_io_test_inject_led_error(0);
	zassert_ok(user_pairing_io_led_set(true, NULL));
	user_pairing_io_get_status(&st);
	zassert_true(st.led_active);
	zassert_equal(led_raw(), 0);
}

/* A press shorter than the bonding threshold produces no request. */
ZTEST(user_pairing_io, test_short_press_no_request)
{
	init_ok();

	press();
	k_sleep(K_MSEC(60)); /* past the 30 ms debounce, before the 100 ms bond */
	release();
	k_sleep(K_MSEC(250)); /* past both threshold deadlines */

	assert_no_requests();
	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_false(st.pressed);
	zassert_false(st.bonding_threshold_armed);
	zassert_false(st.reset_threshold_armed);
}

/* Exactly the bonding threshold: one BONDING request while held. */
ZTEST(user_pairing_io, test_bond_threshold_exactly_once_while_held)
{
	init_ok();

	press();
	wait_bond(WAIT_MS);

	zassert_equal(fake_bonding_calls, 1, "bonding requested more than once");
	zassert_equal(fake_reset_calls, 0, "reset before its threshold");

	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_true(st.pressed, "button still held");
	zassert_false(st.bonding_threshold_armed, "bonding armed flag not cleared");
	zassert_true(st.reset_threshold_armed, "reset threshold still pending");

	release();
	k_sleep(K_MSEC(60));
}

/* Release after the bonding threshold (before reset) leaves exactly one
 * BONDING request and never a RESET. */
ZTEST(user_pairing_io, test_release_after_bonding_before_reset)
{
	init_ok();

	press();
	wait_bond(WAIT_MS);
	release();
	k_sleep(K_MSEC(300)); /* past the reset deadline from the press */

	zassert_equal(fake_bonding_calls, 1, "one bonding");
	zassert_equal(fake_reset_calls, 0, "reset must be cancelled by release");

	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_false(st.pressed);
	zassert_false(st.bonding_threshold_armed);
	zassert_false(st.reset_threshold_armed);
}

/* Continuous hold reaches BONDING then RESET exactly once each; the reset
 * supersedes but does not erase the observed earlier BONDING request. */
ZTEST(user_pairing_io, test_continuous_hold_bonding_then_reset)
{
	init_ok();

	press();
	wait_bond(WAIT_MS);
	zassert_equal(fake_reset_calls, 0, "reset fired before its threshold");
	wait_reset(WAIT_MS);

	zassert_equal(fake_bonding_calls, 1, "bonding exactly once");
	zassert_equal(fake_reset_calls, 1, "reset exactly once");

	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_true(st.pressed);
	zassert_false(st.bonding_threshold_armed);
	zassert_false(st.reset_threshold_armed);
	zassert_equal(st.hold_generation, 1, "one continuous hold generation");

	release();
	k_sleep(K_MSEC(60));
}

/* Release before the reset threshold cancels the pending RESET. */
ZTEST(user_pairing_io, test_release_before_reset_cancels_reset)
{
	init_ok();

	press();
	wait_bond(WAIT_MS);
	zassert_equal(fake_reset_calls, 0);
	release();
	k_sleep(K_MSEC(300));

	zassert_equal(fake_reset_calls, 0, "reset fired after release");
	zassert_equal(fake_bonding_calls, 1);
}

/* Duplicate press/release events are no-ops. */
ZTEST(user_pairing_io, test_duplicate_press_release_noop)
{
	init_ok();

	/* Press event lands ~31 ms after the emul edge (30 ms gpio-keys
	 * debounce + one tick); the bonding threshold fires ~100 ms after
	 * the EVENT, so the duplicate injection and the release must both
	 * happen well before ~131 ms. */
	press();
	k_sleep(K_MSEC(60)); /* press event processed, threshold armed */

	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_true(st.pressed);
	zassert_equal(st.hold_generation, 1);

	/* Duplicate press event (direct injection, no edge involved). */
	zassert_ok(input_report(g_input_dev, INPUT_EV_KEY, g_button_code, 1, true, K_NO_WAIT));
	k_sleep(K_MSEC(20));
	user_pairing_io_get_status(&st);
	zassert_true(st.pressed);
	zassert_equal(st.hold_generation, 1, "duplicate press bumped the generation");

	/* Release well before the bonding threshold deadline. */
	release();
	k_sleep(K_MSEC(80));
	user_pairing_io_get_status(&st);
	zassert_false(st.pressed);
	zassert_equal(st.hold_generation, 2);

	/* Duplicate release event: no-op. */
	zassert_ok(input_report(g_input_dev, INPUT_EV_KEY, g_button_code, 0, true, K_NO_WAIT));
	k_sleep(K_MSEC(40));
	user_pairing_io_get_status(&st);
	zassert_false(st.pressed);
	zassert_equal(st.hold_generation, 2, "duplicate release bumped the generation");
	assert_no_requests();
}

/* A bounce shorter than the gpio-keys debounce produces no threshold arm
 * and no request. */
ZTEST(user_pairing_io, test_bounce_shorter_than_debounce_no_arm)
{
	init_ok();

	press();
	k_sleep(K_MSEC(10)); /* well inside the 30 ms debounce window */
	release();
	k_sleep(K_MSEC(200)); /* past the debounce + thresholds */

	assert_no_requests();
	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_false(st.pressed);
	zassert_false(st.bonding_threshold_armed);
	zassert_false(st.reset_threshold_armed);
	zassert_equal(st.hold_generation, 0, "bounce armed a hold generation");
}

/* Stale bonding/reset work from prior hold generations cannot fire on a
 * new hold: two quick holds produce no request, then a real hold works. */
ZTEST(user_pairing_io, test_stale_prior_generation_work_no_fire)
{
	init_ok();

	/* Hold 1 (gen 1): released before the bonding threshold. */
	press();
	k_sleep(K_MSEC(50));
	release();
	k_sleep(K_MSEC(80));

	/* Hold 2 (gen 3): released before the bonding threshold. */
	press();
	k_sleep(K_MSEC(50));
	release();
	k_sleep(K_MSEC(300)); /* past every prior threshold deadline */

	assert_no_requests();
	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_false(st.pressed);
	zassert_false(st.bonding_threshold_armed);
	zassert_false(st.reset_threshold_armed);

	/* A real hold still works: the module is not wedged. */
	press();
	wait_bond(WAIT_MS);
	zassert_equal(fake_bonding_calls, 1, "real hold after stale holds");
	zassert_equal(fake_reset_calls, 0);
	release();
	k_sleep(K_MSEC(60));
}

/* Two complete holds produce independent generations and one request
 * each. */
ZTEST(user_pairing_io, test_two_complete_holds_independent_generations)
{
	init_ok();

	/* Hold 1: press -> BONDING -> release (gen 1 -> gen 2). */
	press();
	wait_bond(WAIT_MS);
	release();
	k_sleep(K_MSEC(80)); /* release event processed, works cancelled */

	/* Hold 2: press -> BONDING -> release (gen 3 -> gen 4). */
	press();
	wait_bond(WAIT_MS);
	release();
	k_sleep(K_MSEC(80));

	zassert_equal(fake_bonding_calls, 2, "one request per hold");
	zassert_equal(fake_reset_calls, 0);

	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_false(st.pressed);
	zassert_false(st.bonding_threshold_armed);
	zassert_false(st.reset_threshold_armed);
	zassert_equal(st.hold_generation, 4, "independent generations per hold");
}

/* A button held at initialization arms both thresholds from init time
 * (no gpio-keys event is needed for the boot-hold case). */
ZTEST(user_pairing_io, test_held_at_init_arms_thresholds)
{
	/* Press BEFORE init; the gpio-keys driver has already sampled its
	 * pin at POST_KERNEL, so no event fires — the module's own read
	 * must treat init time as the debounced press edge. */
	press();
	init_ok();

	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_true(st.pressed, "held button must be reflected at init");
	zassert_true(st.bonding_threshold_armed);
	zassert_true(st.reset_threshold_armed);

	wait_bond(WAIT_MS);
	zassert_equal(fake_bonding_calls, 1, "init-time hold must reach BONDING");
	zassert_equal(fake_reset_calls, 0);

	release();
	k_sleep(K_MSEC(300)); /* past the reset deadline */
	zassert_equal(fake_reset_calls, 0, "release after bond cancels reset");
}

/* Events from another input device, another key code, the wrong type, or
 * sync=false produce no request. */
ZTEST(user_pairing_io, test_foreign_events_ignored)
{
	init_ok();

	/* Another input device (NULL device; the registration filters by
	 * the gpio-keys device). */
	zassert_ok(input_report(NULL, INPUT_EV_KEY, g_button_code, 1, true, K_NO_WAIT));
	/* Another key code on our device. */
	zassert_ok(input_report(g_input_dev, INPUT_EV_KEY, (uint16_t)(g_button_code + 1), 1, true,
				K_NO_WAIT));
	/* Wrong event type. */
	zassert_ok(input_report(g_input_dev, INPUT_EV_REL, g_button_code, 1, true, K_NO_WAIT));
	/* sync=false (gpio-keys always reports sync=true). */
	zassert_ok(input_report(g_input_dev, INPUT_EV_KEY, g_button_code, 1, false, K_NO_WAIT));
	/* A release-shaped foreign event too. */
	zassert_ok(input_report(g_input_dev, INPUT_EV_KEY, g_button_code, 0, true, K_NO_WAIT));

	k_sleep(K_MSEC(150)); /* past the bonding threshold */

	assert_no_requests();
	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_false(st.pressed);
	zassert_false(st.bonding_threshold_armed);
	zassert_false(st.reset_threshold_armed);
	zassert_equal(st.hold_generation, 0, "foreign event started a hold");

	/* A real button press still works afterwards. */
	press();
	wait_bond(WAIT_MS);
	zassert_equal(fake_bonding_calls, 1, "real press after foreign events");
	release();
	k_sleep(K_MSEC(60));
}

/* A negative bonding request return is surfaced in status and never
 * retried or duplicated. */
ZTEST(user_pairing_io, test_fake_bonding_negative_no_retry)
{
	fake_bonding_ret = -EIO;
	init_ok();

	press();
	wait_bond(WAIT_MS);
	zassert_equal(fake_bonding_calls, 1, "bonding called once");

	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_equal(st.last_error, -EIO, "errno not surfaced in status");

	/* Keep holding past another bonding window: no retry. */
	k_sleep(K_MSEC(150));
	zassert_equal(fake_bonding_calls, 1, "negative return retried");
	release();
	k_sleep(K_MSEC(60));

	/* -ECANCELED (post-fatal) is logged and causes no duplicate
	 * recovery either. */
	suite_before(NULL);
	fake_bonding_ret = -ECANCELED;
	init_ok();
	press();
	wait_bond(WAIT_MS);
	zassert_equal(fake_bonding_calls, 1);
	user_pairing_io_get_status(&st);
	zassert_equal(st.last_error, -ECANCELED);
	k_sleep(K_MSEC(150));
	zassert_equal(fake_bonding_calls, 1, "post-fatal bonding retried");
	release();
	k_sleep(K_MSEC(60));
}

/* A negative reset request return is surfaced in status and never
 * retried or duplicated. */
ZTEST(user_pairing_io, test_fake_reset_negative_no_retry)
{
	fake_reset_ret = -EIO;
	init_ok();

	press();
	wait_bond(WAIT_MS);
	zassert_equal(fake_bonding_calls, 1);
	wait_reset(WAIT_MS);
	zassert_equal(fake_reset_calls, 1, "reset called once");

	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_equal(st.last_error, -EIO, "reset errno not surfaced");

	/* Keep holding past another reset window: no retry. */
	k_sleep(K_MSEC(250));
	zassert_equal(fake_reset_calls, 1, "negative reset retried");
	release();
	k_sleep(K_MSEC(60));
}

/* A hold-threshold scheduling failure cancels both thresholds, clears the
 * pressed/armed state, surfaces the errno in status, and is never retried
 * (the system work queue cannot reject on native_sim, so the fault is
 * injected through the test-only seam). */
ZTEST(user_pairing_io, test_schedule_failure_cancels_and_clears)
{
	init_ok();
	user_pairing_io_test_inject_schedule_error(EIO);

	press();
	k_sleep(K_MSEC(80)); /* press event processed; the schedule fails */

	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_equal(st.last_error, -EIO, "schedule errno not surfaced");
	zassert_false(st.pressed, "pressed state not cleared on schedule failure");
	zassert_false(st.bonding_threshold_armed, "bonding threshold still armed");
	zassert_false(st.reset_threshold_armed, "reset threshold still armed");
	assert_no_requests();

	/* No retry: waiting past the threshold deadlines changes nothing. */
	k_sleep(K_MSEC(200));
	assert_no_requests();

	/* With the seam cleared, a real hold works (no wedged state). */
	user_pairing_io_test_inject_schedule_error(0);
	release();
	k_sleep(K_MSEC(60));
	press();
	wait_bond(WAIT_MS);
	zassert_equal(fake_bonding_calls, 1, "hold after schedule failure");
	release();
	k_sleep(K_MSEC(60));
}

/* get_status(NULL) is a documented safe no-op. */
ZTEST(user_pairing_io, test_get_status_null_safe)
{
	user_pairing_io_get_status(NULL);
}

/* No operation occurs before a successful init: button events before
 * init are ignored, and the module starts clean afterwards. */
ZTEST(user_pairing_io, test_no_operation_before_init)
{
	press();
	k_sleep(K_MSEC(60)); /* press event delivered to the uninitialized module */
	release();
	k_sleep(K_MSEC(80));  /* release event delivered too */
	k_sleep(K_MSEC(200)); /* past every threshold deadline */

	assert_no_requests();
	struct user_pairing_io_status st;

	user_pairing_io_get_status(&st);
	zassert_false(st.initialized);
	zassert_false(st.pressed);

	init_ok();
	user_pairing_io_get_status(&st);
	zassert_true(st.initialized);
	zassert_false(st.pressed);
	zassert_false(st.bonding_threshold_armed);
	zassert_false(st.reset_threshold_armed);
	assert_no_requests();
}
