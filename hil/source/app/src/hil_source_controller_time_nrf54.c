/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Read the nRF54L15 SoftDevice Controller clock from Zephyr-owned GRTC.
 */

#include <errno.h>
#include <stdint.h>

#include <nrfx_grtc.h>

#include "hil_source_controller_time.h"

int hil_source_controller_time_init(void)
{
	return nrfx_grtc_init_check() ? 0 : -ENODEV;
}

int hil_source_controller_time_get(uint32_t *time_us)
{
	if (time_us == NULL) {
		return -EINVAL;
	}
	if (!nrfx_grtc_init_check()) {
		return -ENODEV;
	}
	if (!nrfx_grtc_ready_check()) {
		return -EAGAIN;
	}

	*time_us = (uint32_t)nrfx_grtc_syscounter_get();
	return 0;
}
