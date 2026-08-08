/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Direct tests of the production boot coordinator
 * (src/app_lifecycle.c).  Records ordered callback IDs with configurable
 * return codes; proves init order, cold-reboot-once semantics for every
 * fatal step, the nonfatal platform step, advertising restart, and
 * -EINVAL handling for malformed/null operations.
 */

#include <errno.h>
#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "app_lifecycle.h"

/* ── recorded callback model ─────────────────────────────────────── */

enum cb_id {
	CB_WATCHDOG = 1,
	CB_BLUETOOTH,
	CB_SETTINGS,
	CB_VOLUME,
	CB_BAP,
	CB_SINK,
	CB_PLATFORM,
	CB_ADVERTISING,
};

#define MAX_CALLS 16

static int cb_log[MAX_CALLS];
static int cb_count;

static int ret_watchdog = 0;
static int ret_bluetooth = 0;
static int ret_settings = 0;
static int ret_volume = 0;
static int ret_bap = 0;
static int ret_sink = 0;
static int ret_advertising = 0;
static int reboot_count = 0;

static void log_cb(enum cb_id id)
{
	if (cb_count < MAX_CALLS) {
		cb_log[cb_count++] = (int)id;
	}
}

static int cb_watchdog(void)
{
	log_cb(CB_WATCHDOG);
	return ret_watchdog;
}

static int cb_bluetooth(void)
{
	log_cb(CB_BLUETOOTH);
	return ret_bluetooth;
}

static int cb_settings(void)
{
	log_cb(CB_SETTINGS);
	return ret_settings;
}

static int cb_volume(void)
{
	log_cb(CB_VOLUME);
	return ret_volume;
}

static int cb_bap(void)
{
	log_cb(CB_BAP);
	return ret_bap;
}

static int cb_sink(void)
{
	log_cb(CB_SINK);
	return ret_sink;
}

static void cb_platform(void)
{
	log_cb(CB_PLATFORM);
}

static int cb_advertising(void)
{
	log_cb(CB_ADVERTISING);
	return ret_advertising;
}

static void cb_cold_reboot(void)
{
	reboot_count++;
}

static void reset_model(void)
{
	cb_count = 0;
	ret_watchdog = 0;
	ret_bluetooth = 0;
	ret_settings = 0;
	ret_volume = 0;
	ret_bap = 0;
	ret_sink = 0;
	ret_advertising = 0;
	reboot_count = 0;
}

static struct app_lifecycle_ops make_ops(bool with_platform)
{
	struct app_lifecycle_ops ops = {
		.watchdog_init = cb_watchdog,
		.bluetooth_init = cb_bluetooth,
		.settings_init = cb_settings,
		.volume_init = cb_volume,
		.bap_init = cb_bap,
		.sink_init = cb_sink,
		.platform_init = with_platform ? cb_platform : NULL,
		.advertising_start = cb_advertising,
		.cold_reboot = cb_cold_reboot,
	};

	return ops;
}

static void expect_order(const int *ids, int n)
{
	zassert_equal(cb_count, n, "call count %d != %d", cb_count, n);
	for (int i = 0; i < n; i++) {
		zassert_equal(cb_log[i], ids[i], "call %d id %d != %d", i, cb_log[i], ids[i]);
	}
}

/* ── tests ───────────────────────────────────────────────────────── */

ZTEST_SUITE(app_lifecycle, NULL, NULL, NULL, NULL, NULL);

/* Exact all-success order incl. platform init and initial advertising. */
ZTEST(app_lifecycle, test_boot_all_success_with_platform)
{
	reset_model();
	struct app_lifecycle_ops ops = make_ops(true);
	int expected[] = {CB_WATCHDOG, CB_BLUETOOTH, CB_SETTINGS, CB_VOLUME,
			  CB_BAP,      CB_SINK,      CB_PLATFORM, CB_ADVERTISING};

	zassert_equal(app_lifecycle_boot(&ops), 0);
	expect_order(expected, ARRAY_SIZE(expected));
	zassert_equal(reboot_count, 0);
}

/* Platform callback absent: same order minus the platform entry. */
ZTEST(app_lifecycle, test_boot_all_success_platform_absent)
{
	reset_model();
	struct app_lifecycle_ops ops = make_ops(false);
	int expected[] = {CB_WATCHDOG, CB_BLUETOOTH, CB_SETTINGS,   CB_VOLUME,
			  CB_BAP,      CB_SINK,      CB_ADVERTISING};

	zassert_equal(app_lifecycle_boot(&ops), 0);
	expect_order(expected, ARRAY_SIZE(expected));
	zassert_equal(reboot_count, 0);
}

/* Each of the seven fatal steps failing independently: no later
 * callback, exactly one cold reboot, original errno returned. */
ZTEST(app_lifecycle, test_boot_watchdog_failure)
{
	reset_model();
	ret_watchdog = -ENODEV;
	struct app_lifecycle_ops ops = make_ops(true);
	int expected[] = {CB_WATCHDOG};

	zassert_equal(app_lifecycle_boot(&ops), -ENODEV);
	expect_order(expected, ARRAY_SIZE(expected));
	zassert_equal(reboot_count, 1);
}

ZTEST(app_lifecycle, test_boot_bluetooth_failure)
{
	reset_model();
	ret_bluetooth = -EHOSTDOWN;
	struct app_lifecycle_ops ops = make_ops(true);
	int expected[] = {CB_WATCHDOG, CB_BLUETOOTH};

	zassert_equal(app_lifecycle_boot(&ops), -EHOSTDOWN);
	expect_order(expected, ARRAY_SIZE(expected));
	zassert_equal(reboot_count, 1);
}

ZTEST(app_lifecycle, test_boot_settings_failure)
{
	reset_model();
	ret_settings = -EIO;
	struct app_lifecycle_ops ops = make_ops(true);
	int expected[] = {CB_WATCHDOG, CB_BLUETOOTH, CB_SETTINGS};

	zassert_equal(app_lifecycle_boot(&ops), -EIO);
	expect_order(expected, ARRAY_SIZE(expected));
	zassert_equal(reboot_count, 1);
}

ZTEST(app_lifecycle, test_boot_volume_failure)
{
	reset_model();
	ret_volume = -ENOMEM;
	struct app_lifecycle_ops ops = make_ops(true);
	int expected[] = {CB_WATCHDOG, CB_BLUETOOTH, CB_SETTINGS, CB_VOLUME};

	zassert_equal(app_lifecycle_boot(&ops), -ENOMEM);
	expect_order(expected, ARRAY_SIZE(expected));
	zassert_equal(reboot_count, 1);
}

ZTEST(app_lifecycle, test_boot_bap_failure)
{
	reset_model();
	ret_bap = -EAGAIN;
	struct app_lifecycle_ops ops = make_ops(true);
	int expected[] = {CB_WATCHDOG, CB_BLUETOOTH, CB_SETTINGS, CB_VOLUME, CB_BAP};

	zassert_equal(app_lifecycle_boot(&ops), -EAGAIN);
	expect_order(expected, ARRAY_SIZE(expected));
	zassert_equal(reboot_count, 1);
}

ZTEST(app_lifecycle, test_boot_sink_failure)
{
	reset_model();
	ret_sink = -ENODEV;
	struct app_lifecycle_ops ops = make_ops(true);
	int expected[] = {CB_WATCHDOG, CB_BLUETOOTH, CB_SETTINGS, CB_VOLUME, CB_BAP, CB_SINK};

	zassert_equal(app_lifecycle_boot(&ops), -ENODEV);
	expect_order(expected, ARRAY_SIZE(expected));
	zassert_equal(reboot_count, 1);
}

/* Initial advertising failure: platform ran (nonfatal), advertising is
 * the final fatal step, one reboot, error returned. */
ZTEST(app_lifecycle, test_boot_advertising_failure)
{
	reset_model();
	ret_advertising = -EIO;
	struct app_lifecycle_ops ops = make_ops(true);
	int expected[] = {CB_WATCHDOG, CB_BLUETOOTH, CB_SETTINGS, CB_VOLUME,
			  CB_BAP,      CB_SINK,      CB_PLATFORM, CB_ADVERTISING};

	zassert_equal(app_lifecycle_boot(&ops), -EIO);
	expect_order(expected, ARRAY_SIZE(expected));
	zassert_equal(reboot_count, 1);
}

/* Restart success invokes only advertising, no reboot. */
ZTEST(app_lifecycle, test_restart_success)
{
	reset_model();
	struct app_lifecycle_ops ops = make_ops(true);
	int expected[] = {CB_ADVERTISING};

	zassert_equal(app_lifecycle_restart_advertising(&ops), 0);
	expect_order(expected, ARRAY_SIZE(expected));
	zassert_equal(reboot_count, 0);
}

/* Restart failure invokes advertising then one cold reboot, returns err. */
ZTEST(app_lifecycle, test_restart_failure)
{
	reset_model();
	ret_advertising = -EIO;
	struct app_lifecycle_ops ops = make_ops(true);
	int expected[] = {CB_ADVERTISING};

	zassert_equal(app_lifecycle_restart_advertising(&ops), -EIO);
	expect_order(expected, ARRAY_SIZE(expected));
	zassert_equal(reboot_count, 1);
}

/* NULL ops: -EINVAL, no calls, no reboot. */
ZTEST(app_lifecycle, test_null_ops_rejected)
{
	reset_model();

	zassert_equal(app_lifecycle_boot(NULL), -EINVAL);
	zassert_equal(app_lifecycle_restart_advertising(NULL), -EINVAL);
	zassert_equal(cb_count, 0);
	zassert_equal(reboot_count, 0);
}

/* Every missing required callback: -EINVAL without calls/reboot.
 * platform_init is optional and must NOT be rejected. */
ZTEST(app_lifecycle, test_missing_required_callbacks_rejected)
{
	reset_model();
	struct app_lifecycle_ops ops = make_ops(true);

	ops.watchdog_init = NULL;
	zassert_equal(app_lifecycle_boot(&ops), -EINVAL);
	zassert_equal(cb_count, 0);
	zassert_equal(reboot_count, 0);
	reset_model();

	ops = make_ops(true);
	ops.bluetooth_init = NULL;
	zassert_equal(app_lifecycle_boot(&ops), -EINVAL);
	zassert_equal(cb_count, 0);
	reset_model();

	ops = make_ops(true);
	ops.settings_init = NULL;
	zassert_equal(app_lifecycle_boot(&ops), -EINVAL);
	zassert_equal(cb_count, 0);
	reset_model();

	ops = make_ops(true);
	ops.volume_init = NULL;
	zassert_equal(app_lifecycle_boot(&ops), -EINVAL);
	zassert_equal(cb_count, 0);
	reset_model();

	ops = make_ops(true);
	ops.bap_init = NULL;
	zassert_equal(app_lifecycle_boot(&ops), -EINVAL);
	zassert_equal(cb_count, 0);
	reset_model();

	ops = make_ops(true);
	ops.sink_init = NULL;
	zassert_equal(app_lifecycle_boot(&ops), -EINVAL);
	zassert_equal(cb_count, 0);
	reset_model();

	ops = make_ops(true);
	ops.advertising_start = NULL;
	zassert_equal(app_lifecycle_boot(&ops), -EINVAL);
	zassert_equal(cb_count, 0);
	reset_model();

	ops = make_ops(true);
	ops.cold_reboot = NULL;
	zassert_equal(app_lifecycle_boot(&ops), -EINVAL);
	zassert_equal(cb_count, 0);
	reset_model();

	/* Restart path: advertising_start and cold_reboot are required. */
	ops = make_ops(true);
	ops.advertising_start = NULL;
	zassert_equal(app_lifecycle_restart_advertising(&ops), -EINVAL);
	zassert_equal(cb_count, 0);

	ops = make_ops(true);
	ops.cold_reboot = NULL;
	zassert_equal(app_lifecycle_restart_advertising(&ops), -EINVAL);
	zassert_equal(cb_count, 0);

	/* Optional platform_init must not be rejected. */
	ops = make_ops(true);
	ops.platform_init = NULL;
	zassert_equal(app_lifecycle_boot(&ops), 0);
	zassert_equal(reboot_count, 0);
	zassert_equal(cb_count, 7); /* no platform entry */
}
