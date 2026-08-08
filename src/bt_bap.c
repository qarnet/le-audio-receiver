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
#include "audio_stats.h"
#include "audio_perf.h"
#include "audio_offload.h"
#include "audio_stream_session.h"
#include "bt_bap_pairing_adapter.h"
#include "bt_pairing_policy.h"
#include "stream_lifecycle.h"

#if defined(CONFIG_BSIM_OBSERVER)
#include "bsim_observer.h"
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

/* Pairing-mode policy (OPEN / BONDED_ONLY) and the lock serializing the
 * advertising set and controller-filter state between the main thread,
 * the shell thread, and future button work handlers. */
static struct bt_pairing_policy pairing_policy;
static K_MUTEX_DEFINE(pairing_adv_lock);

/* Short lifecycle lock serializing every stream_lifecycle_* call and
 * the audio-path transition generation between the BT RX thread and the
 * shell thread.  Fixed nesting is lifecycle_lock then sink mutex; never
 * reversed.  No lifecycle_lock hold spans offload cancellation/scheduling,
 * sink drain/I2S, decode, logging, or Bluetooth stack calls. */
static K_MUTEX_DEFINE(lifecycle_lock);

/* Audio-path transition generation: incremented on every closed→open and
 * open→closed gate transition.  stream_started() captures it when opening
 * and rechecks after its outside-lock open work so a shell stop that lands
 * in between invalidates the (offload-start) work and leaves final state
 * closed. */
static uint32_t audio_path_generation;

/* stream ops — defined below; sink_release_slot re-registers them */
static struct bt_bap_stream_ops stream_ops;

/*
 * Sink stream pool.  Production registers exactly
 * CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT sink ASEs and allocates one stream
 * per ASE.  The BSim build registers three sink ASEs (to reach the
 * otherwise unreachable NO_MEM path) but keeps the repository stream
 * pool limited to two via the test-only CONFIG_BSIM_SINK_POOL_LIMIT
 * symbol; production builds contain no such symbol.  The pool size MUST
 * equal AUDIO_STREAM_SESSION_MAX_SLOTS (same expression).
 */
#if defined(CONFIG_BSIM_SINK_POOL_LIMIT)
#define MAX_SINK_ASE CONFIG_BSIM_SINK_POOL_LIMIT
#else
#define MAX_SINK_ASE CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT
#endif

BUILD_ASSERT(MAX_SINK_ASE == AUDIO_STREAM_SESSION_MAX_SLOTS,
	     "bt_bap stream pool must match the audio stream session slot count");

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

static struct bt_bap_stream sinks[MAX_SINK_ASE];

static size_t sink_idx(const struct bt_bap_stream *s)
{
	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		if (s == &sinks[i]) {
			return i;
		}
	}
	__ASSERT(false, "Unknown sink stream %p", s);
	return 0;
}

static size_t stream_alloc_idx(void)
{
	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		if (!sinks[i].conn) {
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
static int validate_codec_cfg(const struct bt_audio_codec_cfg *codec_cfg,
			      struct audio_stream_codec_shape *shape, struct bt_bap_ascs_rsp *rsp)
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
	struct audio_stream_codec_shape shape;

	memset(&shape, 0, sizeof(shape));
	if (validate_codec_cfg(codec_cfg, &shape, rsp) != 0) {
		LOG_INF("Codec config rejected: code 0x%02x reason 0x%02x", rsp->code, rsp->reason);
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

	*stream = &sinks[idx];
	audio_stream_session_config(idx, &shape);

	LOG_INF("  ASE[%zu] configured: num_sink_ase=%zu chan_count=%u freq=%u dur=%u octets=%u",
		idx, audio_stream_session_configured_count(),
		audio_stream_session_shape(idx)->chan_count,
		audio_stream_session_shape(idx)->freq_hz,
		audio_stream_session_shape(idx)->frame_dur_us,
		audio_stream_session_shape(idx)->octets_per_frame);

	/*
	 * Register with lifecycle gate AFTER the slot is stored, so the
	 * started callback can distinguish Mode A (two configured ASEs)
	 * from Mode B / mono (single ASE).
	 */
	k_mutex_lock(&lifecycle_lock, K_FOREVER);
	stream_lifecycle_sink_configured(idx);
	k_mutex_unlock(&lifecycle_lock);

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
	audio_stream_session_qos(idx, qos->pd);

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
	 * at Config time.  Enable fails safely (CONF_REJECTED / CODEC_DATA)
	 * if the retained config no longer matches the stored shape.
	 */
	struct audio_stream_codec_shape shape;
	const struct audio_stream_codec_shape *stored = audio_stream_session_shape(idx);

	if (stored == NULL) {
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_REJECTED,
				       BT_BAP_ASCS_REASON_CODEC_DATA);
		return -EINVAL;
	}

	memset(&shape, 0, sizeof(shape));
	if (validate_codec_cfg(stream->codec_cfg, &shape, rsp) != 0) {
		return -EINVAL;
	}
	if (shape.freq_hz != stored->freq_hz || shape.frame_dur_us != stored->frame_dur_us ||
	    shape.octets_per_frame != stored->octets_per_frame ||
	    shape.frame_blocks_per_sdu != stored->frame_blocks_per_sdu ||
	    shape.chan_count != stored->chan_count) {
		LOG_ERR("Enable: retained codec config no longer matches stored shape");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_REJECTED,
				       BT_BAP_ASCS_REASON_CODEC_DATA);
		return -EINVAL;
	}

	int ret = audio_stream_session_enable(idx);

	if (ret < 0) {
		LOG_ERR("LC3 decoder setup failed (freq=%d dur=%d ch=%d)", stored->freq_hz,
			stored->frame_dur_us, stored->chan_count);
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_REJECTED,
				       BT_BAP_ASCS_REASON_CODEC_DATA);
		return ret;
	}
#endif
	return 0;
}

static int lc3_start(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Start: stream[%zu]", sink_idx(stream));
	audio_stream_session_start_clear();
	return 0;
}

static int lc3_metadata(struct bt_bap_stream *stream, const uint8_t meta[], size_t meta_len,
			struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Metadata: stream %p meta_len %zu", stream, meta_len);
	return 0;
}

/*
 * One explicit private teardown transition owner.
 *
 * Every stop/disable/disabled/release/disconnect/shell-stop composition
 * runs through teardown_transition(), which owns all low-level
 * session/lifecycle/sink/offload/stats/observer calls.  Thin callbacks
 * only translate into events.  Universal order (no lock spans
 * Bluetooth/decode/offload/I2S):
 *
 *   close lifecycle gate + sink push admission under lifecycle_lock
 *     -> release lock
 *     -> session RX lease drain (audio_stream_session_rx_close)
 *     -> sink push drain/stop (audio_sink_stop)
 *     -> offload stop (audio_offload_stream_stop)
 *     -> state reset
 *
 * Idempotence derives from the stream_lifecycle first-edge return, the
 * session configured flag, the sink stop cohort, and the offload
 * generation — there are no new global teardown flags and no second
 * state machine mirroring stream_lifecycle.
 */
enum teardown_event {
	TEARDOWN_CLOSE,      /* lc3_stop / stream_stopped: normal close, no stats */
	TEARDOWN_DISABLE,    /* lc3_disable: session decoder-disable, serialized */
	TEARDOWN_DISABLED,   /* stream_disabled_cb: normal close + summary + stats reset */
	TEARDOWN_RELEASE,    /* lc3_release(slot) */
	TEARDOWN_DISCONNECT, /* disconnected (default conn): returns advertising-wake bool */
	TEARDOWN_FORCED,     /* shell bt_bap_audio_path_stop */
};

/*
 * Global close primitive.  Closes the gate FIRST under the lifecycle lock
 * (no later receive callback can decode/push), increments the transition
 * generation on the open->closed edge, and closes sink push admission
 * nonblocking; then drains session receive leases outside the lock and —
 * on the first edge only — drains the sink pushes and stops offload, in
 * that order (R7 approved delta: sink-drain-before-offload, for both
 * normal and forced paths).  Normal BT close also clears the Mode A
 * assembler / sequence trackers after the drain; the shell forced path
 * never clears BT-RX-owned state.  Emits the gate-close observer exactly
 * once on the open->closed transition.  Safe to call repeatedly.
 *
 * @return true when this call performed the first open->closed edge.
 */
static bool teardown_close_path(bool forced)
{
	bool was_open;

	k_mutex_lock(&lifecycle_lock, K_FOREVER);
	if (forced) {
		was_open = stream_lifecycle_force_close();
	} else {
		was_open = stream_lifecycle_audio_path_close();
	}
	if (was_open) {
		audio_path_generation++;
	}
	audio_sink_stream_close();
	k_mutex_unlock(&lifecycle_lock);

	/* Close session receive admission and wait for admitted
	 * receive leases to drain BEFORE any decoder/assembler/sequence
	 * reset runs.  Outside the lifecycle lock: the drain wait must
	 * never run under it.  Idempotent; in-flight leases on the shared
	 * BT RX WQ have already completed when this runs from a callback,
	 * and a shell-thread close waits only for a WQ recv lease. */
	audio_stream_session_rx_close();

	if (was_open) {
		/* R7 order: sink drain/finalize first, THEN the offload stop,
		 * for both normal and forced paths (the old normal path
		 * stopped offload before the sink drain). */
		audio_sink_stop();
		audio_offload_stream_stop();
		LOG_INF("Audio path gate CLOSED");
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_gate_close();
#endif
	}
	if (!forced) {
		/* Mode A/sequence state is BT-RX-owned; only callbacks on
		 * the BT RX thread may clear it (the shell path preserves
		 * it, R1 policy).  Runs after the admission drain above. */
		audio_stream_session_start_clear();
	}
	return was_open;
}

/*
 * R7 dispatcher: owns every teardown composition.  @p slot is the sink
 * slot index for the slot-specific events (RELEASE/DISABLE/DISABLED).
 * Returns whether the advertising-restart semaphore must fire (DISCONNECT
 * only; all other events return false).
 */
static bool teardown_transition(enum teardown_event ev, size_t slot)
{
	switch (ev) {
	case TEARDOWN_CLOSE:
		/* lc3_stop / stream_stopped: normal close; a duplicate
		 * (already-closed gate) has no global side effect.  Stats
		 * retained (CLOSE is not a stats-reset event). */
		teardown_close_path(false);
		return false;

	case TEARDOWN_DISABLE:
		/* lc3_disable remains a session decoder-disable event, but it
		 * is transition state and is serialized under the lifecycle
		 * lock so config/start/disable/release/disconnect share the
		 * same lifecycle serialization (fixed nesting: lifecycle_lock
		 * then session mutex; session APIs never take lifecycle_lock). */
		k_mutex_lock(&lifecycle_lock, K_FOREVER);
		audio_stream_session_disable(slot);
		k_mutex_unlock(&lifecycle_lock);
		return false;

	case TEARDOWN_DISABLED: {
		bool was_open = teardown_close_path(false);

		if (was_open) {
			LOG_INF("Audio path gate CLOSED (first disable)");
		}

		/* Stream summary: log the counters before the stats reset so
		 * the gate can extract explicit SDUs/decoded/I2S evidence.
		 * audio_sink_stop() never mutates audio_stats, so the
		 * sink-before-offload delta does not change the summary
		 * values (output shape unchanged). */
		struct audio_stats stats = audio_stats_get();

		LOG_INF("Stream[%zu] summary: SDUs=%zu decoded=%u plc=%u "
			"decode_err=%u i2s_underrun=%u stream_reset=%u",
			slot, audio_stream_session_recv_count(slot), stats.total_frames,
			stats.plc_frames, stats.decode_errors, stats.i2s_underruns,
			stats.stream_resets);

		/* Stats reset once per disabled completion.  No second
		 * unconditional audio_sink_stop on a duplicate disabled
		 * event (the first close already stopped the sink). */
		audio_stats_reset();
		return false;
	}

	case TEARDOWN_RELEASE: {
		/* Duplicate release of an already-cleaned slot is an
		 * observable no-op: no observer, no close, no stats, no
		 * stream touch.  The session configured flag is the
		 * slot-ownership truth (ASCS normally rejects a duplicate
		 * Release PDU before this callback; this guard is the
		 * app-side idempotence boundary).  The check runs under
		 * the fixed lifecycle -> session lock nesting. */
		k_mutex_lock(&lifecycle_lock, K_FOREVER);
		bool configured = audio_stream_session_configured(slot);
		k_mutex_unlock(&lifecycle_lock);

		if (!configured) {
			return false;
		}

		bool was_open = teardown_close_path(false);

		if (was_open) {
			/* Stop the audio sink immediately on the release that
			 * closed an open gate, before returning to ASCS, so
			 * the sink oracle can finalize the segment.  Gate
			 * close + sink stop precede the release observer;
			 * the disconnect cleanup still fires later
			 * (rel_ss_seq < disc_seq). */
			LOG_INF("Release: audio sink stopped");
#if defined(CONFIG_BSIM_OBSERVER)
			bsim_observer_release_sink_stop();
#endif
		}

		/* Reset only this app slot after the admission close/drain
		 * above; the bt_bap_stream object is never touched.  The
		 * second Mode A slot still cleans up here even though the
		 * global gate is already closed. */
		audio_stream_session_release(slot);
		k_mutex_lock(&lifecycle_lock, K_FOREVER);
		stream_lifecycle_sink_release(slot);
		k_mutex_unlock(&lifecycle_lock);
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_cleanup_release(slot);
#endif
		return false;
	}

	case TEARDOWN_DISCONNECT:
		/* Normal global close once; lifecycle reset under the lock;
		 * session reset after the drain; stats reset once;
		 * disconnect-cleanup observer once.  The caller handles
		 * conn unref / default_conn; the advertising-restart sem
		 * decision comes from this return value. */
		teardown_close_path(false);
		k_mutex_lock(&lifecycle_lock, K_FOREVER);
		stream_lifecycle_reset();
		k_mutex_unlock(&lifecycle_lock);
		audio_stream_session_reset_all();
		audio_stats_reset();
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_cleanup_disconnect();
#endif
		return true;

	case TEARDOWN_FORCED:
		/* Shell stop: global forced close; sink then offload once on
		 * the first edge; no assembler/stats clear; the force latch
		 * stays latched for the current configured slot set. */
		teardown_close_path(true);
		return false;
	}
	return false;
}

static int lc3_disable(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Disable: stream %p", stream);
	teardown_transition(TEARDOWN_DISABLE, sink_idx(stream));
	return 0;
}

static int lc3_stop(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Stop: stream %p", stream);
	teardown_transition(TEARDOWN_CLOSE, sink_idx(stream));
	return 0;
}

static int lc3_release(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	LOG_INF("Release: stream %p", stream);
	teardown_transition(TEARDOWN_RELEASE, sink_idx(stream));
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

/* ── Data path adapter: ISO recv → session ─────────────────────── */

/*
 * stream_recv adapter (R6): decomposes the ISO callback into scalars and
 * forwards them to the audio stream session, which owns the whole
 * decode/conceal/volume/push path.  This adapter retains ONLY:
 *
 *   - the ISO_RECV perf wrap (start/end exactly once per callback);
 *   - the lifecycle gate snapshot (one read under lifecycle_lock);
 *   - the timing-reference update for stream 0 (valid + TS + gate open,
 *     using the session's stored presentation delay);
 *   - gate-independent valid-receive counting via the session counter
 *     (valid packets count and the periodic SDU log fires regardless
 *     of gate state);
 *   - the gate-closed throttle + bsim recv_gate_blocked observer event.
 */
static void stream_recv(struct bt_bap_stream *stream, const struct bt_iso_recv_info *info,
			struct net_buf *buf)
{
	uint32_t t0 = audio_perf_cycle_start();

	size_t idx = sink_idx(stream);
	const bool valid = (info->flags & BT_ISO_FLAGS_VALID) != 0;
	const bool has_ts = (info->flags & BT_ISO_FLAGS_TS) != 0;

	/* One gate snapshot under the lifecycle lock for the whole
	 * callback; the shell thread can force-close concurrently. */
	bool gate_open;

	k_mutex_lock(&lifecycle_lock, K_FOREVER);
	gate_open = stream_lifecycle_audio_path_is_open();
	k_mutex_unlock(&lifecycle_lock);

	/* Feed validated timestamp + presentation delay to
	 * hardware timing measurement (nRF54L15 GRTC path).  Only stream 0
	 * is used as the timing reference.  Gated behind audio-path-open
	 * to prevent late callbacks from re-arming hardware timers after
	 * teardown.  The presentation delay lives in the session.
	 *
	 * Drift compensation is per-block in audio_sink_push(), driven by
	 * PCLK feedforward + buffer-phase PI.  ISO timestamps go ONLY to
	 * audio_timing for GRTC scheduling.
	 */
	if (idx == 0 && valid && has_ts && gate_open) {
		audio_timing_sdu_ref_update(info->ts, audio_stream_session_pd(0));
	}

	if (valid) {
		/* Gate-independent counting/log: the session owns the
		 * counter, the adapter logs it. */
		size_t cnt = audio_stream_session_recv_valid_count(idx);
#if defined(CONFIG_INFO_REPORTING_INTERVAL) && CONFIG_INFO_REPORTING_INTERVAL > 0
		if ((cnt % CONFIG_INFO_REPORTING_INTERVAL) == 0U) {
			LOG_INF("Audio stream[%zu]: %zu SDU", idx, cnt);
		}
#else
		(void)cnt;
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
	if (!gate_open) {
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

	/* Decomposed scalar receive: the session copies only valid/has_ts/
	 * ts/seq_num/data/len and retains no ISO info or net_buf pointers.
	 * A concurrent close (shell) may reject it via session admission;
	 * the return value is informational. */
	audio_stream_session_recv(idx, valid, has_ts, info->ts, info->seq_num, buf->data, buf->len);

	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_ISO_RECV);
}

/* ── Stream ops ──────────────────────────────────────────────────── */

static void stream_stopped(struct bt_bap_stream *s, uint8_t reason)
{
	size_t idx = sink_idx(s);

	LOG_INF("Stream[%zu] stopped: reason 0x%02X", idx, reason);

	/* Normal close through the teardown coordinator.  The disabled
	 * callback may fire later and close again harmlessly (duplicate
	 * close has no global side effect). */
	teardown_transition(TEARDOWN_CLOSE, idx);
}

static void stream_started(struct bt_bap_stream *s)
{
	size_t idx = sink_idx(s);
	struct bt_iso_info info;

	bt_iso_chan_get_info(s->iso, &info);
	LOG_INF("Stream[%zu] started: CIG %u CIS %u", idx, info.unicast.cig_id,
		info.unicast.cis_id);
	audio_stream_session_recv_reset(idx);

	/* R1 open transition:
	 *  1. under lifecycle lock: run stream_lifecycle_sink_started();
	 *  2. on closed→open: bump/capture the transition generation and
	 *     call audio_sink_stream_open() under the fixed lifecycle→sink
	 *     lock order;
	 *  3. if sink open fails (-EIO), roll the lifecycle gate back to
	 *     closed, bump the generation again, keep sink admission closed,
	 *     and skip the open work (session receive admission is never
	 *     opened);
	 *  4. otherwise release the lock, run the existing open work outside
	 *     it, then recheck lifecycle-open + generation so a shell stop
	 *     that landed in between leaves final state closed (session
	 *     receive admission is closed again on the stale path). */
	bool gate_opened = false;
	uint32_t open_gen = 0;
	bool sink_open_failed = false;

	k_mutex_lock(&lifecycle_lock, K_FOREVER);
	gate_opened = stream_lifecycle_sink_started(idx);
	if (gate_opened) {
		audio_path_generation++;
		open_gen = audio_path_generation;
		if (audio_sink_stream_open() < 0) {
			stream_lifecycle_audio_path_close();
			audio_path_generation++;
			gate_opened = false;
			sink_open_failed = true;
		}
	}
	k_mutex_unlock(&lifecycle_lock);

	if (sink_open_failed) {
		LOG_ERR("Audio path open failed: sink admission unavailable");
		return;
	}

	if (gate_opened) {
		LOG_INF("Audio path gate OPEN (stream[%zu] completed the set)", idx);
		/* Clear any pending Mode A halves from the previous session,
		 * then open session receive admission at the exact safe
		 * point BEFORE data admission (recv callbacks flow only
		 * after this callback returns on the shared BT RX WQ). */
		audio_stream_session_start_clear();
		audio_stream_session_rx_open();

		/* Reset perf counters at start of new audio session.
		 * Metrics from previous session are discarded here; use
		 * 'audio perf' before gate opens to inspect completed-session data.
		 */
		audio_perf_reset();

		/* Start offload pipeline for new stream. */
		audio_offload_stream_start();

#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_gate_open();
#endif

		/* Recheck: a shell stop that ran while the open work executed
		 * (or a later ordinary close) invalidates this open; stop the
		 * offload and close session receive admission again so final
		 * state is closed. */
		bool stale_open = false;

		k_mutex_lock(&lifecycle_lock, K_FOREVER);
		if (!stream_lifecycle_audio_path_is_open() || audio_path_generation != open_gen) {
			stale_open = true;
		}
		k_mutex_unlock(&lifecycle_lock);

		if (stale_open) {
			audio_stream_session_rx_close();
			audio_offload_stream_stop();
		}
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

	/* The DISABLED coordinator event owns the whole completion —
	 * first close (gate + sink drain + offload), the exact summary
	 * snapshot/log, and the stats reset once.  The first close must
	 * run first so late callbacks on the other ASE cannot decode,
	 * push, or restart I2S after the gate closes. */
	teardown_transition(TEARDOWN_DISABLED, idx);
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

	/* After storing/refing the connection, translate the connect to
	 * the pairing-mode owner (forwarded only when notifications are
	 * enabled).  This callback performs no security request directly —
	 * the pairing-mode owner requests L2 in BONDING. */
#if defined(CONFIG_USER_PAIRING_CONTROL)
	bt_bap_pairing_adapter_notify_connected();
#endif

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

	/* The DISCONNECT coordinator event owns the whole teardown
	 * (normal global close once, lifecycle reset, session reset after
	 * the drain, stats reset once, cleanup observer once) and returns
	 * whether the advertising-restart semaphore must fire.  The
	 * callback keeps only the connection-object ownership (unref /
	 * default_conn); no direct low-level teardown calls. */
	bool wake = teardown_transition(TEARDOWN_DISCONNECT, 0);

	bt_conn_unref(default_conn);
	default_conn = NULL;

	/* After teardown and connection ownership cleanup, translate the
	 * disconnect to the pairing-mode owner exactly once (non-matching
	 * disconnects returned above).  The adapter forwards only when
	 * notifications are enabled; it never restarts advertising itself. */
#if defined(CONFIG_USER_PAIRING_CONTROL)
	bt_bap_pairing_adapter_notify_disconnected();
#endif

	/* Available contexts persist from initial registration.
	 * No restore needed — ACL disconnect does not alter the default. */
	if (wake) {
		k_sem_give(&sem_disconnected);
	}
}

#if defined(CONFIG_USER_PAIRING_CONTROL)
/*
 * P4 security-changed translation.  Success is err == BT_SECURITY_ERR_SUCCESS;
 * bonded is true only when the security change succeeded AND the peer has a
 * stored bond.  The adapter forwards both booleans when notifications are
 * enabled; the pairing-mode phase guard makes a duplicate completion a no-op.
 */
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	bool success = (err == BT_SECURITY_ERR_SUCCESS);
	bool bonded = false;

	if (success) {
		bonded = bt_le_bond_exists(BT_ID_DEFAULT, bt_conn_get_dst(conn));
	}
	LOG_INF("Security changed: level %u err %u bonded %d", level, err, bonded);
	bt_bap_pairing_adapter_notify_security_changed(success, bonded);
}
#endif /* CONFIG_USER_PAIRING_CONTROL */

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
#if defined(CONFIG_USER_PAIRING_CONTROL)
	.security_changed = security_changed,
#endif
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
	const bt_addr_le_t *addr = bt_conn_get_dst(conn);

	/* Runs on the BT RX workqueue (cooperative): pure policy check only,
	 * no HCI commands here.  The controller filter is the primary
	 * enforcement; this is defense in depth. */
	if (bt_pairing_policy_pairing_accept(&pairing_policy, addr) == BT_PAIRING_POLICY_REJECT) {
		char a[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(addr, a, sizeof(a));
		LOG_WRN("Pairing rejected (BONDED_ONLY): unbonded peer %s", a);
		return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
	}
	LOG_INF("Pairing accepted");
	return BT_SECURITY_ERR_SUCCESS;
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_INF("Pairing complete, bonded: %d", bonded);
	if (bonded) {
		/* Mark desired policy state only — no HCI from this
		 * callback.  The controller filter is rebuilt at the next
		 * advertising restart from thread context. */
		int err = bt_pairing_policy_mark_bonded(&pairing_policy, bt_conn_get_dst(conn));

		if (err) {
			LOG_WRN("Pairing policy full (%d): new bond not on the "
				"controller filter until the next rebuild",
				err);
		}
#if defined(CONFIG_USER_PAIRING_CONTROL)
		if (!bt_bap_pairing_adapter_notifications_enabled()) {
			/* Legacy behavior until P5 enables notifications: a
			 * completed bonded pairing selects BONDED_ONLY.  Once
			 * enabled, the desired mode is owned only by the P1
			 * operations.  set_mode cannot fail for a valid enum
			 * value. */
			(void)bt_pairing_policy_set_mode(&pairing_policy,
							 BT_PAIRING_POLICY_MODE_BONDED_ONLY);
		}
#else
		/* Legacy behavior (feature off): a completed bonded pairing
		 * selects BONDED_ONLY as the desired mode even when the
		 * inventory was already full.  The mode and inventory are
		 * separate state.  set_mode cannot fail for a valid enum
		 * value. */
		(void)bt_pairing_policy_set_mode(&pairing_policy,
						 BT_PAIRING_POLICY_MODE_BONDED_ONLY);
#endif
	}
#if defined(CONFIG_USER_PAIRING_CONTROL)
	/* Mark happened above; notify bonded/unbonded honestly.  The
	 * adapter forwards only when notifications are enabled. */
	bt_bap_pairing_adapter_notify_pairing_complete(bonded);
#endif
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_WRN("Pairing failed: %d", reason);
#if defined(CONFIG_USER_PAIRING_CONTROL)
	/* Remote pairing failure is a normal outcome: forward to the
	 * pairing-mode owner (gate-checked); the controller stays BONDING. */
	bt_bap_pairing_adapter_notify_pairing_failed();
#endif
}

static struct bt_conn_auth_info_cb conn_auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static struct bt_conn_auth_cb conn_auth_cb = {
	.pairing_accept = pairing_accept,
};

/* ── pairing-mode filter / advertising restart helpers ────────────── */

struct bond_collector {
	bt_addr_le_t addrs[BT_PAIRING_POLICY_MAX_ENTRIES];
	size_t count;
};

static void collect_bond(const struct bt_bond_info *info, void *user_data)
{
	struct bond_collector *c = user_data;

	/* Deleted identities surface as BT_ADDR_LE_ANY (all-zero address);
	 * they can never enter the controller filter and are not stored. */
	if (bt_addr_le_cmp(&info->addr, BT_ADDR_LE_ANY) == 0) {
		return;
	}
	if (c->count < BT_PAIRING_POLICY_MAX_ENTRIES) {
		bt_addr_le_copy(&c->addrs[c->count], &info->addr);
		c->count++;
	}
}

static void find_live_peer(struct bt_conn *conn, void *data)
{
	struct bt_conn **peer = data;
	struct bt_conn_info info;

	if (*peer == NULL && bt_conn_get_info(conn, &info) == 0 &&
	    (info.state == BT_CONN_STATE_CONNECTED || info.state == BT_CONN_STATE_DISCONNECTING)) {
		/* CONNECTED and DISCONNECTING both count as an active connection:
		 * restarting advertising while the controller tears down a link
		 * fails (ENOMEM), so the OPEN restart is deferred to the main loop
		 * when either state is present. */
		*peer = bt_conn_ref(conn);
	}
}

/*
 * Shared advertising-rebuild primitives (P4).  Each assumes
 * pairing_adv_lock is held; the legacy feature-off restart and the P4
 * adapter both serialize the controller filter / advertising-set state
 * under that lock.
 */

static int bt_bap_adv_stop_locked(void)
{
	int err = bt_le_ext_adv_stop(adv);

	if (err) {
		LOG_ERR("Adv stop failed: %d", err);
	}
	return err;
}

static int bt_bap_adv_fal_clear_locked(void)
{
	int err = bt_le_filter_accept_list_clear();

	if (err) {
		LOG_ERR("Filter accept list clear failed: %d", err);
	}
	return err;
}

/* Enumerate persisted bonds and rebuild the policy inventory.  The
 * collector is filled so the legacy caller can derive the mode from the
 * count; the P4 adapter operations never derive mode. */
static int bt_bap_adv_bonds_replace_locked(struct bond_collector *collector)
{
	int err;

	memset(collector, 0, sizeof(*collector));
	bt_foreach_bond(BT_ID_DEFAULT, collect_bond, collector);
	err = bt_pairing_policy_replace_bonds(&pairing_policy, collector->addrs, collector->count);
	if (err) {
		LOG_ERR("Pairing policy rebuild failed: %d (%zu bonds)", err, collector->count);
	}
	return err;
}

/*
 * Populate the extended-advertising parameters per the current policy
 * snapshot and rebuild the controller filter when the snapshot mode is
 * BONDED_ONLY (FILTER_CONN is set even with zero entries).  BONDING/OPEN
 * leaves connection filtering off while the inventory stays preserved.
 */
static int bt_bap_adv_filter_locked(struct bt_le_adv_param *param)
{
	struct bt_pairing_policy_snapshot snap;
	int err;

	/* One atomic snapshot: repeated get_mode/get_entry accessor calls could
	 * interleave with pairing_complete()'s mark_bonded on the RX workqueue
	 * and yield a torn mode/entries view during the filter rebuild. */
	bt_pairing_policy_snapshot(&pairing_policy, &snap);
	if (snap.mode != BT_PAIRING_POLICY_MODE_BONDED_ONLY) {
		return 0;
	}
	param->options |= BT_LE_ADV_OPT_FILTER_CONN;
	for (size_t i = 0; i < snap.count; i++) {
		err = bt_le_filter_accept_list_add(&snap.entries[i]);
		if (err) {
			LOG_ERR("Filter accept list add failed: %d", err);
			return err;
		}
	}
	return 0;
}

/* Filter rebuild + parameter update + start (lock held). */
static int bt_bap_adv_filter_start_locked(void)
{
	struct bt_le_adv_param param = *BT_BAP_ADV_PARAM_CONN_QUICK;
	int err;

	err = bt_bap_adv_filter_locked(&param);
	if (err) {
		return err;
	}
	err = bt_le_ext_adv_update_param(adv, &param);
	if (err) {
		LOG_ERR("Adv param update failed: %d", err);
		return err;
	}
	err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		LOG_ERR("Adv start failed: %d", err);
	}
	return err;
}

/*
 * Legacy feature-off advertising restart, assuming pairing_adv_lock is
 * held.  Sequence:
 *   1. stop active advertising (already-stopped is benign, no warning);
 *   2. clear the controller filter accept list while no role uses it;
 *   3. enumerate persisted bonds and rebuild the policy snapshot;
 *   4. select OPEN or BONDED_ONLY parameters (filter connections only,
 *      scan responses stay visible);
 *   5. update extended-advertising parameters while stopped;
 *   6. start advertising.
 * Every failure propagates — no step reports false success.
 *
 * The OPEN/BONDED_ONLY mode derivation from the bond count is isolated
 * here (legacy main-loop ownership, feature off); the P4 adapter
 * operations never derive mode.  P5 routes the main loop through the
 * pairing-mode owner and removes this derivation.
 */
static int bt_bap_restart_advertising_locked(void)
{
	struct bond_collector collector;
	int err;

	err = bt_bap_adv_stop_locked();
	if (err) {
		return err;
	}
	err = bt_bap_adv_fal_clear_locked();
	if (err) {
		return err;
	}
	err = bt_bap_adv_bonds_replace_locked(&collector);
	if (err) {
		return err;
	}
	/* Legacy derivation: any persisted bond selects BONDED_ONLY. */
	err = bt_pairing_policy_set_mode(&pairing_policy,
					 (collector.count > 0) ? BT_PAIRING_POLICY_MODE_BONDED_ONLY
							       : BT_PAIRING_POLICY_MODE_OPEN);
	if (err) {
		LOG_ERR("Pairing policy mode select failed: %d (%zu bonds)", err, collector.count);
		return err;
	}
	return bt_bap_adv_filter_start_locked();
}

/* ── public API ───────────────────────────────────────────────────── */

#if defined(CONFIG_USER_PAIRING_CONTROL)
/* Backend table defined at the end of this TU (with the P4 adapter block);
 * declared here so bt_bap_init() can install it. */
static const struct bt_bap_pairing_backend bt_bap_pairing_backend;
#endif

int bt_bap_init(void)
{
	bt_pairing_policy_init(&pairing_policy);
	if (audio_stream_session_init() != 0) {
		LOG_ERR("Audio stream session init failed");
		return -EINVAL;
	}

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
		bt_bap_stream_cb_register(&sinks[i], &stream_ops);
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

#if defined(CONFIG_USER_PAIRING_CONTROL)
	/* Install the immutable pairing adapter backend after the
	 * advertising set exists; applied access starts SUSPENDED.  Failure
	 * propagates as a boot/BAP init failure. */
	err = bt_bap_pairing_adapter_init(&bt_bap_pairing_backend, NULL);
	if (err) {
		LOG_ERR("Pairing adapter init failed: %d", err);
		return err;
	}
#endif

	return 0;
}

int bt_bap_restart_advertising(void)
{
	int err;

	k_mutex_lock(&pairing_adv_lock, K_FOREVER);
	err = bt_bap_restart_advertising_locked();
	k_mutex_unlock(&pairing_adv_lock);
	return err;
}

void bt_bap_wait_disconnect(void)
{
	k_sem_take(&sem_disconnected, K_FOREVER);
}

int bt_bap_pairing_reset(void)
{
	struct bt_conn *peer = NULL;
	struct bt_conn_info info;
	bool conn_active;
	int err;
	int rc = 0;

	/* Desired state -> OPEN; the controller filter is cleared at the
	 * next advertising restart (or by the immediate restart below).
	 * Both policy calls run under pairing_adv_lock so they cannot
	 * interleave with an in-flight advertising-restart rebuild of the
	 * filter.  Mode and inventory are set as two explicit independent
	 * calls: set_mode cannot fail for a valid enum value.  P4 replaces
	 * this old transition path with the pairing-mode owner. */
	k_mutex_lock(&pairing_adv_lock, K_FOREVER);
	(void)bt_pairing_policy_set_mode(&pairing_policy, BT_PAIRING_POLICY_MODE_OPEN);
	bt_pairing_policy_clear_bonds(&pairing_policy);
	k_mutex_unlock(&pairing_adv_lock);

	/* Clear all persisted bonds.  bt_unpair() also disconnects any
	 * connected bonded peer (REMOTE_USER_TERM_CONN). */
	err = bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
	if (err) {
		LOG_ERR("Pairing reset: bt_unpair failed: %d", err);
		rc = err;
	}

	/* Any live connection (CONNECTED or already DISCONNECTING) means the
	 * main loop will perform the OPEN advertising restart once the link
	 * is gone; restarting now would race the controller teardown.  A
	 * connected but unbonded peer (OPEN-mode edge case) is disconnected
	 * here. */
	bt_conn_foreach(BT_CONN_TYPE_LE, find_live_peer, &peer);
	if (peer != NULL) {
		conn_active = true;
		if (bt_conn_get_info(peer, &info) == 0 && info.state == BT_CONN_STATE_CONNECTED) {
			err = bt_conn_disconnect(peer, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			if (err) {
				LOG_ERR("Pairing reset: disconnect failed: %d", err);
				if (rc == 0) {
					rc = err;
				}
			}
		}
		bt_conn_unref(peer);
	} else {
		conn_active = false;
	}

	/* No connection at all: restart advertising in OPEN mode now. */
	if (!conn_active) {
		k_mutex_lock(&pairing_adv_lock, K_FOREVER);
		err = bt_bap_restart_advertising_locked();
		k_mutex_unlock(&pairing_adv_lock);
		if (err) {
			LOG_ERR("Pairing reset: advertising restart failed: %d", err);
			if (rc == 0) {
				rc = err;
			}
		}
	}

	return rc;
}

void bt_bap_audio_path_stop(void)
{
	/* R1 shell stop order, now owned by the R7 coordinator:
	 *   1. force-close the lifecycle gate and sink push admission
	 *      (forced close latches the gate: stream-started callbacks
	 *      cannot reopen this configured slot set);
	 *   2. audio_sink_stop() drains every admitted push, then DROPs;
	 *   3. audio_offload_stream_stop() only after the drain, so an
	 *      admitted push can never race the offload reset.
	 * The shell thread never clears/resets Mode A, decoder, sequence,
	 * or stats state.  On an already-closed gate the close primitive
	 * is a no-op for the sink/offload stop as well. */
	teardown_transition(TEARDOWN_FORCED, 0);
}

#if defined(CONFIG_USER_PAIRING_CONTROL)

/* ── P4 pairing adapter backend (concrete Zephyr Bluetooth side) ─────
 *
 * The adapter (bt_bap_pairing_adapter.c) owns the operation mechanics,
 * the applied access state, and the callback-event translation gate;
 * every Bluetooth call below stays in bt_bap.c.  The advertising steps
 * run under pairing_adv_lock via the adv_locked runner; the peer
 * operations take and release their own connection refs and never read
 * default_conn from controller work without synchronization. */

static int bt_bap_pairing_adv_locked(int (*fn)(void *ctx), void *ctx)
{
	int ret;

	k_mutex_lock(&pairing_adv_lock, K_FOREVER);
	ret = fn(ctx);
	k_mutex_unlock(&pairing_adv_lock);
	return ret;
}

/* Step slots: pairing_adv_lock is already held (adv_locked runner). */

static int bt_bap_pairing_step_adv_stop(void *ctx)
{
	return bt_bap_adv_stop_locked();
}

static int bt_bap_pairing_step_fal_clear(void *ctx)
{
	return bt_bap_adv_fal_clear_locked();
}

static int bt_bap_pairing_step_bonds_replace(void *ctx)
{
	struct bond_collector collector;

	/* Enumeration + inventory replacement only; the adapter never
	 * derives the desired mode from the count. */
	return bt_bap_adv_bonds_replace_locked(&collector);
}

static void bt_bap_pairing_step_policy_snapshot(struct bt_bap_pairing_policy_snap *snap, void *ctx)
{
	struct bt_pairing_policy_snapshot s;

	bt_pairing_policy_snapshot(&pairing_policy, &s);
	snap->mode = s.mode;
	snap->count = s.count;
}

/* Parameter update + controller filter rebuild per the adapter's filter
 * decision (FILTER_CONN is set even with zero inventory entries). */
static int bt_bap_pairing_step_adv_update_param(const struct bt_bap_pairing_adv_req *req, void *ctx)
{
	struct bt_pairing_policy_snapshot snap;
	struct bt_le_adv_param param = *BT_BAP_ADV_PARAM_CONN_QUICK;
	int err;

	if (req->filter_connections) {
		param.options |= BT_LE_ADV_OPT_FILTER_CONN;
		bt_pairing_policy_snapshot(&pairing_policy, &snap);
		for (size_t i = 0; i < snap.count; i++) {
			err = bt_le_filter_accept_list_add(&snap.entries[i]);
			if (err) {
				LOG_ERR("Filter accept list add failed: %d", err);
				return err;
			}
		}
	}
	err = bt_le_ext_adv_update_param(adv, &param);
	if (err) {
		LOG_ERR("Adv param update failed: %d", err);
	}
	return err;
}

static int bt_bap_pairing_step_adv_start(void *ctx)
{
	int err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);

	if (err) {
		LOG_ERR("Adv start failed: %d", err);
	}
	return err;
}

/* Policy mutation (pure spinlock-protected calls, no advertising lock). */

static int bt_bap_pairing_impl_policy_set_mode(enum bt_pairing_policy_mode mode, void *ctx)
{
	int err = bt_pairing_policy_set_mode(&pairing_policy, mode);

	if (err) {
		LOG_ERR("Pairing policy mode select failed: %d", err);
	}
	return err;
}

static void bt_bap_pairing_impl_policy_clear(void *ctx)
{
	/* Inventory clear only; the desired mode is preserved (P1 remains
	 * SUSPENDED until the reset feedback ends). */
	bt_pairing_policy_clear_bonds(&pairing_policy);
}

/* Peer operations.  Each obtains exactly one owned ref via
 * find_live_peer()/bt_conn_foreach(), never reads default_conn from
 * controller work without synchronization, and always releases the ref
 * before returning. */

static int bt_bap_pairing_impl_peer_disconnect(struct bt_bap_pairing_peer_result *result, void *ctx)
{
	struct bt_conn *peer = NULL;
	struct bt_conn_info info;
	int err;

	bt_conn_foreach(BT_CONN_TYPE_LE, find_live_peer, &peer);
	if (peer == NULL) {
		result->state = BT_BAP_PAIRING_PEER_ABSENT;
		result->err = 0;
		return 0;
	}

	err = bt_conn_get_info(peer, &info);
	if (err) {
		result->state = BT_BAP_PAIRING_PEER_OTHER;
		result->err = err;
		bt_conn_unref(peer);
		return err;
	}

	if (info.state == BT_CONN_STATE_CONNECTED) {
		err = bt_conn_disconnect(peer, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		result->state = BT_BAP_PAIRING_PEER_CONNECTED;
		result->err = err;
		bt_conn_unref(peer);
		return err;
	}

	/* DISCONNECTING: no duplicate command; the adapter reports pending. */
	result->state = BT_BAP_PAIRING_PEER_DISCONNECTING;
	result->err = 0;
	bt_conn_unref(peer);
	return 0;
}

static int bt_bap_pairing_impl_peer_security(struct bt_bap_pairing_peer_result *result, void *ctx)
{
	struct bt_conn *peer = NULL;
	struct bt_conn_info info;
	int err;

	bt_conn_foreach(BT_CONN_TYPE_LE, find_live_peer, &peer);
	if (peer == NULL) {
		result->state = BT_BAP_PAIRING_PEER_ABSENT;
		result->err = 0;
		return 0;
	}

	err = bt_conn_get_info(peer, &info);
	if (err) {
		result->state = BT_BAP_PAIRING_PEER_OTHER;
		result->err = err;
		bt_conn_unref(peer);
		return err;
	}

	if (info.state != BT_CONN_STATE_CONNECTED) {
		result->state = BT_BAP_PAIRING_PEER_DISCONNECTING;
		result->err = 0;
		bt_conn_unref(peer);
		return 0;
	}

	err = bt_conn_set_security(peer, BT_SECURITY_L2);
	result->state = BT_BAP_PAIRING_PEER_CONNECTED;
	result->err = err;
	bt_conn_unref(peer);
	return err;
}

/* Storage deletion (bt_unpair with BT_ADDR_LE_ANY returns 0 with zero
 * bonds, deletes every stored bond, and disconnects bonded peers — P1
 * disconnects first, so this never duplicates connection ownership). */

static int bt_bap_pairing_impl_storage_delete(void *ctx)
{
	int err = bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);

	if (err) {
		LOG_ERR("Pairing reset: bt_unpair failed: %d", err);
	}
	return err;
}

/* Pairing-mode notification enqueue (P1 API; controller RX-context
 * safe).  Returns 0 or -ECANCELED after a fatal; unexpected returns are
 * logged by the adapter, never retried. */

static int bt_bap_pairing_impl_notify_connected(void *ctx)
{
	return pairing_mode_notify_connected();
}

static int bt_bap_pairing_impl_notify_disconnected(void *ctx)
{
	return pairing_mode_notify_disconnected();
}

static int bt_bap_pairing_impl_notify_pairing_complete(bool bonded, void *ctx)
{
	return pairing_mode_notify_pairing_complete(bonded);
}

static int bt_bap_pairing_impl_notify_pairing_failed(void *ctx)
{
	return pairing_mode_notify_pairing_failed();
}

static int bt_bap_pairing_impl_notify_security_changed(bool success, bool bonded, void *ctx)
{
	return pairing_mode_notify_security_changed(success, bonded);
}

static const struct bt_bap_pairing_backend bt_bap_pairing_backend = {
	.adv_locked = bt_bap_pairing_adv_locked,
	.adv_stop = bt_bap_pairing_step_adv_stop,
	.fal_clear = bt_bap_pairing_step_fal_clear,
	.bonds_replace = bt_bap_pairing_step_bonds_replace,
	.policy_snapshot = bt_bap_pairing_step_policy_snapshot,
	.adv_update_param = bt_bap_pairing_step_adv_update_param,
	.adv_start = bt_bap_pairing_step_adv_start,
	.policy_set_mode = bt_bap_pairing_impl_policy_set_mode,
	.policy_clear = bt_bap_pairing_impl_policy_clear,
	.peer_disconnect = bt_bap_pairing_impl_peer_disconnect,
	.peer_security = bt_bap_pairing_impl_peer_security,
	.storage_delete = bt_bap_pairing_impl_storage_delete,
	.notify_connected = bt_bap_pairing_impl_notify_connected,
	.notify_disconnected = bt_bap_pairing_impl_notify_disconnected,
	.notify_pairing_complete = bt_bap_pairing_impl_notify_pairing_complete,
	.notify_pairing_failed = bt_bap_pairing_impl_notify_pairing_failed,
	.notify_security_changed = bt_bap_pairing_impl_notify_security_changed,
};

/* ── P4 public production surface (thin adapter calls) ────────────── */

int bt_bap_pairing_set_access_mode(enum pairing_access_mode mode, void *ctx)
{
	return bt_bap_pairing_adapter_set_access_mode(mode, ctx);
}

int bt_bap_pairing_advertising_suspend(void *ctx)
{
	return bt_bap_pairing_adapter_advertising_suspend(ctx);
}

int bt_bap_pairing_advertising_start(void *ctx)
{
	return bt_bap_pairing_adapter_advertising_start(ctx);
}

int bt_bap_pairing_disconnect_peer(bool *pending, void *ctx)
{
	return bt_bap_pairing_adapter_disconnect_peer(pending, ctx);
}

int bt_bap_pairing_delete_all_bonds(void *ctx)
{
	return bt_bap_pairing_adapter_delete_all_bonds(ctx);
}

int bt_bap_pairing_request_security(void *ctx)
{
	return bt_bap_pairing_adapter_request_security(ctx);
}

void bt_bap_pairing_notifications_enable(void)
{
	bt_bap_pairing_adapter_notifications_enable();
}

#endif /* CONFIG_USER_PAIRING_CONTROL */
