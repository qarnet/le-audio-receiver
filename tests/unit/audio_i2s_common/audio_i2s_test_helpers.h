/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared test scaffolding for the audio_i2s suites (header-only).
 * Test-side helpers only; production state-machine logic is never
 * duplicated here.
 */

#ifndef AUDIO_I2S_TEST_HELPERS_H
#define AUDIO_I2S_TEST_HELPERS_H

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "fake_i2s.h"
#include "mock_audio.h"
#include "audio_i2s_test_hook.h"
#include "audio_sink.h"

#define TEST_FRAMES_480  480
#define TEST_FRAMES_360  360
#define TEST_BYTES_480   (TEST_FRAMES_480 * 2 * 2) /* 1920 */
#define TEST_BYTES_360   (TEST_FRAMES_360 * 2 * 2) /* 1440 */
#define TEST_SLAB_BLOCKS 16

/* Startup contract shared with production src/audio_i2s.c: ten distinct
 * silence blocks plus the first data block (11 total) are queued before
 * START.  STARTUP_DATA_WRITE_IDX is the record index of the data block;
 * STARTUP_FIRST_STEADY_WRITE_IDX is the first write record of a steady
 * (already-started) push.
 */
#define STARTUP_SILENCE_BLOCKS         10
#define STARTUP_TOTAL_BLOCKS           (STARTUP_SILENCE_BLOCKS + 1)
#define STARTUP_DATA_WRITE_IDX         (STARTUP_TOTAL_BLOCKS - 1) /* 10 */
#define STARTUP_FIRST_STEADY_WRITE_IDX STARTUP_TOTAL_BLOCKS       /* 11 */

/* ── input buffers ───────────────────────────────────────────────── */

/** Fill buf with frames*2 interleaved samples: sample[i] = base + i. */
static inline void test_fill_input(int16_t *buf, size_t frames, int16_t base)
{
	for (size_t i = 0; i < frames * 2; i++) {
		buf[i] = (int16_t)(base + (int16_t)i);
	}
}

static inline int16_t *test_input_480(void)
{
	static int16_t buf[TEST_FRAMES_480 * 2];

	test_fill_input(buf, TEST_FRAMES_480, 100);
	return buf;
}

static inline int16_t *test_input_360(void)
{
	static int16_t buf[TEST_FRAMES_360 * 2];

	test_fill_input(buf, TEST_FRAMES_360, 200);
	return buf;
}

/* ── slab helpers ────────────────────────────────────────────────── */

static inline int test_slab_free(void)
{
	return k_mem_slab_num_free_get(audio_i2s_test_get_slab());
}

/* Pre-allocate n slab blocks (held by the test; released by reset).
 * Storage lives in test_support.c so all translation units share it.
 */
extern void *test_held[TEST_SLAB_BLOCKS];
extern int test_held_count;
void test_prealloc_blocks(int n);
void test_release_held(void);

/* ── reset between tests ─────────────────────────────────────────── */

/**
 * Full per-test reset.  Order matters: fake-owned blocks are purged first,
 * then test-held blocks released, then module-static state and mocks reset.
 */
static inline void test_reset_all(void)
{
	fake_i2s_reset();
	test_release_held();
	mock_audio_reset_all();
	audio_i2s_test_set_device_ready(true);
	audio_i2s_test_set_slab_alloc_failure(false);
	audio_i2s_test_reset_module_state();
}

/* ── init / stream helpers ───────────────────────────────────────── */

static inline void test_init_ok(void)
{
	zassert_equal(audio_sink_init(), 0, "init");
	zassert_true(audio_i2s_test_is_configured(), "configured after init");
	zassert_equal(fake_i2s_configure_calls(), 1, "configure called once");
	/* Normal tests drive the BAP gate: admission opens after init so
	 * pushes are accepted.  A dedicated test proves init alone leaves
	 * admission closed. */
	zassert_equal(audio_sink_stream_open(), 0, "admission opened after init");
	zassert_true(audio_i2s_test_is_accepting(), "admission open");
}

/** Full startup: init + first push (ten silence + data + START). */
static inline void test_start_stream(void)
{
	test_init_ok();
	zassert_equal(audio_sink_push(test_input_480(), TEST_FRAMES_480 * 2), 0, "startup push");
	zassert_true(audio_i2s_test_is_started(), "started after startup push");
	zassert_equal(fake_i2s_write_calls(), STARTUP_TOTAL_BLOCKS, "eleven startup writes");
	zassert_equal(fake_i2s_trigger_calls(), 1, "one startup trigger");
	zassert_equal(fake_i2s_trigger_rec(0)->cmd, I2S_TRIGGER_START, "START trigger");
}

/* ── write-record content checks ─────────────────────────────────── */

static inline bool test_rec_silence(const struct fake_i2s_write_rec *rec)
{
	for (size_t i = 0; i < FAKE_I2S_SNAPSHOT_BYTES; i++) {
		if (rec->snapshot[i] != 0) {
			return false;
		}
	}
	return true;
}

/** Snapshot equals the int16 sample value pattern (hi << 8) | lo. */
static inline bool test_rec_pattern(const struct fake_i2s_write_rec *rec, uint8_t lo, uint8_t hi)
{
	for (size_t i = 0; i + 1 < FAKE_I2S_SNAPSHOT_BYTES; i += 2) {
		if (rec->snapshot[i] != lo || rec->snapshot[i + 1] != hi) {
			return false;
		}
	}
	return true;
}

/** Snapshot equals the first 8 samples of the given input buffer. */
static inline bool test_rec_matches_input(const struct fake_i2s_write_rec *rec,
					  const int16_t *input)
{
	for (size_t i = 0; i + 1 < FAKE_I2S_SNAPSHOT_BYTES; i += 2) {
		uint16_t sample = (uint16_t)input[i / 2];

		if (rec->snapshot[i] != (uint8_t)(sample & 0xff) ||
		    rec->snapshot[i + 1] != (uint8_t)(sample >> 8)) {
			return false;
		}
	}
	return true;
}

/** Every write record's pointer is distinct from every other. */
static inline void test_assert_distinct_pointers(int first, int last)
{
	for (int i = first; i < last; i++) {
		for (int j = i + 1; j < last; j++) {
			zassert_not_equal(fake_i2s_write_rec(i)->ptr, fake_i2s_write_rec(j)->ptr,
					  "pointer reuse between writes");
		}
	}
}

static inline void test_assert_no_duplicate_writes(void)
{
	zassert_equal(fake_i2s_duplicate_write_violations(), 0, "double-submit violation");
}

/** Exact slab reclaim proof: no free block lost after a failure path. */
static inline void test_assert_slab_fully_reclaimable(void)
{
	zassert_equal(test_slab_free(), TEST_SLAB_BLOCKS, "slab fully reclaimable");
}

#endif /* AUDIO_I2S_TEST_HELPERS_H */
