/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock implementation of the flpr_handshake API surface consumed by the
 * ring manager.  See mock_flpr_handshake.h for controls.
 */
#include "mock_flpr_handshake.h"

#include <errno.h>
#include <string.h>

#include "flpr_handshake.h" /* real production header */

/* ── Mock state ─────────────────────────────────────────────── */

static bool mock_ready;
static bool mock_acked;
static int mock_send_ret;
static struct flpr_msg mock_sent[MOCK_HS_MAX_SENT];
static uint32_t mock_sent_count;

static bool mock_auto_ack;
static int32_t mock_ack_data_offset;

static flpr_handshake_ring_handler_t mock_captured_reset_ack;
static flpr_handshake_ring_handler_t mock_captured_consumer;
static flpr_handshake_ring_handler_t mock_captured_report;
static flpr_handshake_ring_handler_t mock_captured_stall_ack;
static void *mock_captured_user_data;

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
					   flpr_handshake_ring_handler_t report_fn,
					   flpr_handshake_ring_handler_t stall_ack_fn,
					   void *user_data)
{
	mock_captured_reset_ack = reset_ack_fn;
	mock_captured_consumer = consumer_fn;
	mock_captured_report = report_fn;
	mock_captured_stall_ack = stall_ack_fn;
	mock_captured_user_data = user_data;
}

int flpr_handshake_send_msg(const struct flpr_msg *msg)
{
	if (!msg) {
		return -EINVAL;
	}
	if (mock_send_ret < 0) {
		mock_sent_count++;
		return mock_send_ret;
	}
	if (mock_sent_count < MOCK_HS_MAX_SENT) {
		mock_sent[mock_sent_count] = *msg;
	}
	mock_sent_count++;

	/* Optional synchronous ACK echo for reset and stall messages.
	 * R1: the ACK echoes the request's sequence token so the
	 * production handlers correlate by sequence. */
	if (mock_auto_ack && msg->type == FLPR_MSG_RING_RESET && mock_captured_reset_ack) {
		struct flpr_msg ack = {
			.type = FLPR_MSG_RING_RESET_ACK,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = msg->seq,
			.data = msg->data + (uint32_t)mock_ack_data_offset,
		};
		mock_captured_reset_ack(&ack, mock_captured_user_data);
	} else if (mock_auto_ack && msg->type == FLPR_MSG_RING_STALL && mock_captured_stall_ack) {
		struct flpr_msg ack = {
			.type = FLPR_MSG_RING_STALL_ACK,
			.version = FLPR_PROTOCOL_VERSION,
			.seq = msg->seq,
			.data = msg->data + (uint32_t)mock_ack_data_offset,
		};
		mock_captured_stall_ack(&ack, mock_captured_user_data);
	}

	return 0;
}

/* ── Test controls ──────────────────────────────────────────── */

void mock_hs_reset(void)
{
	mock_ready = false;
	mock_acked = false;
	mock_send_ret = 0;
	mock_sent_count = 0;
	memset(mock_sent, 0, sizeof(mock_sent));
	mock_auto_ack = false;
	mock_ack_data_offset = 0;
	mock_captured_reset_ack = NULL;
	mock_captured_consumer = NULL;
	mock_captured_report = NULL;
	mock_captured_stall_ack = NULL;
	mock_captured_user_data = NULL;
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

void mock_hs_set_auto_ack(bool enable)
{
	mock_auto_ack = enable;
}

void mock_hs_set_auto_ack_data_offset(int32_t delta)
{
	mock_ack_data_offset = delta;
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

void mock_hs_invoke_report(const struct flpr_msg *msg)
{
	if (mock_captured_report) {
		mock_captured_report(msg, mock_captured_user_data);
	}
}

void mock_hs_invoke_stall_ack(const struct flpr_msg *msg)
{
	if (mock_captured_stall_ack) {
		mock_captured_stall_ack(msg, mock_captured_user_data);
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
	       mock_captured_report != NULL && mock_captured_stall_ack != NULL;
}
