/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock implementations of the FLPR APIs that the nRF54 shell command
 * bodies call (R8): handshake status, the acceptance module surface
 * (stress, fault hang, ring test, stalls, stale produce, acceptance
 * status, gates), core ring status, offload, and runtime.  Status
 * structs are controlled by the test; acceptance/stress/stall mechanics
 * are link-only stubs (their real production modules have their own
 * direct suites).
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "audio_offload.h"
#include "fake_flpr_deps.h"
#include "flpr_acceptance.h"
#include "flpr_handshake.h"
#include "flpr_ring.h"
#include "flpr_ring_mgr.h"
#include "flpr_runtime.h"

/* ---- handshake ---- */

static struct flpr_status test_hs_status;

void flpr_handshake_get_status(struct flpr_status *status)
{
	*status = test_hs_status;
}

void test_flpr_set_status(const struct flpr_status *s)
{
	test_hs_status = *s;
}

/* ---- acceptance module ---- */

static struct flpr_acceptance_status test_acc_status;
static int test_hang_result;
static int test_ring_test_result;
static int test_flpr_stall_result;
static int test_flpr_stall_timed_result;
static bool test_stall_producer_value;
static bool test_stall_producer_called;
static uint32_t test_stall_acked;
static int test_gates_result;
static bool test_stress_active_value;
static struct flpr_status test_stress_snapshot;

void flpr_acceptance_init(void)
{
}

void flpr_acceptance_get_status(struct flpr_acceptance_status *status)
{
	*status = test_acc_status;
}

void flpr_acceptance_stall_producer(bool stall)
{
	test_stall_producer_value = stall;
	test_stall_producer_called = true;
}

bool flpr_acceptance_stall_producer_active(void)
{
	return test_stall_producer_value;
}

int flpr_acceptance_flpr_stall(uint8_t stall_bits, uint32_t timeout_ms)
{
	(void)stall_bits;
	(void)timeout_ms;
	return test_flpr_stall_result;
}

int flpr_acceptance_flpr_stall_timed(uint8_t stall_bits, uint32_t duration_ms, uint32_t timeout_ms)
{
	(void)stall_bits;
	(void)duration_ms;
	(void)timeout_ms;
	return test_flpr_stall_timed_result;
}

uint32_t flpr_acceptance_flpr_stall_acked(void)
{
	return test_stall_acked;
}

int flpr_acceptance_test_run(uint32_t block_count, uint32_t timeout_ms,
			     struct flpr_acceptance_status *out)
{
	(void)block_count;
	(void)timeout_ms;
	*out = test_acc_status;
	return test_ring_test_result;
}

int flpr_acceptance_test_run_rate(uint32_t block_count, uint32_t timeout_ms, uint32_t rate_per_sec,
				  struct flpr_acceptance_status *out)
{
	(void)block_count;
	(void)timeout_ms;
	(void)rate_per_sec;
	*out = test_acc_status;
	return test_ring_test_result;
}

int flpr_acceptance_produce_stale_test(uint32_t stale_epoch)
{
	(void)stale_epoch;
	return 0;
}

static void stress_fields_into(struct flpr_status *out)
{
	if (!out) {
		return;
	}
	out->stress_active = test_stress_snapshot.stress_active;
	out->stress_count = test_stress_snapshot.stress_count;
	out->stress_sent = test_stress_snapshot.stress_sent;
	out->stress_recv = test_stress_snapshot.stress_recv;
	out->stress_timeouts = test_stress_snapshot.stress_timeouts;
	out->stress_stale = test_stress_snapshot.stress_stale;
	out->stress_mismatch = test_stress_snapshot.stress_mismatch;
	out->stress_err_send = test_stress_snapshot.stress_err_send;
}

void flpr_acceptance_stress(uint32_t count, struct flpr_status *out)
{
	(void)count;
	stress_fields_into(out);
}

bool flpr_acceptance_stress_active(void)
{
	return test_stress_active_value;
}

void flpr_acceptance_stress_snapshot(struct flpr_status *out)
{
	stress_fields_into(out);
}

int flpr_acceptance_send_fault_hang(uint32_t timeout_ms)
{
	(void)timeout_ms;
	return test_hang_result;
}

int flpr_acceptance_run_gates(uint32_t count, void *ctx, flpr_acceptance_print_t print)
{
	(void)count;
	(void)ctx;
	(void)print;
	return test_gates_result;
}

/* ---- ring manager (core status only) ---- */

static struct flpr_ring_status test_ring_status;
static int test_reset_result;
static int test_init_result;

void flpr_ring_mgr_get_status(struct flpr_ring_status *status)
{
	*status = test_ring_status;
}

int flpr_ring_mgr_coordinated_reset(uint32_t new_epoch, uint32_t timeout_ms)
{
	(void)new_epoch;
	(void)timeout_ms;
	return test_reset_result;
}

int flpr_ring_mgr_init(void)
{
	return test_init_result;
}

int flpr_ring_mgr_produce_block(const uint8_t *pcm_data, uint16_t valid_frames, uint32_t sequence,
				int32_t correction_ppm, bool compute_crc)
{
	(void)pcm_data;
	(void)valid_frames;
	(void)sequence;
	(void)correction_ppm;
	(void)compute_crc;
	return FLPR_PRODUCE_OK;
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
	return FLPR_CONSUME_EMPTY;
}

int flpr_ring_mgr_wait_consume(uint32_t timeout_ms)
{
	(void)timeout_ms;
	return 0;
}

int flpr_ring_mgr_notify_producer(void)
{
	return 0;
}

/* ---- test controls ---- */

void test_flpr_set_ring_status(const struct flpr_ring_status *s)
{
	test_ring_status = *s;
}

void test_flpr_set_acceptance_status(const struct flpr_acceptance_status *s)
{
	test_acc_status = *s;
}

void test_flpr_set_hang_result(int result)
{
	test_hang_result = result;
}

void test_flpr_set_ring_test_result(int result)
{
	test_ring_test_result = result;
}

void test_flpr_set_reset_result(int result)
{
	test_reset_result = result;
}

void test_flpr_set_init_result(int result)
{
	test_init_result = result;
}

void test_flpr_set_stall_result(int result)
{
	test_flpr_stall_result = result;
}

void test_flpr_set_stall_timed_result(int result)
{
	test_flpr_stall_timed_result = result;
}

void test_flpr_set_stall_acked(uint32_t value)
{
	test_stall_acked = value;
}

void test_flpr_set_stress_active(bool active)
{
	test_stress_active_value = active;
}

void test_flpr_set_stress_snapshot(const struct flpr_status *s)
{
	test_stress_snapshot = *s;
}

void test_flpr_set_gates_result(int result)
{
	test_gates_result = result;
}

bool test_flpr_stall_producer_called(void)
{
	return test_stall_producer_called;
}

bool test_flpr_stall_producer_value(void)
{
	return test_stall_producer_value;
}

/* ---- offload ---- */

static struct audio_offload_status test_offload_status;
static struct audio_offload_asrc_stats test_asrc_stats;
static bool test_offload_healthy;

void audio_offload_get_status(struct audio_offload_status *s)
{
	*s = test_offload_status;
}

void audio_offload_get_asrc_stats(struct audio_offload_asrc_stats *s)
{
	*s = test_asrc_stats;
}

bool audio_offload_is_healthy(void)
{
	return test_offload_healthy;
}

void test_flpr_set_offload_status(const struct audio_offload_status *s)
{
	test_offload_status = *s;
}

void test_flpr_set_asrc_stats(const struct audio_offload_asrc_stats *s)
{
	test_asrc_stats = *s;
}

void test_flpr_set_offload_healthy(bool healthy)
{
	test_offload_healthy = healthy;
}

/* ---- runtime ---- */

static struct flpr_runtime_status test_runtime_status;
static int test_restart_result;
static int test_restart_calls;

void flpr_runtime_get_status(struct flpr_runtime_status *out)
{
	*out = test_runtime_status;
}

int flpr_runtime_restart(uint32_t timeout_ms)
{
	(void)timeout_ms;
	test_restart_calls++;
	return test_restart_result;
}

void test_flpr_set_runtime_status(const struct flpr_runtime_status *s)
{
	test_runtime_status = *s;
}

void test_flpr_set_restart_result(int result)
{
	test_restart_result = result;
}

int test_flpr_restart_calls(void)
{
	return test_restart_calls;
}

/* ---- global reset ---- */

void test_flpr_reset(void)
{
	memset(&test_hs_status, 0, sizeof(test_hs_status));
	memset(&test_ring_status, 0, sizeof(test_ring_status));
	memset(&test_acc_status, 0, sizeof(test_acc_status));
	memset(&test_offload_status, 0, sizeof(test_offload_status));
	memset(&test_asrc_stats, 0, sizeof(test_asrc_stats));
	memset(&test_runtime_status, 0, sizeof(test_runtime_status));
	memset(&test_stress_snapshot, 0, sizeof(test_stress_snapshot));
	test_stall_acked = 0;
	test_offload_healthy = false;
	test_restart_result = 0;
	test_restart_calls = 0;
	test_hang_result = -ENOTSUP;
	test_ring_test_result = 0;
	test_reset_result = 0;
	test_init_result = 0;
	test_flpr_stall_result = 0;
	test_flpr_stall_timed_result = 0;
	test_gates_result = 0;
	test_stall_producer_called = false;
	test_stall_producer_value = false;
	test_stress_active_value = false;
}
