/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock implementations for every production dependency of src/audio_i2s.c.
 * See mock_audio.h for the capture state and defaults.
 */

#include <string.h>

#include "mock_audio.h"

#include "audio_timing.h"
#include "audio_drift.h"
#include "audio_clock_actuator.h"
#include "audio_rate_convert.h"
#include "audio_asrc.h"
#include "audio_offload.h"
#include "audio_stats.h"
#include "audio_perf.h"

/* ── timing ──────────────────────────────────────────────────────── */
int mock_timing_init_ret;
int mock_timing_init_calls;
int mock_timing_reset_calls;

int audio_timing_init(void)
{
	mock_timing_init_calls++;
	return mock_timing_init_ret;
}

void audio_timing_reset(void)
{
	mock_timing_reset_calls++;
}

/* ── drift ───────────────────────────────────────────────────────── */
int32_t mock_drift_update_ret;
int mock_drift_update_calls;
int mock_drift_last_slab_free;
int mock_drift_reset_calls;

int32_t audio_drift_controller_update(int slab_free_count)
{
	mock_drift_update_calls++;
	mock_drift_last_slab_free = slab_free_count;
	return mock_drift_update_ret;
}

void audio_drift_reset(void)
{
	mock_drift_reset_calls++;
}

/* ── clock actuator ──────────────────────────────────────────────── */
int mock_actuator_init_ret;
int mock_actuator_init_calls;
int mock_actuator_apply_calls;
int32_t mock_actuator_last_ppm;
int mock_actuator_reset_calls;

int audio_clock_actuator_init(void)
{
	mock_actuator_init_calls++;
	return mock_actuator_init_ret;
}

int audio_clock_actuator_apply_ppm(int32_t ppm)
{
	mock_actuator_apply_calls++;
	mock_actuator_last_ppm = ppm;
	return 0;
}

int audio_clock_actuator_reset(void)
{
	mock_actuator_reset_calls++;
	return 0;
}

/* ── rate converter ──────────────────────────────────────────────── */
int mock_rate_convert_init_calls;
uint32_t mock_rate_convert_last_in_rate;
uint32_t mock_rate_convert_last_out_rate;
size_t mock_rate_convert_next_ret;
int mock_rate_convert_next_calls;
size_t mock_rate_convert_last_input_frames;
size_t mock_rate_convert_next_seq[MOCK_RATE_CONVERT_SEQ_MAX];
int mock_rate_convert_next_seq_len;
int mock_rate_convert_next_seq_pos;

void audio_rate_converter_init(struct audio_rate_converter *ctx, uint32_t input_rate_hz,
			       uint32_t output_rate_hz)
{
	mock_rate_convert_init_calls++;
	mock_rate_convert_last_in_rate = input_rate_hz;
	mock_rate_convert_last_out_rate = output_rate_hz;
	ctx->remainder = 0;
	ctx->input_rate_hz = input_rate_hz;
	ctx->output_rate_hz = output_rate_hz;
}

size_t audio_rate_converter_next_frames(struct audio_rate_converter *ctx, size_t input_frames)
{
	(void)ctx;
	mock_rate_convert_next_calls++;
	mock_rate_convert_last_input_frames = input_frames;
	if (mock_rate_convert_next_seq_len > 0 &&
	    mock_rate_convert_next_seq_pos < mock_rate_convert_next_seq_len) {
		return mock_rate_convert_next_seq[mock_rate_convert_next_seq_pos++];
	}
	return mock_rate_convert_next_ret;
}

/* ── ASRC ────────────────────────────────────────────────────────── */
int mock_asrc_init_ret;
int mock_asrc_init_calls;
int mock_asrc_reset_calls;
int mock_asrc_process_ret;
size_t mock_asrc_process_consumed;
size_t mock_asrc_process_produced;
int16_t mock_asrc_process_next_l;
int16_t mock_asrc_process_next_r;
int16_t mock_asrc_output_pattern;
int mock_asrc_process_calls;
const int16_t *mock_asrc_last_input;
size_t mock_asrc_last_input_frames;
int32_t mock_asrc_last_ppm;
int16_t mock_asrc_last_prev_l;
int16_t mock_asrc_last_prev_r;
bool mock_asrc_last_prev_valid;

int mock_asrc_state_export_calls;
int16_t mock_asrc_last_export_prev_l;
int16_t mock_asrc_last_export_prev_r;
bool mock_asrc_last_export_prev_valid;
uint64_t mock_asrc_last_export_phase;
uint64_t mock_asrc_last_export_step_base;
uint64_t mock_asrc_export_phase;
uint64_t mock_asrc_export_step_base;

int mock_asrc_state_import_calls;
int mock_asrc_state_import_ret;
uint64_t mock_asrc_import_phase;
uint64_t mock_asrc_import_step_base;
int16_t mock_asrc_import_prev_l;
int16_t mock_asrc_import_prev_r;
uint8_t mock_asrc_import_prev_valid;

int audio_asrc_init(struct audio_asrc *ctx, uint32_t input_rate_hz, uint32_t output_rate_hz)
{
	(void)input_rate_hz;
	(void)output_rate_hz;
	mock_asrc_init_calls++;
	if (mock_asrc_init_ret != 0) {
		return mock_asrc_init_ret;
	}
	ctx->phase = ASRC_Q32_ONE;
	ctx->step_base = 0;
	return 0;
}

void audio_asrc_reset(struct audio_asrc *ctx)
{
	mock_asrc_reset_calls++;
	/* Minimal continuity semantics so the reset is observable through
	 * the next export (phase returns to first-block state).
	 */
	ctx->phase = ASRC_Q32_ONE;
}

int audio_asrc_process(struct audio_asrc *ctx, const int16_t *input, size_t input_frames,
		       int16_t *output, size_t output_capacity, int32_t correction_ppm,
		       int16_t prev_l, int16_t prev_r, bool prev_valid, size_t *input_consumed,
		       size_t *output_produced, int16_t *next_prev_l, int16_t *next_prev_r)
{
	(void)ctx;
	mock_asrc_process_calls++;
	mock_asrc_last_input = input;
	mock_asrc_last_input_frames = input_frames;
	mock_asrc_last_ppm = correction_ppm;
	mock_asrc_last_prev_l = prev_l;
	mock_asrc_last_prev_r = prev_r;
	mock_asrc_last_prev_valid = prev_valid;

	if (mock_asrc_process_ret != 0) {
		return mock_asrc_process_ret;
	}

	*input_consumed = mock_asrc_process_consumed;
	*output_produced = mock_asrc_process_produced;
	*next_prev_l = mock_asrc_process_next_l;
	*next_prev_r = mock_asrc_process_next_r;

	/* Deterministic output pattern bounded by the real capacity. */
	size_t to_write = mock_asrc_process_produced * 2;

	if (to_write > output_capacity * 2) {
		to_write = output_capacity * 2;
	}
	for (size_t i = 0; i < to_write; i++) {
		output[i] = mock_asrc_output_pattern;
	}
	return 0;
}

void audio_asrc_state_export(const struct audio_asrc *ctx, int16_t prev_l, int16_t prev_r,
			     bool prev_valid, struct audio_asrc_state *dst)
{
	mock_asrc_state_export_calls++;
	mock_asrc_last_export_prev_l = prev_l;
	mock_asrc_last_export_prev_r = prev_r;
	mock_asrc_last_export_prev_valid = prev_valid;
	mock_asrc_last_export_phase = ctx->phase;
	mock_asrc_last_export_step_base = ctx->step_base;

	memset(dst, 0, sizeof(*dst));
	dst->phase = mock_asrc_export_phase;
	dst->step_base = mock_asrc_export_step_base;
	dst->prev_l = prev_l;
	dst->prev_r = prev_r;
	dst->prev_valid = prev_valid ? 1 : 0;
}

int audio_asrc_state_import(struct audio_asrc *ctx, const struct audio_asrc_state *src,
			    int16_t *prev_l_out, int16_t *prev_r_out, bool *prev_valid_out)
{
	mock_asrc_state_import_calls++;
	if (mock_asrc_state_import_ret != 0) {
		return mock_asrc_state_import_ret;
	}
	ctx->phase = mock_asrc_import_phase;
	ctx->step_base = src->step_base;
	*prev_l_out = mock_asrc_import_prev_l;
	*prev_r_out = mock_asrc_import_prev_r;
	*prev_valid_out = mock_asrc_import_prev_valid;
	return 0;
}

/* ── offload ASRC ────────────────────────────────────────────────── */
int mock_offload_ret;
uint16_t mock_offload_output_frames;
uint64_t mock_offload_post_phase;
uint64_t mock_offload_post_step_base;
int16_t mock_offload_post_prev_l;
int16_t mock_offload_post_prev_r;
uint8_t mock_offload_post_prev_valid;
int16_t mock_offload_output_pattern;
int mock_offload_calls;
const int16_t *mock_offload_last_input;
uint16_t mock_offload_last_input_frames;
uint32_t mock_offload_last_sequence;
int32_t mock_offload_last_ppm;
struct audio_asrc_state mock_offload_last_pre_state;
uint16_t mock_offload_last_capacity;

int audio_offload_process_asrc(const int16_t *input, uint16_t input_frames, uint32_t sequence,
			       int32_t correction_ppm, const struct audio_asrc_state *pre_state,
			       int16_t *output, uint16_t output_capacity,
			       struct audio_offload_asrc_result *result)
{
	mock_offload_calls++;
	mock_offload_last_input = input;
	mock_offload_last_input_frames = input_frames;
	mock_offload_last_sequence = sequence;
	mock_offload_last_ppm = correction_ppm;
	mock_offload_last_pre_state = *pre_state;
	mock_offload_last_capacity = output_capacity;

	if (mock_offload_ret != 0) {
		return mock_offload_ret;
	}

	result->output_frames = mock_offload_output_frames;
	result->post_state.phase = mock_offload_post_phase;
	result->post_state.step_base = mock_offload_post_step_base;
	result->post_state.prev_l = mock_offload_post_prev_l;
	result->post_state.prev_r = mock_offload_post_prev_r;
	result->post_state.prev_valid = mock_offload_post_prev_valid;
	result->processing_cycles = 1234;

	/* Emulate a real round trip writing output (bounded by capacity). */
	size_t to_write = mock_offload_output_frames * 2;

	if (to_write > output_capacity * 2) {
		to_write = output_capacity * 2;
	}
	for (size_t i = 0; i < to_write; i++) {
		output[i] = mock_offload_output_pattern;
	}
	return 0;
}

/* ── stats ───────────────────────────────────────────────────────── */
int mock_stats_underrun_calls;
int mock_stats_stream_reset_calls;

void audio_stats_i2s_underrun(void)
{
	mock_stats_underrun_calls++;
}

void audio_stats_stream_reset(void)
{
	mock_stats_stream_reset_calls++;
}

/* ── perf ────────────────────────────────────────────────────────── */
uint32_t mock_perf_cycle_start_ret;
int mock_perf_cycle_start_calls;
int mock_perf_cycle_end_calls;
enum audio_perf_path mock_perf_last_path;
int mock_perf_queue_sample_calls;
int mock_perf_last_slab_free;
size_t mock_perf_last_output_frames;
int mock_perf_push_failure_calls;
int mock_perf_repeat_fallback_calls;
int mock_perf_asrc_capacity_failure_calls;
int mock_perf_i2s_write_failure_calls;
int mock_perf_i2s_last_write_errno;
int mock_perf_i2s_dma_restart_calls;
int mock_perf_rx_callback_start_calls;
uint32_t mock_perf_i2s_write_start_ret;
int mock_perf_i2s_write_start_calls;
int mock_perf_i2s_write_end_calls;
uint32_t mock_perf_i2s_last_write_start;
bool mock_perf_i2s_last_write_success;
int mock_perf_i2s_dma_started_calls;

uint32_t audio_perf_cycle_start(void)
{
	mock_perf_cycle_start_calls++;
	return mock_perf_cycle_start_ret;
}

void audio_perf_cycle_end(uint32_t start, enum audio_perf_path path)
{
	(void)start;
	mock_perf_cycle_end_calls++;
	mock_perf_last_path = path;
}

void audio_perf_queue_sample(int slab_free, size_t output_frames)
{
	mock_perf_queue_sample_calls++;
	mock_perf_last_slab_free = slab_free;
	mock_perf_last_output_frames = output_frames;
}

void audio_perf_push_failure(void)
{
	mock_perf_push_failure_calls++;
}

void audio_perf_repeat_fallback(void)
{
	mock_perf_repeat_fallback_calls++;
}

void audio_perf_asrc_capacity_failure(void)
{
	mock_perf_asrc_capacity_failure_calls++;
}

void audio_perf_i2s_write_failure(int err)
{
	mock_perf_i2s_write_failure_calls++;
	mock_perf_i2s_last_write_errno = err;
}

void audio_perf_i2s_dma_restart(void)
{
	mock_perf_i2s_dma_restart_calls++;
}

void audio_perf_rx_callback_start(uint32_t start)
{
	(void)start;
	mock_perf_rx_callback_start_calls++;
}

void audio_perf_i2s_dma_started(void)
{
	mock_perf_i2s_dma_started_calls++;
}

uint32_t audio_perf_i2s_write_start(void)
{
	mock_perf_i2s_write_start_calls++;
	return mock_perf_i2s_write_start_ret;
}

void audio_perf_i2s_write_end(uint32_t start, bool success)
{
	mock_perf_i2s_write_end_calls++;
	mock_perf_i2s_last_write_start = start;
	mock_perf_i2s_last_write_success = success;
}

/* ── reset ────────────────────────────────────────────────────────── */

void mock_audio_reset_all(void)
{
	mock_timing_init_ret = 0;
	mock_timing_init_calls = 0;
	mock_timing_reset_calls = 0;

	mock_drift_update_ret = 0;
	mock_drift_update_calls = 0;
	mock_drift_last_slab_free = 0;
	mock_drift_reset_calls = 0;

	mock_actuator_init_ret = 0;
	mock_actuator_init_calls = 0;
	mock_actuator_apply_calls = 0;
	mock_actuator_last_ppm = 0;
	mock_actuator_reset_calls = 0;

	mock_rate_convert_init_calls = 0;
	mock_rate_convert_last_in_rate = 0;
	mock_rate_convert_last_out_rate = 0;
	mock_rate_convert_next_ret = 480;
	mock_rate_convert_next_calls = 0;
	mock_rate_convert_last_input_frames = 0;
	memset(mock_rate_convert_next_seq, 0, sizeof(mock_rate_convert_next_seq));
	mock_rate_convert_next_seq_len = 0;
	mock_rate_convert_next_seq_pos = 0;

	mock_asrc_init_ret = 0;
	mock_asrc_init_calls = 0;
	mock_asrc_reset_calls = 0;
	mock_asrc_process_ret = 0;
	mock_asrc_process_consumed = 480;
	mock_asrc_process_produced = 480;
	mock_asrc_process_next_l = 0;
	mock_asrc_process_next_r = 0;
	mock_asrc_output_pattern = 0x1122;
	mock_asrc_process_calls = 0;
	mock_asrc_last_input = NULL;
	mock_asrc_last_input_frames = 0;
	mock_asrc_last_ppm = 0;
	mock_asrc_last_prev_l = 0;
	mock_asrc_last_prev_r = 0;
	mock_asrc_last_prev_valid = false;

	mock_asrc_state_export_calls = 0;
	mock_asrc_last_export_prev_l = 0;
	mock_asrc_last_export_prev_r = 0;
	mock_asrc_last_export_prev_valid = false;
	mock_asrc_last_export_phase = 0;
	mock_asrc_last_export_step_base = 0;
	mock_asrc_export_phase = 0x0102030405060708ULL;
	mock_asrc_export_step_base = 0x1020304050607080ULL;

	mock_asrc_state_import_calls = 0;
	mock_asrc_state_import_ret = 0;
	mock_asrc_import_phase = 0xABCDEF0123456789ULL;
	mock_asrc_import_step_base = 0x1020304050607080ULL;
	mock_asrc_import_prev_l = -11;
	mock_asrc_import_prev_r = 22;
	mock_asrc_import_prev_valid = 1;

	mock_offload_ret = 0;
	mock_offload_output_frames = 480;
	mock_offload_post_phase = 0x123456789ABCDEF0ULL;
	mock_offload_post_step_base = 0x1020304050607080ULL;
	mock_offload_post_prev_l = -33;
	mock_offload_post_prev_r = 44;
	mock_offload_post_prev_valid = 1;
	mock_offload_output_pattern = 0x33CC;
	mock_offload_calls = 0;
	mock_offload_last_input = NULL;
	mock_offload_last_input_frames = 0;
	mock_offload_last_sequence = 0;
	mock_offload_last_ppm = 0;
	memset(&mock_offload_last_pre_state, 0, sizeof(mock_offload_last_pre_state));
	mock_offload_last_capacity = 0;

	mock_stats_underrun_calls = 0;
	mock_stats_stream_reset_calls = 0;

	mock_perf_cycle_start_ret = 0;
	mock_perf_cycle_start_calls = 0;
	mock_perf_cycle_end_calls = 0;
	mock_perf_last_path = 0;
	mock_perf_queue_sample_calls = 0;
	mock_perf_last_slab_free = 0;
	mock_perf_last_output_frames = 0;
	mock_perf_push_failure_calls = 0;
	mock_perf_repeat_fallback_calls = 0;
	mock_perf_asrc_capacity_failure_calls = 0;
	mock_perf_i2s_write_failure_calls = 0;
	mock_perf_i2s_last_write_errno = 0;
	mock_perf_i2s_dma_restart_calls = 0;
	mock_perf_rx_callback_start_calls = 0;
	mock_perf_i2s_write_start_ret = 0;
	mock_perf_i2s_write_start_calls = 0;
	mock_perf_i2s_write_end_calls = 0;
	mock_perf_i2s_last_write_start = 0;
	mock_perf_i2s_last_write_success = false;
	mock_perf_i2s_dma_started_calls = 0;
}
