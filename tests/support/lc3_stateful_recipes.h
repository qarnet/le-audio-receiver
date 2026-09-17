/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-only LC3 decoder-history recipes shared by calibration tools.
 */

#ifndef LE_AUDIO_RECEIVER_LC3_STATEFUL_RECIPES_H_
#define LE_AUDIO_RECEIVER_LC3_STATEFUL_RECIPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum lc3_stateful_action {
	LC3_STATEFUL_ACTION_PLC = 0,
	LC3_STATEFUL_ACTION_CORPUS = 1,
};

enum lc3_stateful_reference_kind {
	LC3_STATEFUL_REFERENCE_PORTABLE_PCM = 0,
	LC3_STATEFUL_REFERENCE_GENERATED_PCM = 1,
};

struct lc3_stateful_step {
	enum lc3_stateful_action action;
	uint16_t first_sequence;
	uint16_t count;
};

struct lc3_stateful_recipe {
	const char *id;
	const char *source_stem;
	const char *reference_path;
	enum lc3_stateful_reference_kind reference_kind;
	uint16_t reference_first_frame;
	uint32_t duration_us;
	uint16_t frame_bytes;
	uint16_t samples_per_frame;
	uint16_t output_action_count;
	uint16_t valid_frame_count;
	const struct lc3_stateful_step *steps;
	size_t step_count;
};

extern const struct lc3_stateful_recipe lc3_stateful_recipes[];
extern const size_t lc3_stateful_recipe_count;

/* Validates source geometry and replay accounting without decoder dependencies. */
bool lc3_stateful_recipes_validate(const struct lc3_stateful_recipe *recipes, size_t recipe_count);

#endif /* LE_AUDIO_RECEIVER_LC3_STATEFUL_RECIPES_H_ */
