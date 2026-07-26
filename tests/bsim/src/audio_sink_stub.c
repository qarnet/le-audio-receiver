/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_sink.h"

#include <stdatomic.h>
#include <bs_tracing.h>
#include <bstests.h>

#define PASS_FRAME_COUNT 100

static atomic_int frame_count;

int audio_sink_init(void)
{
	return 0;
}

void audio_sink_stop(void)
{
	atomic_store(&frame_count, 0);
}

int audio_sink_push(const int16_t *data, size_t sample_count)
{
	(void)data;
	(void)sample_count;

	int cnt = atomic_fetch_add(&frame_count, 1) + 1;

	if (cnt == PASS_FRAME_COUNT) {
		PASS("le_audio_receiver: received %d decoded audio frames\n", cnt);
	}

	return 0;
}
