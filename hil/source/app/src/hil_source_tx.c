/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * TX send driver (RH1B): fixed net_buf pool + bt_bap_stream_send().
 */

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include "hil_source_tx.h"

#define HIL_SOURCE_TX_POOL_COUNT CONFIG_BT_ISO_TX_BUF_COUNT
#define HIL_SOURCE_TX_MTU        CONFIG_BT_ISO_TX_MTU

static struct bt_bap_stream *tx_streams[HIL_SOURCE_MAX_TX_STREAMS];
static uint8_t tx_stream_count;
static bool tx_stopped;

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

void hil_source_tx_stop(void)
{
	uint8_t i;

	tx_stopped = true;
	/* Reject further sends and clear the attached stream pointers and
	 * count: the next tx_start re-activates only the exact current
	 * segment stream count. */
	for (i = 0U; i < HIL_SOURCE_MAX_TX_STREAMS; i++) {
		tx_streams[i] = NULL;
	}
	tx_stream_count = 0U;
}
