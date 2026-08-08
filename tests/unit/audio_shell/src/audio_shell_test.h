/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Declarations of the AUDIO_SHELL_TEST seam wrappers compiled into
 * src/audio_shell.c when AUDIO_SHELL_TEST is defined (test builds only;
 * never present in production firmware).
 */

#ifndef AUDIO_SHELL_TEST_H
#define AUDIO_SHELL_TEST_H

#include <stddef.h>

#include <zephyr/shell/shell.h>

int audio_shell_test_cmd_status(const struct shell *sh, size_t argc, char **argv);
int audio_shell_test_cmd_reset_stats(const struct shell *sh, size_t argc, char **argv);
int audio_shell_test_cmd_stop(const struct shell *sh, size_t argc, char **argv);
int audio_shell_test_cmd_perf(const struct shell *sh, size_t argc, char **argv);
int audio_shell_test_cmd_perf_reset(const struct shell *sh, size_t argc, char **argv);
int audio_shell_test_cmd_bt_unpair(const struct shell *sh, size_t argc, char **argv);

#endif /* AUDIO_SHELL_TEST_H */
