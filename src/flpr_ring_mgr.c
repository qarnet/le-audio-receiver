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

/* Test counters (protected by ring_lock). */
static bool test_active;
static uint32_t test_blocks_sent;
static uint32_t test_blocks_recv;
static uint32_t test_crc_errors;
static uint32_t test_seq_gaps;
static uint32_t test_full_events;
static uint32_t test_empty_events;
static uint32_t test_stale_events;
static uint32_t test_flpr_blocks;  /* blocks FLPR reports processing */
static uint32_t test_flpr_crc_err; /* FLPR-reported CRC errors */
static uint32_t test_output_full;  /* FLPR-reported output-full */

/* Test control. */
static bool stall_producer_enabled;

/* ── Semaphore for consumer notifications ────────────────────────── */

static struct k_sem consume_sem;

/* ── Coordinated reset state ────────────────────────────────────── */

static struct k_sem reset_ack_sem;

static uint32_t reset_ack_epoch; /* epoch received in ACK, 0 = failure */
static bool reset_ack_received;

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
	k_spin_unlock(&ring_lock, key);

	/* Wake the test loop. */
	k_sem_give(&consume_sem);
}

static void on_ring_test_report(const struct flpr_msg *msg, void *user_data)
{
	(void)user_data;
	k_spinlock_key_t key = k_spin_lock(&ring_lock);
	test_flpr_blocks = (uint32_t)msg->seq;
	test_flpr_crc_err = msg->data;
	k_spin_unlock(&ring_lock, key);
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
	return flpr_handshake_send_msg(&notify);
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

	/* Register IPC handlers for ring messages. */
	flpr_handshake_register_ring_handlers(on_ring_reset_ack, on_ring_consumer,
					      on_ring_test_report, NULL);

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
	/* Consumer callbacks are now handled via the semaphore pattern.
	 * This function is retained for API compatibility; the semaphore
	 * suffices for the ring test loop. */
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
	LOG_INF("RING_RESET sent, waiting for ACK...");

	/* Wait for RING_RESET_ACK. */
	ret = k_sem_take(&reset_ack_sem, K_MSEC(timeout_ms));
	LOG_INF("RING_RESET_ACK wait returned: %d", ret);
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
	test_active = false;
	test_blocks_sent = 0;
	test_blocks_recv = 0;
	test_crc_errors = 0;
	test_seq_gaps = 0;
	test_full_events = 0;
	test_empty_events = 0;
	test_stale_events = 0;
	test_flpr_blocks = 0;
	test_flpr_crc_err = 0;
	test_output_full = 0;
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
	status->test_seq_gaps = test_seq_gaps;
	status->test_full_events = test_full_events;
	status->test_empty_events = test_empty_events;
	status->test_stale_events = test_stale_events;
	status->test_producer_blocks = test_flpr_blocks;
	status->test_output_full = test_output_full;

	k_spin_unlock(&ring_lock, key);
}

enum flpr_produce_result flpr_ring_mgr_produce_block(const uint8_t *pcm_data, uint16_t valid_frames,
						     uint32_t sequence, int32_t correction_ppm,
						     bool compute_crc)
{
	uint32_t idx;
	int ret;

	if (valid_frames > FLPR_RING_PAYLOAD_MAX_INPUT) {
		valid_frames = FLPR_RING_PAYLOAD_MAX_INPUT;
	}

	/* Stall injection. */
	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		if (stall_producer_enabled) {
			test_full_events++;
			k_spin_unlock(&ring_lock, key);
			return FLPR_PRODUCE_FULL;
		}
		k_spin_unlock(&ring_lock, key);
	}

	ret = flpr_ring_produce_begin(RING_INPUT_BASE, &idx);
	if (ret != 0) {
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		test_full_events++;
		k_spin_unlock(&ring_lock, key);
		return FLPR_PRODUCE_FULL;
	}

	uint8_t *slot = flpr_ring_slot_base(RING_INPUT_BASE, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);

	/* Fill metadata. */
	meta->sequence = sequence;
	meta->epoch = ring_stream_epoch;
	meta->valid_frames = valid_frames;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	meta->correction_ppm = correction_ppm;

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
						     uint32_t *sequence_out, uint32_t *crc32_out)
{
	uint8_t *slot_base;
	struct flpr_ring_slot_meta *meta;
	int ret;

	ret = flpr_ring_consume_begin(RING_OUTPUT_BASE, ring_stream_epoch, &slot_base, &meta);
	if (ret == -1) {
		return FLPR_CONSUME_EMPTY;
	}
	if (ret == -2) {
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		test_stale_events++;
		k_spin_unlock(&ring_lock, key);
		return FLPR_CONSUME_STALE;
	}

	/* Read metadata. */
	uint16_t vf = meta->valid_frames;
	if (vf > FLPR_RING_PAYLOAD_CAPACITY_FRAMES) {
		vf = FLPR_RING_PAYLOAD_CAPACITY_FRAMES;
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

/* ── Ring test ──────────────────────────────────────────────────── */
/* Static buffers for ring test — too large for shell thread stack.
 * Single-writer: only used by the blocking test loop one at a time. */
static uint8_t test_pattern[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
static uint8_t test_recv_buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES];

int flpr_ring_mgr_test_run(uint32_t block_count, uint32_t timeout_ms, struct flpr_ring_status *out)
{
	int ret;
	uint32_t sent = 0;
	uint32_t start = k_uptime_get_32();

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
		test_seq_gaps = 0;
		test_full_events = 0;
		test_empty_events = 0;
		test_stale_events = 0;
		test_flpr_blocks = 0;
		test_flpr_crc_err = 0;
		test_output_full = 0;
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

	while (sent < block_count) {
		uint32_t elapsed = k_uptime_get_32() - start;
		if (elapsed > timeout_ms) {
			LOG_WRN("Ring test timeout at %u/%u blocks (%u ms)", sent, block_count,
				elapsed);
			break;
		}

		/* Drain output ring first (free up space for FLPR). */
		for (int drain = 0; drain < 8; drain++) {
			enum flpr_consume_result cr =
				flpr_ring_mgr_consume_block(test_recv_buf, NULL, NULL, NULL);
			if (cr == FLPR_CONSUME_EMPTY) {
				break;
			}
			if (cr == FLPR_CONSUME_STALE) {
				continue;
			}
		}

		/* Fill pattern: first 4 bytes = sequence, rest varies. */
		*(uint32_t *)test_pattern = sent;
		for (size_t i = 4; i < sizeof(test_pattern); i++) {
			test_pattern[i] = (uint8_t)(sent + i);
		}

		/* Produce block. */
		enum flpr_produce_result pr = flpr_ring_mgr_produce_block(
			test_pattern, FLPR_RING_PAYLOAD_MAX_INPUT, sent, 0, true);
		if (pr == FLPR_PRODUCE_FULL) {
			/* Ring full — wait for consumer notification. */
			ret = k_sem_take(&consume_sem, K_MSEC(50));
			if (ret != 0) {
				/* Timeout waiting for consumer — drain and retry. */
				continue;
			}
			continue;
		}

		/* Notify FLPR that data is available. */
		flpr_ring_mgr_notify_producer();

		sent++;

		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		test_blocks_sent = sent;
		k_spin_unlock(&ring_lock, key);

		/* Wait for consumer notification from FLPR.
		 * Short timeout: FLPR is fast, but we may need to drain. */
		ret = k_sem_take(&consume_sem, K_MSEC(100));
		if (ret != 0) {
			/* Consumer may be busy — drain output anyway. */
		}
	}

	/* Final drain. */
	uint32_t drain_start = k_uptime_get_32();
	while ((k_uptime_get_32() - drain_start) < 2000) {
		enum flpr_consume_result cr =
			flpr_ring_mgr_consume_block(test_recv_buf, NULL, NULL, NULL);
		if (cr == FLPR_CONSUME_EMPTY) {
			/* Wait a bit more for final FLPR output. */
			k_sem_take(&consume_sem, K_MSEC(100));
			if (flpr_ring_mgr_consume_block(test_recv_buf, NULL, NULL, NULL) ==
			    FLPR_CONSUME_EMPTY) {
				break;
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

	return 0;
}
