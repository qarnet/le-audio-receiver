/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Direct tests of the production shell command bodies
 * (src/audio_shell.c) through the real Zephyr dummy backend and
 * shell_execute_cmd().  Subsystem state is controlled via the fakes;
 * audio_perf.c is production source with deterministic injection.
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
#include <zephyr/ztest.h>

#include "audio_perf.h"
#include "audio_shell_test.h"
#include "fake_audio_shell_deps.h"

/* ── helpers ─────────────────────────────────────────────────────── */

/* The dummy backend's shell thread runs at the lowest application
 * priority and may not have executed shell_start() before the test
 * thread does; drive the real shell state machine into ACTIVE
 * explicitly.  -ENOTSUP means it is already active. */
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

static void assert_output_has_line(const char *out, const char *line)
{
	char buf[256];
	size_t len = strlen(line);

	zassert_true(len < sizeof(buf) - 2, "line too long: %s", line);
	snprintf(buf, sizeof(buf), "\r\n%s\r\n", line);
	assert_output_contains(out, buf);
}

/* ── suite ───────────────────────────────────────────────────────── */

ZTEST_SUITE(audio_shell, NULL, NULL, NULL, NULL, NULL);

/* audio status: exact field order/labels and values. */
ZTEST(audio_shell, test_status_exact_fields)
{
	test_shell_reset_counters();
	struct audio_stats s = {
		.total_frames = 1234,
		.plc_frames = 56,
		.decode_errors = 2,
		.i2s_underruns = 3,
		.stream_resets = 1,
	};

	test_shell_set_stats(&s);
	test_shell_set_drift("STEADY", -5);
	test_shell_set_volume(195, true);

	const char *out = run_cmd("audio status", NULL);

	assert_output_has_line(out, "--- Audio status ---");
	assert_output_has_line(out, "  Frames decoded : 1234");
	assert_output_has_line(out, "  PLC frames     : 56 (4%)");
	assert_output_has_line(out, "  Decode errors  : 2");
	assert_output_has_line(out, "  I2S underruns  : 3");
	assert_output_has_line(out, "  Stream resets  : 1");
	assert_output_has_line(out, "  Drift state    : STEADY");
	assert_output_has_line(out, "  Drift ppm      : -5");
	assert_output_has_line(out, "  Resampler      : identity");
	assert_output_has_line(out, "  Volume         : 195 / 255 (muted)");
}

/* Zero frames: no division by zero, explicit (0%). */
ZTEST(audio_shell, test_status_zero_frames_no_div0)
{
	test_shell_reset_counters();
	struct audio_stats s = {0};

	test_shell_set_stats(&s);
	test_shell_set_drift("INIT", 0);
	test_shell_set_volume(0, false);

	const char *out = run_cmd("audio status", NULL);

	assert_output_has_line(out, "  Frames decoded : 0");
	assert_output_has_line(out, "  PLC frames     : 0 (0%)");
}

/* Unmuted volume: no suffix. */
ZTEST(audio_shell, test_status_unmuted_no_suffix)
{
	test_shell_reset_counters();
	struct audio_stats s = {.total_frames = 10};

	test_shell_set_stats(&s);
	test_shell_set_volume(200, false);

	const char *out = run_cmd("audio status", NULL);

	assert_output_has_line(out, "  Volume         : 200 / 255");
	zassert_is_null(strstr(out, "(muted)"), "unexpected muted suffix:\n%s", out);
}

/* Large plc/total: uint64 numerator must not overflow.
 * 4000000000 * 100 would wrap in uint32 (0x21D78400 % 2^32 = 0x21D78400*...);
 * expected truncating result: 4000000000 * 100 / 4294967295 = 93. */
ZTEST(audio_shell, test_status_large_percentage_no_overflow)
{
	test_shell_reset_counters();
	struct audio_stats s = {
		.total_frames = UINT32_MAX,
		.plc_frames = 4000000000U,
	};

	test_shell_set_stats(&s);

	const char *out = run_cmd("audio status", NULL);

	assert_output_has_line(out, "  PLC frames     : 4000000000 (93%)");
}

/* audio perf: path labels, exact values, one-decimal deadline %. */
#if defined(CONFIG_AUDIO_PERF_MEASUREMENT)
ZTEST(audio_shell, test_perf_exact_values_and_labels)
{
	test_shell_reset_counters();
	audio_perf_reset();

	/* native_sim: 1 cycle == 1 us (SYS_CLOCK_HW_CYCLES_PER_SEC=1000000),
	 * so max_us == max_cycles and permille = max_us * 1000 / 10000. */
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_ISO_RECV, 10000);
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_LC3_DECODE, 2000);
	audio_perf_queue_sample(3, 240);
	audio_perf_push_failure();
	audio_perf_repeat_fallback();
	audio_perf_asrc_capacity_failure();

	const char *out = run_cmd("audio perf", NULL);

	assert_output_has_line(out, "--- Performance ---");
	assert_output_has_line(
		out, "  Path          Count   Avg(cyc)  Avg(us)  Max(cyc)  Max(us)  %deadline");
	/* 10000 us * 1000 / 10000 = 1000 permille -> 100.0% */
	assert_output_contains(out, "iso_recv");
	assert_output_contains(out, "100.0%");
	assert_output_contains(out, "lc3_decode");
	/* 2000 us * 1000 / 10000 = 200 permille -> 20.0% */
	assert_output_contains(out, "20.0%");
	assert_output_contains(out, "volume");
	assert_output_contains(out, "sink_push");
	assert_output_contains(out, "asrc");
	assert_output_has_line(out, "  Queue:");
	assert_output_has_line(out, "    Slab free     : 3 / 3 (min/max)");
	assert_output_has_line(out, "    Output frames : 240 / 240 (min/max)");
	assert_output_has_line(out, "    Output blocks : 1");
	assert_output_has_line(out, "    Push failures : 1");
	assert_output_has_line(out, "    Repeat fb     : 1");
	assert_output_has_line(out, "    ASRC cap fail : 1");
}

/* audio perf: zero counts average to zero; no divide by zero. */
ZTEST(audio_shell, test_perf_zero_counts)
{
	test_shell_reset_counters();
	audio_perf_reset();

	const char *out = run_cmd("audio perf", NULL);

	assert_output_contains(out, "iso_recv");
	assert_output_contains(out, "0.0%");
	assert_output_has_line(out, "    Output blocks : 0");
}

/* audio perf: avg over multiple injections uses integer truncation. */
ZTEST(audio_shell, test_perf_average_integer_truncation)
{
	test_shell_reset_counters();
	audio_perf_reset();

	/* count=2, total=15000, avg=7500, max=10000 -> 100.0% */
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_VOLUME, 5000);
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_VOLUME, 10000);

	const char *out = run_cmd("audio perf", NULL);

	assert_output_contains(out, "7500");
	assert_output_contains(out, "100.0%");
}

/* audio perf-reset: zeroes production perf state observably. */
ZTEST(audio_shell, test_perf_reset_observable_zeroing)
{
	test_shell_reset_counters();
	audio_perf_reset();
	audio_perf_test_inject_cycles(AUDIO_PERF_PATH_SINK_PUSH, 10000);

	int rc = -1;
	const char *out = run_cmd("audio perf-reset", &rc);

	zassert_equal(rc, 0);
	assert_output_has_line(out, "Perf counters cleared.");

	out = run_cmd("audio perf", NULL);
	assert_output_contains(out, "sink_push");
	/* After reset the path must be zeroed: count 0, max 0 -> 0.0%. */
	assert_output_contains(out, "0.0%");
}
#else  /* !CONFIG_AUDIO_PERF_MEASUREMENT */
/* Measurement disabled: production snapshot is a zeroed no-op and no
 * deadline exists.  The command must print a truthful unavailable (zero)
 * deadline percentage — never a fake value computed against a made-up
 * deadline — and stay division-safe.  Table shape stays identical. */
ZTEST(audio_shell, test_perf_disabled_truthful_unavailable)
{
	test_shell_reset_counters();
	const char *out = run_cmd("audio perf", NULL);

	assert_output_has_line(out, "--- Performance ---");
	assert_output_contains(out, "iso_recv");
	assert_output_contains(out, "lc3_decode");
	assert_output_contains(out, "volume");
	assert_output_contains(out, "sink_push");
	assert_output_contains(out, "asrc");
	/* Every path: count 0 and a zero/unavailable deadline percentage. */
	assert_output_contains(out, "0.0%");
	assert_output_has_line(out, "  Queue:");
	assert_output_has_line(out, "    Slab free     : 0 / 0 (min/max)");
	assert_output_has_line(out, "    Output frames : 0 / 0 (min/max)");
	assert_output_has_line(out, "    Output blocks : 0");
	assert_output_has_line(out, "    Push failures : 0");
	assert_output_has_line(out, "    Repeat fb     : 0");
	assert_output_has_line(out, "    ASRC cap fail : 0");
}
#endif /* CONFIG_AUDIO_PERF_MEASUREMENT */

/* audio reset-stats: calls the dependency exactly once, stable text. */
ZTEST(audio_shell, test_reset_stats_exactly_once)
{
	test_shell_reset_counters();
	zassert_equal(test_shell_stats_reset_calls(), 0);

	int rc = -1;
	const char *out = run_cmd("audio reset-stats", &rc);

	zassert_equal(rc, 0);
	assert_output_has_line(out, "Stats cleared.");
	zassert_equal(test_shell_stats_reset_calls(), 1);
}

/* audio stop: calls the BAP audio-path stop exactly once; the old direct
 * sink stop is no longer called; output and return stay exact. */
ZTEST(audio_shell, test_stop_exactly_once)
{
	test_shell_reset_counters();
	zassert_equal(test_shell_bap_path_stop_calls(), 0);
	zassert_equal(test_shell_sink_stop_calls(), 0);

	int rc = -1;
	const char *out = run_cmd("audio stop", &rc);

	zassert_equal(rc, 0);
	assert_output_has_line(out, "I2S stopped; drift reset.");
	zassert_equal(test_shell_bap_path_stop_calls(), 1, "BAP path stop called exactly once");
	zassert_equal(test_shell_sink_stop_calls(), 0, "direct sink stop not called");
}

/* bt unpair success: zero result and stable pairing-reset text. */
ZTEST(audio_shell, test_bt_unpair_success)
{
	test_shell_reset_counters();
	test_shell_set_unpair_result(0);

	int rc = -1;
	const char *out = run_cmd("bt unpair", &rc);

	zassert_equal(rc, 0);
	assert_output_has_line(out, "Pairing mode reset: bonds cleared; open pairing enabled.");
	zassert_equal(test_shell_unpair_calls(), 1);
}

/* bt unpair failure: error text and exact negative errno propagation. */
ZTEST(audio_shell, test_bt_unpair_error_propagation)
{
	test_shell_reset_counters();
	test_shell_set_unpair_result(-EACCES);

	int rc = 0;
	const char *out = run_cmd("bt unpair", &rc);

	zassert_equal(rc, -EACCES);
	assert_output_contains(out, "bt_bap_pairing_reset failed: -13");
}

/* The AUDIO_SHELL_TEST wrapper seam behaves identically to the
 * registered command dispatch for the same handler. */
ZTEST(audio_shell, test_wrapper_seam_matches_dispatch)
{
	test_shell_reset_counters();
	struct audio_stats s = {.total_frames = 7, .plc_frames = 1};

	test_shell_set_stats(&s);
	test_shell_set_drift("STEADY", 0);
	test_shell_set_volume(100, false);

	const struct shell *sh = active_shell();
	int rc = -1;

	shell_backend_dummy_clear_output(sh);
	rc = audio_shell_test_cmd_status(sh, 1, NULL);
	zassert_equal(rc, 0);

	const char *out = run_cmd("audio status", NULL);

	assert_output_has_line(out, "  Frames decoded : 7");
	assert_output_has_line(out, "  PLC frames     : 1 (14%)");
}

/* This suite compiles only the audio/bt shell TUs (no FLPR shell, no
 * acceptance TU) — the same configuration-off shape as the nRF5340 target
 * and a normal audio diagnostics build.  The FLPR acceptance commands must
 * not exist in the registry: unknown command, never acceptance output. */
ZTEST(audio_shell, test_flpr_acceptance_absent_config_off)
{
	test_shell_reset_counters();

	int rc = 0;
	const char *out = run_cmd("flpr ring status", &rc);

	zassert_not_equal(rc, 0,
			  "flpr ring status must not resolve without "
			  "CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS");
	zassert_is_null(strstr(out, "FLPR PCM rings"), "acceptance output must not appear:\n%s",
			out);

	rc = 0;
	out = run_cmd("flpr hang", &rc);
	zassert_not_equal(rc, 0,
			  "flpr hang must not resolve without "
			  "CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS");

	rc = 0;
	out = run_cmd("flpr status", &rc);
	zassert_not_equal(rc, 0,
			  "flpr status must not resolve without "
			  "CONFIG_SOC_NRF54L15 FLPR TUs");
}
