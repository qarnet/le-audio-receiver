/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dedicated LE Audio source fixture coordinator (RH1B).
 *
 * The coordinator owns dispatch, the run worker, the TX stage
 * orchestration (pacing, lockstep, stage caps, outstanding, progress
 * timeout), the universal cleanup, and all HIL1 status/hello formatting.
 * All Bluetooth work happens through the backend ops table: in the
 * production build the table points at the real BAP/TX modules; under
 * CONFIG_HIL_SOURCE_APP_TEST the table is injected by the native test
 * suite so the coordinator runs against a scripted fake backend.
 */

#ifndef HIL_SOURCE_APP_H
#define HIL_SOURCE_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/kernel.h>

#include "hil_source_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── frozen timing / depth constants ──────────────────────────────── */

#define HIL_SOURCE_OP_TIMEOUT_CONNECT_MS    20000U
#define HIL_SOURCE_OP_TIMEOUT_SECURITY_MS   20000U
#define HIL_SOURCE_OP_TIMEOUT_DISCOVER_MS   10000U
#define HIL_SOURCE_OP_TIMEOUT_STREAM_MS     10000U
#define HIL_SOURCE_OUTPUT_SUBMIT_TIMEOUT_MS 1000U
#define HIL_SOURCE_IDLE_TERMINAL_WAIT_MS    15000U
#ifndef CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET
#define HIL_SOURCE_TX_OUTSTANDING_TARGET 3U
#else
#define HIL_SOURCE_TX_OUTSTANDING_TARGET ((uint32_t)CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET)
#endif
#define HIL_SOURCE_TX_PROGRESS_TIMEOUT_MS 2000U
#define HIL_SOURCE_TX_DRAIN_TIMEOUT_MS    5000U
#define HIL_SOURCE_MAX_STREAMS            2U

/* Maximum encoded TX SDU byte size.  The ISO TX path is bounded by
 * CONFIG_BT_ISO_TX_MTU (255); the coordinator never uses the 1024-byte
 * output line scratch as SDU scratch. */
#define HIL_SOURCE_TX_SDU_MAX 255U

enum hil_source_stream_connect_outcome {
	HIL_SOURCE_STREAM_CONNECT_OUTCOME_CONNECTED,
	HIL_SOURCE_STREAM_CONNECT_OUTCOME_RETRYABLE_FAILURE,
	HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR,
};

/* ── backend ops ──────────────────────────────────────────────────── */

struct hil_source_backend_ops {
	/* identity / bonds */
	int (*get_identity)(bt_addr_le_t *out);
	int (*bond_count)(void);
	int (*unpair)(const bt_addr_le_t *peer);
	/* True when the exact configured peer (address + type) has a stored
	 * bond.  Used to require an L2 bond before discovery on both fresh
	 * pairing and bonded reconnect. */
	bool (*peer_bonded)(const bt_addr_le_t *peer);

	/* run shape applied once per segment before connect */
	void (*set_run_shape)(enum hil_source_mode mode, enum hil_source_profile profile);

	/* run lifecycle: kick + completion sem */
	int (*kick_connect)(const bt_addr_le_t *addr);
	int (*kick_security)(void);
	int (*kick_discover)(void);
	int (*kick_configure)(uint8_t stream_count);
	int (*kick_qos)(uint8_t stream_count);
	/* Submit exactly one enable request for stream_idx.  The coordinator
	 * serializes multi-stream enable by waiting for sem_enabled and checking
	 * op_error before submitting the next stream. */
	int (*kick_enable)(uint8_t stream_idx);
	/* Submit one CIS connection request for stream_idx.  NCS permits only
	 * one pending bt_bap_stream_connect(); the coordinator waits for the
	 * matching public stream_ops.connected() completion before submitting the
	 * next request, and matching failed-CIS stream_ops.disconnected()
	 * completion is treated as a retryable failure, with at most one
	 * coordinator-worker retry for that same stream. */
	int (*kick_stream_connect)(uint8_t stream_idx);
	int (*kick_start)(uint8_t stream_count);

	/* Explicit TX activation: called once per segment after every
	 * sem_started completion and before the run advances to streaming.
	 * Production attaches the current segment's stream objects to the
	 * TX driver; the fake records the call. */
	int (*tx_start)(uint8_t stream_count);

	struct k_sem *(*sem_connected)(void);
	struct k_sem *(*sem_security)(void);
	struct k_sem *(*sem_discovered)(void);
	struct k_sem *(*sem_configured)(void);
	struct k_sem *(*sem_qos)(void);
	struct k_sem *(*sem_enabled)(void);
	struct k_sem *(*sem_stream_connected)(void);
	enum hil_source_stream_connect_outcome (*stream_connect_outcome)(uint8_t stream_idx);
	struct k_sem *(*sem_started)(void);

	/* most recent async lifecycle operation error (0 = none); the
	 * coordinator checks it after every bounded wait */
	int (*op_error)(void);

	/* TX send driver */
	int (*tx_send)(uint8_t stream_idx, uint16_t seq, const uint8_t *sdu, size_t len);
	void (*tx_stop)(void);

	/* teardown */
	int (*kick_disable)(uint8_t stream_idx);
	struct k_sem *(*sem_disabled)(void);
	int (*kick_release)(uint8_t stream_idx);
	struct k_sem *(*sem_released)(void);
	int (*kick_disconnect)(void);
	struct k_sem *(*sem_disconnected)(void);
	bool (*conn_present)(void);
	bool (*stream_attached)(uint8_t stream_idx);
	int (*kick_group_delete)(void);
	void (*conn_unref)(void);
	void (*reset_segment)(void);

	/* status getters */
	uint8_t (*security_level)(void);
	int (*security_error)(void);
	uint8_t (*discovered_sink_count)(void);
	bool (*group_present)(void);
	uint8_t (*disconnect_reason)(void);
	uint8_t (*first_ascs_code)(void);
	uint8_t (*first_ascs_reason)(void);
};

/* ── public API ───────────────────────────────────────────────────── */

/* Initialize the coordinator: requires an active output (init order:
 * output first, then app), an injected/static backend, resets the run
 * state and configured IDs, and starts the worker thread.  Returns 0 or
 * a negative errno; a fatal init failure is not retried. */
int hil_source_app_init(void);

/* One best-effort HIL1 status record for a fatal boot failure
 * (kind status, command_id/run_id "boot", ok:false, error failed, with
 * the failing phase and errno in trusted data).  Never blocks the boot
 * path; a submit failure is swallowed. */
void hil_source_app_fatal_status(const char *phase, int err);

/* Dispatch one `hil <raw JSON>` command body (exactly the JSON object
 * span after the command name).  Returns 0 when the command was accepted
 * for handling or a negative errno for a rejected request. */
int hil_source_app_dispatch(const char *json, size_t len);

/* Sent-callback entry point, called by the real BAP stream ops in
 * production and by the fake backend in native tests.  Decrements the
 * logical outstanding count and records the sent-callback counter;
 * stale callbacks after a generation change are counted, not applied. */
void hil_source_app_tx_sent(uint8_t stream_idx);

/* Format the full current status HIL1 line (envelope included) into buf.
 * Returns the written length or a negative errno; on failure the buffer
 * is an empty C string when cap is nonzero.  Exposed so tests can prove
 * formatter truncation fails closed. */
int hil_source_app_format_status_line(char *buf, size_t cap, const char *command_id,
				      const char *run_id);

#ifdef CONFIG_HIL_SOURCE_APP_TEST
/* Test seam: inject the backend ops table.  Must be called before
 * hil_source_app_init(). */
void hil_source_app_test_set_backend(const struct hil_source_backend_ops *ops);
#endif /* CONFIG_HIL_SOURCE_APP_TEST */

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_APP_H */
