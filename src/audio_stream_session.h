/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * App-owned BAP sink receive/session state (R6).
 *
 * The session is the exclusive owner of app audio receive state: validated
 * codec shape per sink slot, LC3 decoder contexts, per-CIS ISO sequence
 * trackers, the shared Mode A event assembler, configured occupancy,
 * presentation delay, receive counters, and the
 * decode/conceal/volume/push mechanics with their statistics, perf, and
 * BSim-observer calls.
 *
 * It NEVER owns or clears Bluetooth stack objects: struct bt_bap_stream,
 * conn, ep, codec_cfg, qos, iso all stay with bt_bap.c/ASCS.  The receive
 * adapter copies scalar ISO info / SDU data only; the session retains no
 * bt_iso_recv_info or net_buf pointers.
 *
 * Receive admission is independent of the lifecycle gate: only rx_open()
 * (at the successful stream_started gate-open edge) enables admission;
 * config/release/reset_all never reopen it.  Teardown calls rx_close(),
 * which closes admission, bumps the generation, and waits for admitted
 * receive leases to drain before any decoder/assembler/sequence reset.
 */

#ifndef AUDIO_STREAM_SESSION_H
#define AUDIO_STREAM_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(CONFIG_BSIM_SINK_POOL_LIMIT)
#define AUDIO_STREAM_SESSION_MAX_SLOTS CONFIG_BSIM_SINK_POOL_LIMIT
#else
#define AUDIO_STREAM_SESSION_MAX_SLOTS CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT
#endif

/* Validated LC3 codec shape, carried out of the ASCS Config callback.
 * Deliberately SMALL (five scalars): the full per-slot decoder state
 * (struct audio_decode_ctx embeds two lc3_decoder_mem_48k_t objects,
 * ~4.2 KB each) must never live on the BT RX WQ stack. */
struct audio_stream_codec_shape {
	uint16_t freq_hz;
	uint16_t frame_dur_us;
	uint16_t octets_per_frame;
	uint8_t frame_blocks_per_sdu;
	uint8_t chan_count;
};

enum audio_stream_mode {
	AUDIO_STREAM_MODE_MONO = 0, /* 1 ASE, chan_count 1 */
	AUDIO_STREAM_MODE_MODEB,    /* 1 ASE, chan_count 2 */
	AUDIO_STREAM_MODE_MODEA,    /* 2 ASEs, both mono */
};

/**
 * Initialize the session: zero all slot/assembler state, configured_count,
 * and leave receive admission CLOSED (only rx_open() enables it).
 * Thread-context; called once from bt_bap_init().  Idempotent: a repeat
 * call fully resets the session.
 *
 * @retval 0 on success
 */
int audio_stream_session_init(void);

/**
 * Store the validated codec shape for sink @p idx (ASCS Config success).
 * Resets the slot's receive count, presentation delay, decoder context,
 * and sequence tracker, and increments the configured count.  The slot
 * becomes "configured" (occupancy for the lifecycle gate) but receive
 * admission is NOT opened here.
 *
 * @param idx   sink slot index (< AUDIO_STREAM_SESSION_MAX_SLOTS)
 * @param shape validated codec shape (non-NULL)
 *
 * @retval 0 on success
 * @retval -EINVAL on out-of-range idx or NULL shape
 */
int audio_stream_session_config(size_t idx, const struct audio_stream_codec_shape *shape);

/**
 * Store the negotiated presentation delay for sink @p idx (ASCS QoS).
 *
 * @retval 0 on success
 * @retval -EINVAL on out-of-range idx or unconfigured slot
 */
int audio_stream_session_qos(size_t idx, uint32_t pd_us);

/**
 * Configure the LC3 decoders for sink @p idx (ASCS Enable): calls
 * audio_decode_config() with the stored shape, configures the Mode A
 * assembler interval, and tells the audio sink the expected frames per
 * push.  The bt_bap Enable callback revalidates the retained codec config
 * against the stored shape before calling this.
 *
 * @retval 0 on success
 * @retval -EINVAL on out-of-range idx, unconfigured slot, or decoder
 *         setup failure (decoder context left fully reset)
 */
int audio_stream_session_enable(size_t idx);

/**
 * Disable sink @p idx (ASCS Disable): null the decoder pointers; the
 * stored shape and configured flag are KEPT so a later Enable works
 * without a fresh Config.
 */
void audio_stream_session_disable(size_t idx);

/**
 * Clear the Mode A assembler and every per-slot ISO sequence tracker
 * (ASCS Start, gate-open edge, non-forced teardown).  The next stream's
 * first delivered callback re-bases instead of misreading a gap across
 * stream boundaries.
 */
void audio_stream_session_start_clear(void);

/**
 * Release sink @p idx (ASCS Release): after receive admission was closed
 * and drained by the caller, reset the slot's decoder/sequence state,
 * clear shape/pd/recv count, and decrement the configured count.  Does
 * not touch the bt_bap_stream object and does not reopen admission.
 */
void audio_stream_session_release(size_t idx);

/**
 * Reset ALL session state (ACL disconnect): close is already done by the
 * caller via rx_close(); this clears every slot (shape, recv count, pd,
 * decoder, sequence), the Mode A assembler, and the configured count.
 * Receive admission stays closed.
 */
void audio_stream_session_reset_all(void);

/**
 * Enable receive admission.  Called ONLY at the successful stream_started
 * gate-open edge, before data admission.  Idempotent.
 */
void audio_stream_session_rx_open(void);

/**
 * Close receive admission and wait until every admitted receive lease
 * drains (idempotent; called from teardown paths outside an active
 * session lease).  Bumps the generation so late receives are rejected.
 * Never called while holding a session lease; never from ISR.
 */
void audio_stream_session_rx_close(void);

/**
 * Receive one ISO SDU for sink @p idx (bt_bap recv adapter, gate open).
 *
 * Copies only the decomposed scalars/data; acquires a receive lease under
 * the session mutex, then runs the whole decode/conceal/volume/push path
 * OUTSIDE the lock.  Handles: per-CIS sequence-gap concealment (PLC push
 * or synthetic Mode A LOST sentinel), malformed-SDU rejection (exactly one
 * decode-error increment, no decode/push/Mode A mutation, before the
 * current decode/store), Mode B / Mode A / mono decode with volume and
 * one sink push, hard decode-error skip, and the Mode A missing-TS
 * rejection.  All statistics, perf, and BSim-observer calls happen here.
 *
 * @retval 0 on every handled receive path
 * @retval -EINVAL on admission closed / out-of-range idx / unconfigured
 *         slot (no state mutation, no observer event)
 */
int audio_stream_session_recv(size_t idx, bool valid, bool has_ts, uint32_t ts, uint16_t seq,
			      const uint8_t *data, size_t len);

/**
 * Increment the valid-receive counter for sink @p idx and return the new
 * count.  Called by the bt_bap adapter for every VALID packet BEFORE the
 * gate check (gate-independent counting, identical to the pre-R6
 * behavior); the adapter uses the returned count for the periodic SDU log.
 */
size_t audio_stream_session_recv_valid_count(size_t idx);

/**
 * Zero the receive counter of sink @p idx (stream_started per-slot reset).
 */
void audio_stream_session_recv_reset(size_t idx);

/* ── accessors (thread-safe scalar reads) ─────────────────────────── */

/** True when sink @p idx has a stored shape (configured, not released). */
bool audio_stream_session_configured(size_t idx);

/** Number of configured (not released) sink slots. */
size_t audio_stream_session_configured_count(void);

/**
 * Stored shape of sink @p idx, or NULL when the slot is not configured /
 * out of range.  The pointer is stable until the slot is reconfigured.
 */
const struct audio_stream_codec_shape *audio_stream_session_shape(size_t idx);

/**
 * Mode inference for sink @p idx: chan_count >= 2 → MODEB; else
 * configured_count >= 2 → MODEA; else MONO.  Matches the pre-R6
 * bt_bap decision (decode.chan_count >= 2 / num_sink_ase >= 2).
 */
enum audio_stream_mode audio_stream_session_mode(size_t idx);

/** Negotiated presentation delay of sink @p idx (0 when not configured). */
uint32_t audio_stream_session_pd(size_t idx);

/** Valid-receive counter of sink @p idx (0 when not configured). */
size_t audio_stream_session_recv_count(size_t idx);

#if defined(CONFIG_ZTEST)
/**
 * Test-only accessors (GCOVR-excluded, absent from production builds).
 * Prove the lease discipline: the session mutex is never held across
 * decode/sink (a concurrent thread can acquire it during a push), and
 * rx_close waits for in-flight leases.
 */
int audio_stream_session_test_lock_try(void); /* k_mutex_lock K_NO_WAIT: 0 or -EBUSY */
void audio_stream_session_test_lock_release(void);
size_t audio_stream_session_test_in_flight(void);
bool audio_stream_session_test_admission_open(void);
#endif /* CONFIG_ZTEST */

#endif /* AUDIO_STREAM_SESSION_H */
