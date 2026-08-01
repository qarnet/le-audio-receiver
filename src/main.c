/*
 * Copyright (c) 2021-2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 *
 * LE Audio Receiver: main() lifecycle — watchdog device/thread/config,
 * boot-coordinator wiring, advertising restart loop. The ordered fatal
 * boot sequence itself lives in the narrow, unit-tested coordinator
 * app_lifecycle.c; this file retains all hardware wiring and adapts the
 * real subsystem calls to the coordinator's operations structure. BT and
 * decode logic live in bt_bap.c / audio_decode.c. I2S output lives in
 * audio_i2s.c behind audio_sink.h.
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

#include "app_lifecycle.h"
#include "audio_sink.h"
#include "audio_volume.h"
#include "audio_offload.h"
#include "bt_bap.h"

#if defined(CONFIG_SOC_NRF54L15)
#include "flpr_handshake.h"
#include "flpr_runtime.h"
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

/* ── lifecycle operations (adapted to app_lifecycle_ops) ───────── */

static int bluetooth_init(void)
{
	int err = bt_enable(NULL);

	if (err == 0) {
		LOG_INF("BLE ready");
	}
	return err;
}

static int settings_init(void)
{
	int err = settings_load();

	if (err == 0) {
		LOG_INF("settings_load() OK");
	}
	return err;
}

static int volume_init(void)
{
	return audio_volume_init();
}

static int bap_init(void)
{
	return bt_bap_init();
}

static int sink_init(void)
{
	return audio_sink_init();
}

static int advertising_start(void)
{
	return bt_bap_restart_advertising();
}

static void cold_reboot(void)
{
	sys_reboot(SYS_REBOOT_COLD);
}

#if defined(CONFIG_SOC_NRF54L15)
static void platform_init(void)
{
	/* FLPR handshake: non-blocking, non-fatal if FLPR absent.
	 * VPR launcher has already released FLPR from reset at this point
	 * (NORDIC_VPR_LAUNCHER init at POST_KERNEL level). */
	flpr_handshake_init();

	/* Phase 6 Stage 2: init audio offload (FLPR ring transport).
	 * Non-blocking — may defer ring init if FLPR not ready yet. */
	audio_offload_init();

	/* Phase 6 Stage 4A: init FLPR runtime restart manager.
	 * Derives DT addresses, non-blocking. */
	flpr_runtime_init();
}
#endif /* CONFIG_SOC_NRF54L15 */

/* ── main ─────────────────────────────────────────────────────────── */

int main(void)
{
	static const struct app_lifecycle_ops ops = {
		.watchdog_init = wdt_init,
		.bluetooth_init = bluetooth_init,
		.settings_init = settings_init,
		.volume_init = volume_init,
		.bap_init = bap_init,
		.sink_init = sink_init,
#if defined(CONFIG_SOC_NRF54L15)
		.platform_init = platform_init,
#endif
		.advertising_start = advertising_start,
		.cold_reboot = cold_reboot,
	};
	int err;

	/* Fatal init order is owned by the coordinator: watchdog, Bluetooth,
	 * settings (after BT, before BAP/PACS registration), volume, BAP, I2S,
	 * optional nonfatal platform init, initial advertising. */
	err = app_lifecycle_boot(&ops);
	if (err != 0) {
		/* app_lifecycle_boot() already requested the cold reboot; reaching
		 * this point means the reboot callback returned (production
		 * sys_reboot() never returns). Never continue into normal
		 * operation after a failed boot. */
		sys_reboot(SYS_REBOOT_COLD);
		while (true) {
			k_sleep(K_FOREVER);
		}
	}

	LOG_INF("Advertising as \"%s\"", CONFIG_BT_DEVICE_NAME);

	while (true) {
		/* Heartbeat runs via k_work_delayable (flpr_handshake_init).
		 * No poll call needed in main loop. */
		bt_bap_wait_disconnect();
		LOG_INF("Restarting advertising...");

		err = app_lifecycle_restart_advertising(&ops);
		if (err != 0) {
			/* Coordinator already requested the cold reboot; same
			 * no-continue guard as boot. */
			sys_reboot(SYS_REBOOT_COLD);
			while (true) {
				k_sleep(K_FOREVER);
			}
		}
		LOG_INF("Advertising again");
	}

	/* Unreachable — loop exits only on reboot */
	return 0;
}
