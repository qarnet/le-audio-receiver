/*
 * Copyright (c) 2021-2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 *
 * LE Audio Receiver: main() lifecycle — watchdog, init ordering,
 * advertising restart loop. BT and decode logic live in bt_bap.c /
 * audio_decode.c. I2S output lives in audio_i2s.c behind audio_sink.h.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/autoconf.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#include "audio_sink.h"
#include "audio_volume.h"
#include "audio_offload.h"
#include "bt_bap.h"

#if defined(CONFIG_SOC_NRF54L15)
#include "flpr_handshake.h"
#endif

#if defined(CONFIG_WATCHDOG)
#include <zephyr/drivers/watchdog.h>
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if defined(CONFIG_WATCHDOG)
#define WDT_NODE DT_NODELABEL(wdt0)
#if DT_NODE_HAS_STATUS(WDT_NODE, okay)
static const struct device *const wdt_dev = DEVICE_DT_GET(WDT_NODE);
static int wdt_chan;

static void wdt_feed_thread_fn(void *a, void *b, void *c)
{
	while (true) {
		wdt_feed(wdt_dev, wdt_chan);
		k_sleep(K_SECONDS(2));
	}
}
K_THREAD_DEFINE(wdt_tid, 512, wdt_feed_thread_fn, NULL, NULL, NULL, K_PRIO_PREEMPT(10), 0, 0);

static int wdt_init(void)
{
	if (!device_is_ready(wdt_dev)) {
		LOG_ERR("WDT not ready");
		return -ENODEV;
	}
	struct wdt_timeout_cfg cfg = {
		.window.min = 0,
		.window.max = 5000,
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
	};
	wdt_chan = wdt_install_timeout(wdt_dev, &cfg);
	if (wdt_chan < 0) {
		LOG_ERR("WDT install failed: %d", wdt_chan);
		return wdt_chan;
	}
	int err = wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);

	if (err) {
		LOG_ERR("WDT setup failed: %d", err);
		return err;
	}
	LOG_INF("Watchdog started (5 s timeout)");
	return 0;
}
#else
static inline int wdt_init(void)
{
	return 0;
}
#endif /* DT_NODE_HAS_STATUS */
#else
static inline int wdt_init(void)
{
	return 0;
}
#endif /* CONFIG_WATCHDOG */

/* ── main ─────────────────────────────────────────────────────────── */

int main(void)
{
	int err;

	if (wdt_init()) {
		LOG_ERR("Watchdog init failed");
		sys_reboot(SYS_REBOOT_COLD);
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed: %d", err);
		sys_reboot(SYS_REBOOT_COLD);
	}
	LOG_INF("BLE ready");

	err = settings_load();
	if (err) {
		LOG_ERR("settings_load() failed: %d", err);
		sys_reboot(SYS_REBOOT_COLD);
	}
	LOG_INF("settings_load() OK");

	/* CAS (Common Audio Service) is NOT registered — CONFIG_BT_CAP_ACCEPTOR=n
	 * avoids the CAP context check on ASE Enable. Available contexts are
	 * managed through bt_pacs_set_available_contexts() directly. */

	err = audio_volume_init();
	if (err) {
		LOG_ERR("VCP init failed: %d", err);
		sys_reboot(SYS_REBOOT_COLD);
	}

	err = bt_bap_init();
	if (err) {
		LOG_ERR("BAP init failed: %d", err);
		sys_reboot(SYS_REBOOT_COLD);
	}

	err = audio_sink_init();
	if (err) {
		LOG_ERR("I2S init failed: %d", err);
		sys_reboot(SYS_REBOOT_COLD);
	}

#if defined(CONFIG_SOC_NRF54L15)
	/* FLPR handshake: non-blocking, non-fatal if FLPR absent.
	 * VPR launcher has already released FLPR from reset at this point
	 * (NORDIC_VPR_LAUNCHER init at POST_KERNEL level). */
	flpr_handshake_init();

	/* Phase 6 Stage 2: init audio offload (FLPR ring transport).
	 * Non-blocking — may defer ring init if FLPR not ready yet. */
	audio_offload_init();
#endif

	err = bt_bap_restart_advertising();
	if (err) {
		LOG_ERR("Adv start failed: %d", err);
		sys_reboot(SYS_REBOOT_COLD);
	}

	LOG_INF("Advertising as \"%s\"", CONFIG_BT_DEVICE_NAME);

	while (true) {
		/* Heartbeat runs via k_work_delayable (flpr_handshake_init).
		 * No poll call needed in main loop. */
		bt_bap_wait_disconnect();
		LOG_INF("Restarting advertising...");

		err = bt_bap_restart_advertising();
		if (err) {
			LOG_ERR("Adv restart failed: %d", err);
			sys_reboot(SYS_REBOOT_COLD);
		}
		LOG_INF("Advertising again");
	}

	/* Unreachable — loop exits only on reboot */
	return 0;
}
