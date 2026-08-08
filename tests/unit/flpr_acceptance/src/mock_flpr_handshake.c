/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock of the flpr_handshake API surface for the flpr_acceptance suite
 * (R8): implements BOTH the production slot (reset ACK + consumer,
 * registered by flpr_ring_mgr_init) and the diagnostic slot
 * (report/stall-ack/pong/hang, registered by flpr_acceptance_init), so
 * the real core ring manager and the real acceptance module run against
 * one faithful transport mock.  See mock_flpr_handshake.h for controls.
 */
#include "mock_flpr_handshake.h"

#include <errno.h>
#include <string.h>

#include "flpr_handshake.h" /* real production header */

/* ── Mock state ─────────────────────────────────────────────── */

static bool mock_ready;
static bool mock_acked;
static int mock_send_ret;
static uint32_t mock_fail_from; /* sends at index >= N fail with -EIO */
static struct flpr_msg mock_sent[MOCK_HS_MAX_SENT];
static uint32_t mock_sent_count;

static bool mock_auto_ack;
static int32_t mock_ack_data_offset;

static bool mock_send_block;
static K_SEM_DEFINE(mock_send_gate, 0, 1);

static flpr_handshake_ring_handler_t mock_captured_reset_ack;
static flpr_handshake_ring_handler_t mock_captured_consumer;
static flpr_handshake_ring_handler_t mock_captured_diag;
static void *mock_captured_user_data;
static void *mock_captured_diag_user_data;

/* ── flpr_handshake API ─────────────────────────────────────── */

void flpr_handshake_get_status(struct flpr_status *status)
{
	if (status) {
		memset(status, 0, sizeof(*status));
		status->ready = mock_ready;
		status->acked = mock_acked;
	}
}

void flpr_handshake_register_ring_handlers(flpr_handshake_ring_handler_t reset_ack_fn,
					   flpr_handshake_ring_handler_t consumer_fn,
					   void *user_data)
{
	mock_captured_reset_ack = reset_ack_fn;
	mock_captured_consumer = consumer_fn;
	mock_captured_user_data = user_data;
}

void flpr_handshake_register_diag_handlers(flpr_handshake_ring_handler_t diag_handle_fn,
					   void *user_data)
{
	mock_captured_diag = diag_handle_fn;
	mock_captured_diag_user_data = user_data;
}

int flpr_handshake_send_msg(const struct flpr_msg *msg)
{
	if (!msg) {
		return -EINVAL;
	}
	if (mock_send_block) {
		/* Park the sender after recording the attempt (used by the
		 * stress clamp test to freeze the worker mid-run). */
		if (mock_sent_count < MOCK_HS_MAX_SENT) {
			mock_sent[mock_sent_count] = *msg;
		}
		mock_sent_count++;
		k_sem_take(&mock_send_gate, K_FOREVER);
		return 0;
	}
	if (mock_sent_count >= mock_fail_from || mock_send_ret < 0) {
		mock_sent_count++;
		return (mock_send_ret < 0) ? mock_send_ret : -EIO;
	}
	if (mock_sent_count < MOCK_HS_MAX_SENT) {
		mock_sent[mock_sent_count] = *msg;
	}
	mock_sent_count++;

	/* Optional synchronous ACK echo for reset and stall messages.
	 * The ACK echoes the request's sequence token so the
	 * production correlation engines match by sequence. */
	if (mock_auto_ack && msg->type == FLPR_MSG_RING_RESET && mock_captured_reset_ack) {
		struct flpr_msg ack = {
			.type = FLPR_MSG_RING_RESET_ACK,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = msg->seq,
			.data = msg->data + (uint32_t)mock_ack_data_offset,
		};
		mock_captured_reset_ack(&ack, mock_captured_user_data);
	} else if (mock_auto_ack && msg->type == FLPR_MSG_RING_STALL && mock_captured_diag) {
		struct flpr_msg ack = {
			.type = FLPR_MSG_RING_STALL_ACK,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = msg->seq,
			.data = msg->data + (uint32_t)mock_ack_data_offset,
		};
		mock_captured_diag(&ack, mock_captured_diag_user_data);
	}

	return 0;
}

/* ── Test controls ──────────────────────────────────────────── */

void mock_hs_reset(void)
{
	mock_ready = false;
	mock_acked = false;
	mock_send_ret = 0;
	mock_fail_from = UINT32_MAX;
	mock_sent_count = 0;
	memset(mock_sent, 0, sizeof(mock_sent));
	mock_auto_ack = false;
	mock_ack_data_offset = 0;
	mock_send_block = false;
	mock_captured_reset_ack = NULL;
	mock_captured_consumer = NULL;
	mock_captured_diag = NULL;
	mock_captured_user_data = NULL;
	mock_captured_diag_user_data = NULL;
}

void mock_hs_set_ready_acked(bool ready, bool acked)
{
	mock_ready = ready;
	mock_acked = acked;
}

void mock_hs_set_send_result(int ret)
{
	mock_send_ret = ret;
}

void mock_hs_set_send_fail_from(uint32_t index)
{
	mock_fail_from = index;
}

void mock_hs_set_auto_ack(bool enable)
{
	mock_auto_ack = enable;
}

void mock_hs_set_auto_ack_data_offset(int32_t delta)
{
	mock_ack_data_offset = delta;
}

void mock_hs_set_send_block(bool enable)
{
	mock_send_block = enable;
	if (!enable) {
		k_sem_give(&mock_send_gate);
	}
}

void mock_hs_invoke_reset_ack(const struct flpr_msg *msg)
{
	if (mock_captured_reset_ack) {
		mock_captured_reset_ack(msg, mock_captured_user_data);
	}
}

void mock_hs_invoke_consumer(const struct flpr_msg *msg)
{
	if (mock_captured_consumer) {
		mock_captured_consumer(msg, mock_captured_user_data);
	}
}

void mock_hs_invoke_diag(const struct flpr_msg *msg)
{
	if (mock_captured_diag) {
		mock_captured_diag(msg, mock_captured_diag_user_data);
	}
}

uint32_t mock_hs_sent_count(void)
{
	return mock_sent_count;
}

const struct flpr_msg *mock_hs_sent_at(uint32_t i)
{
	return (i < mock_sent_count) ? &mock_sent[i] : NULL;
}

uint32_t mock_hs_sent_type_count(uint8_t type)
{
	uint32_t n = 0;

	for (uint32_t i = 0; i < mock_sent_count; i++) {
		if (mock_sent[i].type == type) {
			n++;
		}
	}
	return n;
}

bool mock_hs_handlers_registered(void)
{
	return mock_captured_reset_ack != NULL && mock_captured_consumer != NULL &&
	       mock_captured_diag != NULL;
}
