/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fake bsim_observer for the audio_stream_session unit suite.
 */

#include "bsim_observer.h"
#include "fake_observer.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define FAKE_OBSERVER_MAX_PRE_PUSH 32U

static atomic_uint malformed_cnt;
static atomic_uint stale_half_cnt;
static atomic_uint missing_ts_cnt;
static atomic_uint pre_push_cnt;
static struct bsim_observer_push last_push;
static struct bsim_observer_push pre_pushes[FAKE_OBSERVER_MAX_PRE_PUSH];
static atomic_bool push_ready;

static bool push_half_well_formed(bool valid, const uint8_t *payload, size_t payload_len)
{
	if (valid) {
		return payload != NULL && payload_len > 0U &&
		       payload_len <= BSIM_OBSERVER_MAX_PAYLOAD_BYTES;
	}

	return payload == NULL && payload_len == 0U;
}

static const struct bsim_observer_push_half *pre_push_half_at(uint32_t index, bool right)
{
	if (index >= FAKE_OBSERVER_MAX_PRE_PUSH || index >= atomic_load(&pre_push_cnt)) {
		return NULL;
	}

	return right ? &pre_pushes[index].right : &pre_pushes[index].left;
}

void fake_observer_reset(void)
{
	atomic_store(&malformed_cnt, 0U);
	atomic_store(&stale_half_cnt, 0U);
	atomic_store(&missing_ts_cnt, 0U);
	atomic_store(&pre_push_cnt, 0U);
	atomic_store(&push_ready, false);
	memset(&last_push, 0, sizeof(last_push));
	memset(pre_pushes, 0, sizeof(pre_pushes));
}

uint32_t fake_observer_malformed_sdu(void)
{
	return atomic_load(&malformed_cnt);
}

uint32_t fake_observer_stale_half(void)
{
	return atomic_load(&stale_half_cnt);
}

uint32_t fake_observer_missing_ts(void)
{
	return atomic_load(&missing_ts_cnt);
}

uint32_t fake_observer_pre_push_count(void)
{
	return atomic_load(&pre_push_cnt);
}

bool fake_observer_last_push_l_valid(void)
{
	return last_push.left.source_valid;
}

bool fake_observer_last_push_r_valid(void)
{
	return last_push.right.source_valid;
}

uint16_t fake_observer_last_push_l_payload_len(void)
{
	return last_push.left.payload_len;
}

uint16_t fake_observer_last_push_r_payload_len(void)
{
	return last_push.right.payload_len;
}

const uint8_t *fake_observer_last_push_l_payload(void)
{
	return last_push.left.payload;
}

const uint8_t *fake_observer_last_push_r_payload(void)
{
	return last_push.right.payload;
}

bool fake_observer_pre_push_l_valid_at(uint32_t index)
{
	const struct bsim_observer_push_half *half = pre_push_half_at(index, false);

	return half != NULL && half->source_valid;
}

bool fake_observer_pre_push_r_valid_at(uint32_t index)
{
	const struct bsim_observer_push_half *half = pre_push_half_at(index, true);

	return half != NULL && half->source_valid;
}

uint16_t fake_observer_pre_push_l_payload_len_at(uint32_t index)
{
	const struct bsim_observer_push_half *half = pre_push_half_at(index, false);

	return half != NULL ? half->payload_len : 0U;
}

uint16_t fake_observer_pre_push_r_payload_len_at(uint32_t index)
{
	const struct bsim_observer_push_half *half = pre_push_half_at(index, true);

	return half != NULL ? half->payload_len : 0U;
}

const uint8_t *fake_observer_pre_push_l_payload_at(uint32_t index)
{
	const struct bsim_observer_push_half *half = pre_push_half_at(index, false);

	return half != NULL ? half->payload : NULL;
}

const uint8_t *fake_observer_pre_push_r_payload_at(uint32_t index)
{
	const struct bsim_observer_push_half *half = pre_push_half_at(index, true);

	return half != NULL ? half->payload : NULL;
}

/* bsim_observer API used by production session call sites. */

void bsim_observer_config(bool accepted, enum bt_audio_dir dir, enum bt_bap_ascs_rsp_code code,
			  enum bt_bap_ascs_reason reason)
{
	(void)accepted;
	(void)dir;
	(void)code;
	(void)reason;
}

void bsim_observer_gate_open(void)
{
}

void bsim_observer_gate_close(void)
{
}

void bsim_observer_malformed_sdu(void)
{
	atomic_fetch_add(&malformed_cnt, 1U);
}

void bsim_observer_recv_gate_blocked(void)
{
}

void bsim_observer_stale_half(void)
{
	atomic_fetch_add(&stale_half_cnt, 1U);
}

void bsim_observer_cleanup_release(unsigned int slot)
{
	(void)slot;
}

void bsim_observer_cleanup_disconnect(void)
{
}

void bsim_observer_release_sink_stop(void)
{
}

void bsim_observer_missing_ts(void)
{
	atomic_fetch_add(&missing_ts_cnt, 1U);
}

void bsim_observer_pre_push(bool l_valid, const uint8_t *l_payload, size_t l_payload_len,
			    bool r_valid, const uint8_t *r_payload, size_t r_payload_len)
{
	struct bsim_observer_push snapshot = {0};
	const uint32_t index = atomic_load(&pre_push_cnt);

	atomic_store(&push_ready, false);
	if (!push_half_well_formed(l_valid, l_payload, l_payload_len) ||
	    !push_half_well_formed(r_valid, r_payload, r_payload_len)) {
		return;
	}

	snapshot.left.source_valid = l_valid;
	snapshot.left.payload_len = (uint16_t)l_payload_len;
	snapshot.right.source_valid = r_valid;
	snapshot.right.payload_len = (uint16_t)r_payload_len;
	if (l_valid) {
		memcpy(snapshot.left.payload, l_payload, l_payload_len);
	}
	if (r_valid) {
		memcpy(snapshot.right.payload, r_payload, r_payload_len);
	}

	last_push = snapshot;
	if (index < FAKE_OBSERVER_MAX_PRE_PUSH) {
		pre_pushes[index] = snapshot;
	}
	atomic_fetch_add(&pre_push_cnt, 1U);
	atomic_store(&push_ready, true);
}

bool bsim_observer_take_push(struct bsim_observer_push *out)
{
	if (out == NULL || !atomic_exchange(&push_ready, false)) {
		return false;
	}

	*out = last_push;
	return true;
}

uint32_t bsim_observer_get_config_accepted(void)
{
	return 0U;
}

uint32_t bsim_observer_get_config_rejected(void)
{
	return 0U;
}

int bsim_observer_get_last_config_dir(void)
{
	return 0;
}

int bsim_observer_get_last_config_code(void)
{
	return 0;
}

int bsim_observer_get_last_config_reason(void)
{
	return 0;
}

int bsim_observer_get_last_rej_dir(void)
{
	return 0;
}

int bsim_observer_get_last_rej_code(void)
{
	return 0;
}

int bsim_observer_get_last_rej_reason(void)
{
	return 0;
}

uint32_t bsim_observer_get_gate_open(void)
{
	return 0U;
}

uint32_t bsim_observer_get_gate_close(void)
{
	return 0U;
}

uint32_t bsim_observer_get_malformed_sdu(void)
{
	return atomic_load(&malformed_cnt);
}

uint32_t bsim_observer_get_recv_gate_blocked(void)
{
	return 0U;
}

uint32_t bsim_observer_get_stale_half(void)
{
	return atomic_load(&stale_half_cnt);
}

uint32_t bsim_observer_get_release_cleanup(void)
{
	return 0U;
}

uint32_t bsim_observer_get_disconnect_cleanup(void)
{
	return 0U;
}

uint32_t bsim_observer_get_release_sink_stop(void)
{
	return 0U;
}

uint32_t bsim_observer_get_missing_ts(void)
{
	return atomic_load(&missing_ts_cnt);
}

uint32_t bsim_observer_get_event_seq(void)
{
	return 0U;
}

uint32_t bsim_observer_get_release_sink_stop_seq(void)
{
	return 0U;
}

uint32_t bsim_observer_get_disconnect_seq(void)
{
	return 0U;
}
