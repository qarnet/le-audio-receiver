/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock transport for the verify-enabled ASRC offload unit tests.
 *
 * Unlike the plain offload_asrc mock, flpr_ring_mgr_consume_asrc_result()
 * computes the EXACT real CPU ASRC output + exported post-state (the same
 * computation the shadow verification runs), so the exact-match test
 * succeeds bit-for-bit.  Corruption knobs then inject controlled damage at
 * the process_asrc validation boundaries (metadata vs shadow compare).
 */

#include "flpr_ring_mgr.h"
#include "flpr_handshake.h"
#include "flpr_ring.h"
#include "flpr_runtime.h"
#include "audio_asrc.h"

#include <string.h>
#include <zephyr/kernel.h>

/* ── Mock control variables ────────────────────────────────────────── */

bool mock_init_fails;
bool mock_flpr_healthy;
bool mock_reset_fails;

/* Produce: result */
enum flpr_produce_result mock_produce_result;

/* Notify / wait: results */
int mock_notify_result;
int mock_wait_result;

/* Consume: result */
enum flpr_consume_result mock_consume_result;

/* Recorded produce params (the shadow runs the same inputs). */
int mock_produce_asrc_calls;
const int16_t *mock_produce_asrc_data;
uint32_t mock_produce_asrc_seq;
int32_t mock_produce_asrc_ppm;
struct audio_asrc_state mock_produce_asrc_pre_state;

/* Consume: recorded call count */
int mock_consume_asrc_calls;

/* Corruption knobs (all default off). */
bool mock_seq_wrong;                     /* echo wrong sequence (metadata boundary) */
uint32_t mock_corrupt_seq;               /* wrong sequence to echo */
bool mock_corrupt_sample;                /* flip one scratch sample after compute (shadow) */
bool mock_output_frames_override;        /* override output_frames (shadow frame-count compare) */
uint16_t mock_override_frames;           /* value for the override */
bool mock_corrupt_post_state_phase;      /* flip phase in exported post-state (shadow) */
bool mock_corrupt_post_state_prev_l;     /* corrupt prev_l (shadow) */
bool mock_corrupt_post_state_prev_valid; /* corrupt prev_valid (shadow) */
bool mock_corrupt_post_state_reserved;   /* reserved byte (metadata state fault) */
bool mock_corrupt_post_state_step_base;  /* step_base mismatch (metadata state fault) */
bool mock_post_state_step_base_zero;     /* import failure (metadata state fault) */
bool mock_corrupt_correction_oob;        /* echo OOB ppm, compute with 0 (shadow process fail) */
int32_t mock_processing_status;
uint32_t mock_processing_cycles;
uint32_t mock_rtt_cycles;

/* ── flpr_handshake mock ──────────────────────────────────────────── */

void flpr_handshake_get_status(struct flpr_status *status)
{
	memset(status, 0, sizeof(*status));
	status->ready = true;
	status->acked = true;
	status->healthy = mock_flpr_healthy;
}

void flpr_handshake_register_health_cb(flpr_health_transition_cb_t cb, void *user_data)
{
	(void)cb;
	(void)user_data;
}

/* ── flpr_ring_mgr mock (identity path) ────────────────────────────── */

int flpr_ring_mgr_init(void)
{
	if (mock_init_fails) {
		return -EAGAIN;
	}
	return 0;
}

int flpr_ring_mgr_coordinated_reset(uint32_t new_epoch, uint32_t timeout_ms)
{
	(void)timeout_ms;
	if (mock_reset_fails) {
		return -ETIMEDOUT;
	}
	(void)new_epoch;
	return 0;
}

enum flpr_produce_result flpr_ring_mgr_produce_block(const uint8_t *pcm_data, uint16_t vf,
						     uint32_t sequence, int32_t correction_ppm,
						     bool compute_crc)
{
	(void)pcm_data;
	(void)vf;
	(void)sequence;
	(void)correction_ppm;
	(void)compute_crc;
	return mock_produce_result;
}

int flpr_ring_mgr_notify_producer(void)
{
	return mock_notify_result;
}

int flpr_ring_mgr_wait_consume(uint32_t timeout_ms)
{
	(void)timeout_ms;
	return mock_wait_result;
}

enum flpr_consume_result flpr_ring_mgr_consume_block(uint8_t *pcm_out, uint16_t *valid_frames_out,
						     uint32_t *sequence_out, uint32_t *crc32_out,
						     uint32_t *latency_cycles_out)
{
	(void)pcm_out;
	(void)valid_frames_out;
	(void)sequence_out;
	(void)crc32_out;
	(void)latency_cycles_out;
	return mock_consume_result;
}

void flpr_ring_mgr_stall_producer(bool stall)
{
	(void)stall;
}

int flpr_ring_mgr_test_run_rate(uint32_t block_count, uint32_t timeout_ms, uint32_t rate_per_sec,
				struct flpr_ring_status *out)
{
	(void)block_count;
	(void)timeout_ms;
	(void)rate_per_sec;
	if (out) {
		memset(out, 0, sizeof(*out));
	}
	return 0;
}

int flpr_ring_mgr_test_run(uint32_t block_count, uint32_t timeout_ms, struct flpr_ring_status *out)
{
	return flpr_ring_mgr_test_run_rate(block_count, timeout_ms, 0, out);
}

int flpr_ring_mgr_produce_stale_test(uint32_t stale_epoch)
{
	(void)stale_epoch;
	return 0;
}

int flpr_ring_mgr_flpr_stall(uint8_t stall_bits, uint32_t timeout_ms)
{
	(void)stall_bits;
	(void)timeout_ms;
	return 0;
}

int flpr_ring_mgr_flpr_stall_timed(uint8_t stall_bits, uint32_t duration_ms, uint32_t timeout_ms)
{
	(void)stall_bits;
	(void)duration_ms;
	(void)timeout_ms;
	return 0;
}

uint32_t flpr_ring_mgr_flpr_stall_acked(void)
{
	return 0;
}

void flpr_ring_mgr_get_status(struct flpr_ring_status *status)
{
	if (status) {
		memset(status, 0, sizeof(*status));
	}
}

/* ── ASRC typed produce mock ───────────────────────────────────────── */

enum flpr_produce_result flpr_ring_mgr_produce_asrc(const int16_t *pcm_data, uint16_t valid_frames,
						    uint32_t sequence, int32_t correction_ppm,
						    const struct audio_asrc_state *pre_state)
{
	(void)valid_frames;

	mock_produce_asrc_calls++;
	mock_produce_asrc_data = pcm_data;
	mock_produce_asrc_seq = sequence;
	mock_produce_asrc_ppm = correction_ppm;
	if (pre_state) {
		memcpy(&mock_produce_asrc_pre_state, pre_state, sizeof(*pre_state));
	}

	return mock_produce_result;
}

/* ── ASRC typed consume mock: REAL CPU ASRC reference + corruption ──── */

enum flpr_consume_result flpr_ring_mgr_consume_asrc_result(int16_t *pcm_out,
							   uint16_t output_capacity,
							   struct flpr_consume_asrc_result *result)
{
	mock_consume_asrc_calls++;

	if (!pcm_out || !result || output_capacity < 481) {
		return FLPR_CONSUME_INVALID;
	}
	if (mock_consume_result != FLPR_CONSUME_OK) {
		return mock_consume_result;
	}

	/* Compute with a VALID ppm when the request is out of range, so the
	 * payload/post-state are real; the shadow then fails on its own
	 * process call (asrc_ret != 0) while the echo passes. */
	int32_t proc_ppm = mock_produce_asrc_ppm;

	if (mock_corrupt_correction_oob) {
		proc_ppm = 0;
	}

	uint16_t out_frames = 0;
	struct audio_asrc_state post;

	memset(&post, 0, sizeof(post));

	{
		struct audio_asrc ctx;
		int16_t pl = 0, pr = 0;
		bool pv = false;
		int imp =
			audio_asrc_state_import(&ctx, &mock_produce_asrc_pre_state, &pl, &pr, &pv);

		if (imp == 0) {
			size_t consumed = 0, produced = 0;
			int16_t nl = 0, nr = 0;
			int asrc_ret = audio_asrc_process(
				&ctx, mock_produce_asrc_data, FLPR_RING_PAYLOAD_MAX_INPUT, pcm_out,
				FLPR_RING_PAYLOAD_CAPACITY_FRAMES, proc_ppm, pl, pr, pv, &consumed,
				&produced, &nl, &nr);

			if (asrc_ret == 0) {
				out_frames = (uint16_t)produced;
				audio_asrc_state_export(&ctx, nl, nr, true, &post);
			}
		}
	}

	if (mock_corrupt_sample) {
		pcm_out[10] ^= 0xFFFF;
	}

	result->output_frames = mock_output_frames_override ? mock_override_frames : out_frames;
	result->sequence = mock_seq_wrong ? mock_corrupt_seq : mock_produce_asrc_seq;
	result->flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
	result->correction_ppm = mock_corrupt_correction_oob ? 5000 : mock_produce_asrc_ppm;
	result->payload_crc = flpr_ring_crc32((const uint8_t *)pcm_out, (size_t)out_frames * 4U);
	result->processing_cycles = mock_processing_cycles;
	result->processing_status = mock_processing_status;
	result->rtt_cycles = mock_rtt_cycles;

	if (mock_corrupt_post_state_phase) {
		post.phase ^= 0x10U;
	}
	if (mock_corrupt_post_state_prev_l) {
		post.prev_l += 1;
	}
	if (mock_corrupt_post_state_prev_valid) {
		post.prev_valid ^= 1U;
	}
	if (mock_corrupt_post_state_reserved) {
		post.reserved[1] = 0xAA;
	}
	if (mock_corrupt_post_state_step_base) {
		post.step_base += 1;
	}
	if (mock_post_state_step_base_zero) {
		post.step_base = 0;
	}
	result->post_state = post;

	return FLPR_CONSUME_OK;
}

/* ── Stage 4B recovery stubs ───────────────────────────────────────── */

int flpr_runtime_init(void)
{
	return 0;
}

int flpr_runtime_restart(uint32_t timeout_ms)
{
	(void)timeout_ms;
	return 0;
}

void flpr_runtime_get_status(struct flpr_runtime_status *out)
{
	if (out) {
		memset(out, 0, sizeof(*out));
	}
}

int flpr_ring_mgr_remote_restarted(void)
{
	return 0;
}
