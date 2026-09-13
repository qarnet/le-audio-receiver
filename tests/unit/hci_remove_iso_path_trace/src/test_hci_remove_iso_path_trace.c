/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "hci_remove_iso_path_trace.h"

struct fake_trace_deps {
	const char *lookup_names[2];
	int lookup_ids[2];
	size_t lookup_count;
	int filter_ids[2];
	uint32_t filter_levels[2];
	uint32_t filter_returns[2];
	size_t filter_count;
};

static struct fake_trace_deps fake;

static int fake_source_id_get(const char *name)
{
	zassert_true(fake.lookup_count < 2U, "unexpected source lookup");
	fake.lookup_names[fake.lookup_count] = name;
	return fake.lookup_ids[fake.lookup_count++];
}

static uint32_t fake_filter_set(int source_id, uint32_t level)
{
	zassert_true(fake.filter_count < 2U, "unexpected filter mutation");
	fake.filter_ids[fake.filter_count] = source_id;
	fake.filter_levels[fake.filter_count] = level;
	return fake.filter_returns[fake.filter_count++];
}

static const struct hci_remove_iso_path_trace_ops ops = {
	.source_id_get = fake_source_id_get,
	.filter_set = fake_filter_set,
};

static void reset_fake(void)
{
	memset(&fake, 0, sizeof(fake));
	fake.lookup_ids[0] = 7;
	fake.lookup_ids[1] = 9;
	fake.filter_returns[0] = 4U;
	fake.filter_returns[1] = 4U;
}

ZTEST_SUITE(hci_remove_iso_path_trace, NULL, NULL, NULL, NULL, NULL);

ZTEST(hci_remove_iso_path_trace, test_success_exact_lookup_mutation_order)
{
	struct hci_remove_iso_path_trace_result result;

	reset_fake();
	int ret = hci_remove_iso_path_trace_arm(&ops, 4U, &result);

	zassert_equal(ret, 0, "arm success");
	zassert_equal(fake.lookup_count, 2U, "lookup count");
	zassert_equal(strcmp(fake.lookup_names[0], "bt_hci_core"), 0, "core lookup");
	zassert_equal(strcmp(fake.lookup_names[1], "bt_sdc_hci_driver"), 0, "driver lookup");
	zassert_equal(fake.filter_count, 2U, "filter count");
	zassert_equal(fake.filter_ids[0], 7, "core filter order");
	zassert_equal(fake.filter_ids[1], 9, "driver filter order");
	zassert_equal(fake.filter_levels[0], 4U, "core requested level");
	zassert_equal(fake.filter_levels[1], 4U, "driver requested level");
	zassert_equal(result.core_source_id, 7, "core source ID");
	zassert_equal(result.driver_source_id, 9, "driver source ID");
	zassert_equal(result.core_level, 4U, "core returned level");
	zassert_equal(result.driver_level, 4U, "driver returned level");
}

ZTEST(hci_remove_iso_path_trace, test_null_dependency_or_result_rejected_without_calls)
{
	struct hci_remove_iso_path_trace_result result;
	const struct hci_remove_iso_path_trace_ops no_source = {
		.source_id_get = NULL,
		.filter_set = fake_filter_set,
	};
	const struct hci_remove_iso_path_trace_ops no_filter = {
		.source_id_get = fake_source_id_get,
		.filter_set = NULL,
	};

	reset_fake();
	zassert_equal(hci_remove_iso_path_trace_arm(NULL, 4U, &result), -EINVAL, "null operations");
	zassert_equal(hci_remove_iso_path_trace_arm(&no_source, 4U, &result), -EINVAL,
		      "null source callback");
	zassert_equal(hci_remove_iso_path_trace_arm(&no_filter, 4U, &result), -EINVAL,
		      "null filter callback");
	zassert_equal(hci_remove_iso_path_trace_arm(&ops, 4U, NULL), -EINVAL, "null result");
	zassert_equal(fake.lookup_count, 0U, "lookup untouched");
	zassert_equal(fake.filter_count, 0U, "filter untouched");
}

ZTEST(hci_remove_iso_path_trace, test_core_lookup_failure_precedes_filter_mutation)
{
	struct hci_remove_iso_path_trace_result result;

	reset_fake();
	fake.lookup_ids[0] = -1;
	int ret = hci_remove_iso_path_trace_arm(&ops, 4U, &result);

	zassert_equal(ret, -ENOENT, "core lookup failure");
	zassert_equal(fake.lookup_count, 2U, "both lookups run");
	zassert_equal(fake.filter_count, 0U, "filter untouched");
	zassert_equal(result.core_source_id, -1, "core source ID");
	zassert_equal(result.driver_source_id, 9, "driver source ID");
	zassert_equal(result.core_level, 0U, "core level");
	zassert_equal(result.driver_level, 0U, "driver level");
}

ZTEST(hci_remove_iso_path_trace, test_driver_lookup_failure_precedes_filter_mutation)
{
	struct hci_remove_iso_path_trace_result result;

	reset_fake();
	fake.lookup_ids[1] = -1;
	int ret = hci_remove_iso_path_trace_arm(&ops, 4U, &result);

	zassert_equal(ret, -ENOENT, "driver lookup failure");
	zassert_equal(fake.lookup_count, 2U, "both lookups run");
	zassert_equal(fake.filter_count, 0U, "filter untouched");
	zassert_equal(result.core_source_id, 7, "core source ID");
	zassert_equal(result.driver_source_id, -1, "driver source ID");
	zassert_equal(result.core_level, 0U, "core level");
	zassert_equal(result.driver_level, 0U, "driver level");
}

ZTEST(hci_remove_iso_path_trace, test_core_clamped_return_is_erange_and_observed)
{
	struct hci_remove_iso_path_trace_result result;

	reset_fake();
	fake.filter_returns[0] = 3U;
	int ret = hci_remove_iso_path_trace_arm(&ops, 4U, &result);

	zassert_equal(ret, -ERANGE, "core clamp");
	zassert_equal(fake.filter_count, 2U, "both filters run");
	zassert_equal(result.core_level, 3U, "core observed level");
	zassert_equal(result.driver_level, 4U, "driver observed level");
}

ZTEST(hci_remove_iso_path_trace, test_driver_clamped_return_is_erange_and_observed)
{
	struct hci_remove_iso_path_trace_result result;

	reset_fake();
	fake.filter_returns[1] = 3U;
	int ret = hci_remove_iso_path_trace_arm(&ops, 4U, &result);

	zassert_equal(ret, -ERANGE, "driver clamp");
	zassert_equal(fake.filter_count, 2U, "both filters run");
	zassert_equal(result.core_level, 4U, "core observed level");
	zassert_equal(result.driver_level, 3U, "driver observed level");
}
