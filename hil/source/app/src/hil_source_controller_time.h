/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * nRF5340 application-core view of the SoftDevice Controller clock.
 */

#ifndef HIL_SOURCE_CONTROLLER_TIME_H
#define HIL_SOURCE_CONTROLLER_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Arm RTC0 and IPC4 synchronization before Bluetooth starts network core. */
int hil_source_controller_time_init(void);

/* Read controller time modulo 2^32 microseconds. The application RTC is
 * cleared from the network core when MPSL starts its RTC, so this value and
 * SDC ISO timestamps share one clock domain. Returns -EAGAIN until that
 * synchronization pulse has been observed, or -EIO after a network-core
 * restart invalidates the mirrored clock epoch. */
int hil_source_controller_time_get(uint32_t *time_us);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_CONTROLLER_TIME_H */
