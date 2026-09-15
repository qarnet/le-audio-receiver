/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM deterministic multi-channel TX — implementation.
 *
 * One TX thread round-robins over registered streams.  A stream sends
 * only while its endpoint is in the streaming state, and only when the
 * scenario-required stream count is streaming (Mode A holds both).
 * Frames come from fixed, checked-in LC3 corpus files.  The selected corpus
 * frame depends only on validated codec geometry, channel, and logical
 * sequence number.
 *
 * Cross-thread ownership protocol (scenario thread vs TX thread):
 *
 *  - One mutex (tx_lock) protects every tx_streams and tx_audits field
 *    access. It is never held across net_buf_alloc or bt_bap_stream_send
 *    (blocking).
 *  - A candidate slot snapshot is taken under the lock and increments
 *    that slot's in_flight counter, recording generation, stream, and
 *    sequence.  Every path after the unlock (encode failure, send
 *    failure, success, stale registration) decrements in_flight; send
 *    counters/sequence/injection are committed only if the generation
 *    and stream still match the snapshot.
 *  - register() takes the lock and selects only a slot with
 *    bap_stream == NULL and in_flight == 0; the config and retained audit
 *    association plus exact-cap semaphore are initialized while protected,
 *    the generation is bumped to a nonzero value, and bap_stream is published
 *    last. The only memset happens while in_flight == 0 under the lock, so it
 *    can never race an in-flight build/send, and the generation is reassigned
 *    afterwards.
 *  - unregister() clears bap_stream and bumps the generation under the
 *    lock, then waits (without holding the lock) until in_flight == 0.
 *  - pause() sets paused under the lock then waits for in_flight == 0;
 *    resume() is synchronized under the lock.
 *  - The TX thread is the sole mutator of active sequence/hash state while a
 *    slot is in flight; register() cannot touch a slot with in_flight > 0.
 *  - An exact send cap auto-pauses a stream and signals its binary semaphore
 *    when a successful commit reaches the cap. wait_send_limit() snapshots
 *    slot state, waits without tx_lock, then revalidates association,
 *    generation, limit, count, and paused state.
 */

#include "bsim_tx.h"

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(bsim_tx, LOG_LEVEL_INF);

#define BSIM_TX_CORPUS_FRAMES 128U
#define BSIM_TX_FNV1A_PRIME   UINT32_C(0x01000193)
#define BSIM_TX_IDLE_WAIT_MS  1000U

struct bsim_tx_fixture {
	const uint8_t *left;
	const uint8_t *right;
	uint16_t frame_bytes;
	uint32_t frame_duration_us;
	uint32_t frame_count;
};

static const uint8_t bsim_48k_10ms_120b_l[] = {
#include <bsim_48k_10ms_120b_l.lc3.inc>
};

static const uint8_t bsim_48k_10ms_120b_r[] = {
#include <bsim_48k_10ms_120b_r.lc3.inc>
};

static const uint8_t bsim_48k_7p5ms_90b_l[] = {
#include <bsim_48k_7p5ms_90b_l.lc3.inc>
};

static const uint8_t bsim_48k_7p5ms_90b_r[] = {
#include <bsim_48k_7p5ms_90b_r.lc3.inc>
};

BUILD_ASSERT(sizeof(bsim_48k_10ms_120b_l) == BSIM_TX_CORPUS_FRAMES * 120U,
	     "10 ms left corpus size");
BUILD_ASSERT(sizeof(bsim_48k_10ms_120b_r) == BSIM_TX_CORPUS_FRAMES * 120U,
	     "10 ms right corpus size");
BUILD_ASSERT(sizeof(bsim_48k_7p5ms_90b_l) == BSIM_TX_CORPUS_FRAMES * 90U,
	     "7.5 ms left corpus size");
BUILD_ASSERT(sizeof(bsim_48k_7p5ms_90b_r) == BSIM_TX_CORPUS_FRAMES * 90U,
	     "7.5 ms right corpus size");

static const struct bsim_tx_fixture fixture_48k_10ms_120b = {
	.left = bsim_48k_10ms_120b_l,
	.right = bsim_48k_10ms_120b_r,
	.frame_bytes = 120U,
	.frame_duration_us = 10000U,
	.frame_count = BSIM_TX_CORPUS_FRAMES,
};

static const struct bsim_tx_fixture fixture_48k_7p5ms_90b = {
	.left = bsim_48k_7p5ms_90b_l,
	.right = bsim_48k_7p5ms_90b_r,
	.frame_bytes = 90U,
	.frame_duration_us = 7500U,
	.frame_count = BSIM_TX_CORPUS_FRAMES,
};

struct bsim_tx_audit {
	const struct bt_bap_stream *bap_stream;
	uint32_t send_count;
	uint32_t fnv1a_hash;
};

struct bsim_tx_stream {
	struct bt_bap_stream *bap_stream;
	struct bsim_tx_config cfg;
	const struct bsim_tx_fixture *fixture;
	struct bsim_tx_audit *audit;
	uint16_t seq_num;
	uint32_t send_count;
	uint32_t fnv1a_hash;
	uint32_t send_limit; /* 0 = unlimited */
	uint32_t generation; /* bumped on register/unregister; 0 = never used */
	uint32_t in_flight;  /* TX candidates currently past the snapshot */
	struct k_sem send_limit_reached;
	bool paused;
	bool inject_pending;
	uint16_t inject_at_seq;
};

static struct bsim_tx_stream tx_streams[BSIM_TX_MAX_STREAMS];
static struct bsim_tx_audit tx_audits[BSIM_TX_MAX_STREAMS];
static atomic_int required_streaming = 1;

static K_MUTEX_DEFINE(tx_lock);

/* Caller must hold tx_lock. */
static int bsim_tx_streaming_count_locked(void);

static uint32_t bsim_tx_next_generation(uint32_t generation)
{
	generation++;
	return generation == 0U ? 1U : generation;
}

static struct bsim_tx_stream *tx_lookup_locked(const struct bt_bap_stream *bap_stream)
{
	if (bap_stream == NULL) {
		return NULL;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(tx_streams); i++) {
		if (tx_streams[i].bap_stream == bap_stream) {
			return &tx_streams[i];
		}
	}
	return NULL;
}

static struct bsim_tx_audit *tx_audit_lookup_locked(const struct bt_bap_stream *bap_stream)
{
	if (bap_stream == NULL) {
		return NULL;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(tx_audits); i++) {
		if (tx_audits[i].bap_stream == bap_stream) {
			return &tx_audits[i];
		}
	}

	return NULL;
}

static struct bsim_tx_audit *tx_audit_empty_locked(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(tx_audits); i++) {
		if (tx_audits[i].bap_stream == NULL) {
			return &tx_audits[i];
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

static const struct bsim_tx_fixture *bsim_tx_fixture_for_config(const struct bsim_tx_config *cfg)
{
	if (cfg == NULL || cfg->freq_hz != 48000U) {
		return NULL;
	}

	if (cfg->frame_duration_us == fixture_48k_10ms_120b.frame_duration_us &&
	    cfg->octets_per_frame == fixture_48k_10ms_120b.frame_bytes) {
		return &fixture_48k_10ms_120b;
	}
	if (cfg->frame_duration_us == fixture_48k_7p5ms_90b.frame_duration_us &&
	    cfg->octets_per_frame == fixture_48k_7p5ms_90b.frame_bytes) {
		return &fixture_48k_7p5ms_90b;
	}

	return NULL;
}

static int bsim_tx_validate_config(const struct bsim_tx_config *cfg,
				   const struct bsim_tx_fixture **fixture)
{
	const struct bsim_tx_fixture *selected = bsim_tx_fixture_for_config(cfg);

	if (selected == NULL || (cfg->chan_count != 1U && cfg->chan_count != 2U) ||
	    (cfg->chan_count == 1U && cfg->channel_idx > 1U) ||
	    selected->frame_count < BSIM_TX_CORPUS_FRAMES) {
		return -EINVAL;
	}

	if (fixture != NULL) {
		*fixture = selected;
	}

	return 0;
}

static uint32_t bsim_tx_fnv1a_byte(uint32_t hash, uint8_t byte)
{
	return (hash ^ (uint32_t)byte) * BSIM_TX_FNV1A_PRIME;
}

static uint32_t bsim_tx_fnv1a_sdu(uint32_t hash, uint16_t seq, const struct net_buf *buf)
{
	const uint32_t logical_seq = seq;

	for (size_t i = 0U; i < sizeof(logical_seq); i++) {
		hash = bsim_tx_fnv1a_byte(hash, (uint8_t)(logical_seq >> (i * 8U)));
	}
	for (size_t i = 0U; i < buf->len; i++) {
		hash = bsim_tx_fnv1a_byte(hash, buf->data[i]);
	}

	return hash;
}

static int bsim_tx_build_sdu(const struct bsim_tx_config *cfg,
			     const struct bsim_tx_fixture *fixture, uint16_t seq, bool inject,
			     struct net_buf *buf)
{
	const uint8_t *left;
	size_t sdu_len;

	if (cfg == NULL || fixture == NULL || buf == NULL || seq >= fixture->frame_count) {
		return -EINVAL;
	}

	left = fixture->left + ((size_t)seq * fixture->frame_bytes);
	sdu_len = (size_t)cfg->chan_count * fixture->frame_bytes;

	if (inject) {
		if (cfg->chan_count != 1U || cfg->channel_idx != 0U ||
		    fixture != &fixture_48k_10ms_120b) {
			return -EINVAL;
		}
		sdu_len--;
	}

	if (net_buf_tailroom(buf) < sdu_len) {
		return -EMSGSIZE;
	}

	if (inject) {
		net_buf_add_mem(buf, left, fixture->frame_bytes - 1U);
		return 0;
	}

	if (cfg->chan_count == 1U) {
		const uint8_t *frame =
			cfg->channel_idx == 0U
				? left
				: fixture->right + ((size_t)seq * fixture->frame_bytes);

		net_buf_add_mem(buf, frame, fixture->frame_bytes);
	} else {
		net_buf_add_mem(buf, left, fixture->frame_bytes);
		net_buf_add_mem(buf, fixture->right + ((size_t)seq * fixture->frame_bytes),
				fixture->frame_bytes);
	}

	return 0;
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
			struct bsim_tx_config cfg;
			const struct bsim_tx_fixture *fixture;
			uint16_t seq;
			bool inject;
			uint32_t gen;
			uint32_t previous_hash;

			/* Candidate snapshot under the lock: increment
			 * in_flight and record generation/stream/seq.  The
			 * lock is never held across alloc/build/send. */
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
			if (s->seq_num >= BSIM_TX_CORPUS_FRAMES) {
				const uint16_t exhausted_seq = s->seq_num;

				s->paused = true;
				k_mutex_unlock(&tx_lock);
				LOG_ERR("TX[%zu]: corpus exhausted at seq %u", i, exhausted_seq);
				continue;
			}
			stream = s->bap_stream;
			cfg = s->cfg;
			fixture = s->fixture;
			seq = s->seq_num;
			inject = s->inject_pending && s->seq_num == s->inject_at_seq;
			gen = s->generation;
			previous_hash = s->fnv1a_hash;

			s->in_flight++;
			k_mutex_unlock(&tx_lock);

			/* Build the SDU without the lock (active TX state is
			 * TX-thread-owned while in_flight > 0; register
			 * cannot touch this slot). */
			struct net_buf *buf = net_buf_alloc(&tx_pool, K_FOREVER);
			int err;

			if (buf == NULL) {
				LOG_ERR("TX[%zu]: buffer allocation failed", i);
				k_mutex_lock(&tx_lock, K_FOREVER);
				s->in_flight--;
				k_mutex_unlock(&tx_lock);
				continue;
			}

			net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);
			err = bsim_tx_build_sdu(&cfg, fixture, seq, inject, buf);
			if (err != 0) {
				/* Build failure: decrement in_flight, release the buffer,
				 * and leave count, sequence, and audit unchanged. */
				LOG_ERR("TX[%zu]: SDU build failed: %d", i, err);
				k_mutex_lock(&tx_lock, K_FOREVER);
				s->in_flight--;
				k_mutex_unlock(&tx_lock);
				net_buf_unref(buf);
				continue;
			}

			if (inject) {
				LOG_INF("TX[%zu]: injected malformed %u-byte SDU at seq %u", i,
					(unsigned int)buf->len, seq);
			}

			/* Hash the exact final payload while the caller still owns buf. */
			uint32_t candidate_hash = bsim_tx_fnv1a_sdu(previous_hash, seq, buf);

			err = bt_bap_stream_send(stream, buf, seq);

			if (err == 0) {
				sent_any = true;
			} else if (stream_is_streaming(stream)) {
				LOG_ERR("TX[%zu]: send failed: %d", i, err);
			}

			/* Commit under the mutex only if the generation and
			 * stream still match the snapshot; decrement
			 * in_flight on every path. */
			k_mutex_lock(&tx_lock, K_FOREVER);
			if (s->generation == gen && s->bap_stream == stream) {
				if (err == 0) {
					s->send_count++;
					s->fnv1a_hash = candidate_hash;
					s->seq_num++;
					s->audit->send_count = s->send_count;
					s->audit->fnv1a_hash = candidate_hash;
					if (inject) {
						s->inject_pending = false;
					}
					if (s->send_limit > 0U && s->send_count >= s->send_limit) {
						/* Exact send-count cap: pause at the limit. */
						s->paused = true;
						k_sem_give(&s->send_limit_reached);
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
	const struct bsim_tx_fixture *fixture;
	int err;

	if (bap_stream == NULL || cfg == NULL) {
		return -EINVAL;
	}
	err = bsim_tx_validate_config(cfg, &fixture);
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(tx_streams); i++) {
		/* Select only an empty slot with no in-flight TX: the
		 * memset and retained-audit setup below can never race a TX that
		 * already passed its candidate snapshot. */
		if (tx_streams[i].bap_stream == NULL && tx_streams[i].in_flight == 0U) {
			struct bsim_tx_stream *s = &tx_streams[i];
			struct bsim_tx_audit *audit = tx_audit_lookup_locked(bap_stream);
			const uint32_t generation = bsim_tx_next_generation(s->generation);

			if (audit == NULL) {
				audit = tx_audit_empty_locked();
			}
			if (audit == NULL) {
				k_mutex_unlock(&tx_lock);
				return -ENOMEM;
			}

			memset(s, 0, sizeof(*s));
			k_sem_init(&s->send_limit_reached, 0, 1);
			s->cfg = *cfg;
			s->fixture = fixture;
			s->audit = audit;
			s->seq_num = 0U;
			s->fnv1a_hash = BSIM_TX_FNV1A_OFFSET_BASIS;
			/* Reassign a nonzero generation so any stale TX
			 * snapshot from a previous registration fails the
			 * commit check. */
			s->generation = generation;

			audit->bap_stream = bap_stream;
			audit->send_count = 0U;
			audit->fnv1a_hash = BSIM_TX_FNV1A_OFFSET_BASIS;

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
	s->generation = bsim_tx_next_generation(s->generation);
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
		k_sem_reset(&s->send_limit_reached);
		s->send_limit = limit;
		if (limit > 0U && s->send_count >= limit) {
			s->paused = true;
			k_sem_give(&s->send_limit_reached);
		}
	}
	k_mutex_unlock(&tx_lock);
}

int bsim_tx_wait_send_limit(struct bt_bap_stream *bap_stream, uint32_t timeout_ms)
{
	struct bsim_tx_stream *s;
	uint32_t generation;
	uint32_t limit;
	int err;

	if (bap_stream == NULL || timeout_ms == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);
	s = tx_lookup_locked(bap_stream);
	if (s == NULL || s->send_limit == 0U) {
		k_mutex_unlock(&tx_lock);
		return -ENODATA;
	}
	generation = s->generation;
	limit = s->send_limit;
	if (s->send_count >= limit && s->paused) {
		k_mutex_unlock(&tx_lock);
		return 0;
	}
	k_mutex_unlock(&tx_lock);

	err = k_sem_take(&s->send_limit_reached, K_MSEC(timeout_ms));
	if (err != 0) {
		return -ETIMEDOUT;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);
	if (s->bap_stream != bap_stream || s->generation != generation || s->send_limit != limit ||
	    s->send_count < limit || !s->paused) {
		k_mutex_unlock(&tx_lock);
		return -ESTALE;
	}
	k_mutex_unlock(&tx_lock);

	return 0;
}

uint32_t bsim_tx_send_count(struct bt_bap_stream *bap_stream)
{
	k_mutex_lock(&tx_lock, K_FOREVER);
	struct bsim_tx_audit *audit = tx_audit_lookup_locked(bap_stream);
	uint32_t count = (audit != NULL) ? audit->send_count : 0U;

	k_mutex_unlock(&tx_lock);
	return count;
}

int bsim_tx_result(const struct bt_bap_stream *bap_stream, struct bsim_tx_result *result)
{
	struct bsim_tx_audit *audit;

	if (bap_stream == NULL || result == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);
	audit = tx_audit_lookup_locked(bap_stream);
	if (audit == NULL) {
		k_mutex_unlock(&tx_lock);
		return -ENODATA;
	}

	result->send_count = audit->send_count;
	result->fnv1a_hash = audit->fnv1a_hash;
	k_mutex_unlock(&tx_lock);

	return 0;
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
