/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock of audio_drift_frequency_error_update() for tests/unit/timing_nrf54.
 * Captures every delivered ppm so tests can prove that every non-stale
 * measurement reaches the drift feedforward.  Test-owned; never part of
 * a production build.
 */

#include "mock_drift.h"

#include <string.h>

#define MAX_DELIVERIES 64

static int32_t deliveries[MAX_DELIVERIES];
static int delivery_count;

int mock_drift_ppm_count(void)
{
	return delivery_count;
}

int32_t mock_drift_ppm_at(int i)
{
	return deliveries[i];
}

void mock_drift_reset(void)
{
	delivery_count = 0;
	memset(deliveries, 0, sizeof(deliveries));
}

void audio_drift_frequency_error_update(int32_t local_clock_error_ppm)
{
	if (delivery_count < MAX_DELIVERIES) {
		deliveries[delivery_count] = local_clock_error_ppm;
	}
	delivery_count++;
}
