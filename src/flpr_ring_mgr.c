/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP side of PCM ring management for FLPR shared memory transport.
 * Uses flpr_ring.h for pure SPSC ring operations and flpr_handshake.h
 * for IPC-based control messages to FLPR.
 *
 * Ring memory at hardcoded addresses (must match DTS reservation):
 *   CPUAPP→FLPR input ring:  0x2002C000
 *   FLPR→CPUAPP output ring: 0x2002E000
 */

#include "flpr_ring_mgr.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

#include "flpr_ring.h"
#include "flpr_handshake.h"

LOG_MODULE_REGISTER(flpr_ring, LOG_LEVEL_INF);

/* ── Ring base addresses ────────────────────────────────────────── */

#define RING_INPUT_BASE  ((uint8_t *)0x2002C000U)
#define RING_OUTPUT_BASE ((uint8_t *)0x2002E000U)

/* ── State ──────────────────────────────────────────────────────── */

static struct k_spinlock ring_lock;

static uint32_t ring_stream_epoch;
static bool rings_initialized;

/* Test counters. */
static bool test_active;
static uint32_t test_blocks_sent;
static uint32_t test_blocks_recv;
static uint32_t test_crc_errors;
static uint32_t test_seq_gaps;
static uint32_t test_full_events;
static uint32_t test_empty_events;
static uint32_t test_stale_events;
static uint32_t test_start_time_ms;

/* IPC for notifications. Discovered from flpr_handshake init. */
static struct ipc_ept *ring_ipc_ep;

/* ── Internal helpers ────────────────────────────────────────────── */

static int send_ctrl_msg(struct ipc_ept *ep, uint8_t type, uint32_t data)
{
	struct flpr_msg msg = {
		.type = type,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = 0,
		.data = data,
	};
	return ipc_service_send(ep, &msg, sizeof(msg));
}

/* ── Public API ──────────────────────────────────────────────────── */

int flpr_ring_mgr_init(void)
{
	struct flpr_status hs;
	k_spinlock_key_t key;

	flpr_handshake_get_status(&hs);
	if (!hs.ready || !hs.acked) {
		LOG_WRN("FLPR not ready — ring init deferred");
		return -EAGAIN;
	}

	/* Discover IPC endpoint from handshake module.
	 * We re-use the same IPC device/endpoint. */
	/* The flpr_handshake module owns the IPC ep. We'll get it
	 * through an accessor or pass it during ring operations.
	 * For Stage 1, we use a direct device access approach.
	 * TODO: clean endpoint sharing through handshake API. */
	const struct device *ipc_dev = DEVICE_DT_GET(DT_NODELABEL(ipc0));
	if (!device_is_ready(ipc_dev)) {
		return -ENODEV;
	}

	/* Initialize both rings in shared memory. */
	flpr_ring_init(RING_INPUT_BASE, FLPR_RING_CPUAPP_TO_FLPR);
	flpr_ring_init(RING_OUTPUT_BASE, FLPR_RING_FLPR_TO_CPUAPP);

	key = k_spin_lock(&ring_lock);
	rings_initialized = true;
	ring_stream_epoch = 0; /* not yet agreed */
	k_spin_unlock(&ring_lock, key);

	LOG_INF("PCM rings initialized at 0x%08x (in) / 0x%08x (out)", (uint32_t)RING_INPUT_BASE,
		(uint32_t)RING_OUTPUT_BASE);

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
		status->in_epoch = flpr_ring_epoch(RING_INPUT_BASE);
		status->out_producer = flpr_ring_producer(RING_OUTPUT_BASE);
		status->out_consumer = flpr_ring_consumer(RING_OUTPUT_BASE);
		status->out_epoch = flpr_ring_epoch(RING_OUTPUT_BASE);
	}

	status->test_active = test_active;
	status->test_blocks_sent = test_blocks_sent;
	status->test_blocks_recv = test_blocks_recv;
	status->test_crc_errors = test_crc_errors;
	status->test_full = test_full_events;
	status->test_empty = test_empty_events;
	status->test_stale = test_stale_events;

	k_spin_unlock(&ring_lock, key);
}

int flpr_ring_mgr_produce_block(const uint8_t *pcm_data, uint16_t valid_frames, uint32_t sequence,
				int32_t correction_ppm, bool compute_crc)
{
	uint32_t idx;
	int ret;

	ret = flpr_ring_produce_begin(RING_INPUT_BASE, &idx);
	if (ret != 0) {
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		test_full_events++;
		k_spin_unlock(&ring_lock, key);
		return -ENOSPC;
	}

	uint8_t *slot = flpr_ring_slot_base(RING_INPUT_BASE, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);

	/* Fill metadata. */
	meta->sequence = sequence;
	meta->epoch = ring_stream_epoch;
	meta->valid_frames = valid_frames;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	meta->correction_ppm = correction_ppm;

	/* Fill payload. */
	size_t copy_bytes = (size_t)valid_frames * 2U * 2U; /* stereo 16-bit */
	if (copy_bytes > FLPR_RING_PAYLOAD_BYTES) {
		copy_bytes = FLPR_RING_PAYLOAD_BYTES;
	}
	memcpy(flpr_ring_slot_payload(slot), pcm_data, copy_bytes);

	/* Compute CRC if requested (test mode). */
	if (compute_crc) {
		meta->crc32 =
			flpr_ring_crc32(flpr_ring_slot_payload(slot), FLPR_RING_PAYLOAD_BYTES);
	} else {
		meta->crc32 = 0;
	}

	/* Publish. */
	flpr_ring_produce_commit(RING_INPUT_BASE, idx);

	return 0;
}

int flpr_ring_mgr_consume_block(uint8_t *pcm_out, uint16_t *valid_frames_out,
				uint32_t *sequence_out, uint32_t *crc32_out)
{
	uint8_t *slot_base;
	struct flpr_ring_slot_meta *meta;
	int ret;

	ret = flpr_ring_consume_begin(RING_OUTPUT_BASE, ring_stream_epoch, &slot_base, &meta);
	if (ret == -1) {
		return -ENOENT;
	}
	if (ret == -2) {
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		test_stale_events++;
		k_spin_unlock(&ring_lock, key);
		return -ESTALE;
	}

	/* Read metadata. */
	if (valid_frames_out) {
		*valid_frames_out = meta->valid_frames;
	}
	if (sequence_out) {
		*sequence_out = meta->sequence;
	}
	if (crc32_out) {
		*crc32_out = meta->crc32;
	}

	/* Read payload. */
	if (pcm_out) {
		memcpy(pcm_out, flpr_ring_slot_payload(slot_base), FLPR_RING_PAYLOAD_BYTES);
	}

	/* Verify CRC if present. */
	if (test_active && meta->crc32 != 0) {
		uint32_t computed =
			flpr_ring_crc32(flpr_ring_slot_payload(slot_base), FLPR_RING_PAYLOAD_BYTES);
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

	return 0;
}

/* ── Ring test ──────────────────────────────────────────────────── */

int flpr_ring_mgr_test_start(uint32_t block_count)
{
	k_spinlock_key_t key = k_spin_lock(&ring_lock);
	if (test_active) {
		k_spin_unlock(&ring_lock, key);
		return -EBUSY;
	}
	if (!rings_initialized || ring_stream_epoch == 0) {
		k_spin_unlock(&ring_lock, key);
		return -EAGAIN;
	}

	test_active = true;
	test_blocks_sent = 0;
	test_blocks_recv = 0;
	test_crc_errors = 0;
	test_seq_gaps = 0;
	test_full_events = 0;
	test_empty_events = 0;
	test_stale_events = 0;
	test_start_time_ms = k_uptime_get_32();
	k_spin_unlock(&ring_lock, key);

	LOG_INF("Ring test started: target=%u blocks", block_count);
	return 0;
}

int flpr_ring_mgr_test_run(uint32_t block_count, uint32_t timeout_ms, struct flpr_ring_status *out)
{
	int ret;
	uint32_t sent = 0;
	uint32_t start = k_uptime_get_32();
	uint8_t pattern[FLPR_RING_PAYLOAD_BYTES];

	/* Fill pattern with sequence-based data. */
	memset(pattern, 0, sizeof(pattern));

	ret = flpr_ring_mgr_test_start(block_count);
	if (ret != 0) {
		if (out) {
			flpr_ring_mgr_get_status(out);
		}
		return ret;
	}

	while (sent < block_count) {
		uint32_t elapsed = k_uptime_get_32() - start;
		if (elapsed > timeout_ms) {
			LOG_WRN("Ring test timeout at %u/%u blocks (%u ms)", sent, block_count,
				elapsed);
			break;
		}

		/* Drain output ring first (free up space). */
		while (flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL) == 0) {
			/* Drain. */
		}

		/* Build pattern: first 4 bytes = sequence number. */
		*(uint32_t *)pattern = sent;

		/* Send notification to FLPR that data is available. */
		/* We need IPC access — use handshake module's endpoint. */
		/* For Stage 1, the FLPR polls; we don't strictly need IPC
		 * notifications. But we send a RING_PRODUCER to wake FLPR. */

		/* Produce block with CRC. */
		ret = flpr_ring_mgr_produce_block(pattern, FLPR_RING_PAYLOAD_FRAMES, sent, 0, true);
		if (ret == -ENOSPC) {
			/* Full — drain output, retry. */
			k_msleep(1);
			continue;
		}
		if (ret != 0) {
			LOG_ERR("Produce block %u failed: %d", sent, ret);
			break;
		}

		sent++;

		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		test_blocks_sent = sent;
		k_spin_unlock(&ring_lock, key);

		/* Small yield to let FLPR process. */
		k_msleep(1);
	}

	/* Final drain of output ring. */
	while (flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL) == 0) {
		/* Drain. */
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
