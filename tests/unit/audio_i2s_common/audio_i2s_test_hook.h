/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned hook header for the I2S sink state-machine suites
 * (tests/unit/audio_i2s and tests/unit/audio_i2s_identity).
 *
 * src/audio_i2s.c includes this file ONLY when the test-only compile
 * definition AUDIO_I2S_NATIVE_TEST is present (set by test CMakeLists).
 * Production firmware never defines the macro, so production images
 * contain none of these symbols and no test branch.
 *
 * Implementations:
 *   - device readiness and repeat-fallback slab allocation failure are
 *     test-controlled (test-side state);
 *   - module-state reset/snapshots and the slab accessor read the
 *     production module-static state directly (defined in audio_i2s.c
 *     under the same guard).
 */

#ifndef AUDIO_I2S_TEST_HOOK_H
#define AUDIO_I2S_TEST_HOOK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>

/* ── Test-controlled behavior ────────────────────────────────────── */

/** Controllable replacement for device_is_ready() in test builds. */
bool audio_i2s_test_device_is_ready(const struct device *dev);

/** Set the result of audio_i2s_test_device_is_ready(). */
void audio_i2s_test_set_device_ready(bool ready);

/** true → repeat-fallback slab allocation reports failure. */
bool audio_i2s_test_inject_slab_alloc_failure(void);

/** Set the repeat-fallback slab allocation failure injection. */
void audio_i2s_test_set_slab_alloc_failure(bool fail);

/* ── Module-static state reset / snapshots ───────────────────────── */

/**
 * Reset module-static state between tests.  Call AFTER driver-owned
 * blocks have been purged (fake_i2s_reset()/fake_i2s_release_all()).
 */
void audio_i2s_test_reset_module_state(void);

bool audio_i2s_test_is_configured(void);
bool audio_i2s_test_is_started(void);
uint16_t audio_i2s_test_input_frames(void);
size_t audio_i2s_test_saved_frame_len(void);

/* R1 admission/drain snapshots (lock-protected; concurrency tests). */
bool audio_i2s_test_is_accepting(void);
uint32_t audio_i2s_test_active_pushes(void);
uint32_t audio_i2s_test_stop_callers(void);
bool audio_i2s_test_stop_finalizing(void);
uint32_t audio_i2s_test_stop_waiters(void);

/* ASRC variants only (compile-time guarded in audio_i2s.c). */
uint32_t audio_i2s_test_offload_sequence(void);
int16_t audio_i2s_test_asrc_prev_l(void);
int16_t audio_i2s_test_asrc_prev_r(void);
bool audio_i2s_test_asrc_prev_valid(void);

/* Internal slab: pre-exhaustion / exact free-count inspection. */
struct k_mem_slab *audio_i2s_test_get_slab(void);

#endif /* AUDIO_I2S_TEST_HOOK_H */
