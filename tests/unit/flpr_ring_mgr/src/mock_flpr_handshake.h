/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned mock of the flpr_handshake API surface used by the ring
 * manager: get_status / register_ring_handlers / send_msg.
 *
 * The mock captures the registered ring handlers (so tests invoke the
 * production static handlers in flpr_ring_mgr.c), records every sent
 * struct flpr_msg, and exposes controllable ready/acked and send result.
 */
#ifndef MOCK_FLPR_HANDSHAKE_H_
#define MOCK_FLPR_HANDSHAKE_H_

#include <stdint.h>
#include <stdbool.h>

#include "flpr_protocol.h" /* struct flpr_msg */

#ifdef __cplusplus
extern "C" {
#endif

#define MOCK_HS_MAX_SENT 64U

/* Reset all mock state to defaults (ready/acked false, send ok). */
void mock_hs_reset(void);

void mock_hs_set_ready_acked(bool ready, bool acked);

/* Send result: <0 fails, >=0 succeeds. */
void mock_hs_set_send_result(int ret);

/* Optional synchronous ACK injection: when enabled, a successful send of
 * FLPR_MSG_RING_RESET echoes RING_RESET_ACK and a successful send of
 * FLPR_MSG_RING_STALL echoes RING_STALL_ACK through the captured
 * production handlers, with data = msg->data + data_offset. */
void mock_hs_set_auto_ack(bool enable);
void mock_hs_set_auto_ack_data_offset(int32_t delta);

/* Invoke the captured production ring handlers (no-op when unregistered). */
void mock_hs_invoke_reset_ack(const struct flpr_msg *msg);
void mock_hs_invoke_consumer(const struct flpr_msg *msg);
void mock_hs_invoke_report(const struct flpr_msg *msg);
void mock_hs_invoke_stall_ack(const struct flpr_msg *msg);

/* Observation. */
uint32_t mock_hs_sent_count(void);
const struct flpr_msg *mock_hs_sent_at(uint32_t i);
uint32_t mock_hs_sent_type_count(uint8_t type);
bool mock_hs_handlers_registered(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_FLPR_HANDSHAKE_H_ */
