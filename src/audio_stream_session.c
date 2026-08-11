/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * App-owned BAP sink receive/session state — implementation (R6).
 *
 * See audio_stream_session.h for the contract.  This module is the
 * exclusive owner of the app audio receive path extracted from bt_bap.c:
 * validated codec shape, decoder contexts, per-CIS sequence trackers, the
 * shared Mode A assembler, receive counters, presentation delay, and the
 * decode/conceal/volume/push mechanics.  It never touches Bluetooth stack
 * objects (struct bt_bap_stream / conn / ep / codec_cfg / qos / iso).
 *
 * Receive lease discipline: recv() acquires a lease under the session
 * mutex (rejecting when admission is closed / slot invalid), runs the
 * decode path OUTSIDE the lock, then releases the lease and signals the
 * drain condvar.  Teardown (rx_close()) closes admission, bumps the
 * generation, and waits for all admitted leases to drain before the caller
 * resets decoder/assembler/sequence state.  The mutex is never held across
 * decode, volume, sink push, logging, or Bluetooth calls; no session API
 * is ever called from ISR.
 */

#include "audio_stream_session.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#include "audio_sink.h"
#include "audio_decode.h"
#include "audio_modea.h"
#include "audio_iso_seq.h"
#include "audio_stats.h"
#include "audio_perf.h"
#include "audio_volume.h"

#if defined(CONFIG_LIBLC3)
#include "lc3.h"
#endif

#if defined(CONFIG_BSIM_OBSERVER)
#include "bsim_observer.h"
#endif

LOG_MODULE_REGISTER(audio_stream_session, LOG_LEVEL_INF);

BUILD_ASSERT(AUDIO_STREAM_SESSION_MAX_SLOTS == 2,
	     "session slot count must match the fixed two-slot lifecycle gate");

#if defined(CONFIG_LIBLC3)
#define SESSION_SAMPLES_PER_CHANNEL_MAX 480 /* 48 kHz × 10 ms */
#define SESSION_STEREO_OUT_MAX          (SESSION_SAMPLES_PER_CHANNEL_MAX * 2)
#endif

struct audio_stream_slot {
	bool configured;
	struct audio_stream_codec_shape shape;
	uint32_t pd_us;
	size_t recv_cnt;
	struct audio_decode_ctx decode;

	/* Per-CIS HCI packet-sequence tracker (audio_iso_seq.c): detects
	 * emitted HCI SDUs omitted after controller sequencing
	 * (controller-to-host or host-side loss), which appear as a jump
	 * in the delivered packet sequence number, so they can be
	 * concealed as PLC before the current SDU is processed. */
	struct audio_iso_seq seq;

	/* Per-CIS ISO timestamp-cadence tracker (audio_iso_seq.c): detects
	 * the same omitted output events from delivered ISO timestamps
	 * when a controller-side radio event emits no HCI SDU (SW Split
	 * advances its session sequence number only on emitted SDUs, so
	 * those omissions stay invisible to @p seq — FR4 mono: 8876
	 * callbacks, zero sequence gaps, 225 I2S restarts).  Mono and
	 * Mode B merge the two evidence sources with MAX; Mode A keeps the
	 * sequence-only sentinel path. */
	struct audio_iso_cadence cadence;
};

struct audio_stream_session_data {
	struct audio_stream_slot slots[AUDIO_STREAM_SESSION_MAX_SLOTS];
	size_t configured_count;

#if defined(CONFIG_LIBLC3)
	/* Mode A pair identity lives in the shared event assembler
	 * (audio_modea.c): bounded per-channel pending compressed halves +
	 * ISO SDU reference-time pairing.  One event buffer (decoded
	 * immediately at resolution, before the next store) plus
	 * per-channel queues and the L/R/stereo decode buffers. */
	struct modea_state modea;
	struct modea_event modea_ev;
	int16_t l_buf[SESSION_SAMPLES_PER_CHANNEL_MAX];
	int16_t r_buf[SESSION_SAMPLES_PER_CHANNEL_MAX];
	int16_t stereo_out[SESSION_STEREO_OUT_MAX];
#endif

	/* R6 receive lease state: admission/generation/in-flight.  The
	 * kernel primitives live OUTSIDE this struct so init can memset
	 * the data without wiping the mutex/condvar. */
	bool admission_open; /* only rx_open() enables; rx_close() closes */
	uint32_t generation; /* bumped on every rx_close */
	size_t in_flight;
};

/* The mutex guards only admission/generation/in-flight; never decode or
 * sink work.  Thread-context only (BT RX WQ + shell), never ISR. */
static K_MUTEX_DEFINE(session_mutex);
static K_CONDVAR_DEFINE(session_drained);
static struct audio_stream_session_data s;

static struct audio_stream_slot *slot_at(size_t idx)
{
	if (idx >= AUDIO_STREAM_SESSION_MAX_SLOTS) {
		return NULL;
	}
	return &s.slots[idx];
}

/* ── public init / config / qos / enable / disable ───────────────── */

/**
 * Initialize the session: zero all slot/assembler state, configured_count,
 * and leave receive admission CLOSED (only rx_open() enables it).
 * Thread-context; called once from bt_bap_init().
 *
 * @retval 0 on success
 */
int audio_stream_session_init(void)
{
	k_mutex_lock(&session_mutex, K_FOREVER);

	memset(&s, 0, sizeof(s));

	k_mutex_unlock(&session_mutex);
	return 0;
}

int audio_stream_session_config(size_t idx, const struct audio_stream_codec_shape *shape)
{
	if (shape == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&session_mutex, K_FOREVER);

	struct audio_stream_slot *sl = slot_at(idx);

	if (sl == NULL) {
		k_mutex_unlock(&session_mutex);
		return -EINVAL;
	}

	sl->configured = true;
	sl->shape = *shape;
	sl->pd_us = 0U;
	sl->recv_cnt = 0U;
	audio_decode_reset(&sl->decode);
	audio_iso_seq_reset(&sl->seq);
	audio_iso_cadence_reset(&sl->cadence);
	s.configured_count++;

	/* Admission stays closed: only rx_open() at the gate-open edge
	 * enables receive (LIFE-006). */
	k_mutex_unlock(&session_mutex);
	return 0;
}

int audio_stream_session_qos(size_t idx, uint32_t pd_us)
{
	k_mutex_lock(&session_mutex, K_FOREVER);

	struct audio_stream_slot *sl = slot_at(idx);

	if (sl == NULL || !sl->configured) {
		k_mutex_unlock(&session_mutex);
		return -EINVAL;
	}
	sl->pd_us = pd_us;
	k_mutex_unlock(&session_mutex);
	return 0;
}

int audio_stream_session_enable(size_t idx)
{
	k_mutex_lock(&session_mutex, K_FOREVER);

	struct audio_stream_slot *sl = slot_at(idx);

	if (sl == NULL || !sl->configured) {
		k_mutex_unlock(&session_mutex);
		return -EINVAL;
	}
	const struct audio_stream_codec_shape shape = sl->shape;

	k_mutex_unlock(&session_mutex);

#if defined(CONFIG_LIBLC3)
	int ret = audio_decode_config(&sl->decode, shape.chan_count, shape.freq_hz,
				      shape.frame_dur_us, shape.frame_blocks_per_sdu);

	if (ret < 0) {
		LOG_ERR("LC3 decoder setup failed (freq=%d dur=%d ch=%d)", shape.freq_hz,
			shape.frame_dur_us, shape.chan_count);
		return -EINVAL;
	}
	LOG_INF("LC3 decoder[%zu]: %d Hz %d us ch=%d", idx, shape.freq_hz, shape.frame_dur_us,
		shape.chan_count);

	/* Mode A assembler: predicted LOST-event positions advance by the
	 * SDU interval (frame duration).  Configured once per stream shape
	 * (both sinks share the same interval; a Mode A stream has two
	 * mono ASEs so this runs for each). */
	modea_config(&s.modea, shape.frame_dur_us);

	/* Tell the audio sink the expected stereo frames per push
	 * (depends on frame duration: 360 for 7.5 ms, 480 for 10 ms). */
	audio_sink_set_input_frames((uint16_t)sl->decode.samples_per_ch);
#endif /* CONFIG_LIBLC3 */

	return 0;
}

void audio_stream_session_disable(size_t idx)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	struct audio_stream_slot *sl = slot_at(idx);

	if (sl != NULL) {
#if defined(CONFIG_LIBLC3)
		sl->decode.decoder = NULL;
		sl->decode.decoder_r = NULL;
#endif
	}
	k_mutex_unlock(&session_mutex);
}

/* ── assembler / release / reset ─────────────────────────────────── */

static void session_start_clear_locked(void)
{
#if defined(CONFIG_LIBLC3)
	modea_reset(&s.modea);

	/* Per-CIS sequence trackers reset with the assembler: the next
	 * stream's first delivered callback re-bases instead of
	 * misreading a gap across stream boundaries.  The per-CIS
	 * timestamp-cadence trackers reset with them so no cadence gap
	 * can cross a session boundary either. */
	for (size_t i = 0; i < AUDIO_STREAM_SESSION_MAX_SLOTS; i++) {
		audio_iso_seq_reset(&s.slots[i].seq);
		audio_iso_cadence_reset(&s.slots[i].cadence);
	}
#endif /* CONFIG_LIBLC3 */
}

void audio_stream_session_start_clear(void)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	session_start_clear_locked();
	k_mutex_unlock(&session_mutex);
}

void audio_stream_session_release(size_t idx)
{
	k_mutex_lock(&session_mutex, K_FOREVER);

	struct audio_stream_slot *sl = slot_at(idx);

	if (sl == NULL || !sl->configured) {
		k_mutex_unlock(&session_mutex);
		return;
	}
#if defined(CONFIG_LIBLC3)
	audio_decode_reset(&sl->decode);
	audio_iso_seq_reset(&sl->seq);
	audio_iso_cadence_reset(&sl->cadence);
#endif
	sl->recv_cnt = 0U;
	sl->pd_us = 0U;
	sl->configured = false;
	memset(&sl->shape, 0, sizeof(sl->shape));
	if (s.configured_count > 0U) {
		s.configured_count--;
	}
	k_mutex_unlock(&session_mutex);
}

void audio_stream_session_reset_all(void)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	/* The caller drained every admitted lease via rx_close() first. */
	__ASSERT(s.in_flight == 0U, "reset_all with in-flight receive leases");

	for (size_t i = 0; i < AUDIO_STREAM_SESSION_MAX_SLOTS; i++) {
#if defined(CONFIG_LIBLC3)
		audio_decode_reset(&s.slots[i].decode);
		audio_iso_seq_reset(&s.slots[i].seq);
		audio_iso_cadence_reset(&s.slots[i].cadence);
#endif
		s.slots[i].configured = false;
		memset(&s.slots[i].shape, 0, sizeof(s.slots[i].shape));
		s.slots[i].pd_us = 0U;
		s.slots[i].recv_cnt = 0U;
	}
	s.configured_count = 0U;
	session_start_clear_locked();
	k_mutex_unlock(&session_mutex);
}

/* ── receive admission ───────────────────────────────────────────── */

void audio_stream_session_rx_open(void)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	s.admission_open = true;
	k_mutex_unlock(&session_mutex);
}

void audio_stream_session_rx_close(void)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	s.admission_open = false;
	s.generation++;
	while (s.in_flight > 0U) {
		k_condvar_wait(&session_drained, &session_mutex, K_FOREVER);
	}
	k_mutex_unlock(&session_mutex);
}

/* ── data path: decode → conceal → volume → push ─────────────────── */

#if defined(CONFIG_LIBLC3)

/*
 * Conceal one omitted SDU through the production decode helper: PLC
 * decode with the configured byte shape, volume, then one sink push.
 * Mirrors the valid-packet path exactly (Mode B splits per channel; mono
 * duplicates to stereo), so PLC accounting and the configured-shape
 * decoder octets stay identical to a real LOST callback.  A negative
 * decode return skips volume/push and counts the existing decode-error
 * evidence.
 */
static void session_mode_plc_push(size_t idx, struct audio_stream_slot *sl)
{
	size_t len = (size_t)sl->shape.octets_per_frame * sl->shape.frame_blocks_per_sdu;

	if (sl->decode.chan_count == 2) {
		len *= 2U; /* Mode B: [L frame][R frame] per block */
	}
	int ret = audio_decode_sdu(&sl->decode, NULL, len, false, s.stereo_out);

	if (ret < 0) {
		LOG_WRN("stream[%zu]: PLC decode failed %d — skipping volume/push", idx, ret);
		return;
	}
	audio_volume_apply(s.stereo_out, sl->decode.samples_per_ch * 2);
#if defined(CONFIG_BSIM_OBSERVER)
	/* A concealed push is never source-valid (neither half had VALID). */
	bsim_observer_pre_push(false, false);
#endif
	if (audio_sink_push(s.stereo_out, sl->decode.samples_per_ch * 2) < 0) {
		audio_perf_push_failure();
	}
}

/*
 * Mode A: feed one half (a real SDU or a synthetic LOST sentinel for an
 * omitted callback) into the event assembler and process every resolved
 * event through the shared decode/interleave/volume/push path.  At most
 * one event is resolved per store; the caller repeats the call for each
 * omitted sentinel and once for the current half.  Synthetic sentinels
 * use the exact LOST-callback shape (no data, no TS, source-invalid), so
 * the assembler's predicted-position pairing and per-channel PLC apply
 * unchanged and no channel's decoder is ever advanced past a missing
 * event before its concealment.
 */
static void session_mode_a_store_and_process(enum modea_channel ch, const uint8_t *data, size_t len,
					     bool src_valid, bool has_ts, uint32_t ts, uint16_t seq)
{
	enum modea_action act =
		modea_store(&s.modea, ch, data, len, src_valid, has_ts, ts, seq, &s.modea_ev);

	if (act == MODEA_ACTION_DROP) {
		struct modea_stats st;

		modea_get_stats(&s.modea, &st);
		LOG_INF("Mode A: half dropped (overflow=%u rejects=%u)", st.overflow_drops,
			st.rejects);
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_stale_half();
#endif
		return;
	}
	if (act != MODEA_ACTION_EMIT) {
		return;
	}

	/* Both sink decoders must exist to render the resolved event
	 * (the emit may decode the OTHER channel's pending half). */
	if (s.slots[0].decode.decoder == NULL || s.slots[1].decode.decoder == NULL) {
		LOG_WRN("Mode A: decoder not ready for resolved event");
		return;
	}

	/*
	 * Decode each half with ITS OWN channel's decoder, in channel
	 * order (both halves belong to the same CIG event, so both
	 * decoders advance exactly one event).  A missing half is decoded
	 * as PLC (NULL input, same octets as the event's carried half).  A
	 * hard decode error on either half skips the unsafe output and
	 * counts the existing decode-error evidence.
	 */
	bool decoded_ok = true;

	for (int ch_i = 0; ch_i < MODEA_CHANNELS; ch_i++) {
		const bool have = s.modea_ev.half_valid[ch_i];
		const size_t octets = have ? s.modea_ev.len[ch_i] : s.modea_ev.len[1 - ch_i];
		int16_t *dest = (ch_i == MODEA_CH_LEFT) ? s.l_buf : s.r_buf;
		uint32_t t1 = audio_perf_cycle_start();
		const int err = lc3_decode(s.slots[ch_i].decode.decoder,
					   have ? s.modea_ev.data[ch_i] : NULL, octets,
					   LC3_PCM_FORMAT_S16, dest, 1);

		audio_perf_cycle_end(t1, AUDIO_PERF_PATH_LC3_DECODE);
		if (err == 1) {
			audio_stats_frame_plc();
		} else if (err < 0) {
			LOG_WRN("[%d]: LC3 decode error %d", ch_i, err);
			audio_stats_decode_error();
			decoded_ok = false;
		} else {
			audio_stats_frame_decoded();
		}
	}
	if (!decoded_ok) {
		/* Hard decoder error: skip the unsafe interleaved output
		 * (the event is consumed; the failure is counted above). */
		return;
	}

	/* Interleave + push once per resolved event.  Source validity for
	 * the oracle: a half carries its original VALID flag; a PLC half
	 * is never source-valid. */
	audio_decode_interleave(s.l_buf, s.r_buf, s.stereo_out, s.slots[0].decode.samples_per_ch);
	audio_volume_apply(s.stereo_out, s.slots[0].decode.samples_per_ch * 2);

#if defined(CONFIG_BSIM_OBSERVER)
	bsim_observer_pre_push(s.modea_ev.half_src[MODEA_CH_LEFT],
			       s.modea_ev.half_src[MODEA_CH_RIGHT]);
#endif
	if (audio_sink_push(s.stereo_out, s.slots[0].decode.samples_per_ch * 2) < 0) {
		audio_perf_push_failure();
	}
}

/*
 * Mode inference used by the receive path.  Matches the pre-R6 bt_bap
 * decision: chan_count >= 2 → Mode B; else two configured sinks → Mode A;
 * else mono.
 */
static enum audio_stream_mode slot_mode(const struct audio_stream_slot *sl)
{
	if (sl->shape.chan_count >= 2) {
		return AUDIO_STREAM_MODE_MODEB;
	}
	if (s.configured_count >= 2) {
		return AUDIO_STREAM_MODE_MODEA;
	}
	return AUDIO_STREAM_MODE_MONO;
}

/* The whole decode/conceal/volume/push path for one received SDU.  Runs
 * with the receive lease held but WITHOUT the session mutex: the slot /
 * assembler / decoder state is stable because teardown waits for all
 * admitted leases to drain before resetting anything, and production
 * recv callbacks are serialized on the BT RX WQ. */
static void session_recv_path(size_t idx, struct audio_stream_slot *sl, bool valid, bool has_ts,
			      uint32_t ts, uint16_t seq, const uint8_t *data, size_t len)
{
	const enum audio_stream_mode mode = slot_mode(sl);
	const int spc = sl->decode.samples_per_ch;

	if (!sl->decode.decoder) {
		LOG_WRN("LC3 decoder not ready for stream[%zu]", idx);
		return;
	}

	/* ── Per-CIS sequence-gap concealment ────────────────────────
	 * HCI packet-sequence evidence: emitted HCI SDUs omitted after
	 * controller sequencing (controller-to-host or host-side loss)
	 * appear as a jump in the delivered Packet_Sequence_Number;
	 * controller-side radio events with no emitted HCI SDU keep the
	 * sequence contiguous and are detected by timestamp cadence
	 * below instead (FR4 mono: 8876 callbacks, zero sequence gaps,
	 * 225 I2S resets).  Without a response the audio path starves
	 * (`i2s_nrfx: Next buffers not supplied on time`) while PLC stays
	 * near zero.  Every delivered callback advances the per-CIS
	 * tracker; a wrap-safe forward delta in [2, MAX+1] conceals the
	 * omitted SDUs as PLC BEFORE the current SDU is processed, so
	 * output cadence is preserved and no channel's decoder is ever
	 * advanced past a missing event.  Duplicate / backward /
	 * out-of-window deltas resync (counted, logged) instead of
	 * synthesizing unbounded work.  A malformed current packet below
	 * still consumed its sequence position here, so it is never later
	 * double-concealed.
	 */
	uint32_t seq_omitted = 0U;
	uint16_t first_seq = 0U;
	enum audio_iso_seq_result sres =
		audio_iso_seq_update(&sl->seq, seq, &seq_omitted, &first_seq);
	uint32_t omitted = seq_omitted;

	if (sres == AUDIO_ISO_SEQ_RES_RESYNC) {
		LOG_WRN("stream[%zu]: ISO seq discontinuity at %u (resyncs=%u) — no synthesis", idx,
			seq, audio_iso_seq_get_resyncs(&sl->seq));
	}

	/* ── ISO timestamp-cadence concealment (mono / Mode B) ────────
	 * The HCI packet sequence number stays contiguous when the
	 * controller emits no SDU for a lost radio event (SW Split
	 * advances its session sequence only on emitted SDUs), so a
	 * sequence tracker alone misses those omissions (FR4 mono: 8876
	 * callbacks, zero seq gaps, 225 I2S restarts).  Delivered ISO
	 * timestamps jump by the integer multiple of the SDU interval the
	 * omitted events span, so for one-CIS modes every delivered
	 * callback (valid, LOST, empty, malformed — all before payload
	 * validation, preserving the no-double-conceal contract) feeds
	 * the cadence tracker, and the merged omission count is the MAX of
	 * the sequence- and timestamp-derived counts: a host-side dropped
	 * HCI SDU produces both a sequence jump and a timestamp jump for
	 * the same missing output event (MAX conceals it once); a
	 * controller-side radio omission produces only a timestamp jump.
	 * Never add the two sources.  Mode A keeps its sequence-only
	 * synthetic-sentinel path; timestamp-only Mode A synthesis needs
	 * event-position/sentinel design beyond this blocker.
	 *
	 * Cadence WRAP (controller timestamp wrap/rebase, expected) never
	 * warns; cadence RESYNC is unexpected and logs one clear warning
	 * carrying the structured observation evidence (exact reason
	 * enum, current timestamp, delta, interval, estimated event
	 * count, delivered positions, error, accepted scaled tolerance,
	 * cumulative resync count) so any future RESYNC is classifiable
	 * without another blind hardware run.  The observation is
	 * diagnostics only: no state owner, no new stats fields.
	 * No INFO per gap — FR4 observed thousands of omitted events and
	 * per-gap UART logging could perturb real-time behavior; a LOG_DBG
	 * line and the existing aggregate PLC/stream-reset evidence
	 * suffice.
	 */
	if (mode != AUDIO_STREAM_MODE_MODEA) {
		struct audio_iso_cadence_observation cad_obs = {0};
		uint32_t cad_omitted = 0U;
		enum audio_iso_cadence_result cres = audio_iso_cadence_update(
			&sl->cadence, has_ts, ts, sl->shape.frame_dur_us, &cad_omitted, &cad_obs);

		switch (cres) {
		case AUDIO_ISO_CADENCE_RES_GAP:
			if (cad_omitted > omitted) {
				omitted = cad_omitted;
			}
			LOG_DBG("stream[%zu]: ISO ts cadence gap: %u omitted event(s) — conceal",
				idx, cad_omitted);
			break;
		case AUDIO_ISO_CADENCE_RES_RESYNC:
			LOG_WRN("stream[%zu]: ISO ts cadence resync: reason %u, at %u, delta %u "
				"us, "
				"interval %u us, events %u, delivered %u, error %u us, "
				"tolerance %u us, resyncs=%u — no synthesis",
				idx, (unsigned int)cad_obs.reason, ts, cad_obs.delta_us,
				sl->shape.frame_dur_us, cad_obs.event_count,
				cad_obs.delivered_positions, cad_obs.error_us, cad_obs.tolerance_us,
				audio_iso_cadence_get_resyncs(&sl->cadence));
			break;
		case AUDIO_ISO_CADENCE_RES_FIRST:
		case AUDIO_ISO_CADENCE_RES_NO_TS:
		case AUDIO_ISO_CADENCE_RES_CONTIG:
		case AUDIO_ISO_CADENCE_RES_WRAP:
			break;
		}
	}

	if (omitted > 0U) {
		/* The seq-evidence INFO stays gated on an actual sequence
		 * gap and prints the SEQUENCE-derived count (never the
		 * merged count): a cadence-only gap (sequence contiguous,
		 * timestamp jump) is logged at DBG only, and a merged gap
		 * must not label timestamp-only positions as sequence
		 * omissions. */
		if (sres == AUDIO_ISO_SEQ_RES_GAP) {
			LOG_INF("stream[%zu]: ISO seq gap: %u omitted SDU(s) (first %u, cur %u) — "
				"conceal",
				idx, seq_omitted, first_seq, seq);
		}
		if (mode != AUDIO_STREAM_MODE_MODEA) {
			/* Mode B / mono single ASE: one PLC push per omitted
			 * SDU through the production decode helper. */
			for (uint32_t i = 0U; i < omitted; i++) {
				session_mode_plc_push(idx, sl);
			}
		} else {
			/* Mode A: one synthetic LOST sentinel per omitted SDU,
			 * fed into the assembler before the current half so
			 * each missing event resolves (pairing one-sided and
			 * simultaneous gaps) before the current packet. */
			for (uint32_t i = 0U; i < omitted; i++) {
				session_mode_a_store_and_process((enum modea_channel)idx, NULL, 0U,
								 false, false, 0U,
								 (uint16_t)(first_seq + i));
			}
		}
	}

	/*
	 * Valid zero-length ISO SDUs (empty HCI packets some controllers
	 * send when the remote produced no SDU for this event) are NOT
	 * malformed LC3 payloads: record exactly one empty-SDU event and
	 * normalize the local validity to source-invalid so the remaining
	 * decode/conceal path renders PLC at normal cadence (mono and Mode
	 * B via the existing PLC decode/push path, Mode A through the
	 * assembler preserving any original timestamp so the empty half
	 * pairs at its exact event position).  No malformed-SDU observer
	 * evidence and no decode-error increment are produced; the caller's
	 * data and the public API are untouched.  Non-valid zero-length
	 * callbacks (LOST) never reach this branch and keep their existing
	 * behavior without counting empty_sdus.
	 */
	if (valid && len == 0U) {
		audio_stats_empty_sdu();
		valid = false;
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
	if (valid && sl->shape.octets_per_frame > 0U) {
		size_t expected =
			(size_t)sl->shape.octets_per_frame * sl->shape.frame_blocks_per_sdu;

		if (sl->decode.chan_count == 2) {
			expected *= 2U; /* Mode B: [L frame][R frame] per block */
		}
		if (len != expected) {
			LOG_INF("stream[%zu]: malformed SDU len %u != expected %zu", idx, len,
				expected);
			audio_stats_decode_error();
#if defined(CONFIG_BSIM_OBSERVER)
			bsim_observer_malformed_sdu();
#endif
			return;
		}
	}

	if (mode == AUDIO_STREAM_MODE_MODEB) {
		/* Mode B: stereo single-ASE — split SDU per-channel, two
		 * independent decoders with stride=2 handled by
		 * audio_decode_sdu.  A negative decode return skips volume
		 * and sink push.
		 */
		int ret = audio_decode_sdu(&sl->decode, valid ? data : NULL, len, valid,
					   s.stereo_out);

		if (ret < 0) {
			LOG_WRN("stream[%zu]: decode failed %d — skipping volume/push", idx, ret);
			return;
		}
		audio_volume_apply(s.stereo_out, spc * 2);

#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_pre_push(valid, valid);
#endif
		if (audio_sink_push(s.stereo_out, spc * 2) < 0) {
			audio_perf_push_failure();
		}
	} else if (mode == AUDIO_STREAM_MODE_MODEA) {
		/* Mode A: 2 mono ASEs — the event assembler owns bounded
		 * pending COMPRESSED halves per channel plus ISO SDU
		 * reference-time pairing, and resolves each CIG event
		 * exactly once — synthesizing PLC for a missing channel so
		 * output cadence continues under one-CIS loss.  Decode is
		 * deferred to event resolution, which keeps every channel's
		 * decoder chronological (a PLC for a missing older event is
		 * always generated before that channel's newer packet is
		 * decoded).  Synthetic sentinels from the sequence-gap path
		 * above enter through the same helper.
		 *
		 * The ISO SDU reference time is the pairing key, so a
		 * VALID-flag SDU missing the TS flag makes this half
		 * unusable: skip decoder, pairing mutation, and push,
		 * count one receive/decode fault, and emit the test
		 * observer event (a real warning — the normal matrix
		 * proves zero occurrences).  Non-valid SDUs (LOST /
		 * sync-boundary replacements) carry no TS by definition
		 * and keep the concealment/startup-transient path; their
		 * source validity is reported to the oracle separately. */
		if (valid && !has_ts) {
			LOG_WRN("stream[%zu]: Mode A valid SDU missing TS flag — half skipped",
				idx);
			audio_stats_decode_error();
#if defined(CONFIG_BSIM_OBSERVER)
			bsim_observer_missing_ts();
#endif
			return;
		}
		session_mode_a_store_and_process((enum modea_channel)idx, valid ? data : NULL, len,
						 valid, has_ts, ts, seq);
	} else {
		/* Mono single-ASE: decode + mono-to-stereo handled by
		 * audio_decode_sdu.  A negative decode return skips volume
		 * and sink push.
		 */
		int ret = audio_decode_sdu(&sl->decode, valid ? data : NULL, len, valid,
					   s.stereo_out);

		if (ret < 0) {
			LOG_WRN("stream[%zu]: decode failed %d — skipping volume/push", idx, ret);
			return;
		}
		audio_volume_apply(s.stereo_out, spc * 2);

#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_pre_push(valid, valid);
#endif
		if (audio_sink_push(s.stereo_out, spc * 2) < 0) {
			audio_perf_push_failure();
		}
	}
}

#endif /* CONFIG_LIBLC3 */

int audio_stream_session_recv(size_t idx, bool valid, bool has_ts, uint32_t ts, uint16_t seq,
			      const uint8_t *data, size_t len)
{
	if (idx >= AUDIO_STREAM_SESSION_MAX_SLOTS) {
		return -EINVAL;
	}

	/* RX acquire: reject closed admission / unconfigured slot under
	 * the session lock; capture the generation and increment the
	 * in-flight count; then run decode/push outside the lock. */
	k_mutex_lock(&session_mutex, K_FOREVER);
	if (!s.admission_open || !s.slots[idx].configured) {
		k_mutex_unlock(&session_mutex);
		return -EINVAL;
	}
	(void)s.generation;
	s.in_flight++;
	k_mutex_unlock(&session_mutex);

#if defined(CONFIG_LIBLC3)
	session_recv_path(idx, &s.slots[idx], valid, has_ts, ts, seq, data, len);
#else
	(void)valid;
	(void)has_ts;
	(void)ts;
	(void)seq;
	(void)data;
	(void)len;
#endif /* CONFIG_LIBLC3 */

	/* Release the lease on every exit path. */
	k_mutex_lock(&session_mutex, K_FOREVER);
	s.in_flight--;
	if (s.in_flight == 0U) {
		k_condvar_broadcast(&session_drained);
	}
	k_mutex_unlock(&session_mutex);

	return 0;
}

/* ── valid-recv counting / accessors ─────────────────────────────── */

size_t audio_stream_session_recv_valid_count(size_t idx)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	struct audio_stream_slot *sl = slot_at(idx);

	if (sl == NULL) {
		k_mutex_unlock(&session_mutex);
		return 0U;
	}
	sl->recv_cnt++;
	const size_t cnt = sl->recv_cnt;

	k_mutex_unlock(&session_mutex);
	return cnt;
}

void audio_stream_session_recv_reset(size_t idx)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	struct audio_stream_slot *sl = slot_at(idx);

	if (sl != NULL) {
		sl->recv_cnt = 0U;
	}
	k_mutex_unlock(&session_mutex);
}

bool audio_stream_session_configured(size_t idx)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	struct audio_stream_slot *sl = slot_at(idx);
	const bool configured = (sl != NULL) && sl->configured;

	k_mutex_unlock(&session_mutex);
	return configured;
}

size_t audio_stream_session_configured_count(void)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	const size_t count = s.configured_count;

	k_mutex_unlock(&session_mutex);
	return count;
}

const struct audio_stream_codec_shape *audio_stream_session_shape(size_t idx)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	struct audio_stream_slot *sl = slot_at(idx);
	const struct audio_stream_codec_shape *shape =
		(sl != NULL && sl->configured) ? &sl->shape : NULL;

	k_mutex_unlock(&session_mutex);
	return shape;
}

enum audio_stream_mode audio_stream_session_mode(size_t idx)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	struct audio_stream_slot *sl = slot_at(idx);
	enum audio_stream_mode mode = AUDIO_STREAM_MODE_MONO;

	if (sl != NULL && sl->configured) {
		if (sl->shape.chan_count >= 2) {
			mode = AUDIO_STREAM_MODE_MODEB;
		} else if (s.configured_count >= 2) {
			mode = AUDIO_STREAM_MODE_MODEA;
		}
	}
	k_mutex_unlock(&session_mutex);
	return mode;
}

uint32_t audio_stream_session_pd(size_t idx)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	struct audio_stream_slot *sl = slot_at(idx);
	const uint32_t pd = (sl != NULL) ? sl->pd_us : 0U;

	k_mutex_unlock(&session_mutex);
	return pd;
}

size_t audio_stream_session_recv_count(size_t idx)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	struct audio_stream_slot *sl = slot_at(idx);
	const size_t cnt = (sl != NULL) ? sl->recv_cnt : 0U;

	k_mutex_unlock(&session_mutex);
	return cnt;
}

#if defined(CONFIG_ZTEST)
/* GCOVR_EXCL_START — test-only accessors, absent from production builds */
int audio_stream_session_test_lock_try(void)
{
	return k_mutex_lock(&session_mutex, K_NO_WAIT);
}

void audio_stream_session_test_lock_release(void)
{
	k_mutex_unlock(&session_mutex);
}

size_t audio_stream_session_test_in_flight(void)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	const size_t in_flight = s.in_flight;

	k_mutex_unlock(&session_mutex);
	return in_flight;
}

bool audio_stream_session_test_admission_open(void)
{
	k_mutex_lock(&session_mutex, K_FOREVER);
	const bool open = s.admission_open;

	k_mutex_unlock(&session_mutex);
	return open;
}
/* GCOVR_EXCL_STOP */
#endif /* CONFIG_ZTEST */
