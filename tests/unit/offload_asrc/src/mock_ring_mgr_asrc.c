/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock transport for offload ASRC unit tests.
 * Provides controllable implementations of flpr_ring_mgr_ produce/consume
 * ASRC functions plus the identity submit path mocks.
 */

#include "flpr_ring_mgr.h"
#include "flpr_handshake.h"
#include "flpr_ring.h"

#include <string.h>
#include <zephyr/kernel.h>

/* ── Mock control variables ────────────────────────────────────────── */

bool mock_init_fails;
bool mock_flpr_healthy;
bool mock_reset_fails;

/* Produce: result */
enum flpr_produce_result mock_produce_result;

/* Notify: result */
int mock_notify_result;

/* Wait: result */
int mock_wait_result;
uint32_t mock_wait_delay_ms;

/* ASRC consume: result + output data */
enum flpr_consume_result mock_consume_result;
uint16_t mock_consume_valid_frames;
uint32_t mock_consume_sequence;
uint32_t mock_consume_crc;
uint32_t mock_consume_latency;
uint8_t mock_consume_payload[1924];
bool mock_consume_corrupt_payload;
bool mock_consume_corrupt_crc;
bool mock_consume_no_asrc_flag;                  /* strip ASRC flag from metadata */
bool mock_consume_bad_post_state;                /* corrupt post-state reserved byte */
int32_t mock_consume_status;                     /* processing_status value */
uint32_t mock_consume_cycles;                    /* processing_cycles value */
struct audio_asrc_state mock_consume_post_state; /* override post-state */
bool mock_consume_step_base_mismatch;            /* return step_base != request */
bool mock_consume_corrupt_correction;            /* return correction_ppm != request */
bool mock_consume_seq_wrong;                     /* return sequence != request */
uint32_t mock_consume_corrupt_seq;               /* wrong sequence to return */

/* ASRC produce: recorded params */
int mock_produce_asrc_calls;
uint32_t mock_produce_asrc_seq;
int32_t mock_produce_asrc_ppm;
struct audio_asrc_state mock_produce_asrc_pre_state;
const int16_t *mock_produce_asrc_data;

/* ASRC consume: recorded call count */
int mock_consume_asrc_calls;

/* Stall */
bool mock_stall_producer_active;

/* ── flpr_handshake mock ──────────────────────────────────────────── */

void flpr_handshake_get_status(struct flpr_status *status)
{
	memset(status, 0, sizeof(*status));
	status->ready = true;
	status->acked = true;
	status->healthy = mock_flpr_healthy;
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
	if (mock_wait_delay_ms > 0) {
		k_msleep(mock_wait_delay_ms);
	}
	return mock_wait_result;
}

enum flpr_consume_result flpr_ring_mgr_consume_block(uint8_t *pcm_out, uint16_t *valid_frames_out,
						     uint32_t *sequence_out, uint32_t *crc32_out,
						     uint32_t *latency_cycles_out)
{
	/* Used by identity submit tests — not the focus here. */
	if (mock_consume_result == FLPR_CONSUME_OK && pcm_out && mock_consume_valid_frames > 0) {
		memcpy(pcm_out, mock_consume_payload, (size_t)mock_consume_valid_frames * 4U);
	}
	if (valid_frames_out) {
		*valid_frames_out = mock_consume_valid_frames;
	}
	if (sequence_out) {
		*sequence_out = mock_consume_sequence;
	}
	if (crc32_out) {
		*crc32_out = mock_consume_crc;
	}
	if (latency_cycles_out) {
		*latency_cycles_out = mock_consume_latency;
	}
	return mock_consume_result;
}

void flpr_ring_mgr_stall_producer(bool stall)
{
	mock_stall_producer_active = stall;
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

/* ── ASRC typed produce/consume mocks ──────────────────────────────── */

enum flpr_produce_result flpr_ring_mgr_produce_asrc(const int16_t *pcm_data, uint16_t valid_frames,
						    uint32_t sequence, int32_t correction_ppm,
						    const struct audio_asrc_state *pre_state)
{
	(void)valid_frames;

	mock_produce_asrc_calls++;
	mock_produce_asrc_seq = sequence;
	mock_produce_asrc_ppm = correction_ppm;
	mock_produce_asrc_data = pcm_data;
	if (pre_state) {
		memcpy(&mock_produce_asrc_pre_state, pre_state, sizeof(*pre_state));
	}

	if (mock_stall_producer_active) {
		return FLPR_PRODUCE_FULL;
	}

	return mock_produce_result;
}

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

	uint16_t vf = mock_consume_valid_frames;
	uint16_t flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;

	if (mock_consume_no_asrc_flag) {
		flags &= ~FLPR_SLOT_FLAG_ASRC_LINEAR;
	}

	/* CRC handling for corruption tests. */
	uint32_t crc_to_use = mock_consume_crc;
	if (mock_consume_corrupt_crc) {
		crc_to_use ^= 0xDEADBEEFU;
	}

	/* Copy payload to output scratch. */
	if (vf > 0 && pcm_out) {
		/* Copy from mock payload. */
		if (mock_consume_corrupt_payload && vf > 0) {
			/* Copy then corrupt one byte. */
			memcpy(pcm_out, mock_consume_payload, (size_t)vf * 4U);
			pcm_out[10] ^= 0xFFFF;
		} else {
			memcpy(pcm_out, mock_consume_payload, (size_t)vf * 4U);
		}
	}

	/* Fill result. */
	result->output_frames = vf;
	result->sequence =
		mock_consume_seq_wrong ? mock_consume_corrupt_seq : mock_consume_sequence;
	result->flags = flags;
	result->correction_ppm = mock_consume_corrupt_correction ? (mock_produce_asrc_ppm + 1)
								 : mock_produce_asrc_ppm;
	/* Recompute CRC over output payload (defense-in-depth). */
	if (vf > 0 && pcm_out) {
		result->payload_crc = flpr_ring_crc32((const uint8_t *)pcm_out, (size_t)vf * 4U);
	} else {
		result->payload_crc = 0;
	}
	result->processing_cycles = mock_consume_cycles;
	result->processing_status = mock_consume_status;
	result->rtt_cycles = mock_consume_latency;

	/* Post-state. */
	if (mock_consume_bad_post_state) {
		/* Corrupt a reserved byte. */
		memcpy(&result->post_state, &mock_consume_post_state, sizeof(result->post_state));
		result->post_state.reserved[1] = 0xCC;
	} else if (mock_consume_step_base_mismatch) {
		/* Return same post_state but with step_base flipped. */
		memcpy(&result->post_state, &mock_consume_post_state, sizeof(result->post_state));
		result->post_state.step_base ^= 0xFFFFU;
	} else {
		memcpy(&result->post_state, &mock_consume_post_state, sizeof(result->post_state));
	}

	return FLPR_CONSUME_OK;
}

/* ── Recovery stubs ─────────────────────────────────────── */

#include "flpr_runtime.h"

void flpr_handshake_register_health_cb(flpr_health_transition_cb_t cb, void *user_data)
{
	(void)cb;
	(void)user_data;
}

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
