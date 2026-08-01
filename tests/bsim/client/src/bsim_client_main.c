/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM parameterized BAP client — drives the 15 T4 scenarios against the
 * repo receiver over real BAP/ASCS/ISO transport with real liblc3.
 *
 * One binary, one BST test ID per scenario; the runner selects the
 * scenario with -testid.  TX uses the repo-owned deterministic
 * multi-channel transmitter (bsim_tx.c).
 */

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/audio/bap_lc3_preset.h>
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
#include "bsim_tx.h"

#define TEST_TIMEOUT_US (60 * 1000000) /* 60 seconds */
#define MAX_SINKS       3
#define MAX_SRCS        1
#define MAX_STREAMS     3

/* Wait-for-sends pacing: generous margins keep teardown/reconnect timing
 * deterministic in BSim while never outrunning the receiver. */
#define SEND_POLL_MS       50
#define SEND_WAIT_MS       15000
#define TEARDOWN_MARGIN_MS 500

/* ── scenario ids (mirror receiver + runner) ─────────────────────── */

enum bsim_scenario {
	SCN_MONO_10MS = 1,
	SCN_MONO_7P5MS,
	SCN_MODEA_10MS,
	SCN_MODEA_7P5MS,
	SCN_MODEA_REVERSE_START_10MS,
	SCN_MODEB_10MS,
	SCN_MODEB_7P5MS,
	SCN_INVALID_SDU_RESUME_10MS,
	SCN_MODEA_FIRST_STOP_10MS,
	SCN_RELEASE_WITHOUT_DISABLE_10MS,
	SCN_DISCONNECT_STREAMING_10MS,
	SCN_RECONNECT_SECOND_STREAM_10MS,
	SCN_UNSUPPORTED_SOURCE_DIRECTION,
	SCN_NO_FREE_SINK_SLOT,
	SCN_INVALID_CODEC_FIELDS,
};

/* ── globals ─────────────────────────────────────────────────────── */

static struct bt_conn *default_conn;
static struct bt_bap_unicast_group *unicast_group;
static struct bt_bap_ep *sink_eps[MAX_SINKS];
static struct bt_bap_ep *src_eps[MAX_SRCS];
static struct bt_bap_stream streams[MAX_STREAMS];

/* ASCS response capture (exact codes/reasons from the listener). */
static struct bt_bap_ascs_rsp cfg_rsps[24];
static size_t cfg_rsp_cnt;
static struct bt_bap_ascs_rsp rel_rsps[8];
static size_t rel_rsp_cnt;
static struct bt_bap_ascs_rsp dis_rsps[4];
static size_t dis_rsp_cnt;
static K_SEM_DEFINE(sem_cfg_rsp, 0, ARRAY_SIZE(cfg_rsps));
static K_SEM_DEFINE(sem_rel_rsp, 0, ARRAY_SIZE(rel_rsps));
static K_SEM_DEFINE(sem_dis_rsp, 0, ARRAY_SIZE(dis_rsps));

static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_disconnected, 0, 1);
static K_SEM_DEFINE(sem_mtu_exchanged, 0, 1);
static K_SEM_DEFINE(sem_security_updated, 0, 1);
static K_SEM_DEFINE(sem_sinks_discovered, 0, 1);
static K_SEM_DEFINE(sem_src_discovered, 0, 1);
static K_SEM_DEFINE(sem_stream_configured, 0, 4);
static K_SEM_DEFINE(sem_stream_qos, 0, 4);
static K_SEM_DEFINE(sem_stream_enabled, 0, 4);
static K_SEM_DEFINE(sem_stream_started, 0, 4);
static K_SEM_DEFINE(sem_stream_connected, 0, 4);

/* ── presets ─────────────────────────────────────────────────────── */

static struct bt_bap_lc3_preset preset_48_4_1_mono = BT_BAP_LC3_UNICAST_PRESET_48_4_1(
	BT_AUDIO_LOCATION_MONO_AUDIO, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
static struct bt_bap_lc3_preset preset_48_3_1_mono = BT_BAP_LC3_UNICAST_PRESET_48_3_1(
	BT_AUDIO_LOCATION_MONO_AUDIO, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
static struct bt_bap_lc3_preset preset_48_4_1_fl = BT_BAP_LC3_UNICAST_PRESET_48_4_1(
	BT_AUDIO_LOCATION_FRONT_LEFT, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
static struct bt_bap_lc3_preset preset_48_4_1_fr = BT_BAP_LC3_UNICAST_PRESET_48_4_1(
	BT_AUDIO_LOCATION_FRONT_RIGHT, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
static struct bt_bap_lc3_preset preset_48_3_1_fl = BT_BAP_LC3_UNICAST_PRESET_48_3_1(
	BT_AUDIO_LOCATION_FRONT_LEFT, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
static struct bt_bap_lc3_preset preset_48_3_1_fr = BT_BAP_LC3_UNICAST_PRESET_48_3_1(
	BT_AUDIO_LOCATION_FRONT_RIGHT, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);

/* Mode B: one ASE, [L frame][R frame] per SDU.  Per-channel octets equal
 * the mono preset rate; SDU = 2 × octets. */
static struct bt_bap_lc3_preset preset_modeb_10ms = {
	.codec_cfg = BT_AUDIO_CODEC_LC3_CONFIG(
		BT_AUDIO_CODEC_CFG_FREQ_48KHZ, BT_AUDIO_CODEC_CFG_DURATION_10,
		(BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT), 120u, 1,
		BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED),
	.qos = BT_BAP_QOS_CFG_UNFRAMED(10000u, 240u, 5u, 20u, 40000u),
};
static struct bt_bap_lc3_preset preset_modeb_7p5ms = {
	.codec_cfg = BT_AUDIO_CODEC_LC3_CONFIG(
		BT_AUDIO_CODEC_CFG_FREQ_48KHZ, BT_AUDIO_CODEC_CFG_DURATION_7_5,
		(BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT), 90u, 1,
		BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED),
	.qos = BT_BAP_QOS_CFG_UNFRAMED(7500u, 180u, 5u, 15u, 40000u),
};

/* ── connection callbacks ────────────────────────────────────────── */

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err == 0) {
		/* bt_conn_le_create already handed the app its reference in
		 * default_conn; do NOT ref again or the conn object can
		 * never be freed and reconnects find a stale connection. */
		default_conn = conn;
		k_sem_give(&sem_connected);
	}
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	if (conn == default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
		k_sem_give(&sem_disconnected);
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
	.disconnected = disconnected_cb,
	.security_changed = security_changed_cb,
};

static void att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	k_sem_give(&sem_mtu_exchanged);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = att_mtu_updated,
};

/* ── ASCS response listener (exact rsp codes/reasons) ────────────── */

static struct bt_bap_unicast_client_cb unicast_client_cbs;

static void listener_config(struct bt_bap_stream *stream, enum bt_bap_ascs_rsp_code rsp_code,
			    enum bt_bap_ascs_reason reason)
{
	printk("CLI config rsp code=0x%02x reason=0x%02x\n", rsp_code, reason);
	if (cfg_rsp_cnt < ARRAY_SIZE(cfg_rsps)) {
		cfg_rsps[cfg_rsp_cnt++] =
			(struct bt_bap_ascs_rsp){.code = rsp_code, .reason = reason};
		k_sem_give(&sem_cfg_rsp);
	}
}

static void listener_release(struct bt_bap_stream *stream, enum bt_bap_ascs_rsp_code rsp_code,
			     enum bt_bap_ascs_reason reason)
{
	printk("CLI release rsp code=0x%02x reason=0x%02x\n", rsp_code, reason);
	if (rel_rsp_cnt < ARRAY_SIZE(rel_rsps)) {
		rel_rsps[rel_rsp_cnt++] =
			(struct bt_bap_ascs_rsp){.code = rsp_code, .reason = reason};
		k_sem_give(&sem_rel_rsp);
	}
}

static void listener_disable(struct bt_bap_stream *stream, enum bt_bap_ascs_rsp_code rsp_code,
			     enum bt_bap_ascs_reason reason)
{
	printk("CLI disable rsp code=0x%02x reason=0x%02x\n", rsp_code, reason);
	if (dis_rsp_cnt < ARRAY_SIZE(dis_rsps)) {
		dis_rsps[dis_rsp_cnt++] =
			(struct bt_bap_ascs_rsp){.code = rsp_code, .reason = reason};
		k_sem_give(&sem_dis_rsp);
	}
}

static void endpoint_cb(struct bt_conn *conn, enum bt_audio_dir dir, struct bt_bap_ep *ep)
{
	if (ep == NULL) {
		return;
	}
	if (dir == BT_AUDIO_DIR_SINK) {
		for (size_t i = 0U; i < ARRAY_SIZE(sink_eps); i++) {
			if (sink_eps[i] == NULL) {
				sink_eps[i] = ep;
				printk("CLI discovered sink ASE %zu\n", i);
				return;
			}
		}
	} else {
		for (size_t i = 0U; i < ARRAY_SIZE(src_eps); i++) {
			if (src_eps[i] == NULL) {
				src_eps[i] = ep;
				printk("CLI discovered source ASE %zu\n", i);
				return;
			}
		}
	}
}

static void discover_sinks_cb(struct bt_conn *conn, int err, enum bt_audio_dir dir)
{
	k_sem_give(&sem_sinks_discovered);
}

static void discover_src_cb(struct bt_conn *conn, int err, enum bt_audio_dir dir)
{
	k_sem_give(&sem_src_discovered);
}

/* ── stream ops ──────────────────────────────────────────────────── */

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

/* ── scanning / connecting ───────────────────────────────────────── */

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
		printk("CLI failed to connect: %d\n", err);
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

static int scan_and_connect(void)
{
	int err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);

	if (err != 0) {
		printk("CLI scanning failed to start: %d\n", err);
		return err;
	}

	err = k_sem_take(&sem_connected, K_SECONDS(20));
	if (err != 0) {
		FAIL("client: connect timeout\n");
		return err;
	}

	k_sem_take(&sem_mtu_exchanged, K_SECONDS(10));

	err = bt_conn_set_security(default_conn, BT_SECURITY_L2);
	if (err != 0) {
		return err;
	}

	err = k_sem_take(&sem_security_updated, K_SECONDS(20));
	if (err != 0) {
		FAIL("client: security timeout\n");
		return err;
	}

	return 0;
}

static int discover_sinks(void)
{
	/* The discovery callback only reports NEW endpoints; stale entries
	 * from previous connections must be cleared or later rounds
	 * configure freed endpoint objects. */
	memset(sink_eps, 0, sizeof(sink_eps));
	unicast_client_cbs.discover = discover_sinks_cb;

	int err = bt_bap_unicast_client_discover(default_conn, BT_AUDIO_DIR_SINK);

	if (err != 0) {
		return err;
	}
	return k_sem_take(&sem_sinks_discovered, K_SECONDS(10));
}

static int discover_sources(void)
{
	memset(src_eps, 0, sizeof(src_eps));
	unicast_client_cbs.discover = discover_src_cb;

	int err = bt_bap_unicast_client_discover(default_conn, BT_AUDIO_DIR_SOURCE);

	if (err != 0) {
		return err;
	}
	return k_sem_take(&sem_src_discovered, K_SECONDS(10));
}

/* ── config helpers ──────────────────────────────────────────────── */

/*
 * Configure one stream and wait for the exact expected ASCS response.
 * On success the stream ops configured callback also fires; on rejection
 * only the listener response is delivered.
 */
static int config_expect(struct bt_bap_stream *stream, struct bt_bap_ep *ep,
			 const struct bt_audio_codec_cfg *cfg, enum bt_bap_ascs_rsp_code exp_code,
			 enum bt_bap_ascs_reason exp_reason)
{
	size_t rsp_before = cfg_rsp_cnt;
	int err = bt_bap_stream_config(default_conn, stream, ep, (struct bt_audio_codec_cfg *)cfg);

	if (err != 0) {
		printk("CLI bt_bap_stream_config failed: %d\n", err);
		return err;
	}

	err = k_sem_take(&sem_cfg_rsp, K_SECONDS(10));
	if (err != 0) {
		FAIL("client: config response timeout\n");
		return err;
	}

	if (rsp_before + 1U > cfg_rsp_cnt) {
		return -ENODATA;
	}
	struct bt_bap_ascs_rsp got = cfg_rsps[cfg_rsp_cnt - 1U];

	if (got.code != exp_code || got.reason != exp_reason) {
		FAIL("client: config rsp 0x%02x/0x%02x != expected 0x%02x/0x%02x\n", got.code,
		     got.reason, exp_code, exp_reason);
		return -EBADMSG;
	}

	if (exp_code == BT_BAP_ASCS_RSP_CODE_SUCCESS) {
		/* Wait for the stream ops configured callback as well. */
		err = k_sem_take(&sem_stream_configured, K_SECONDS(10));
		if (err != 0) {
			FAIL("client: configured callback timeout\n");
			return err;
		}
	}

	return 0;
}

/*
 * Detach a stream from its endpoint.  For an ASE left idle by a rejected
 * Config the client library resets the stream locally (no PDU is sent);
 * for a configured ASE this sends a real Release op.  Both paths make the
 * stream/ep reusable for another Config attempt.
 */
static int detach_stream(struct bt_bap_stream *stream)
{
	return bt_bap_stream_release(stream);
}

/* ── group + lifecycle helpers ───────────────────────────────────── */

static int create_group(struct bt_bap_lc3_preset **presets, size_t stream_cnt, size_t base)
{
	struct bt_bap_unicast_group_stream_pair_param pair_params[2];
	struct bt_bap_unicast_group_stream_param stream_params[2];
	struct bt_bap_unicast_group_param param;

	memset(pair_params, 0, sizeof(pair_params));
	memset(stream_params, 0, sizeof(stream_params));

	for (size_t i = 0U; i < stream_cnt; i++) {
		stream_params[i].stream = &streams[base + i];
		stream_params[i].qos = &presets[i]->qos;
		pair_params[i].tx_param = &stream_params[i];
	}

	param.params = pair_params;
	param.params_count = stream_cnt;
	param.packing = BT_ISO_PACKING_SEQUENTIAL;

	int err = bt_bap_unicast_group_create(&param, &unicast_group);

	if (err != 0) {
		FAIL("client: unable to create unicast group: %d\n", err);
		return err;
	}
	return 0;
}

static int set_stream_qos(size_t stream_cnt)
{
	int err = bt_bap_stream_qos(default_conn, unicast_group);

	if (err != 0) {
		return err;
	}
	for (size_t i = 0U; i < stream_cnt; i++) {
		err = k_sem_take(&sem_stream_qos, K_SECONDS(10));
		if (err != 0) {
			return err;
		}
	}
	return 0;
}

/* Number of QoS-configured streams on this group (for base != 0 flows). */

static int enable_streams(size_t stream_cnt, struct bt_bap_lc3_preset **presets, size_t base)
{
	for (size_t i = 0U; i < stream_cnt; i++) {
		int err = bt_bap_stream_enable(&streams[base + i], presets[i]->codec_cfg.meta,
					       presets[i]->codec_cfg.meta_len);

		if (err != 0) {
			return err;
		}
		err = k_sem_take(&sem_stream_enabled, K_SECONDS(10));
		if (err != 0) {
			return err;
		}
	}
	return 0;
}

static int connect_streams(size_t stream_cnt, size_t base)
{
	for (size_t i = 0U; i < stream_cnt; i++) {
		int err = bt_bap_stream_connect(&streams[base + i]);

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

/*
 * Start streams in the given order.  Reverse start (scenario 5) begins
 * with stream 1 so both Mode A transmitted sequence counters start at
 * zero when TX finally releases (TX holds until both are streaming).
 */
static int start_streams(size_t stream_cnt, bool reverse, size_t base)
{
	for (size_t step = 0U; step < stream_cnt; step++) {
		size_t i = (reverse ? (stream_cnt - 1U - step) : step) + base;
		int err = bt_bap_stream_start(&streams[i]);

		/* -EINVAL: server already started (sink direction);
		 * -EALREADY: already started.
		 * -EBADMSG: the server auto-streams sink ASEs as soon as the
		 * CIS connects (receiver_ready path), so the local ep state is
		 * already STREAMING before the client Start op is sent. */
		if (err == -EALREADY || err == -EINVAL || err == -EBADMSG) {
			printk("CLI stream %zu already streaming (start %d)\n", i, err);
			continue;
		}
		if (err != 0) {
			return err;
		}
		err = k_sem_take(&sem_stream_started, K_SECONDS(10));
		if (err != 0) {
			return err;
		}
	}
	return 0;
}

/* ── TX helpers ──────────────────────────────────────────────────── */

struct tx_param {
	uint16_t octets_per_frame;
	uint32_t freq_hz;
	uint32_t frame_duration_us;
	uint8_t chan_count;
	uint8_t channel_idx;
};

static int tx_register(size_t stream_idx, const struct tx_param *p)
{
	struct bsim_tx_config cfg = {
		.octets_per_frame = p->octets_per_frame,
		.freq_hz = p->freq_hz,
		.frame_duration_us = p->frame_duration_us,
		.chan_count = p->chan_count,
		.channel_idx = p->channel_idx,
	};

	return bsim_tx_register(&streams[stream_idx], &cfg);
}

static int wait_for_sends(size_t stream_idx, uint32_t target)
{
	uint32_t waited = 0U;

	while (bsim_tx_send_count(&streams[stream_idx]) < target) {
		if (waited >= SEND_WAIT_MS) {
			FAIL("client: stream %zu send timeout (%u < %u)\n", stream_idx,
			     bsim_tx_send_count(&streams[stream_idx]), target);
			return -ETIMEDOUT;
		}
		k_sleep(K_MSEC(SEND_POLL_MS));
		waited += SEND_POLL_MS;
	}
	return 0;
}

/* ── scenario plumbing ───────────────────────────────────────────── */

static int bt_init(void)
{
	int err;

	err = bt_enable(NULL);
	if (err != 0) {
		printk("CLI Bluetooth enable failed: %d\n", err);
		return err;
	}
	printk("CLI Bluetooth initialized\n");

	bt_gatt_cb_register(&gatt_callbacks);

	err = bsim_tx_init();
	if (err != 0) {
		printk("CLI TX init failed: %d\n", err);
		return err;
	}

	return 0;
}

static int init_streams(size_t stream_cnt)
{
	for (size_t i = 0U; i < stream_cnt; i++) {
		streams[i].ops = &stream_ops;
	}
	return 0;
}

/* Full streaming flow: configure + group + qos + enable + connect + start. */
static int stream_up(struct bt_bap_lc3_preset **presets, size_t stream_cnt, bool reverse)
{
	int err;

	printk("CLI configuring %zu stream(s)...\n", stream_cnt);
	for (size_t i = 0U; i < stream_cnt; i++) {
		err = config_expect(&streams[i], sink_eps[i], &presets[i]->codec_cfg,
				    BT_BAP_ASCS_RSP_CODE_SUCCESS, BT_BAP_ASCS_REASON_NONE);
		if (err != 0) {
			return err;
		}
	}

	err = create_group(presets, stream_cnt, 0);
	if (err != 0) {
		return err;
	}

	err = set_stream_qos(stream_cnt);
	if (err != 0) {
		return err;
	}

	err = enable_streams(stream_cnt, presets, 0);
	if (err != 0) {
		return err;
	}

	err = connect_streams(stream_cnt, 0);
	if (err != 0) {
		return err;
	}

	err = start_streams(stream_cnt, reverse, 0);
	if (err != 0) {
		return err;
	}

	return 0;
}

static void client_pass(const char *scenario)
{
	PASS("bsim_client: scenario=%s sends0=%u sends1=%u cfgrsps=%zu relrsps=%zu disrsps=%zu\n",
	     scenario, bsim_tx_send_count(&streams[0]), bsim_tx_send_count(&streams[1]),
	     cfg_rsp_cnt, rel_rsp_cnt, dis_rsp_cnt);
}

/* ── scenario implementations ────────────────────────────────────── */

/*
 * Scenarios 1–7: full streaming.  @p presets and @p tx control the shape.
 * mono: 1 stream/1 ch; Mode A: 2 streams/1 ch each (FL, FR); Mode B:
 * 1 stream/2 ch.  Pass after @p sends_per_stream successful sends on
 * every stream (Mode A: 100 on both).
 */
static int scenario_normal(const char *scenario, struct bt_bap_lc3_preset **presets,
			   size_t stream_cnt, const struct tx_param *tx, int required_streams,
			   bool reverse, uint32_t sends_per_stream)
{
	int err = scan_and_connect();

	if (err != 0) {
		return err;
	}
	printk("CLI discovering sinks...\n");
	err = discover_sinks();
	if (err != 0) {
		return err;
	}

	bsim_tx_set_required_streams(required_streams);
	for (size_t i = 0U; i < stream_cnt; i++) {
		err = tx_register(i, &tx[i]);
		if (err != 0) {
			return err;
		}
		/* Exact send caps: the TX self-pauses at the limit so the
		 * receiver sees exactly sends_per_stream SDUs. */
		bsim_tx_set_send_limit(&streams[i], sends_per_stream);
	}

	err = stream_up(presets, stream_cnt, reverse);
	if (err != 0) {
		return err;
	}

	for (size_t i = 0U; i < stream_cnt; i++) {
		err = wait_for_sends(i, sends_per_stream);
		if (err != 0) {
			return err;
		}
	}
	/* Let the in-flight SDUs drain to the receiver. */
	k_sleep(K_MSEC(TEARDOWN_MARGIN_MS));

	client_pass(scenario);
	return 0;
}

/* Scenario 8: one malformed one-byte SDU at fixed sequence, then resume. */
static int scenario_invalid_sdu_resume(void)
{
	struct tx_param tx = {.octets_per_frame = 120,
			      .freq_hz = 48000,
			      .frame_duration_us = 10000,
			      .chan_count = 1,
			      .channel_idx = 0};
	struct bt_bap_lc3_preset *presets[] = {&preset_48_4_1_mono};
	int err = scan_and_connect();

	if (err != 0) {
		return err;
	}
	err = discover_sinks();
	if (err != 0) {
		return err;
	}

	bsim_tx_set_required_streams(1);
	err = tx_register(0, &tx);
	if (err != 0) {
		return err;
	}
	bsim_tx_set_send_limit(&streams[0], 101);
	/* Inject exactly one 1-byte SDU when the next send would use seq 20. */
	bsim_tx_schedule_malformed(&streams[0], 20);

	err = stream_up(presets, 1, false);
	if (err != 0) {
		return err;
	}

	/* 20 valid frames, malformed at seq 20, then 80 valid frames. */
	err = wait_for_sends(0, 101);
	if (err != 0) {
		return err;
	}
	k_sleep(K_MSEC(TEARDOWN_MARGIN_MS));

	client_pass("invalid_sdu_resume_10ms");
	return 0;
}

/* Scenario 9: first Mode A ASE stops while the second keeps sending. */
static int scenario_modea_first_stop(void)
{
	struct tx_param tx[2] = {
		{.octets_per_frame = 120,
		 .freq_hz = 48000,
		 .frame_duration_us = 10000,
		 .chan_count = 1,
		 .channel_idx = 0},
		{.octets_per_frame = 120,
		 .freq_hz = 48000,
		 .frame_duration_us = 10000,
		 .chan_count = 1,
		 .channel_idx = 1},
	};
	struct bt_bap_lc3_preset *presets[] = {&preset_48_4_1_fl, &preset_48_4_1_fr};
	int err = scan_and_connect();

	if (err != 0) {
		return err;
	}
	err = discover_sinks();
	if (err != 0) {
		return err;
	}

	bsim_tx_set_required_streams(2);
	for (size_t i = 0U; i < 2U; i++) {
		err = tx_register(i, &tx[i]);
		if (err != 0) {
			return err;
		}
	}
	/* Stream 0 caps at 25 sends; stream 1 keeps sending 20 more SDUs
	 * after stream 0 is disabled (45 total). */
	bsim_tx_set_send_limit(&streams[0], 25);
	bsim_tx_set_send_limit(&streams[1], 45);

	err = stream_up(presets, 2, false);
	if (err != 0) {
		return err;
	}

	/* At least 20 paired pushes on both halves. */
	err = wait_for_sends(0, 25);
	if (err != 0) {
		return err;
	}
	err = wait_for_sends(1, 25);
	if (err != 0) {
		return err;
	}
	/* Short margin: the gate must close while stream 1 is still sending
	 * (its remaining SDUs become the closed-gate receive evidence). */
	k_sleep(K_MSEC(100));

	/* Disable first ASE (server closes gate). */
	err = bt_bap_stream_disable(&streams[0]);
	if (err != 0) {
		return err;
	}
	err = k_sem_take(&sem_dis_rsp, K_SECONDS(10));
	if (err != 0) {
		return err;
	}
	if (dis_rsps[dis_rsp_cnt - 1U].code != BT_BAP_ASCS_RSP_CODE_SUCCESS) {
		FAIL("client: disable rsp 0x%02x\n", dis_rsps[dis_rsp_cnt - 1U].code);
		return -EBADMSG;
	}
	printk("CLI stream 0 disabled\n");

	/* Second stream keeps sending at least 20 more SDUs (45 total). */
	err = wait_for_sends(1, 45);
	if (err != 0) {
		return err;
	}
	k_sleep(K_MSEC(TEARDOWN_MARGIN_MS));

	/* Release first ASE, then stop and release the second. */
	err = bt_bap_stream_release(&streams[0]);
	if (err != 0) {
		return err;
	}
	err = k_sem_take(&sem_rel_rsp, K_SECONDS(10));
	if (err != 0) {
		return err;
	}
	if (rel_rsps[rel_rsp_cnt - 1U].code != BT_BAP_ASCS_RSP_CODE_SUCCESS) {
		FAIL("client: release rsp 0x%02x\n", rel_rsps[rel_rsp_cnt - 1U].code);
		return -EBADMSG;
	}

	err = bt_bap_stream_disable(&streams[1]);
	if (err != 0) {
		return err;
	}
	err = k_sem_take(&sem_dis_rsp, K_SECONDS(10));
	if (err != 0) {
		return err;
	}

	err = bt_bap_stream_release(&streams[1]);
	if (err != 0) {
		return err;
	}
	err = k_sem_take(&sem_rel_rsp, K_SECONDS(10));
	if (err != 0) {
		return err;
	}

	/* Clean disconnect. */
	bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	k_sem_take(&sem_disconnected, K_SECONDS(10));

	client_pass("modea_first_stop_10ms");
	return 0;
}

/* Scenario 10: Release directly from streaming (no prior Disable). */
static int scenario_release_without_disable(void)
{
	struct tx_param tx = {.octets_per_frame = 120,
			      .freq_hz = 48000,
			      .frame_duration_us = 10000,
			      .chan_count = 1,
			      .channel_idx = 0};
	struct bt_bap_lc3_preset *presets[] = {&preset_48_4_1_mono};
	int err = scan_and_connect();

	if (err != 0) {
		return err;
	}
	err = discover_sinks();
	if (err != 0) {
		return err;
	}

	bsim_tx_set_required_streams(1);
	err = tx_register(0, &tx);
	if (err != 0) {
		return err;
	}
	bsim_tx_set_send_limit(&streams[0], 25);

	err = stream_up(presets, 1, false);
	if (err != 0) {
		return err;
	}

	err = wait_for_sends(0, 25);
	if (err != 0) {
		return err;
	}
	k_sleep(K_MSEC(300));

	/* Release directly from streaming. */
	err = bt_bap_stream_release(&streams[0]);
	if (err != 0) {
		return err;
	}
	err = k_sem_take(&sem_rel_rsp, K_SECONDS(10));
	if (err != 0) {
		return err;
	}
	if (rel_rsps[rel_rsp_cnt - 1U].code != BT_BAP_ASCS_RSP_CODE_SUCCESS) {
		FAIL("client: release rsp 0x%02x\n", rel_rsps[rel_rsp_cnt - 1U].code);
		return -EBADMSG;
	}

	bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	k_sem_take(&sem_disconnected, K_SECONDS(10));

	client_pass("release_without_disable_10ms");
	return 0;
}

/* Scenario 11: disconnect while streaming. */
static int scenario_disconnect_streaming(void)
{
	struct tx_param tx = {.octets_per_frame = 120,
			      .freq_hz = 48000,
			      .frame_duration_us = 10000,
			      .chan_count = 1,
			      .channel_idx = 0};
	struct bt_bap_lc3_preset *presets[] = {&preset_48_4_1_mono};
	int err = scan_and_connect();

	if (err != 0) {
		return err;
	}
	err = discover_sinks();
	if (err != 0) {
		return err;
	}

	bsim_tx_set_required_streams(1);
	err = tx_register(0, &tx);
	if (err != 0) {
		return err;
	}
	bsim_tx_set_send_limit(&streams[0], 25);

	err = stream_up(presets, 1, false);
	if (err != 0) {
		return err;
	}

	err = wait_for_sends(0, 25);
	if (err != 0) {
		return err;
	}
	k_sleep(K_MSEC(300));

	bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	err = k_sem_take(&sem_disconnected, K_SECONDS(10));
	if (err != 0) {
		return err;
	}

	client_pass("disconnect_streaming_10ms");
	return 0;
}

/* Scenario 12: disconnect, reconnect, second mono 10 ms stream. */
static int scenario_reconnect_second_stream(void)
{
	struct tx_param tx = {.octets_per_frame = 120,
			      .freq_hz = 48000,
			      .frame_duration_us = 10000,
			      .chan_count = 1,
			      .channel_idx = 0};
	struct bt_bap_lc3_preset *presets[] = {&preset_48_4_1_mono};
	int err;

	/* ── session 1: stream at least 20 pushes ── */
	err = scan_and_connect();
	if (err != 0) {
		return err;
	}
	err = discover_sinks();
	if (err != 0) {
		return err;
	}

	bsim_tx_set_required_streams(1);
	err = tx_register(0, &tx);
	if (err != 0) {
		return err;
	}
	bsim_tx_set_send_limit(&streams[0], 25);

	err = stream_up(presets, 1, false);
	if (err != 0) {
		return err;
	}

	err = wait_for_sends(0, 25);
	if (err != 0) {
		return err;
	}
	k_sleep(K_MSEC(TEARDOWN_MARGIN_MS));

	bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	err = k_sem_take(&sem_disconnected, K_SECONDS(10));
	if (err != 0) {
		return err;
	}
	bsim_tx_unregister(&streams[0]);

	/* Let the client library release every endpoint/stream of the old
	 * connection and let the ISO channels tear down (the conn object
	 * lingers until its last reference drops). */
	k_sleep(K_MSEC(2000));

	/* Free session 1's unicast group (single-group pool). */
	if (unicast_group != NULL) {
		bt_bap_unicast_group_delete(unicast_group);
		unicast_group = NULL;
	}

	/* ── session 2: reconnect + rediscover + fresh stream ── */
	err = scan_and_connect();
	if (err != 0) {
		return err;
	}
	err = discover_sinks();
	if (err != 0) {
		return err;
	}

	/* Fresh stream object: no reuse of session-1 state. */
	streams[1].ops = &stream_ops;
	err = tx_register(1, &tx);
	if (err != 0) {
		return err;
	}
	bsim_tx_set_send_limit(&streams[1], 100);

	err = config_expect(&streams[1], sink_eps[0], &presets[0]->codec_cfg,
			    BT_BAP_ASCS_RSP_CODE_SUCCESS, BT_BAP_ASCS_REASON_NONE);
	if (err != 0) {
		return err;
	}

	err = create_group(presets, 1, 1);
	if (err != 0) {
		return err;
	}
	err = set_stream_qos(1);
	if (err != 0) {
		return err;
	}
	err = enable_streams(1, presets, 1);
	if (err != 0) {
		return err;
	}
	err = connect_streams(1, 1);
	if (err != 0) {
		return err;
	}
	err = start_streams(1, false, 1);
	if (err != 0) {
		return err;
	}

	err = wait_for_sends(1, 100);
	if (err != 0) {
		return err;
	}

	client_pass("reconnect_second_stream_10ms");
	return 0;
}

/* Scenario 13: source-direction Config must get CONF_UNSUPPORTED / NONE. */
static int scenario_unsupported_source(void)
{
	int err = scan_and_connect();

	if (err != 0) {
		return err;
	}
	err = discover_sources();
	if (err != 0) {
		return err;
	}
	if (src_eps[0] == NULL) {
		FAIL("client: no source ASE discovered\n");
		return -ENODATA;
	}

	streams[2].ops = &stream_ops;
	err = config_expect(&streams[2], src_eps[0], &preset_48_4_1_mono.codec_cfg,
			    BT_BAP_ASCS_RSP_CODE_CONF_UNSUPPORTED, BT_BAP_ASCS_REASON_NONE);
	if (err != 0) {
		return err;
	}

	bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	k_sem_take(&sem_disconnected, K_SECONDS(10));

	client_pass("unsupported_source_direction");
	return 0;
}

/* Scenario 14: two valid configs, third NO_MEM, clean releases, reuse. */
static int scenario_no_free_sink_slot(void)
{
	int err = scan_and_connect();

	if (err != 0) {
		return err;
	}
	err = discover_sinks();
	if (err != 0) {
		return err;
	}
	if (sink_eps[0] == NULL || sink_eps[1] == NULL || sink_eps[2] == NULL) {
		FAIL("client: need 3 sink ASEs, got %d\n",
		     (sink_eps[0] != NULL) + (sink_eps[1] != NULL) + (sink_eps[2] != NULL));
		return -ENODATA;
	}

	/* First two valid sink Configs succeed. */
	for (size_t i = 0U; i < 2U; i++) {
		streams[i].ops = &stream_ops;
		err = config_expect(&streams[i], sink_eps[i], &preset_48_4_1_mono.codec_cfg,
				    BT_BAP_ASCS_RSP_CODE_SUCCESS, BT_BAP_ASCS_REASON_NONE);
		if (err != 0) {
			return err;
		}
	}

	/* Third valid sink Config must reach the repo callback and get NO_MEM. */
	streams[2].ops = &stream_ops;
	err = config_expect(&streams[2], sink_eps[2], &preset_48_4_1_mono.codec_cfg,
			    BT_BAP_ASCS_RSP_CODE_NO_MEM, BT_BAP_ASCS_REASON_NONE);
	if (err != 0) {
		return err;
	}

	/* Release first two cleanly. */
	for (size_t i = 0U; i < 2U; i++) {
		err = bt_bap_stream_release(&streams[i]);
		if (err != 0) {
			return err;
		}
		err = k_sem_take(&sem_rel_rsp, K_SECONDS(10));
		if (err != 0) {
			return err;
		}
		if (rel_rsps[rel_rsp_cnt - 1U].code != BT_BAP_ASCS_RSP_CODE_SUCCESS) {
			FAIL("client: release rsp 0x%02x\n", rel_rsps[rel_rsp_cnt - 1U].code);
			return -EBADMSG;
		}
	}

	/* Third failure must not have mutated pool/lifecycle counters:
	 * a slot is reusable after the releases.  Let the server's idle
	 * transitions (stream detach) settle first. */
	k_sleep(K_MSEC(200));
	err = config_expect(&streams[0], sink_eps[0], &preset_48_4_1_mono.codec_cfg,
			    BT_BAP_ASCS_RSP_CODE_SUCCESS, BT_BAP_ASCS_REASON_NONE);
	if (err != 0) {
		return err;
	}
	err = bt_bap_stream_release(&streams[0]);
	if (err != 0) {
		return err;
	}
	err = k_sem_take(&sem_rel_rsp, K_SECONDS(10));
	if (err != 0) {
		return err;
	}

	bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	k_sem_take(&sem_disconnected, K_SECONDS(10));

	client_pass("no_free_sink_slot");
	return 0;
}

/* ── Scenario 15: invalid codec field variants ───────────────────── */

#define SCN15_DATA_MAX 16

struct scn15_variant {
	const char *name;
	uint8_t data[SCN15_DATA_MAX];
	uint8_t data_len;
};

static uint8_t scn15_base[SCN15_DATA_MAX];
static uint8_t scn15_base_len;

static void scn15_build_base(void)
{
	/* freq=48k(0x08), dur=10ms(0x01), loc=mono(0x01 00 00 00), len=120 (78 00),
	 * no frame-blocks LTV (omitted when 1). */
	uint8_t base[16] = {
		0x02, 0x01, 0x08,                   /* FREQ = 48 kHz */
		0x02, 0x02, 0x01,                   /* DURATION = 10 ms */
		0x06, 0x03, 0x01, 0x00, 0x00, 0x00, /* CHAN_ALLOC = MONO */
		0x04, 0x04, 0x78, 0x00,             /* FRAME_LEN = 120 */
	};

	scn15_base_len = sizeof(base);
	memcpy(scn15_base, base, sizeof(base));
}

static void scn15_variants(struct scn15_variant *v)
{
	size_t n = 0U;

	/* 1. missing frequency */
	v[n].name = "missing_freq";
	v[n].data_len = 13;
	memcpy(v[n].data, scn15_base, 13);
	v[n].data[0] = 0x00; /* strip FREQ LTV: shift left by 3 */
	memmove(&v[n].data[0], &v[n].data[3], 10);
	v[n].data_len = 10;
	n++;

	/* 2. unsupported frequency (16 kHz) */
	v[n].name = "freq_16khz";
	v[n].data_len = scn15_base_len;
	memcpy(v[n].data, scn15_base, scn15_base_len);
	v[n].data[2] = 0x03; /* BT_AUDIO_CODEC_CFG_FREQ_16KHZ */
	n++;

	/* 3. missing duration */
	v[n].name = "missing_dur";
	v[n].data_len = scn15_base_len;
	memcpy(v[n].data, scn15_base, scn15_base_len);
	memmove(&v[n].data[3], &v[n].data[6], scn15_base_len - 6);
	v[n].data_len = scn15_base_len - 3;
	n++;

	/* 4. invalid duration encoding */
	v[n].name = "bad_dur";
	v[n].data_len = scn15_base_len;
	memcpy(v[n].data, scn15_base, scn15_base_len);
	v[n].data[5] = 0xFF;
	n++;

	/* 5. missing octets per frame */
	v[n].name = "missing_octets";
	v[n].data_len = scn15_base_len;
	memcpy(v[n].data, scn15_base, scn15_base_len);
	memmove(&v[n].data[9], &v[n].data[13], scn15_base_len - 13);
	v[n].data_len = scn15_base_len - 4;
	n++;

	/* 6. octets 19 */
	v[n].name = "octets_19";
	v[n].data_len = scn15_base_len;
	memcpy(v[n].data, scn15_base, scn15_base_len);
	v[n].data[12] = 19;
	v[n].data[11] = 0x00;
	n++;

	/* 7. octets 121 */
	v[n].name = "octets_121";
	v[n].data_len = scn15_base_len;
	memcpy(v[n].data, scn15_base, scn15_base_len);
	v[n].data[12] = 121;
	v[n].data[11] = 0x00;
	n++;

	/* 8. explicit frame blocks 2 */
	v[n].name = "fblocks_2";
	v[n].data_len = scn15_base_len + 3;
	memcpy(v[n].data, scn15_base, scn15_base_len);
	v[n].data[scn15_base_len + 0] = 0x02;
	v[n].data[scn15_base_len + 1] = 0x05; /* FRAME_BLKS_PER_SDU */
	v[n].data[scn15_base_len + 2] = 0x02;
	n++;

	/* 9. channel allocation with 3 channels */
	v[n].name = "chan_3";
	v[n].data_len = scn15_base_len;
	memcpy(v[n].data, scn15_base, scn15_base_len);
	v[n].data[8] = 0x07; /* FL | FR | FC */
	n++;
}

static int scenario_invalid_codec_fields(void)
{
	struct scn15_variant v[16];
	struct bt_bap_ascs_rsp exp_invalid =
		BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID, BT_BAP_ASCS_REASON_CODEC_DATA);
	struct bt_bap_ascs_rsp exp_ok =
		BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_SUCCESS, BT_BAP_ASCS_REASON_NONE);
	int err;

	scn15_build_base();
	scn15_variants(v);

	/*
	 * A rejected Config leaves the client endpoint attached to its
	 * stream and there is no public detach API for an idle ASE, so
	 * each sink endpoint supports exactly one attempt per connection.
	 * The receiver registers three sink ASEs: three rounds of three
	 * attempts cover the nine invalid variants, each round on a fresh
	 * connection (the disconnect releases every client endpoint).
	 */
	for (int round = 0; round < 3; round++) {
		err = scan_and_connect();
		if (err != 0) {
			return err;
		}
		err = discover_sinks();
		if (err != 0) {
			return err;
		}

		for (int k = 0; k < 3; k++) {
			size_t i = (size_t)(round * 3 + k);
			struct bt_audio_codec_cfg cfg;

			memset(&cfg, 0, sizeof(cfg));
			cfg.id = BT_HCI_CODING_FORMAT_LC3;
			cfg.cid = 0x0000;
			cfg.vid = 0x0000;
			cfg.target_latency = BT_AUDIO_CODEC_CFG_TARGET_LATENCY_BALANCED;
			cfg.target_phy = BT_AUDIO_CODEC_CFG_TARGET_PHY_2M;
			cfg.data_len = v[i].data_len;
			memcpy(cfg.data, v[i].data, v[i].data_len);

			printk("CLI config attempt %zu: %s\n", i, v[i].name);
			streams[k].ops = &stream_ops;
			err = config_expect(&streams[k], sink_eps[k], &cfg, exp_invalid.code,
					    exp_invalid.reason);
			if (err != 0) {
				FAIL("client: attempt %zu (%s) rsp mismatch\n", i, v[i].name);
				return err;
			}
		}

		bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		err = k_sem_take(&sem_disconnected, K_SECONDS(10));
		if (err != 0) {
			return err;
		}
		/* Let the client library release the round's endpoints/streams
		 * (rejected-config streams still hold conn references). */
		k_sleep(K_MSEC(2000));
	}

	/* One valid mono shape must succeed on a fresh connection,
	 * proving the nine failures consumed no slot. */
	err = scan_and_connect();
	if (err != 0) {
		return err;
	}
	err = discover_sinks();
	if (err != 0) {
		return err;
	}
	streams[0].ops = &stream_ops;
	err = config_expect(&streams[0], sink_eps[0], &preset_48_4_1_mono.codec_cfg, exp_ok.code,
			    exp_ok.reason);
	if (err != 0) {
		return err;
	}
	err = bt_bap_stream_release(&streams[0]);
	if (err != 0) {
		return err;
	}
	err = k_sem_take(&sem_rel_rsp, K_SECONDS(10));
	if (err != 0) {
		return err;
	}

	/* Missing optional frame blocks falls back to one and succeeds. */
	struct bt_audio_codec_cfg cfg_nofb;

	memset(&cfg_nofb, 0, sizeof(cfg_nofb));
	cfg_nofb.id = BT_HCI_CODING_FORMAT_LC3;
	cfg_nofb.cid = 0x0000;
	cfg_nofb.vid = 0x0000;
	cfg_nofb.target_latency = BT_AUDIO_CODEC_CFG_TARGET_LATENCY_BALANCED;
	cfg_nofb.target_phy = BT_AUDIO_CODEC_CFG_TARGET_PHY_2M;
	cfg_nofb.data_len = scn15_base_len;
	memcpy(cfg_nofb.data, scn15_base, scn15_base_len);

	streams[1].ops = &stream_ops;
	err = config_expect(&streams[1], sink_eps[1], &cfg_nofb, exp_ok.code, exp_ok.reason);
	if (err != 0) {
		return err;
	}
	err = bt_bap_stream_release(&streams[1]);
	if (err != 0) {
		return err;
	}
	err = k_sem_take(&sem_rel_rsp, K_SECONDS(10));
	if (err != 0) {
		return err;
	}

	bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	k_sem_take(&sem_disconnected, K_SECONDS(10));

	client_pass("invalid_codec_fields");
	return 0;
}

/* ── test framework ──────────────────────────────────────────────── */

static void test_init_f(void)
{
	bst_ticker_set_next_tick_absolute(TEST_TIMEOUT_US);
	bst_result = In_progress;
}

static void test_tick_f(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		FAIL("bsim_client: timeout after 60 s\n");
	}
}

static int client_setup(void)
{
	int err = bt_init();

	if (err != 0) {
		FAIL("bsim_client: BT init failed: %d\n", err);
		return err;
	}

	unicast_client_cbs.config = listener_config;
	unicast_client_cbs.release = listener_release;
	unicast_client_cbs.disable = listener_disable;
	unicast_client_cbs.endpoint = endpoint_cb;

	err = bt_bap_unicast_client_register_cb(&unicast_client_cbs);
	if (err != 0) {
		FAIL("bsim_client: register cb failed: %d\n", err);
		return err;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(streams); i++) {
		streams[i].ops = &stream_ops;
	}

	return 0;
}

#define SCENARIO_MAIN(_fn)                                                                         \
	static void test_main_##_fn(void)                                                          \
	{                                                                                          \
		int err = client_setup();                                                          \
		if (err == 0) {                                                                    \
			err = scenario_##_fn();                                                    \
		}                                                                                  \
		if (err != 0 && bst_result != Failed) {                                            \
			FAIL("bsim_client: scenario failed: %d\n", err);                           \
		}                                                                                  \
	}

SCENARIO_MAIN(invalid_sdu_resume)
SCENARIO_MAIN(modea_first_stop)
SCENARIO_MAIN(release_without_disable)
SCENARIO_MAIN(disconnect_streaming)
SCENARIO_MAIN(reconnect_second_stream)
SCENARIO_MAIN(unsupported_source)
SCENARIO_MAIN(no_free_sink_slot)
SCENARIO_MAIN(invalid_codec_fields)

/* Normal scenario wrappers: mono 10/7.5, Mode A 10/7.5 (+reverse), Mode B 10/7.5. */
static void test_main_normal_mono_10ms(void)
{
	struct tx_param tx = {.octets_per_frame = 120,
			      .freq_hz = 48000,
			      .frame_duration_us = 10000,
			      .chan_count = 1,
			      .channel_idx = 0};
	struct bt_bap_lc3_preset *presets[] = {&preset_48_4_1_mono};
	int err = client_setup();

	if (err == 0) {
		err = scenario_normal("mono_10ms", presets, 1, &tx, 1, false, 100);
	}
	if (err != 0 && bst_result != Failed) {
		FAIL("bsim_client: mono_10ms failed: %d\n", err);
	}
}

static void test_main_normal_mono_7p5ms(void)
{
	struct tx_param tx = {.octets_per_frame = 90,
			      .freq_hz = 48000,
			      .frame_duration_us = 7500,
			      .chan_count = 1,
			      .channel_idx = 0};
	struct bt_bap_lc3_preset *presets[] = {&preset_48_3_1_mono};
	int err = client_setup();

	if (err == 0) {
		err = scenario_normal("mono_7p5ms", presets, 1, &tx, 1, false, 100);
	}
	if (err != 0 && bst_result != Failed) {
		FAIL("bsim_client: mono_7p5ms failed: %d\n", err);
	}
}

static void test_main_normal_modea_10ms(void)
{
	struct tx_param tx[2] = {
		{.octets_per_frame = 120,
		 .freq_hz = 48000,
		 .frame_duration_us = 10000,
		 .chan_count = 1,
		 .channel_idx = 0},
		{.octets_per_frame = 120,
		 .freq_hz = 48000,
		 .frame_duration_us = 10000,
		 .chan_count = 1,
		 .channel_idx = 1},
	};
	struct bt_bap_lc3_preset *presets[] = {&preset_48_4_1_fl, &preset_48_4_1_fr};
	int err = client_setup();

	if (err == 0) {
		err = scenario_normal("modea_10ms", presets, 2, tx, 2, false, 100);
	}
	if (err != 0 && bst_result != Failed) {
		FAIL("bsim_client: modea_10ms failed: %d\n", err);
	}
}

static void test_main_normal_modea_7p5ms(void)
{
	struct tx_param tx[2] = {
		{.octets_per_frame = 90,
		 .freq_hz = 48000,
		 .frame_duration_us = 7500,
		 .chan_count = 1,
		 .channel_idx = 0},
		{.octets_per_frame = 90,
		 .freq_hz = 48000,
		 .frame_duration_us = 7500,
		 .chan_count = 1,
		 .channel_idx = 1},
	};
	struct bt_bap_lc3_preset *presets[] = {&preset_48_3_1_fl, &preset_48_3_1_fr};
	int err = client_setup();

	if (err == 0) {
		err = scenario_normal("modea_7p5ms", presets, 2, tx, 2, false, 100);
	}
	if (err != 0 && bst_result != Failed) {
		FAIL("bsim_client: modea_7p5ms failed: %d\n", err);
	}
}

static void test_main_normal_modea_reverse_start(void)
{
	struct tx_param tx[2] = {
		{.octets_per_frame = 120,
		 .freq_hz = 48000,
		 .frame_duration_us = 10000,
		 .chan_count = 1,
		 .channel_idx = 0},
		{.octets_per_frame = 120,
		 .freq_hz = 48000,
		 .frame_duration_us = 10000,
		 .chan_count = 1,
		 .channel_idx = 1},
	};
	struct bt_bap_lc3_preset *presets[] = {&preset_48_4_1_fl, &preset_48_4_1_fr};
	int err = client_setup();

	if (err == 0) {
		err = scenario_normal("modea_reverse_start_10ms", presets, 2, tx, 2, true, 100);
	}
	if (err != 0 && bst_result != Failed) {
		FAIL("bsim_client: modea_reverse_start failed: %d\n", err);
	}
}

static void test_main_normal_modeb_10ms(void)
{
	struct tx_param tx = {.octets_per_frame = 120,
			      .freq_hz = 48000,
			      .frame_duration_us = 10000,
			      .chan_count = 2,
			      .channel_idx = 0};
	struct bt_bap_lc3_preset *presets[] = {&preset_modeb_10ms};
	int err = client_setup();

	if (err == 0) {
		err = scenario_normal("modeb_10ms", presets, 1, &tx, 1, false, 100);
	}
	if (err != 0 && bst_result != Failed) {
		FAIL("bsim_client: modeb_10ms failed: %d\n", err);
	}
}

static void test_main_normal_modeb_7p5ms(void)
{
	struct tx_param tx = {.octets_per_frame = 90,
			      .freq_hz = 48000,
			      .frame_duration_us = 7500,
			      .chan_count = 2,
			      .channel_idx = 0};
	struct bt_bap_lc3_preset *presets[] = {&preset_modeb_7p5ms};
	int err = client_setup();

	if (err == 0) {
		err = scenario_normal("modeb_7p5ms", presets, 1, &tx, 1, false, 100);
	}
	if (err != 0 && bst_result != Failed) {
		FAIL("bsim_client: modeb_7p5ms failed: %d\n", err);
	}
}

static const struct bst_test_instance test_def[] = {
	{
		.test_id = "mono_10ms",
		.test_descr = "T4 mono 10 ms — 100 sends",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_normal_mono_10ms,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "mono_7p5ms",
		.test_descr = "T4 mono 7.5 ms — 100 sends",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_normal_mono_7p5ms,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modea_10ms",
		.test_descr = "T4 Mode A 10 ms — 100 sends per ASE",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_normal_modea_10ms,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modea_7p5ms",
		.test_descr = "T4 Mode A 7.5 ms — 100 sends per ASE",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_normal_modea_7p5ms,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modea_reverse_start_10ms",
		.test_descr = "T4 Mode A 10 ms reverse start",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_normal_modea_reverse_start,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modeb_10ms",
		.test_descr = "T4 Mode B 10 ms — 100 sends",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_normal_modeb_10ms,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modeb_7p5ms",
		.test_descr = "T4 Mode B 7.5 ms — 100 sends",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_normal_modeb_7p5ms,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "invalid_sdu_resume_10ms",
		.test_descr = "T4 malformed SDU then resume",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_invalid_sdu_resume,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modea_first_stop_10ms",
		.test_descr = "T4 first Mode A ASE stops",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_modea_first_stop,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "release_without_disable_10ms",
		.test_descr = "T4 release without disable",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_release_without_disable,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "disconnect_streaming_10ms",
		.test_descr = "T4 disconnect while streaming",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_disconnect_streaming,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "reconnect_second_stream_10ms",
		.test_descr = "T4 reconnect and second stream",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_reconnect_second_stream,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "unsupported_source_direction",
		.test_descr = "T4 source-direction rejection",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_unsupported_source,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "no_free_sink_slot",
		.test_descr = "T4 NO_MEM on third sink",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_no_free_sink_slot,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "invalid_codec_fields",
		.test_descr = "T4 invalid codec field variants",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_invalid_codec_fields,
		.test_tick_f = test_tick_f,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_bsim_client_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_def);
}

bst_test_install_t test_installers[] = {
	test_bsim_client_install,
	NULL,
};
