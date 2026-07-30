/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM receiver test — BT init and test lifecycle.
 *
 * test_main_f initialises Bluetooth, registers BAP/PACS, and starts
 * advertising.  The BAP callbacks (in bt_bap.c) handle incoming streams
 * and push decoded PCM to the audio_sink_stub.  The stub counts pushes
 * and calls PASS after 100 valid frames.
 */

#include <errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>
#include <zephyr/kernel.h>

#include <bstests.h>
#include "bsim_test_helpers.h"
#include "audio_sink.h"
#include "bt_bap.h"

#define TEST_TIMEOUT_US (30 * 1000000) /* 30 seconds */

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

static void test_main_f(void)
{
	int err;

	printk("=== LE Audio Receiver BSIM Test ===\n");

	err = bt_enable(NULL);
	if (err) {
		FAIL("le_audio_receiver: Bluetooth init failed: %d\n", err);
		return;
	}
	printk("BLE ready\n");

	/* settings_load required for dynamic PACS/ASCS registration.
	 * No persistent storage in bsim — the settings_none backend
	 * returns success without loading anything. */
	err = settings_load();
	if (err) {
		FAIL("le_audio_receiver: settings_load failed: %d\n", err);
		return;
	}
	printk("settings_load OK\n");

	err = bt_bap_init();
	if (err) {
		FAIL("le_audio_receiver: BAP init failed: %d\n", err);
		return;
	}

	err = audio_sink_init();
	if (err) {
		FAIL("le_audio_receiver: audio sink init failed: %d\n", err);
		return;
	}

	err = bt_bap_restart_advertising();
	if (err) {
		FAIL("le_audio_receiver: advertising start failed: %d\n", err);
		return;
	}

	printk("Advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);

	/* Wait for the audio sink stub to reach PASS_FRAME_COUNT.
	 * The BAP callbacks handle reception and decoding in the
	 * background (called from BT RX thread). */
	while (bst_result == In_progress) {
		k_sleep(K_MSEC(500));
	}

	/* If we reach here without PASS, the tick timeout will FAIL */
}

static const struct bst_test_instance test_def[] = {
	{
		.test_id = "le_audio_receiver",
		.test_descr = "LE Audio Receiver — 100 decoded frames",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_f,
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
