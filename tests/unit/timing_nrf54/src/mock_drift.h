/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock of audio_drift_frequency_error_update() for tests/unit/timing_nrf54.
 * Captures every delivered ppm so tests can prove that every non-stale
 * measurement reaches the drift feedforward.  Test-owned; never part of
 * a production build.
 */

#ifndef MOCK_DRIFT_H
#define MOCK_DRIFT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int mock_drift_ppm_count(void);
int32_t mock_drift_ppm_at(int i);
void mock_drift_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_DRIFT_H */
