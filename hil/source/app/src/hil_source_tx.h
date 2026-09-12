/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * TX send driver for the dedicated LE Audio source fixture.
 *
 * Production TX path: a fixed net_buf pool sized for the
 * 255-byte ISO MTU, reserve of BT_ISO_CHAN_SEND_RESERVE, and the actual
 * bt_bap_stream_send() calls. The coordinator owns frame pacing,
 * lockstep, stage caps, sequence/outstanding bookkeeping, and submitted
 * counters; this module only moves encoded SDUs onto a stream.
 *
 * SDC timestamp mode: hil_source_tx_send_ts() provides one SDU for a
 * controller-clock ISO event via bt_bap_stream_send_ts().
 * hil_source_tx_read_tx_ts() reads the event timestamp assigned to the
 * previously provided SDU through the SDC HCI VS command, following the
 * nrf iso_time_sync and nrf5340_audio pattern. One untimestamped bootstrap SDU
 * on stream 0 establishes the shared CIG event grid. All regular segment SDUs
 * then use the learned grid.
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

/* Timestamp-pinned send (SDC timestamps mode): provide this SDU for the ISO
 * event whose CIG-event start is `ts` (controller clock, microseconds). Same
 * buffer semantics as hil_source_tx_send(). Successful submission does not
 * prove peer receipt. The coordinator only supplies future timestamps because
 * SDC flushes an SDU whose timestamp is already past when processed. */
int hil_source_tx_send_ts(uint8_t stream_idx, uint16_t seq, const uint8_t *sdu, size_t len,
			  uint32_t ts);

/* Read the controller-assigned TX timestamp (CIG event start, controller-clock
 * microseconds) for the previously provided SDU on a stream. Thread context
 * only (synchronous HCI). Returns 0 and sets *ts, or a negative errno, for
 * example -ENOTCONN before a stream has an ISO connection or during teardown. */
int hil_source_tx_read_tx_ts(uint8_t stream_idx, uint32_t *ts);

/* Read HCI LE_Read_ISO_TX_Sync for the most recently scheduled SDU. The
 * controller returns its synchronization timestamp and sequence number; this
 * does not prove peer receipt. Thread context only (synchronous HCI). */
int hil_source_tx_read_sync(uint8_t stream_idx, uint32_t *ts, uint32_t *seq);

/* Stop the TX driver: rejects further sends and clears the attached
 * stream pointers and count.  The coordinator separately clears logical
 * outstanding only after its own generation change. */
void hil_source_tx_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_TX_H */
