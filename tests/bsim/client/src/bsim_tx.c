/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM deterministic multi-channel TX — implementation.
 *
 * Once required streams are active, one TX thread first sends three
 * round-robin prefill SDUs per stream, then emits one shared SDU-interval tick
 * for all registered streaming streams. Every live stream advances its
 * transport packet sequence number (PSN) exactly once per steady-state tick;
 * successful payloads also advance logical corpus sequence. Frames come from
 * fixed, checked-in LC3 corpus files and depend only on validated codec
 * geometry, channel, and logical sequence number.
 *
 * Cross-thread ownership protocol (scenario thread vs TX thread):
 *
 *  - One mutex (tx_lock) protects every tx_streams and tx_audits field
 *    access. It is never held across net_buf_alloc or bt_bap_stream_send
 *    (blocking).
 *  - Each prefill pass and steady-state tick snapshots every live stream under
 *    the lock and increments each slot's in_flight counter, recording
 *    generation, stream, logical sequence, transport PSN, and prefill state.
 *    Every elapsed tick advances transport PSN only when the snapshot still
 *    matches. Logical sequence, send counters/hash, malformed state, gap
 *    arming, and prefill state commit only after a successful matching send.
 *  - register() takes the lock and selects only a slot with
 *    bap_stream == NULL and in_flight == 0; the config and retained audit
 *    association are initialized while protected, the generation is bumped to
 *    a nonzero value, and bap_stream is published last. The only memset
 *    happens while in_flight == 0 under the lock, so it can never race an
 *    in-flight build/send, and the generation is reassigned afterwards.
 *  - unregister() clears bap_stream and bumps the generation under the
 *    lock, then waits (without holding the lock) until in_flight == 0.
 *  - pause() sets paused under the lock then waits for in_flight == 0;
 *    resume() is synchronized under the lock.
 *  - The TX thread is the sole mutator of active logical/transport sequence
 *    and hash state while a slot is in flight; register() cannot touch a slot
 *    with in_flight > 0.
 *  - Each reusable slot has a process-lifetime condition variable. Send-limit
 *    state changes broadcast under tx_lock. wait_send_limit() atomically
 *    releases/reacquires tx_lock while waiting and fully revalidates
 *    association, generation, limit, count, and paused state after wake.
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

#define BSIM_TX_CORPUS_FRAMES      128U
#define BSIM_TX_FNV1A_PRIME        UINT32_C(0x01000193)
#define BSIM_TX_IDLE_WAIT_MS       1000U
#define BSIM_TX_PREFILL_PER_STREAM 3U

BUILD_ASSERT(CONFIG_BT_ISO_TX_BUF_COUNT >= BSIM_TX_MAX_STREAMS * BSIM_TX_PREFILL_PER_STREAM,
	     "ISO TX pool must hold one prefill for every fixture stream");

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
	uint16_t logical_seq;   /* corpus index and FNV logical sequence */
	uint16_t transport_seq; /* HCI ISO packet sequence number */
	uint32_t send_count;
	uint32_t fnv1a_hash;
	uint32_t send_limit; /* 0 = unlimited */
	uint32_t generation; /* bumped on register/unregister; 0 = never used */
	uint32_t in_flight;  /* TX candidates currently past the snapshot */
	uint32_t gap_after_sends;
	uint32_t gap_intervals;
	uint32_t gap_remaining;
	uint8_t prefill_remaining;
	bool paused;
	bool inject_pending;
	bool gap_armed;
	uint16_t inject_at_seq;
};

enum bsim_tx_tick_action {
	BSIM_TX_TICK_SEND,
	BSIM_TX_TICK_PAUSED,
	BSIM_TX_TICK_GAP,
};

struct bsim_tx_tick_candidate {
	struct bsim_tx_stream *slot;
	struct bt_bap_stream *stream;
	struct bsim_tx_config cfg;
	const struct bsim_tx_fixture *fixture;
	uint16_t logical_seq;
	uint16_t transport_seq;
	uint32_t generation;
	uint32_t previous_hash;
	bool inject;
	bool prefill;
	bool exhausted;
	enum bsim_tx_tick_action action;
};

static struct bsim_tx_stream tx_streams[BSIM_TX_MAX_STREAMS];
static struct k_condvar send_limit_changed[BSIM_TX_MAX_STREAMS];
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

static bool bsim_tx_candidate_matches_locked(const struct bsim_tx_tick_candidate *candidate)
{
	return candidate->slot->generation == candidate->generation &&
	       candidate->slot->bap_stream == candidate->stream;
}

/* Caller must hold tx_lock and have met the required streaming threshold. */
static bool bsim_tx_prefill_pending_locked(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(tx_streams); i++) {
		if (tx_streams[i].bap_stream != NULL &&
		    stream_is_streaming(tx_streams[i].bap_stream) &&
		    tx_streams[i].prefill_remaining > 0U) {
			return true;
		}
	}

	return false;
}

static bool bsim_tx_prefill_pending(void)
{
	bool pending = false;

	k_mutex_lock(&tx_lock, K_FOREVER);
	if (bsim_tx_streaming_count_locked() >= atomic_load(&required_streaming)) {
		pending = bsim_tx_prefill_pending_locked();
	}
	k_mutex_unlock(&tx_lock);

	return pending;
}

static bool bsim_tx_prepare_tick(struct bsim_tx_tick_candidate *candidates, size_t *candidate_count,
				 uint32_t *interval_us, bool *prefill_pass)
{
	size_t count = 0U;
	uint32_t duration_us = 0U;
	bool prefill_active;

	k_mutex_lock(&tx_lock, K_FOREVER);
	if (bsim_tx_streaming_count_locked() < atomic_load(&required_streaming)) {
		k_mutex_unlock(&tx_lock);
		return false;
	}
	prefill_active = bsim_tx_prefill_pending_locked();

	for (size_t i = 0U; i < ARRAY_SIZE(tx_streams); i++) {
		struct bsim_tx_stream *s = &tx_streams[i];
		struct bsim_tx_tick_candidate *candidate;

		if (s->bap_stream == NULL || !stream_is_streaming(s->bap_stream)) {
			continue;
		}
		/* Prefill passes submit only streams that still need controller
		 * reservations. This keeps each pass round-robin and avoids
		 * advancing an already primed stream ahead of a new CIS. */
		if (prefill_active && s->prefill_remaining == 0U) {
			continue;
		}

		if (duration_us == 0U) {
			duration_us = s->cfg.frame_duration_us;
		}

		candidate = &candidates[count++];
		candidate->slot = s;
		candidate->stream = s->bap_stream;
		candidate->cfg = s->cfg;
		candidate->fixture = s->fixture;
		candidate->logical_seq = s->logical_seq;
		candidate->transport_seq = s->transport_seq;
		candidate->generation = s->generation;
		candidate->previous_hash = s->fnv1a_hash;
		candidate->inject = s->inject_pending && s->logical_seq == s->inject_at_seq;
		candidate->prefill = prefill_active;
		candidate->exhausted = false;
		candidate->action = BSIM_TX_TICK_SEND;

		if (s->gap_remaining > 0U) {
			candidate->action = BSIM_TX_TICK_GAP;
		} else if (s->paused) {
			candidate->action = BSIM_TX_TICK_PAUSED;
		} else if (s->logical_seq >= BSIM_TX_CORPUS_FRAMES) {
			s->paused = true;
			candidate->exhausted = true;
			candidate->action = BSIM_TX_TICK_PAUSED;
		}
		s->in_flight++;
	}
	k_mutex_unlock(&tx_lock);

	if (count == 0U) {
		return false;
	}

	*candidate_count = count;
	*interval_us = duration_us;
	*prefill_pass = prefill_active;
	return true;
}

static void bsim_tx_tick_omit(const struct bsim_tx_tick_candidate *candidate)
{
	k_mutex_lock(&tx_lock, K_FOREVER);
	if (bsim_tx_candidate_matches_locked(candidate)) {
		if (candidate->action == BSIM_TX_TICK_GAP && candidate->slot->gap_remaining > 0U) {
			candidate->slot->gap_remaining--;
		}
		candidate->slot->transport_seq++;
	}
	candidate->slot->in_flight--;
	k_mutex_unlock(&tx_lock);
}

static bool bsim_tx_tick_commit_send(const struct bsim_tx_tick_candidate *candidate, int err,
				     uint32_t candidate_hash)
{
	const size_t index = (size_t)(candidate->slot - tx_streams);
	bool sent = false;

	k_mutex_lock(&tx_lock, K_FOREVER);
	if (bsim_tx_candidate_matches_locked(candidate)) {
		struct bsim_tx_stream *s = candidate->slot;

		s->transport_seq++;
		if (err == 0) {
			s->send_count++;
			s->fnv1a_hash = candidate_hash;
			s->logical_seq++;
			s->audit->send_count = s->send_count;
			s->audit->fnv1a_hash = candidate_hash;
			if (candidate->prefill && s->prefill_remaining > 0U) {
				s->prefill_remaining--;
			}
			if (candidate->inject) {
				s->inject_pending = false;
			}
			if (s->gap_armed && s->send_count == s->gap_after_sends) {
				s->gap_armed = false;
				s->gap_remaining = s->gap_intervals;
			}
			if (s->send_limit > 0U && s->send_count >= s->send_limit) {
				/* Exact send-count cap: pause payloads at the limit. */
				s->paused = true;
				(void)k_condvar_broadcast(&send_limit_changed[index]);
			}
			sent = true;
		}
	}
	candidate->slot->in_flight--;
	k_mutex_unlock(&tx_lock);

	return sent;
}

static int bsim_tx_tick_send(const struct bsim_tx_tick_candidate *candidate,
			     struct net_buf_pool *tx_pool)
{
	const size_t index = (size_t)(candidate->slot - tx_streams);
	struct net_buf *buf;
	uint32_t candidate_hash;
	bool sent;
	int err;

	buf = net_buf_alloc(tx_pool, K_NO_WAIT);
	if (buf == NULL) {
		LOG_ERR("TX[%zu]: buffer allocation failed at logical %u transport %u", index,
			candidate->logical_seq, candidate->transport_seq);
		bsim_tx_tick_commit_send(candidate, -ENOMEM, 0U);
		return -ENOMEM;
	}

	net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);
	err = bsim_tx_build_sdu(&candidate->cfg, candidate->fixture, candidate->logical_seq,
				candidate->inject, buf);
	if (err != 0) {
		LOG_ERR("TX[%zu]: SDU build failed at logical %u transport %u: %d", index,
			candidate->logical_seq, candidate->transport_seq, err);
		bsim_tx_tick_commit_send(candidate, err, 0U);
		net_buf_unref(buf);
		return err;
	}

	if (candidate->inject) {
		LOG_INF("TX[%zu]: injected malformed %u-byte SDU at logical %u transport %u", index,
			(unsigned int)buf->len, candidate->logical_seq, candidate->transport_seq);
	}

	/* Hash the exact final payload while the caller still owns buf. */
	candidate_hash = bsim_tx_fnv1a_sdu(candidate->previous_hash, candidate->logical_seq, buf);
	err = bt_bap_stream_send(candidate->stream, buf, candidate->transport_seq);
	if (err != 0) {
		LOG_ERR("TX[%zu]: send failed at logical %u transport %u: %d", index,
			candidate->logical_seq, candidate->transport_seq, err);
	}

	sent = bsim_tx_tick_commit_send(candidate, err, candidate_hash);
	if (err != 0) {
		net_buf_unref(buf);
	}

	return err != 0 ? err : (sent ? 0 : -ESTALE);
}

static void tx_thread_func(void *arg1, void *arg2, void *arg3)
{
	NET_BUF_POOL_FIXED_DEFINE(tx_pool, CONFIG_BT_ISO_TX_BUF_COUNT,
				  BT_ISO_SDU_BUF_SIZE(CONFIG_BT_ISO_TX_MTU),
				  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

	while (true) {
		struct bsim_tx_tick_candidate candidates[ARRAY_SIZE(tx_streams)];
		size_t candidate_count;
		uint32_t interval_us;
		bool prefill_pass;
		bool prefill_can_continue;

		if (!bsim_tx_prepare_tick(candidates, &candidate_count, &interval_us,
					  &prefill_pass)) {
			k_sleep(K_MSEC(10));
			continue;
		}
		prefill_can_continue = prefill_pass;

		for (size_t i = 0U; i < candidate_count; i++) {
			if (candidates[i].action == BSIM_TX_TICK_SEND) {
				if (bsim_tx_tick_send(&candidates[i], &tx_pool) != 0) {
					prefill_can_continue = false;
				}
			} else {
				if (candidates[i].exhausted) {
					LOG_ERR("TX[%zu]: corpus exhausted at logical seq %u", i,
						candidates[i].logical_seq);
				}
				bsim_tx_tick_omit(&candidates[i]);
				if (prefill_pass) {
					prefill_can_continue = false;
				}
			}
		}

		if (prefill_can_continue && bsim_tx_prefill_pending()) {
			continue;
		}

		k_sleep(K_USEC(interval_us));
	}
}

int bsim_tx_init(void)
{
	static bool thread_started;

	if (!thread_started) {
		static K_KERNEL_STACK_DEFINE(tx_thread_stack, 4096U);
		static struct k_thread tx_thread;
		const int tx_thread_prio = K_PRIO_PREEMPT(5);
		int err;

		for (size_t i = 0U; i < ARRAY_SIZE(send_limit_changed); i++) {
			err = k_condvar_init(&send_limit_changed[i]);
			if (err != 0) {
				return err;
			}
		}

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
		if (tx_streams[i].bap_stream != NULL &&
		    tx_streams[i].cfg.frame_duration_us != cfg->frame_duration_us) {
			k_mutex_unlock(&tx_lock);
			return -EINVAL;
		}
	}

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
			s->cfg = *cfg;
			s->fixture = fixture;
			s->audit = audit;
			s->logical_seq = 0U;
			s->transport_seq = 0U;
			s->prefill_remaining = BSIM_TX_PREFILL_PER_STREAM;
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
			(void)k_condvar_broadcast(&send_limit_changed[i]);

			LOG_INF("TX: registered slot %zu stream %p (ch=%u octets=%u "
				"logical/transport start at 0)",
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
	size_t index;

	k_mutex_lock(&tx_lock, K_FOREVER);
	struct bsim_tx_stream *s = tx_lookup_locked(bap_stream);

	if (s == NULL) {
		k_mutex_unlock(&tx_lock);
		return -ENODATA;
	}
	index = (size_t)(s - tx_streams);
	/* Clear the pointer and bump the generation under the lock so any
	 * in-flight TX fails its commit check; then wait for it to drain
	 * without holding the lock. */
	s->bap_stream = NULL;
	s->generation = bsim_tx_next_generation(s->generation);
	(void)k_condvar_broadcast(&send_limit_changed[index]);
	k_mutex_unlock(&tx_lock);

	return tx_wait_idle(s);
}

int bsim_tx_forget_result(const struct bt_bap_stream *stream)
{
	if (stream == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&tx_lock, K_FOREVER);
	struct bsim_tx_audit *audit = tx_audit_lookup_locked(stream);

	if (audit == NULL) {
		k_mutex_unlock(&tx_lock);
		return -ENODATA;
	}
	for (size_t i = 0U; i < ARRAY_SIZE(tx_streams); i++) {
		struct bsim_tx_stream *slot = &tx_streams[i];

		if (slot->bap_stream == stream || (slot->audit == audit && slot->in_flight != 0U)) {
			k_mutex_unlock(&tx_lock);
			return -EBUSY;
		}
	}
	for (size_t i = 0U; i < ARRAY_SIZE(tx_streams); i++) {
		if (tx_streams[i].audit == audit) {
			/* The corresponding candidate is inactive and drained. */
			tx_streams[i].audit = NULL;
		}
	}
	memset(audit, 0, sizeof(*audit));
	k_mutex_unlock(&tx_lock);
	return 0;
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

int bsim_tx_schedule_gap(struct bt_bap_stream *bap_stream, uint32_t after_sends, uint32_t intervals)
{
	struct bsim_tx_stream *s;

	if (bap_stream == NULL || after_sends == 0U || intervals == 0U ||
	    after_sends >= BSIM_TX_CORPUS_FRAMES) {
		return -EINVAL;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);
	s = tx_lookup_locked(bap_stream);
	if (s == NULL) {
		k_mutex_unlock(&tx_lock);
		return -ENODATA;
	}
	if (s->gap_armed || s->gap_remaining > 0U) {
		k_mutex_unlock(&tx_lock);
		return -EALREADY;
	}
	if (s->send_count >= after_sends) {
		k_mutex_unlock(&tx_lock);
		return -EINVAL;
	}

	s->gap_armed = true;
	s->gap_after_sends = after_sends;
	s->gap_intervals = intervals;
	k_mutex_unlock(&tx_lock);

	return 0;
}

void bsim_tx_set_send_limit(struct bt_bap_stream *bap_stream, uint32_t limit)
{
	size_t index;

	k_mutex_lock(&tx_lock, K_FOREVER);
	struct bsim_tx_stream *s = tx_lookup_locked(bap_stream);

	if (s != NULL) {
		index = (size_t)(s - tx_streams);
		s->send_limit = limit;
		if (limit > 0U && s->send_count >= limit) {
			s->paused = true;
		}
		(void)k_condvar_broadcast(&send_limit_changed[index]);
	}
	k_mutex_unlock(&tx_lock);
}

int bsim_tx_wait_send_limit(struct bt_bap_stream *bap_stream, uint32_t timeout_ms)
{
	uint32_t generation;
	uint32_t limit;
	size_t index;
	int err;

	if (bap_stream == NULL || timeout_ms == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);
	{
		struct bsim_tx_stream *s = tx_lookup_locked(bap_stream);

		if (s == NULL || s->send_limit == 0U) {
			k_mutex_unlock(&tx_lock);
			return -ENODATA;
		}
		index = (size_t)(s - tx_streams);
		generation = s->generation;
		limit = s->send_limit;
		if (s->send_count >= limit && s->paused) {
			k_mutex_unlock(&tx_lock);
			return 0;
		}
	}

	err = k_condvar_wait(&send_limit_changed[index], &tx_lock, K_MSEC(timeout_ms));
	if (err != 0) {
		k_mutex_unlock(&tx_lock);
		return -ETIMEDOUT;
	}

	{
		struct bsim_tx_stream *s = &tx_streams[index];

		if (s->bap_stream != bap_stream || s->generation != generation ||
		    s->send_limit != limit || s->send_count < limit || !s->paused) {
			k_mutex_unlock(&tx_lock);
			return -ESTALE;
		}
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
