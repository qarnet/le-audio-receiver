/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Faithful audio_sink.h fake for the audio_stream_session unit suite.
 *
 * Records every accepted push (count, last sample count, and a copy of
 * the FIRST push for golden content assertions) and exposes test knobs:
 * forced push failure (-ENOMEM) for the sink-failure accounting path, and
 * a blocking push mode (semaphore in / semaphore out) that lets the
 * concurrency tests park a receive lease inside a push while the test
 * thread inspects session lock discipline and rx_close drain behavior.
 */

#include "audio_sink.h"
#include "fake_sink.h"

#include <zephyr/kernel.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define FAKE_SINK_MAX_PUSHES  64
#define FAKE_SINK_MAX_SAMPLES 960 /* 48 kHz × 10 ms × 2 ch */

static int16_t first_push[FAKE_SINK_MAX_SAMPLES];
static atomic_uint push_count;
static atomic_uint last_sample_count;
static atomic_uint fail_pushes;  /* pushes that returned < 0 */
static atomic_uint input_frames; /* last audio_sink_set_input_frames value */
static atomic_bool admission_open;
static atomic_bool configured;
static atomic_bool fail_next;    /* when set, push returns -ENOMEM */
static atomic_bool block_pushes; /* when set, push parks between two semaphores */
static K_SEM_DEFINE(in_sem, 0, 1);
static K_SEM_DEFINE(out_sem, 0, 1);

void fake_sink_reset(void)
{
	atomic_store(&push_count, 0U);
	atomic_store(&last_sample_count, 0U);
	atomic_store(&fail_pushes, 0U);
	atomic_store(&input_frames, 0U);
	atomic_store(&admission_open, false);
	atomic_store(&configured, false);
	atomic_store(&fail_next, false);
	atomic_store(&block_pushes, false);
	memset(first_push, 0, sizeof(first_push));
	k_sem_reset(&in_sem);
	k_sem_reset(&out_sem);
}

/* Ordered FNV-1a of the first @p n int16 values of the first accepted push. */
uint32_t fake_sink_first_push_hash(size_t n)
{
	uint32_t hash = 0x811c9dc5UL;

	if (n > FAKE_SINK_MAX_SAMPLES) {
		n = FAKE_SINK_MAX_SAMPLES;
	}
	for (size_t i = 0; i < n; i++) {
		uint16_t u = (uint16_t)first_push[i];

		hash ^= (uint8_t)(u & 0xFF);
		hash *= 0x01000193UL;
		hash ^= (uint8_t)((u >> 8) & 0xFF);
		hash *= 0x01000193UL;
	}
	return hash;
}

uint32_t fake_sink_push_count(void)
{
	return atomic_load(&push_count);
}

uint32_t fake_sink_last_sample_count(void)
{
	return atomic_load(&last_sample_count);
}

uint32_t fake_sink_input_frames(void)
{
	return atomic_load(&input_frames);
}

uint32_t fake_sink_fail_pushes(void)
{
	return atomic_load(&fail_pushes);
}

void fake_sink_set_fail_next(bool fail)
{
	atomic_store(&fail_next, fail);
}

void fake_sink_set_block_pushes(bool block)
{
	atomic_store(&block_pushes, block);
}

void fake_sink_blocked_enter(void)
{
	k_sem_take(&in_sem, K_FOREVER);
}

void fake_sink_blocked_release(void)
{
	k_sem_give(&out_sem);
}

/* ── public audio_sink API ───────────────────────────────────────── */

int audio_sink_init(void)
{
	atomic_store(&configured, true);
	atomic_store(&admission_open, false);
	return 0;
}

int audio_sink_stream_open(void)
{
	atomic_store(&admission_open, true);
	return 0;
}

void audio_sink_stream_close(void)
{
	atomic_store(&admission_open, false);
}

void audio_sink_stop(void)
{
	atomic_store(&admission_open, false);
}

void audio_sink_set_input_frames(uint16_t frames)
{
	atomic_store(&input_frames, frames);
}

int audio_sink_push(const int16_t *data, size_t sample_count)
{
	if (data == NULL || sample_count == 0U || (sample_count & 1U)) {
		return -EINVAL;
	}

	if (atomic_load(&fail_next)) {
		atomic_store(&fail_next, false);
		atomic_fetch_add(&fail_pushes, 1U);
		return -ENOMEM;
	}

	if (atomic_load(&block_pushes)) {
		/* Park the lease inside the push: proves rx_close waits and
		 * that the session mutex is not held across the push. */
		k_sem_give(&in_sem);
		k_sem_take(&out_sem, K_FOREVER);
	}

	uint32_t idx = atomic_fetch_add(&push_count, 1U);

	if (idx == 0U) {
		memcpy(first_push, data, sample_count * sizeof(int16_t));
	}
	atomic_store(&last_sample_count, sample_count);
	return 0;
}
