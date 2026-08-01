/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned shadow of the installed NCS v3.3.0 <hal/nrf_clock.h>
 * feature macro.  Defaults to HFCLKAUDIO present; the no-HFCLKAUDIO
 * variant suite compiles with -DNRF_CLOCK_HAS_HFCLKAUDIO=0 to prove
 * the no-op branch.  Never part of a production build.
 */

#ifndef NRF_CLOCK_H__
#define NRF_CLOCK_H__

#ifndef NRF_CLOCK_HAS_HFCLKAUDIO
#define NRF_CLOCK_HAS_HFCLKAUDIO 1
#endif

#endif /* NRF_CLOCK_H__ */
