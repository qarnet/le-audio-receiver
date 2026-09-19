/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM deterministic multi-channel TX — repository-owned replacement for
 * the upstream repeated-tone stream_tx/stream_lc3 helpers.
 *
 * Keeps the real BAP send API while selecting sequence-indexed, checked-in
 * LC3 corpus frames.  Every successful send contributes its logical sequence
 * and final payload bytes to the retained per-stream FNV-1a audit.
 */

#ifndef BSIM_TX_H
#define BSIM_TX_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/bluetooth/audio/bap.h>

#define BSIM_TX_MAX_STREAMS        2
#define BSIM_TX_FNV1A_OFFSET_BASIS UINT32_C(0x811C9DC5)

struct bsim_tx_config {
	/** Negotiated octets per frame per channel (from the codec preset). */
	uint16_t octets_per_frame;
	/** Sampling frequency in Hz (48000). */
	uint32_t freq_hz;
	/** Frame duration in microseconds (7500 or 10000). */
	uint32_t frame_duration_us;
	/**
	 * Channel count of this stream's SDU: 1 = mono / Mode A half,
	 * 2 = Mode B stereo single-ASE ([L frame][R frame] per SDU).
	 */
	uint8_t chan_count;
	/** Mode A channel selection: 0 = Front Left, 1 = Front Right. */
	uint8_t channel_idx;
};

struct bsim_tx_result {
	uint32_t send_count;
	uint32_t fnv1a_hash;
};

/**
 * Initialize the TX thread.  Idempotent.
 */
int bsim_tx_init(void);

/**
 * Register a stream for TX.  The stream must already carry the negotiated
 * codec config (used only for logging; the explicit @p cfg selects fixed
 * corpus frames).  seq_num starts at 0.
 *
 * @retval 0 success
 * @retval -EINVAL null or unsupported corpus geometry
 * @retval -ENOMEM all TX slots in use
 */
int bsim_tx_register(struct bt_bap_stream *bap_stream, const struct bsim_tx_config *cfg);

/** Unregister a stream from TX (stops sending on it). */
int bsim_tx_unregister(struct bt_bap_stream *bap_stream);

/** Pause sending on one stream (keeps registration state). */
void bsim_tx_pause(struct bt_bap_stream *bap_stream);

/** Resume sending on one stream. */
void bsim_tx_resume(struct bt_bap_stream *bap_stream);

/**
 * Number of streams that must be in the streaming state before TX starts
 * sending at all.  Used to hold Mode A TX until both ASEs stream (both
 * transmitted sequence counters then begin at zero, including reverse
 * start order).
 */
void bsim_tx_set_required_streams(int n);

/**
 * Inject exactly one malformed one-byte SDU when the next send on
 * @p bap_stream would use @p at_seq, then resume valid LC3 frames.
 */
void bsim_tx_schedule_malformed(struct bt_bap_stream *bap_stream, uint16_t at_seq);

/**
 * Cap the number of successful sends on one stream: the stream pauses
 * itself once its send count reaches @p limit.  0 = unlimited (default).
 */
void bsim_tx_set_send_limit(struct bt_bap_stream *bap_stream, uint32_t limit);

/**
 * Wait until @p bap_stream has committed its configured nonzero exact send
 * limit and auto-paused.
 *
 * @retval 0 cap reached
 * @retval -EINVAL NULL stream or zero timeout
 * @retval -ENODATA stream is not registered or has no nonzero limit
 * @retval -ETIMEDOUT notification wait expired
 * @retval -ESTALE registration generation, stream association, or expected
 *         limit state changed while waiting
 */
int bsim_tx_wait_send_limit(struct bt_bap_stream *bap_stream, uint32_t timeout_ms);

/** Successful send count for one stream. */
uint32_t bsim_tx_send_count(struct bt_bap_stream *bap_stream);

/**
 * Retained successful-send result for one logical stream.  The result remains
 * available after bsim_tx_unregister().
 *
 * @retval 0 result copied
 * @retval -EINVAL NULL argument
 * @retval -ENODATA stream has never been registered
 */
int bsim_tx_result(const struct bt_bap_stream *bap_stream, struct bsim_tx_result *result);

/** Number of registered streams currently in the streaming state. */
int bsim_tx_streaming_count(void);

#endif /* BSIM_TX_H */
