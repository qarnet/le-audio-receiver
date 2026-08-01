/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM deterministic multi-channel TX — repository-owned replacement for
 * the upstream repeated-tone stream_tx/stream_lc3 helpers.
 *
 * Keeps the real BAP send and liblc3 encode APIs.  PCM is generated with
 * defined unsigned integer arithmetic only (no floating point), differs
 * by channel and evolves by sequence number, so swap, duplication, stale
 * pairing, overwrite, and cross-pairing change the receiver hashes.
 */

#ifndef BSIM_TX_H
#define BSIM_TX_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/bluetooth/audio/bap.h>

#define BSIM_TX_MAX_STREAMS 2

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

/**
 * Initialize the TX thread.  Idempotent.
 */
int bsim_tx_init(void);

/**
 * Register a stream for TX.  The stream must already carry the negotiated
 * codec config (used only for logging; the explicit @p cfg drives
 * encoding).  seq_num starts at 0.
 *
 * @retval 0 success
 * @retval -ENOMEM all TX slots in use
 */
int bsim_tx_register(struct bt_bap_stream *bap_stream, const struct bsim_tx_config *cfg);

/** Unregister a stream from TX (stops sending on it). */
int bsim_tx_unregister(struct bt_bap_stream *bap_stream);

/** Pause sending on one stream (keeps registration/encoder state). */
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

/** Successful send count for one stream. */
uint32_t bsim_tx_send_count(struct bt_bap_stream *bap_stream);

/** Number of registered streams currently in the streaming state. */
int bsim_tx_streaming_count(void);

#endif /* BSIM_TX_H */
