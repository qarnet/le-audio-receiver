/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * HIL1 output queue and single writer (RH1B).
 *
 * Every HIL1 line, command response, and async event leaves the fixture
 * through this one module: one queue and one writer thread.  No direct
 * printk/printf/shell_print/shell_fprintf outside hil_source_output.c.
 */

#ifndef HIL_SOURCE_OUTPUT_H
#define HIL_SOURCE_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Queue holds eight complete lines of 1024 bytes plus length. 1024 bytes
 * fits the maximum legal command/run IDs (63/64) in a two-stream status
 * record; the TX SDU path is separately bounded by HIL_SOURCE_TX_SDU_MAX
 * and never uses this scratch. */
#define HIL_SOURCE_OUTPUT_LINE_SIZE   1024U
#define HIL_SOURCE_OUTPUT_QUEUE_DEPTH 8U

/* Initialize the output queue and start the writer thread (production
 * build).  Under CONFIG_HIL_SOURCE_APP_TEST no thread is started; the
 * test drains the queue through hil_source_output_test_drain().  Returns
 * 0 or a negative errno. */
int hil_source_output_init(void);

/* True once init succeeded and before any shutdown; inactive output
 * rejects submissions. */
bool hil_source_output_active(void);

/* Submit one complete line (no line terminator).  Rejects a non-HIL1
 * prefix, an embedded CR/LF/NUL, an overlong line, or an inactive output;
 * never truncates.  `timeout_ms` bounds the queue wait (the worker and
 * dispatch paths use 1000 ms; a queue timeout is a fatal runtime error
 * and triggers an abort where a run exists).  Returns 0 or a negative
 * errno. */
int hil_source_output_submit(const char *line, size_t len, uint32_t timeout_ms);

#ifdef CONFIG_HIL_SOURCE_APP_TEST
/* Test seam: set the capture callback invoked for every drained line.
 * Passing a NULL fn disables capture (lines are still drained). */
void hil_source_output_test_set_capture(void (*fn)(const char *line, size_t len, void *arg),
					void *arg);

/* Drain every queued line through the capture callback and return the
 * number of drained lines. */
uint32_t hil_source_output_test_drain(void);
#endif /* CONFIG_HIL_SOURCE_APP_TEST */

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_OUTPUT_H */
