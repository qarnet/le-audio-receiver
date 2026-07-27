/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP side of PCM ring management for FLPR shared memory transport.
 * Ring addresses resolved from devicetree, not hardcoded.
 * IPC control through flpr_handshake module's endpoint + handler API.
 *
 * Lock discipline:
 *   ring_lock spinlock protects local test counters and stall flags.
 *   IPC callbacks run with interrupts disabled; they only set a semaphore
 *   (k_sem_give) outside the spinlock to wake the test loop.
 *   flpr_handshake_send_msg() is lock-free (never called under ring_lock).
 */

#include "flpr_ring_mgr.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/__assert.h>

#include "flpr_ring.h"
#include "flpr_handshake.h"

LOG_MODULE_REGISTER(flpr_ring, LOG_LEVEL_INF);

/* ── Devicetree resolved addresses ───────────────────────────────── */

#define DT_PCM_RING DT_NODELABEL(pcm_ring)

#if DT_NODE_EXISTS(DT_PCM_RING)
/* pcm_ring: 16 KiB at 0x2002C000.  Split into two 8 KiB SPSC rings. */
#define RING_DT_BASE DT_REG_ADDR(DT_PCM_RING)
#define RING_DT_SIZE DT_REG_SIZE(DT_PCM_RING)

BUILD_ASSERT(RING_DT_SIZE == 0x4000U, "pcm_ring DT size must be 16 KiB");
BUILD_ASSERT(RING_DT_SIZE == 2U * FLPR_RING_TOTAL_SIZE,
	     "pcm_ring must fit exactly two 8 KiB rings");
BUILD_ASSERT(FLPR_RING_TOTAL_SIZE == 8192U, "ring size must be 8 KiB");

/* Input ring (CPUAPP→FLPR): lower 8 KiB. */
#define RING_INPUT_BASE ((uint8_t *)(uintptr_t)(RING_DT_BASE))

/* Output ring (FLPR→CPUAPP): upper 8 KiB. */
#define RING_OUTPUT_BASE ((uint8_t *)(uintptr_t)(RING_DT_BASE + FLPR_RING_TOTAL_SIZE))

/* Verify rings are in the 0x2002C000..0x20030000 gap and below FLPR
 * execution SRAM at 0x20030000. */
BUILD_ASSERT(RING_DT_BASE == 0x2002C000U, "input ring base must be 0x2002C000");
BUILD_ASSERT(RING_DT_BASE + RING_DT_SIZE == 0x20030000U,
	     "ring end must be exactly 0x20030000 (FLPR SRAM start)");

#else
#error "DT node pcm_ring not found — add reservation to cpuapp overlay"
#endif

/* ── State ──────────────────────────────────────────────────────── */

static struct k_spinlock ring_lock;

static uint32_t ring_stream_epoch;
static bool rings_initialized;

/* Diagnostic counters (protected by ring_lock). */
static uint32_t diag_notify_sent;
static uint32_t diag_notify_err;
static uint32_t diag_sem_gives;
static uint32_t diag_sem_takes;

/* Test counters (protected by ring_lock). */
static bool test_active;
static uint32_t test_blocks_sent;
static uint32_t test_blocks_recv;
static uint32_t test_crc_errors;
static uint32_t test_payload_errors;
static uint32_t test_seq_gaps;
static uint32_t test_full_events;
static uint32_t test_backpressure;
static uint32_t test_empty_events;
static uint32_t test_stale_events;
static uint32_t test_flpr_blocks;  /* blocks FLPR reports processing */
static uint32_t test_flpr_crc_err; /* FLPR-reported CRC errors */
static uint32_t test_output_full;  /* FLPR-reported output-full */

/* FLPR-reported diagnostic counters. */
static uint32_t test_flpr_notify_rcv;
static uint32_t test_flpr_worker_wake;
static uint32_t test_flpr_consume_ok;
static uint32_t test_flpr_consume_empty;
static uint32_t test_flpr_consume_stale;
static uint32_t test_flpr_produce_ok;
static uint32_t test_flpr_produce_full;

/* Latency accumulators (protected by ring_lock). */
static uint32_t latency_min;
static uint32_t latency_max;
static uint64_t latency_sum;
static uint32_t latency_count;

/* Test control. */
static bool stall_producer_enabled;

/* ── Semaphore for consumer notifications ────────────────────────── */

static struct k_sem consume_sem;

/* ── Coordinated reset state ────────────────────────────────────── */

static struct k_sem reset_ack_sem;

static uint32_t reset_ack_epoch; /* epoch received in ACK, 0 = failure */
static bool reset_ack_received;

/* ── FLPR stall control ─────────────────────────────────────────── */

static struct k_sem stall_ack_sem;

/* ── IPC handlers (called from flpr_handshake receive context) ───── */

static void on_ring_reset_ack(const struct flpr_msg *msg, void *user_data)
{
	(void)user_data;
	reset_ack_epoch = msg->data;
	reset_ack_received = true;
	k_sem_give(&reset_ack_sem);
}

static void on_ring_consumer(const struct flpr_msg *msg, void *user_data)
{
	(void)user_data;
	/* Update FLPR-reported block count. */
	k_spinlock_key_t key = k_spin_lock(&ring_lock);
	test_flpr_blocks = (uint32_t)msg->seq;
	diag_sem_gives++;
	k_spin_unlock(&ring_lock, key);

	/* Wake the test loop. */
	k_sem_give(&consume_sem);
}

static void on_ring_test_report(const struct flpr_msg *msg, void *user_data)
{
	(void)user_data;
	k_spinlock_key_t key = k_spin_lock(&ring_lock);

	/* Subtype encoded in seq high byte:
	 *   0x00 → block_count + crc_errors
	 *   0xD1 → consume_ok (full 32-bit in data)
	 *   0xD2 → produce_ok (full 32-bit in data)
	 *   0xD3 → notify_rcv + worker_wake + produce_full
	 *   0xD4 → cons_empty + cons_stale (data lo/hi 16-bit) */
	uint8_t subtype = (uint8_t)(msg->seq >> 8);

	switch (subtype) {
	case 0x00:
		/* FLPR: seq lo 16 = crc_errors, data = block_count */
		test_flpr_crc_err = (uint32_t)(msg->seq & 0xFFFFU);
		test_flpr_blocks = msg->data;
		break;
	case 0xD1:
		test_flpr_consume_ok = msg->data;
		break;
	case 0xD2:
		test_flpr_produce_ok = msg->data;
		break;
	case 0xD3:
		test_flpr_notify_rcv = (uint32_t)(msg->seq & 0xFFU);
		test_flpr_worker_wake = (uint32_t)(msg->data & 0xFFFFU);
		test_flpr_produce_full = (uint32_t)((msg->data >> 16) & 0xFFFFU);
		break;
	case 0xD4:
		test_flpr_consume_empty = (uint32_t)(msg->data & 0xFFFFU);
		test_flpr_consume_stale = (uint32_t)((msg->data >> 16) & 0xFFFFU);
		break;
	default:
		break;
	}
	k_spin_unlock(&ring_lock, key);
}

static void on_ring_stall_ack(const struct flpr_msg *msg, void *user_data)
{
	(void)msg;
	(void)user_data;
	k_sem_give(&stall_ack_sem);
}

/* ── Notification ────────────────────────────────────────────────── */

int flpr_ring_mgr_notify_producer(void)
{
	struct flpr_msg notify = {
		.type = FLPR_MSG_RING_PRODUCER,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = 0,
		.data = 0,
	};
	int ret = flpr_handshake_send_msg(&notify);

	k_spinlock_key_t key = k_spin_lock(&ring_lock);
	diag_notify_sent++;
	if (ret < 0) {
		diag_notify_err++;
	}
	k_spin_unlock(&ring_lock, key);

	return ret;
}

/* ── Public API ──────────────────────────────────────────────────── */

int flpr_ring_mgr_init(void)
{
	struct flpr_status hs;

	flpr_handshake_get_status(&hs);
	if (!hs.ready || !hs.acked) {
		LOG_WRN("FLPR not ready — ring init deferred");
		return -EAGAIN;
	}

	/* Initialize semaphores. */
	k_sem_init(&consume_sem, 0, 1000001);
	k_sem_init(&reset_ack_sem, 0, 1);
	k_sem_init(&stall_ack_sem, 0, 1);

	/* Register IPC handlers for ring messages. */
	flpr_handshake_register_ring_handlers(on_ring_reset_ack, on_ring_consumer,
					      on_ring_test_report, on_ring_stall_ack, NULL);

	/* Initialize both rings in shared memory. */
	flpr_ring_init(RING_INPUT_BASE, FLPR_RING_CPUAPP_TO_FLPR);
	flpr_ring_init(RING_OUTPUT_BASE, FLPR_RING_FLPR_TO_CPUAPP);

	k_spinlock_key_t key = k_spin_lock(&ring_lock);
	rings_initialized = true;
	ring_stream_epoch = 0; /* not yet agreed */
	k_spin_unlock(&ring_lock, key);

	LOG_INF("PCM rings at 0x%08x (in) / 0x%08x (out), 481-frame capacity", RING_DT_BASE,
		RING_DT_BASE + FLPR_RING_TOTAL_SIZE);

	return 0;
}

void flpr_ring_mgr_set_consume_cb(flpr_ring_consume_cb_t cb, void *user_data)
{
	(void)cb;
	(void)user_data;
}

int flpr_ring_mgr_coordinated_reset(uint32_t new_epoch, uint32_t timeout_ms)
{
	struct flpr_status hs;
	int ret;

	flpr_handshake_get_status(&hs);
	if (!hs.ready || !hs.acked) {
		return -EAGAIN;
	}

	if (new_epoch == 0) {
		new_epoch = k_cycle_get_32();
	}
	if (new_epoch == 0) {
		LOG_ERR("Failed to generate non-zero epoch");
		return -EINVAL;
	}

	/* Drain any stale semaphore give. */
	while (k_sem_take(&reset_ack_sem, K_NO_WAIT) == 0) {
	}

	LOG_INF("Coordinated reset: proposing epoch=%u to FLPR", new_epoch);

	/* Send RING_RESET to FLPR with proposed epoch. */
	struct flpr_msg reset_req = {
		.type = FLPR_MSG_RING_RESET,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = 0,
		.data = new_epoch,
	};
	ret = flpr_handshake_send_msg(&reset_req);

	if (ret < 0) {
		LOG_ERR("RING_RESET send failed: %d", ret);
		return -EIO;
	}

	/* Wait for RING_RESET_ACK. */
	ret = k_sem_take(&reset_ack_sem, K_MSEC(timeout_ms));
	if (ret != 0) {
		LOG_WRN("RING_RESET_ACK timeout (%u ms)", timeout_ms);
		return -ETIMEDOUT;
	}

	/* Verify FLPR acked with the same epoch. */
	if (!reset_ack_received || reset_ack_epoch != new_epoch) {
		LOG_ERR("RING_RESET_ACK epoch mismatch: expected %u, got %u", new_epoch,
			reset_ack_epoch);
		return -EIO;
	}

	/* Apply the epoch reset on CPUAPP side. */
	ret = flpr_ring_mgr_reset(new_epoch);
	if (ret != 0) {
		return ret;
	}

	LOG_INF("Coordinated ring reset: epoch=%u", new_epoch);
	return 0;
}

int flpr_ring_mgr_reset(uint32_t new_epoch)
{
	if (new_epoch == 0) {
		return -EINVAL;
	}

	int ret_in = flpr_ring_reset_epoch(RING_INPUT_BASE, new_epoch);
	int ret_out = flpr_ring_reset_epoch(RING_OUTPUT_BASE, new_epoch);

	if (ret_in != 0 || ret_out != 0) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&ring_lock);
	ring_stream_epoch = new_epoch;
	diag_notify_sent = 0;
	diag_notify_err = 0;
	diag_sem_gives = 0;
	diag_sem_takes = 0;
	test_active = false;
	test_blocks_sent = 0;
	test_blocks_recv = 0;
	test_crc_errors = 0;
	test_payload_errors = 0;
	test_seq_gaps = 0;
	test_full_events = 0;
	test_backpressure = 0;
	test_empty_events = 0;
	test_stale_events = 0;
	test_flpr_blocks = 0;
	test_flpr_crc_err = 0;
	test_output_full = 0;
	test_flpr_notify_rcv = 0;
	test_flpr_worker_wake = 0;
	test_flpr_consume_ok = 0;
	test_flpr_consume_empty = 0;
	test_flpr_consume_stale = 0;
	test_flpr_produce_ok = 0;
	test_flpr_produce_full = 0;
	latency_min = UINT32_MAX;
	latency_max = 0;
	latency_sum = 0;
	latency_count = 0;
	stall_producer_enabled = false;
	k_spin_unlock(&ring_lock, key);

	LOG_INF("PCM rings reset: epoch=%u", new_epoch);
	return 0;
}

void flpr_ring_mgr_get_status(struct flpr_ring_status *status)
{
	if (!status) {
		return;
	}
	memset(status, 0, sizeof(*status));

	k_spinlock_key_t key = k_spin_lock(&ring_lock);

	status->initialized = rings_initialized;
	status->epoch = ring_stream_epoch;

	status->notify_sent = diag_notify_sent;
	status->notify_err = diag_notify_err;
	status->sem_gives = diag_sem_gives;
	status->sem_takes = diag_sem_takes;

	if (rings_initialized) {
		status->in_producer = flpr_ring_producer(RING_INPUT_BASE);
		status->in_consumer = flpr_ring_consumer(RING_INPUT_BASE);
		status->in_used = flpr_ring_used(status->in_producer, status->in_consumer);
		status->in_space = flpr_ring_space(status->in_producer, status->in_consumer);
		status->in_epoch = flpr_ring_epoch(RING_INPUT_BASE);

		status->out_producer = flpr_ring_producer(RING_OUTPUT_BASE);
		status->out_consumer = flpr_ring_consumer(RING_OUTPUT_BASE);
		status->out_used = flpr_ring_used(status->out_producer, status->out_consumer);
		status->out_space = flpr_ring_space(status->out_producer, status->out_consumer);
		status->out_epoch = flpr_ring_epoch(RING_OUTPUT_BASE);
	}

	status->test_active = test_active;
	status->test_blocks_sent = test_blocks_sent;
	status->test_blocks_recv = test_blocks_recv;
	status->test_crc_errors = test_crc_errors;
	status->test_payload_errors = test_payload_errors;
	status->test_seq_gaps = test_seq_gaps;
	status->test_full_events = test_full_events;
	status->test_backpressure = test_backpressure;
	status->test_empty_events = test_empty_events;
	status->test_stale_events = test_stale_events;
	status->test_producer_blocks = test_flpr_blocks;
	status->test_output_full = test_output_full;

	status->flpr_notify_rcv = test_flpr_notify_rcv;
	status->flpr_worker_wake = test_flpr_worker_wake;
	status->flpr_consume_ok = test_flpr_consume_ok;
	status->flpr_consume_empty = test_flpr_consume_empty;
	status->flpr_consume_stale = test_flpr_consume_stale;
	status->flpr_produce_ok = test_flpr_produce_ok;
	status->flpr_produce_full = test_flpr_produce_full;

	status->latency_min = latency_min;
	status->latency_max = latency_max;
	status->latency_sum = latency_sum;
	status->latency_count = latency_count;

	k_spin_unlock(&ring_lock, key);
}

enum flpr_produce_result flpr_ring_mgr_produce_block(const uint8_t *pcm_data, uint16_t valid_frames,
						     uint32_t sequence, int32_t correction_ppm,
						     bool compute_crc)
{
	uint32_t idx;
	int ret;

	/* Reject invalid frame counts instead of silently clamping. */
	if (valid_frames > FLPR_RING_PAYLOAD_MAX_INPUT) {
		return FLPR_PRODUCE_INVALID;
	}

	/* Stall injection. */
	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		if (stall_producer_enabled) {
			test_backpressure++;
			k_spin_unlock(&ring_lock, key);
			return FLPR_PRODUCE_FULL;
		}
		k_spin_unlock(&ring_lock, key);
	}

	ret = flpr_ring_produce_begin(RING_INPUT_BASE, &idx);
	if (ret == -ENOSPC) {
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		test_full_events++;
		k_spin_unlock(&ring_lock, key);
		return FLPR_PRODUCE_FULL;
	}
	if (ret != 0) {
		return FLPR_PRODUCE_INVALID;
	}

	uint8_t *slot = flpr_ring_slot_base(RING_INPUT_BASE, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);

	/* Fill metadata. */
	meta->sequence = sequence;
	meta->epoch = ring_stream_epoch;
	meta->valid_frames = valid_frames;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	meta->correction_ppm = correction_ppm;
	meta->cpu_timestamp = k_cycle_get_32();

	/* Fill payload.  Copy only valid bytes; zero remainder. */
	size_t copy_bytes = (size_t)valid_frames * 4U; /* stereo 16-bit */
	memset(flpr_ring_slot_payload(slot), 0, FLPR_RING_PAYLOAD_CAPACITY_BYTES);
	if (pcm_data && copy_bytes > 0) {
		memcpy(flpr_ring_slot_payload(slot), pcm_data, copy_bytes);
	}

	/* CRC over valid payload bytes ONLY. */
	if (compute_crc) {
		meta->crc32 = flpr_ring_crc32(flpr_ring_slot_payload(slot), copy_bytes);
	} else {
		meta->crc32 = 0;
	}

	/* Publish. */
	flpr_ring_produce_commit(RING_INPUT_BASE, idx);

	return FLPR_PRODUCE_OK;
}

enum flpr_consume_result flpr_ring_mgr_consume_block(uint8_t *pcm_out, uint16_t *valid_frames_out,
						     uint32_t *sequence_out, uint32_t *crc32_out,
						     uint32_t *latency_cycles_out)
{
	uint8_t *slot_base;
	struct flpr_ring_slot_meta *meta;
	int ret;

	ret = flpr_ring_consume_begin(RING_OUTPUT_BASE, ring_stream_epoch, &slot_base, &meta);
	if (ret == -ENOENT) {
		return FLPR_CONSUME_EMPTY;
	}
	if (ret == -ESTALE) {
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		test_stale_events++;
		k_spin_unlock(&ring_lock, key);
		return FLPR_CONSUME_STALE;
	}

	/* Read metadata. */
	uint16_t vf = meta->valid_frames;
	/* Reject invalid frame counts instead of silently clamping. */
	if (vf > FLPR_RING_PAYLOAD_CAPACITY_FRAMES) {
		flpr_ring_consume_done(RING_OUTPUT_BASE);
		return FLPR_CONSUME_INVALID;
	}
	if (valid_frames_out) {
		*valid_frames_out = vf;
	}
	if (sequence_out) {
		*sequence_out = meta->sequence;
	}
	if (crc32_out) {
		*crc32_out = meta->crc32;
	}

	/* Compute roundtrip latency from cpu_timestamp. */
	if (latency_cycles_out) {
		uint32_t now = k_cycle_get_32();
		uint32_t latency = now - meta->cpu_timestamp;
		if (latency > 0) {
			*latency_cycles_out = latency;
		} else {
			*latency_cycles_out = 0;
		}

		/* Track min/max/avg. */
		if (latency > 0 && test_active) {
			k_spinlock_key_t key = k_spin_lock(&ring_lock);
			if (latency < latency_min) {
				latency_min = latency;
			}
			if (latency > latency_max) {
				latency_max = latency;
			}
			latency_sum += latency;
			latency_count++;
			k_spin_unlock(&ring_lock, key);
		}
	}

	/* Read payload. */
	if (pcm_out) {
		memcpy(pcm_out, flpr_ring_slot_payload(slot_base), vf > 0 ? (size_t)vf * 4U : 0);
	}

	/* Verify CRC over valid bytes if present. */
	if (test_active && meta->crc32 != 0) {
		uint32_t computed = flpr_ring_crc32(flpr_ring_slot_payload(slot_base),
						    vf > 0 ? (size_t)vf * 4U : 0);
		if (computed != meta->crc32) {
			k_spinlock_key_t key = k_spin_lock(&ring_lock);
			test_crc_errors++;
			k_spin_unlock(&ring_lock, key);
		}
	}

	/* Independent payload verification: regenerate from sequence
	 * and memcmp. */
	if (test_active && vf > 0) {
		uint32_t frame_errs = flpr_ring_verify_payload(flpr_ring_slot_payload(slot_base),
							       (size_t)vf * 4U, meta->sequence);
		if (frame_errs > 0) {
			k_spinlock_key_t key = k_spin_lock(&ring_lock);
			test_payload_errors += frame_errs;
			k_spin_unlock(&ring_lock, key);
		}
	}

	flpr_ring_consume_done(RING_OUTPUT_BASE);

	k_spinlock_key_t key = k_spin_lock(&ring_lock);
	test_blocks_recv++;
	k_spin_unlock(&ring_lock, key);

	return FLPR_CONSUME_OK;
}

void flpr_ring_mgr_stall_producer(bool stall)
{
	k_spinlock_key_t key = k_spin_lock(&ring_lock);
	stall_producer_enabled = stall;
	k_spin_unlock(&ring_lock, key);
}

int flpr_ring_mgr_flpr_stall(uint8_t stall_bits, uint32_t timeout_ms)
{
	/* Drain stale semaphore. */
	while (k_sem_take(&stall_ack_sem, K_NO_WAIT) == 0) {
	}

	struct flpr_msg stall_msg = {
		.type = FLPR_MSG_RING_STALL,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = 0,
		.data = stall_bits,
	};
	int ret = flpr_handshake_send_msg(&stall_msg);
	if (ret < 0) {
		return ret;
	}

	ret = k_sem_take(&stall_ack_sem, K_MSEC(timeout_ms));
	if (ret != 0) {
		return -ETIMEDOUT;
	}
	return 0;
}

/* ── Ring test ──────────────────────────────────────────────────── */
/* Static buffers for ring test — too large for shell thread stack.
 * Single-writer: only used by the blocking test loop one at a time. */
static uint8_t test_pattern[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
static uint8_t test_recv_buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES];

int flpr_ring_mgr_test_run(uint32_t block_count, uint32_t timeout_ms, struct flpr_ring_status *out)
{
	return flpr_ring_mgr_test_run_rate(block_count, timeout_ms, 0, out);
}

int flpr_ring_mgr_test_run_rate(uint32_t block_count, uint32_t timeout_ms, uint32_t rate_per_sec,
				struct flpr_ring_status *out)
{
	uint32_t sent = 0;
	uint32_t start = k_uptime_get_32();
	uint32_t last_seq = 0;
	bool any_error = false;

	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		if (!rings_initialized || ring_stream_epoch == 0) {
			k_spin_unlock(&ring_lock, key);
			if (out) {
				flpr_ring_mgr_get_status(out);
			}
			return -EAGAIN;
		}
		if (test_active) {
			k_spin_unlock(&ring_lock, key);
			if (out) {
				flpr_ring_mgr_get_status(out);
			}
			return -EBUSY;
		}

		test_active = true;
		test_blocks_sent = 0;
		test_blocks_recv = 0;
		test_crc_errors = 0;
		test_payload_errors = 0;
		test_seq_gaps = 0;
		test_full_events = 0;
		test_backpressure = 0;
		test_empty_events = 0;
		test_stale_events = 0;
		test_flpr_blocks = 0;
		test_flpr_crc_err = 0;
		test_output_full = 0;
		test_flpr_notify_rcv = 0;
		test_flpr_worker_wake = 0;
		test_flpr_consume_ok = 0;
		test_flpr_consume_empty = 0;
		test_flpr_consume_stale = 0;
		test_flpr_produce_ok = 0;
		test_flpr_produce_full = 0;
		latency_min = UINT32_MAX;
		latency_max = 0;
		latency_sum = 0;
		latency_count = 0;
		k_spin_unlock(&ring_lock, key);
	}

	/* Drain semaphore before starting. */
	while (k_sem_take(&consume_sem, K_NO_WAIT) == 0) {
	}

	/* Send RING_TEST_START to FLPR. */
	{
		struct flpr_msg start_msg = {
			.type = FLPR_MSG_RING_TEST_START,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = 0,
			.data = block_count,
		};
		flpr_handshake_send_msg(&start_msg);
	}

	LOG_INF("Ring test: sending %u blocks...", block_count);

	/* Event-driven batch send loop:
	 *
	 * Batch up to ring capacity, notify FLPR once per batch,
	 * then wait for consume_sem (or 10 ms timeout) before draining
	 * and producing the next batch.
	 *
	 * This avoids busy-polling drains and lets IPC notifications
	 * drive the pipeline.  The FLPR polling fallback (10 ms) provides
	 * a worst-case deadline — notifications are immediate via the IPC
	 * callback. */
#define BATCH_MAX 4

	while (sent < block_count) {
		uint32_t elapsed = k_uptime_get_32() - start;
		if (elapsed > timeout_ms) {
			LOG_WRN("Ring test timeout at %u/%u blocks (%u ms)", sent, block_count,
				elapsed);
			any_error = true;
			break;
		}

		/* Rate limiting before production: sleep if ahead of schedule.
		 * Pure average pacing from test start time — no per-second
		 * windows, no initial burst, no reset.  64-bit multiply
		 * avoids overflow at high block counts.
		 *
		 * Sleep cap at 5000 ms prevents the test thread from sleeping
		 * through the entire remaining budget; long sleeps are split
		 * across multiple iterations. */
		if (rate_per_sec > 0) {
			uint64_t target = flpr_rate_limit_target_ms(sent, rate_per_sec);
			uint64_t actual = (uint64_t)k_uptime_get_32() - (uint64_t)start;
			if (target > actual) {
				uint64_t deficit = target - actual;
				if (deficit > 5000) {
					deficit = 5000;
				}
				k_msleep((uint32_t)deficit);
			}
		}

		/* Drain any available output first. */
		{
			uint16_t vf;
			uint32_t seq_out;
			uint32_t latency;
			while (flpr_ring_mgr_consume_block(test_recv_buf, &vf, &seq_out, NULL,
							   &latency) == FLPR_CONSUME_OK) {
			}
		}

		/* Produce up to BATCH_MAX blocks in one go. */
		uint32_t batch_sent = 0;
		for (uint32_t b = 0; b < BATCH_MAX && sent < block_count; b++) {
			flpr_ring_gen_payload(test_pattern, sizeof(test_pattern), sent);

			enum flpr_produce_result pr = flpr_ring_mgr_produce_block(
				test_pattern, FLPR_RING_PAYLOAD_MAX_INPUT, sent, 0, true);
			if (pr == FLPR_PRODUCE_FULL) {
				break; /* ring full — drain + retry next iteration */
			}
			if (pr != FLPR_PRODUCE_OK) {
				any_error = true;
				break;
			}

			if (sent > 0 && sent != last_seq + 1) {
				k_spinlock_key_t key = k_spin_lock(&ring_lock);
				test_seq_gaps++;
				k_spin_unlock(&ring_lock, key);
			}
			last_seq = sent;
			sent++;
			batch_sent++;

			k_spinlock_key_t key = k_spin_lock(&ring_lock);
			test_blocks_sent = sent;
			k_spin_unlock(&ring_lock, key);
		}

		if (batch_sent > 0) {
			/* Notify FLPR once per batch. */
			if (flpr_ring_mgr_notify_producer() < 0) {
				k_msleep(1);
				continue;
			}
		}

		/* Wait for FLPR to process (notification or poll fallback).
		 * Short timeout avoids busy-wait — the IPC callback gives
		 * consume_sem when output data is available. */
		if (sent < block_count) {
			k_sem_take(&consume_sem, K_MSEC(10));
		}
	}
#undef BATCH_MAX

	/* Final drain: wait until recv == sent or global timeout. */
	{
		uint32_t drain_start = k_uptime_get_32();
		uint32_t remaining = timeout_ms - (drain_start - start);
		if (remaining > timeout_ms) {
			remaining = 100;
		}

		while ((k_uptime_get_32() - drain_start) < remaining) {
			uint16_t vf;
			uint32_t seq_out;
			uint32_t latency;
			bool drained = false;

			while (flpr_ring_mgr_consume_block(test_recv_buf, &vf, &seq_out, NULL,
							   &latency) == FLPR_CONSUME_OK) {
				drained = true;
			}

			k_spinlock_key_t key = k_spin_lock(&ring_lock);
			uint32_t recv_now = test_blocks_recv;
			k_spin_unlock(&ring_lock, key);

			if (recv_now >= sent) {
				break;
			}

			if (!drained) {
				k_sem_take(&consume_sem, K_MSEC(50));
			}
		}
	}

	/* Send RING_TEST_STOP to FLPR (get final report). */
	{
		struct flpr_msg stop_msg = {
			.type = FLPR_MSG_RING_TEST_STOP,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = 0,
			.data = 0,
		};
		flpr_handshake_send_msg(&stop_msg);
	}

	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		test_active = false;
		k_spin_unlock(&ring_lock, key);
	}

	if (out) {
		flpr_ring_mgr_get_status(out);
	}

	/* Return nonzero if ANY failure. */
	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		bool has_error = (sent != block_count) || (test_blocks_recv != block_count) ||
				 (test_crc_errors > 0) || (test_payload_errors > 0) ||
				 (test_stale_events > 0) || any_error;
		k_spin_unlock(&ring_lock, key);

		if (has_error) {
			return -1;
		}
	}

	return 0;
}

/* ── Test helpers (stale injection) ─────────────────────────────── */

int flpr_ring_mgr_produce_stale_test(uint32_t stale_epoch)
{
	uint32_t idx;
	int ret;

	if (!rings_initialized) {
		return -EAGAIN;
	}
	if (stale_epoch == 0) {
		return -EINVAL;
	}

	/* Allocate slot in OUTPUT ring directly (FLPR→CPUAPP). */
	ret = flpr_ring_produce_begin(RING_OUTPUT_BASE, &idx);
	if (ret != 0) {
		return ret;
	}

	uint8_t *slot = flpr_ring_slot_base(RING_OUTPUT_BASE, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);

	/* Fill with stale epoch — consumer will reject. */
	memset(slot, 0, FLPR_RING_SLOT_STRIDE);
	meta->epoch = stale_epoch;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	meta->valid_frames = 0;

	flpr_ring_produce_commit(RING_OUTPUT_BASE, idx);
	return 0;
}

int flpr_ring_mgr_wait_consume(uint32_t timeout_ms)
{
	return k_sem_take(&consume_sem, K_MSEC(timeout_ms));
}
