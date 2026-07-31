/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-only failure injection for lc3_decode().
 *
 * NCS v3.3.0 liblc3 1.1.2 only returns a hard negative from
 * lc3_decode() for parameter errors (NULL handle, or frame size outside
 * the 20..400 byte range).  audio_decode_sdu() pre-validates both, so
 * the hard-error accounting path in the production decoder is
 * unreachable with real liblc3 input.  This module wraps lc3_decode at
 * link time (-Wl,--wrap=lc3_decode) so tests can inject a hard failure
 * for one specific decoder handle and verify the production error
 * accounting.  All other invocations pass through to the real liblc3
 * implementation.
 */

#ifndef LC3_WRAP_H
#define LC3_WRAP_H

#include "lc3.h"

/* Reset injection state (call before each test). */
void lc3_wrap_reset(void);

/* Make the next lc3_decode() call for @p decoder return -1. */
void lc3_wrap_fail_decoder(lc3_decoder_t decoder);

/* Total lc3_decode() invocations observed since the last reset. */
int lc3_wrap_invocation_count(void);

#endif /* LC3_WRAP_H */
