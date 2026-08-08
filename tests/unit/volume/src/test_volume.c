/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * T2C — production volume suite.
 *
 * Compiles and executes the REAL src/audio_volume.c (VCP branch) against
 * a test-local shadow of the NCS v3.3.0 VCP renderer types plus a fake
 * bt_vcp_vol_rend_register() backend, and the REAL src/audio_perf.c for
 * hook-balance proof.  See docs/testing/t2-audio-pipeline-tests.md.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "audio_volume.h"
#include "audio_perf.h"
#include "fake_vcp.h"

/* ── helpers ─────────────────────────────────────────────────────── */

#define GUARD_VAL 0x5A5A

static void reset_before_each(void *unused)
{
	ARG_UNUSED(unused);
	fake_vcp_reset();
	audio_perf_reset();
}

ZTEST_SUITE(volume, NULL, NULL, reset_before_each, NULL, NULL);

static void set_volume(uint8_t vol, uint8_t mute)
{
	zassert_true(fake_vcp_emit_state(NULL, 0, vol, mute), "callback installed");
}

static int16_t trunc_div(int32_t num, int32_t den)
{
	return (int16_t)(num / den);
}

/* ── init and registration ───────────────────────────────────────── */

ZTEST(volume, test_init_success_registers_defaults)
{
	zassert_ok(audio_volume_init(), "init");
	zassert_equal(fake_vcp_register_calls(), 1, "registered once");
	zassert_equal(fake_vcp_registered_volume(), 195, "default volume 195");
	zassert_equal(fake_vcp_registered_mute(), BT_VCP_STATE_UNMUTED, "default unmuted");
	zassert_equal(fake_vcp_registered_step(), 16, "step 16");
	zassert_equal(audio_volume_get(), 195, "state volume");
	zassert_false(audio_volume_is_muted(), "state unmuted");
}

ZTEST(volume, test_init_failure_propagated_state_deterministic)
{
	fake_vcp_set_register_result(-EACCES);
	zassert_equal(audio_volume_init(), -EACCES, "register error propagated");
	zassert_equal(fake_vcp_register_calls(), 1, "register attempted");

	/* Default state remains deterministic after a failed registration. */
	zassert_equal(audio_volume_get(), 195, "default volume");
	zassert_false(audio_volume_is_muted(), "default unmuted");

	/* Repeated failed init is stable. */
	zassert_equal(audio_volume_init(), -EACCES, "again");
	zassert_equal(audio_volume_get(), 195, "still default");
}

/* ── callback state updates ──────────────────────────────────────── */

ZTEST(volume, test_callback_success_updates_packed_state)
{
	zassert_ok(audio_volume_init(), "init");

	zassert_true(fake_vcp_emit_state(NULL, 0, 80, BT_VCP_STATE_MUTED), "emit");
	zassert_equal(audio_volume_get(), 80, "volume 80");
	zassert_true(audio_volume_is_muted(), "muted");

	zassert_true(fake_vcp_emit_state(NULL, 0, 200, BT_VCP_STATE_UNMUTED), "emit");
	zassert_equal(audio_volume_get(), 200, "volume 200");
	zassert_false(audio_volume_is_muted(), "unmuted");

	zassert_true(fake_vcp_emit_state(NULL, 0, 0, BT_VCP_STATE_UNMUTED), "emit");
	zassert_equal(audio_volume_get(), 0, "volume 0");
	zassert_false(audio_volume_is_muted(), "unmuted at volume 0");
}

ZTEST(volume, test_callback_error_leaves_state_unchanged)
{
	zassert_ok(audio_volume_init(), "init");
	set_volume(120, 0);

	zassert_true(fake_vcp_emit_state(NULL, -EIO, 7, BT_VCP_STATE_MUTED), "emit");
	zassert_equal(audio_volume_get(), 120, "volume unchanged on error");
	zassert_false(audio_volume_is_muted(), "mute unchanged on error");

	zassert_true(fake_vcp_emit_state(NULL, 0x80, 7, BT_VCP_STATE_MUTED), "emit gatt err");
	zassert_equal(audio_volume_get(), 120, "volume unchanged on gatt error");
}

/* ── apply behavior ──────────────────────────────────────────────── */

ZTEST(volume, test_volume_zero_zeroes)
{
	int16_t buf[8] = {1000, -1000, INT16_MAX, INT16_MIN, 1, -1, 12345, -12345};

	zassert_ok(audio_volume_init(), "init");
	set_volume(0, 0);

	audio_volume_apply(buf, 8);

	for (int i = 0; i < 8; i++) {
		zassert_equal(buf[i], 0, "zeroed at %d", i);
	}
}

ZTEST(volume, test_mute_zeroes_regardless_of_volume)
{
	int16_t buf[8] = {1000, -1000, INT16_MAX, INT16_MIN, 1, -1, 12345, -12345};

	zassert_ok(audio_volume_init(), "init");
	set_volume(255, 1);

	audio_volume_apply(buf, 8);

	for (int i = 0; i < 8; i++) {
		zassert_equal(buf[i], 0, "muted at %d", i);
	}
}

ZTEST(volume, test_volume_255_bit_exact_unity)
{
	static const int16_t samples[] = {INT16_MIN, INT16_MAX, -1, 0, 1};
	int16_t buf[sizeof(samples) / sizeof(samples[0])];

	zassert_ok(audio_volume_init(), "init");
	set_volume(255, 0);

	memcpy(buf, samples, sizeof(samples));
	audio_volume_apply(buf, 5);

	for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
		zassert_equal(buf[i], samples[i], "unity at %zu", i);
	}
}

ZTEST(volume, test_intermediate_volume_matches_truncating_formula)
{
	static const int16_t samples[] = {INT16_MIN, INT16_MAX, -32767, 32767,  -1, 0, 1,
					  100,       -100,      12345,  -12345, 3,  -3};
	static const uint8_t vols[] = {1, 17, 100, 127, 128, 200, 254};
	int16_t buf[sizeof(samples) / sizeof(samples[0])];

	zassert_ok(audio_volume_init(), "init");
	set_volume(100, 0);

	for (size_t v = 0; v < sizeof(vols) / sizeof(vols[0]); v++) {
		set_volume(vols[v], 0);
		memcpy(buf, samples, sizeof(samples));
		audio_volume_apply(buf, sizeof(samples) / sizeof(samples[0]));

		for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
			zassert_equal(buf[i], trunc_div((int32_t)samples[i] * vols[v], 255),
				      "vol %u sample %zu", vols[v], i);
		}
	}
}

ZTEST(volume, test_zero_samples_no_memory_change)
{
	int16_t buf[8] = {GUARD_VAL, GUARD_VAL, GUARD_VAL, GUARD_VAL,
			  GUARD_VAL, GUARD_VAL, GUARD_VAL, GUARD_VAL};

	zassert_ok(audio_volume_init(), "init");
	set_volume(17, 0);

	audio_volume_apply(buf, 0);

	for (int i = 0; i < 8; i++) {
		zassert_equal(buf[i], GUARD_VAL, "untouched at %d", i);
	}
}

ZTEST(volume, test_null_and_zero_safe_all_states)
{
	/* NULL with zero and nonzero samples, and zero samples with a real
	 * buffer, must be deterministic no-ops under every production state:
	 * muted, volume zero, unity, and an intermediate scale.
	 */
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	const struct {
		uint8_t vol;
		uint8_t mute;
	} states[] = {
		{255, 1}, /* muted */
		{0, 0},   /* volume zero */
		{255, 0}, /* unity */
		{100, 0}, /* intermediate */
	};
	int16_t buf[8] = {GUARD_VAL, GUARD_VAL, GUARD_VAL, GUARD_VAL,
			  GUARD_VAL, GUARD_VAL, GUARD_VAL, GUARD_VAL};
	uint32_t count_before;

	zassert_ok(audio_volume_init(), "init");

	for (size_t s = 0; s < sizeof(states) / sizeof(states[0]); s++) {
		set_volume(states[s].vol, states[s].mute);

		audio_perf_snapshot(paths, NULL);
		count_before = paths[AUDIO_PERF_PATH_VOLUME].count;

		audio_volume_apply(NULL, 0);
		audio_volume_apply(NULL, 16);
		audio_volume_apply(buf, 0);

		/* One balanced performance sample per call, including the
		 * no-data exits.
		 */
		audio_perf_snapshot(paths, NULL);
		zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, count_before + 3,
			      "balanced no-data exits (state %zu)", s);

		/* State unchanged and real buffer untouched. */
		zassert_equal(audio_volume_get(), states[s].vol, "volume state %zu", s);
		zassert_equal(audio_volume_is_muted(), states[s].mute != 0, "mute state %zu", s);
		for (int i = 0; i < 8; i++) {
			zassert_equal(buf[i], GUARD_VAL, "buffer untouched at %d (state %zu)", i,
				      s);
		}
	}
}

/* ── perf hook balance (real audio_perf.c) ───────────────────────── */

ZTEST(volume, test_perf_hook_balanced_all_exits)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	int16_t buf[64];

	zassert_ok(audio_volume_init(), "init");

	/* mute exit */
	set_volume(255, 1);
	memset(buf, 0x55, sizeof(buf));
	audio_volume_apply(buf, 64);
	audio_perf_snapshot(paths, NULL);
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 1, "mute exit balanced");

	/* zero exit */
	set_volume(0, 0);
	memset(buf, 0x55, sizeof(buf));
	audio_volume_apply(buf, 64);
	audio_perf_snapshot(paths, NULL);
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 2, "zero exit balanced");

	/* unity exit */
	set_volume(255, 0);
	memset(buf, 0x55, sizeof(buf));
	audio_volume_apply(buf, 64);
	audio_perf_snapshot(paths, NULL);
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 3, "unity exit balanced");

	/* scale exit */
	set_volume(100, 0);
	memset(buf, 0x55, sizeof(buf));
	audio_volume_apply(buf, 64);
	audio_perf_snapshot(paths, NULL);
	zassert_equal(paths[AUDIO_PERF_PATH_VOLUME].count, 4, "scale exit balanced");
}

/* ── concurrent callback toggling ────────────────────────────────── */

#define TOGGLER_ITERS 5000
#define APPLIER_ITERS 2000
#define APPLY_SAMPLES 128

K_THREAD_STACK_DEFINE(toggler_stack, 1024);
static struct k_thread toggler_thread;
static volatile bool toggler_done;

static void toggler_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (int i = 0; i < TOGGLER_ITERS; i++) {
		if ((i & 1) == 0) {
			(void)fake_vcp_emit_state(NULL, 0, 255, 0);
		} else {
			(void)fake_vcp_emit_state(NULL, 0, 0, 1);
		}
		k_yield();
	}
	toggler_done = true;
}

ZTEST(volume, test_concurrent_callback_toggle_atomic_snapshot)
{
	int16_t buf[APPLY_SAMPLES];

	zassert_ok(audio_volume_init(), "init");

	/* Anchor the initial state to one of the two toggled packed states
	 * so every apply sees either unity-scaled or zeroed output.
	 */
	set_volume(0, 1);

	toggler_done = false;
	k_thread_create(&toggler_thread, toggler_stack, K_THREAD_STACK_SIZEOF(toggler_stack),
			toggler_fn, NULL, NULL, NULL, 0, K_PREEMPT_THREAD, K_NO_WAIT);

	for (int i = 0; i < APPLIER_ITERS; i++) {
		for (int s = 0; s < APPLY_SAMPLES; s++) {
			buf[s] = 1234;
		}
		audio_volume_apply(buf, APPLY_SAMPLES);

		/* One atomic snapshot per buffer: either unity-scaled (1234)
		 * or zeroed (0), never a mix of both.
		 */
		int16_t first = buf[0];

		zassert_true(first == 0 || first == 1234, "pure snapshot at iter %d", i);
		for (int s = 1; s < APPLY_SAMPLES; s++) {
			zassert_equal(buf[s], first, "uniform at iter %d", i);
		}
	}

	k_thread_join(&toggler_thread, K_FOREVER);
	zassert_true(toggler_done, "toggler finished");
}
