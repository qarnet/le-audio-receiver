/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-only access to internal audio_offload symbols.
 */

#ifndef AUDIO_OFFLOAD_TEST_HELPERS_H
#define AUDIO_OFFLOAD_TEST_HELPERS_H

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Work functions — made non-static so tests can call directly. */
void prep_work_fn(struct k_work *work);
void recovery_work_fn(struct k_work *work);

/* Work items — exposed so tests can cancel pending work. */
extern struct k_work_q g_offload_wq;
extern struct k_work_delayable g_prep_work;
extern struct k_work_delayable g_recovery_work;

/* Submit mutex — exposed for busy/timeout tests. */
extern struct k_mutex g_submit_lock;

/* ── R5 deterministic stage hooks (CONFIG_ZTEST-scoped in production) ──
 * Enum must match the production definition in src/audio_offload.c. */
enum asrc_test_stage {
	ASRC_TEST_STAGE_MUTEX_ACQUIRED,
	ASRC_TEST_STAGE_MUTEX_TIMEOUT,
	ASRC_TEST_STAGE_BEFORE_COMMIT,
};

extern void (*asrc_test_stage_hook)(enum asrc_test_stage stage, void *user_data);
extern void *asrc_test_stage_user_data;

/* Single-recovery-schedule proof (GCOVR-excluded production accessor). */
bool audio_offload_test_is_recovery_scheduled(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_OFFLOAD_TEST_HELPERS_H */
