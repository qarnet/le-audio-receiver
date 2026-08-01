/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared test support state that must be visible across translation
 * units (header-only statics would fragment per TU).  Only the
 * test-held slab block tracking lives here; everything else is in the
 * helpers header.
 */

#include <zephyr/ztest.h>

#include "audio_i2s_test_helpers.h"

void *test_held[TEST_SLAB_BLOCKS];
int test_held_count;

void test_prealloc_blocks(int n)
{
	for (int i = 0; i < n; i++) {
		void *b;

		zassert_equal(k_mem_slab_alloc(audio_i2s_test_get_slab(), &b, K_NO_WAIT), 0,
			      "prealloc %d", i);
		zassert_true(test_held_count < TEST_SLAB_BLOCKS, "held overflow");
		test_held[test_held_count++] = b;
	}
}

void test_release_held(void)
{
	for (int i = 0; i < test_held_count; i++) {
		k_mem_slab_free(audio_i2s_test_get_slab(), test_held[i]);
	}
	test_held_count = 0;
}
