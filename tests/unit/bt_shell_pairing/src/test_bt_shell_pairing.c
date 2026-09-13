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
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "pairing_mode.h"

/* AUDIO_SHELL_TEST seam wrapper exposed by src/bt_shell.c in test builds
 * only; declared here (the audio_shell suites share the same header). */
int audio_shell_test_cmd_bt_unpair(const struct shell *sh, size_t argc, char **argv);
int audio_shell_test_cmd_bt_identity(const struct shell *sh, size_t argc, char **argv);

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

/* ── fake bt_id_get (identity seam) ──────────────────────────────── */

#define TEST_MAX_IDENTITIES 8

static bt_addr_le_t g_fake_ids[TEST_MAX_IDENTITIES];
static size_t g_fake_id_count;
static size_t g_fake_bond_count;

/* The production `bt identity` command body calls this; the tests script
 * the identity table.  bt_addr_le_to_str() stays the real inline
 * formatter from the public addr.h header, never mocked. */
void bt_id_get(bt_addr_le_t *addrs, size_t *count)
{
	size_t n = MIN(*count, g_fake_id_count);

	memcpy(addrs, g_fake_ids, n * sizeof(bt_addr_le_t));
	*count = g_fake_id_count;
}

static void fake_ids_reset(void)
{
	memset(g_fake_ids, 0, sizeof(g_fake_ids));
	g_fake_id_count = 0;
}

void bt_foreach_bond(uint8_t id, void (*func)(const struct bt_bond_info *info, void *user_data),
		     void *user_data)
{
	struct bt_bond_info info = {0};
	size_t i;

	ARG_UNUSED(id);
	for (i = 0U; i < g_fake_bond_count; i++) {
		func(&info, user_data);
	}
}

static void fake_bonds_set(size_t count)
{
	g_fake_bond_count = count;
}

static void fake_ids_set(const bt_addr_le_t *ids, size_t count)
{
	fake_ids_reset();
	fake_bonds_set(0U);
	g_fake_id_count = MIN(count, TEST_MAX_IDENTITIES);
	memcpy(g_fake_ids, ids, g_fake_id_count * sizeof(bt_addr_le_t));
}

/* Canonical identity bytes that render as "DB:A6:0C:05:A2:AA": the shell
 * prints val[5]..val[0], so the array holds the reversed byte order. */
static void set_identity(bt_addr_le_t *id, uint8_t type, uint8_t b0, uint8_t b1, uint8_t b2,
			 uint8_t b3, uint8_t b4, uint8_t b5)
{
	id->type = type;
	id->a.val[0] = b0;
	id->a.val[1] = b1;
	id->a.val[2] = b2;
	id->a.val[3] = b3;
	id->a.val[4] = b4;
	id->a.val[5] = b5;
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

/* Receiver cleanup boundary: exact persisted-bond count, read only. */
ZTEST(bt_shell_pairing, test_bonds_reports_exact_count)
{
	fake_bonds_set(0U);
	int rc = -1;
	const char *out = run_cmd("bt bonds", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Bond count: 0");

	fake_bonds_set(3U);
	out = run_cmd("bt bonds", &rc);
	zassert_equal(rc, 0);
	assert_output_contains(out, "Bond count: 3");
}

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

/* ── bt identity (feature-on build) ──────────────────────────────── */

/* Success, public type: exact identity line, zero result, no pairing
 * reset involved. */
ZTEST(bt_shell_pairing, test_identity_success_public)
{
	fake_reset_reset();
	fake_ids_reset();
	bt_addr_le_t ids[1];

	set_identity(&ids[0], BT_ADDR_LE_PUBLIC, 0xAA, 0xA2, 0x05, 0x0C, 0xA6, 0xDB);
	fake_ids_set(ids, 1);

	int rc = -1;
	const char *out = run_cmd("bt identity", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Identity: DB:A6:0C:05:A2:AA (public)");
	zassert_equal(g_fake_calls, 0, "identity is read-only, no reset");
}

/* Success, random type: exact "(random)" suffix. */
ZTEST(bt_shell_pairing, test_identity_success_random)
{
	fake_reset_reset();
	fake_ids_reset();
	bt_addr_le_t ids[1];

	set_identity(&ids[0], BT_ADDR_LE_RANDOM, 0xAA, 0xA2, 0x05, 0x0C, 0xA6, 0xDB);
	fake_ids_set(ids, 1);

	int rc = -1;
	const char *out = run_cmd("bt identity", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Identity: DB:A6:0C:05:A2:AA (random)");
}

/* Identity aliases retain public/random-only HIL wire output. */
ZTEST(bt_shell_pairing, test_identity_public_id_normalizes_to_public)
{
	fake_reset_reset();
	fake_ids_reset();
	bt_addr_le_t ids[1];

	set_identity(&ids[0], BT_ADDR_LE_PUBLIC_ID, 0xAA, 0xA2, 0x05, 0x0C, 0xA6, 0xDB);
	fake_ids_set(ids, 1);

	int rc = -1;
	const char *out = run_cmd("bt identity", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Identity: DB:A6:0C:05:A2:AA (public)");
}

/* No usable identity: shell error text and -ENOENT, never a fabricated
 * address. */
ZTEST(bt_shell_pairing, test_identity_no_identity_fails)
{
	fake_reset_reset();
	fake_ids_reset();
	fake_ids_set(NULL, 0);

	int rc = 0;
	const char *out = run_cmd("bt identity", &rc);

	zassert_equal(rc, -ENOENT);
	assert_output_contains(out, "Identity unavailable.");
}

/* Deleted (BT_ADDR_LE_ANY) identity 0 is skipped; the first usable
 * identity is printed. */
ZTEST(bt_shell_pairing, test_identity_skips_any_and_prints_next)
{
	fake_reset_reset();
	fake_ids_reset();
	bt_addr_le_t ids[2];

	memset(&ids[0], 0, sizeof(ids[0]));
	set_identity(&ids[1], BT_ADDR_LE_PUBLIC, 0xAA, 0xA2, 0x05, 0x0C, 0xA6, 0xDB);
	fake_ids_set(ids, 2);

	int rc = -1;
	const char *out = run_cmd("bt identity", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Identity: DB:A6:0C:05:A2:AA (public)");
}

/* BT_ADDR_LE_ANY is public/all-zero only. A random zero identity remains
 * valid and must not be dropped by the cleanup identity boundary. */
ZTEST(bt_shell_pairing, test_identity_zero_random_is_usable)
{
	fake_reset_reset();
	fake_ids_reset();
	bt_addr_le_t ids[1];

	memset(&ids[0], 0, sizeof(ids[0]));
	ids[0].type = BT_ADDR_LE_RANDOM;
	fake_ids_set(ids, 1);

	int rc = -1;
	const char *out = run_cmd("bt identity", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Identity: 00:00:00:00:00:00 (random)");
}
