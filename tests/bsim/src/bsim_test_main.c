/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include <bstests.h>

#define TEST_TIMEOUT_US (30 * 1e6)  /* 30 seconds */

static void test_init_f(void)
{
	bst_ticker_set_next_tick_absolute(TEST_TIMEOUT_US);
	bst_result = In_progress;
}

static void test_tick_f(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		FAIL("le_audio_receiver: timeout after 30 s — no PASS\n");
	}
}

static const struct bst_test_instance test_def[] = {
	{
		.test_id = "le_audio_receiver",
		.test_descr = "LE Audio Receiver — 100 decoded frames",
		.test_pre_init_f = test_init_f,
		.test_tick_f = test_tick_f,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_le_audio_receiver_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_def);
}

bst_test_install_t test_installers[] = {
	test_le_audio_receiver_install,
	NULL,
};

void bst_main(void)
{
	bst_test_install(test_installers);
}
