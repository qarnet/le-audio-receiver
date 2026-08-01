/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned shadow of the installed NCS v3.3.0 <helpers/nrfx_gppi.h>.
 * Declares exactly the API surface used by src/audio_timing_nrf54.c;
 * the mock implementation lives in src/mock_hal.c.  Never part of a
 * production build.
 */

#ifndef NRFX_GPPI_H__
#define NRFX_GPPI_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t nrfx_gppi_handle_t;

int nrfx_gppi_conn_alloc(uint32_t eep, uint32_t tep, nrfx_gppi_handle_t *p_handle);
void nrfx_gppi_conn_enable(nrfx_gppi_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* NRFX_GPPI_H__ */
