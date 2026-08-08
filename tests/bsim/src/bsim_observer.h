/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM-only passive observer of the production BAP flow.
 *
 * The observer is compiled only into the BSim receiver build
 * (CONFIG_BSIM_OBSERVER).  It emits deterministic event records and
 * counts events from real production code paths; it never drives
 * production state.  Production firmware builds contain no observer
 * symbols.
 */

#ifndef BSIM_OBSERVER_H
#define BSIM_OBSERVER_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>

/** ASE Config accepted/rejected with direction/code/reason. */
void bsim_observer_config(bool accepted, enum bt_audio_dir dir, enum bt_bap_ascs_rsp_code code,
			  enum bt_bap_ascs_reason reason);

/** Audio-path gate opened (all required ASEs started). */
void bsim_observer_gate_open(void);

/** Audio-path gate closed (first close of an open gate). */
void bsim_observer_gate_close(void);

/** Valid-flag SDU rejected before decode (length mismatch). */
void bsim_observer_malformed_sdu(void);

/** Valid SDU received while the audio-path gate is closed. */
void bsim_observer_recv_gate_blocked(void);

/** Mode A older unmatched half discarded. */
void bsim_observer_stale_half(void);

/** Release cleanup completed for a sink slot. */
void bsim_observer_cleanup_release(unsigned int slot);

/** Disconnect cleanup completed. */
void bsim_observer_cleanup_disconnect(void);

/** Release closed an open audio path and stopped the sink immediately. */
void bsim_observer_release_sink_stop(void);

/** Mode A SDU arrived without the ISO TS flag (half skipped). */
void bsim_observer_missing_ts(void);

/**
 * Record the source-validity of the two halves about to be pushed
 * (called by the production receive path immediately before
 * audio_sink_push): mono/Mode B = packet BT_ISO_FLAGS_VALID for both
 * halves; Mode A = each half's own original VALID flag (a PLC half is
 * never source-valid).  Affects only the test oracle, never production
 * state.
 */
void bsim_observer_pre_push(bool l_valid, bool r_valid);

/** Source validity of the most recently recorded pre-push SDU (both
 *  halves valid). */
bool bsim_observer_get_last_push_src_valid(void);

/** Per-half source validity of the most recently recorded pre-push SDU. */
bool bsim_observer_get_last_push_l_valid(void);
bool bsim_observer_get_last_push_r_valid(void);

/* ── queries for the receiver scenario driver ────────────────────── */

uint32_t bsim_observer_get_config_accepted(void);
uint32_t bsim_observer_get_config_rejected(void);
int bsim_observer_get_last_config_dir(void);
int bsim_observer_get_last_config_code(void);
int bsim_observer_get_last_config_reason(void);
/* Last REJECTED config (the last config overall may be a success). */
int bsim_observer_get_last_rej_dir(void);
int bsim_observer_get_last_rej_code(void);
int bsim_observer_get_last_rej_reason(void);
uint32_t bsim_observer_get_gate_open(void);
uint32_t bsim_observer_get_gate_close(void);
uint32_t bsim_observer_get_malformed_sdu(void);
uint32_t bsim_observer_get_recv_gate_blocked(void);
uint32_t bsim_observer_get_stale_half(void);
uint32_t bsim_observer_get_release_cleanup(void);
uint32_t bsim_observer_get_disconnect_cleanup(void);
uint32_t bsim_observer_get_release_sink_stop(void);
uint32_t bsim_observer_get_missing_ts(void);

/* Monotonic event sequence numbers for ordering proofs. */
uint32_t bsim_observer_get_event_seq(void);
uint32_t bsim_observer_get_release_sink_stop_seq(void);
uint32_t bsim_observer_get_disconnect_seq(void);

#endif /* BSIM_OBSERVER_H */
