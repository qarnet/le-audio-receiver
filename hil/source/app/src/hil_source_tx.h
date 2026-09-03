/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * TX send driver for the dedicated LE Audio source fixture (RH1B).
 *
 * The real radio send path only: a fixed net_buf pool sized for the
 * 255-byte ISO MTU, reserve of BT_ISO_CHAN_SEND_RESERVE, and the actual
 * bt_bap_stream_send() calls.  The coordinator owns frame pacing,
 * lockstep, stage caps, sequence/outstanding bookkeeping, and the RH1A
 * submitted counters; this module only moves encoded SDUs onto a stream.
 */

#ifndef HIL_SOURCE_TX_H
#define HIL_SOURCE_TX_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/audio/bap.h>

/* Maximum number of concurrent TX streams (two for Mode A). */
#define HIL_SOURCE_MAX_TX_STREAMS 2U

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the fixed TX net_buf pool.  Returns 0 or a negative errno. */
int hil_source_tx_init(void);

/* Attach the production stream objects (one per configured stream).
 * Called once per segment before the first send. */
int hil_source_tx_attach(struct bt_bap_stream *streams, uint8_t count);

/* Send one encoded SDU on a stream with the given sequence number.
 * Allocates from the fixed pool, copies the SDU, and hands the buffer to
 * the ISO stack.  Returns 0 on success (buffer ownership transferred) or
 * a negative errno (buffer unref'd by the driver).  Sends after
 * hil_source_tx_stop() are rejected. */
int hil_source_tx_send(uint8_t stream_idx, uint16_t seq, const uint8_t *sdu, size_t len);

/* Stop the TX driver: rejects further sends and clears the attached
 * stream pointers and count.  The coordinator separately clears logical
 * outstanding only after its own generation change. */
void hil_source_tx_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_TX_H */
