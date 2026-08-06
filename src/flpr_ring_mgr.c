/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPUAPP side of PCM ring management for FLPR shared memory transport.
 * Ring addresses resolved from devicetree, not hardcoded.
 * IPC control through flpr_handshake module's endpoint + handler API.
 *
 * R8: production runtime only.  Acceptance orchestration/state lives in
 * src/flpr_acceptance.c (CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS); core
 * produce/consume paths invoke narrow config-gated acceptance hooks for
 * the diagnostic counters, and the shared control-ACK engine
 * (src/flpr_control_ack.c) owns reset/stall ACK correlation.
 *
 * Lock discipline:
 *   ring_data_lock (mutex) serializes cpuapp-side bulk ring
 *   memory/header operations (produce fill/commit, consume copy/done,
 *   reset, coordinated reset, remote-restarted header reinit,
 *   status header snapshots) against reset/reinit.  IPC callback
 *   contexts never take it.  Fixed order is ring_data_lock then
 *   ring_lock (and the acceptance lock, a leaf); never reverse.
 *   ring_lock protects the production control/diagnostic fields.
 *   flpr_handshake_send_msg() is lock-free (never called under
 *   ring_lock).
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
#include "flpr_control_ack.h"
#include "flpr_ring_mgr_internal.h"
#include "flpr_acceptance.h"

LOG_MODULE_REGISTER(flpr_ring, LOG_LEVEL_INF);

/* ── Ring memory and cycle source ──────────────────────────────────
 * Production: devicetree-resolved shared SRAM at 0x2002C000..0x20030000
 * with fixed-address assertions.  Test mode (FLPR_RING_MGR_NATIVE_TEST):
 * two aligned host arrays of FLPR_RING_TOTAL_SIZE each — native_sim has
 * no MMU/address translation, so the physical addresses cannot be
 * dereferenced.  Test arrays never change production DT. */

#if defined(FLPR_RING_MGR_NATIVE_TEST)

#include "flpr_ring_mgr_hooks.h"

#define RING_INPUT_BASE  flpr_ring_mgr_test_input_ring()
#define RING_OUTPUT_BASE flpr_ring_mgr_test_output_ring()
#define RING_CYCLE_GET() flpr_ring_mgr_test_cycle_get()

#else

#define DT_PCM_RING DT_NODELABEL(pcm_ring)

#if DT_NODE_EXISTS(DT_PCM_RING)
/* pcm_ring: 16 KiB at 0x2002C000.  Split into two 8 KiB SPSC rings. */
#define RING_DT_BASE     DT_REG_ADDR(DT_PCM_RING)
#define RING_DT_SIZE     DT_REG_SIZE(DT_PCM_RING)

BUILD_ASSERT(RING_DT_SIZE == 0x4000U, "pcm_ring DT size must be 16 KiB");
BUILD_ASSERT(RING_DT_SIZE == 2U * FLPR_RING_TOTAL_SIZE,
	     "pcm_ring must fit exactly two 8 KiB rings");
BUILD_ASSERT(FLPR_RING_TOTAL_SIZE == 8192U, "ring size must be 8 KiB");

/* Input ring (CPUAPP→FLPR): lower 8 KiB. */
#define RING_INPUT_BASE  ((uint8_t *)(uintptr_t)(RING_DT_BASE))

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

#define RING_CYCLE_GET() k_cycle_get_32()

#endif /* FLPR_RING_MGR_NATIVE_TEST */

/* ── State ──────────────────────────────────────────────────────── */

/* R1: ring_data_lock (mutex) serializes cpuapp-side bulk ring
 * memory/header operations (produce fill/commit, consume copy/done,
 * reset, coordinated reset, stall transaction, remote-restarted header
 * reinit, stale-test production, status header snapshots) against
 * reset/reinit.  IPC callback contexts never take it.  Fixed order is
 * ring_data_lock then ring_lock; never reverse.  ring_lock continues
 * protecting manager control/diagnostic fields, not bulk copies. */
static K_MUTEX_DEFINE(ring_data_lock);

static struct k_spinlock ring_lock;

static uint32_t ring_stream_epoch;
static bool rings_initialized;

/* Production diagnostic counters (protected by ring_lock). */
static uint32_t diag_notify_sent;
static uint32_t diag_notify_err;
static uint32_t diag_sem_gives;
static uint32_t diag_sem_takes;
static uint32_t diag_stale_notify; /* Stage 2: consumer notifications with wrong epoch */
static uint32_t diag_sem_drained;  /* Stage 2: consume_sem tokens drained at reset */

/* ── Semaphore for consumer notifications ────────────────────────── */

static struct k_sem consume_sem;

/* ── Coordinated reset ACK correlation (R1, engine-owned) ───────────
 * The stall ACK instance lives in flpr_acceptance.c; both are cleared
 * by flpr_control_ack_reset_session() from remote_restarted(). */

static struct k_sem reset_ack_sem;
static struct flpr_control_ack reset_ack_ctl;

/* ── IPC handlers (called from flpr_handshake receive context) ───── */

static void on_ring_reset_ack(const struct flpr_msg *msg, void *user_data)
{
	(void)user_data;
	flpr_control_ack_handle(&reset_ack_ctl, msg);
}

static void on_ring_consumer(const struct flpr_msg *msg, void *user_data)
{
	(void)user_data;
	/* RING_CONSUMER wire contract (FLPR_PROTOCOL_VERSION ≥ 3):
	 *   seq  = (uint16_t)(ring_test_block_count & 0xffff)
	 *   data = ring_stream_epoch (the FLPR-side epoch at produce time)
	 *
	 * Match against local ring_stream_epoch to reject stale
	 * notifications from before a reset / during invalidation window
	 * (epoch=0).  Only matching-epoch notifications give the consume
	 * semaphore.
	 *
	 * Stale rejections are counted for diagnostics; they never
	 * wake the consumer path (no ENOENT from submit against
	 * an empty reset output ring). */
	k_spinlock_key_t key = k_spin_lock(&ring_lock);

	if (msg->data != ring_stream_epoch) {
		diag_stale_notify++;
		k_spin_unlock(&ring_lock, key);
		return; /* stale: do NOT give semaphore */
	}

	/* Matching epoch: record FLPR-reported block count (acceptance
	 * hook, config-gated), give sem. */
#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
	flpr_acceptance_note_flpr_blocks((uint32_t)msg->seq);
#endif
	diag_sem_gives++;
	k_spin_unlock(&ring_lock, key);

	k_sem_give(&consume_sem);
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

/* Local reset body: caller MUST hold ring_data_lock (defined below). */
static int flpr_ring_mgr_reset_locked(uint32_t new_epoch);

int flpr_ring_mgr_init(void)
{
	struct flpr_status hs;

	flpr_handshake_get_status(&hs);
	if (!hs.ready || !hs.acked) {
		LOG_WRN("FLPR not ready — ring init deferred");
		return -EAGAIN;
	}

	/* R1 repair: the full initialization decision and first-init
	 * mutation run under ring_data_lock so a repeated/concurrent init
	 * (public contract: repeated calls are safe; the shell exposes
	 * direct init) can never reinitialize live semaphores, handlers,
	 * headers, epoch, counters, or queued ring data.  If already
	 * initialized, init is a non-destructive no-op.  ring_lock is
	 * taken only for the rings_initialized inspection and the final
	 * publish — never across k_sem_init, handler registration, or
	 * ring-memory initialization.  Lock order preserved:
	 * ring_data_lock → ring_lock. */
	k_mutex_lock(&ring_data_lock, K_FOREVER);

	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		bool already = rings_initialized;

		k_spin_unlock(&ring_lock, key);
		if (already) {
			k_mutex_unlock(&ring_data_lock);
			return 0;
		}
	}

	/* Initialize semaphores. */
	k_sem_init(&consume_sem, 0, 1000001);
	k_sem_init(&reset_ack_sem, 0, 1);
	flpr_control_ack_init(&reset_ack_ctl, &reset_ack_sem);
	flpr_control_ack_register(&reset_ack_ctl);

	/* Register the PRODUCTION IPC handlers for ring messages (reset
	 * ACK + consumer).  Diagnostic handlers (report/stall/pong/hang)
	 * are registered by flpr_acceptance_init(). */
	flpr_handshake_register_ring_handlers(on_ring_reset_ack, on_ring_consumer, NULL);

	/* Initialize both rings in shared memory. */
	flpr_ring_init(RING_INPUT_BASE, FLPR_RING_CPUAPP_TO_FLPR);
	flpr_ring_init(RING_OUTPUT_BASE, FLPR_RING_FLPR_TO_CPUAPP);

	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		rings_initialized = true;
		ring_stream_epoch = 0; /* not yet agreed */
		k_spin_unlock(&ring_lock, key);
	}

	k_mutex_unlock(&ring_data_lock);

#if defined(FLPR_RING_MGR_NATIVE_TEST)
	LOG_INF("PCM rings at %p (in) / %p (out), 481-frame capacity", (void *)RING_INPUT_BASE,
		(void *)RING_OUTPUT_BASE);
#else
	LOG_INF("PCM rings at 0x%08x (in) / 0x%08x (out), 481-frame capacity", RING_DT_BASE,
		RING_DT_BASE + FLPR_RING_TOTAL_SIZE);
#endif

	return 0;
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
		new_epoch = RING_CYCLE_GET();
	}
	if (new_epoch == 0) {
		LOG_ERR("Failed to generate non-zero epoch");
		return -EINVAL;
	}

	/* R1: hold ring_data_lock across the entire coordinated reset
	 * (send/ACK wait/local reset) so a concurrent produce/consume can
	 * never mutate ring memory/headers mid-transaction.  The ACK
	 * callback takes ring_lock only, so it can always wake the waiter. */
	k_mutex_lock(&ring_data_lock, K_FOREVER);

	/* Disarm/clear + drain, then allocate the next nonzero 16-bit
	 * request token and arm before send. */
	flpr_control_ack_begin(&reset_ack_ctl);
	int token = flpr_control_ack_arm(&reset_ack_ctl, new_epoch);

	if (token < 0) {
		k_mutex_unlock(&ring_data_lock);
		return token; /* -EOVERFLOW: token space exhausted this session */
	}

	LOG_INF("Coordinated reset: proposing epoch=%u to FLPR", new_epoch);

	/* Send RING_RESET to FLPR with proposed epoch (token in seq). */
	struct flpr_msg reset_req = {
		.type = FLPR_MSG_RING_RESET,
		.version = FLPR_PROTOCOL_VERSION,
		.seq = (uint16_t)token,
		.data = new_epoch,
	};
	ret = flpr_handshake_send_msg(&reset_req);

	if (ret < 0) {
		flpr_control_ack_disarm(&reset_ack_ctl);
		LOG_ERR("RING_RESET send failed: %d", ret);
		k_mutex_unlock(&ring_data_lock);
		return -EIO;
	}

	/* Wait for RING_RESET_ACK and verify the exact epoch. */
	ret = flpr_control_ack_wait(&reset_ack_ctl, timeout_ms, new_epoch);
	if (ret != 0) {
		if (ret == -ETIMEDOUT) {
			LOG_WRN("RING_RESET_ACK timeout (%u ms)", timeout_ms);
		} else {
			LOG_ERR("RING_RESET_ACK epoch mismatch: expected %u", new_epoch);
		}
		k_mutex_unlock(&ring_data_lock);
		return ret;
	}

	/* Apply the epoch reset on CPUAPP side. */
	ret = flpr_ring_mgr_reset_locked(new_epoch);
	if (ret != 0) {
		k_mutex_unlock(&ring_data_lock);
		return ret;
	}

	LOG_INF("Coordinated ring reset: epoch=%u", new_epoch);
	k_mutex_unlock(&ring_data_lock);
	return 0;
}

/* Local reset body: caller MUST hold ring_data_lock. */
static int flpr_ring_mgr_reset_locked(uint32_t new_epoch)
{
	if (new_epoch == 0) {
		return -EINVAL;
	}

	/* Step 1: invalidate in-flight notifications BEFORE touching
	 * shared rings.  Set ring_stream_epoch=0 under lock so any
	 * notification arriving between now and the final publish
	 * is rejected as stale (epoch mismatch). */
	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		ring_stream_epoch = 0;
		k_spin_unlock(&ring_lock, key);
	}

	/* Step 2: reset shared rings.  On failure leave epoch invalid
	 * (already 0) and return error — no partial state. */
	int ret_in = flpr_ring_reset_epoch(RING_INPUT_BASE, new_epoch);
	int ret_out = flpr_ring_reset_epoch(RING_OUTPUT_BASE, new_epoch);

	if (ret_in != 0 || ret_out != 0) {
		return -EINVAL;
	}

	/* Step 3: drain stale consume_sem tokens into a local accumulator.
	 * Any token in-flight from a previous epoch would wake submit
	 * against an empty reset output ring, producing -ENOENT.
	 * K_NO_WAIT — never block here. */
	uint32_t drained = 0;
	while (k_sem_take(&consume_sem, K_NO_WAIT) == 0) {
		drained++;
	}

	/* Step 4: publish new epoch + reset production diagnostics under
	 * lock.  diag_sem_drained set to drained (NOT zeroed) so callers
	 * can observe how many tokens were flushed.  Acceptance counters
	 * (test/latency/stall state) are reset by the acceptance module
	 * through flpr_acceptance_remote_restarted(). */
	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		ring_stream_epoch = new_epoch;
		diag_notify_sent = 0;
		diag_notify_err = 0;
		diag_sem_gives = 0;
		diag_sem_takes = 0;
		diag_stale_notify = 0;
		diag_sem_drained = drained;
		k_spin_unlock(&ring_lock, key);

		LOG_INF("PCM rings reset: epoch=%u", new_epoch);
		return 0;
	}
}

int flpr_ring_mgr_reset(uint32_t new_epoch)
{
	k_mutex_lock(&ring_data_lock, K_FOREVER);
	int ret = flpr_ring_mgr_reset_locked(new_epoch);
	k_mutex_unlock(&ring_data_lock);
	return ret;
}

void flpr_ring_mgr_get_status(struct flpr_ring_status *status)
{
	if (!status) {
		return;
	}
	memset(status, 0, sizeof(*status));

	/* R1: ring header snapshots need ring_data_lock so reset/reinit
	 * cannot tear them mid-read. */
	k_mutex_lock(&ring_data_lock, K_FOREVER);

	k_spinlock_key_t key = k_spin_lock(&ring_lock);

	status->initialized = rings_initialized;
	status->epoch = ring_stream_epoch;

	status->notify_sent = diag_notify_sent;
	status->notify_err = diag_notify_err;
	status->sem_gives = diag_sem_gives;
	status->sem_takes = diag_sem_takes;
	status->stale_notify = diag_stale_notify;
	status->sem_drained = diag_sem_drained;

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

	k_spin_unlock(&ring_lock, key);

	k_mutex_unlock(&ring_data_lock);
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

	/* R1: hold ring_data_lock across produce begin/fill/commit so a
	 * concurrent reset cannot zero headers/memory mid-produce. */
	k_mutex_lock(&ring_data_lock, K_FOREVER);

	/* Stall injection (acceptance-owned, config-gated). */
#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
	{
		if (flpr_acceptance_stall_producer_active()) {
			flpr_acceptance_note_backpressure();
			k_mutex_unlock(&ring_data_lock);
			return FLPR_PRODUCE_FULL;
		}
	}
#endif

	/* Snapshot the stream epoch under ring_lock while the data lock
	 * prevents reset.  Epoch 0 (not yet agreed / invalidated) makes
	 * produce return INVALID before any slot mutation. */
	uint32_t epoch;

	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		epoch = ring_stream_epoch;
		k_spin_unlock(&ring_lock, key);
	}
	if (epoch == 0) {
		k_mutex_unlock(&ring_data_lock);
		return FLPR_PRODUCE_INVALID;
	}

	ret = flpr_ring_produce_begin(RING_INPUT_BASE, &idx);
	if (ret == -ENOSPC) {
#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
		flpr_acceptance_note_full();
#endif
		k_mutex_unlock(&ring_data_lock);
		return FLPR_PRODUCE_FULL;
	}
	if (ret != 0) {
		k_mutex_unlock(&ring_data_lock);
		return FLPR_PRODUCE_INVALID;
	}

#if defined(FLPR_RING_MGR_NATIVE_TEST)
	/* R1 barrier-test hook: pause after produce begin while still
	 * holding ring_data_lock (see flpr_ring_mgr_hooks.h). */
	flpr_ring_mgr_test_pause_after_produce_begin();
#endif

	uint8_t *slot = flpr_ring_slot_base(RING_INPUT_BASE, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);

	/* Fill metadata (local epoch snapshot). */
	meta->sequence = sequence;
	meta->epoch = epoch;
	meta->valid_frames = valid_frames;
	meta->flags = FLPR_SLOT_FLAG_VALID;
	meta->correction_ppm = correction_ppm;
	meta->cpu_timestamp = RING_CYCLE_GET();

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

	k_mutex_unlock(&ring_data_lock);
	return FLPR_PRODUCE_OK;
}

enum flpr_consume_result flpr_ring_mgr_consume_block(uint8_t *pcm_out, uint16_t *valid_frames_out,
						     uint32_t *sequence_out, uint32_t *crc32_out,
						     uint32_t *latency_cycles_out)
{
	uint8_t *slot_base;
	struct flpr_ring_slot_meta *meta;
	int ret;

	/* R1: hold ring_data_lock across consume begin/copy/done so a
	 * concurrent reset cannot zero headers/memory mid-consume. */
	k_mutex_lock(&ring_data_lock, K_FOREVER);

	/* Epoch snapshot under ring_lock (reset cannot run: data lock). */
	uint32_t epoch;

	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		epoch = ring_stream_epoch;
		k_spin_unlock(&ring_lock, key);
	}

	ret = flpr_ring_consume_begin(RING_OUTPUT_BASE, epoch, &slot_base, &meta);
	if (ret == -ENOENT) {
		k_mutex_unlock(&ring_data_lock);
		return FLPR_CONSUME_EMPTY;
	}
	if (ret == -ESTALE) {
#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
		flpr_acceptance_note_stale();
#endif
		k_mutex_unlock(&ring_data_lock);
		return FLPR_CONSUME_STALE;
	}

	/* Read metadata. */
	uint16_t vf = meta->valid_frames;
	/* Reject invalid frame counts instead of silently clamping. */
	if (vf > FLPR_RING_PAYLOAD_CAPACITY_FRAMES) {
		flpr_ring_consume_done(RING_OUTPUT_BASE);
		k_mutex_unlock(&ring_data_lock);
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
		uint32_t now = RING_CYCLE_GET();
		uint32_t latency = now - meta->cpu_timestamp;
		if (latency > 0) {
			*latency_cycles_out = latency;
		} else {
			*latency_cycles_out = 0;
		}

		/* Track min/max/avg (acceptance-owned, config-gated). */
#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
		if (latency > 0 && flpr_acceptance_test_active()) {
			flpr_acceptance_note_latency(latency);
		}
#endif
	}

	/* Read payload. */
	if (pcm_out) {
		memcpy(pcm_out, flpr_ring_slot_payload(slot_base), vf > 0 ? (size_t)vf * 4U : 0);
	}

	/* Verify CRC over valid bytes if present (acceptance test mode). */
#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
	if (flpr_acceptance_test_active() && meta->crc32 != 0) {
		uint32_t computed = flpr_ring_crc32(flpr_ring_slot_payload(slot_base),
						    vf > 0 ? (size_t)vf * 4U : 0);
		if (computed != meta->crc32) {
			flpr_acceptance_note_crc_err();
		}
	}

	/* Independent payload verification: regenerate from sequence
	 * and memcmp (acceptance test mode). */
	if (flpr_acceptance_test_active() && vf > 0) {
		uint32_t frame_errs = flpr_ring_verify_payload(flpr_ring_slot_payload(slot_base),
							       (size_t)vf * 4U, meta->sequence);
		if (frame_errs > 0) {
			flpr_acceptance_note_payload_err(frame_errs);
		}
	}
#endif

	flpr_ring_consume_done(RING_OUTPUT_BASE);

#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
	flpr_acceptance_note_recv();
#endif

	k_mutex_unlock(&ring_data_lock);
	return FLPR_CONSUME_OK;
}

int flpr_ring_mgr_wait_consume(uint32_t timeout_ms)
{
	return k_sem_take(&consume_sem, K_MSEC(timeout_ms));
}

/* ── Remote restart (Stage 4B) ──────────────────────────────────────
 *
 * Reinitialize rings after FLPR was reset by a remote restart.
 * Callable only while offload is RECOVERING/stopped — no active submit
 * may race this call.
 *
 * 1. Invalidate local epoch (ring_stream_epoch = 0).
 * 2. Drain consumer/reset/stall semaphores (engine clears every
 *    registered control-ACK instance and resets its token counter).
 * 3. Reinitialize shared headers and handlers (ring_init on both rings).
 *
 * Must be followed by flpr_ring_mgr_coordinated_reset() with nonzero epoch
 * to establish the new stream epoch with the restarted FLPR.
 */

int flpr_ring_mgr_remote_restarted(void)
{
	/* R1: hold ring_data_lock across the remote-restarted header
	 * reinit (a known quiescence boundary: no active submit may race
	 * this call). */
	k_mutex_lock(&ring_data_lock, K_FOREVER);

	/* Step 1: Invalidate local epoch.
	 * Set ring_stream_epoch = 0 under lock so any notification arriving
	 * between now and the next coordinated reset is rejected as stale. */
	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		ring_stream_epoch = 0;
		k_spin_unlock(&ring_lock, key);
	}

	/* Step 2: Drain consumer semaphore. */
	{
		uint32_t drained = 0;
		while (k_sem_take(&consume_sem, K_NO_WAIT) == 0) {
			drained++;
		}
		LOG_INF("ring remote restart: drained %u consume_sem tokens", drained);
	}

	/* Step 3: Clear ALL registered control-ACK request state, reset
	 * the 16-bit token counters, and drain the ACK semaphores
	 * (quiescence boundary). */
	flpr_control_ack_reset_session();

	/* Step 4: Reinitialize shared rings in shared memory.
	 * The restarted FLPR already called ring_init on its side.
	 * We re-init here so headers are consistent. */
	flpr_ring_init(RING_INPUT_BASE, FLPR_RING_CPUAPP_TO_FLPR);
	flpr_ring_init(RING_OUTPUT_BASE, FLPR_RING_FLPR_TO_CPUAPP);

	/* Step 5: Reset production diagnostics.  Acceptance counters and
	 * stall state are reset by the acceptance module. */
	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		diag_notify_sent = 0;
		diag_notify_err = 0;
		diag_sem_gives = 0;
		diag_sem_takes = 0;
		diag_stale_notify = 0;
		diag_sem_drained = 0;
		k_spin_unlock(&ring_lock, key);
	}

#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
	flpr_acceptance_remote_restarted();
#endif

	LOG_INF("ring remote restart: reinitialized shared headers, epoch invalidated");
	k_mutex_unlock(&ring_data_lock);
	return 0;
}

/* ── Typed ASRC produce / consume ─────────────────────────────────── */

enum flpr_produce_result flpr_ring_mgr_produce_asrc(const int16_t *pcm_data, uint16_t valid_frames,
						    uint32_t sequence, int32_t correction_ppm,
						    const struct audio_asrc_state *pre_state)
{
	uint32_t idx;
	int ret;

	if (valid_frames != FLPR_RING_PAYLOAD_MAX_INPUT || !pre_state) {
		return FLPR_PRODUCE_INVALID;
	}

	/* R1: ring_data_lock across produce begin/fill/commit (see
	 * produce_block). */
	k_mutex_lock(&ring_data_lock, K_FOREVER);

	/* Stall injection (acceptance-owned, config-gated). */
#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
	{
		if (flpr_acceptance_stall_producer_active()) {
			flpr_acceptance_note_backpressure();
			k_mutex_unlock(&ring_data_lock);
			return FLPR_PRODUCE_FULL;
		}
	}
#endif

	/* Epoch snapshot under ring_lock; epoch 0 → INVALID before slot
	 * mutation. */
	uint32_t epoch;

	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		epoch = ring_stream_epoch;
		k_spin_unlock(&ring_lock, key);
	}
	if (epoch == 0) {
		k_mutex_unlock(&ring_data_lock);
		return FLPR_PRODUCE_INVALID;
	}

	ret = flpr_ring_produce_begin(RING_INPUT_BASE, &idx);
	if (ret == -ENOSPC) {
#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
		flpr_acceptance_note_full();
#endif
		k_mutex_unlock(&ring_data_lock);
		return FLPR_PRODUCE_FULL;
	}
	if (ret != 0) {
		k_mutex_unlock(&ring_data_lock);
		return FLPR_PRODUCE_INVALID;
	}

#if defined(FLPR_RING_MGR_NATIVE_TEST)
	/* R1 barrier-test hook (see flpr_ring_mgr_hooks.h). */
	flpr_ring_mgr_test_pause_after_produce_begin();
#endif

	uint8_t *slot = flpr_ring_slot_base(RING_INPUT_BASE, idx);
	struct flpr_ring_slot_meta *meta = flpr_ring_slot_meta_ptr(slot);

	/* Fill metadata — ASRC flag set. */
	meta->sequence = sequence;
	meta->epoch = epoch;
	meta->valid_frames = valid_frames;
	meta->flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
	meta->correction_ppm = correction_ppm;
	meta->cpu_timestamp = RING_CYCLE_GET();

	/* Copy typed ASRC pre-state. */
	memcpy(&meta->asrc_state, pre_state, sizeof(*pre_state));

	/* Fill payload. */
	size_t copy_bytes = (size_t)valid_frames * 4U;
	memset(flpr_ring_slot_payload(slot), 0, FLPR_RING_PAYLOAD_CAPACITY_BYTES);
	if (pcm_data && copy_bytes > 0) {
		memcpy(flpr_ring_slot_payload(slot), pcm_data, copy_bytes);
	}

	/* CRC over valid payload — always enabled. */
	meta->crc32 = flpr_ring_crc32(flpr_ring_slot_payload(slot), copy_bytes);

	/* Publish. */
	flpr_ring_produce_commit(RING_INPUT_BASE, idx);

	k_mutex_unlock(&ring_data_lock);
	return FLPR_PRODUCE_OK;
}

enum flpr_consume_result flpr_ring_mgr_consume_asrc_result(int16_t *pcm_out,
							   uint16_t output_capacity,
							   struct flpr_consume_asrc_result *result)
{
	uint8_t *slot_base;
	struct flpr_ring_slot_meta *meta;
	int ret;

	if (!pcm_out || !result || output_capacity < FLPR_RING_PAYLOAD_CAPACITY_FRAMES) {
		return FLPR_CONSUME_INVALID;
	}

	/* Zero result output_frames before anything — "untouched on failure"
	 * means the caller sees output_frames=0 on error. */
	result->output_frames = 0;

	/* R1: hold ring_data_lock across consume begin/copy/done. */
	k_mutex_lock(&ring_data_lock, K_FOREVER);

	/* Epoch snapshot under ring_lock (reset cannot run: data lock). */
	uint32_t epoch;

	{
		k_spinlock_key_t key = k_spin_lock(&ring_lock);
		epoch = ring_stream_epoch;
		k_spin_unlock(&ring_lock, key);
	}

	ret = flpr_ring_consume_begin(RING_OUTPUT_BASE, epoch, &slot_base, &meta);
	if (ret == -ENOENT) {
		k_mutex_unlock(&ring_data_lock);
		return FLPR_CONSUME_EMPTY;
	}
	if (ret == -ESTALE) {
#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
		flpr_acceptance_note_stale();
#endif
		k_mutex_unlock(&ring_data_lock);
		return FLPR_CONSUME_STALE;
	}
	if (ret != 0) {
		k_mutex_unlock(&ring_data_lock);
		return FLPR_CONSUME_INVALID;
	}

	/* Validate ASRC flag is set. */
	if (!(meta->flags & FLPR_SLOT_FLAG_ASRC_LINEAR)) {
		flpr_ring_consume_done(RING_OUTPUT_BASE);
		k_mutex_unlock(&ring_data_lock);
		return FLPR_CONSUME_INVALID;
	}

	/* Validate frame range: 1..481 for normal output.
	 * Error output from FLPR (processing_status < 0, valid_frames=0)
	 * is a valid transport response — we return it as CONSUME_OK with
	 * output_frames=0 so caller can distinguish from EMPTY/STALE. */
	uint16_t vf = meta->valid_frames;
	if (vf > FLPR_RING_PAYLOAD_CAPACITY_FRAMES) {
		flpr_ring_consume_done(RING_OUTPUT_BASE);
		k_mutex_unlock(&ring_data_lock);
		return FLPR_CONSUME_INVALID;
	}

	/* Error output: valid_frames=0, processing_status<0 is valid. */
	if (vf == 0 && meta->processing_status >= 0) {
		flpr_ring_consume_done(RING_OUTPUT_BASE);
		k_mutex_unlock(&ring_data_lock);
		return FLPR_CONSUME_INVALID;
	}

	/* Validate reserved bytes in asrc_state are zero. */
	{
		const uint8_t *res = meta->asrc_state.reserved;
		if (res[0] != 0 || res[1] != 0 || res[2] != 0) {
			flpr_ring_consume_done(RING_OUTPUT_BASE);
			k_mutex_unlock(&ring_data_lock);
			return FLPR_CONSUME_INVALID;
		}
	}

	/* Verify payload CRC (recompute) BEFORE copying anything to the
	 * caller: the header contract guarantees validation failures leave
	 * the caller's PCM buffer untouched (only result->output_frames is
	 * zeroed).  CRC reads the ring payload directly — no scratch
	 * buffer. */
	if (vf > 0 && meta->crc32 != 0) {
		uint32_t computed =
			flpr_ring_crc32(flpr_ring_slot_payload(slot_base), (size_t)vf * 4U);
		if (computed != meta->crc32) {
			flpr_ring_consume_done(RING_OUTPUT_BASE);
			k_mutex_unlock(&ring_data_lock);
			return FLPR_CONSUME_INVALID;
		}
	}

	/* Read payload only after every validation passed. */
	if (vf > 0) {
		size_t copy_bytes = (size_t)vf * 4U;
		memcpy(pcm_out, flpr_ring_slot_payload(slot_base), copy_bytes);
	}

	/* Fill result — snapshot metadata BEFORE consume_done.
	 * Recompute payload CRC for defense in depth. */
	result->output_frames = vf;
	result->sequence = meta->sequence;
	result->flags = meta->flags;
	result->correction_ppm = meta->correction_ppm;

	/* Recompute payload CRC independently. */
	if (vf > 0) {
		result->payload_crc =
			flpr_ring_crc32(flpr_ring_slot_payload(slot_base), (size_t)vf * 4U);
	} else {
		result->payload_crc = 0;
	}

	memcpy(&result->post_state, &meta->asrc_state, sizeof(result->post_state));
	result->processing_cycles = meta->processing_cycles;
	result->processing_status = meta->processing_status;

	/* RTT from cpu_timestamp. */
	uint32_t now = RING_CYCLE_GET();
	uint32_t latency = now - meta->cpu_timestamp;
	result->rtt_cycles = (latency > 0) ? latency : 0;

	flpr_ring_consume_done(RING_OUTPUT_BASE);

#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)
	flpr_acceptance_note_recv();
#endif

	k_mutex_unlock(&ring_data_lock);
	return FLPR_CONSUME_OK;
}

/* ── Internal context accessors (R8) ────────────────────────────────
 * Shared with the acceptance module so it can hold the SAME bulk-op
 * mutex over ring memory (no duplicate locks) and reach the raw output
 * ring for the stale-epoch diagnostic probe. */

struct k_mutex *flpr_ring_mgr_data_lock(void)
{
	return &ring_data_lock;
}

uint8_t *flpr_ring_mgr_input_ring(void)
{
	return RING_INPUT_BASE;
}

uint8_t *flpr_ring_mgr_output_ring(void)
{
	return RING_OUTPUT_BASE;
}

#if defined(FLPR_RING_MGR_NATIVE_TEST)
/* GCOVR_EXCL_START — test-only helpers, absent from production builds */

/* ── Test-only helpers ─────────────────────────────────────────────
 * Compiled only under FLPR_RING_MGR_NATIVE_TEST (native_sim suite).
 * Production builds contain none of these symbols.  These helpers reset
 * module-static state and expose ring memory/semaphore observability;
 * they never implement state transitions — production code does. */

/* R1 barrier-test gate state (native test builds only). */
static struct k_sem *pause_entered;
static struct k_sem *pause_release;
static bool pause_armed;

void flpr_ring_mgr_test_reset_state(void)
{
	/* Re-initialize semaphores and module state for a clean test. */
	k_sem_init(&consume_sem, 0, 1000001);
	k_sem_init(&reset_ack_sem, 0, 1);
	flpr_control_ack_init(&reset_ack_ctl, &reset_ack_sem);
	pause_armed = false;
	pause_entered = NULL;
	pause_release = NULL;

	ring_stream_epoch = 0;
	rings_initialized = false;

	diag_notify_sent = 0;
	diag_notify_err = 0;
	diag_sem_gives = 0;
	diag_sem_takes = 0;
	diag_stale_notify = 0;
	diag_sem_drained = 0;

	flpr_ring_init(RING_INPUT_BASE, FLPR_RING_CPUAPP_TO_FLPR);
	flpr_ring_init(RING_OUTPUT_BASE, FLPR_RING_FLPR_TO_CPUAPP);
}

/* ── R1 test hooks ─────────────────────────────────────────────── */

void flpr_ring_mgr_test_arm_pause_after_produce_begin(struct k_sem *entered, struct k_sem *release)
{
	pause_armed = (entered != NULL && release != NULL);
	pause_entered = entered;
	pause_release = release;
}

/* Called by produce_block/produce_asrc after produce begin while still
 * holding ring_data_lock.  One-shot: signals entered, waits on release. */
void flpr_ring_mgr_test_pause_after_produce_begin(void)
{
	if (pause_armed) {
		pause_armed = false;
		if (pause_entered != NULL && pause_release != NULL) {
			k_sem_give(pause_entered);
			k_sem_take(pause_release, K_FOREVER);
		}
	}
}

/* Reset-side ACK correlation observability (stall side lives in the
 * acceptance module). */
uint32_t flpr_ring_mgr_test_stale_ack_count(void)
{
	return flpr_control_ack_test_stale_count(&reset_ack_ctl);
}

void flpr_ring_mgr_test_set_next_reset_token(uint16_t token)
{
	flpr_control_ack_test_set_next_token(&reset_ack_ctl, token);
}

uint32_t flpr_ring_mgr_test_consume_sem_count(void)
{
	return k_sem_count_get(&consume_sem);
}

uint32_t flpr_ring_mgr_test_reset_ack_sem_count(void)
{
	return k_sem_count_get(&reset_ack_sem);
}
/* GCOVR_EXCL_STOP */

#endif /* FLPR_RING_MGR_NATIVE_TEST */
