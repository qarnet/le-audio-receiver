/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fake bsim_observer for the audio_stream_session unit suite.
 *
 * Provides the same public API as the BSim passive observer so the
 * production session's CONFIG_BSIM_OBSERVER call sites compile and the
 * tests can assert observer-event behavior (malformed-SDU, missing-TS,
 * stale-half, pre-push source validity).  Counter semantics mirror
 * tests/bsim/src/bsim_observer.c.
 */

#include "bsim_observer.h"
#include "fake_observer.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

static atomic_uint malformed_cnt;
static atomic_uint stale_half_cnt;
static atomic_uint missing_ts_cnt;
static atomic_bool last_push_l_valid;
static atomic_bool last_push_r_valid;
static atomic_uint pre_push_cnt;

void fake_observer_reset(void)
{
	atomic_store(&malformed_cnt, 0U);
	atomic_store(&stale_half_cnt, 0U);
	atomic_store(&missing_ts_cnt, 0U);
	atomic_store(&pre_push_cnt, 0U);
	atomic_store(&last_push_l_valid, false);
	atomic_store(&last_push_r_valid, false);
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
	return atomic_load(&last_push_l_valid);
}

bool fake_observer_last_push_r_valid(void)
{
	return atomic_load(&last_push_r_valid);
}

/* ── bsim_observer API (session call sites) ──────────────────────── */

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

void bsim_observer_pre_push(bool l_valid, bool r_valid)
{
	atomic_store(&last_push_l_valid, l_valid);
	atomic_store(&last_push_r_valid, r_valid);
	atomic_fetch_add(&pre_push_cnt, 1U);
}

bool bsim_observer_get_last_push_src_valid(void)
{
	return atomic_load(&last_push_l_valid) && atomic_load(&last_push_r_valid);
}

bool bsim_observer_get_last_push_l_valid(void)
{
	return atomic_load(&last_push_l_valid);
}

bool bsim_observer_get_last_push_r_valid(void)
{
	return atomic_load(&last_push_r_valid);
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
