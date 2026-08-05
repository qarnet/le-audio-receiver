/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Faithful audio_volume.h fake for the audio_stream_session unit suite.
 *
 * Identity volume (no scaling, never muted): the session's volume call
 * site is exercised (a per-call counter proves the ordering), while the
 * real audio_volume.c scaling/VCP behavior stays proven by the volume
 * suite.  Identity keeps the golden push hashes equal to the checked-in
 * decode fixtures, so the session tests prove decode->volume->push wiring
 * exactly.
 */

#include "audio_volume.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

static atomic_uint apply_count;

void fake_volume_reset(void)
{
	atomic_store(&apply_count, 0U);
}

uint32_t fake_volume_apply_count(void)
{
	return atomic_load(&apply_count);
}

int audio_volume_init(void)
{
	return 0;
}

uint8_t audio_volume_get(void)
{
	return 255U;
}

bool audio_volume_is_muted(void)
{
	return false;
}

void audio_volume_apply(int16_t *buf, size_t samples)
{
	(void)buf;
	(void)samples;
	atomic_fetch_add(&apply_count, 1U);
}
