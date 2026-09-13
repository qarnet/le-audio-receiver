/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * HIL1 output queue and single writer (RH1B).
 *
 * One queue (eight lines of 1024 bytes plus length) and one writer thread.
 * Validation happens before enqueue: a rejected line is never truncated
 * and never partially emitted.  A queue timeout on submit is a fatal
 * runtime error the caller (coordinator) turns into an abort.  Under
 * CONFIG_HIL_SOURCE_APP_TEST the writer thread is replaced by a
 * test-owned drain so native tests capture exact submitted lines without
 * a shell.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/sys/util.h>

#include "hil_source_output.h"

struct hil_source_output_line {
	char data[HIL_SOURCE_OUTPUT_LINE_SIZE];
	uint16_t len;
};

K_MSGQ_DEFINE(output_queue, sizeof(struct hil_source_output_line), HIL_SOURCE_OUTPUT_QUEUE_DEPTH,
	      4);
static bool output_active;

#if !defined(CONFIG_HIL_SOURCE_APP_TEST)
/* Each dequeue owns one 1026-byte output line on stack before shell_fprintf()
 * adds its call depth. Keep enough bounded headroom for both operations. */
K_THREAD_STACK_DEFINE(output_writer_stack, 4096);
static struct k_thread output_writer_thread;

static void output_writer_fn(void *a, void *b, void *c)
{
	const struct shell *sh = shell_backend_uart_get_ptr();
	struct hil_source_output_line line;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		if (k_msgq_get(&output_queue, &line, K_FOREVER) != 0) {
			continue;
		}
		if (sh != NULL) {
			shell_fprintf(sh, SHELL_NORMAL, "%.*s\n", (int)line.len, line.data);
		}
	}
}
#endif /* !CONFIG_HIL_SOURCE_APP_TEST */

#ifdef CONFIG_HIL_SOURCE_APP_TEST
static void (*capture_fn)(const char *line, size_t len, void *arg);
static void *capture_arg;
#endif

int hil_source_output_init(void)
{
	if (output_active) {
		return 0; /* idempotent */
	}
#if !defined(CONFIG_HIL_SOURCE_APP_TEST)
	/* Production output requires a live serial shell backend before the
	 * writer may start; without it every line would be silently dropped.
	 * The test build stays capture-backed and never requires the shell. */
	if (shell_backend_uart_get_ptr() == NULL) {
		return -ENODEV;
	}
#endif /* !CONFIG_HIL_SOURCE_APP_TEST */
	k_msgq_purge(&output_queue);
	output_active = true;

#if !defined(CONFIG_HIL_SOURCE_APP_TEST)
	k_thread_create(&output_writer_thread, output_writer_stack,
			K_KERNEL_STACK_SIZEOF(output_writer_stack), output_writer_fn, NULL, NULL,
			NULL, K_PRIO_PREEMPT(6), 0, K_NO_WAIT);
	k_thread_name_set(&output_writer_thread, "hil_output");
#endif /* !CONFIG_HIL_SOURCE_APP_TEST */

	return 0;
}

bool hil_source_output_active(void)
{
	return output_active;
}

int hil_source_output_submit(const char *line, size_t len, uint32_t timeout_ms)
{
	struct hil_source_output_line entry;
	k_timeout_t timeout;
	size_t i;

	if (!output_active || line == NULL) {
		return -ENODEV;
	}
	if (len > HIL_SOURCE_OUTPUT_LINE_SIZE) {
		return -EOVERFLOW;
	}
	/* Only HIL1-prefixed records may leave the fixture; everything else
	 * is rejected up front. */
	if (len < 5U || memcmp(line, "HIL1 ", 5U) != 0) {
		return -EINVAL;
	}
	for (i = 0U; i < len; i++) {
		char c = line[i];

		if (c == '\r' || c == '\n' || c == '\0') {
			return -EINVAL;
		}
	}
	if (len == 0U) {
		return -EINVAL;
	}

	entry.len = (uint16_t)len;
	memcpy(entry.data, line, len);

	timeout = (timeout_ms == 0U) ? K_NO_WAIT : K_MSEC(timeout_ms);
	if (k_msgq_put(&output_queue, &entry, timeout) != 0) {
		return -EAGAIN;
	}
	return 0;
}

#ifdef CONFIG_HIL_SOURCE_APP_TEST
void hil_source_output_test_set_capture(void (*fn)(const char *line, size_t len, void *arg),
					void *arg)
{
	capture_fn = fn;
	capture_arg = arg;
}

uint32_t hil_source_output_test_drain(void)
{
	struct hil_source_output_line entry;
	uint32_t drained = 0U;

	while (k_msgq_get(&output_queue, &entry, K_NO_WAIT) == 0) {
		if (capture_fn != NULL) {
			capture_fn(entry.data, entry.len, capture_arg);
		}
		drained++;
	}
	return drained;
}
#endif /* CONFIG_HIL_SOURCE_APP_TEST */
