/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock of nrfx_clock_hfclkaudio_config_set() capturing every register
 * write.  Test-owned; never part of a production build.
 */

#ifndef MOCK_CLOCK_H
#define MOCK_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int mock_clock_write_count(void);
uint16_t mock_clock_write_at(int i);
void mock_clock_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_CLOCK_H */
