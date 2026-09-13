/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Scripted fake backend for the hil_source_app native suite.
 *
 * Implements the same backend ops table the production BAP/TX modules
 * provide, but records every call in a ledger and completes operations
 * only when the test signals the corresponding event semaphore.  The
 * test can script kick failures, identity/bond values, connection and
 * stream attachment state, and the TX send/sent behavior.
 */

#ifndef FAKE_HIL_SOURCE_BACKEND_H
#define FAKE_HIL_SOURCE_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "hil_source_app.h"

/* Ledger operation identifiers (one per ops entry point). */
enum fake_op {
	FAKE_OP_GET_IDENTITY,
	FAKE_OP_BOND_COUNT,
	FAKE_OP_UNPAIR,
	FAKE_OP_PEER_BONDED,
	FAKE_OP_SET_RUN_SHAPE,
	FAKE_OP_CONNECT,
	FAKE_OP_SECURITY,
	FAKE_OP_DISCOVER,
	FAKE_OP_CONFIGURE,
	FAKE_OP_QOS,
	FAKE_OP_ENABLE,
	FAKE_OP_STREAM_CONNECT,
	FAKE_OP_START,
	FAKE_OP_TX_START,
	FAKE_OP_TX_SEND,
	FAKE_OP_TX_SEND_TS,
	FAKE_OP_TX_READ_TX_TS,
	FAKE_OP_TX_READ_SYNC,
	FAKE_OP_TX_STOP,
	FAKE_OP_DISABLE,
	FAKE_OP_RELEASE,
	FAKE_OP_DISCONNECT,
	FAKE_OP_GROUP_DELETE,
	FAKE_OP_CONN_UNREF,
	FAKE_OP_RESET_SEGMENT,
	FAKE_OP_COUNT
};

/* One ledger entry (opcode + parameters). */
struct fake_record {
	enum fake_op op;
	uint8_t stream_idx;
	uint16_t seq;
	uint16_t len;
	uint8_t count; /* stream count for bulk kicks; enable uses stream_idx */
	uint8_t addr[6];
	uint8_t addr_type;
	uint32_t ts; /* Pinned ISO-event timestamp (TX_SEND_TS only). */
};

/* The fake ops table. */
const struct hil_source_backend_ops *fake_backend_ops(void);

/* Clear ledger, counters, scripted values, and all event semaphores. */
void fake_backend_reset(void);

/* ── scripting ───────────────────────────────────────────────────── */

/* Identity reported by get_identity; display string in canonical
 * "XX:XX:XX:XX:XX:XX" form, type 0 = public, 1 = random. */
void fake_set_identity(const char *display, uint8_t type);
/* Result of get_identity: 0 (identity set) or -errno. */
void fake_set_identity_result(int err);
void fake_set_bond_count(int n);
/* Exact configured-peer bond answer (default true). */
void fake_set_peer_bonded(bool bonded);
/* When true, kick_security completes immediately without needing a
 * FAKE_OP_SECURITY signal, simulating the already-secure bonded reconnect
 * shape where no security_changed callback fires. */
void fake_set_security_auto_complete(bool enabled);
/* Scripted kick/poll result for a whole op class (0 = success). */
void fake_set_kick_result(enum fake_op op, int err);
/* Make enable submission return -EBUSY for `attempts` calls on one stream,
 * then use the scripted FAKE_OP_ENABLE result. */
void fake_set_enable_busy_attempts(uint8_t stream_idx, uint32_t attempts);
void fake_set_op_error(int err);
void fake_set_conn_present(bool present);
void fake_set_attached(uint8_t stream_idx, bool attached);
void fake_set_group_present(bool present);
void fake_set_discovered_sinks(uint8_t count);
void fake_set_security_level(uint8_t level);
void fake_set_security_error(int err);
void fake_set_disconnect_reason(uint8_t reason);
void fake_set_ascs(uint8_t code, uint8_t reason);
/* When true (default), kick_release detaches the stream synchronously
 * (simulating release-on-IDLE); when false the coordinator must wait for
 * a manual FAKE_OP_RELEASE signal. */
void fake_set_auto_detach(bool enabled);

/* Suppress delivered sent callbacks for one stream (Mode A asymmetric
 * stall regression): while suppressed, the pump and fake_signal_sent do
 * not deliver that stream's sent callbacks to the coordinator. */
void fake_set_suppress_sent(uint8_t stream_idx, bool suppress);
bool fake_sent_suppressed(uint8_t stream_idx);

/* ── test-side signals ───────────────────────────────────────────── */

/* Give the op event semaphore `count` times (completes that many waits). */
void fake_signal(enum fake_op op, uint8_t count);
/* Deliver one failed-CIS callback for a currently pending matching stream. */
void fake_signal_stream_connect_failure(uint8_t stream_idx);
/* Deliver one sent callback for a stream to the coordinator. */
void fake_signal_sent(uint8_t stream_idx);
/* Notify a test semaphore when one stream reaches the configured outstanding
 * target in TX sends.
 * Notification occurs from fake tx_send before it returns; a lower-priority
 * test completion thread can then deliver the callback after the coordinator
 * records the outstanding send. */
void fake_set_depth_target_signal(uint8_t stream_idx, struct k_sem *sem);
/* Notify a test semaphore when exact send count is reached for one stream. */
void fake_set_send_signal(uint8_t stream_idx, uint32_t send_count_target, struct k_sem *sem);
/* Block one timestamped send before its accepted-send count changes. */
void fake_set_ts_send_block(uint8_t stream_idx, uint32_t send_count_target, struct k_sem *entered,
			    struct k_sem *release);
/* Block one conn_present status getter. If it enters during the blocked
 * timestamped send, advance virtual controller time on the next read. The
 * observed send counts expose whether STATUS entered mid-batch. */
void fake_set_status_block(struct k_sem *entered, struct k_sem *release,
			   int32_t mid_batch_controller_advance_us);
uint32_t fake_status_observed_send_count(uint8_t stream_idx);

/* ── observability ───────────────────────────────────────────────── */

uint32_t fake_ledger_count(void);
const struct fake_record *fake_ledger(void);
/* Number of times an op was invoked. */
uint32_t fake_kick_count(enum fake_op op);
/* Number of TX sends per stream and delivered sent callbacks per stream. */
uint32_t fake_send_count(uint8_t stream_idx);
uint32_t fake_sent_count(uint8_t stream_idx);
/* Enable submission/completion accounting.  Busy submissions are not
 * accepted and must not receive sem_enabled completion tokens. */
uint32_t fake_enable_busy_rejection_count(void);
uint32_t fake_enable_accepted_count(void);
uint32_t fake_enable_completion_count(void);
/* Stream-connect accounting.  Only accepted requests may complete; a
 * second request while one is pending is rejected as -EBUSY. */
uint32_t fake_stream_connect_busy_rejection_count(void);
uint32_t fake_stream_connect_accepted_count(void);
uint32_t fake_stream_connect_completion_count(void);
uint32_t fake_stream_connect_failure_count(void);
/* Timestamp-mode evidence: the last pinned timestamp recorded by
 * fake_tx_send_ts per stream, the count of timestamped sends, and the
 * scripted readback result selector (default 0 = success). */
uint32_t fake_ts_last(uint8_t stream_idx);
uint32_t fake_ts_send_count(uint8_t stream_idx);
void fake_ts_set_readback_result(int result);
void fake_ts_set_readback_base(uint32_t timestamp);
/* Add a signed offset to controller-now only when the bootstrap readback
 * seeds the first gate. Later successful batches resume ideal pacing. */
void fake_ts_set_initial_time_offset(int32_t offset_us);
/* Advance controller-now once after Mode A stream 0 submits, before stream 1
 * gets its final lead check. */
void fake_ts_set_peer_submit_offset(int32_t offset_us);
/* Hold controller-now constant after bootstrap seeds the mirrored clock. */
void fake_ts_set_time_frozen(bool frozen);
void fake_ts_set_time_result(int result);
uint32_t fake_ts_time_get_count(void);
/* The configured run shape from the last FAKE_OP_SET_RUN_SHAPE call. */
enum hil_source_mode fake_run_mode(void);
enum hil_source_profile fake_run_profile(void);
/* The exact address passed to the last FAKE_OP_CONNECT. */
const uint8_t *fake_connect_addr(void);
uint8_t fake_connect_addr_type(void);

#endif /* FAKE_HIL_SOURCE_BACKEND_H */
