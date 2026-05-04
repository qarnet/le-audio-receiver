/*
 * Copyright (c) 2021-2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 *
 * LE Audio Receiver: nRF5340 DK + PCM5102A I2S DAC
 * BAP Unicast Server — sink-only, mono or stereo, LC3 decode → I2S.
 *
 * Supports both stereo modes phones may use:
 *   Mode A: 2 separate mono ASEs  (1 channel each, left + right) — common
 *   Mode B: 1 stereo ASE          (2 channels per SDU, interleaved in-codec)
 *   Mono:   single mono ASE, plays on both I2S channels.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/audio/lc3.h>
#include <zephyr/bluetooth/audio/pacs.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/byteorder.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/sys_clock.h>
#include <zephyr/types.h>

#include "audio_i2s.h"

#if defined(CONFIG_LIBLC3)
#include "lc3.h"
#endif

#define MAX_SINK_ASE          CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT
#define MAX_SINK_CHANNELS     2

#define AVAILABLE_SINK_CONTEXT  (BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED | \
				 BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | \
				 BT_AUDIO_CONTEXT_TYPE_MEDIA | \
				 BT_AUDIO_CONTEXT_TYPE_GAME | \
				 BT_AUDIO_CONTEXT_TYPE_INSTRUCTIONAL)

static const struct bt_audio_codec_cap lc3_codec_cap = BT_AUDIO_CODEC_CAP_LC3(
	BT_AUDIO_CODEC_CAP_FREQ_ANY, BT_AUDIO_CODEC_CAP_DURATION_10,
	BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(2), 40u, 120u, 1u,
	BT_AUDIO_CONTEXT_TYPE_MEDIA);

static struct bt_conn *default_conn;

struct audio_sink {
	struct bt_bap_stream stream;
	size_t recv_cnt;
	int chan_count;		/* per-ASE channel count from codec cfg */
#if defined(CONFIG_LIBLC3)
	lc3_decoder_t decoder;
	void *decoder_mem;	/* points to lc3_decoder_mem_48k_t */
#endif
};

static struct audio_sink sinks[MAX_SINK_ASE];
static size_t num_sink_ase;

static const struct bt_bap_qos_cfg_pref qos_pref =
	BT_BAP_QOS_CFG_PREF(true, BT_GAP_LE_PHY_2M, 0x02, 10, 40000, 40000, 40000, 40000);

static K_SEM_DEFINE(sem_disconnected, 0, 1);

static uint8_t unicast_server_addata[] = {
	BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL),
	BT_AUDIO_UNICAST_ANNOUNCEMENT_TARGETED,
	BT_BYTES_LIST_LE16(AVAILABLE_SINK_CONTEXT),
	BT_BYTES_LIST_LE16(0),		/* no source */
	0x00,
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL)),
	BT_DATA(BT_DATA_SVC_DATA16, unicast_server_addata, ARRAY_SIZE(unicast_server_addata)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* ── LC3 decoder static storage ─────────────────────────────────── */

#if defined(CONFIG_LIBLC3)

#define SAMPLE_RATE          48000
#define FRAME_DURATION_US    10000
#define SAMPLES_PER_CHANNEL  ((FRAME_DURATION_US * SAMPLE_RATE) / USEC_PER_SEC)
/* Largest output the decoder might produce in one recv callback */
#define OUT_BUF_SAMPLES      (SAMPLES_PER_CHANNEL * MAX_SINK_CHANNELS)

static lc3_decoder_mem_48k_t decoder_mem[MAX_SINK_ASE];
static int frames_per_sdu[MAX_SINK_ASE];

/* Scratch buffer for mono decode, then interleaved stereo push buffer */
static int16_t mono_buf[SAMPLES_PER_CHANNEL];
static int16_t l_buf[SAMPLES_PER_CHANNEL];
static int16_t r_buf[SAMPLES_PER_CHANNEL];
static int16_t stereo_out[OUT_BUF_SAMPLES];

/* Synchronisation: each ASE recv pushes its decoded data into the
 * channel slot, then we push stereo when both (or the single) ASE
 * have been received in this 10 ms window.
 */
static bool l_received;
static bool r_received;

#endif /* CONFIG_LIBLC3 */

/* ── helpers ─────────────────────────────────────────────────────── */

void print_hex(const uint8_t *ptr, size_t len)
{
	while (len-- != 0) {
		printk("%02x", *ptr++);
	}
}

static bool print_cb(struct bt_data *data, void *user_data)
{
	const char *str = (const char *)user_data;

	printk("%s: type 0x%02x value_len %u\n", str, data->type, data->data_len);
	print_hex(data->data, data->data_len);
	printk("\n");
	return true;
}

static void print_codec_cfg(const struct bt_audio_codec_cfg *codec_cfg)
{
	printk("codec_cfg 0x%02x cid 0x%04x vid 0x%04x count %u\n",
	       codec_cfg->id, codec_cfg->cid, codec_cfg->vid, codec_cfg->data_len);

	if (codec_cfg->id == BT_HCI_CODING_FORMAT_LC3) {
		bt_audio_data_parse(codec_cfg->data, codec_cfg->data_len, print_cb, "data");

		int ret;
		ret = bt_audio_codec_cfg_get_freq(codec_cfg);
		if (ret > 0) {
			printk("  Frequency: %d Hz\n", bt_audio_codec_cfg_freq_to_freq_hz(ret));
		}
		ret = bt_audio_codec_cfg_get_frame_dur(codec_cfg);
		if (ret > 0) {
			printk("  Frame Duration: %d us\n",
			       bt_audio_codec_cfg_frame_dur_to_frame_dur_us(ret));
		}
		printk("  Octets per frame: %d\n",
		       bt_audio_codec_cfg_get_octets_per_frame(codec_cfg));
		printk("  Frames per SDU: %d\n",
		       bt_audio_codec_cfg_get_frame_blocks_per_sdu(codec_cfg, true));
	} else {
		print_hex(codec_cfg->data, codec_cfg->data_len);
	}
	bt_audio_data_parse(codec_cfg->meta, codec_cfg->meta_len, print_cb, "meta");
}

static void print_qos(const struct bt_bap_qos_cfg *qos)
{
	printk("QoS: interval %u framing 0x%02x phy 0x%02x sdu %u "
	       "rtn %u latency %u pd %u\n",
	       qos->interval, qos->framing, qos->phy, qos->sdu,
	       qos->rtn, qos->latency, qos->pd);
}

/* ── index helpers ───────────────────────────────────────────────── */

static size_t sink_idx(const struct bt_bap_stream *s)
{
	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		if (s == &sinks[i].stream) {
			return i;
		}
	}
	__ASSERT(false, "Unknown sink stream %p", s);
	return 0;
}

static size_t stream_alloc_idx(void)
{
	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		if (!sinks[i].stream.conn) {
			return i;
		}
	}
	return MAX_SINK_ASE;	/* no free slot */
}

/* ── ASCS callbacks ──────────────────────────────────────────────── */

static int lc3_config(struct bt_conn *conn, const struct bt_bap_ep *ep, enum bt_audio_dir dir,
		      const struct bt_audio_codec_cfg *codec_cfg, struct bt_bap_stream **stream,
		      struct bt_bap_qos_cfg_pref *const pref, struct bt_bap_ascs_rsp *rsp)
{
	printk("ASE Config: conn %p ep %p dir %u\n", conn, ep, dir);
	print_codec_cfg(codec_cfg);

	if (dir != BT_AUDIO_DIR_SINK) {
		printk("Source direction unsupported\n");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_UNSUPPORTED,
				       BT_BAP_ASCS_REASON_NONE);
		return -EINVAL;
	}

	size_t idx = stream_alloc_idx();
	if (idx >= MAX_SINK_ASE) {
		printk("No free sink slot (max %d)\n", MAX_SINK_ASE);
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_NO_MEM, BT_BAP_ASCS_REASON_NONE);
		return -ENOMEM;
	}

	*stream = &sinks[idx].stream;
	sinks[idx].decoder = NULL;
	sinks[idx].recv_cnt = 0;
	num_sink_ase++;

	int cc = bt_audio_codec_cfg_get_chan_allocation(codec_cfg, NULL, false);
	printk("  (chan alloc get returned code %d; using chan_count=1 per ASE)\n", cc);
	sinks[idx].chan_count = 1;

	*pref = qos_pref;
	return 0;
}

static int lc3_qos(struct bt_bap_stream *stream, const struct bt_bap_qos_cfg *qos,
		   struct bt_bap_ascs_rsp *rsp)
{
	printk("QoS: stream %p\n", stream);
	print_qos(qos);
	return 0;
}

static int lc3_enable(struct bt_bap_stream *stream, const uint8_t meta[], size_t meta_len,
		      struct bt_bap_ascs_rsp *rsp)
{
	size_t idx = sink_idx(stream);
	printk("Enable: stream[%zu] meta_len %zu\n", idx, meta_len);

#if defined(CONFIG_LIBLC3)
	int ret;

	ret = bt_audio_codec_cfg_get_freq(stream->codec_cfg);
	if (ret <= 0) {
		printk("Error: freq not set\n");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
				       BT_BAP_ASCS_REASON_CODEC_DATA);
		return ret;
	}
	int freq = bt_audio_codec_cfg_freq_to_freq_hz(ret);

	ret = bt_audio_codec_cfg_get_frame_dur(stream->codec_cfg);
	if (ret <= 0) {
		printk("Error: frame dur not set\n");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
				       BT_BAP_ASCS_REASON_CODEC_DATA);
		return ret;
	}
	int frame_us = bt_audio_codec_cfg_frame_dur_to_frame_dur_us(ret);

	frames_per_sdu[idx] =
		bt_audio_codec_cfg_get_frame_blocks_per_sdu(stream->codec_cfg, true);

	sinks[idx].decoder = lc3_setup_decoder(frame_us, freq, 0, &decoder_mem[idx]);
	if (!sinks[idx].decoder) {
		printk("LC3 decoder setup failed\n");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
				       BT_BAP_ASCS_REASON_CODEC_DATA);
		return -1;
	}
	printk("LC3 decoder[%zu]: %d Hz, %d us\n", idx, freq, frame_us);
#endif
	return 0;
}

static int lc3_start(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Start: stream[%zu]\n", sink_idx(stream));
	l_received = false;
	r_received = false;
	return 0;
}

static int lc3_metadata(struct bt_bap_stream *stream, const uint8_t meta[], size_t meta_len,
			struct bt_bap_ascs_rsp *rsp)
{
	printk("Metadata: stream %p meta_len %zu\n", stream, meta_len);
	return 0;
}

static int lc3_disable(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Disable: stream %p\n", stream);
	sinks[sink_idx(stream)].decoder = NULL;
	return 0;
}

static int lc3_stop(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Stop: stream %p\n", stream);
	return 0;
}

static int lc3_release(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Release: stream %p\n", stream);
	size_t idx = sink_idx(stream);
	sinks[idx].decoder = NULL;
	memset(&sinks[idx], 0, sizeof(sinks[idx]));
	if (num_sink_ase > 0) {
		num_sink_ase--;
	}
	return 0;
}

static struct bt_bap_unicast_server_register_param param = {
	CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT,
	CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT
};

static const struct bt_bap_unicast_server_cb unicast_server_cb = {
	.config   = lc3_config,
	.qos      = lc3_qos,
	.enable   = lc3_enable,
	.start    = lc3_start,
	.metadata = lc3_metadata,
	.disable  = lc3_disable,
	.stop     = lc3_stop,
	.release  = lc3_release,
};

/* ── Data path: LC3 decode → stereo interleave → I2S push ───────── */

#if defined(CONFIG_LIBLC3)

static void push_stereo(void)
{
	if (num_sink_ase == 2 && l_received && r_received) {
		/* Two separate mono ASEs — interleave */
		for (int n = 0; n < SAMPLES_PER_CHANNEL; n++) {
			stereo_out[2 * n]     = l_buf[n];
			stereo_out[2 * n + 1] = r_buf[n];
		}
		audio_i2s_push(stereo_out, SAMPLES_PER_CHANNEL * 2);
		l_received = false;
		r_received = false;

	} else if (num_sink_ase == 1) {
		/* Single mono ASE — duplicate to both channels */
		for (int n = 0; n < SAMPLES_PER_CHANNEL; n++) {
			stereo_out[2 * n]     = l_received ? l_buf[n] : r_buf[n];
			stereo_out[2 * n + 1] = l_received ? l_buf[n] : r_buf[n];
		}
		audio_i2s_push(stereo_out, SAMPLES_PER_CHANNEL * 2);
		l_received = false;
		r_received = false;
	}
}

static void stream_recv(struct bt_bap_stream *stream,
			const struct bt_iso_recv_info *info,
			struct net_buf *buf)
{
	size_t idx = sink_idx(stream);
	struct audio_sink *as = &sinks[idx];
	const bool valid = (info->flags & BT_ISO_FLAGS_VALID) != 0;
	const int octets_per_frame =
		frames_per_sdu[idx] > 0 ? (buf->len / frames_per_sdu[idx]) : buf->len;

	if (valid) {
		as->recv_cnt++;
		if (CONFIG_INFO_REPORTING_INTERVAL > 0 &&
		    (as->recv_cnt % CONFIG_INFO_REPORTING_INTERVAL) == 0U) {
			printk("Audio stream[%zu]: %zu SDU\n", idx, as->recv_cnt);
		}
	} else {
		printk("Bad packet stream[%zu]: 0x%02X\n", idx, info->flags);
	}

	if (!as->decoder) {
		printk("LC3 decoder not ready for stream[%zu]\n", idx);
		return;
	}

	for (int i = 0; i < frames_per_sdu[idx]; i++) {
		const int err = lc3_decode(
			as->decoder,
			valid ? net_buf_pull_mem(buf, octets_per_frame) : NULL,
			octets_per_frame,
			LC3_PCM_FORMAT_S16,
			mono_buf, 1);

		if (err == 1) {
			/* PLC performed by codec itself */
		} else if (err < 0) {
			printk("[%zu:%d]: LC3 decode error %d\n", idx, i, err);
		}
	}

	/* Store decoded mono into the correct channel slot */
	if (idx == 0) {
		memcpy(l_buf, mono_buf, SAMPLES_PER_CHANNEL * sizeof(int16_t));
		l_received = true;
	} else {
		memcpy(r_buf, mono_buf, SAMPLES_PER_CHANNEL * sizeof(int16_t));
		r_received = true;
	}

	push_stereo();
}

#else /* !LIBLC3 — pass-thru path, mostly for compile check */

static void stream_recv(struct bt_bap_stream *stream,
			const struct bt_iso_recv_info *info,
			struct net_buf *buf)
{
	if (info->flags & BT_ISO_FLAGS_VALID) {
		sinks[sink_idx(stream)].recv_cnt++;
	}
}

#endif /* CONFIG_LIBLC3 */

/* ── Stream ops ──────────────────────────────────────────────────── */

static void stream_stopped(struct bt_bap_stream *s, uint8_t reason)
{
	printk("Stream[%zu] stopped: reason 0x%02X\n", sink_idx(s), reason);
}

static void stream_started(struct bt_bap_stream *s)
{
	struct bt_iso_info info;

	bt_iso_chan_get_info(s->iso, &info);
	printk("Stream[%zu] started: CIG %u CIS %u\n", sink_idx(s),
	       info.unicast.cig_id, info.unicast.cis_id);
	sinks[sink_idx(s)].recv_cnt = 0U;
}

static void stream_enabled_cb(struct bt_bap_stream *s)
{
	int err = bt_bap_stream_start(s);
	if (err) {
		printk("Failed to start stream[%zu]: %d\n", sink_idx(s), err);
	}
}

static struct bt_bap_stream_ops stream_ops = {
	.recv    = stream_recv,
	.stopped = stream_stopped,
	.started = stream_started,
	.enabled = stream_enabled_cb,
};

/* ── Connection callbacks ────────────────────────────────────────── */

static void connected(struct bt_conn *conn, uint8_t err)
{
	char a[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), a, sizeof(a));
	if (err) {
		printk("Connect failed: %s err %u %s\n", a, err, bt_hci_err_to_str(err));
		default_conn = NULL;
		return;
	}
	printk("Connected: %s\n", a);
	default_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn != default_conn) {
		return;
	}

	char a[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), a, sizeof(a));
	printk("Disconnected: %s reason 0x%02x\n", a, reason);

	audio_i2s_stop();

	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		sinks[i].decoder = NULL;
	}
	num_sink_ase = 0;

	bt_conn_unref(default_conn);
	default_conn = NULL;
	k_sem_give(&sem_disconnected);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected,
	.disconnected = disconnected,
};

/* ── PACS / contexts / location ──────────────────────────────────── */

static struct bt_pacs_cap cap_sink = { .codec_cap = &lc3_codec_cap };

static int set_location(void)
{
	int err = bt_pacs_set_location(BT_AUDIO_DIR_SINK,
				       BT_AUDIO_LOCATION_FRONT_LEFT |
				       BT_AUDIO_LOCATION_FRONT_RIGHT);
	if (err) {
		printk("set_location: %d\n", err);
	}
	return err;
}

static int set_supported_contexts(void)
{
	int err = bt_pacs_set_supported_contexts(BT_AUDIO_DIR_SINK,
						 AVAILABLE_SINK_CONTEXT);
	if (err) {
		printk("set_supported_contexts: %d\n", err);
	}
	return err;
}

static int set_available_contexts(void)
{
	int err = bt_pacs_set_available_contexts(BT_AUDIO_DIR_SINK,
						 AVAILABLE_SINK_CONTEXT);
	if (err) {
		printk("set_available_contexts: %d\n", err);
	}
	return err;
}

/* ── main ─────────────────────────────────────────────────────────── */

int main(void)
{
	const struct bt_pacs_register_param pacs_param = {
		.snk_pac = true, .snk_loc = true,
	};
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed: %d\n", err);
		return 0;
	}
	printk("BLE ready\n");

	if (bt_pacs_register(&pacs_param)) {
		printk("PACS register failed\n");
		return 0;
	}

	bt_bap_unicast_server_register(&param);
	bt_bap_unicast_server_register_cb(&unicast_server_cb);
	bt_pacs_cap_register(BT_AUDIO_DIR_SINK, &cap_sink);

	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		bt_bap_stream_cb_register(&sinks[i].stream, &stream_ops);
	}

	if (set_location() || set_supported_contexts() || set_available_contexts()) {
		return 0;
	}

	err = audio_i2s_init();
	if (err) {
		printk("I2S init failed: %d\n", err);
		return 0;
	}

	struct bt_le_ext_adv *adv;
	err = bt_le_ext_adv_create(BT_BAP_ADV_PARAM_CONN_QUICK, NULL, &adv);
	if (err) {
		printk("Adv create failed: %d\n", err);
		return 0;
	}
	err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Adv data failed: %d\n", err);
		return 0;
	}
	err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		printk("Adv start failed: %d\n", err);
		return 0;
	}

	printk("Advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);

	while (true) {
		k_sem_take(&sem_disconnected, K_FOREVER);
		printk("Restarting advertising...\n");

		err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
		if (err) {
			printk("Adv restart failed: %d\n", err);
			break;
		}
		printk("Advertising again\n");
	}

	return 0;
}
