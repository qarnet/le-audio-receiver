/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned shadow of the installed NCS v3.3.0
 * <nrfx_clock_hfclkaudio.h>.  Declares exactly the API surface used by
 * src/audio_clock_actuator_apll.c; the mock implementation lives in
 * src/mock_clock.c.  Never part of a production build.
 */

#ifndef NRFX_CLOCK_HFCLKAUDIO_H__
#define NRFX_CLOCK_HFCLKAUDIO_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void nrfx_clock_hfclkaudio_config_set(uint16_t freq_value);

#ifdef __cplusplus
}
#endif

#endif /* NRFX_CLOCK_HFCLKAUDIO_H__ */
