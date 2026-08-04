/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock implementations of the public subsystem APIs that
 * src/audio_shell.c calls.  audio_perf_* comes from the real production
 * audio_perf.c; everything else is controlled here.
 */

#include <stdbool.h>
#include <stdint.h>

#include "audio_drift.h"
#include "audio_sink.h"
#include "audio_stats.h"
#include "audio_volume.h"
#include "fake_audio_shell_deps.h"

/* Forward declarations of counters defined below (sink/unpair sections). */
static int sink_stop_calls;
static int unpair_result;
static int unpair_calls;
static int bap_path_stop_calls;

/* ---- stats ---- */

static struct audio_stats test_stats;
static int stats_reset_calls;

struct audio_stats audio_stats_get(void)
{
	return test_stats;
}

void audio_stats_reset(void)
{
	stats_reset_calls++;
}

void test_shell_set_stats(const struct audio_stats *s)
{
	test_stats = *s;
}

int test_shell_stats_reset_calls(void)
{
	return stats_reset_calls;
}

/* ---- call counters (reset between tests) ---- */

void test_shell_reset_counters(void)
{
	stats_reset_calls = 0;
	sink_stop_calls = 0;
	unpair_calls = 0;
	unpair_result = 0;
	bap_path_stop_calls = 0;
}

/* ---- drift ---- */

static const char *drift_state = "INIT";
static int32_t drift_ppm;

const char *audio_drift_state_str(void)
{
	return drift_state;
}

int32_t audio_drift_get_ppm(void)
{
	return drift_ppm;
}

void test_shell_set_drift(const char *state, int32_t ppm)
{
	drift_state = state;
	drift_ppm = ppm;
}

/* ---- volume ---- */

static uint8_t vol_value;
static bool muted;

uint8_t audio_volume_get(void)
{
	return vol_value;
}

bool audio_volume_is_muted(void)
{
	return muted;
}

void test_shell_set_volume(uint8_t vol, bool m)
{
	vol_value = vol;
	muted = m;
}

/* ---- sink ---- */

void audio_sink_stop(void)
{
	sink_stop_calls++;
}

int test_shell_sink_stop_calls(void)
{
	return sink_stop_calls;
}

/* ---- BAP audio-path stop (R1: shell 'audio stop' routes through it) ---- */

void bt_bap_audio_path_stop(void)
{
	bap_path_stop_calls++;
}

int test_shell_bap_path_stop_calls(void)
{
	return bap_path_stop_calls;
}

/* ---- bt unpair (production pairing-mode reset) ---- */

int bt_bap_pairing_reset(void)
{
	unpair_calls++;
	return unpair_result;
}

void test_shell_set_unpair_result(int result)
{
	unpair_result = result;
}

int test_shell_unpair_calls(void)
{
	return unpair_calls;
}

/* ---- perf ---- */

void test_shell_perf_reset_state(void)
{
	/* The shell test drives real audio_perf.c; nothing to reset here. */
}
