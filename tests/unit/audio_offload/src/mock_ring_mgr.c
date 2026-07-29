/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock transport for audio_offload unit tests.
 *
 * Provides controllable implementations of flpr_ring_mgr_* and
 * flpr_handshake_get_status that the test cases manipulate via
 * module-level variables to simulate normal operation, faults,
 * timeouts, CRC corruption, payload mismatch, etc.
 */

#include "flpr_ring_mgr.h"
#include "flpr_handshake.h"
#include "flpr_ring.h"
#include "flpr_runtime.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mock_ring, LOG_LEVEL_INF);

/* ── Mock control variables (set by test before each call) ───────── */

bool mock_init_fails;
bool mock_flpr_healthy;
bool mock_reset_fails;

/* Produce: result, optional delay */
enum flpr_produce_result mock_produce_result;
uint32_t mock_produce_delay_ms;

/* Notify: result */
int mock_notify_result;

/* Wait: result (0 = success, nonzero = timeout) */
int mock_wait_result;
uint32_t mock_wait_delay_ms;

/* Consume: result, plus the data to return */
enum flpr_consume_result mock_consume_result;
uint16_t mock_consume_valid_frames;
uint32_t mock_consume_sequence;
uint32_t mock_consume_crc;
uint32_t mock_consume_latency;
uint8_t mock_consume_payload[1924]; /* FLPR_RING_PAYLOAD_CAPACITY_BYTES */
bool mock_consume_corrupt_payload;  /* flip one byte */
bool mock_consume_corrupt_crc;      /* flip CRC */

/* Stall */
bool mock_stall_producer_active;

/* Test counters so tests can observe what was called. */
int mock_init_calls;
int mock_reset_calls;
int mock_produce_calls;
int mock_notify_calls;
int mock_wait_calls;
int mock_consume_calls;
uint32_t mock_last_sequence;
uint32_t mock_last_epoch;
const uint8_t *mock_last_pcm; /* last PCM ptr passed to produce */
bool mock_last_crc;           /* was crc computed */

/* Reset everything to defaults. */
static void mock_asrc_reset(void);
static void mock_recovery_reset(void);

static void mock_reset(void)
{
	mock_init_fails = false;
	mock_flpr_healthy = true;
	mock_reset_fails = false;
	mock_produce_result = FLPR_PRODUCE_OK;
	mock_produce_delay_ms = 0;
	mock_notify_result = 0;
	mock_wait_result = 0;
	mock_wait_delay_ms = 0;
	mock_consume_result = FLPR_CONSUME_OK;
	mock_consume_valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
	mock_consume_sequence = 0;
	mock_consume_crc = 0;
	mock_consume_latency = 500;
	mock_consume_corrupt_payload = false;
	mock_consume_corrupt_crc = false;
	mock_stall_producer_active = false;
	mock_init_calls = 0;
	mock_reset_calls = 0;
	mock_produce_calls = 0;
	mock_notify_calls = 0;
	mock_wait_calls = 0;
	mock_consume_calls = 0;
	mock_last_sequence = 0;
	mock_last_epoch = 0;
	mock_last_pcm = NULL;
	mock_last_crc = false;
	memset(mock_consume_payload, 0, sizeof(mock_consume_payload));
	mock_asrc_reset();
	mock_recovery_reset();
}

/* ── flpr_handshake mock ─────────────────────────────────────────── */

void flpr_handshake_get_status(struct flpr_status *status)
{
	memset(status, 0, sizeof(*status));
	status->ready = true;
	status->acked = true;
	status->healthy = mock_flpr_healthy;
}

/* ── flpr_ring_mgr mock implementations ──────────────────────────── */

int flpr_ring_mgr_init(void)
{
	mock_init_calls++;
	if (mock_init_fails) {
		return -EAGAIN;
	}
	return 0;
}

int flpr_ring_mgr_coordinated_reset(uint32_t new_epoch, uint32_t timeout_ms)
{
	(void)timeout_ms;
	mock_reset_calls++;
	mock_last_epoch = new_epoch;
	if (mock_reset_fails) {
		return -ETIMEDOUT;
	}
	return 0;
}

enum flpr_produce_result flpr_ring_mgr_produce_block(const uint8_t *pcm_data, uint16_t valid_frames,
						     uint32_t sequence, int32_t correction_ppm,
						     bool compute_crc)
{
	(void)valid_frames;
	(void)correction_ppm;

	mock_produce_calls++;
	mock_last_sequence = sequence;
	mock_last_pcm = pcm_data;
	mock_last_crc = compute_crc;

	if (mock_produce_delay_ms > 0) {
		k_msleep(mock_produce_delay_ms);
	}

	if (mock_stall_producer_active) {
		return FLPR_PRODUCE_FULL;
	}

	return mock_produce_result;
}

int flpr_ring_mgr_notify_producer(void)
{
	mock_notify_calls++;
	return mock_notify_result;
}

int flpr_ring_mgr_wait_consume(uint32_t timeout_ms)
{
	(void)timeout_ms;
	mock_wait_calls++;
	if (mock_wait_delay_ms > 0) {
		k_msleep(mock_wait_delay_ms);
	}
	return mock_wait_result;
}

enum flpr_consume_result flpr_ring_mgr_consume_block(uint8_t *pcm_out, uint16_t *valid_frames_out,
						     uint32_t *sequence_out, uint32_t *crc32_out,
						     uint32_t *latency_cycles_out)
{
	mock_consume_calls++;

	if (mock_consume_corrupt_crc) {
		mock_consume_crc ^= 0xDEADBEEFU;
	}

	if (mock_consume_result == FLPR_CONSUME_OK) {
		/* Fill output with mock payload. */
		if (mock_consume_corrupt_payload) {
			/* Corrupt one byte. */
			mock_consume_payload[42] ^= 0xFF;
			/* Recalculate CRC to match corrupted payload,
			 * so CRC check passes and payload check detects. */
			if (mock_consume_valid_frames > 0) {
				mock_consume_crc =
					flpr_ring_crc32(mock_consume_payload,
							(size_t)mock_consume_valid_frames * 4U);
			}
		}

		if (pcm_out && mock_consume_valid_frames > 0) {
			memcpy(pcm_out, mock_consume_payload,
			       (size_t)mock_consume_valid_frames * 4U);
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
	}

	return mock_consume_result;
}

void flpr_ring_mgr_stall_producer(bool stall)
{
	mock_stall_producer_active = stall;
}

void flpr_ring_mgr_set_consume_cb(flpr_ring_consume_cb_t cb, void *user_data)
{
	(void)cb;
	(void)user_data;
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

/* ── Stage 4B recovery mocks ───────────────────────────────────── */

/* Mock control variables for runtime restart and remote reinit. */
int mock_runtime_restart_result;
uint32_t mock_runtime_restart_calls;
bool mock_runtime_restart_called;
uint32_t mock_runtime_new_epoch;

int mock_remote_restarted_result;
uint32_t mock_remote_restarted_calls;

/* Reset mock recovery state (called from mock_reset via setup). */
static void mock_recovery_reset(void)
{
	mock_runtime_restart_result = 0;
	mock_runtime_restart_calls = 0;
	mock_runtime_restart_called = false;
	mock_runtime_new_epoch = 0xABCD0001;
	mock_remote_restarted_result = 0;
	mock_remote_restarted_calls = 0;
}

void flpr_handshake_register_health_cb(flpr_health_transition_cb_t cb, void *user_data)
{
	(void)cb;
	(void)user_data;
	/* Stub: callback registered, never fired in tests unless explicitly called. */
}

int flpr_runtime_init(void)
{
	return 0;
}

int flpr_runtime_restart(uint32_t timeout_ms)
{
	(void)timeout_ms;
	mock_runtime_restart_calls++;
	mock_runtime_restart_called = true;
	return mock_runtime_restart_result;
}

void flpr_runtime_get_status(struct flpr_runtime_status *out)
{
	if (out) {
		memset(out, 0, sizeof(*out));
		out->new_epoch = mock_runtime_new_epoch;
		out->state = FLPR_RUNTIME_IDLE;
	}
}

int flpr_ring_mgr_remote_restarted(void)
{
	mock_remote_restarted_calls++;
	return mock_remote_restarted_result;
}

/* ── Stage 3B: ASRC typed produce/consume stubs ──────────────────────
 * produce_asrc uses mock_produce_result (shared), notify/wait use shared
 * mock variables.  Only consume_asrc_result has its own mock data. */

enum flpr_consume_result mock_asrc_consume_result;
struct flpr_consume_asrc_result mock_asrc_consume_data;
int mock_asrc_consume_calls;

static void mock_asrc_reset(void)
{
	mock_asrc_consume_result = FLPR_CONSUME_OK;
	memset(&mock_asrc_consume_data, 0, sizeof(mock_asrc_consume_data));
	mock_asrc_consume_data.output_frames = 480;
	mock_asrc_consume_data.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
	mock_asrc_consume_data.processing_status = 0;
	mock_asrc_consume_data.rtt_cycles = 500;
	mock_asrc_consume_data.processing_cycles = 300;
	mock_asrc_consume_calls = 0;
}

enum flpr_produce_result flpr_ring_mgr_produce_asrc(const int16_t *pcm_data, uint16_t valid_frames,
						    uint32_t sequence, int32_t correction_ppm,
						    const struct audio_asrc_state *pre_state)
{
	(void)pcm_data;
	(void)valid_frames;
	(void)sequence;
	(void)correction_ppm;
	(void)pre_state;
	mock_produce_calls++;
	return mock_produce_result;
}

enum flpr_consume_result flpr_ring_mgr_consume_asrc_result(int16_t *pcm_out,
							   uint16_t output_capacity,
							   struct flpr_consume_asrc_result *result)
{
	(void)output_capacity;
	mock_asrc_consume_calls++;
	if (result) {
		memcpy(result, &mock_asrc_consume_data, sizeof(*result));
	}
	if (pcm_out && mock_asrc_consume_result == FLPR_CONSUME_OK &&
	    mock_asrc_consume_data.output_frames > 0) {
		memset(pcm_out, 0xAB, (size_t)mock_asrc_consume_data.output_frames * 4U);
	}
	return mock_asrc_consume_result;
}
