/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared mock state type for flpr_runtime unit tests.
 * Tracks VPR register writes and call counts for verification.
 */
#ifndef MOCK_STATE_H_
#define MOCK_STATE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOCK_MAX_DMCONTROL_WRITES 8

typedef struct {
	uint32_t dmcontrol_writes[MOCK_MAX_DMCONTROL_WRITES];
	uint32_t dmcontrol_write_count;
	uint32_t dmcontrol_set_count;
	uint32_t dmcontrol_get_count;
	bool cpurun;
	uint32_t cpurun_set_count;
	uint32_t cpurun_get_count;
	uint32_t initpc;
	uint32_t initpc_set_count;
	uint32_t initpc_get_count;
} mock_vpr_state_t;

extern mock_vpr_state_t mock_vpr;

void mock_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_STATE_H_ */
