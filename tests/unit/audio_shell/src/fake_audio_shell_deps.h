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
#include <stdint.h>

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

/* ---- bt unpair ---- */
void test_shell_set_unpair_result(int result);
int test_shell_unpair_calls(void);

/* ---- perf (real production audio_perf.c) ---- */
void test_shell_perf_reset_state(void);

#endif /* FAKE_AUDIO_SHELL_DEPS_H */
