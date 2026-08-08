/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock of nrfx_clock_hfclkaudio_config_set() capturing every register
 * write in order.  Test-owned; never part of a production build.
 */

#include "mock_clock.h"

#include <nrfx_clock_hfclkaudio.h>

#include <string.h>

#define MAX_WRITES 64

static uint16_t writes[MAX_WRITES];
static int write_count;

int mock_clock_write_count(void)
{
	return write_count;
}

uint16_t mock_clock_write_at(int i)
{
	return writes[i];
}

void mock_clock_reset(void)
{
	write_count = 0;
	memset(writes, 0, sizeof(writes));
}

void nrfx_clock_hfclkaudio_config_set(uint16_t freq_value)
{
	if (write_count < MAX_WRITES) {
		writes[write_count] = freq_value;
	}
	write_count++;
}
