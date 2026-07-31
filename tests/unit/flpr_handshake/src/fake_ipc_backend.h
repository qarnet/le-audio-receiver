/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fake IPC service backend for the FLPR handshake suite (native_sim).
 *
 * Follows the NCS v3.3.0 pattern from
 * zephyr/tests/subsys/ipc/ipc_service/: a test-local devicetree binding
 * (fake-ipc-backend), a struct ipc_service_backend registered via
 * DEVICE_DT_INST_DEFINE, and the real Zephyr ipc_service_* APIs used by
 * production src/flpr_handshake.c.
 *
 * The backend captures the registered endpoint config and invokes its
 * real callbacks (bound/unbound/received/error).  Test controls expose
 * open/register/deregister/send return values, optional automatic bound
 * callback at register, sent-message history, arbitrary incoming
 * messages, and an optional send-block mode (for stress clamp tests).
 */
#ifndef FAKE_IPC_BACKEND_H_
#define FAKE_IPC_BACKEND_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "flpr_protocol.h" /* struct flpr_msg */

#ifdef __cplusplus
extern "C" {
#endif

#define FAKE_IPC_MAX_SENT 256U

/* Reset all backend controls (results ok, auto-bound on, block off). */
void fake_ipc_reset(void);

/* Return-value controls. */
void fake_ipc_set_open_result(int ret);
void fake_ipc_set_register_result(int ret);
void fake_ipc_set_deregister_result(int ret);
void fake_ipc_set_send_result(int ret);

/* When true (default), register_endpoint invokes cfg->cb.bound. */
void fake_ipc_set_auto_bound(bool enable);

/* When true, sends block forever on an internal semaphore (used to park
 * a stress worker mid-send while observing clamped state). */
void fake_ipc_set_send_block(bool enable);

/* Incoming-message injection: invokes the captured received callback. */
void fake_ipc_receive(const void *data, size_t len);

/* Unbind / error injection: invoke the captured callbacks. */
void fake_ipc_unbind(void);
void fake_ipc_error(const char *message);

/* Observation. */
uint32_t fake_ipc_sent_count(void);
const struct flpr_msg *fake_ipc_sent_at(uint32_t i);
uint32_t fake_ipc_sent_type_count(uint8_t type);
uint32_t fake_ipc_send_calls(void);
bool fake_ipc_endpoint_registered(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_IPC_BACKEND_H_ */
