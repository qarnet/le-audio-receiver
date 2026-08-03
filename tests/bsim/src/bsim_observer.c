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
static atomic_bool last_push_src_valid;
static atomic_bool last_push_l_valid;
static atomic_bool last_push_r_valid;

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

void bsim_observer_pre_push(bool l_valid, bool r_valid)
{
	atomic_store(&last_push_l_valid, l_valid);
	atomic_store(&last_push_r_valid, r_valid);
	atomic_store(&last_push_src_valid, l_valid && r_valid);
}

bool bsim_observer_get_last_push_src_valid(void)
{
	return atomic_load(&last_push_src_valid);
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
