/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned mock of the flpr_handshake API surface for the
 * flpr_acceptance suite (R8): production slot (reset ACK + consumer)
 * AND diagnostic slot (report/stall-ack/pong/hang).  Captures the
 * registered handlers (so tests invoke the production static handlers
 * in flpr_ring_mgr.c and flpr_acceptance.c), records every sent
 * struct flpr_msg, and exposes controllable ready/acked, send result,
 * scripted send failure, and synchronous ACK echo.
 */
#ifndef MOCK_FLPR_HANDSHAKE_H_
#define MOCK_FLPR_HANDSHAKE_H_

#include <stdint.h>
#include <stdbool.h>

#include "flpr_protocol.h" /* struct flpr_msg */

#ifdef __cplusplus
extern "C" {
#endif

#define MOCK_HS_MAX_SENT 256U

/* Reset all mock state to defaults (ready/acked false, send ok). */
void mock_hs_reset(void);

void mock_hs_set_ready_acked(bool ready, bool acked);

/* Send result: <0 fails every send, >=0 succeeds. */
void mock_hs_set_send_result(int ret);

/* Scripted failure: every send at index >= N fails with -EIO. */
void mock_hs_set_send_fail_from(uint32_t index);

/* Optional synchronous ACK injection: when enabled, a successful send of
 * FLPR_MSG_RING_RESET echoes RING_RESET_ACK through the captured
 * production handler, and a successful send of FLPR_MSG_RING_STALL
 * echoes RING_STALL_ACK through the captured diagnostic handler, with
 * data = msg->data + data_offset. */
void mock_hs_set_auto_ack(bool enable);
void mock_hs_set_auto_ack_data_offset(int32_t delta);

/* Blocking send: park every sender until disabled (stress clamp test). */
void mock_hs_set_send_block(bool enable);

/* Invoke the captured production/diagnostic handlers (no-op when
 * unregistered). */
void mock_hs_invoke_reset_ack(const struct flpr_msg *msg);
void mock_hs_invoke_consumer(const struct flpr_msg *msg);
void mock_hs_invoke_diag(const struct flpr_msg *msg);

/* Observation. */
uint32_t mock_hs_sent_count(void);
const struct flpr_msg *mock_hs_sent_at(uint32_t i);
uint32_t mock_hs_sent_type_count(uint8_t type);
bool mock_hs_handlers_registered(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_FLPR_HANDSHAKE_H_ */
