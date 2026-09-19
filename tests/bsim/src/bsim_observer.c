/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM-only passive observer — implementation.
 *
 * See bsim_observer.h.  All counters are atomic; the receiver scenario
 * driver reads them through the query functions.  Event records are
 * printed with the "OBS" prefix for human-readable log inspection; the
 * strict gate parses the receiver PASS record (which carries the same
 * values) instead of these lines.
 */

#include "bsim_observer.h"

#include <stdatomic.h>
#include <string.h>

#include <zephyr/sys/printk.h>

static atomic_uint cfg_accepted;
static atomic_uint cfg_rejected;
static atomic_uint gate_open_cnt;
static atomic_uint gate_close_cnt;
static atomic_uint malformed_sdu_cnt;
static atomic_uint recv_gate_blocked_cnt;
static atomic_uint stale_half_cnt;
static atomic_uint release_cleanup_cnt;
static atomic_uint disconnect_cleanup_cnt;
static atomic_int last_dir;
static atomic_int last_code;
static atomic_int last_reason;
static atomic_int last_rej_dir;
static atomic_int last_rej_code;
static atomic_int last_rej_reason;

static atomic_uint event_seq;
static atomic_uint rel_ss_cnt;
static atomic_uint rel_ss_seq;
static atomic_uint mts_cnt;
static atomic_uint disc_seq;
static struct bsim_observer_push push_snapshot;
static bool push_ready;

static uint32_t obs_next_event(void)
{
	return atomic_fetch_add(&event_seq, 1);
}

void bsim_observer_config(bool accepted, enum bt_audio_dir dir, enum bt_bap_ascs_rsp_code code,
			  enum bt_bap_ascs_reason reason)
{
	if (accepted) {
		atomic_fetch_add(&cfg_accepted, 1);
	} else {
		atomic_fetch_add(&cfg_rejected, 1);
	}
	atomic_store(&last_dir, (int)dir);
	atomic_store(&last_code, (int)code);
	atomic_store(&last_reason, (int)reason);
	(void)obs_next_event();
	if (!accepted) {
		atomic_store(&last_rej_dir, (int)dir);
		atomic_store(&last_rej_code, (int)code);
		atomic_store(&last_rej_reason, (int)reason);
	}
	printk("OBS config %s dir=%u code=0x%02x reason=0x%02x\n", accepted ? "ok" : "rej",
	       (unsigned int)dir, (unsigned int)code, (unsigned int)reason);
}

void bsim_observer_gate_open(void)
{
	(void)obs_next_event();
	atomic_fetch_add(&gate_open_cnt, 1);
	printk("OBS gate open\n");
}

void bsim_observer_gate_close(void)
{
	(void)obs_next_event();
	atomic_fetch_add(&gate_close_cnt, 1);
	printk("OBS gate close\n");
}

void bsim_observer_malformed_sdu(void)
{
	(void)obs_next_event();
	atomic_fetch_add(&malformed_sdu_cnt, 1);
	printk("OBS malformed sdu\n");
}

void bsim_observer_recv_gate_blocked(void)
{
	(void)obs_next_event();
	atomic_fetch_add(&recv_gate_blocked_cnt, 1);
	printk("OBS recv gate blocked\n");
}

void bsim_observer_stale_half(void)
{
	(void)obs_next_event();
	atomic_fetch_add(&stale_half_cnt, 1);
	printk("OBS stale half\n");
}

void bsim_observer_cleanup_release(unsigned int slot)
{
	(void)obs_next_event();
	atomic_fetch_add(&release_cleanup_cnt, 1);
	printk("OBS release cleanup slot=%u\n", slot);
}

void bsim_observer_cleanup_disconnect(void)
{
	atomic_store(&disc_seq, obs_next_event());
	atomic_fetch_add(&disconnect_cleanup_cnt, 1);
	printk("OBS disconnect cleanup\n");
}

void bsim_observer_release_sink_stop(void)
{
	atomic_store(&rel_ss_seq, obs_next_event());
	atomic_fetch_add(&rel_ss_cnt, 1);
	printk("OBS release sink stop\n");
}

void bsim_observer_missing_ts(void)
{
	(void)obs_next_event();
	atomic_fetch_add(&mts_cnt, 1);
	printk("OBS missing ts\n");
}

static bool push_half_well_formed(bool valid, const uint8_t *payload, size_t payload_len)
{
	if (valid) {
		return payload != NULL && payload_len > 0U &&
		       payload_len <= BSIM_OBSERVER_MAX_PAYLOAD_BYTES;
	}

	return payload == NULL && payload_len == 0U;
}

void bsim_observer_pre_push(bool l_valid, const uint8_t *l_payload, size_t l_payload_len,
			    bool r_valid, const uint8_t *r_payload, size_t r_payload_len)
{
	/* Clear readiness first so malformed metadata cannot leave an earlier
	 * snapshot available to a later sink push. RX and sink execute in one
	 * serialized call stack, so no lock is required. */
	push_ready = false;
	memset(&push_snapshot, 0, sizeof(push_snapshot));

	if (!push_half_well_formed(l_valid, l_payload, l_payload_len) ||
	    !push_half_well_formed(r_valid, r_payload, r_payload_len)) {
		return;
	}

	push_snapshot.left.source_valid = l_valid;
	push_snapshot.left.payload_len = (uint16_t)l_payload_len;
	push_snapshot.right.source_valid = r_valid;
	push_snapshot.right.payload_len = (uint16_t)r_payload_len;
	if (l_valid) {
		memcpy(push_snapshot.left.payload, l_payload, l_payload_len);
	}
	if (r_valid) {
		memcpy(push_snapshot.right.payload, r_payload, r_payload_len);
	}
	push_ready = true;
}

bool bsim_observer_take_push(struct bsim_observer_push *out)
{
	if (out == NULL || !push_ready) {
		return false;
	}

	*out = push_snapshot;
	push_ready = false;
	return true;
}

uint32_t bsim_observer_get_config_accepted(void)
{
	return atomic_load(&cfg_accepted);
}

uint32_t bsim_observer_get_config_rejected(void)
{
	return atomic_load(&cfg_rejected);
}

int bsim_observer_get_last_config_dir(void)
{
	return atomic_load(&last_dir);
}

int bsim_observer_get_last_config_code(void)
{
	return atomic_load(&last_code);
}

int bsim_observer_get_last_config_reason(void)
{
	return atomic_load(&last_reason);
}

int bsim_observer_get_last_rej_dir(void)
{
	return atomic_load(&last_rej_dir);
}

int bsim_observer_get_last_rej_code(void)
{
	return atomic_load(&last_rej_code);
}

int bsim_observer_get_last_rej_reason(void)
{
	return atomic_load(&last_rej_reason);
}

uint32_t bsim_observer_get_gate_open(void)
{
	return atomic_load(&gate_open_cnt);
}

uint32_t bsim_observer_get_gate_close(void)
{
	return atomic_load(&gate_close_cnt);
}

uint32_t bsim_observer_get_malformed_sdu(void)
{
	return atomic_load(&malformed_sdu_cnt);
}

uint32_t bsim_observer_get_recv_gate_blocked(void)
{
	return atomic_load(&recv_gate_blocked_cnt);
}

uint32_t bsim_observer_get_stale_half(void)
{
	return atomic_load(&stale_half_cnt);
}

uint32_t bsim_observer_get_release_cleanup(void)
{
	return atomic_load(&release_cleanup_cnt);
}

uint32_t bsim_observer_get_disconnect_cleanup(void)
{
	return atomic_load(&disconnect_cleanup_cnt);
}

uint32_t bsim_observer_get_release_sink_stop(void)
{
	return atomic_load(&rel_ss_cnt);
}

uint32_t bsim_observer_get_missing_ts(void)
{
	return atomic_load(&mts_cnt);
}

uint32_t bsim_observer_get_event_seq(void)
{
	return atomic_load(&event_seq);
}

uint32_t bsim_observer_get_release_sink_stop_seq(void)
{
	return atomic_load(&rel_ss_seq);
}

uint32_t bsim_observer_get_disconnect_seq(void)
{
	return atomic_load(&disc_seq);
}
