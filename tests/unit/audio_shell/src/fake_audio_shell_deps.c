/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock implementations of the public subsystem APIs that
 * src/audio_shell.c calls.  audio_perf_* comes from the real production
 * audio_perf.c; everything else is controlled here.
 */

#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/util.h>

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
static struct bt_bap_iso_link_quality test_iso_link_quality[TEST_SHELL_MAX_ISO_LINK_QUALITY];
static size_t test_iso_link_quality_count;
static int test_iso_link_quality_result;

/* ---- bt identity (fake bt_id_get) ---- */

static bt_addr_le_t test_identities[TEST_SHELL_MAX_IDENTITIES];
static size_t test_identity_count;

/* The production bt_shell identity command calls this; the test suites
 * script the identity table.  bt_addr_le_to_str() itself is the real
 * inline formatter from the public addr.h header, never mocked. */
void bt_id_get(bt_addr_le_t *addrs, size_t *count)
{
	size_t n = MIN(*count, test_identity_count);

	memcpy(addrs, test_identities, n * sizeof(bt_addr_le_t));
	*count = test_identity_count;
}

void test_shell_set_identities(const bt_addr_le_t *ids, size_t count)
{
	size_t n = MIN(count, TEST_SHELL_MAX_IDENTITIES);

	test_identity_count = n;
	memcpy(test_identities, ids, n * sizeof(bt_addr_le_t));
}

const bt_addr_le_t *test_shell_identity_at(size_t index)
{
	if (index >= test_identity_count) {
		return NULL;
	}
	return &test_identities[index];
}

size_t test_shell_identity_count(void)
{
	return test_identity_count;
}

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
	test_iso_link_quality_count = 0U;
	test_iso_link_quality_result = 0;
	/* Default identity table: exactly one usable public identity so the
	 * identity command never fails by accident in unrelated tests. */
	test_identities[0].type = BT_ADDR_LE_PUBLIC;
	memset(test_identities[0].a.val, 0, sizeof(test_identities[0].a.val));
	test_identities[0].a.val[0] = 0xAA;
	test_identities[0].a.val[5] = 0x01;
	test_identity_count = 1;
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

/* ---- BAP audio-path stop (shell 'audio stop' routes through it) ---- */

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

/* ---- bt ISO link quality (fake public API for shell boundary tests) ---- */

int bt_bap_iso_link_quality_get_active(struct bt_bap_iso_link_quality *snapshots, size_t capacity,
				       size_t *count)
{
	if (test_iso_link_quality_result != 0) {
		return test_iso_link_quality_result;
	}
	if (capacity < test_iso_link_quality_count) {
		return -ENOSPC;
	}

	memcpy(snapshots, test_iso_link_quality, test_iso_link_quality_count * sizeof(*snapshots));
	*count = test_iso_link_quality_count;
	return 0;
}

void test_shell_set_iso_link_quality(const struct bt_bap_iso_link_quality *snapshots, size_t count,
				     int result)
{
	size_t n = MIN(count, TEST_SHELL_MAX_ISO_LINK_QUALITY);

	test_iso_link_quality_count = n;
	test_iso_link_quality_result = result;
	memcpy(test_iso_link_quality, snapshots, n * sizeof(*snapshots));
}

/* ---- perf ---- */

void test_shell_perf_reset_state(void)
{
	/* The shell test drives real audio_perf.c; nothing to reset here. */
}
