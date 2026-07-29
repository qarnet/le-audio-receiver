/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared BSIM test helpers — PASS/FAIL macros and bst_result extern.
 */
#ifndef BSIM_TEST_HELPERS_H
#define BSIM_TEST_HELPERS_H

#include <bs_tracing.h>
#include <bstests.h>

extern enum bst_result_t bst_result;

#define FAIL(...)                                                                                  \
	do {                                                                                       \
		bst_result = Failed;                                                               \
		bs_trace_error_time_line(__VA_ARGS__);                                             \
	} while (0)

#define PASS(...)                                                                                  \
	do {                                                                                       \
		bst_result = Passed;                                                               \
		bs_trace_info_time(1, __VA_ARGS__);                                                \
	} while (0)

#endif /* BSIM_TEST_HELPERS_H */
