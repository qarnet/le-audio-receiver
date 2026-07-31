/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lc3_wrap.h"

#include <stdbool.h>
#include <stddef.h>

/* Linker wrap of the real liblc3 entry point (see lc3_wrap.h). */
int __real_lc3_decode(lc3_decoder_t decoder, const void *in, int nbytes, enum lc3_pcm_format fmt,
		      void *pcm, int stride);

#define LC3_WRAP_MAX_FAIL 4

static lc3_decoder_t fail_decoders[LC3_WRAP_MAX_FAIL];
static int fail_count;
static int invocation_count;

int __wrap_lc3_decode(lc3_decoder_t decoder, const void *in, int nbytes, enum lc3_pcm_format fmt,
		      void *pcm, int stride)
{
	invocation_count++;

	for (int i = 0; i < fail_count; i++) {
		if (decoder == fail_decoders[i]) {
			return -1;
		}
	}

	return __real_lc3_decode(decoder, in, nbytes, fmt, pcm, stride);
}

void lc3_wrap_reset(void)
{
	fail_count = 0;
	invocation_count = 0;
}

void lc3_wrap_fail_decoder(lc3_decoder_t decoder)
{
	if (fail_count < LC3_WRAP_MAX_FAIL) {
		fail_decoders[fail_count++] = decoder;
	}
}

int lc3_wrap_invocation_count(void)
{
	return invocation_count;
}
