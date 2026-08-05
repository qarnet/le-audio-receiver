/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Linker-wrap seam for lc3_decode (see lc3_wrap.c).  The session suite
 * uses the fail-all mode: the production session owns the decoder
 * pointers internally, so hard-failure tests fail EVERY lc3_decode call
 * and assert the observable outcome (decode-error accounting, no push,
 * event consumed) rather than inspecting private state.
 */

#ifndef LC3_WRAP_H
#define LC3_WRAP_H

#include <stdbool.h>

#include "lc3.h"

int __real_lc3_decode(lc3_decoder_t decoder, const void *in, int nbytes, enum lc3_pcm_format fmt,
		      void *pcm, int stride);

void lc3_wrap_reset(void);
void lc3_wrap_fail_decoder(lc3_decoder_t decoder);
void lc3_wrap_fail_all(bool fail);
int lc3_wrap_invocation_count(void);

#endif /* LC3_WRAP_H */
