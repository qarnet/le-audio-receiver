/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Direct tests of the production feature-on `bt unpair` command body
 * (src/bt_shell.c compiled under CONFIG_USER_PAIRING_INPUT) through the
 * real Zephyr dummy backend and shell_execute_cmd(), against a fake link
 * implementation of pairing_mode_request_reset_sync() that records the
 * exact timeout argument and returns scripted results.
 *
 * Contract proven: the feature-on path calls the synchronous reset
 * exactly once per command, passes the configured
 * USER_PAIRING_SHELL_RESET_TIMEOUT_MS as the timeout argument, returns
 * the exact result, prints the exact success/error text, never claims
 * success on -ETIMEDOUT (the transition continues asynchronously), and
 * never references the legacy bt_bap_pairing_reset() — that symbol is
 * deliberately NOT provided in this test link, so compiling the legacy
 * branch would fail the build (a structural guarantee, not just an
 * assertion).
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/ztest.h>

#include "pairing_mode.h"

/* AUDIO_SHELL_TEST seam wrapper exposed by src/bt_shell.c in test builds
 * only; declared here (the audio_shell suites share the same header). */
int audio_shell_test_cmd_bt_unpair(const struct shell *sh, size_t argc, char **argv);

/* ── fake pairing_mode_request_reset_sync ────────────────────────── */

static k_timeout_t g_fake_timeout;
static int g_fake_result;
static int g_fake_calls;

/* The production command body calls this; the ledger records the exact
 * timeout argument so the tests can prove the configured Kconfig value
 * reaches the controller. */
int pairing_mode_request_reset_sync(k_timeout_t timeout)
{
	g_fake_calls++;
	g_fake_timeout = timeout;
	return g_fake_result;
}

static void fake_reset_reset(void)
{
	g_fake_calls = 0;
	g_fake_result = 0;
	g_fake_timeout = K_NO_WAIT;
}

/* ── dummy-backend shell harness (same pattern as audio_shell) ───── */

static const struct shell *active_shell(void)
{
	const struct shell *sh = shell_backend_dummy_get_ptr();
	int ret = shell_start(sh);

	zassert_true(ret == 0 || ret == -ENOTSUP, "shell_start: %d", ret);
	return sh;
}

static const char *run_cmd(const char *cmd, int *rc)
{
	const struct shell *sh = active_shell();

	shell_backend_dummy_clear_output(sh);
	int ret = shell_execute_cmd(sh, cmd);

	if (rc != NULL) {
		*rc = ret;
	}
	return shell_backend_dummy_get_output(sh, &(size_t){0});
}

static void assert_output_contains(const char *out, const char *needle)
{
	zassert_not_null(strstr(out, needle), "output missing '%s':\n%s", needle, out);
}

/* ── suite ───────────────────────────────────────────────────────── */

ZTEST_SUITE(bt_shell_pairing, NULL, NULL, NULL, NULL, NULL);

/* Success: exact feature-on text, exact result, one call, and the
 * configured timeout argument reaching the controller. */
ZTEST(bt_shell_pairing, test_success_exact_text_and_timeout)
{
	fake_reset_reset();
	g_fake_result = 0;

	int rc = -1;
	const char *out = run_cmd("bt unpair", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(
		out, "Pairing reset complete: bonds cleared; BONDING advertising active.");
	zassert_equal(g_fake_calls, 1, "exactly one synchronous reset");
	/* The timeout argument is the exact configured Kconfig value. */
	zassert_equal(g_fake_timeout.ticks,
		      k_ms_to_ticks_ceil32(CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS),
		      "exact configured timeout argument");
}

/* -ETIMEDOUT: the transition continues asynchronously — error text,
 * exact result, never success, never a second reset. */
ZTEST(bt_shell_pairing, test_etimedout_prints_error)
{
	fake_reset_reset();
	g_fake_result = -ETIMEDOUT;

	int rc = 0;
	const char *out = run_cmd("bt unpair", &rc);

	zassert_equal(rc, -ETIMEDOUT);
	char expected[64];

	snprintf(expected, sizeof(expected), "pairing_mode reset failed: %d", -ETIMEDOUT);
	assert_output_contains(out, expected);
	zassert_equal(g_fake_calls, 1, "no second reset on timeout");
}

/* Another errno: exact negative errno propagation. */
ZTEST(bt_shell_pairing, test_other_errno_propagation)
{
	fake_reset_reset();
	g_fake_result = -EACCES;

	int rc = 0;
	const char *out = run_cmd("bt unpair", &rc);

	zassert_equal(rc, -EACCES);
	char expected[64];

	snprintf(expected, sizeof(expected), "pairing_mode reset failed: %d", -EACCES);
	assert_output_contains(out, expected);
	zassert_equal(g_fake_calls, 1);
}

/* One call only across a full success path: the command never issues a
 * duplicate reset and never falls back to any legacy API (none exists
 * in this link). */
ZTEST(bt_shell_pairing, test_one_call_only_no_legacy)
{
	fake_reset_reset();
	g_fake_result = 0;

	int rc = -1;
	const char *out = run_cmd("bt unpair", &rc);

	zassert_equal(rc, 0);
	zassert_equal(g_fake_calls, 1, "exactly one call");
	zassert_equal(strstr(out, "bt_bap_pairing_reset"), NULL,
		      "no legacy API text in feature-on output");
}

/* The AUDIO_SHELL_TEST wrapper seam dispatches identically to the
 * registered command for the feature-on path. */
ZTEST(bt_shell_pairing, test_wrapper_seam_matches_dispatch)
{
	fake_reset_reset();
	g_fake_result = 0;

	const struct shell *sh = active_shell();

	shell_backend_dummy_clear_output(sh);
	int rc = audio_shell_test_cmd_bt_unpair(sh, 1, NULL);

	zassert_equal(rc, 0);
	zassert_equal(g_fake_calls, 1);
}
