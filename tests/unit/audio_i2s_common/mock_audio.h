/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Controllable mocks for every production dependency of src/audio_i2s.c.
 *
 * Mocks implement the exact public signatures from the production headers
 * but capture arguments and return configurable results instead of
 * reproducing algorithms.  State is plain globals; tests reset via
 * mock_audio_reset_all() before each case and assert on the capture
 * fields below.
 *
 * ASRC/offload mocks write deterministic PCM patterns into their output
 * buffers so tests can prove exactly which output was queued to I2S.
 */

#ifndef MOCK_AUDIO_H
#define MOCK_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_asrc.h"
#include "audio_perf.h"

/* ── timing ──────────────────────────────────────────────────────── */
extern int mock_timing_init_ret;
extern int mock_timing_init_calls;
extern int mock_timing_reset_calls;

/* ── drift ───────────────────────────────────────────────────────── */
extern int32_t mock_drift_update_ret;
extern int mock_drift_update_calls;
extern int mock_drift_last_slab_free;
extern int mock_drift_reset_calls;

/* ── clock actuator ──────────────────────────────────────────────── */
extern int mock_actuator_init_ret;
extern int mock_actuator_init_calls;
extern int mock_actuator_apply_calls;
extern int32_t mock_actuator_last_ppm;
extern int mock_actuator_reset_calls;

/* ── rate converter ──────────────────────────────────────────────── */
extern int mock_rate_convert_init_calls;
extern uint32_t mock_rate_convert_last_in_rate;
extern uint32_t mock_rate_convert_last_out_rate;
extern size_t mock_rate_convert_next_ret;
extern int mock_rate_convert_next_calls;
extern size_t mock_rate_convert_last_input_frames;
/* Optional per-call return sequence (startup pre-fill uses exactly 14
 * calls).  When seq_len > 0, call i returns seq[i] for i < seq_len, then
 * falls back to mock_rate_convert_next_ret. */
#define MOCK_RATE_CONVERT_SEQ_MAX 16
extern size_t mock_rate_convert_next_seq[MOCK_RATE_CONVERT_SEQ_MAX];
extern int mock_rate_convert_next_seq_len;
extern int mock_rate_convert_next_seq_pos;

/* ── ASRC ────────────────────────────────────────────────────────── */
extern int mock_asrc_init_ret;
extern int mock_asrc_init_calls;
extern int mock_asrc_reset_calls;
extern int mock_asrc_process_ret;
extern size_t mock_asrc_process_consumed;
extern size_t mock_asrc_process_produced;
extern int16_t mock_asrc_process_next_l;
extern int16_t mock_asrc_process_next_r;
extern int16_t mock_asrc_output_pattern;
extern int mock_asrc_process_calls;
extern const int16_t *mock_asrc_last_input;
extern size_t mock_asrc_last_input_frames;
extern int32_t mock_asrc_last_ppm;
extern int16_t mock_asrc_last_prev_l;
extern int16_t mock_asrc_last_prev_r;
extern bool mock_asrc_last_prev_valid;

extern int mock_asrc_state_export_calls;
extern int16_t mock_asrc_last_export_prev_l;
extern int16_t mock_asrc_last_export_prev_r;
extern bool mock_asrc_last_export_prev_valid;
extern uint64_t mock_asrc_last_export_phase; /* read from ctx */
extern uint64_t mock_asrc_last_export_step_base;
extern uint64_t mock_asrc_export_phase; /* sentinel written into dst */
extern uint64_t mock_asrc_export_step_base;

extern int mock_asrc_state_import_calls;
extern int mock_asrc_state_import_ret;
extern uint64_t mock_asrc_import_phase;
extern uint64_t mock_asrc_import_step_base;
extern int16_t mock_asrc_import_prev_l;
extern int16_t mock_asrc_import_prev_r;
extern uint8_t mock_asrc_import_prev_valid;

/* ── offload ASRC ────────────────────────────────────────────────── */
extern int mock_offload_ret;
extern uint16_t mock_offload_output_frames;
extern uint64_t mock_offload_post_phase;
extern uint64_t mock_offload_post_step_base;
extern int16_t mock_offload_post_prev_l;
extern int16_t mock_offload_post_prev_r;
extern uint8_t mock_offload_post_prev_valid;
extern int16_t mock_offload_output_pattern;
extern int mock_offload_calls;
extern const int16_t *mock_offload_last_input;
extern uint16_t mock_offload_last_input_frames;
extern uint32_t mock_offload_last_sequence;
extern int32_t mock_offload_last_ppm;
extern struct audio_asrc_state mock_offload_last_pre_state;
extern uint16_t mock_offload_last_capacity;

/* ── stats ───────────────────────────────────────────────────────── */
extern int mock_stats_underrun_calls;
extern int mock_stats_stream_reset_calls;

/* ── perf ────────────────────────────────────────────────────────── */
extern uint32_t mock_perf_cycle_start_ret;
extern int mock_perf_cycle_start_calls;
extern int mock_perf_cycle_end_calls;
extern enum audio_perf_path mock_perf_last_path;
extern int mock_perf_queue_sample_calls;
extern int mock_perf_last_slab_free;
extern size_t mock_perf_last_output_frames;
extern int mock_perf_push_failure_calls;
extern int mock_perf_repeat_fallback_calls;
extern int mock_perf_asrc_capacity_failure_calls;
extern int mock_perf_i2s_write_failure_calls;
extern int mock_perf_i2s_last_write_errno;
extern int mock_perf_i2s_dma_restart_calls;
extern int mock_perf_rx_callback_start_calls;
extern uint32_t mock_perf_i2s_write_start_ret;
extern int mock_perf_i2s_write_start_calls;
extern int mock_perf_i2s_write_end_calls;
extern uint32_t mock_perf_i2s_last_write_start;
extern bool mock_perf_i2s_last_write_success;
extern int mock_perf_i2s_dma_started_calls;

/** Reset every mock to defaults (zero call counts, zero rets, sentinels). */
void mock_audio_reset_all(void);

#endif /* MOCK_AUDIO_H */
