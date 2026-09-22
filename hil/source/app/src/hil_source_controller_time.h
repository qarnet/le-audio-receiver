/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Platform view of the SoftDevice Controller clock.
 */

#ifndef HIL_SOURCE_CONTROLLER_TIME_H
#define HIL_SOURCE_CONTROLLER_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize platform controller-clock infrastructure. */
int hil_source_controller_time_init(void);

/* Read platform controller time modulo 2^32 microseconds. Returns -EAGAIN
 * while synchronization or the counter is not ready, -ENODEV when platform
 * clock infrastructure is unavailable, or -EIO when an nRF5340 network-core
 * restart invalidates the mirrored clock epoch. */
int hil_source_controller_time_get(uint32_t *time_us);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_CONTROLLER_TIME_H */
