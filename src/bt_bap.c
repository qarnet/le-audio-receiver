/*
 * Copyright (c) 2021-2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 *
 * LE Audio Receiver: BAP Unicast Server, ASCS callbacks, pairing,
 * advertising, PACS registration.
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
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/types.h>

#include "audio_sink.h"
#include "audio_timing.h"
#include "audio_decode.h"
#include "audio_stats.h"
#include "audio_volume.h"
#include "stream_lifecycle.h"

#if defined(CONFIG_LIBLC3)
#include "lc3.h"
#endif

LOG_MODULE_REGISTER(bt_bap, LOG_LEVEL_INF);

#define MAX_SINK_ASE      CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT
#define MAX_SINK_CHANNELS 2

#define AVAILABLE_SINK_CONTEXT                                                                     \
	(BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED | BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL |                \
	 BT_AUDIO_CONTEXT_TYPE_MEDIA | BT_AUDIO_CONTEXT_TYPE_GAME |                                \
	 BT_AUDIO_CONTEXT_TYPE_INSTRUCTIONAL)

/*
 * Advertise support for 48 kHz only, 7.5 and 10 ms frames, 1 or 2 channels.
 * Octet range 20–120 covers all standard LC3 configurations for this rate.
 */
static const struct bt_audio_codec_cap lc3_codec_cap = BT_AUDIO_CODEC_CAP_LC3(
	BT_AUDIO_CODEC_CAP_FREQ_48KHZ,
	BT_AUDIO_CODEC_CAP_DURATION_7_5 | BT_AUDIO_CODEC_CAP_DURATION_10,
	BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1) | BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(2), 20u,
	120u, 1u, AVAILABLE_SINK_CONTEXT);

static struct bt_conn *default_conn;

#if defined(CONFIG_LIBLC3)
#define SAMPLES_PER_CHANNEL_MAX 480 /* 48 kHz × 10 ms */
#define STEREO_OUT_MAX          (SAMPLES_PER_CHANNEL_MAX * 2)
#endif

struct bt_sink {
	struct bt_bap_stream stream;
	size_t recv_cnt;
	uint32_t pd_us; /* negotiated presentation delay */
	struct audio_decode_ctx decode;
};

static struct bt_sink sinks[MAX_SINK_ASE];
static size_t num_sink_ase;

static const struct bt_bap_qos_cfg_pref qos_pref =
	BT_BAP_QOS_CFG_PREF(true, BT_GAP_LE_PHY_2M, 0x02, 10, 10000, 80000, 40000, 40000);

static K_SEM_DEFINE(sem_disconnected, 0, 1);

static struct bt_le_ext_adv *adv;

static uint8_t unicast_server_addata[] = {
	BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL),
	BT_AUDIO_UNICAST_ANNOUNCEMENT_GENERAL,
	BT_BYTES_LIST_LE16(AVAILABLE_SINK_CONTEXT),
	BT_BYTES_LIST_LE16(0), /* no source */
	0x00,
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL)),
	BT_DATA(BT_DATA_SVC_DATA16, unicast_server_addata, ARRAY_SIZE(unicast_server_addata)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* ── LC3 decode static buffers ───────────────────────────────────── */

#if defined(CONFIG_LIBLC3)

static int16_t l_buf[SAMPLES_PER_CHANNEL_MAX];
static int16_t r_buf[SAMPLES_PER_CHANNEL_MAX];
static int16_t stereo_out[STEREO_OUT_MAX];
static bool l_received;
static bool r_received;

#endif /* CONFIG_LIBLC3 */

/* ── helpers ─────────────────────────────────────────────────────── */

static bool print_cb(struct bt_data *data, void *user_data)
{
	const char *str = (const char *)user_data;

	LOG_INF("%s: type 0x%02x value_len %u", str, data->type, data->data_len);
	LOG_HEXDUMP_DBG(data->data, data->data_len, "value");
	return true;
}

static void print_codec_cfg(const struct bt_audio_codec_cfg *codec_cfg)
{
	LOG_INF("codec_cfg 0x%02x cid 0x%04x vid 0x%04x count %u", codec_cfg->id, codec_cfg->cid,
		codec_cfg->vid, codec_cfg->data_len);

	if (codec_cfg->id == BT_HCI_CODING_FORMAT_LC3) {
		bt_audio_data_parse(codec_cfg->data, codec_cfg->data_len, print_cb, "data");

		int ret;

		ret = bt_audio_codec_cfg_get_freq(codec_cfg);
		if (ret > 0) {
			LOG_INF("  Frequency: %d Hz", bt_audio_codec_cfg_freq_to_freq_hz(ret));
		}
		ret = bt_audio_codec_cfg_get_frame_dur(codec_cfg);
		if (ret > 0) {
			LOG_INF("  Frame Duration: %d us",
				bt_audio_codec_cfg_frame_dur_to_frame_dur_us(ret));
		}
		LOG_INF("  Octets per frame: %d",
			bt_audio_codec_cfg_get_octets_per_frame(codec_cfg));
		LOG_INF("  Frames per SDU: %d",
			bt_audio_codec_cfg_get_frame_blocks_per_sdu(codec_cfg, true));
	} else {
		LOG_HEXDUMP_DBG(codec_cfg->data, codec_cfg->data_len, "codec data");
	}
	bt_audio_data_parse(codec_cfg->meta, codec_cfg->meta_len, print_cb, "meta");
}

static void print_qos(const struct bt_bap_qos_cfg *qos)
{
	LOG_INF("QoS: interval %u framing 0x%02x phy 0x%02x sdu %u "
		"rtn %u latency %u pd %u",
		qos->interval, qos->framing, qos->phy, qos->sdu, qos->rtn, qos->latency, qos->pd);
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
	return MAX_SINK_ASE; /* no free slot */
}

/* ── ASCS callbacks ──────────────────────────────────────────────── */

static int lc3_config(struct bt_conn *conn, const struct bt_bap_ep *ep, enum bt_audio_dir dir,
		      const struct bt_audio_codec_cfg *codec_cfg, struct bt_bap_stream **stream,
		      struct bt_bap_qos_cfg_pref *const pref, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("ASE Config: conn %p ep %p dir %u", conn, ep, dir);
	print_codec_cfg(codec_cfg);

	if (dir != BT_AUDIO_DIR_SINK) {
		LOG_WRN("Source direction unsupported");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_UNSUPPORTED,
				       BT_BAP_ASCS_REASON_NONE);
		return -EINVAL;
	}

	size_t idx = stream_alloc_idx();

	if (idx >= MAX_SINK_ASE) {
		LOG_ERR("No free sink slot (max %d)", MAX_SINK_ASE);
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_NO_MEM, BT_BAP_ASCS_REASON_NONE);
		return -ENOMEM;
	}

	*stream = &sinks[idx].stream;
#if defined(CONFIG_LIBLC3)
	sinks[idx].decode.decoder = NULL;
#endif
	sinks[idx].recv_cnt = 0;
	num_sink_ase++;

	enum bt_audio_location chan_alloc;
	int cc = bt_audio_codec_cfg_get_chan_allocation(codec_cfg, &chan_alloc, false);

	if (cc == 0) {
		int cnt = POPCOUNT(chan_alloc);

		sinks[idx].decode.chan_count = (cnt > 0) ? cnt : 1;
		LOG_INF("  chan alloc 0x%08x count=%d", chan_alloc, sinks[idx].decode.chan_count);
	} else {
		LOG_DBG("  chan alloc not found (%d), defaulting chan_count=1", cc);
		sinks[idx].decode.chan_count = 1;
	}

	/*
	 * Register with lifecycle gate AFTER final chan_count is known,
	 * so the started callback can distinguish Mode A (two mono ASEs)
	 * from Mode B / mono (single ASE).
	 */
	LOG_DBG("lifecycle: sink[%zu] configured chan_count=%d", idx, sinks[idx].decode.chan_count);
	stream_lifecycle_sink_configured(idx, sinks[idx].decode.chan_count);

	LOG_INF("  ASE[%zu] configured: num_sink_ase=%zu", idx, num_sink_ase);

	*pref = qos_pref;
	return 0;
}

static int lc3_qos(struct bt_bap_stream *stream, const struct bt_bap_qos_cfg *qos,
		   struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("QoS: stream %p", stream);
	print_qos(qos);

	size_t idx = sink_idx(stream);
	sinks[idx].pd_us = qos->pd;

	return 0;
}

static int lc3_enable(struct bt_bap_stream *stream, const uint8_t meta[], size_t meta_len,
		      struct bt_bap_ascs_rsp *rsp)
{
	size_t idx = sink_idx(stream);

	LOG_INF("Enable: stream[%zu] meta_len %zu", idx, meta_len);

#if defined(CONFIG_LIBLC3)
	int cc = sinks[idx].decode.chan_count;
	int ret = bt_audio_codec_cfg_get_freq(stream->codec_cfg);

	if (ret <= 0) {
		LOG_ERR("freq not set");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
				       BT_BAP_ASCS_REASON_CODEC_DATA);
		return ret;
	}
	int freq = bt_audio_codec_cfg_freq_to_freq_hz(ret);

	ret = bt_audio_codec_cfg_get_frame_dur(stream->codec_cfg);
	if (ret <= 0) {
		LOG_ERR("frame dur not set");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
				       BT_BAP_ASCS_REASON_CODEC_DATA);
		return ret;
	}
	int frame_us = bt_audio_codec_cfg_frame_dur_to_frame_dur_us(ret);

	int frames_per_sdu = bt_audio_codec_cfg_get_frame_blocks_per_sdu(stream->codec_cfg, true);

	ret = audio_decode_config(&sinks[idx].decode, cc, freq, frame_us, frames_per_sdu);
	if (ret < 0) {
		LOG_ERR("LC3 decoder setup failed (freq=%d dur=%d ch=%d)", freq, frame_us, cc);
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
				       BT_BAP_ASCS_REASON_CODEC_DATA);
		return ret;
	}
	LOG_INF("LC3 decoder[%zu]: %d Hz %d us ch=%d", idx, freq, frame_us, cc);
#endif
	return 0;
}

static int lc3_start(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Start: stream[%zu]", sink_idx(stream));
	l_received = false;
	r_received = false;
	return 0;
}

static int lc3_metadata(struct bt_bap_stream *stream, const uint8_t meta[], size_t meta_len,
			struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Metadata: stream %p meta_len %zu", stream, meta_len);
	return 0;
}

static int lc3_disable(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Disable: stream %p", stream);
#if defined(CONFIG_LIBLC3)
	size_t idx = sink_idx(stream);

	sinks[idx].decode.decoder = NULL;
	sinks[idx].decode.decoder_r = NULL;
#endif
	return 0;
}

static int lc3_stop(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Stop: stream %p", stream);

	/* Close audio path gate on stop (idempotent).  The disabled
	 * callback may fire later and close it again harmlessly.
	 */
	if (stream_lifecycle_audio_path_close()) {
#if defined(CONFIG_LIBLC3)
		l_received = false;
		r_received = false;
#endif
	}
	return 0;
}

static int lc3_release(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Release: stream %p", stream);
	size_t idx = sink_idx(stream);

#if defined(CONFIG_LIBLC3)
	sinks[idx].decode.decoder = NULL;
	sinks[idx].decode.decoder_r = NULL;
#endif
	/* Close audio path gate on release.  Idempotent — safe if
	 * already closed by an earlier disable/stop callback.
	 */
	if (stream_lifecycle_audio_path_close()) {
#if defined(CONFIG_LIBLC3)
		l_received = false;
		r_received = false;
#endif
	}

	memset(&sinks[idx], 0, sizeof(sinks[idx]));
	if (num_sink_ase > 0) {
		num_sink_ase--;
	}
	return 0;
}

static const struct bt_bap_unicast_server_cb unicast_server_cb = {
	.config = lc3_config,
	.qos = lc3_qos,
	.enable = lc3_enable,
	.start = lc3_start,
	.metadata = lc3_metadata,
	.disable = lc3_disable,
	.stop = lc3_stop,
	.release = lc3_release,
};

/* ── Data path: LC3 decode → stereo interleave → I2S push ───────── */

#if defined(CONFIG_LIBLC3)

static void push_stereo(void)
{
	if (num_sink_ase >= 1 && l_received && r_received) {
		int n = sinks[0].decode.samples_per_ch;

		audio_decode_interleave(l_buf, r_buf, stereo_out, n);
		audio_volume_apply(stereo_out, n * 2);
		int pr = audio_sink_push(stereo_out, n * 2);
		static size_t push_cnt;
		if (push_cnt < 5) {
			LOG_INF("push_stereo: n=%d push_ret=%d", n, pr);
			push_cnt++;
		}
		l_received = false;
		r_received = false;
	}
}

static void stream_recv(struct bt_bap_stream *stream, const struct bt_iso_recv_info *info,
			struct net_buf *buf)
{
	size_t idx = sink_idx(stream);
	struct bt_sink *as = &sinks[idx];
	const bool valid = (info->flags & BT_ISO_FLAGS_VALID) != 0;
	const bool has_ts = (info->flags & BT_ISO_FLAGS_TS) != 0;
	const int f_per_sdu = as->decode.frames_per_sdu;
	const int spc = as->decode.samples_per_ch;
	static size_t diagnostic_cnt;

	/* Phase 4b.1: feed validated timestamp + presentation delay to
	 * hardware timing measurement (nRF54L15 GRTC path).  Only stream 0
	 * is used as the timing reference.  Gated behind audio-path-open
	 * to prevent late callbacks from re-arming hardware timers after
	 * teardown.
	 *
	 * Phase 4b.2: drift compensation is now per-block in
	 * audio_sink_push(), driven by PCLK feedforward + buffer-phase PI.
	 * ISO timestamps go ONLY to audio_timing for GRTC scheduling.
	 */
	if (idx == 0 && valid && has_ts && stream_lifecycle_audio_path_is_open()) {
		audio_timing_sdu_ref_update(info->ts, sinks[0].pd_us);
	}

	if (diagnostic_cnt < 5) {
		LOG_INF("stream_recv[%zu]: valid=%d buf_len=%u f_per_sdu=%d spc=%d cc=%d "
			"num_ase=%zu",
			idx, valid, buf->len, f_per_sdu, spc, as->decode.chan_count, num_sink_ase);
		diagnostic_cnt++;
	}
	/* Running tally: log every 50th packet so we can see if valid data
	 * ever arrives (vs. only invalid/empty CIS events).
	 */
	static size_t valid_cnt, invalid_cnt;
	if (valid) {
		valid_cnt++;
	} else {
		invalid_cnt++;
	}
	if (((valid_cnt + invalid_cnt) % 50U) == 0U) {
		LOG_INF("stream_recv tally[%zu]: valid=%zu invalid=%zu", idx, valid_cnt,
			invalid_cnt);
	}

	if (valid) {
		as->recv_cnt++;
#if defined(CONFIG_INFO_REPORTING_INTERVAL) && CONFIG_INFO_REPORTING_INTERVAL > 0
		if ((as->recv_cnt % CONFIG_INFO_REPORTING_INTERVAL) == 0U) {
			LOG_INF("Audio stream[%zu]: %zu SDU", idx, as->recv_cnt);
		}
#endif
	} else {
		LOG_DBG("Bad packet stream[%zu]: 0x%02X", idx, info->flags);
	}

	/* ── Audio-path gate ─────────────────────────────────────────
	 * After a stream disables/stops/releases, the gate is closed.
	 * Late callbacks on the remaining ASE must NOT decode,
	 * interleave, push, or update audio timing/drift — any of
	 * these can restart I2S DMA.
	 */
	if (!stream_lifecycle_audio_path_is_open()) {
		static size_t gate_blocked;
		if (gate_blocked < 3 && valid) {
			LOG_INF("stream_recv[%zu]: gate closed, skipping decode", idx);
			gate_blocked++;
		}
		return;
	}

	if (!as->decode.decoder) {
		LOG_WRN("LC3 decoder not ready for stream[%zu]", idx);
		return;
	}

	if (as->decode.chan_count >= 2) {
		/* Mode B: stereo single-ASE — split SDU per-channel, two
		 * independent decoders with stride=2 handled by
		 * audio_decode_sdu.
		 */
		audio_decode_sdu(&as->decode, valid ? buf->data : NULL, buf->len, valid,
				 stereo_out);
		audio_volume_apply(stereo_out, spc * 2);
		audio_sink_push(stereo_out, spc * 2);
	} else if (num_sink_ase >= 2) {
		/* Mode A: 2 mono ASEs — decode to separate L/R buffers,
		 * then interleave when both have arrived.
		 */
		const int octets_per_frame = f_per_sdu > 0 ? (buf->len / f_per_sdu) : buf->len;
		int16_t *dest = (idx == 0) ? l_buf : r_buf;

		for (int i = 0; i < f_per_sdu; i++) {
			const int err =
				lc3_decode(as->decode.decoder,
					   valid ? net_buf_pull_mem(buf, octets_per_frame) : NULL,
					   octets_per_frame, LC3_PCM_FORMAT_S16, dest, 1);
			if (err == 1) {
				audio_stats_frame_plc();
			} else if (err < 0) {
				LOG_WRN("[%zu:%d]: LC3 decode error %d", idx, i, err);
				audio_stats_decode_error();
			} else {
				audio_stats_frame_decoded();
			}
		}
		if (idx == 0) {
			l_received = true;
		} else {
			r_received = true;
		}
		push_stereo();
	} else {
		/* Mono single-ASE: decode + mono-to-stereo handled by
		 * audio_decode_sdu.
		 */
		audio_decode_sdu(&as->decode, valid ? buf->data : NULL, buf->len, valid,
				 stereo_out);
		audio_volume_apply(stereo_out, spc * 2);
		audio_sink_push(stereo_out, spc * 2);
	}
}

#else /* !LIBLC3 — pass-thru path, mostly for compile check */

static void stream_recv(struct bt_bap_stream *stream, const struct bt_iso_recv_info *info,
			struct net_buf *buf)
{
	const bool valid = (info->flags & BT_ISO_FLAGS_VALID) != 0;
	const bool has_ts = (info->flags & BT_ISO_FLAGS_TS) != 0;

	if (valid) {
		sinks[sink_idx(stream)].recv_cnt++;

		/* Phase 4b.1: timing measurement for stream 0 */
		size_t idx = sink_idx(stream);
		if (idx == 0 && has_ts && stream_lifecycle_audio_path_is_open()) {
			audio_timing_sdu_ref_update(info->ts, sinks[0].pd_us);
		}
	}
}

#endif /* CONFIG_LIBLC3 */

/* ── Stream ops ──────────────────────────────────────────────────── */

static void stream_stopped(struct bt_bap_stream *s, uint8_t reason)
{
	size_t idx = sink_idx(s);

	LOG_INF("Stream[%zu] stopped: reason 0x%02X", idx, reason);

	/* Close audio path gate on stop (idempotent).  The disabled
	 * callback may fire later and close it again harmlessly.
	 */
	if (stream_lifecycle_audio_path_close()) {
#if defined(CONFIG_LIBLC3)
		l_received = false;
		r_received = false;
#endif
	}
}

static void stream_started(struct bt_bap_stream *s)
{
	size_t idx = sink_idx(s);
	struct bt_iso_info info;

	bt_iso_chan_get_info(s->iso, &info);
	LOG_INF("Stream[%zu] started: CIG %u CIS %u", idx, info.unicast.cig_id,
		info.unicast.cis_id);
	sinks[idx].recv_cnt = 0U;

	/* Lifecycle gate: open audio path when all required ASEs are started. */
	bool gate_opened = stream_lifecycle_sink_started(idx);
	if (gate_opened) {
		LOG_INF("Audio path gate OPEN (stream[%zu] completed the set)", idx);
	}
}

static void stream_enabled_cb(struct bt_bap_stream *s)
{
	int err = bt_bap_stream_start(s);

	if (err) {
		LOG_ERR("Failed to start stream[%zu]: %d", sink_idx(s), err);
	}
}

static void stream_disabled_cb(struct bt_bap_stream *s)
{
	size_t idx = sink_idx(s);

	LOG_INF("Stream[%zu] disabled", idx);

	/*
	 * Close the audio-path gate BEFORE stopping the sink.
	 * Must be first so late callbacks on the other ASE cannot
	 * decode, interleave, push, or restart I2S after the gate
	 * closes.  Clear channel-pair state (l_received/r_received)
	 * on the same transition so stale halves cannot pair.
	 */
	bool was_open = stream_lifecycle_audio_path_close();
	if (was_open) {
		LOG_INF("Audio path gate CLOSED (first disable)");
#if defined(CONFIG_LIBLC3)
		l_received = false;
		r_received = false;
#endif
	}

	/*
	 * audio_sink_stop() is idempotent and already calls
	 * audio_timing_reset() internally.  Do NOT duplicate the
	 * audio_timing_reset() call — it is redundant here.
	 */
	audio_sink_stop();
	audio_stats_reset();
}

static struct bt_bap_stream_ops stream_ops = {
	.recv = stream_recv,
	.stopped = stream_stopped,
	.started = stream_started,
	.enabled = stream_enabled_cb,
	.disabled = stream_disabled_cb,
};

/* ── Connection callbacks ────────────────────────────────────────── */

static void connected(struct bt_conn *conn, uint8_t err)
{
	char a[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), a, sizeof(a));
	if (err) {
		LOG_ERR("Connect failed: %s err %u %s", a, err, bt_hci_err_to_str(err));
		default_conn = NULL;
		return;
	}
	LOG_INF("Connected: %s", a);
	default_conn = bt_conn_ref(conn);

	/* Signal no contexts available while this connection owns the ASEs */
	bt_pacs_set_available_contexts(BT_AUDIO_DIR_SINK, BT_AUDIO_CONTEXT_TYPE_NONE);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn != default_conn) {
		return;
	}

	char a[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), a, sizeof(a));
	LOG_INF("Disconnected: %s reason 0x%02x", a, reason);

	/*
	 * Reset lifecycle gate so reconnect works without re-running
	 * audio_sink_init().  Close gate first (idempotent), then
	 * clear all state including the per-sink started flags.
	 */
	stream_lifecycle_audio_path_close();
	stream_lifecycle_reset();

#if defined(CONFIG_LIBLC3)
	l_received = false;
	r_received = false;

	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		audio_decode_reset(&sinks[i].decode);
	}
#endif

	audio_sink_stop();
	audio_stats_reset();

	num_sink_ase = 0;

	bt_conn_unref(default_conn);
	default_conn = NULL;

	/* Restore available contexts for the next client */
	bt_pacs_set_available_contexts(BT_AUDIO_DIR_SINK, AVAILABLE_SINK_CONTEXT);

	k_sem_give(&sem_disconnected);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

/* ── PACS / contexts / location ──────────────────────────────────── */

static struct bt_pacs_cap cap_sink = {.codec_cap = &lc3_codec_cap};

static int set_location(void)
{
	int err = bt_pacs_set_location(BT_AUDIO_DIR_SINK, BT_AUDIO_LOCATION_FRONT_LEFT |
								  BT_AUDIO_LOCATION_FRONT_RIGHT);
	if (err) {
		LOG_ERR("set_location: %d", err);
	}
	return err;
}

static int set_supported_contexts(void)
{
	int err = bt_pacs_set_supported_contexts(BT_AUDIO_DIR_SINK, AVAILABLE_SINK_CONTEXT);
	if (err) {
		LOG_ERR("set_supported_contexts: %d", err);
	}
	return err;
}

static int set_available_contexts(void)
{
	int err = bt_pacs_set_available_contexts(BT_AUDIO_DIR_SINK, AVAILABLE_SINK_CONTEXT);
	if (err) {
		LOG_ERR("set_available_contexts: %d", err);
	}
	return err;
}

/* ── Pairing callbacks (Just Works) ────────────────────────────────── */

static enum bt_security_err pairing_accept(struct bt_conn *conn,
					   const struct bt_conn_pairing_feat *const feat)
{
	LOG_INF("Pairing accepted");
	return BT_SECURITY_ERR_SUCCESS;
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_INF("Pairing complete, bonded: %d", bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_WRN("Pairing failed: %d", reason);
}

static struct bt_conn_auth_info_cb conn_auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static struct bt_conn_auth_cb conn_auth_cb = {
	.pairing_accept = pairing_accept,
};

/* ── public API ───────────────────────────────────────────────────── */

int bt_bap_init(void)
{
	const struct bt_pacs_register_param pacs_param = {
		.snk_pac = true,
		.snk_loc = true,
	};
	static struct bt_bap_unicast_server_register_param param = {
		CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT, CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT};
	int err;

	bt_conn_auth_cb_register(&conn_auth_cb);
	bt_conn_auth_info_cb_register(&conn_auth_info_cb);

	if (bt_pacs_register(&pacs_param)) {
		LOG_ERR("PACS register failed");
		return -EIO;
	}

	err = bt_bap_unicast_server_register(&param);
	if (err) {
		LOG_ERR("BAP unicast server register failed: %d", err);
		return err;
	}

	err = bt_bap_unicast_server_register_cb(&unicast_server_cb);
	if (err) {
		LOG_ERR("BAP unicast server cb register failed: %d", err);
		return err;
	}

	err = bt_pacs_cap_register(BT_AUDIO_DIR_SINK, &cap_sink);
	if (err) {
		LOG_ERR("PACS cap register failed: %d", err);
		return err;
	}

	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		bt_bap_stream_cb_register(&sinks[i].stream, &stream_ops);
	}

	if (set_location() || set_supported_contexts() || set_available_contexts()) {
		return -EIO;
	}

	err = bt_le_ext_adv_create(BT_BAP_ADV_PARAM_CONN_QUICK, NULL, &adv);
	if (err) {
		LOG_ERR("Adv create failed: %d", err);
		return err;
	}
	err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("Adv data failed: %d", err);
		return err;
	}

	return 0;
}

int bt_bap_restart_advertising(void)
{
	return bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
}

void bt_bap_wait_disconnect(void)
{
	k_sem_take(&sem_disconnected, K_FOREVER);
}
