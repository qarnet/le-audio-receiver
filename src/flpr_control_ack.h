/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared control-ACK correlation engine for FLPR cpuapp control
 * transactions (R8).  ONE owner of the request/ACK correlation logic
 * used by the production coordinated reset (flpr_ring_mgr) and the
 * acceptance FLPR-stall requests (flpr_acceptance).  Never duplicate.
 */

#ifndef FLPR_CONTROL_ACK_H_
#define FLPR_CONTROL_ACK_H_

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>

#include "flpr_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One armed request/ACK correlation instance (reset, stall, ...). */
struct flpr_control_ack {
	struct k_sem *sem;      /* waiter wakeup (given outside the lock) */
	uint32_t payload;       /* stored ACK data */
	uint32_t expected_data; /* data the ACK must echo */
	uint16_t expected_seq;  /* armed expected request token */
	bool armed;             /* request in flight */
	uint32_t next_token;    /* next 16-bit token (1..0xFFFF, no wrap) */
	uint32_t stale_count;   /* stale/duplicate ACKs ignored */
};

/**
 * @brief Initialize one instance and attach its waiter semaphore.
 *
 * @param ctl  Instance to initialize.
 * @param sem  Waiter semaphore (limit 1).
 * @return 0 on success, -EINVAL on NULL arguments.
 */
int flpr_control_ack_init(struct flpr_control_ack *ctl, struct k_sem *sem);

/**
 * @brief Register an instance for session reset
 *        (flpr_control_ack_reset_session()).
 *
 * Idempotent per instance.  Registration survives across requests.
 */
void flpr_control_ack_register(struct flpr_control_ack *ctl);

/**
 * @brief Disarm + clear the request state, then drain stale semaphore
 *        tokens while disarmed (handlers ignore ACKs and do not give).
 */
void flpr_control_ack_begin(struct flpr_control_ack *ctl);

/**
 * @brief Allocate the next nonzero 16-bit token without wrap, install
 *        the expected data/token, and arm.
 *
 * @return The token, or -EOVERFLOW when the token space of the current
 *         session is exhausted (cleared only by
 *         flpr_control_ack_reset_session()).
 */
int flpr_control_ack_arm(struct flpr_control_ack *ctl, uint32_t expected_data);

/**
 * @brief Disarm (send failure / timeout).  The payload is cleared so a
 *        late ACK is ignored even when its data equals a later retry.
 */
void flpr_control_ack_disarm(struct flpr_control_ack *ctl);

/**
 * @brief First-ACK-while-armed-and-matching handler: stores the payload,
 *        disarms, then gives the semaphore outside the lock.
 *        Stale/duplicate ACKs are counted and ignored.
 *        Call from IPC receive context.
 */
void flpr_control_ack_handle(struct flpr_control_ack *ctl, const struct flpr_msg *msg);

/**
 * @brief Wait for the ACK, snapshot the stored payload and verify the
 *        exact expected data.  Timeout disarms.
 *
 * @return 0 on exact match, -ETIMEDOUT on timeout, -EIO on ACK data
 *         mismatch.
 */
int flpr_control_ack_wait(struct flpr_control_ack *ctl, uint32_t timeout_ms,
			  uint32_t expected_data);

/**
 * @brief Last acked payload (diagnostics).
 */
uint32_t flpr_control_ack_payload(struct flpr_control_ack *ctl);

/**
 * @brief Clear every registered instance's request state and reset the
 *        token counters to 1 (known quiescence boundary: remote
 *        restart), then drain every instance's semaphore.
 */
void flpr_control_ack_reset_session(void);

#if defined(FLPR_CONTROL_ACK_NATIVE_TEST)
/* GCOVR_EXCL_START — test-only helpers, absent from production builds */

void flpr_control_ack_test_set_next_token(struct flpr_control_ack *ctl, uint16_t token);
uint32_t flpr_control_ack_test_stale_count(struct flpr_control_ack *ctl);
void flpr_control_ack_test_reset_registry(void);

/* GCOVR_EXCL_STOP */
#endif /* FLPR_CONTROL_ACK_NATIVE_TEST */

#ifdef __cplusplus
}
#endif

#endif /* FLPR_CONTROL_ACK_H_ */
