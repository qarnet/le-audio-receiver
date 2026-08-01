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
#include "audio_perf.h"
#include "audio_volume.h"
#include "audio_offload.h"
#include "stream_lifecycle.h"

#if defined(CONFIG_BSIM_OBSERVER)
#include "bsim_observer.h"
#endif

#if defined(CONFIG_LIBLC3)
#include "lc3.h"
#endif

LOG_MODULE_REGISTER(bt_bap, LOG_LEVEL_INF);

/*
 * Sink stream pool.  Production registers exactly
 * CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT sink ASEs and allocates one stream
 * per ASE.  The BSim build registers three sink ASEs (to reach the
 * otherwise unreachable NO_MEM path) but keeps the repository stream
 * pool limited to two via the test-only CONFIG_BSIM_SINK_POOL_LIMIT
 * symbol; production builds contain no such symbol.
 */
#if defined(CONFIG_BSIM_SINK_POOL_LIMIT)
#define MAX_SINK_ASE CONFIG_BSIM_SINK_POOL_LIMIT
#else
#define MAX_SINK_ASE CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT
#endif

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

#if defined(CONFIG_BSIM_SOURCE_ASE)
/*
 * BSim-only source capability.  Production is sink-only (zero source
 * ASEs); the BSim source endpoint exists solely to exercise the
 * repository's source-direction rejection through the real ASCS server
 * (the server refuses source Configs it has no PAC cap for before the
 * application callback runs, so a source PAC must exist in BSim).
 */
static const struct bt_audio_codec_cap lc3_source_codec_cap =
	BT_AUDIO_CODEC_CAP_LC3(BT_AUDIO_CODEC_CAP_FREQ_48KHZ,
			       BT_AUDIO_CODEC_CAP_DURATION_7_5 | BT_AUDIO_CODEC_CAP_DURATION_10,
			       BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1), 20u, 120u, 1u,
			       (BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED | BT_AUDIO_CONTEXT_TYPE_MEDIA));
#endif /* CONFIG_BSIM_SOURCE_ASE */

static struct bt_conn *default_conn;

/* stream ops — defined below; sink_release_slot re-registers them */
static struct bt_bap_stream_ops stream_ops;

#if defined(CONFIG_LIBLC3)
#define SAMPLES_PER_CHANNEL_MAX 480 /* 48 kHz × 10 ms */
#define STEREO_OUT_MAX          (SAMPLES_PER_CHANNEL_MAX * 2)
#endif

struct bt_sink {
	struct bt_bap_stream stream;
	size_t recv_cnt;
	uint32_t pd_us; /* negotiated presentation delay */
	struct audio_decode_ctx decode;

	/*
	 * Validated codec shape, stored at Config time.  Enable re-validates
	 * the retained codec config against this shape and fails safely on
	 * mismatch.  SDU length validation in stream_recv uses these fields.
	 */
	uint16_t freq_hz;
	uint16_t frame_dur_us;
	uint16_t octets_per_frame;
	uint8_t frame_blocks_per_sdu;
	uint8_t chan_count;

	/* Mode A pair identity.  half_ts is the ISO SDU reference time
	 * (BT_ISO_FLAGS_TS): both CISes of one CIG carry the same SDU
	 * reference at the same CIG event, so equal half_ts values pair the
	 * two halves of the same audio frame.  half_seq tracks the
	 * controller-reported ISO seq_num (offset between CISes by their
	 * activation delay — useless as a pairing key); half_idx counts
	 * this half's received SDUs since stream start (diagnostics). */
	bool half_valid;
	bool half_valid_src; /* original BT_ISO_FLAGS_VALID of this half's SDU */
	uint32_t half_ts;
	uint16_t half_seq;
	uint16_t half_idx;
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

static void mode_a_halves_clear(void)
{
	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		sinks[i].half_valid = false;
		sinks[i].half_idx = 0U;
	}
}

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
		if (ret >= 0) {
			LOG_INF("  Frequency: %d Hz", bt_audio_codec_cfg_freq_to_freq_hz(ret));
		}
		ret = bt_audio_codec_cfg_get_frame_dur(codec_cfg);
		if (ret >= 0) {
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

/* ── early codec-shape validation ─────────────────────────────────── */

/*
 * Validate one LC3 codec configuration against the advertised receiver
 * shape BEFORE any slot allocation or lifecycle mutation:
 *
 *  - codec ID is LC3;
 *  - frequency field present, converts successfully, exactly 48000 Hz;
 *  - frame-duration field present, converts successfully, 7500 or 10000 us;
 *  - octets-per-frame field present, 20..120 inclusive;
 *  - frame blocks per SDU resolves to exactly 1 (missing optional field
 *    falls back to 1 per the Zephyr/spec default; explicit other values
 *    rejected);
 *  - channel allocation absent means mono; a present allocation must
 *    contain exactly one or two channels, never zero or more than two.
 *
 * On success the validated shape is written to @p shape.  On failure the
 * caller's @p rsp is set to CONF_REJECTED / CODEC_DATA (the only valid
 * application rejection code for a bad codec configuration; CONF_INVALID
 * is excluded from the ASCS application response codes) and -EINVAL is
 * returned.  Nothing is mutated.
 */
static int validate_codec_cfg(const struct bt_audio_codec_cfg *codec_cfg, struct bt_sink *shape,
			      struct bt_bap_ascs_rsp *rsp)
{
	int ret;
	int freq_hz;
	int frame_us;
	int chan_count;
	enum bt_audio_location chan_alloc;

	if (codec_cfg->id != BT_HCI_CODING_FORMAT_LC3) {
		LOG_INF("Codec id 0x%02x not supported", codec_cfg->id);
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_UNSUPPORTED,
				       BT_BAP_ASCS_REASON_CODEC);
		return -EINVAL;
	}

	ret = bt_audio_codec_cfg_get_freq(codec_cfg);
	if (ret < 0) {
		LOG_INF("Codec config: missing/invalid frequency (%d)", ret);
		goto invalid;
	}
	freq_hz = bt_audio_codec_cfg_freq_to_freq_hz(ret);
	if (freq_hz != 48000) {
		LOG_INF("Codec config: unsupported frequency %d Hz", freq_hz);
		goto invalid;
	}

	ret = bt_audio_codec_cfg_get_frame_dur(codec_cfg);
	if (ret < 0) {
		LOG_INF("Codec config: missing/invalid frame duration (%d)", ret);
		goto invalid;
	}
	frame_us = bt_audio_codec_cfg_frame_dur_to_frame_dur_us(ret);
	if (frame_us != 7500 && frame_us != 10000) {
		LOG_INF("Codec config: unsupported frame duration %d us", frame_us);
		goto invalid;
	}

	ret = bt_audio_codec_cfg_get_octets_per_frame(codec_cfg);
	if (ret < 0) {
		LOG_INF("Codec config: missing/invalid octets per frame (%d)", ret);
		goto invalid;
	}
	if (ret < 20 || ret > 120) {
		LOG_INF("Codec config: octets per frame %d out of advertised range 20..120", ret);
		goto invalid;
	}
	const int octets = ret;

	ret = bt_audio_codec_cfg_get_frame_blocks_per_sdu(codec_cfg, true);
	if (ret < 0 || ret != 1) {
		LOG_INF("Codec config: frame blocks per SDU %d (must be exactly 1)", ret);
		goto invalid;
	}

	ret = bt_audio_codec_cfg_get_chan_allocation(codec_cfg, &chan_alloc, false);
	if (ret == -ENODATA) {
		/* Channel allocation absent → mono. */
		chan_count = 1;
	} else if (ret == 0) {
		/* BT_AUDIO_LOCATION_MONO_AUDIO is defined as 0, so a present
		 * allocation of 0 is the standard explicit mono encoding and
		 * counts as exactly one channel.  Any other value must carry
		 * exactly one or two channel bits. */
		if (chan_alloc == BT_AUDIO_LOCATION_MONO_AUDIO) {
			chan_count = 1;
		} else {
			chan_count = POPCOUNT(chan_alloc);
			if (chan_count == 0 || chan_count > MAX_SINK_CHANNELS) {
				LOG_INF("Codec config: channel allocation 0x%08x has %d channels",
					chan_alloc, chan_count);
				goto invalid;
			}
		}
	} else {
		LOG_INF("Codec config: missing/invalid channel allocation (%d)", ret);
		goto invalid;
	}

	shape->freq_hz = (uint16_t)freq_hz;
	shape->frame_dur_us = (uint16_t)frame_us;
	shape->octets_per_frame = (uint16_t)octets;
	shape->frame_blocks_per_sdu = 1;
	shape->chan_count = (uint8_t)chan_count;
	return 0;

invalid:
	*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_REJECTED, BT_BAP_ASCS_REASON_CODEC_DATA);
	return -EINVAL;
}

/* ── ASCS callbacks ──────────────────────────────────────────────── */

static int lc3_config(struct bt_conn *conn, const struct bt_bap_ep *ep, enum bt_audio_dir dir,
		      const struct bt_audio_codec_cfg *codec_cfg, struct bt_bap_stream **stream,
		      struct bt_bap_qos_cfg_pref *const pref, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("ASE Config: conn %p ep %p dir %u", conn, ep, dir);
	print_codec_cfg(codec_cfg);

	if (dir != BT_AUDIO_DIR_SINK) {
		LOG_INF("Source direction unsupported");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_UNSUPPORTED,
				       BT_BAP_ASCS_REASON_NONE);
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_config(false, dir, rsp->code, rsp->reason);
#endif
		return -EINVAL;
	}

	/*
	 * Validate the codec shape BEFORE slot allocation or any lifecycle
	 * mutation: a rejected Config must not allocate a sink slot,
	 * increment num_sink_ase, touch a decoder/lifecycle slot, or
	 * consume capacity needed by a later valid request.
	 */
	struct bt_sink shape;

	memset(&shape, 0, sizeof(shape));
	if (validate_codec_cfg(codec_cfg, &shape, rsp) != 0) {
		LOG_WRN("Codec config rejected: code 0x%02x reason 0x%02x", rsp->code, rsp->reason);
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_config(false, dir, rsp->code, rsp->reason);
#endif
		return -EINVAL;
	}

	size_t idx = stream_alloc_idx();

	if (idx >= MAX_SINK_ASE) {
		LOG_INF("No free sink slot (max %d)", MAX_SINK_ASE);
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_NO_MEM, BT_BAP_ASCS_REASON_NONE);
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_config(false, dir, rsp->code, rsp->reason);
#endif
		return -ENOMEM;
	}

	*stream = &sinks[idx].stream;
#if defined(CONFIG_LIBLC3)
	sinks[idx].decode.decoder = NULL;
#endif
	sinks[idx].recv_cnt = 0;
	sinks[idx].pd_us = 0;
	sinks[idx].freq_hz = shape.freq_hz;
	sinks[idx].frame_dur_us = shape.frame_dur_us;
	sinks[idx].octets_per_frame = shape.octets_per_frame;
	sinks[idx].frame_blocks_per_sdu = shape.frame_blocks_per_sdu;
	sinks[idx].chan_count = shape.chan_count;
	sinks[idx].half_valid = false;
	sinks[idx].half_idx = 0U;
	num_sink_ase++;

	LOG_INF("  ASE[%zu] configured: num_sink_ase=%zu chan_count=%u freq=%u dur=%u octets=%u",
		idx, num_sink_ase, sinks[idx].chan_count, sinks[idx].freq_hz,
		sinks[idx].frame_dur_us, sinks[idx].octets_per_frame);

	/*
	 * Register with lifecycle gate AFTER final chan_count is known,
	 * so the started callback can distinguish Mode A (two mono ASEs)
	 * from Mode B / mono (single ASE).
	 */
	stream_lifecycle_sink_configured(idx, sinks[idx].chan_count);

#if defined(CONFIG_BSIM_OBSERVER)
	bsim_observer_config(true, dir, BT_BAP_ASCS_RSP_CODE_SUCCESS, BT_BAP_ASCS_REASON_NONE);
#endif

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
	/*
	 * Re-validate the retained codec config against the shape stored
	 * at Config time.  Enable fails safely (CONF_INVALID / CODEC_DATA)
	 * if the retained config no longer matches the stored shape.
	 */
	struct bt_sink shape;

	memset(&shape, 0, sizeof(shape));
	if (validate_codec_cfg(stream->codec_cfg, &shape, rsp) != 0) {
		return -EINVAL;
	}
	if (shape.freq_hz != sinks[idx].freq_hz || shape.frame_dur_us != sinks[idx].frame_dur_us ||
	    shape.octets_per_frame != sinks[idx].octets_per_frame ||
	    shape.frame_blocks_per_sdu != sinks[idx].frame_blocks_per_sdu ||
	    shape.chan_count != sinks[idx].chan_count) {
		LOG_ERR("Enable: retained codec config no longer matches stored shape");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_REJECTED,
				       BT_BAP_ASCS_REASON_CODEC_DATA);
		return -EINVAL;
	}

	int ret = audio_decode_config(&sinks[idx].decode, sinks[idx].chan_count, sinks[idx].freq_hz,
				      sinks[idx].frame_dur_us, sinks[idx].frame_blocks_per_sdu);
	if (ret < 0) {
		LOG_ERR("LC3 decoder setup failed (freq=%d dur=%d ch=%d)", sinks[idx].freq_hz,
			sinks[idx].frame_dur_us, sinks[idx].chan_count);
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_REJECTED,
				       BT_BAP_ASCS_REASON_CODEC_DATA);
		return ret;
	}
	LOG_INF("LC3 decoder[%zu]: %d Hz %d us ch=%d", idx, sinks[idx].freq_hz,
		sinks[idx].frame_dur_us, sinks[idx].chan_count);

	/* Tell the audio sink the expected stereo frames per push
	 * (depends on frame duration: 360 for 7.5 ms, 480 for 10 ms).
	 */
	audio_sink_set_input_frames((uint16_t)sinks[idx].decode.samples_per_ch);
#endif
	return 0;
}

static int lc3_start(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Start: stream[%zu]", sink_idx(stream));
#if defined(CONFIG_LIBLC3)
	mode_a_halves_clear();
#endif
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

/*
 * Centralized audio-path close: used by stop/disabled/release/disconnect
 * so teardown diverges as little as possible.  Closes the gate FIRST (no
 * later receive callback can decode/push), stops offload exactly once
 * through the idempotent API, clears pending Mode A halves, and emits the
 * gate-close observer event on the open→closed transition.  Safe to call
 * repeatedly.
 */
static bool sink_close_audio_path(void)
{
	bool was_open = stream_lifecycle_audio_path_close();

	if (was_open) {
		LOG_INF("Audio path gate CLOSED");
		audio_offload_stream_stop();
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_gate_close();
#endif
	}
#if defined(CONFIG_LIBLC3)
	mode_a_halves_clear();
#endif
	return was_open;
}

/*
 * Release one sink slot: close path, stop offload and audio sink exactly
 * once through the idempotent APIs, clear lifecycle configuration for the
 * released slot, reset the decoder and app-owned slot state so the slot is
 * reusable, and preserve truthful PACS contexts (no context mutation).
 *
 * The bt_bap_stream struct itself is left intact: the ASCS server owns
 * conn/ep/codec_cfg/iso and clears them when the ASE reaches idle
 * (bt_bap_stream_detach).  Wiping the stream here crashes the server's
 * streaming-exit transition, which dereferences stream->iso after the
 * application release callback returns.
 */
static void sink_release_slot(size_t idx)
{
	bool was_open = sink_close_audio_path();

	if (was_open) {
		/* Stop the audio sink immediately on Release, before
		 * returning to ASCS, so the sink oracle can finalize the
		 * segment (snapshotting the statistics) before any later
		 * disabled/disconnect path.  audio_sink_stop() is
		 * idempotent; later paths must not create a second segment
		 * or hide pushes. */
		LOG_INF("Release: audio sink stopped");
		audio_sink_stop();
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_release_sink_stop();
#endif
	}

#if defined(CONFIG_LIBLC3)
	audio_decode_reset(&sinks[idx].decode);
#endif
	sinks[idx].recv_cnt = 0;
	sinks[idx].pd_us = 0;
	sinks[idx].freq_hz = 0;
	sinks[idx].frame_dur_us = 0;
	sinks[idx].octets_per_frame = 0;
	sinks[idx].frame_blocks_per_sdu = 0;
	sinks[idx].chan_count = 0;
	sinks[idx].half_valid = false;
	sinks[idx].half_idx = 0U;
	stream_lifecycle_sink_release(idx);
	if (num_sink_ase > 0) {
		num_sink_ase--;
	}
#if defined(CONFIG_BSIM_OBSERVER)
	bsim_observer_cleanup_release(idx);
#endif
}

static int lc3_stop(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Stop: stream %p", stream);
	sink_close_audio_path();
	return 0;
}

static int lc3_release(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Release: stream %p", stream);
	sink_release_slot(sink_idx(stream));
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

static void stream_recv(struct bt_bap_stream *stream, const struct bt_iso_recv_info *info,
			struct net_buf *buf)
{
	uint32_t t0 = audio_perf_cycle_start();

	size_t idx = sink_idx(stream);
	struct bt_sink *as = &sinks[idx];
	const bool valid = (info->flags & BT_ISO_FLAGS_VALID) != 0;
	const bool has_ts = (info->flags & BT_ISO_FLAGS_TS) != 0;
	const int f_per_sdu = as->decode.frames_per_sdu;
	const int spc = as->decode.samples_per_ch;

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
#if defined(CONFIG_BSIM_OBSERVER)
		if (valid) {
			bsim_observer_recv_gate_blocked();
		}
#endif
		audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);
		return;
	}

	if (!as->decode.decoder) {
		LOG_WRN("LC3 decoder not ready for stream[%zu]", idx);
		audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);
		return;
	}

	/*
	 * Exact SDU payload validation (valid packets only).  A valid-flag
	 * packet whose length does not match the configured shape is
	 * rejected BEFORE any decode/pull/copy: exactly one
	 * decode-error/malformed-SDU evidence increment, no liblc3 call,
	 * no left/right pairing-state mutation, no volume apply, no push.
	 * PLC (valid=false) remains supported with the configured byte
	 * shape and may produce concealment output.
	 */
	if (valid && as->octets_per_frame > 0U) {
		size_t expected = (size_t)as->octets_per_frame * as->frame_blocks_per_sdu;

		if (as->decode.chan_count == 2) {
			expected *= 2U; /* Mode B: [L frame][R frame] per block */
		}
		if (buf->len != expected) {
			LOG_INF("stream[%zu]: malformed SDU len %u != expected %zu", idx, buf->len,
				expected);
			audio_stats_decode_error();
#if defined(CONFIG_BSIM_OBSERVER)
			bsim_observer_malformed_sdu();
#endif
			audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);
			return;
		}
	}

	if (as->decode.chan_count >= 2) {
		/* Mode B: stereo single-ASE — split SDU per-channel, two
		 * independent decoders with stride=2 handled by
		 * audio_decode_sdu.  A negative decode return skips volume
		 * and sink push.
		 */
		int ret = audio_decode_sdu(&as->decode, valid ? buf->data : NULL, buf->len, valid,
					   stereo_out);

		if (ret < 0) {
			LOG_WRN("stream[%zu]: decode failed %d — skipping volume/push", idx, ret);
			audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);
			return;
		}
		audio_volume_apply(stereo_out, spc * 2);

#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_pre_push(valid);
#endif
		if (audio_sink_push(stereo_out, spc * 2) < 0) {
			audio_perf_push_failure();
		}
	} else if (num_sink_ase >= 2) {
		/* Mode A: 2 mono ASEs — decode to separate L/R buffers.
		 * Hard decoder errors skip that half and cannot pair it;
		 * PLC halves may pair and produce concealment output.
		 * The ISO SDU reference time is the pairing key, so a
		 * missing TS flag makes this half unusable: skip decoder,
		 * pairing mutation, and push, count one receive/decode
		 * fault, and emit the test observer event (a real warning
		 * — the normal matrix proves zero occurrences). */
		if (!(info->flags & BT_ISO_FLAGS_TS)) {
			LOG_WRN("stream[%zu]: Mode A SDU missing TS flag — half skipped", idx);
			audio_stats_decode_error();
#if defined(CONFIG_BSIM_OBSERVER)
			bsim_observer_missing_ts();
#endif
			audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);
			return;
		}

		const int octets_per_frame = f_per_sdu > 0 ? (buf->len / f_per_sdu) : buf->len;
		int16_t *dest = (idx == 0) ? l_buf : r_buf;
		bool decoded_ok = true;

		for (int i = 0; i < f_per_sdu; i++) {
			uint32_t t1 = audio_perf_cycle_start();
			const int err =
				lc3_decode(as->decode.decoder,
					   valid ? net_buf_pull_mem(buf, octets_per_frame) : NULL,
					   octets_per_frame, LC3_PCM_FORMAT_S16, dest, 1);
			audio_perf_cycle_end(t1, AUDIO_PERF_PATH_LC3_DECODE);
			if (err == 1) {
				audio_stats_frame_plc();
			} else if (err < 0) {
				LOG_WRN("[%zu:%d]: LC3 decode error %d", idx, i, err);
				audio_stats_decode_error();
				decoded_ok = false;
			} else {
				audio_stats_frame_decoded();
			}
		}
		if (!decoded_ok) {
			/* Hard decoder error: this half cannot pair.  Leave
			 * half-valid state untouched so a stale half can
			 * never pair with the failed one. */
			audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);
			return;
		}

		/*
		 * Mode A pair identity: interleave and push only when both
		 * halves belong to the same CIG event.  The SW Split LL
		 * numbers each CIS from a CIG-global counter, so the two
		 * CIS seq spaces carry a constant offset (their activation
		 * delay) that no client-side TX hold can remove — exact
		 * seq or per-half-index pairing cannot match.  Both CISes
		 * of one CIG share the ISO SDU reference time at each
		 * event, so equal half_ts values identify the two halves of
		 * the same audio frame; a wrap-safe 32-bit comparison
		 * discards only the older unmatched half.  Each half's
		 * original ISO-valid flag is stored separately from the
		 * decoder result: a push is source-valid only when BOTH
		 * paired halves had VALID set (the test oracle's startup
		 * boundary uses this).  The controller-reported ISO
		 * seq_num stays tracked per half.
		 */
		as->half_valid = true;
		as->half_valid_src = valid;
		as->half_ts = info->ts;
		as->half_seq = info->seq_num;
		as->half_idx++;

		const size_t other = (idx == 0) ? 1 : 0;

		if (sinks[other].half_valid) {
			const uint32_t diff = (uint32_t)(as->half_ts - sinks[other].half_ts);

			if (diff == 0U) {
				/* Same CIG event — pair and push. */
				audio_decode_interleave(l_buf, r_buf, stereo_out, spc);
				audio_volume_apply(stereo_out, spc * 2);

#if defined(CONFIG_BSIM_OBSERVER)
				bsim_observer_pre_push(sinks[0].half_valid_src &&
						       sinks[1].half_valid_src);
#endif
				if (audio_sink_push(stereo_out, spc * 2) < 0) {
					audio_perf_push_failure();
				}
				as->half_valid = false;
				sinks[other].half_valid = false;
			} else if (diff < 0x80000000U) {
				/* This half is newer (larger ts) — discard the older half. */
				LOG_INF("Mode A: stale half discarded (ts %u < %u, seq %u < %u)",
					sinks[other].half_ts, as->half_ts, sinks[other].half_seq,
					as->half_seq);
				sinks[other].half_valid = false;
#if defined(CONFIG_BSIM_OBSERVER)
				bsim_observer_stale_half();
#endif
			} else {
				/* Other half is newer — discard this (older) half. */
				LOG_INF("Mode A: stale half discarded (ts %u < %u, seq %u < %u)",
					as->half_ts, sinks[other].half_ts, as->half_seq,
					sinks[other].half_seq);
				as->half_valid = false;
#if defined(CONFIG_BSIM_OBSERVER)
				bsim_observer_stale_half();
#endif
			}
		}

	} else {
		/* Mono single-ASE: decode + mono-to-stereo handled by
		 * audio_decode_sdu.  A negative decode return skips volume
		 * and sink push.
		 */
		int ret = audio_decode_sdu(&as->decode, valid ? buf->data : NULL, buf->len, valid,
					   stereo_out);

		if (ret < 0) {
			LOG_WRN("stream[%zu]: decode failed %d — skipping volume/push", idx, ret);
			audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);
			return;
		}
		audio_volume_apply(stereo_out, spc * 2);

#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_pre_push(valid);
#endif
		if (audio_sink_push(stereo_out, spc * 2) < 0) {
			audio_perf_push_failure();
		}
	}

	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);
}

#else /* !LIBLC3 — pass-thru path, mostly for compile check */

static void stream_recv(struct bt_bap_stream *stream, const struct bt_iso_recv_info *info,
			struct net_buf *buf)
{
	uint32_t t0 = audio_perf_cycle_start();
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
	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);
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
	sink_close_audio_path();
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
#if defined(CONFIG_LIBLC3)
		/* Clear any pending Mode A halves from the previous session. */
		mode_a_halves_clear();
#endif
		/* Phase 5.0: reset perf counters at start of new audio session.
		 * Metrics from previous session are discarded here; use
		 * 'audio perf' before gate opens to inspect completed-session data.
		 */
		audio_perf_reset();

		/* Phase 6 Stage 2: start offload pipeline for new stream. */
		audio_offload_stream_start();

#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_gate_open();
#endif
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
	 * closes.  The centralized close clears pending Mode A halves
	 * on the same transition so stale halves cannot pair.
	 */
	bool was_open = sink_close_audio_path();
	if (was_open) {
		LOG_INF("Audio path gate CLOSED (first disable)");
	}

	/*
	 * Stream summary: log key counters before reset so the gate
	 * can extract explicit SDUs/decoded/I2S evidence.
	 */
	{
		struct audio_stats stats = audio_stats_get();
		LOG_INF("Stream[%zu] summary: SDUs=%zu decoded=%u plc=%u "
			"decode_err=%u i2s_underrun=%u stream_reset=%u",
			idx, sinks[idx].recv_cnt, stats.total_frames, stats.plc_frames,
			stats.decode_errors, stats.i2s_underruns, stats.stream_resets);
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

	/* Keep available contexts truthful — ACL connection is not ASE ownership.
	 * Stock desktop policy (BlueZ/WirePlumber) reads PACS during connection
	 * and needs to see the correct available contexts to create audio devices. */
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
	 * Centralized teardown: close gate (idempotent), clear pending
	 * Mode A halves, then stop offload and the audio sink exactly
	 * once through idempotent APIs.  lifecycle reset clears the
	 * per-sink started flags so reconnect works without re-running
	 * audio_sink_init().
	 */
	sink_close_audio_path();
	stream_lifecycle_reset();
	audio_offload_stream_stop();

#if defined(CONFIG_LIBLC3)
	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		audio_decode_reset(&sinks[i].decode);
	}
#endif

	audio_sink_stop();
	audio_stats_reset();

	num_sink_ase = 0;

#if defined(CONFIG_BSIM_OBSERVER)
	bsim_observer_cleanup_disconnect();
#endif

	bt_conn_unref(default_conn);
	default_conn = NULL;

	/* Available contexts persist from initial registration.
	 * No restore needed — ACL disconnect does not alter the default. */
	k_sem_give(&sem_disconnected);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

/* ── PACS / contexts / location ──────────────────────────────────── */

static struct bt_pacs_cap cap_sink = {.codec_cap = &lc3_codec_cap};
#if defined(CONFIG_BSIM_SOURCE_ASE)
static struct bt_pacs_cap cap_source = {.codec_cap = &lc3_source_codec_cap};
#endif

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
#if defined(CONFIG_BSIM_SOURCE_ASE)
		.src_pac = true,
		.src_loc = true,
#endif
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

#if defined(CONFIG_BSIM_SOURCE_ASE)
	err = bt_pacs_cap_register(BT_AUDIO_DIR_SOURCE, &cap_source);
	if (err) {
		LOG_ERR("PACS source cap register failed: %d", err);
		return err;
	}
#endif

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
