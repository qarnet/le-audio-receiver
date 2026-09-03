/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Scripted fake backend implementation (RH1B native suite).
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "fake_hil_source_backend.h"
#include "hil_source_types.h"

#define FAKE_LEDGER_MAX 4096

static struct fake_record ledger[FAKE_LEDGER_MAX];
static uint32_t ledger_count;
static uint32_t send_count[2];
static uint32_t sent_count[2];
static uint32_t enable_busy_attempts[2];
static uint32_t enable_busy_rejection_count;
static uint32_t enable_accepted_count;
static uint32_t enable_completion_count;
static uint32_t stream_connect_busy_rejection_count;
static uint32_t stream_connect_accepted_count;
static uint32_t stream_connect_completion_count;
static uint32_t stream_connect_failure_count;
static uint8_t stream_connect_pending_idx = UINT8_MAX;
static uint8_t stream_connect_completion_idx = UINT8_MAX;
static enum hil_source_stream_connect_outcome stream_connect_completion_outcome =
	HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;
static struct k_spinlock stream_connect_lock;

static int kick_results[FAKE_OP_COUNT];
static int scripted_op_error;
static bool scripted_conn_present;
static bool scripted_attached[2];
static bool scripted_group_present;
static uint8_t scripted_sinks;
static uint8_t scripted_security_level;
static int scripted_security_error;
static uint8_t scripted_disconnect_reason;
static uint8_t scripted_ascs_code;
static uint8_t scripted_ascs_reason;
static int scripted_bond_count;
static bool scripted_peer_bonded;
static bool security_auto_complete;
static bool auto_detach;
static bool suppress_sent[2];
static struct k_sem *depth_target_signal;
static uint8_t depth_target_stream;
static struct k_sem *send_signal;
static uint8_t send_signal_stream;
static uint32_t send_signal_target;

static char scripted_identity[20];
static uint8_t scripted_identity_type;
static int scripted_identity_result;

static enum hil_source_mode run_mode;
static enum hil_source_profile run_profile;
static uint8_t connect_addr[6];
static uint8_t connect_addr_type;

static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_security, 0, 1);
static K_SEM_DEFINE(sem_discovered, 0, 1);
static K_SEM_DEFINE(sem_configured, 0, 8);
static K_SEM_DEFINE(sem_qos, 0, 8);
static K_SEM_DEFINE(sem_enabled, 0, 8);
static K_SEM_DEFINE(sem_stream_connected, 0, 8);
static K_SEM_DEFINE(sem_started, 0, 8);
static K_SEM_DEFINE(sem_disabled, 0, 8);
static K_SEM_DEFINE(sem_released, 0, 8);
static K_SEM_DEFINE(sem_disconnected, 0, 1);

static void record(enum fake_op op, uint8_t stream_idx, uint16_t seq, uint16_t len, uint8_t count,
		   const uint8_t *addr, uint8_t addr_type)
{
	struct fake_record *r;

	if (ledger_count >= FAKE_LEDGER_MAX) {
		return;
	}
	r = &ledger[ledger_count++];
	r->op = op;
	r->stream_idx = stream_idx;
	r->seq = seq;
	r->len = len;
	r->count = count;
	r->addr_type = addr_type;
	if (addr != NULL) {
		memcpy(r->addr, addr, 6);
	}
}

/* ── ops implementations ─────────────────────────────────────────── */

static int fake_get_identity(bt_addr_le_t *out)
{
	int i;

	record(FAKE_OP_GET_IDENTITY, 0, 0, 0, 0, NULL, 0);
	if (scripted_identity_result != 0 || out == NULL) {
		return scripted_identity_result;
	}
	out->type = scripted_identity_type;
	/* Display string "XX:XX:XX:XX:XX:XX" -> on-air little endian. */
	for (i = 0; i < 6; i++) {
		unsigned int byte;

		if (sscanf(scripted_identity + i * 3, "%2x", &byte) != 1) {
			return -EINVAL;
		}
		out->a.val[5 - i] = (uint8_t)byte;
	}
	return 0;
}

static int fake_bond_count(void)
{
	record(FAKE_OP_BOND_COUNT, 0, 0, 0, 0, NULL, 0);
	return scripted_bond_count;
}

static int fake_unpair(const bt_addr_le_t *peer)
{
	record(FAKE_OP_UNPAIR, 0, 0, 0, 0, peer->a.val, peer->type);
	return kick_results[FAKE_OP_UNPAIR];
}

static bool fake_peer_bonded(const bt_addr_le_t *peer)
{
	record(FAKE_OP_PEER_BONDED, 0, 0, 0, 0, peer->a.val, peer->type);
	return scripted_peer_bonded;
}

static void fake_set_run_shape(enum hil_source_mode mode, enum hil_source_profile profile)
{
	run_mode = mode;
	run_profile = profile;
	record(FAKE_OP_SET_RUN_SHAPE, 0, 0, 0, (uint8_t)mode, NULL, (uint8_t)profile);
}

#define FAKE_KICK_IMPL(name, op)                                                                   \
	static int fake_kick_##name(uint8_t count)                                                 \
	{                                                                                          \
		record(op, 0, 0, 0, count, NULL, 0);                                               \
		return kick_results[op];                                                           \
	}

static int fake_kick_connect(const bt_addr_le_t *addr)
{
	record(FAKE_OP_CONNECT, 0, 0, 0, 0, addr->a.val, addr->type);
	memcpy(connect_addr, addr->a.val, 6);
	connect_addr_type = addr->type;
	if (kick_results[FAKE_OP_CONNECT] == 0) {
		/* A successful direct connect establishes the connection;
		 * the coordinator checks conn_present after the wait. */
		scripted_conn_present = true;
	}
	return kick_results[FAKE_OP_CONNECT];
}

static int fake_kick_security(void)
{
	record(FAKE_OP_SECURITY, 0, 0, 0, 0, NULL, 0);
	if (security_auto_complete) {
		/* Simulate the already-secure bonded reconnect shape: the
		 * operation completes without any security_changed callback. */
		scripted_security_level = 2U;
		scripted_security_error = 0;
		k_sem_give(&sem_security);
	}
	return kick_results[FAKE_OP_SECURITY];
}

static int fake_kick_discover(void)
{
	record(FAKE_OP_DISCOVER, 0, 0, 0, 0, NULL, 0);
	return kick_results[FAKE_OP_DISCOVER];
}

static int fake_kick_configure(uint8_t count)
{
	uint8_t i;

	record(FAKE_OP_CONFIGURE, 0, 0, 0, count, NULL, 0);
	if (kick_results[FAKE_OP_CONFIGURE] == 0) {
		/* Streams attach on a successful configure, mirroring the
		 * production BAP client. */
		for (i = 0U; i < count && i < 2U; i++) {
			scripted_attached[i] = true;
		}
	}
	return kick_results[FAKE_OP_CONFIGURE];
}

static int fake_kick_qos(uint8_t count)
{
	record(FAKE_OP_QOS, 0, 0, 0, count, NULL, 0);
	if (kick_results[FAKE_OP_QOS] == 0) {
		scripted_group_present = true;
	}
	return kick_results[FAKE_OP_QOS];
}

static int fake_kick_enable(uint8_t stream_idx)
{
	record(FAKE_OP_ENABLE, stream_idx, 0, 0, 0, NULL, 0);
	if (stream_idx < 2U && enable_busy_attempts[stream_idx] > 0U) {
		enable_busy_attempts[stream_idx]--;
		enable_busy_rejection_count++;
		return -EBUSY;
	}
	if (kick_results[FAKE_OP_ENABLE] == 0) {
		enable_accepted_count++;
	}
	return kick_results[FAKE_OP_ENABLE];
}

FAKE_KICK_IMPL(start, FAKE_OP_START)

/* Model NCS single-CIS connect ownership.  A second connect while the first
 * is pending is a real -EBUSY rejection and must not receive a completion.
 * -EALREADY remains the deliberate already-connected success case used by
 * the production backend, with its one completion token supplied directly. */
static int fake_kick_stream_connect(uint8_t stream_idx)
{
	k_spinlock_key_t key;
	int ret;

	record(FAKE_OP_STREAM_CONNECT, stream_idx, 0, 0, 0, NULL, 0);
	if (stream_idx >= 2U || stream_idx >= hil_source_mode_stream_count(run_mode)) {
		return -EINVAL;
	}
	key = k_spin_lock(&stream_connect_lock);
	if (stream_connect_pending_idx != UINT8_MAX) {
		stream_connect_busy_rejection_count++;
		k_spin_unlock(&stream_connect_lock, key);
		return -EBUSY;
	}
	stream_connect_completion_idx = UINT8_MAX;
	stream_connect_completion_outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;
	ret = kick_results[FAKE_OP_STREAM_CONNECT];
	if (ret == -EALREADY) {
		stream_connect_accepted_count++;
		stream_connect_completion_count++;
		stream_connect_completion_idx = stream_idx;
		stream_connect_completion_outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_CONNECTED;
		k_spin_unlock(&stream_connect_lock, key);
		k_sem_give(&sem_stream_connected);
		return 0;
	}
	if (ret != 0) {
		k_spin_unlock(&stream_connect_lock, key);
		return ret;
	}
	stream_connect_accepted_count++;
	stream_connect_pending_idx = stream_idx;
	k_spin_unlock(&stream_connect_lock, key);
	return 0;
}

static int fake_kick_tx_start(uint8_t count)
{
	record(FAKE_OP_TX_START, 0, 0, 0, count, NULL, 0);
	return kick_results[FAKE_OP_TX_START];
}

static int fake_kick_disable(uint8_t stream_idx)
{
	record(FAKE_OP_DISABLE, stream_idx, 0, 0, 0, NULL, 0);
	return kick_results[FAKE_OP_DISABLE];
}

static int fake_kick_release(uint8_t stream_idx)
{
	record(FAKE_OP_RELEASE, stream_idx, 0, 0, 0, NULL, 0);
	if (auto_detach && stream_idx < 2U) {
		scripted_attached[stream_idx] = false;
		k_sem_give(&sem_released);
	}
	return kick_results[FAKE_OP_RELEASE];
}

static int fake_kick_disconnect(void)
{
	record(FAKE_OP_DISCONNECT, 0, 0, 0, 0, NULL, 0);
	if (kick_results[FAKE_OP_DISCONNECT] == 0) {
		/* The disconnected callback fires asynchronously in
		 * production; the fake completes it immediately. */
		scripted_conn_present = false;
		k_sem_give(&sem_disconnected);
	}
	return kick_results[FAKE_OP_DISCONNECT];
}

static bool fake_conn_present(void)
{
	return scripted_conn_present;
}

static bool fake_stream_attached(uint8_t stream_idx)
{
	if (stream_idx >= 2U) {
		return false;
	}
	return scripted_attached[stream_idx];
}

static int fake_kick_group_delete(void)
{
	record(FAKE_OP_GROUP_DELETE, 0, 0, 0, 0, NULL, 0);
	if (kick_results[FAKE_OP_GROUP_DELETE] == 0) {
		/* Mirror production: the group is only reported absent once a
		 * delete actually succeeds; a failed delete retains it so a
		 * later retry can succeed. */
		scripted_group_present = false;
	}
	return kick_results[FAKE_OP_GROUP_DELETE];
}

static void fake_conn_unref(void)
{
	record(FAKE_OP_CONN_UNREF, 0, 0, 0, 0, NULL, 0);
	scripted_conn_present = false;
}

static void fake_reset_segment(void)
{
	k_spinlock_key_t key;

	record(FAKE_OP_RESET_SEGMENT, 0, 0, 0, 0, NULL, 0);
	key = k_spin_lock(&stream_connect_lock);
	stream_connect_pending_idx = UINT8_MAX;
	stream_connect_completion_idx = UINT8_MAX;
	stream_connect_completion_outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;
	k_spin_unlock(&stream_connect_lock, key);
	k_sem_reset(&sem_stream_connected);
}

static int fake_tx_send(uint8_t stream_idx, uint16_t seq, const uint8_t *sdu, size_t len)
{
	record(FAKE_OP_TX_SEND, stream_idx, seq, (uint16_t)len, 0, sdu, 0);
	if (stream_idx < 2U) {
		send_count[stream_idx]++;
		if (depth_target_signal != NULL && stream_idx == depth_target_stream &&
		    send_count[stream_idx] == HIL_SOURCE_TX_OUTSTANDING_TARGET) {
			struct k_sem *sem = depth_target_signal;

			depth_target_signal = NULL;
			k_sem_give(sem);
		}
		if (send_signal != NULL && stream_idx == send_signal_stream &&
		    send_count[stream_idx] == send_signal_target) {
			struct k_sem *sem = send_signal;

			send_signal = NULL;
			k_sem_give(sem);
		}
	}
	return kick_results[FAKE_OP_TX_SEND];
}

static void fake_tx_stop(void)
{
	record(FAKE_OP_TX_STOP, 0, 0, 0, 0, NULL, 0);
}

static struct k_sem *fake_sem_connected(void)
{
	return &sem_connected;
}

static struct k_sem *fake_sem_security(void)
{
	return &sem_security;
}

static struct k_sem *fake_sem_discovered(void)
{
	return &sem_discovered;
}

static struct k_sem *fake_sem_configured(void)
{
	return &sem_configured;
}

static struct k_sem *fake_sem_qos(void)
{
	return &sem_qos;
}

static struct k_sem *fake_sem_enabled(void)
{
	return &sem_enabled;
}

static struct k_sem *fake_sem_stream_connected(void)
{
	return &sem_stream_connected;
}

static enum hil_source_stream_connect_outcome fake_stream_connect_outcome(uint8_t stream_idx)
{
	enum hil_source_stream_connect_outcome outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;
	k_spinlock_key_t key = k_spin_lock(&stream_connect_lock);

	if (stream_connect_completion_idx == stream_idx) {
		outcome = stream_connect_completion_outcome;
	}
	k_spin_unlock(&stream_connect_lock, key);
	return outcome;
}

static struct k_sem *fake_sem_started(void)
{
	return &sem_started;
}

static struct k_sem *fake_sem_disabled(void)
{
	return &sem_disabled;
}

static struct k_sem *fake_sem_released(void)
{
	return &sem_released;
}

static struct k_sem *fake_sem_disconnected(void)
{
	return &sem_disconnected;
}

static int fake_op_error(void)
{
	return scripted_op_error;
}

static uint8_t fake_security_level(void)
{
	return scripted_security_level;
}

static int fake_security_error(void)
{
	return scripted_security_error;
}

static uint8_t fake_discovered_sink_count(void)
{
	return scripted_sinks;
}

static bool fake_group_present(void)
{
	return scripted_group_present;
}

static uint8_t fake_disconnect_reason(void)
{
	return scripted_disconnect_reason;
}

static uint8_t fake_first_ascs_code(void)
{
	return scripted_ascs_code;
}

static uint8_t fake_first_ascs_reason(void)
{
	return scripted_ascs_reason;
}

static const struct hil_source_backend_ops fake_ops = {
	.get_identity = fake_get_identity,
	.bond_count = fake_bond_count,
	.unpair = fake_unpair,
	.peer_bonded = fake_peer_bonded,
	.set_run_shape = fake_set_run_shape,
	.kick_connect = fake_kick_connect,
	.kick_security = fake_kick_security,
	.kick_discover = fake_kick_discover,
	.kick_configure = fake_kick_configure,
	.kick_qos = fake_kick_qos,
	.kick_enable = fake_kick_enable,
	.kick_stream_connect = fake_kick_stream_connect,
	.kick_start = fake_kick_start,
	.tx_start = fake_kick_tx_start,
	.sem_connected = fake_sem_connected,
	.sem_security = fake_sem_security,
	.sem_discovered = fake_sem_discovered,
	.sem_configured = fake_sem_configured,
	.sem_qos = fake_sem_qos,
	.sem_enabled = fake_sem_enabled,
	.sem_stream_connected = fake_sem_stream_connected,
	.stream_connect_outcome = fake_stream_connect_outcome,
	.sem_started = fake_sem_started,
	.op_error = fake_op_error,
	.tx_send = fake_tx_send,
	.tx_stop = fake_tx_stop,
	.kick_disable = fake_kick_disable,
	.sem_disabled = fake_sem_disabled,
	.kick_release = fake_kick_release,
	.sem_released = fake_sem_released,
	.kick_disconnect = fake_kick_disconnect,
	.sem_disconnected = fake_sem_disconnected,
	.conn_present = fake_conn_present,
	.stream_attached = fake_stream_attached,
	.kick_group_delete = fake_kick_group_delete,
	.conn_unref = fake_conn_unref,
	.reset_segment = fake_reset_segment,
	.security_level = fake_security_level,
	.security_error = fake_security_error,
	.discovered_sink_count = fake_discovered_sink_count,
	.group_present = fake_group_present,
	.disconnect_reason = fake_disconnect_reason,
	.first_ascs_code = fake_first_ascs_code,
	.first_ascs_reason = fake_first_ascs_reason,
};

const struct hil_source_backend_ops *fake_backend_ops(void)
{
	return &fake_ops;
}

/* ── scripting setters ───────────────────────────────────────────── */

void fake_backend_reset(void)
{
	memset(ledger, 0, sizeof(ledger));
	ledger_count = 0U;
	send_count[0] = send_count[1] = 0U;
	sent_count[0] = sent_count[1] = 0U;
	enable_busy_attempts[0] = enable_busy_attempts[1] = 0U;
	enable_busy_rejection_count = 0U;
	enable_accepted_count = 0U;
	enable_completion_count = 0U;
	stream_connect_busy_rejection_count = 0U;
	stream_connect_accepted_count = 0U;
	stream_connect_completion_count = 0U;
	stream_connect_failure_count = 0U;
	stream_connect_pending_idx = UINT8_MAX;
	stream_connect_completion_idx = UINT8_MAX;
	stream_connect_completion_outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;
	memset(kick_results, 0, sizeof(kick_results));
	scripted_op_error = 0;
	scripted_conn_present = false;
	scripted_attached[0] = scripted_attached[1] = false;
	scripted_group_present = false;
	scripted_sinks = 2U;
	scripted_security_level = 2U;
	scripted_security_error = 0;
	scripted_disconnect_reason = 0U;
	scripted_ascs_code = 0U;
	scripted_ascs_reason = 0U;
	scripted_bond_count = 0;
	scripted_peer_bonded = true;
	security_auto_complete = false;
	auto_detach = true;
	suppress_sent[0] = suppress_sent[1] = false;
	depth_target_signal = NULL;
	depth_target_stream = 0U;
	send_signal = NULL;
	send_signal_stream = 0U;
	send_signal_target = 0U;
	strcpy(scripted_identity, "AA:BB:CC:DD:EE:FF");
	scripted_identity_type = 1U;
	scripted_identity_result = 0;
	run_mode = HIL_SOURCE_MODE_MONO;
	run_profile = HIL_SOURCE_PROFILE_48_4_1;
	memset(connect_addr, 0, sizeof(connect_addr));
	connect_addr_type = 0U;
	k_sem_reset(&sem_connected);
	k_sem_reset(&sem_security);
	k_sem_reset(&sem_discovered);
	k_sem_reset(&sem_configured);
	k_sem_reset(&sem_qos);
	k_sem_reset(&sem_enabled);
	k_sem_reset(&sem_stream_connected);
	k_sem_reset(&sem_started);
	k_sem_reset(&sem_disabled);
	k_sem_reset(&sem_released);
	k_sem_reset(&sem_disconnected);
}

void fake_set_identity(const char *display, uint8_t type)
{
	strncpy(scripted_identity, display, sizeof(scripted_identity) - 1U);
	scripted_identity[sizeof(scripted_identity) - 1U] = '\0';
	scripted_identity_type = type;
}

void fake_set_identity_result(int err)
{
	scripted_identity_result = err;
}

void fake_set_bond_count(int n)
{
	scripted_bond_count = n;
}

void fake_set_peer_bonded(bool bonded)
{
	scripted_peer_bonded = bonded;
}

void fake_set_security_auto_complete(bool enabled)
{
	security_auto_complete = enabled;
}

void fake_set_kick_result(enum fake_op op, int err)
{
	if (op >= 0 && op < FAKE_OP_COUNT) {
		kick_results[op] = err;
	}
}

void fake_set_enable_busy_attempts(uint8_t stream_idx, uint32_t attempts)
{
	if (stream_idx < 2U) {
		enable_busy_attempts[stream_idx] = attempts;
	}
}

void fake_set_op_error(int err)
{
	scripted_op_error = err;
}

void fake_set_conn_present(bool present)
{
	scripted_conn_present = present;
}

void fake_set_attached(uint8_t stream_idx, bool attached)
{
	if (stream_idx < 2U) {
		scripted_attached[stream_idx] = attached;
	}
}

void fake_set_group_present(bool present)
{
	scripted_group_present = present;
}

void fake_set_discovered_sinks(uint8_t count)
{
	scripted_sinks = count;
}

void fake_set_security_level(uint8_t level)
{
	scripted_security_level = level;
}

void fake_set_security_error(int err)
{
	scripted_security_error = err;
}

void fake_set_disconnect_reason(uint8_t reason)
{
	scripted_disconnect_reason = reason;
}

void fake_set_ascs(uint8_t code, uint8_t reason)
{
	scripted_ascs_code = code;
	scripted_ascs_reason = reason;
}

void fake_set_auto_detach(bool enabled)
{
	auto_detach = enabled;
}

void fake_set_suppress_sent(uint8_t stream_idx, bool suppress)
{
	if (stream_idx < 2U) {
		suppress_sent[stream_idx] = suppress;
	}
}

bool fake_sent_suppressed(uint8_t stream_idx)
{
	if (stream_idx >= 2U) {
		return false;
	}
	return suppress_sent[stream_idx];
}

/* ── signals ─────────────────────────────────────────────────────── */

static struct k_sem *fake_sem_for(enum fake_op op)
{
	switch (op) {
	case FAKE_OP_CONNECT:
		return &sem_connected;
	case FAKE_OP_SECURITY:
		return &sem_security;
	case FAKE_OP_DISCOVER:
		return &sem_discovered;
	case FAKE_OP_CONFIGURE:
		return &sem_configured;
	case FAKE_OP_QOS:
		return &sem_qos;
	case FAKE_OP_ENABLE:
		return &sem_enabled;
	case FAKE_OP_STREAM_CONNECT:
		return &sem_stream_connected;
	case FAKE_OP_START:
		return &sem_started;
	case FAKE_OP_DISABLE:
		return &sem_disabled;
	case FAKE_OP_RELEASE:
		return &sem_released;
	case FAKE_OP_DISCONNECT:
		return &sem_disconnected;
	default:
		return NULL;
	}
}

void fake_signal(enum fake_op op, uint8_t count)
{
	struct k_sem *sem = fake_sem_for(op);
	uint8_t i;

	if (sem == NULL) {
		return;
	}
	if (op == FAKE_OP_ENABLE) {
		enable_completion_count += count;
	}
	if (op == FAKE_OP_STREAM_CONNECT) {
		for (i = 0U; i < count; i++) {
			uint8_t stream_idx;
			bool signal = false;
			k_spinlock_key_t key = k_spin_lock(&stream_connect_lock);

			if (stream_connect_pending_idx != UINT8_MAX) {
				stream_idx = stream_connect_pending_idx;
				stream_connect_pending_idx = UINT8_MAX;
				stream_connect_completion_idx = stream_idx;
				stream_connect_completion_outcome =
					HIL_SOURCE_STREAM_CONNECT_OUTCOME_CONNECTED;
				stream_connect_completion_count++;
				signal = true;
			}
			k_spin_unlock(&stream_connect_lock, key);
			if (!signal) {
				/* Rejected or already-completed work gets no token. */
				continue;
			}
			k_sem_give(sem);
		}
		return;
	}
	for (i = 0U; i < count; i++) {
		k_sem_give(sem);
	}
}

void fake_signal_stream_connect_failure(uint8_t stream_idx)
{
	bool signal = false;
	k_spinlock_key_t key = k_spin_lock(&stream_connect_lock);

	if (stream_connect_pending_idx == stream_idx) {
		stream_connect_pending_idx = UINT8_MAX;
		stream_connect_completion_idx = stream_idx;
		stream_connect_completion_outcome =
			HIL_SOURCE_STREAM_CONNECT_OUTCOME_RETRYABLE_FAILURE;
		stream_connect_failure_count++;
		signal = true;
	}
	k_spin_unlock(&stream_connect_lock, key);
	if (signal) {
		k_sem_give(&sem_stream_connected);
	}
}

void fake_signal_sent(uint8_t stream_idx)
{
	if (stream_idx >= 2U || suppress_sent[stream_idx]) {
		return;
	}
	sent_count[stream_idx]++;
	hil_source_app_tx_sent(stream_idx);
}

void fake_set_depth_target_signal(uint8_t stream_idx, struct k_sem *sem)
{
	if (stream_idx >= 2U) {
		return;
	}
	depth_target_stream = stream_idx;
	depth_target_signal = sem;
}

void fake_set_send_signal(uint8_t stream_idx, uint32_t send_count_target, struct k_sem *sem)
{
	if (stream_idx >= 2U) {
		return;
	}
	send_signal_stream = stream_idx;
	send_signal_target = send_count_target;
	send_signal = sem;
}

/* ── observability ───────────────────────────────────────────────── */

uint32_t fake_ledger_count(void)
{
	return ledger_count;
}

const struct fake_record *fake_ledger(void)
{
	return ledger;
}

uint32_t fake_kick_count(enum fake_op op)
{
	uint32_t n = 0U;
	uint32_t i;

	for (i = 0U; i < ledger_count; i++) {
		if (ledger[i].op == op) {
			n++;
		}
	}
	return n;
}

uint32_t fake_send_count(uint8_t stream_idx)
{
	if (stream_idx >= 2U) {
		return 0U;
	}
	return send_count[stream_idx];
}

uint32_t fake_sent_count(uint8_t stream_idx)
{
	if (stream_idx >= 2U) {
		return 0U;
	}
	return sent_count[stream_idx];
}

uint32_t fake_enable_busy_rejection_count(void)
{
	return enable_busy_rejection_count;
}

uint32_t fake_enable_accepted_count(void)
{
	return enable_accepted_count;
}

uint32_t fake_enable_completion_count(void)
{
	return enable_completion_count;
}

uint32_t fake_stream_connect_busy_rejection_count(void)
{
	return stream_connect_busy_rejection_count;
}

uint32_t fake_stream_connect_accepted_count(void)
{
	return stream_connect_accepted_count;
}

uint32_t fake_stream_connect_completion_count(void)
{
	uint32_t count;
	k_spinlock_key_t key = k_spin_lock(&stream_connect_lock);

	count = stream_connect_completion_count;
	k_spin_unlock(&stream_connect_lock, key);
	return count;
}

uint32_t fake_stream_connect_failure_count(void)
{
	uint32_t count;
	k_spinlock_key_t key = k_spin_lock(&stream_connect_lock);

	count = stream_connect_failure_count;
	k_spin_unlock(&stream_connect_lock, key);
	return count;
}

enum hil_source_mode fake_run_mode(void)
{
	return run_mode;
}

enum hil_source_profile fake_run_profile(void)
{
	return run_profile;
}

const uint8_t *fake_connect_addr(void)
{
	return connect_addr;
}

uint8_t fake_connect_addr_type(void)
{
	return connect_addr_type;
}
