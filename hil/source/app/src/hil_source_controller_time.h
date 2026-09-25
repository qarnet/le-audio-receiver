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

/* Read nRF54L15 controller time from Zephyr-owned GRTC, modulo 2^32 us.
 * NULL returns -EINVAL; uninitialized GRTC returns -ENODEV; not-ready
 * GRTC returns -EAGAIN. On success, writes *time_us and returns 0.
 * No network-core mirror or mirrored clock epoch is used. */
int hil_source_controller_time_get(uint32_t *time_us);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_CONTROLLER_TIME_H */
