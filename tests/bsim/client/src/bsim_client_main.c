/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM valid-LC3 client — sink-only.  Discovers one remote sink ASE,
 * configures one TX stream with tx_param, sends LC3 frames, polls counter.
 *
 * Reuses upstream stream_tx.c / stream_lc3.c for real LC3 encoding.
 * bt_bap_stream_send wrapper (preset_override.h) atomically counts
 * successful sends via bsim_client_tx_count.
 */

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/types.h>

#include "bsim_client_test_helpers.h"

/* ── Extern (defined here, used by preset_override.h wrapper) ──── */
atomic_int bsim_client_tx_count;

/* ── Forward declarations for upstream stream TX ─────────────────── */
extern void stream_tx_init(void);
extern int stream_tx_register(struct bt_bap_stream *bap_stream);
extern int stream_tx_unregister(struct bt_bap_stream *bap_stream);

#define PASS_SEND_COUNT 100
#define TEST_TIMEOUT_US (40 * 1000000)

/* ── Semaphores for BAP lifecycle ──────────────────────────────── */
static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_mtu_exchanged, 0, 1);
static K_SEM_DEFINE(sem_security_updated, 0, 1);
static K_SEM_DEFINE(sem_sinks_discovered, 0, 1);
static K_SEM_DEFINE(sem_stream_configured, 0, 1);
static K_SEM_DEFINE(sem_stream_qos, 0, 1); /* one sink stream */
static K_SEM_DEFINE(sem_stream_enabled, 0, 1);
static K_SEM_DEFINE(sem_stream_started, 0, 1);
static K_SEM_DEFINE(sem_stream_connected, 0, 1);

static struct bt_conn *default_conn;
static struct bt_bap_unicast_group *unicast_group;

/* Remote ASE endpoints discovered from server — sink-only */
static struct bt_bap_ep *g_sinks[1];

#define TEST_STREAM_CNT 1
static struct bt_bap_stream test_streams[TEST_STREAM_CNT];
static size_t configured_sink_stream_count;

/* Preset: forced-include maps 16_2_1 → 48_4_1 */
static struct bt_bap_lc3_preset preset_16_2_1 = BT_BAP_LC3_UNICAST_PRESET_16_2_1(
	BT_AUDIO_LOCATION_MONO_AUDIO, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);

/* ── Callbacks ──────────────────────────────────────────────────── */

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err == 0 && conn == default_conn) {
		k_sem_give(&sem_connected);
	}
}

static void security_changed_cb(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	if (err == 0) {
		k_sem_give(&sem_security_updated);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.security_changed = security_changed_cb,
};

static void att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	k_sem_give(&sem_mtu_exchanged);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = att_mtu_updated,
};

static void stream_configured(struct bt_bap_stream *stream, const struct bt_bap_qos_cfg_pref *pref)
{
	k_sem_give(&sem_stream_configured);
}

static void stream_qos_set(struct bt_bap_stream *stream)
{
	k_sem_give(&sem_stream_qos);
}

static void stream_enabled(struct bt_bap_stream *stream)
{
	k_sem_give(&sem_stream_enabled);
}

static void stream_started(struct bt_bap_stream *stream)
{
	/* Register for TX when the source (TX direction) stream starts */
	if (IS_ENABLED(CONFIG_BT_AUDIO_TX)) {
		stream_tx_register(stream);
	}
	k_sem_give(&sem_stream_started);
}

static void stream_connected_cb(struct bt_bap_stream *stream)
{
	k_sem_give(&sem_stream_connected);
}

static struct bt_bap_stream_ops stream_ops = {
	.configured = stream_configured,
	.qos_set = stream_qos_set,
	.enabled = stream_enabled,
	.started = stream_started,
	.connected = stream_connected_cb,
};

/* ── ASE discovery callbacks ─────────────────────────────────────── */

static struct bt_bap_unicast_client_cb unicast_client_cbs;

static void add_remote_sink(struct bt_bap_ep *ep)
{
	for (size_t i = 0U; i < ARRAY_SIZE(g_sinks); i++) {
		if (g_sinks[i] == NULL) {
			g_sinks[i] = ep;
			return;
		}
	}
}

static void endpoint_cb(struct bt_conn *conn, enum bt_audio_dir dir, struct bt_bap_ep *ep)
{
	if (dir == BT_AUDIO_DIR_SINK) {
		add_remote_sink(ep);
	}
	/* source direction: ignored (sink-only client) */
}

static struct bt_bap_unicast_client_cb unicast_client_cbs = {
	.endpoint = endpoint_cb,
};

static void discover_sinks_cb(struct bt_conn *conn, int err, enum bt_audio_dir dir)
{
	k_sem_give(&sem_sinks_discovered);
}

/* ── Scanning ───────────────────────────────────────────────────── */

static bool check_audio_and_connect(struct bt_data *data, void *user_data)
{
	bt_addr_le_t *addr = user_data;

	if (default_conn != NULL) {
		return false;
	}

	bt_le_scan_stop();

	int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_BAP_CONN_PARAM_RELAXED,
				    &default_conn);
	if (err != 0) {
		printk("Failed to connect: %d\n", err);
	}
	return false;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	if (default_conn != NULL) {
		return;
	}
	bt_data_parse(ad, check_audio_and_connect, (void *)addr);
}

static void start_scan(void)
{
	int err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);

	if (err != 0) {
		printk("Scanning failed to start (err %d)\n", err);
	}
}

/* ── BT Setup ────────────────────────────────────────────────────── */

static int bt_init(void)
{
	int err;

	err = bt_enable(NULL);
	if (err != 0) {
		printk("Bluetooth enable failed (err %d)\n", err);
		return err;
	}
	printk("Bluetooth initialized\n");

	bt_gatt_cb_register(&gatt_callbacks);

	if (IS_ENABLED(CONFIG_BT_AUDIO_TX)) {
		stream_tx_init();
	}

	return 0;
}

static int scan_and_connect(void)
{
	start_scan();

	int err = k_sem_take(&sem_connected, K_SECONDS(10));

	if (err != 0) {
		FAIL("valid_lc3_client: connect timeout\n");
		return err;
	}

	k_sem_take(&sem_mtu_exchanged, K_SECONDS(5));

	err = bt_conn_set_security(default_conn, BT_SECURITY_L2);
	if (err != 0) {
		return err;
	}

	err = k_sem_take(&sem_security_updated, K_SECONDS(10));
	if (err != 0) {
		FAIL("valid_lc3_client: security timeout\n");
		return err;
	}

	return 0;
}

static int discover_sinks(void)
{
	unicast_client_cbs.discover = discover_sinks_cb;

	int err = bt_bap_unicast_client_discover(default_conn, BT_AUDIO_DIR_SINK);

	if (err != 0) {
		return err;
	}

	return k_sem_take(&sem_sinks_discovered, K_SECONDS(10));
}

static int configure_stream(struct bt_bap_stream *stream, struct bt_bap_ep *ep)
{
	int err;

	err = bt_bap_stream_config(default_conn, stream, ep, &preset_16_2_1.codec_cfg);
	if (err != 0) {
		return err;
	}

	return k_sem_take(&sem_stream_configured, K_SECONDS(10));
}

static int configure_streams(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(g_sinks); i++) {
		if (g_sinks[i] == NULL) {
			continue;
		}
		int err = configure_stream(&test_streams[i], g_sinks[i]);

		if (err != 0) {
			printk("Could not configure sink stream[%zu]: %d\n", i, err);
			return err;
		}
		configured_sink_stream_count++;
	}
	return 0;
}

static size_t create_unicast_group(struct bt_bap_unicast_group **out_group)
{
	struct bt_bap_unicast_group_stream_param stream_params[TEST_STREAM_CNT];
	struct bt_bap_unicast_group_stream_pair_param pair_params[1];
	struct bt_bap_unicast_group_param param;
	size_t stream_cnt = 0;

	memset(stream_params, 0, sizeof(stream_params));
	memset(pair_params, 0, sizeof(pair_params));

	/* One server sink → one client TX stream */
	for (size_t i = 0; i < ARRAY_SIZE(g_sinks); i++) {
		if (g_sinks[i] == NULL) {
			break;
		}
		stream_params[stream_cnt].stream = &test_streams[stream_cnt];
		stream_params[stream_cnt].qos = &preset_16_2_1.qos;
		pair_params[i].tx_param = &stream_params[stream_cnt];
		stream_cnt++;
	}

	if (stream_cnt == 0) {
		FAIL("valid_lc3_client: No streams in group\n");
		return 0;
	}

	param.params = pair_params;
	param.params_count = 1;
	param.packing = BT_ISO_PACKING_SEQUENTIAL;

	int err = bt_bap_unicast_group_create(&param, out_group);

	if (err != 0) {
		FAIL("valid_lc3_client: Unable to create unicast group: %d\n", err);
		return 0;
	}

	return stream_cnt;
}

static int set_stream_qos(void)
{
	int err = bt_bap_stream_qos(default_conn, unicast_group);

	if (err != 0) {
		printk("Unable to setup QoS: %d\n", err);
		return err;
	}

	/* Wait for QoS callback for each configured stream */
	for (size_t i = 0; i < configured_sink_stream_count; i++) {
		err = k_sem_take(&sem_stream_qos, K_SECONDS(10));
		if (err != 0) {
			return err;
		}
	}

	return 0;
}

static int enable_streams(void)
{
	size_t total = configured_sink_stream_count;

	for (size_t i = 0; i < total; i++) {
		int err = bt_bap_stream_enable(&test_streams[i], preset_16_2_1.codec_cfg.meta,
					       preset_16_2_1.codec_cfg.meta_len);
		if (err != 0) {
			printk("Unable to enable stream[%zu]: %d\n", i, err);
			return err;
		}
		err = k_sem_take(&sem_stream_enabled, K_SECONDS(10));
		if (err != 0) {
			return err;
		}
	}
	return 0;
}

static int connect_streams(void)
{
	size_t total = configured_sink_stream_count;

	for (size_t i = 0; i < total; i++) {
		int err = bt_bap_stream_connect(&test_streams[i]);

		if (err == -EALREADY) {
			continue;
		}
		if (err != 0) {
			return err;
		}
		err = k_sem_take(&sem_stream_connected, K_SECONDS(10));
		if (err != 0) {
			return err;
		}
	}
	return 0;
}

static int start_streams(void)
{
	size_t total = configured_sink_stream_count;

	for (size_t i = 0; i < total; i++) {
		struct bt_bap_stream *stream = &test_streams[i];

		int err = bt_bap_stream_start(stream);

		/* -EINVAL means the server has already started this stream
		 * (sink direction).  -EALREADY means already started. */
		if (err == -EALREADY || err == -EINVAL) {
			printk("Stream[%zu] already started or server-managed\n", i);
			continue;
		}
		if (err != 0) {
			printk("Unable to start stream[%zu]: %d\n", i, err);
			return err;
		}
		err = k_sem_take(&sem_stream_started, K_SECONDS(10));
		if (err != 0) {
			return err;
		}
	}
	return 0;
}

/* ── Test framework ─────────────────────────────────────────────── */

static void test_init_f(void)
{
	bst_ticker_set_next_tick_absolute(TEST_TIMEOUT_US);
	bst_result = In_progress;
}

static void test_tick_f(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		FAIL("valid_lc3_client: timeout after 40 s — "
		     "only %d sends (need %d)\n",
		     atomic_load(&bsim_client_tx_count), PASS_SEND_COUNT);
	}
}

static void test_main_f(void)
{
	/* ── BT init ── */
	int err = bt_init();

	if (err != 0) {
		FAIL("valid_lc3_client: BT init failed: %d\n", err);
		return;
	}

	err = bt_bap_unicast_client_register_cb(&unicast_client_cbs);
	if (err != 0) {
		FAIL("valid_lc3_client: register cb failed: %d\n", err);
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(test_streams); i++) {
		test_streams[i].ops = &stream_ops;
	}

	/* ── BAP lifecycle ── */
	printk("Scanning and connecting...\n");
	err = scan_and_connect();
	if (err) {
		return;
	}

	printk("Discovering sinks...\n");
	err = discover_sinks();
	if (err) {
		return;
	}

	printk("Configuring streams...\n");
	err = configure_streams();
	if (err) {
		return;
	}

	printk("Creating unicast group...\n");
	size_t stream_cnt = create_unicast_group(&unicast_group);

	if (stream_cnt == 0) {
		return;
	}

	printk("Setting stream QoS...\n");
	err = set_stream_qos();
	if (err) {
		return;
	}

	printk("Enabling streams...\n");
	err = enable_streams();
	if (err) {
		return;
	}

	printk("Connecting streams...\n");
	err = connect_streams();
	if (err) {
		return;
	}

	printk("Starting streams...\n");
	err = start_streams();
	if (err) {
		return;
	}

	printk("Streams started — waiting for %d sends\n", PASS_SEND_COUNT);

	/* ── Poll send counter ── */
	while (bst_result == In_progress) {
		int count = atomic_load(&bsim_client_tx_count);

		if (count >= PASS_SEND_COUNT) {
			PASS("valid_lc3_client: %d successful sends (>= %d)\n", count,
			     PASS_SEND_COUNT);
			return;
		}
		k_sleep(K_MSEC(100));
	}
}

static const struct bst_test_instance test_def[] = {
	{
		.test_id = "valid_lc3_client",
		.test_descr = "BSIM valid-LC3 client — 48_4_1 preset, 100 sends",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_f,
		.test_tick_f = test_tick_f,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_valid_lc3_client_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_def);
}

bst_test_install_t test_installers[] = {
	test_valid_lc3_client_install,
	NULL,
};
