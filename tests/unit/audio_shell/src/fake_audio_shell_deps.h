/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-local fakes of the subsystem APIs consumed by src/audio_shell.c,
 * with knobs for the shell test suites.  audio_perf.c is compiled for
 * real (production instrumentation); everything else is faked here.
 */

#ifndef FAKE_AUDIO_SHELL_DEPS_H
#define FAKE_AUDIO_SHELL_DEPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/addr.h>

#include "bt_bap.h"
#include "audio_stats.h"

/* ---- stats ---- */
void test_shell_set_stats(const struct audio_stats *s);
int test_shell_stats_reset_calls(void);

/* ---- call counters (reset between tests) ---- */
void test_shell_reset_counters(void);

/* ---- drift ---- */
void test_shell_set_drift(const char *state, int32_t ppm);

/* ---- volume ---- */
void test_shell_set_volume(uint8_t vol, bool muted);

/* ---- sink ---- */
int test_shell_sink_stop_calls(void);

/* ---- BAP audio-path stop (R1) ---- */
int test_shell_bap_path_stop_calls(void);

/* ---- bt unpair ---- */
void test_shell_set_unpair_result(int result);
int test_shell_unpair_calls(void);

/* ---- bt ISO link quality ---- */
#define TEST_SHELL_MAX_ISO_LINK_QUALITY 2
void test_shell_set_iso_link_quality(const struct bt_bap_iso_link_quality *snapshots, size_t count,
				     int result);

/* ---- bt identity ---- */
/* Fake bt_id_get() identity table.  test_shell_set_identities() copies up
 * to TEST_SHELL_MAX_IDENTITIES entries; test_shell_reset_counters() resets
 * to exactly one usable public identity so unrelated tests never observe
 * "Identity unavailable." by accident. */
#define TEST_SHELL_MAX_IDENTITIES 8
void test_shell_set_identities(const bt_addr_le_t *ids, size_t count);
const bt_addr_le_t *test_shell_identity_at(size_t index);
size_t test_shell_identity_count(void);

/* ---- perf (real production audio_perf.c) ---- */
void test_shell_perf_reset_state(void);

#endif /* FAKE_AUDIO_SHELL_DEPS_H */
