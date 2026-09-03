/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dedicated LE Audio source fixture coordinator (RH1B).
 *
 * Owns shell dispatch, the run worker thread (all blocking BAP operations
 * and resource teardown), the TX stage orchestration (pacing, lockstep,
 * stage caps, outstanding, progress timeout), the universal cleanup, and
 * all HIL1 status/hello formatting.  Bluetooth work goes through the
 * backend ops table: production binds the real BAP/TX modules; under
 * CONFIG_HIL_SOURCE_APP_TEST the native suite injects a fake backend.
 *
 * Emit helpers assume the app mutex is held by the caller (dispatch holds
 * it; the worker takes it around every emit).  The TX stage takes the
 * mutex around shared TX bookkeeping (outstanding/seq/submitted) because
 * the sent-callback handler runs on another thread.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "hil_source_app.h"
#include "hil_source_output.h"
#include "hil_source_protocol.h"
#include "hil_source_record.h"
#include "hil_source_signal.h"
#include "hil_source_state.h"
#include "hil_source_types.h"

#define HIL_APP_ENABLE_BUSY_RETRY_INTERVAL_MS 10U
#define HIL_APP_ENABLE_BUSY_RETRY_TIMEOUT_MS  1000U
#define HIL_APP_STREAM_CONNECT_RETRY_DELAY_MS 10U
#define HIL_APP_STREAM_CONNECT_MAX_RETRIES    1U

#ifdef CONFIG_HIL_SOURCE_APP_TEST
static const struct hil_source_backend_ops *g_backend_ops;
#else
#include "hil_source_bap.h"
static const struct hil_source_backend_ops *g_backend_ops;
#endif

/* ── app state ───────────────────────────────────────────────────── */

static struct hil_source_state run_state;
static char config_command_id[HIL_SOURCE_MAX_COMMAND_ID_LEN + 1U];
static char config_run_id[HIL_SOURCE_MAX_RUN_ID_LEN + 1U];
static bool have_config_ids;
static char async_command_id[HIL_SOURCE_MAX_COMMAND_ID_LEN + 1U];
static bt_addr_le_t run_addr_le;
static bool conn_owned;
static bool runtime_error;
static bool worker_started;

static K_MUTEX_DEFINE(app_mutex);
static K_SEM_DEFINE(sem_worker, 0, 1);
static K_SEM_DEFINE(sem_run_done, 0, 1);
/* Binary condition wake for TX backpressure and drain waits.  The logical
 * outstanding counters remain the source of truth; a token only says that
 * state may have changed. */
static K_SEM_DEFINE(sem_tx_wake, 0, 1);
K_THREAD_STACK_DEFINE(worker_stack, 8192);
static struct k_thread worker_thread;

/* ── TX stage state (per segment) ────────────────────────────────── */

static uint32_t tx_outstanding[HIL_SOURCE_MAX_STREAMS];
static uint16_t tx_seq[HIL_SOURCE_MAX_STREAMS];
static uint32_t tx_submitted[HIL_SOURCE_MAX_STREAMS];
static struct hil_source_signal_encoder tx_encoders[HIL_SOURCE_MAX_STREAMS];
static enum hil_source_signal_stage tx_enc_stage[HIL_SOURCE_MAX_STREAMS];
static int64_t tx_last_activity[HIL_SOURCE_MAX_STREAMS];
static bool tx_active;

/* ── helpers ─────────────────────────────────────────────────────── */

/* These helpers are called with app_mutex held.  Semaphore give/reset do not
 * wait and do not call Bluetooth, so keeping them in the same critical
 * section orders a callback wake against cleanup and the next TX generation. */
static void hil_app_tx_wake_locked(void)
{
	k_sem_give(&sem_tx_wake);
}

static void hil_app_tx_wake_reset_locked(void)
{
	k_sem_reset(&sem_tx_wake);
}

/* Wait until TX state changes or deadline expires.  Caller must not hold the
 * app mutex.  The caller rechecks stop/error/deadline after every return so a
 * stop racing the timeout is still reported as cancellation. */
static int hil_app_tx_wait_until(int64_t deadline)
{
	int64_t remaining = deadline - k_uptime_get();
	uint32_t timeout_ms;

	if (remaining <= 0) {
		return -ETIMEDOUT;
	}
	timeout_ms = (remaining > (int64_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)remaining;
	return k_sem_take(&sem_tx_wake, K_MSEC(timeout_ms));
}

static void hil_app_set_runtime_error_locked(void)
{
	runtime_error = true;
	hil_app_tx_wake_locked();
}

static bool hil_app_stop_pending_locked(void)
{
	return run_state.stop_requested || runtime_error;
}

static int hil_app_advance_state(enum hil_source_run_state next)
{
	int ret = hil_source_state_advance(&run_state, next);

	if (ret != 0) {
		hil_app_set_runtime_error_locked();
	}
	return ret;
}

/* Wait on an event semaphore with a deadline, polling the stop flag at
 * least every 100 ms.  Returns 0, -ECANCELED (host stop), -EIO (fatal
 * runtime error), or -ETIMEDOUT.  Caller must NOT hold the app mutex. */
static int hil_app_wait_event(struct k_sem *sem, uint32_t timeout_ms)
{
	uint32_t waited = 0U;

	while (waited < timeout_ms) {
		uint32_t slice;
		bool stop_requested;
		bool fatal;

		k_mutex_lock(&app_mutex, K_FOREVER);
		stop_requested = run_state.stop_requested;
		fatal = runtime_error;
		k_mutex_unlock(&app_mutex);
		if (stop_requested || fatal) {
			return stop_requested ? -ECANCELED : -EIO;
		}
		slice = MIN(100U, timeout_ms - waited);
		if (k_sem_take(sem, K_MSEC(slice)) == 0) {
			return 0;
		}
		waited += slice;
	}
	return -ETIMEDOUT;
}

/* Cleanup-specific bounded wait.  Unlike hil_app_wait_event it never
 * exits because a stop request or runtime error is already set: cleanup
 * must observe real disable/release/disconnect completions, and a stop
 * must not short-circuit those waits.  Returns 0 or -ETIMEDOUT. */
static int hil_app_wait_cleanup(struct k_sem *sem, uint32_t timeout_ms)
{
	if (k_sem_take(sem, K_MSEC(timeout_ms)) == 0) {
		return 0;
	}
	return -ETIMEDOUT;
}

/* The RH0 wire parser consumes "HIL1 " + JSON; the record formatters emit
 * the JSON envelope only, so every submit path prefixes the record. */
static int hil_app_submit_record(const char *record, size_t len)
{
	char line[HIL_SOURCE_OUTPUT_LINE_SIZE];

	if (record == NULL || len + 5U > HIL_SOURCE_OUTPUT_LINE_SIZE) {
		return -EOVERFLOW;
	}
	memcpy(line, "HIL1 ", 5U);
	memcpy(line + 5U, record, len);
	return hil_source_output_submit(line, len + 5U, HIL_SOURCE_OUTPUT_SUBMIT_TIMEOUT_MS);
}

static int hil_app_emit_record_line(char *line, size_t len)
{
	if (hil_app_submit_record(line, len) != 0) {
		hil_app_set_runtime_error_locked();
		return -EIO;
	}
	return 0;
}

/* ── record emission (app mutex held by caller) ──────────────────── */

static int hil_app_emit_state_record(void)
{
	char line[HIL_SOURCE_OUTPUT_LINE_SIZE];
	int n;

	n = hil_source_record_format_state(line, sizeof(line), k_uptime_get_32(), async_command_id,
					   config_run_id, run_state.segment,
					   run_state.current_state);
	if (n < 0) {
		hil_app_set_runtime_error_locked();
		return n;
	}
	if (hil_app_emit_record_line(line, (size_t)n) != 0) {
		return -EIO;
	}
	return 0;
}

static int hil_app_emit_abort(enum hil_source_abort_cause cause)
{
	char line[HIL_SOURCE_OUTPUT_LINE_SIZE];
	int n;

	n = hil_source_record_format_abort_teardown(line, sizeof(line), k_uptime_get_32(),
						    async_command_id, config_run_id,
						    run_state.segment, cause);
	if (n < 0) {
		hil_app_set_runtime_error_locked();
		return n;
	}
	if (hil_app_emit_record_line(line, (size_t)n) != 0) {
		return -EIO;
	}
	return 0;
}

static int hil_app_emit_terminal(enum hil_source_verdict verdict)
{
	char line[HIL_SOURCE_OUTPUT_LINE_SIZE];
	int n;

	n = hil_source_record_format_terminal(line, sizeof(line), k_uptime_get_32(),
					      async_command_id, config_run_id, run_state.segment,
					      verdict);
	if (n < 0) {
		hil_app_set_runtime_error_locked();
		return n;
	}
	if (hil_app_emit_record_line(line, (size_t)n) != 0) {
		return -EIO;
	}
	return 0;
}

/* ── identity helpers ────────────────────────────────────────────── */

static const char *hil_app_addr_type_name(const bt_addr_le_t *addr)
{
	switch (addr->type) {
	case BT_ADDR_LE_PUBLIC:
	case BT_ADDR_LE_PUBLIC_ID:
		return "public";
	case BT_ADDR_LE_RANDOM:
	case BT_ADDR_LE_RANDOM_ID:
		return "random";
	default:
		return "unknown";
	}
}

static void hil_app_addr_to_display(const bt_addr_le_t *addr, char *buf, size_t cap)
{
	int i;
	int n = 0;

	if (cap == 0U) {
		return;
	}
	buf[0] = '\0';
	for (i = 5; i >= 0; i--) {
		int w;

		w = snprintf(buf + n, cap - (size_t)n, "%s%02X", (i < 5) ? ":" : "",
			     addr->a.val[i]);
		if (w < 0 || (size_t)w >= cap - (size_t)n) {
			return;
		}
		n += w;
	}
}

static void hil_app_config_to_le(const struct hil_source_config *cfg, bt_addr_le_t *out)
{
	int i;

	out->type = (cfg->peer_address_type == HIL_SOURCE_ADDR_RANDOM) ? BT_ADDR_LE_RANDOM
								       : BT_ADDR_LE_PUBLIC;
	for (i = 0; i < 6; i++) {
		out->a.val[i] = cfg->peer_address[5 - i];
	}
}

/* ── profile frame counts ────────────────────────────────────────── */

static uint32_t hil_app_preamble_frames(void)
{
	return HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES /
	       hil_source_profile_frame_samples(run_state.config.profile);
}

static uint32_t hil_app_tail_frames(void)
{
	uint32_t us = hil_source_profile_frame_duration_us(run_state.config.profile);

	if (us == 0U) {
		return 0U;
	}
	return (HIL_SOURCE_TAIL_MIN_US + us - 1U) / us;
}

static enum hil_source_signal_stage hil_app_frame_stage(uint32_t submitted, uint32_t preamble,
							uint32_t scored)
{
	if (submitted < preamble) {
		return HIL_SOURCE_SIGNAL_PREAMBLE;
	}
	if (submitted < preamble + scored) {
		return HIL_SOURCE_SIGNAL_SCORED;
	}
	return HIL_SOURCE_SIGNAL_TAIL;
}

/* ── TX stage ────────────────────────────────────────────────────── */

static int hil_app_tx_init_encoders(void)
{
	enum hil_source_mode mode = run_state.config.mode;
	enum hil_source_profile profile = run_state.config.profile;
	uint32_t seed = run_state.config.signal_seed;
	int err;

	memset(tx_encoders, 0, sizeof(tx_encoders));
	switch (mode) {
	case HIL_SOURCE_MODE_MONO:
		err = hil_source_signal_encoder_init(&tx_encoders[0], HIL_SOURCE_MODE_MONO, profile,
						     seed, HIL_SOURCE_CHANNEL_LEFT, -1);
		break;
	case HIL_SOURCE_MODE_A:
		err = hil_source_signal_encoder_init(&tx_encoders[0], HIL_SOURCE_MODE_A, profile,
						     seed, HIL_SOURCE_CHANNEL_LEFT, -1);
		if (err == 0) {
			err = hil_source_signal_encoder_init(&tx_encoders[1], HIL_SOURCE_MODE_A,
							     profile, seed,
							     HIL_SOURCE_CHANNEL_RIGHT, -1);
		}
		break;
	case HIL_SOURCE_MODE_B:
		err = hil_source_signal_encoder_init(&tx_encoders[0], HIL_SOURCE_MODE_B, profile,
						     seed, HIL_SOURCE_CHANNEL_LEFT,
						     HIL_SOURCE_CHANNEL_RIGHT);
		break;
	default:
		return -EINVAL;
	}
	tx_enc_stage[0] = HIL_SOURCE_SIGNAL_PREAMBLE;
	tx_enc_stage[1] = HIL_SOURCE_SIGNAL_PREAMBLE;
	return err;
}

/* Advance encoders to the next stage; emits the scored_complete state
 * record at the scored->tail boundary, before the first tail SDU.
 * App mutex held by caller. */
static int hil_app_tx_ensure_stage(enum hil_source_signal_stage stage, uint8_t streams,
				   bool *scored_complete_emitted)
{
	uint32_t i;

	if (stage == tx_enc_stage[0]) {
		return 0;
	}
	if (stage == HIL_SOURCE_SIGNAL_TAIL && !*scored_complete_emitted) {
		*scored_complete_emitted = true;
		if (hil_app_advance_state(HIL_SOURCE_RUN_STATE_SCORED_COMPLETE) != 0) {
			return -EIO;
		}
		if (hil_app_emit_state_record() != 0) {
			return -EIO;
		}
	}
	for (i = 0U; i < streams; i++) {
		if (hil_source_signal_set_stage(&tx_encoders[i], stage) != 0) {
			return -EIO;
		}
		tx_enc_stage[i] = stage;
	}
	return 0;
}

/* Returns 0 on complete, -ECANCELED (stop), -ETIMEDOUT (progress or
 * drain timeout), or a negative errno (send/encode/internal error). */
static int hil_app_tx_run(void)
{
	uint32_t preamble = hil_app_preamble_frames();
	uint32_t scored = run_state.config.scored_sdu_count;
	uint32_t tail = hil_app_tail_frames();
	uint32_t per_stream_total = preamble + scored + tail;
	uint8_t streams = hil_source_mode_stream_count(run_state.config.mode);
	bool scored_complete_emitted = false;
	uint32_t i;
	int err;

	if (preamble == 0U || tail == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&app_mutex, K_FOREVER);
	/* Start each TX generation with no completion event from a prior
	 * segment/run.  Reset while locked so a stale callback cannot add a token
	 * between reset and activation. */
	hil_app_tx_wake_reset_locked();
	for (i = 0U; i < HIL_SOURCE_MAX_STREAMS; i++) {
		tx_outstanding[i] = 0U;
		tx_seq[i] = 0U;
		tx_submitted[i] = 0U;
		tx_last_activity[i] = k_uptime_get();
	}
	tx_active = true;
	k_mutex_unlock(&app_mutex);

	err = hil_app_tx_init_encoders();
	if (err != 0) {
		return err;
	}

	for (;;) {
		uint32_t work = 0U;
		bool can_send;
		enum hil_source_signal_stage stage;

		k_mutex_lock(&app_mutex, K_FOREVER);
		if (hil_app_stop_pending_locked()) {
			bool stop_requested = run_state.stop_requested;

			k_mutex_unlock(&app_mutex);
			return stop_requested ? -ECANCELED : -EIO;
		}
		for (i = 0U; i < streams; i++) {
			if (tx_submitted[i] < per_stream_total) {
				work++;
			}
		}
		if (work == 0U) {
			k_mutex_unlock(&app_mutex);
			break;
		}
		if (streams == 1U) {
			can_send = tx_outstanding[0] < HIL_SOURCE_TX_OUTSTANDING_TARGET;
		} else {
			can_send = tx_outstanding[0] < HIL_SOURCE_TX_OUTSTANDING_TARGET &&
				   tx_outstanding[1] < HIL_SOURCE_TX_OUTSTANDING_TARGET;
		}
		if (!can_send) {
			int64_t now = k_uptime_get();
			int64_t deadline = INT64_MAX;

			/* Per-stream progress timeout: when a blocking stream
			 * (at outstanding depth) has had no own progress for
			 * 2 s, abort.  Activity on another stream must not
			 * mask a stuck stream. */
			for (i = 0U; i < streams; i++) {
				if (tx_submitted[i] < per_stream_total &&
				    tx_outstanding[i] >= HIL_SOURCE_TX_OUTSTANDING_TARGET &&
				    tx_last_activity[i] + HIL_SOURCE_TX_PROGRESS_TIMEOUT_MS <=
					    now) {
					k_mutex_unlock(&app_mutex);
					return -ETIMEDOUT;
				}
				if (tx_submitted[i] < per_stream_total &&
				    tx_outstanding[i] >= HIL_SOURCE_TX_OUTSTANDING_TARGET) {
					int64_t stream_deadline = tx_last_activity[i] +
								  HIL_SOURCE_TX_PROGRESS_TIMEOUT_MS;

					if (stream_deadline < deadline) {
						deadline = stream_deadline;
					}
				}
			}
			k_mutex_unlock(&app_mutex);
			/* Valid sent callbacks, stop requests, and runtime errors wake
			 * this wait.  The deadline is absolute, not restarted after a
			 * wake. */
			(void)hil_app_tx_wait_until(deadline);
			continue;
		}

		/* Stage alignment: all streams must be on the same stage
		 * (Mode A sends each semantic frame as a pair). */
		stage = hil_app_frame_stage(tx_submitted[0], preamble, scored);
		for (i = 1U; i < streams; i++) {
			if (hil_app_frame_stage(tx_submitted[i], preamble, scored) != stage) {
				k_mutex_unlock(&app_mutex);
				return -EIO;
			}
		}
		err = hil_app_tx_ensure_stage(stage, streams, &scored_complete_emitted);
		if (err != 0) {
			k_mutex_unlock(&app_mutex);
			return err;
		}
		k_mutex_unlock(&app_mutex);

		/* Encode and send one semantic frame per stream. */
		for (i = 0U; i < streams; i++) {
			uint8_t sdu[HIL_SOURCE_TX_SDU_MAX];
			enum hil_source_signal_stage sent_stage;
			uint32_t sent_index;
			uint16_t seq;
			int n;
			int ret;

			n = hil_source_signal_encode_next(&tx_encoders[i], sdu, sizeof(sdu));
			if (n < 0) {
				return n;
			}

			k_mutex_lock(&app_mutex, K_FOREVER);
			sent_index = tx_submitted[i];
			sent_stage = hil_app_frame_stage(sent_index, preamble, scored);
			seq = tx_seq[i];
			k_mutex_unlock(&app_mutex);

			ret = g_backend_ops->tx_send(i, seq, sdu, (size_t)n);
			if (ret != 0) {
				k_mutex_lock(&app_mutex, K_FOREVER);
				hil_source_state_counter_send_failure(&run_state, i);
				k_mutex_unlock(&app_mutex);
				return ret;
			}

			k_mutex_lock(&app_mutex, K_FOREVER);
			tx_seq[i] = (uint16_t)(tx_seq[i] + 1U);
			tx_outstanding[i]++;
			tx_submitted[i]++;
			tx_last_activity[i] = k_uptime_get();
			if (sent_stage == HIL_SOURCE_SIGNAL_SCORED) {
				err = hil_source_state_counter_submit_scored(&run_state, i);
			} else {
				err = hil_source_state_counter_submit(&run_state, i);
			}
			k_mutex_unlock(&app_mutex);
			if (err != 0) {
				return err;
			}
		}
	}

	/* All stage caps reached: wait up to 5 s for outstanding to drain. */
	{
		int64_t deadline = k_uptime_get() + HIL_SOURCE_TX_DRAIN_TIMEOUT_MS;

		for (;;) {
			bool drained;
			int64_t now;

			k_mutex_lock(&app_mutex, K_FOREVER);
			if (hil_app_stop_pending_locked()) {
				bool stop_requested = run_state.stop_requested;

				k_mutex_unlock(&app_mutex);
				return stop_requested ? -ECANCELED : -EIO;
			}
			drained = tx_outstanding[0] == 0U &&
				  (streams == 1U || tx_outstanding[1] == 0U);
			k_mutex_unlock(&app_mutex);
			if (drained) {
				break;
			}
			now = k_uptime_get();
			if (now >= deadline) {
				return -ETIMEDOUT;
			}
			/* Valid sent callbacks and active cancellation requests share
			 * this wake.  Recheck state after timeout or reset. */
			(void)hil_app_tx_wait_until(deadline);
		}
	}
	return 0;
}

/* ── universal cleanup (idempotent, TX-state-guarded) ────────────── */

static int hil_app_cleanup(void)
{
	uint8_t streams;
	uint8_t i;
	int first_err = 0;
	int ret;

	k_mutex_lock(&app_mutex, K_FOREVER);
	streams = hil_source_mode_stream_count(run_state.config.mode);
	k_mutex_unlock(&app_mutex);

	/* 1. stop TX generation and clear logical outstanding. */
	k_mutex_lock(&app_mutex, K_FOREVER);
	tx_active = false;
	/* Drop completion events from this generation.  A stale callback that
	 * arrives after this point sees tx_active false and cannot give a token;
	 * callbacks that raced before it are reset here before the next segment. */
	hil_app_tx_wake_reset_locked();
	for (i = 0U; i < HIL_SOURCE_MAX_STREAMS; i++) {
		tx_outstanding[i] = 0U;
	}
	k_mutex_unlock(&app_mutex);
	g_backend_ops->tx_stop();

	/* 2. disable each attached streaming/enabling sink stream.  The kick
	 * resets the operation error; an ASCS rejection then stores -EBADMSG
	 * and signals the wait once.  The error is read after the wait and
	 * preserved as the first cleanup error; cleanup continues. */
	for (i = 0U; i < streams; i++) {
		if (!g_backend_ops->stream_attached(i)) {
			continue;
		}
		ret = g_backend_ops->kick_disable(i);
		if (ret == 0) {
			ret = hil_app_wait_cleanup(g_backend_ops->sem_disabled(),
						   HIL_SOURCE_OP_TIMEOUT_STREAM_MS);
		}
		if (ret != 0 && first_err == 0) {
			first_err = ret;
		}
		if (g_backend_ops->op_error() != 0 && first_err == 0) {
			first_err = g_backend_ops->op_error();
		}
	}

	/* 3. release every attached stream and wait released; release on an
	 * idle ASE may detach synchronously, so re-inspect attachment
	 * before waiting. */
	for (i = 0U; i < streams; i++) {
		if (!g_backend_ops->stream_attached(i)) {
			continue;
		}
		ret = g_backend_ops->kick_release(i);
		if (ret == 0 && g_backend_ops->stream_attached(i)) {
			ret = hil_app_wait_cleanup(g_backend_ops->sem_released(),
						   HIL_SOURCE_OP_TIMEOUT_STREAM_MS);
		}
		if (ret != 0 && first_err == 0) {
			first_err = ret;
		}
		if (g_backend_ops->op_error() != 0 && first_err == 0) {
			first_err = g_backend_ops->op_error();
		}
	}

	/* 4. disconnect the exact connection if present. */
	if (g_backend_ops->conn_present()) {
		ret = g_backend_ops->kick_disconnect();
		if (ret == 0) {
			ret = hil_app_wait_cleanup(g_backend_ops->sem_disconnected(),
						   HIL_SOURCE_OP_TIMEOUT_STREAM_MS);
		}
		if (ret != 0 && first_err == 0) {
			first_err = ret;
		}
	}

	/* 5. delete the group only when every stream is detached; attached
	 * streams that never released are a cleanup failure and block the
	 * group delete (which would also prevent a reconnect segment). */
	{
		bool any_attached = false;

		for (i = 0U; i < HIL_SOURCE_MAX_STREAMS; i++) {
			if (g_backend_ops->stream_attached(i)) {
				any_attached = true;
				break;
			}
		}
		if (!any_attached) {
			ret = g_backend_ops->kick_group_delete();
			if (ret != 0 && first_err == 0) {
				first_err = ret;
			}
		} else if (first_err == 0) {
			first_err = -EBUSY;
		}
	}

	/* 6. unref the app-owned connection exactly once. */
	k_mutex_lock(&app_mutex, K_FOREVER);
	if (conn_owned) {
		conn_owned = false;
		g_backend_ops->conn_unref();
	}
	k_mutex_unlock(&app_mutex);

	/* 7. clear endpoint pointers/semaphores/callback storage. */
	g_backend_ops->reset_segment();

	return first_err;
}

/* ── worker ──────────────────────────────────────────────────────── */

static int hil_app_op_connect(void)
{
	int ret;

	ret = g_backend_ops->kick_connect(&run_addr_le);
	if (ret != 0) {
		return ret;
	}
	ret = hil_app_wait_event(g_backend_ops->sem_connected(), HIL_SOURCE_OP_TIMEOUT_CONNECT_MS);
	if (ret != 0) {
		return ret;
	}
	if (g_backend_ops->op_error() != 0) {
		return g_backend_ops->op_error();
	}
	if (!g_backend_ops->conn_present()) {
		return -EIO;
	}
	k_mutex_lock(&app_mutex, K_FOREVER);
	conn_owned = true;
	k_mutex_unlock(&app_mutex);
	return 0;
}

static int hil_app_op_security(void)
{
	int ret;

	ret = g_backend_ops->kick_security();
	if (ret != 0) {
		return ret;
	}
	ret = hil_app_wait_event(g_backend_ops->sem_security(), HIL_SOURCE_OP_TIMEOUT_SECURITY_MS);
	if (ret != 0) {
		return ret;
	}
	/* L2 security plus an exact configured-peer bond is required before
	 * discovery, on fresh pairing and on bonded reconnect alike.  A
	 * no-op bt_conn_set_security() on an already-secure bonded reconnect
	 * must not be accepted without the bond evidence. */
	if (g_backend_ops->op_error() != 0) {
		return g_backend_ops->op_error();
	}
	if (g_backend_ops->security_error() != 0) {
		return g_backend_ops->security_error();
	}
	if (g_backend_ops->security_level() < BT_SECURITY_L2) {
		return -EACCES;
	}
	if (!g_backend_ops->peer_bonded(&run_addr_le)) {
		return -EACCES;
	}
	return 0;
}

static int hil_app_op_discover(uint8_t streams)
{
	int ret;

	ret = g_backend_ops->kick_discover();
	if (ret != 0) {
		return ret;
	}
	ret = hil_app_wait_event(g_backend_ops->sem_discovered(),
				 HIL_SOURCE_OP_TIMEOUT_DISCOVER_MS);
	if (ret != 0) {
		return ret;
	}
	if (g_backend_ops->op_error() != 0) {
		return g_backend_ops->op_error();
	}
	if (g_backend_ops->discovered_sink_count() < streams) {
		return -ENODEV;
	}
	return 0;
}

static int hil_app_op_stream(uint8_t streams, int (*kick)(uint8_t), struct k_sem *(*sem)(void),
			     uint32_t timeout_ms)
{
	uint8_t i;
	int ret;

	ret = kick(streams);
	if (ret != 0) {
		return ret;
	}
	for (i = 0U; i < streams; i++) {
		ret = hil_app_wait_event(sem(), timeout_ms);
		if (ret != 0) {
			return ret;
		}
		/* A rejection listener signals its operation semaphore once and
		 * stores the first error; fail as soon as it is visible so a
		 * rejected operation does not stall until the wait deadline. */
		if (g_backend_ops->op_error() != 0) {
			return g_backend_ops->op_error();
		}
	}
	if (g_backend_ops->op_error() != 0) {
		return g_backend_ops->op_error();
	}
	return 0;
}

/* NCS bt_bap_stream_connect() submits one CIS connection at a time. A
 * second separate-CIS request while first is CONNECTING or ENCRYPT_PENDING
 * returns -EBUSY. Mode A waits for matching public stream_ops.connected()
 * completion before kicking next stream. Matching failed-CIS
 * stream_ops.disconnected() completion is retryable; coordinator worker makes
 * at most one retry of same stream. */
static int hil_app_retry_state(void);

static int hil_app_op_stream_connect(uint8_t streams)
{
	uint8_t stream_idx;

	for (stream_idx = 0U; stream_idx < streams; stream_idx++) {
		uint32_t retries = 0U;

		for (;;) {
			enum hil_source_stream_connect_outcome outcome;
			int ret;

			ret = g_backend_ops->kick_stream_connect(stream_idx);
			if (ret != 0) {
				return ret;
			}
			ret = hil_app_wait_event(g_backend_ops->sem_stream_connected(),
						 HIL_SOURCE_OP_TIMEOUT_STREAM_MS);
			if (ret != 0) {
				return ret;
			}

			outcome = g_backend_ops->stream_connect_outcome(stream_idx);
			switch (outcome) {
			case HIL_SOURCE_STREAM_CONNECT_OUTCOME_CONNECTED:
				if (g_backend_ops->op_error() != 0) {
					return g_backend_ops->op_error();
				}
				break;
			case HIL_SOURCE_STREAM_CONNECT_OUTCOME_RETRYABLE_FAILURE:
				if (retries >= HIL_APP_STREAM_CONNECT_MAX_RETRIES) {
					return -EIO;
				}
				retries++;
				ret = hil_app_retry_state();
				if (ret != 0) {
					return ret;
				}
				k_sleep(K_MSEC(HIL_APP_STREAM_CONNECT_RETRY_DELAY_MS));
				ret = hil_app_retry_state();
				if (ret != 0) {
					return ret;
				}
				continue;
			case HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR:
			default:
				ret = g_backend_ops->op_error();
				return (ret != 0) ? ret : -EIO;
			}
			break;
		}
	}

	return 0;
}

static int hil_app_retry_state(void)
{
	bool stop_requested;
	bool fatal;

	k_mutex_lock(&app_mutex, K_FOREVER);
	stop_requested = run_state.stop_requested;
	fatal = runtime_error;
	k_mutex_unlock(&app_mutex);

	if (stop_requested) {
		return -ECANCELED;
	}
	return fatal ? -EIO : 0;
}

/* The sem_enabled event comes from stream_ops.enabled and reports the remote
 * ASE enabled state.  It is not the local NCS GATT-write completion that later
 * clears the same-ACL local busy flag; no public callback exposes that clear.
 * Retry only the public kick's synchronous -EBUSY result, without using any
 * private NCS API or manufacturing an enabled completion. */
static int hil_app_kick_enable_with_retry(uint8_t stream_idx)
{
	int64_t deadline = k_uptime_get() + HIL_APP_ENABLE_BUSY_RETRY_TIMEOUT_MS;
	int ret;

	for (;;) {
		int64_t remaining;
		uint32_t delay_ms;

		ret = g_backend_ops->kick_enable(stream_idx);
		if (ret != -EBUSY) {
			return ret;
		}

		ret = hil_app_retry_state();
		if (ret != 0) {
			return ret;
		}
		remaining = deadline - k_uptime_get();
		if (remaining <= 0) {
			return -ETIMEDOUT;
		}
		delay_ms = (remaining > HIL_APP_ENABLE_BUSY_RETRY_INTERVAL_MS)
				   ? HIL_APP_ENABLE_BUSY_RETRY_INTERVAL_MS
				   : (uint32_t)remaining;
		k_sleep(K_MSEC(delay_ms));

		ret = hil_app_retry_state();
		if (ret != 0) {
			return ret;
		}
		if (k_uptime_get() >= deadline) {
			return -ETIMEDOUT;
		}
	}
}

/* Enable is the one multi-stream operation that must be submitted one stream
 * at a time.  A successful kick retains the existing remote completion wait;
 * a busy retry never gets a completion token of its own. */
static int hil_app_op_enable(uint8_t streams)
{
	uint8_t stream_idx;

	for (stream_idx = 0U; stream_idx < streams; stream_idx++) {
		int ret;

		ret = hil_app_kick_enable_with_retry(stream_idx);
		if (ret != 0) {
			return ret;
		}
		ret = hil_app_wait_event(g_backend_ops->sem_enabled(),
					 HIL_SOURCE_OP_TIMEOUT_STREAM_MS);
		if (ret != 0) {
			return ret;
		}
		if (g_backend_ops->op_error() != 0) {
			return g_backend_ops->op_error();
		}
	}

	return 0;
}

static void hil_app_run(void)
{
	uint8_t streams;
	bool reconnect_done = false;
	int err = 0;

	k_mutex_lock(&app_mutex, K_FOREVER);
	conn_owned = false;
	runtime_error = false;
	streams = hil_source_mode_stream_count(run_state.config.mode);
	k_mutex_unlock(&app_mutex);

	/* Segment 0: idle (already entered by start) then configured.  Every
	 * state mutation and record emit runs under the app mutex; a record
	 * failure becomes err = -EIO (first errno preserved) before abort. */
	k_mutex_lock(&app_mutex, K_FOREVER);
	err = hil_app_emit_state_record();
	k_mutex_unlock(&app_mutex);
	if (err != 0) {
		err = -EIO;
		goto abort;
	}
	k_mutex_lock(&app_mutex, K_FOREVER);
	err = hil_app_advance_state(HIL_SOURCE_RUN_STATE_CONFIGURED);
	k_mutex_unlock(&app_mutex);
	if (err != 0) {
		goto abort;
	}
	k_mutex_lock(&app_mutex, K_FOREVER);
	err = hil_app_emit_state_record();
	k_mutex_unlock(&app_mutex);
	if (err != 0) {
		err = -EIO;
		goto abort;
	}

	for (;;) {
		uint32_t seg;

		k_mutex_lock(&app_mutex, K_FOREVER);
		seg = run_state.segment;
		if (run_state.current_state != HIL_SOURCE_RUN_STATE_CONNECTING) {
			err = hil_app_advance_state(HIL_SOURCE_RUN_STATE_CONNECTING);
		}
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			goto abort;
		}
		k_mutex_lock(&app_mutex, K_FOREVER);
		err = hil_app_emit_state_record();
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			err = -EIO;
			goto abort;
		}

		k_mutex_lock(&app_mutex, K_FOREVER);
		g_backend_ops->set_run_shape(run_state.config.mode, run_state.config.profile);
		k_mutex_unlock(&app_mutex);

		err = hil_app_op_connect();
		if (err != 0) {
			goto abort;
		}
		err = hil_app_op_security();
		if (err != 0) {
			goto abort;
		}
		k_mutex_lock(&app_mutex, K_FOREVER);
		err = hil_app_advance_state(HIL_SOURCE_RUN_STATE_SECURED);
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			goto abort;
		}
		k_mutex_lock(&app_mutex, K_FOREVER);
		err = hil_app_emit_state_record();
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			err = -EIO;
			goto abort;
		}

		err = hil_app_op_discover(streams);
		if (err != 0) {
			goto abort;
		}
		k_mutex_lock(&app_mutex, K_FOREVER);
		err = hil_app_advance_state(HIL_SOURCE_RUN_STATE_DISCOVERED);
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			goto abort;
		}
		k_mutex_lock(&app_mutex, K_FOREVER);
		err = hil_app_emit_state_record();
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			err = -EIO;
			goto abort;
		}

		err = hil_app_op_stream(streams, g_backend_ops->kick_configure,
					g_backend_ops->sem_configured,
					HIL_SOURCE_OP_TIMEOUT_STREAM_MS);
		if (err != 0) {
			goto abort;
		}
		err = hil_app_op_stream(streams, g_backend_ops->kick_qos, g_backend_ops->sem_qos,
					HIL_SOURCE_OP_TIMEOUT_STREAM_MS);
		if (err != 0) {
			goto abort;
		}
		k_mutex_lock(&app_mutex, K_FOREVER);
		err = hil_app_advance_state(HIL_SOURCE_RUN_STATE_QOS);
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			goto abort;
		}
		k_mutex_lock(&app_mutex, K_FOREVER);
		err = hil_app_emit_state_record();
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			err = -EIO;
			goto abort;
		}

		err = hil_app_op_enable(streams);
		if (err != 0) {
			goto abort;
		}
		err = hil_app_op_stream_connect(streams);
		if (err != 0) {
			goto abort;
		}
		err = hil_app_op_stream(streams, g_backend_ops->kick_start,
					g_backend_ops->sem_started,
					HIL_SOURCE_OP_TIMEOUT_STREAM_MS);
		if (err != 0) {
			goto abort;
		}
		/* Explicit TX activation: attach this segment's streams once,
		 * after every started completion and before streaming. */
		err = g_backend_ops->tx_start(streams);
		if (err != 0) {
			goto abort;
		}
		k_mutex_lock(&app_mutex, K_FOREVER);
		err = hil_app_advance_state(HIL_SOURCE_RUN_STATE_STREAMING);
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			goto abort;
		}
		k_mutex_lock(&app_mutex, K_FOREVER);
		err = hil_app_emit_state_record();
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			err = -EIO;
			goto abort;
		}

		/* TX stage: preamble, scored, tail with stage caps. */
		err = hil_app_tx_run();
		if (err != 0) {
			goto abort;
		}

		k_mutex_lock(&app_mutex, K_FOREVER);
		err = hil_app_advance_state(HIL_SOURCE_RUN_STATE_TEARDOWN);
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			goto abort;
		}
		k_mutex_lock(&app_mutex, K_FOREVER);
		err = hil_app_emit_state_record();
		k_mutex_unlock(&app_mutex);
		if (err != 0) {
			err = -EIO;
			goto abort;
		}

		err = hil_app_cleanup();
		if (err != 0) {
			goto abort;
		}

		k_mutex_lock(&app_mutex, K_FOREVER);
		{
			bool reconnect_now =
				run_state.config.reconnect_policy == HIL_SOURCE_RECONNECT_ONCE &&
				!run_state.aborted && !reconnect_done && seg == 0U;

			if (reconnect_now) {
				err = hil_source_state_next_segment(&run_state);
				reconnect_done = true;
			}
			k_mutex_unlock(&app_mutex);
			if (err != 0) {
				goto abort;
			}
			if (reconnect_now) {
				continue;
			}
			break;
		}
	}

	/* Atomic terminal publication.  Under the app mutex, validate PASS
	 * against a copy of the state; the real state stays active teardown
	 * until the PASS terminal record has been accepted by the output
	 * queue.  Only then is the PASS snapshot committed.  Shell status
	 * cannot observe the intermediate state because the mutex stays
	 * held. */
	k_mutex_lock(&app_mutex, K_FOREVER);
	{
		struct hil_source_state terminal = run_state;

		err = hil_source_state_terminal(&terminal, HIL_SOURCE_VERDICT_PASS);
		if (err == 0) {
			err = hil_app_emit_terminal(HIL_SOURCE_VERDICT_PASS);
			if (err == 0) {
				run_state = terminal;
			}
		}
		if (err != 0) {
			/* Real state is still active teardown: preserve the
			 * first error, terminalize FAIL, and emit the FAIL
			 * terminal best effort.  Status must report fail,
			 * never pass. */
			(void)hil_source_state_error_report(&run_state, err);
			(void)hil_source_state_terminal(&run_state, HIL_SOURCE_VERDICT_FAIL);
			(void)hil_app_emit_terminal(HIL_SOURCE_VERDICT_FAIL);
		}
	}
	k_mutex_unlock(&app_mutex);
	goto done;

abort: {
	enum hil_source_abort_cause abort_cause;
	int cerr;
	bool stop_requested;

	k_mutex_lock(&app_mutex, K_FOREVER);
	stop_requested = run_state.stop_requested;
	k_mutex_unlock(&app_mutex);
	if (err == -ETIMEDOUT) {
		abort_cause = HIL_SOURCE_ABORT_TIMEOUT;
	} else if (err == -ECANCELED && stop_requested) {
		abort_cause = HIL_SOURCE_ABORT_STOP;
	} else {
		abort_cause = HIL_SOURCE_ABORT_ERROR;
	}

	k_mutex_lock(&app_mutex, K_FOREVER);
	if (err != 0 && err != -ECANCELED) {
		hil_source_state_error_report(&run_state, err);
	}
	{
		bool transitioned =
			hil_source_state_abort_to_teardown(&run_state, abort_cause) == 0;

		k_mutex_unlock(&app_mutex);
		/* Emit the exact abort teardown record once, and only when the
		 * abort edge was actually accepted (an already-teardown state
		 * keeps its ordinary teardown record). */
		if (transitioned) {
			k_mutex_lock(&app_mutex, K_FOREVER);
			(void)hil_app_emit_abort(abort_cause);
			k_mutex_unlock(&app_mutex);
		}
	}

	/* Universal cleanup best effort; terminal fail after it. */
	cerr = hil_app_cleanup();
	if (cerr != 0) {
		k_mutex_lock(&app_mutex, K_FOREVER);
		hil_source_state_error_report(&run_state, cerr);
		k_mutex_unlock(&app_mutex);
	}
	k_mutex_lock(&app_mutex, K_FOREVER);
	(void)hil_source_state_terminal(&run_state, HIL_SOURCE_VERDICT_FAIL);
	k_mutex_unlock(&app_mutex);
	k_mutex_lock(&app_mutex, K_FOREVER);
	(void)hil_app_emit_terminal(HIL_SOURCE_VERDICT_FAIL);
	k_mutex_unlock(&app_mutex);
}
	goto done;

done:
	k_sem_give(&sem_run_done);
	return;
}

static void hil_app_worker_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		k_sem_take(&sem_worker, K_FOREVER);
		hil_app_run();
	}
}

/* ── sent callback ───────────────────────────────────────────────── */
void hil_source_app_tx_sent(uint8_t stream_idx)
{
	bool stale;
	bool zero_error = false;

	if (stream_idx >= HIL_SOURCE_MAX_STREAMS) {
		return;
	}
	k_mutex_lock(&app_mutex, K_FOREVER);
	/* Stale callbacks (after the run left streaming: teardown/cleanup,
	 * terminal, or a TX stop between segments) are counted, not applied
	 * to outstanding. */
	stale = !run_state.active || run_state.current_state == HIL_SOURCE_RUN_STATE_TEARDOWN ||
		!tx_active;
	if (run_state.active) {
		(void)hil_source_state_counter_sent_callback(&run_state, stream_idx);
	}
	if (stale) {
		k_mutex_unlock(&app_mutex);
		return;
	}
	if (tx_outstanding[stream_idx] == 0U) {
		/* Callback for zero outstanding while TX is current is an
		 * internal error. */
		zero_error = true;
	} else {
		tx_outstanding[stream_idx]--;
		tx_last_activity[stream_idx] = k_uptime_get();
	}
	if (zero_error) {
		hil_app_set_runtime_error_locked();
	} else {
		/* Give while holding app_mutex.  Cleanup resets the event under
		 * the same lock, so a callback racing generation teardown cannot
		 * leave a stale wake token for the next segment. */
		hil_app_tx_wake_locked();
	}
	k_mutex_unlock(&app_mutex);
}

/* ── status / hello formatting (app mutex held by caller) ────────── */

static const char *hil_app_nullable_name(const char *name)
{
	return (name != NULL) ? name : "null";
}

static int hil_app_build_status_data(char *buf, size_t cap, enum hil_source_command cmd, bool ok,
				     const char *error_name)
{
	char streams_json[512];
	uint8_t streams = hil_source_mode_stream_count(run_state.config.mode);
	uint8_t i;
	int p = 0;
	int n;
	const char *mode_name = hil_source_mode_name(run_state.config.mode);
	const char *profile_name = hil_source_profile_name(run_state.config.profile);
	const char *policy_name =
		hil_source_reconnect_policy_name(run_state.config.reconnect_policy);
	const char *state_name = hil_source_state_name(run_state.current_state);
	const char *verdict_name = hil_source_verdict_name(run_state.terminal);
	const char *cause_name = hil_source_abort_cause_name(run_state.abort_cause);
	const char *cmd_name = hil_source_command_name(cmd);

	if (run_state.terminal == HIL_SOURCE_VERDICT_NONE) {
		verdict_name = "none";
	}

	streams_json[0] = '\0';
	for (i = 0U; i < streams; i++) {
		n = snprintf(streams_json + p, sizeof(streams_json) - (size_t)p,
			     "%s{\"seq\":%u,\"sub\":%u,\"sc\":%u,"
			     "\"sf\":%u,\"cb\":%u,\"out\":%u}",
			     (i == 0U) ? "" : ",", (unsigned int)tx_seq[i],
			     run_state.counters[i].submitted_sdus,
			     run_state.counters[i].submitted_scored_sdus,
			     run_state.counters[i].send_failures,
			     run_state.counters[i].sent_callbacks, (unsigned int)tx_outstanding[i]);
		if (n < 0 || (size_t)n >= sizeof(streams_json) - (size_t)p) {
			return -ENOBUFS;
		}
		p += n;
	}

	n = snprintf(buf, cap,
		     "{\"command\":\"%s\",\"ok\":%s,\"error\":\"%s\","
		     "\"active\":%s,\"state\":\"%s\",\"segment\":%u,"
		     "\"aborted\":%s,\"cause\":\"%s\",\"stop_requested\":%s,"
		     "\"verdict\":\"%s\",\"first_errno\":%d,"
		     "\"mode\":\"%s\",\"profile\":\"%s\",\"reconnect\":\"%s\",\"scored_target\":%u,"
		     "\"stream_count\":%u,\"streams\":[%s],"
		     "\"connected\":%s,\"security_level\":%u,\"security_error\":%d,"
		     "\"sink_ase_count\":%u,\"group\":%s,"
		     "\"disconnect_reason\":%u,\"first_ascs_code\":%u,\"first_ascs_reason\":%u,"
		     "\"bond_count\":%d}",
		     hil_app_nullable_name(cmd_name), ok ? "true" : "false",
		     hil_app_nullable_name(error_name), run_state.active ? "true" : "false",
		     hil_app_nullable_name(state_name), run_state.segment,
		     run_state.aborted ? "true" : "false", hil_app_nullable_name(cause_name),
		     run_state.stop_requested ? "true" : "false",
		     hil_app_nullable_name(verdict_name), run_state.first_error,
		     hil_app_nullable_name(mode_name), hil_app_nullable_name(profile_name),
		     hil_app_nullable_name(policy_name), run_state.config.scored_sdu_count, streams,
		     streams_json, g_backend_ops->conn_present() ? "true" : "false",
		     g_backend_ops->security_level(), g_backend_ops->security_error(),
		     g_backend_ops->discovered_sink_count(),
		     g_backend_ops->group_present() ? "true" : "false",
		     g_backend_ops->disconnect_reason(), g_backend_ops->first_ascs_code(),
		     g_backend_ops->first_ascs_reason(), g_backend_ops->bond_count());
	if (n < 0 || (size_t)n >= cap) {
		return -ENOBUFS;
	}
	return n;
}

static int hil_app_emit_status_data(const char *command_id, const char *run_id, uint32_t segment,
				    enum hil_source_command cmd, bool ok, const char *error_name)
{
	char data[HIL_SOURCE_OUTPUT_LINE_SIZE];
	char line[HIL_SOURCE_OUTPUT_LINE_SIZE];
	int n;
	int m;
	int ret = 0;

	m = hil_app_build_status_data(data, sizeof(data), cmd, ok, error_name);
	if (m < 0) {
		ret = m;
		goto out;
	}
	n = hil_source_record_format_line(line, sizeof(line), k_uptime_get_32(), command_id, run_id,
					  segment, "status", data, (size_t)m);
	if (n < 0) {
		ret = n;
		goto out;
	}
	if (hil_app_emit_record_line(line, (size_t)n) != 0) {
		ret = -EIO;
		goto out;
	}
out:
	if (ret != 0 && run_state.active) {
		/* A failed status emission marks the run failed when a run
		 * is active; the worker observes runtime_error and aborts. */
		hil_app_set_runtime_error_locked();
	}
	return ret;
}

int hil_source_app_format_status_line(char *buf, size_t cap, const char *command_id,
				      const char *run_id)
{
	char data[HIL_SOURCE_OUTPUT_LINE_SIZE];
	int n;
	int m;

	if (buf == NULL) {
		return -EINVAL;
	}
	if (cap > 0U) {
		buf[0] = '\0';
	}
	k_mutex_lock(&app_mutex, K_FOREVER);
	m = hil_app_build_status_data(data, sizeof(data), HIL_SOURCE_CMD_STATUS, true,
				      HIL_SOURCE_ERROR_OK);
	if (m < 0) {
		k_mutex_unlock(&app_mutex);
		return m;
	}
	n = hil_source_record_format_line(buf, cap, k_uptime_get_32(), command_id, run_id,
					  run_state.segment, "status", data, (size_t)m);
	k_mutex_unlock(&app_mutex);
	if (n < 0) {
		if (cap > 0U) {
			buf[0] = '\0';
		}
		return n;
	}
	return n;
}

static int hil_app_emit_hello_data(const char *command_id, const char *run_id)
{
	bt_addr_le_t id;
	char id_str[32];
	const char *id_type = "none";
	char data[HIL_SOURCE_OUTPUT_LINE_SIZE];
	char line[HIL_SOURCE_OUTPUT_LINE_SIZE];
	int n;

	if (g_backend_ops->get_identity(&id) == 0) {
		hil_app_addr_to_display(&id, id_str, sizeof(id_str));
		id_type = hil_app_addr_type_name(&id);
	} else {
		strncpy(id_str, "none", sizeof(id_str) - 1U);
		id_str[sizeof(id_str) - 1U] = '\0';
	}

	n = snprintf(data, sizeof(data),
		     "{\"command\":\"hello\",\"ok\":true,\"error\":\"ok\","
		     "\"firmware_id\":\"%s\",\"protocol_version\":%u,"
		     "\"identity\":\"%s\",\"identity_type\":\"%s\","
		     "\"active\":%s,\"state\":\"%s\",\"segment\":%u,"
		     "\"bond_count\":%d,"
		     "\"sample_rate\":%u,"
		     "\"frame_samples_48_3_1\":%u,\"octets_48_3_1\":%u,"
		     "\"sdu_mono_48_3_1\":%u,\"sdu_modeb_48_3_1\":%u,"
		     "\"frame_samples_48_4_1\":%u,\"octets_48_4_1\":%u,"
		     "\"sdu_mono_48_4_1\":%u,\"sdu_modeb_48_4_1\":%u,"
		     "\"preamble_samples\":%u,"
		     "\"tail_frames_48_3_1\":%u,\"tail_frames_48_4_1\":%u,"
		     "\"left_carrier_hz\":%u,\"right_carrier_hz\":%u,"
		     "\"default_seed\":%u}",
		     HIL_SOURCE_FIRMWARE_ID, HIL_SOURCE_PROTOCOL_VERSION, id_str, id_type,
		     run_state.active ? "true" : "false",
		     hil_app_nullable_name(hil_source_state_name(run_state.current_state)),
		     run_state.segment, g_backend_ops->bond_count(), HIL_SOURCE_SAMPLE_RATE_HZ,
		     hil_source_profile_frame_samples(HIL_SOURCE_PROFILE_48_3_1),
		     hil_source_profile_octets_per_channel(HIL_SOURCE_PROFILE_48_3_1),
		     hil_source_profile_sdu_size(HIL_SOURCE_PROFILE_48_3_1, HIL_SOURCE_MODE_MONO),
		     hil_source_profile_sdu_size(HIL_SOURCE_PROFILE_48_3_1, HIL_SOURCE_MODE_B),
		     hil_source_profile_frame_samples(HIL_SOURCE_PROFILE_48_4_1),
		     hil_source_profile_octets_per_channel(HIL_SOURCE_PROFILE_48_4_1),
		     hil_source_profile_sdu_size(HIL_SOURCE_PROFILE_48_4_1, HIL_SOURCE_MODE_MONO),
		     hil_source_profile_sdu_size(HIL_SOURCE_PROFILE_48_4_1, HIL_SOURCE_MODE_B),
		     HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES,
		     (HIL_SOURCE_TAIL_MIN_US + 7500U - 1U) / 7500U,
		     (HIL_SOURCE_TAIL_MIN_US + 10000U - 1U) / 10000U, HIL_SOURCE_LEFT_CARRIER_HZ,
		     HIL_SOURCE_RIGHT_CARRIER_HZ, (unsigned int)HIL_SOURCE_DEFAULT_SIGNAL_SEED);
	if (n < 0 || (size_t)n >= sizeof(data)) {
		if (run_state.active) {
			hil_app_set_runtime_error_locked();
		}
		return -ENOBUFS;
	}
	n = hil_source_record_format_line(line, sizeof(line), k_uptime_get_32(), command_id, run_id,
					  0U, "status", data, (size_t)n);
	if (n < 0) {
		goto out;
	}
	if (hil_app_emit_record_line(line, (size_t)n) != 0) {
		n = -EIO;
		goto out;
	}
	return 0;
out:
	if (run_state.active) {
		hil_app_set_runtime_error_locked();
	}
	return (n < 0) ? n : -EIO;
}

/* ── dispatch ────────────────────────────────────────────────────── */

static int hil_app_emit_parse_error(enum hil_source_parse_result pr)
{
	const char *name = hil_source_parse_result_name(pr);
	char data[128];
	char line[HIL_SOURCE_OUTPUT_LINE_SIZE];
	int n;

	n = snprintf(data, sizeof(data),
		     "{\"command\":\"status\",\"ok\":false,\"error\":\"parse_error\","
		     "\"parse_result\":\"%s\"}",
		     (name != NULL) ? name : "unknown");
	if (n < 0 || (size_t)n >= sizeof(data)) {
		return -ENOBUFS;
	}
	n = hil_source_record_format_line(line, sizeof(line), k_uptime_get_32(), "parse-error",
					  "unbound", 0U, "status", data, (size_t)n);
	if (n < 0) {
		if (run_state.active) {
			hil_app_set_runtime_error_locked();
		}
		return n;
	}
	if (hil_app_emit_record_line(line, (size_t)n) != 0) {
		return -EIO;
	}
	return 0;
}

static int hil_app_handle_hello(const struct hil_source_command_in *cmd)
{
	return hil_app_emit_hello_data(cmd->command_id, cmd->run_id);
}

static int hil_app_handle_configure(const struct hil_source_command_in *cmd)
{
	struct hil_source_config cfg;
	int ret;
	int emit;

	if (run_state.active || run_state.terminal != HIL_SOURCE_VERDICT_NONE) {
		emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						HIL_SOURCE_CMD_CONFIGURE, false,
						HIL_SOURCE_ERROR_BUSY);
		return (emit != 0) ? emit : -EBUSY;
	}
	memset(&cfg, 0, sizeof(cfg));
	cfg.mode = cmd->mode;
	cfg.profile = cmd->profile;
	memcpy(cfg.peer_address, cmd->peer_address, sizeof(cfg.peer_address));
	cfg.peer_address_type = cmd->peer_address_type;
	cfg.scored_sdu_count = cmd->scored_sdu_count;
	cfg.signal_seed = cmd->signal_seed;
	cfg.reconnect_policy = cmd->reconnect_policy;

	ret = hil_source_state_configure(&run_state, &cfg);
	if (ret != 0) {
		emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						HIL_SOURCE_CMD_CONFIGURE, false,
						HIL_SOURCE_ERROR_INVALID_REQUEST);
		return (emit != 0) ? emit : ret;
	}
	strncpy(config_command_id, cmd->command_id, sizeof(config_command_id) - 1U);
	config_command_id[sizeof(config_command_id) - 1U] = '\0';
	strncpy(config_run_id, cmd->run_id, sizeof(config_run_id) - 1U);
	config_run_id[sizeof(config_run_id) - 1U] = '\0';
	have_config_ids = true;
	hil_app_config_to_le(&cfg, &run_addr_le);

	return hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U, HIL_SOURCE_CMD_CONFIGURE,
					true, HIL_SOURCE_ERROR_OK);
}

static int hil_app_handle_start(const struct hil_source_command_in *cmd)
{
	int ret;
	int emit;

	if (!have_config_ids) {
		emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						HIL_SOURCE_CMD_START, false,
						HIL_SOURCE_ERROR_NOT_CONFIGURED);
		return (emit != 0) ? emit : -EINVAL;
	}
	if (strcmp(config_run_id, cmd->run_id) != 0) {
		emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						HIL_SOURCE_CMD_START, false,
						HIL_SOURCE_ERROR_INVALID_REQUEST);
		return (emit != 0) ? emit : -EINVAL;
	}
	if (run_state.active) {
		emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						HIL_SOURCE_CMD_START, false, HIL_SOURCE_ERROR_BUSY);
		return (emit != 0) ? emit : -EBUSY;
	}
	ret = hil_source_state_start(&run_state);
	if (ret != 0) {
		emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						HIL_SOURCE_CMD_START, false,
						HIL_SOURCE_ERROR_INVALID_REQUEST);
		return (emit != 0) ? emit : ret;
	}
	strncpy(async_command_id, cmd->command_id, sizeof(async_command_id) - 1U);
	async_command_id[sizeof(async_command_id) - 1U] = '\0';

	{
		char line[HIL_SOURCE_OUTPUT_LINE_SIZE];
		int n;

		n = hil_source_record_format_ack_start(line, sizeof(line), k_uptime_get_32(),
						       async_command_id, config_run_id, 0U);
		if (n > 0 && hil_app_emit_record_line(line, (size_t)n) == 0) {
			/* sem_run_done is reset here, before the worker wakes,
			 * so an idle that requests stop later can never lose
			 * the worker's completion token. */
			k_sem_reset(&sem_run_done);
			k_sem_give(&sem_worker);
			return 0;
		}
	}

	/* The start ACK could not be emitted: do not launch the worker
	 * silently.  Roll the accepted start into a diagnosed fail path so
	 * no active unreachable run is left behind; the terminal snapshot
	 * (verdict fail, first errno -EIO) is visible to a later status
	 * query once output can resume.  The caller (dispatch) holds the app
	 * mutex, so no nested lock is taken here. */
	(void)hil_source_state_error_report(&run_state, -EIO);
	(void)hil_source_state_abort_to_teardown(&run_state, HIL_SOURCE_ABORT_ERROR);
	(void)hil_source_state_terminal(&run_state, HIL_SOURCE_VERDICT_FAIL);
	(void)hil_app_emit_abort(HIL_SOURCE_ABORT_ERROR);
	(void)hil_app_emit_terminal(HIL_SOURCE_VERDICT_FAIL);
	return -EIO;
}

static int hil_app_handle_status(const struct hil_source_command_in *cmd)
{
	int emit;

	if (!have_config_ids) {
		emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						HIL_SOURCE_CMD_STATUS, false,
						HIL_SOURCE_ERROR_INVALID_REQUEST);
		return (emit != 0) ? emit : -EINVAL;
	}
	if (strcmp(config_run_id, cmd->run_id) != 0) {
		emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						HIL_SOURCE_CMD_STATUS, false,
						HIL_SOURCE_ERROR_INVALID_REQUEST);
		return (emit != 0) ? emit : -EINVAL;
	}
	return hil_app_emit_status_data(cmd->command_id, cmd->run_id, run_state.segment,
					HIL_SOURCE_CMD_STATUS, true, HIL_SOURCE_ERROR_OK);
}

static int hil_app_handle_stop(const struct hil_source_command_in *cmd)
{
	int emit;

	if (!have_config_ids || strcmp(config_run_id, cmd->run_id) != 0) {
		emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						HIL_SOURCE_CMD_STOP, false,
						HIL_SOURCE_ERROR_INVALID_REQUEST);
		return (emit != 0) ? emit : -EINVAL;
	}
	if (run_state.active) {
		hil_source_state_stop_request(&run_state);
		hil_app_tx_wake_locked();
	}
	return hil_app_emit_status_data(cmd->command_id, cmd->run_id, run_state.segment,
					HIL_SOURCE_CMD_STOP, true, HIL_SOURCE_ERROR_OK);
}

static int hil_app_handle_unpair(const struct hil_source_command_in *cmd)
{
	bt_addr_le_t peer;
	int i;
	int ret;
	int emit;

	if (run_state.active) {
		emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						HIL_SOURCE_CMD_UNPAIR, false,
						HIL_SOURCE_ERROR_BUSY);
		return (emit != 0) ? emit : -EBUSY;
	}
	peer.type = (cmd->peer_address_type == HIL_SOURCE_ADDR_RANDOM) ? BT_ADDR_LE_RANDOM
								       : BT_ADDR_LE_PUBLIC;
	for (i = 0; i < 6; i++) {
		peer.a.val[i] = cmd->peer_address[5 - i];
	}
	ret = g_backend_ops->unpair(&peer);
	if (ret != 0 && ret != -ENOENT) {
		emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						HIL_SOURCE_CMD_UNPAIR, false,
						HIL_SOURCE_ERROR_FAILED);
		return (emit != 0) ? emit : ret;
	}
	return hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U, HIL_SOURCE_CMD_UNPAIR,
					true, HIL_SOURCE_ERROR_OK);
}

static int hil_app_handle_idle(const struct hil_source_command_in *cmd)
{
	bool was_active = false;
	int ret;

	k_mutex_lock(&app_mutex, K_FOREVER);
	if (have_config_ids && strcmp(config_run_id, cmd->run_id) != 0) {
		ret = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
					       HIL_SOURCE_CMD_IDLE, false,
					       HIL_SOURCE_ERROR_INVALID_REQUEST);
		k_mutex_unlock(&app_mutex);
		if (ret != 0) {
			return ret;
		}
		return -EINVAL;
	}
	if (run_state.active) {
		was_active = true;
		hil_source_state_stop_request(&run_state);
		hil_app_tx_wake_locked();
	}
	k_mutex_unlock(&app_mutex);

	if (was_active) {
		/* Wait bounded for the worker to reach terminal fail.
		 * sem_run_done was reset at the accepted start, before the
		 * worker woke, so the worker's completion token can never be
		 * lost by this wait. */
		if (k_sem_take(&sem_run_done, K_MSEC(HIL_SOURCE_IDLE_TERMINAL_WAIT_MS)) != 0) {
			/* Timeout: the run is still active.  Do not run a
			 * concurrent cleanup and do not reset the run state;
			 * the worker owns cleanup for the active run.  The
			 * failed response emission is returned, never silently
			 * dropped. */
			k_mutex_lock(&app_mutex, K_FOREVER);
			ret = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
						       HIL_SOURCE_CMD_IDLE, false,
						       HIL_SOURCE_ERROR_FAILED);
			k_mutex_unlock(&app_mutex);
			return (ret != 0) ? ret : -ETIMEDOUT;
		}
	}

	/* After the run completed (or when nothing was active), the
	 * idempotent cleanup re-establishes the disconnected / no-group /
	 * no-TX invariant before resetting the run state. */
	ret = hil_app_cleanup();
	if (ret != 0 && ret != -ECANCELED) {
		k_mutex_lock(&app_mutex, K_FOREVER);
		{
			int emit = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U,
							    HIL_SOURCE_CMD_IDLE, false,
							    HIL_SOURCE_ERROR_FAILED);

			k_mutex_unlock(&app_mutex);
			return (emit != 0) ? emit : ret;
		}
	}

	k_mutex_lock(&app_mutex, K_FOREVER);
	hil_source_state_reset(&run_state);
	have_config_ids = false;
	async_command_id[0] = '\0';
	ret = hil_app_emit_status_data(cmd->command_id, cmd->run_id, 0U, HIL_SOURCE_CMD_IDLE, true,
				       HIL_SOURCE_ERROR_OK);
	k_mutex_unlock(&app_mutex);
	return ret;
}

int hil_source_app_dispatch(const char *json, size_t len)
{
	struct hil_source_command_in cmd;
	enum hil_source_parse_result pr;
	int ret = 0;

	pr = hil_source_parse(json, len, &cmd);
	if (pr != HIL_SOURCE_PARSE_OK) {
		/* Parse-error emission reads/writes run_state.active and
		 * runtime_error, so it must run under the app mutex even
		 * though parsing itself precedes the normal dispatch lock. */
		k_mutex_lock(&app_mutex, K_FOREVER);
		ret = hil_app_emit_parse_error(pr);
		k_mutex_unlock(&app_mutex);
		if (ret != 0) {
			return ret;
		}
		return -EINVAL;
	}

	k_mutex_lock(&app_mutex, K_FOREVER);
	switch (cmd.command) {
	case HIL_SOURCE_CMD_HELLO:
		ret = hil_app_handle_hello(&cmd);
		break;
	case HIL_SOURCE_CMD_CONFIGURE:
		ret = hil_app_handle_configure(&cmd);
		break;
	case HIL_SOURCE_CMD_START:
		ret = hil_app_handle_start(&cmd);
		break;
	case HIL_SOURCE_CMD_STATUS:
		ret = hil_app_handle_status(&cmd);
		break;
	case HIL_SOURCE_CMD_STOP:
		ret = hil_app_handle_stop(&cmd);
		break;
	case HIL_SOURCE_CMD_UNPAIR:
		ret = hil_app_handle_unpair(&cmd);
		break;
	case HIL_SOURCE_CMD_IDLE:
		/* The bounded terminal wait and cleanup must not hold the
		 * app mutex (the worker needs it to progress). */
		k_mutex_unlock(&app_mutex);
		return hil_app_handle_idle(&cmd);
	default:
		ret = -EINVAL;
		break;
	}
	k_mutex_unlock(&app_mutex);
	return ret;
}

/* ── init / fatal / shell ────────────────────────────────────────── */

void hil_source_app_fatal_status(const char *phase, int err)
{
	char line[HIL_SOURCE_OUTPUT_LINE_SIZE];
	int n;

	if (!hil_source_output_active() || phase == NULL) {
		return;
	}
	n = snprintf(line, sizeof(line),
		     "HIL1 {\"protocol_version\":%u,\"kind\":\"status\",\"firmware_id\":\"%s\","
		     "\"monotonic_ms\":%u,\"command_id\":\"boot\",\"run_id\":\"boot\","
		     "\"segment\":0,\"data\":{\"command\":\"status\",\"ok\":false,"
		     "\"error\":\"failed\",\"phase\":\"%s\",\"errno\":%d}}",
		     HIL_SOURCE_PROTOCOL_VERSION, HIL_SOURCE_FIRMWARE_ID, k_uptime_get_32(), phase,
		     err);
	if (n < 0 || (size_t)n >= sizeof(line)) {
		return;
	}
	(void)hil_source_output_submit(line, (size_t)n, HIL_SOURCE_OUTPUT_SUBMIT_TIMEOUT_MS);
}

int hil_source_app_init(void)
{
#ifndef CONFIG_HIL_SOURCE_APP_TEST
	if (g_backend_ops == NULL) {
		g_backend_ops = hil_source_bap_ops();
	}
#endif
	if (g_backend_ops == NULL) {
		return -EINVAL;
	}
	/* Init order: output first, then the coordinator. */
	if (!hil_source_output_active()) {
		return -EIO;
	}

	k_mutex_lock(&app_mutex, K_FOREVER);
	hil_source_state_reset(&run_state);
	have_config_ids = false;
	runtime_error = false;
	conn_owned = false;
	tx_active = false;
	hil_app_tx_wake_reset_locked();
	async_command_id[0] = '\0';
	k_mutex_unlock(&app_mutex);

	if (!worker_started) {
		k_thread_create(&worker_thread, worker_stack, K_KERNEL_STACK_SIZEOF(worker_stack),
				hil_app_worker_fn, NULL, NULL, NULL, K_PRIO_PREEMPT(4), 0,
				K_NO_WAIT);
		k_thread_name_set(&worker_thread, "hil_worker");
		worker_started = true;
	}
	return 0;
}

#ifdef CONFIG_HIL_SOURCE_APP_TEST
void hil_source_app_test_set_backend(const struct hil_source_backend_ops *ops)
{
	g_backend_ops = ops;
}
#endif /* CONFIG_HIL_SOURCE_APP_TEST */

#ifndef CONFIG_HIL_SOURCE_APP_TEST
#include <zephyr/shell/shell.h>

static int cmd_hil(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	if (argc != 2) {
		return -EINVAL;
	}
	return hil_source_app_dispatch(argv[1], strlen(argv[1]));
}

SHELL_CMD_ARG_REGISTER(hil, NULL, NULL, cmd_hil, 1, SHELL_OPT_ARG_RAW);
#endif /* !CONFIG_HIL_SOURCE_APP_TEST */
