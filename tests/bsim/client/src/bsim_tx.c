/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM deterministic multi-channel TX — implementation.
 *
 * One TX thread round-robins over registered streams.  A stream sends
 * only while its endpoint is in the streaming state, and only when the
 * scenario-required stream count is streaming (Mode A holds both).
 * Per-channel encoders are independent; PCM patterns are deterministic
 * integer functions of (channel, sequence number, sample index).
 */

#include "bsim_tx.h"

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <lc3.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys_clock.h>
#include <zephyr/types.h>

LOG_MODULE_REGISTER(bsim_tx, LOG_LEVEL_INF);

#define BSIM_TX_MAX_SAMPLES 480 /* 48 kHz × 10 ms */

struct bsim_tx_stream {
	struct bt_bap_stream *bap_stream;
	struct bsim_tx_config cfg;
	lc3_encoder_t encoder[2];
	lc3_encoder_mem_48k_t encoder_mem[2];
	uint16_t seq_num;
	uint32_t send_count;
	uint32_t send_limit; /* 0 = unlimited */
	bool paused;
	bool inject_pending;
	uint16_t inject_at_seq;
};

static struct bsim_tx_stream tx_streams[BSIM_TX_MAX_STREAMS];
static atomic_int required_streaming = 1;

static struct bsim_tx_stream *tx_lookup(const struct bt_bap_stream *bap_stream)
{
	for (size_t i = 0U; i < ARRAY_SIZE(tx_streams); i++) {
		if (tx_streams[i].bap_stream == bap_stream) {
			return &tx_streams[i];
		}
	}
	return NULL;
}

static bool stream_is_streaming(const struct bt_bap_stream *bap_stream)
{
	struct bt_bap_ep_info ep_info;
	int err;

	if (bap_stream == NULL || bap_stream->ep == NULL) {
		return false;
	}

	err = bt_bap_ep_get_info(bap_stream->ep, &ep_info);
	if (err != 0) {
		return false;
	}

	return ep_info.state == BT_BAP_EP_STATE_STREAMING;
}

/*
 * Deterministic integer PCM pattern: differs by channel and evolves by
 * sequence number.  Full-range int16 values (nonzero energy per frame).
 * No floating point.
 */
static uint32_t bsim_tx_hash_mix(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

static void bsim_tx_fill_pcm(const struct bsim_tx_stream *s, int16_t *pcm, uint8_t channel_sel)
{
	const uint32_t n = (s->cfg.frame_duration_us * s->cfg.freq_hz) / USEC_PER_SEC;

	for (uint32_t i = 0U; i < n; i++) {
		uint32_t v = s->seq_num ^ ((i + 1U) * 747796405U) ^ ((uint32_t)channel_sel << 24);

		v = bsim_tx_hash_mix(v);
		pcm[i] = (int16_t)(v & 0xFFFFU);
	}
}

static bool bsim_tx_encode_sdu(struct bsim_tx_stream *s, struct net_buf *buf)
{
	int16_t pcm[BSIM_TX_MAX_SAMPLES];

	for (uint8_t ch = 0U; ch < s->cfg.chan_count; ch++) {
		uint8_t channel_sel;

		if (s->cfg.chan_count == 2) {
			channel_sel = ch; /* Mode B: L then R */
		} else {
			channel_sel = s->cfg.channel_idx; /* mono / Mode A half */
		}

		bsim_tx_fill_pcm(s, pcm, channel_sel);

		int err = lc3_encode(s->encoder[ch], LC3_PCM_FORMAT_S16, pcm, 1,
				     s->cfg.octets_per_frame, net_buf_tail(buf));

		if (err < 0) {
			LOG_ERR("LC3 encode failed: %d", err);
			return false;
		}
		buf->len += s->cfg.octets_per_frame;
	}
	return true;
}

static void tx_thread_func(void *arg1, void *arg2, void *arg3)
{
	NET_BUF_POOL_FIXED_DEFINE(tx_pool, CONFIG_BT_ISO_TX_BUF_COUNT,
				  BT_ISO_SDU_BUF_SIZE(CONFIG_BT_ISO_TX_MTU),
				  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

	while (true) {
		bool sent_any = false;

		for (size_t i = 0U; i < ARRAY_SIZE(tx_streams); i++) {
			struct bsim_tx_stream *s = &tx_streams[i];

			if (s->bap_stream == NULL || s->paused) {
				continue;
			}
			if (!stream_is_streaming(s->bap_stream)) {
				continue;
			}
			/* Hold sending until the scenario-required stream
			 * count is streaming (Mode A: both). */
			if (bsim_tx_streaming_count() < atomic_load(&required_streaming)) {
				continue;
			}

			struct net_buf *buf = net_buf_alloc(&tx_pool, K_FOREVER);

			net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);

			if (s->inject_pending && s->seq_num == s->inject_at_seq) {
				/* Exactly one malformed one-byte SDU at a
				 * controlled sequence, then resume valid LC3. */
				net_buf_add_u8(buf, 0xAA);
				s->inject_pending = false;
				LOG_INF("TX[%zu]: injected malformed 1-byte SDU at seq %u", i,
					s->seq_num);
			} else if (!bsim_tx_encode_sdu(s, buf)) {
				net_buf_unref(buf);
				continue;
			}

			int err = bt_bap_stream_send(s->bap_stream, buf, s->seq_num);

			if (err == 0) {
				s->send_count++;
				s->seq_num++;
				sent_any = true;
				if (s->send_limit > 0U && s->send_count >= s->send_limit) {
					/* Exact send-count cap: pause at the limit. */
					s->paused = true;
				}
			} else {
				LOG_ERR("TX[%zu]: send failed: %d", i, err);
				net_buf_unref(buf);
			}
		}

		/* Never spin: sleep whenever no send succeeded this round
		 * (streams not yet streaming, buffer backpressure, errors). */
		if (!sent_any) {
			k_sleep(K_MSEC(10));
		}
	}
}

int bsim_tx_init(void)
{
	static bool thread_started;

	if (!thread_started) {
		static K_KERNEL_STACK_DEFINE(tx_thread_stack, 4096U);
		static struct k_thread tx_thread;
		const int tx_thread_prio = K_PRIO_PREEMPT(5);

		k_thread_create(&tx_thread, tx_thread_stack, K_KERNEL_STACK_SIZEOF(tx_thread_stack),
				tx_thread_func, NULL, NULL, NULL, tx_thread_prio, 0, K_NO_WAIT);
		k_thread_name_set(&tx_thread, "BSIM TX");
		thread_started = true;
	}
	return 0;
}

int bsim_tx_register(struct bt_bap_stream *bap_stream, const struct bsim_tx_config *cfg)
{
	if (bap_stream == NULL || cfg == NULL) {
		return -EINVAL;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(tx_streams); i++) {
		if (tx_streams[i].bap_stream == NULL) {
			struct bsim_tx_stream *s = &tx_streams[i];

			memset(s, 0, sizeof(*s));
			s->bap_stream = bap_stream;
			s->cfg = *cfg;
			s->seq_num = 0U;

			for (uint8_t ch = 0U; ch < cfg->chan_count; ch++) {
				s->encoder[ch] =
					lc3_setup_encoder(cfg->frame_duration_us, cfg->freq_hz, 0,
							  &s->encoder_mem[ch]);
				if (s->encoder[ch] == NULL) {
					LOG_ERR("TX: encoder setup failed for stream %zu ch %u", i,
						ch);
					s->bap_stream = NULL;
					return -ENOEXEC;
				}
			}

			LOG_INF("TX: registered stream %p (ch=%u octets=%u seq starts at 0)", i,
				bap_stream, cfg->chan_count, cfg->octets_per_frame);
			return 0;
		}
	}

	return -ENOMEM;
}

int bsim_tx_unregister(struct bt_bap_stream *bap_stream)
{
	struct bsim_tx_stream *s = tx_lookup(bap_stream);

	if (s == NULL) {
		return -ENODATA;
	}
	s->bap_stream = NULL;
	return 0;
}

void bsim_tx_pause(struct bt_bap_stream *bap_stream)
{
	struct bsim_tx_stream *s = tx_lookup(bap_stream);

	if (s != NULL) {
		s->paused = true;
	}
}

void bsim_tx_resume(struct bt_bap_stream *bap_stream)
{
	struct bsim_tx_stream *s = tx_lookup(bap_stream);

	if (s != NULL) {
		s->paused = false;
	}
}

void bsim_tx_set_required_streams(int n)
{
	atomic_store(&required_streaming, n);
}

void bsim_tx_schedule_malformed(struct bt_bap_stream *bap_stream, uint16_t at_seq)
{
	struct bsim_tx_stream *s = tx_lookup(bap_stream);

	if (s != NULL) {
		s->inject_pending = true;
		s->inject_at_seq = at_seq;
	}
}

void bsim_tx_set_send_limit(struct bt_bap_stream *bap_stream, uint32_t limit)
{
	struct bsim_tx_stream *s = tx_lookup(bap_stream);

	if (s != NULL) {
		s->send_limit = limit;
	}
}

uint32_t bsim_tx_send_count(struct bt_bap_stream *bap_stream)
{
	struct bsim_tx_stream *s = tx_lookup(bap_stream);

	return (s != NULL) ? s->send_count : 0U;
}

int bsim_tx_streaming_count(void)
{
	int n = 0;

	for (size_t i = 0U; i < ARRAY_SIZE(tx_streams); i++) {
		if (tx_streams[i].bap_stream != NULL &&
		    stream_is_streaming(tx_streams[i].bap_stream)) {
			n++;
		}
	}
	return n;
}
