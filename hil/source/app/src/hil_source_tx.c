/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * TX send driver: fixed net_buf pool + bt_bap_stream_send(), plus
 * SDC timestamp-mode support: bt_bap_stream_send_ts() and the HCI VS ISO
 * Read TX Timestamp readback used to establish the CIG event grid
 * (nrfxlib SDC isochronous_channels.rst "preferred way of providing data").
 */

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include <bluetooth/hci_vs_sdc.h>
#include <sdc_hci.h>

#include "hil_source_tx.h"

#define HIL_SOURCE_TX_POOL_COUNT          CONFIG_BT_ISO_TX_BUF_COUNT
#define HIL_SOURCE_TX_MTU                 CONFIG_BT_ISO_TX_MTU
#define HIL_SOURCE_TX_CONN_HANDLE_INVALID UINT16_MAX

static struct bt_bap_stream *tx_streams[HIL_SOURCE_MAX_TX_STREAMS];
static uint8_t tx_stream_count;
static bool tx_stopped;
/* Cached ISO connection handles per stream. HCI handle zero is valid, so
 * UINT16_MAX marks an unresolved handle. */
static uint16_t tx_conn_handles[HIL_SOURCE_MAX_TX_STREAMS];

NET_BUF_POOL_FIXED_DEFINE(tx_pool, HIL_SOURCE_TX_POOL_COUNT, BT_ISO_SDU_BUF_SIZE(HIL_SOURCE_TX_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

int hil_source_tx_init(void)
{
	tx_stopped = false;
	return 0;
}

int hil_source_tx_attach(struct bt_bap_stream *streams, uint8_t count)
{
	uint8_t i;

	if (streams == NULL || count == 0U || count > HIL_SOURCE_MAX_TX_STREAMS) {
		return -EINVAL;
	}
	for (i = 0U; i < count; i++) {
		tx_streams[i] = &streams[i];
		tx_conn_handles[i] = HIL_SOURCE_TX_CONN_HANDLE_INVALID;
	}
	tx_stream_count = count;
	tx_stopped = false;
	return 0;
}

int hil_source_tx_send(uint8_t stream_idx, uint16_t seq, const uint8_t *sdu, size_t len)
{
	struct bt_bap_stream *stream;
	struct net_buf *buf;
	int err;

	if (tx_stopped) {
		return -EIO;
	}
	if (stream_idx >= tx_stream_count || sdu == NULL || len == 0U) {
		return -EINVAL;
	}
	if (len > HIL_SOURCE_TX_MTU) {
		return -EOVERFLOW;
	}
	stream = tx_streams[stream_idx];

	buf = net_buf_alloc(&tx_pool, K_NO_WAIT);
	if (buf == NULL) {
		return -ENOBUFS;
	}
	net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);
	net_buf_add_mem(buf, sdu, len);

	err = bt_bap_stream_send(stream, buf, seq);
	if (err != 0) {
		net_buf_unref(buf);
		return err;
	}
	return 0;
}

int hil_source_tx_send_ts(uint8_t stream_idx, uint16_t seq, const uint8_t *sdu, size_t len,
			  uint32_t ts)
{
	struct bt_bap_stream *stream;
	struct net_buf *buf;
	int err;

	if (tx_stopped) {
		return -EIO;
	}
	if (stream_idx >= tx_stream_count || sdu == NULL || len == 0U) {
		return -EINVAL;
	}
	if (len > HIL_SOURCE_TX_MTU) {
		return -EOVERFLOW;
	}
	stream = tx_streams[stream_idx];

	buf = net_buf_alloc(&tx_pool, K_NO_WAIT);
	if (buf == NULL) {
		return -ENOBUFS;
	}
	net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);
	net_buf_add_mem(buf, sdu, len);

	err = bt_bap_stream_send_ts(stream, buf, seq, ts);
	if (err != 0) {
		net_buf_unref(buf);
		return err;
	}
	return 0;
}

int hil_source_tx_read_sync(uint8_t stream_idx, uint32_t *ts, uint32_t *seq)
{
	struct bt_bap_stream *stream;
	struct bt_iso_tx_info info;
	int err;

	if (ts == NULL || seq == NULL) {
		return -EINVAL;
	}
	if (tx_stopped || stream_idx >= tx_stream_count) {
		return -EIO;
	}
	stream = tx_streams[stream_idx];

	err = bt_bap_stream_get_tx_sync(stream, &info);
	if (err != 0) {
		return err;
	}
	*ts = info.ts;
	*seq = info.seq_num;
	return 0;
}

int hil_source_tx_read_tx_ts(uint8_t stream_idx, uint32_t *ts)
{
	struct bt_bap_stream *stream;
	sdc_hci_cmd_vs_iso_read_tx_timestamp_t params;
	sdc_hci_cmd_vs_iso_read_tx_timestamp_return_t rsp;
	uint16_t conn_handle;
	int err;

	if (ts == NULL) {
		return -EINVAL;
	}
	if (tx_stopped || stream_idx >= tx_stream_count) {
		return -EIO;
	}
	stream = tx_streams[stream_idx];

	conn_handle = tx_conn_handles[stream_idx];
	if (conn_handle == HIL_SOURCE_TX_CONN_HANDLE_INVALID) {
		const struct bt_conn *iso_conn;

		/* bt_bap_stream.iso is the public ISO channel reference,
		 * valid once the stream is in a group (bap.h). */
		if (stream->iso == NULL || stream->iso->iso == NULL) {
			return -ENOTCONN;
		}
		iso_conn = stream->iso->iso;
		err = bt_hci_get_conn_handle(iso_conn, &conn_handle);
		if (err != 0) {
			return err;
		}
		tx_conn_handles[stream_idx] = conn_handle;
	}

	params.conn_handle = conn_handle;
	err = hci_vs_sdc_iso_read_tx_timestamp(&params, &rsp);
	if (err != 0) {
		return err;
	}
	*ts = rsp.tx_time_stamp;
	return 0;
}

void hil_source_tx_stop(void)
{
	uint8_t i;

	tx_stopped = true;
	/* Reject further sends and clear the attached stream pointers and
	 * count: the next tx_start re-activates only the exact current
	 * segment stream count. */
	for (i = 0U; i < HIL_SOURCE_MAX_TX_STREAMS; i++) {
		tx_streams[i] = NULL;
		tx_conn_handles[i] = HIL_SOURCE_TX_CONN_HANDLE_INVALID;
	}
	tx_stream_count = 0U;
}
