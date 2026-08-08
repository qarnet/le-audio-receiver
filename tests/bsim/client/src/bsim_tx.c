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
 *
 * Cross-thread ownership protocol (scenario thread vs TX thread):
 *
 *  - One mutex (tx_lock) protects every tx_streams field access.  It is
 *    never held across net_buf_alloc or bt_bap_stream_send (blocking).
 *  - A candidate slot snapshot is taken under the lock and increments
 *    that slot's in_flight counter, recording generation, stream, and
 *    sequence.  Every path after the unlock (encode failure, send
 *    failure, success, stale registration) decrements in_flight; send
 *    counters/sequence/injection are committed only if the generation
 *    and stream still match the snapshot.
 *  - register() takes the lock and selects only a slot with
 *    bap_stream == NULL and in_flight == 0; the config and encoders are
 *    initialized while protected, the generation is bumped to a nonzero
 *    value, and bap_stream is published last.  The only memset happens
 *    while in_flight == 0 under the lock, so it can never race an
 *    in-flight encode/send, and the generation is reassigned afterwards.
 *  - unregister() clears bap_stream and bumps the generation under the
 *    lock, then waits (without holding the lock) until in_flight == 0.
 *  - pause() sets paused under the lock then waits for in_flight == 0;
 *    resume() is synchronized under the lock.
 *  - The TX thread is the sole mutator of the encoder state while a slot
 *    is in flight; register() cannot touch a slot with in_flight > 0.
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

#define BSIM_TX_IDLE_WAIT_MS 1000U

struct bsim_tx_stream {
	struct bt_bap_stream *bap_stream;
	struct bsim_tx_config cfg;
	lc3_encoder_t encoder[2];
	lc3_encoder_mem_48k_t encoder_mem[2];
	uint16_t seq_num;
	uint32_t send_count;
	uint32_t send_limit; /* 0 = unlimited */
	uint32_t generation; /* bumped on register/unregister; 0 = never used */
	uint32_t in_flight;  /* TX candidates currently past the snapshot */
	bool paused;
	bool inject_pending;
	uint16_t inject_at_seq;
};

static struct bsim_tx_stream tx_streams[BSIM_TX_MAX_STREAMS];
static atomic_int required_streaming = 1;

static K_MUTEX_DEFINE(tx_lock);

/* Caller must hold tx_lock. */
static int bsim_tx_streaming_count_locked(void);

static struct bsim_tx_stream *tx_lookup_locked(const struct bt_bap_stream *bap_stream)
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
			struct bt_bap_stream *stream;
			uint16_t seq;
			bool inject;
			uint32_t gen;

			/* Candidate snapshot under the lock: increment
			 * in_flight and record generation/stream/seq.  The
			 * lock is never held across alloc/encode/send. */
			k_mutex_lock(&tx_lock, K_FOREVER);
			if (s->bap_stream == NULL || s->paused) {
				k_mutex_unlock(&tx_lock);
				continue;
			}
			if (!stream_is_streaming(s->bap_stream)) {
				k_mutex_unlock(&tx_lock);
				continue;
			}
			/* Hold sending until the scenario-required stream
			 * count is streaming (Mode A: both). */
			if (bsim_tx_streaming_count_locked() < atomic_load(&required_streaming)) {
				k_mutex_unlock(&tx_lock);
				continue;
			}
			stream = s->bap_stream;
			seq = s->seq_num;
			inject = s->inject_pending && s->seq_num == s->inject_at_seq;
			gen = s->generation;

			s->in_flight++;
			k_mutex_unlock(&tx_lock);

			/* Build the SDU without the lock (encoder state is
			 * TX-thread-owned while in_flight > 0; register
			 * cannot touch this slot). */
			struct net_buf *buf = net_buf_alloc(&tx_pool, K_FOREVER);

			net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);

			if (inject) {
				/* Exactly one malformed SDU at a controlled
				 * sequence, then resume valid LC3.  The ISO
				 * stack drops a 1-byte SDU before the BAP
				 * callback (never observed receiver-side), so
				 * the malformed SDU is one byte short of the
				 * configured shape (119 of 120) — still a
				 * wrong-length SDU for the receiver's exact
				 * payload validation. */
				for (int j = 0; j < (int)s->cfg.octets_per_frame - 1; j++) {
					net_buf_add_u8(buf, (uint8_t)(0x40 + j));
				}
				LOG_INF("TX[%zu]: injected malformed %u-byte SDU at seq %u", i,
					s->cfg.octets_per_frame - 1U, seq);
			} else if (!bsim_tx_encode_sdu(s, buf)) {
				/* Encode failure: decrement in_flight, no commit. */
				k_mutex_lock(&tx_lock, K_FOREVER);
				s->in_flight--;
				k_mutex_unlock(&tx_lock);
				net_buf_unref(buf);
				continue;
			}

			int err = bt_bap_stream_send(stream, buf, seq);

			if (err == 0) {
				sent_any = true;
			} else {
				LOG_ERR("TX[%zu]: send failed: %d", i, err);
			}

			/* Commit under the mutex only if the generation and
			 * stream still match the snapshot; decrement
			 * in_flight on every path. */
			k_mutex_lock(&tx_lock, K_FOREVER);
			if (s->generation == gen && s->bap_stream == stream) {
				if (err == 0) {
					s->send_count++;
					s->seq_num++;
					if (inject) {
						s->inject_pending = false;
					}
					if (s->send_limit > 0U && s->send_count >= s->send_limit) {
						/* Exact send-count cap: pause at the limit. */
						s->paused = true;
					}
				}
			}
			s->in_flight--;
			k_mutex_unlock(&tx_lock);

			if (err != 0) {
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

	k_mutex_lock(&tx_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(tx_streams); i++) {
		/* Select only an empty slot with no in-flight TX: the
		 * memset and encoder setup below can never race a TX that
		 * already passed its candidate snapshot. */
		if (tx_streams[i].bap_stream == NULL && tx_streams[i].in_flight == 0U) {
			struct bsim_tx_stream *s = &tx_streams[i];

			memset(s, 0, sizeof(*s));
			s->cfg = *cfg;
			s->seq_num = 0U;
			/* Reassign a nonzero generation so any stale TX
			 * snapshot from a previous registration fails the
			 * commit check. */
			s->generation++;

			for (uint8_t ch = 0U; ch < cfg->chan_count; ch++) {
				s->encoder[ch] =
					lc3_setup_encoder(cfg->frame_duration_us, cfg->freq_hz, 0,
							  &s->encoder_mem[ch]);
				if (s->encoder[ch] == NULL) {
					LOG_ERR("TX: encoder setup failed for slot %zu ch %u", i,
						ch);
					k_mutex_unlock(&tx_lock);
					return -ENOEXEC;
				}
			}

			/* Publish the stream pointer last. */
			s->bap_stream = bap_stream;

			LOG_INF("TX: registered slot %zu stream %p (ch=%u octets=%u "
				"seq starts at 0)",
				i, bap_stream, cfg->chan_count, cfg->octets_per_frame);
			k_mutex_unlock(&tx_lock);
			return 0;
		}
	}
	k_mutex_unlock(&tx_lock);

	return -ENOMEM;
}

/* Wait (without holding tx_lock) until the slot has no in-flight TX. */
static int tx_wait_idle(struct bsim_tx_stream *s)
{
	uint32_t waited = 0U;

	while (true) {
		k_mutex_lock(&tx_lock, K_FOREVER);
		bool busy = s->in_flight != 0U;

		k_mutex_unlock(&tx_lock);
		if (!busy) {
			return 0;
		}
		if (waited >= BSIM_TX_IDLE_WAIT_MS) {
			LOG_ERR("TX: slot busy for %u ms", BSIM_TX_IDLE_WAIT_MS);
			return -EBUSY;
		}
		k_sleep(K_MSEC(1));
		waited++;
	}
}

int bsim_tx_unregister(struct bt_bap_stream *bap_stream)
{
	k_mutex_lock(&tx_lock, K_FOREVER);
	struct bsim_tx_stream *s = tx_lookup_locked(bap_stream);

	if (s == NULL) {
		k_mutex_unlock(&tx_lock);
		return -ENODATA;
	}
	/* Clear the pointer and bump the generation under the lock so any
	 * in-flight TX fails its commit check; then wait for it to drain
	 * without holding the lock. */
	s->bap_stream = NULL;
	s->generation++;
	k_mutex_unlock(&tx_lock);

	return tx_wait_idle(s);
}

void bsim_tx_pause(struct bt_bap_stream *bap_stream)
{
	k_mutex_lock(&tx_lock, K_FOREVER);
	struct bsim_tx_stream *s = tx_lookup_locked(bap_stream);

	if (s != NULL) {
		s->paused = true;
	}
	k_mutex_unlock(&tx_lock);

	if (s != NULL) {
		(void)tx_wait_idle(s);
	}
}

void bsim_tx_resume(struct bt_bap_stream *bap_stream)
{
	k_mutex_lock(&tx_lock, K_FOREVER);
	struct bsim_tx_stream *s = tx_lookup_locked(bap_stream);

	if (s != NULL) {
		s->paused = false;
	}
	k_mutex_unlock(&tx_lock);
}

void bsim_tx_set_required_streams(int n)
{
	atomic_store(&required_streaming, n);
}

void bsim_tx_schedule_malformed(struct bt_bap_stream *bap_stream, uint16_t at_seq)
{
	k_mutex_lock(&tx_lock, K_FOREVER);
	struct bsim_tx_stream *s = tx_lookup_locked(bap_stream);

	if (s != NULL) {
		s->inject_pending = true;
		s->inject_at_seq = at_seq;
	}
	k_mutex_unlock(&tx_lock);
}

void bsim_tx_set_send_limit(struct bt_bap_stream *bap_stream, uint32_t limit)
{
	k_mutex_lock(&tx_lock, K_FOREVER);
	struct bsim_tx_stream *s = tx_lookup_locked(bap_stream);

	if (s != NULL) {
		s->send_limit = limit;
	}
	k_mutex_unlock(&tx_lock);
}

uint32_t bsim_tx_send_count(struct bt_bap_stream *bap_stream)
{
	k_mutex_lock(&tx_lock, K_FOREVER);
	struct bsim_tx_stream *s = tx_lookup_locked(bap_stream);
	uint32_t count = (s != NULL) ? s->send_count : 0U;

	k_mutex_unlock(&tx_lock);
	return count;
}

/* Caller must hold tx_lock. */
static int bsim_tx_streaming_count_locked(void)
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

int bsim_tx_streaming_count(void)
{
	k_mutex_lock(&tx_lock, K_FOREVER);
	int n = bsim_tx_streaming_count_locked();

	k_mutex_unlock(&tx_lock);
	return n;
}
