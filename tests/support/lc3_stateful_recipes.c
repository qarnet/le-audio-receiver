/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lc3_stateful_recipes.h"

#define LC3_STATEFUL_CORPUS_FRAMES 128U

struct lc3_stateful_source_geometry {
	const char *stem;
	const char *portable_pcm_path;
	uint32_t duration_us;
	uint16_t frame_bytes;
	uint16_t samples_per_frame;
};

static const struct lc3_stateful_source_geometry source_geometries[] = {
	{
		.stem = "bsim_48k_10ms_120b_l",
		.portable_pcm_path = "bsim_48k_10ms_120b_l.pcm",
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
	},
	{
		.stem = "bsim_48k_10ms_120b_r",
		.portable_pcm_path = "bsim_48k_10ms_120b_r.pcm",
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
	},
	{
		.stem = "bsim_48k_7p5ms_90b_l",
		.portable_pcm_path = "bsim_48k_7p5ms_90b_l.pcm",
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
	},
	{
		.stem = "bsim_48k_7p5ms_90b_r",
		.portable_pcm_path = "bsim_48k_7p5ms_90b_r.pcm",
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
	},
};

static const struct lc3_stateful_step start8_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 8U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 100U},
};

static const struct lc3_stateful_step start7_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 7U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 100U},
};

static const struct lc3_stateful_step start11_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 11U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 100U},
};

static const struct lc3_stateful_step modea_start_7p5ms_l_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 12U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 101U},
};

static const struct lc3_stateful_step modea_start_7p5ms_r_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 10U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 1U},
	{LC3_STATEFUL_ACTION_PLC, 0U, 2U},
	{LC3_STATEFUL_ACTION_CORPUS, 1U, 100U},
};

static const struct lc3_stateful_step skip20_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 8U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 20U},
	{LC3_STATEFUL_ACTION_CORPUS, 21U, 80U},
};

static const struct lc3_stateful_step loss48x18_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 8U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 48U},
	{LC3_STATEFUL_ACTION_PLC, 0U, 18U},
	{LC3_STATEFUL_ACTION_CORPUS, 48U, 34U},
};

const struct lc3_stateful_recipe lc3_stateful_recipes[] = {
	{
		.id = "start8_10ms_l",
		.source_stem = "bsim_48k_10ms_120b_l",
		.reference_path = "bsim_48k_10ms_120b_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 108U,
		.valid_frame_count = 100U,
		.steps = start8_steps,
		.step_count = sizeof(start8_steps) / sizeof(start8_steps[0]),
	},
	{
		.id = "start8_10ms_r",
		.source_stem = "bsim_48k_10ms_120b_r",
		.reference_path = "bsim_48k_10ms_120b_r.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 108U,
		.valid_frame_count = 100U,
		.steps = start8_steps,
		.step_count = sizeof(start8_steps) / sizeof(start8_steps[0]),
	},
	{
		.id = "start11_7p5ms_l",
		.source_stem = "bsim_48k_7p5ms_90b_l",
		.reference_path = "bsim_48k_7p5ms_90b_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
		.output_action_count = 111U,
		.valid_frame_count = 100U,
		.steps = start11_steps,
		.step_count = sizeof(start11_steps) / sizeof(start11_steps[0]),
	},
	{
		.id = "start11_7p5ms_r",
		.source_stem = "bsim_48k_7p5ms_90b_r",
		.reference_path = "bsim_48k_7p5ms_90b_r.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
		.output_action_count = 111U,
		.valid_frame_count = 100U,
		.steps = start11_steps,
		.step_count = sizeof(start11_steps) / sizeof(start11_steps[0]),
	},
	{
		.id = "modea_start_7p5ms_l",
		.source_stem = "bsim_48k_7p5ms_90b_l",
		.reference_path = "bsim_48k_7p5ms_90b_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
		.output_action_count = 113U,
		.valid_frame_count = 101U,
		.steps = modea_start_7p5ms_l_steps,
		.step_count =
			sizeof(modea_start_7p5ms_l_steps) / sizeof(modea_start_7p5ms_l_steps[0]),
	},
	{
		.id = "modea_start_7p5ms_r",
		.source_stem = "bsim_48k_7p5ms_90b_r",
		.reference_path = "stateful_48k_7p5ms_modea_start_r.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_GENERATED_PCM,
		.reference_first_frame = 0U,
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
		.output_action_count = 113U,
		.valid_frame_count = 101U,
		.steps = modea_start_7p5ms_r_steps,
		.step_count =
			sizeof(modea_start_7p5ms_r_steps) / sizeof(modea_start_7p5ms_r_steps[0]),
	},
	{
		.id = "skip20_10ms_l",
		.source_stem = "bsim_48k_10ms_120b_l",
		.reference_path = "stateful_48k_10ms_skip20_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_GENERATED_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 108U,
		.valid_frame_count = 100U,
		.steps = skip20_steps,
		.step_count = sizeof(skip20_steps) / sizeof(skip20_steps[0]),
	},
	{
		.id = "loss48x18_10ms_r",
		.source_stem = "bsim_48k_10ms_120b_r",
		.reference_path = "stateful_48k_10ms_loss48x18_r.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_GENERATED_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 108U,
		.valid_frame_count = 82U,
		.steps = loss48x18_steps,
		.step_count = sizeof(loss48x18_steps) / sizeof(loss48x18_steps[0]),
	},
	{
		.id = "start7_10ms_l",
		.source_stem = "bsim_48k_10ms_120b_l",
		.reference_path = "bsim_48k_10ms_120b_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 107U,
		.valid_frame_count = 100U,
		.steps = start7_steps,
		.step_count = sizeof(start7_steps) / sizeof(start7_steps[0]),
	},
};

const size_t lc3_stateful_recipe_count =
	sizeof(lc3_stateful_recipes) / sizeof(lc3_stateful_recipes[0]);

struct lc3_stateful_expected_recipe {
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

static const struct lc3_stateful_step expected_start8_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 8U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 100U},
};

static const struct lc3_stateful_step expected_start7_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 7U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 100U},
};

static const struct lc3_stateful_step expected_start11_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 11U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 100U},
};

static const struct lc3_stateful_step expected_modea_start_7p5ms_l_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 12U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 101U},
};

static const struct lc3_stateful_step expected_modea_start_7p5ms_r_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 10U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 1U},
	{LC3_STATEFUL_ACTION_PLC, 0U, 2U},
	{LC3_STATEFUL_ACTION_CORPUS, 1U, 100U},
};

static const struct lc3_stateful_step expected_skip20_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 8U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 20U},
	{LC3_STATEFUL_ACTION_CORPUS, 21U, 80U},
};

static const struct lc3_stateful_step expected_loss48x18_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 8U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 48U},
	{LC3_STATEFUL_ACTION_PLC, 0U, 18U},
	{LC3_STATEFUL_ACTION_CORPUS, 48U, 34U},
};

static const struct lc3_stateful_expected_recipe expected_recipes[] = {
	{
		.id = "start8_10ms_l",
		.source_stem = "bsim_48k_10ms_120b_l",
		.reference_path = "bsim_48k_10ms_120b_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 108U,
		.valid_frame_count = 100U,
		.steps = expected_start8_steps,
		.step_count = sizeof(expected_start8_steps) / sizeof(expected_start8_steps[0]),
	},
	{
		.id = "start8_10ms_r",
		.source_stem = "bsim_48k_10ms_120b_r",
		.reference_path = "bsim_48k_10ms_120b_r.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 108U,
		.valid_frame_count = 100U,
		.steps = expected_start8_steps,
		.step_count = sizeof(expected_start8_steps) / sizeof(expected_start8_steps[0]),
	},
	{
		.id = "start11_7p5ms_l",
		.source_stem = "bsim_48k_7p5ms_90b_l",
		.reference_path = "bsim_48k_7p5ms_90b_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
		.output_action_count = 111U,
		.valid_frame_count = 100U,
		.steps = expected_start11_steps,
		.step_count = sizeof(expected_start11_steps) / sizeof(expected_start11_steps[0]),
	},
	{
		.id = "start11_7p5ms_r",
		.source_stem = "bsim_48k_7p5ms_90b_r",
		.reference_path = "bsim_48k_7p5ms_90b_r.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
		.output_action_count = 111U,
		.valid_frame_count = 100U,
		.steps = expected_start11_steps,
		.step_count = sizeof(expected_start11_steps) / sizeof(expected_start11_steps[0]),
	},
	{
		.id = "modea_start_7p5ms_l",
		.source_stem = "bsim_48k_7p5ms_90b_l",
		.reference_path = "bsim_48k_7p5ms_90b_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
		.output_action_count = 113U,
		.valid_frame_count = 101U,
		.steps = expected_modea_start_7p5ms_l_steps,
		.step_count = sizeof(expected_modea_start_7p5ms_l_steps) /
			      sizeof(expected_modea_start_7p5ms_l_steps[0]),
	},
	{
		.id = "modea_start_7p5ms_r",
		.source_stem = "bsim_48k_7p5ms_90b_r",
		.reference_path = "stateful_48k_7p5ms_modea_start_r.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_GENERATED_PCM,
		.reference_first_frame = 0U,
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
		.output_action_count = 113U,
		.valid_frame_count = 101U,
		.steps = expected_modea_start_7p5ms_r_steps,
		.step_count = sizeof(expected_modea_start_7p5ms_r_steps) /
			      sizeof(expected_modea_start_7p5ms_r_steps[0]),
	},
	{
		.id = "skip20_10ms_l",
		.source_stem = "bsim_48k_10ms_120b_l",
		.reference_path = "stateful_48k_10ms_skip20_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_GENERATED_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 108U,
		.valid_frame_count = 100U,
		.steps = expected_skip20_steps,
		.step_count = sizeof(expected_skip20_steps) / sizeof(expected_skip20_steps[0]),
	},
	{
		.id = "loss48x18_10ms_r",
		.source_stem = "bsim_48k_10ms_120b_r",
		.reference_path = "stateful_48k_10ms_loss48x18_r.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_GENERATED_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 108U,
		.valid_frame_count = 82U,
		.steps = expected_loss48x18_steps,
		.step_count =
			sizeof(expected_loss48x18_steps) / sizeof(expected_loss48x18_steps[0]),
	},
	{
		.id = "start7_10ms_l",
		.source_stem = "bsim_48k_10ms_120b_l",
		.reference_path = "bsim_48k_10ms_120b_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 107U,
		.valid_frame_count = 100U,
		.steps = expected_start7_steps,
		.step_count = sizeof(expected_start7_steps) / sizeof(expected_start7_steps[0]),
	},
};

static bool string_equal(const char *left, const char *right)
{
	if (left == NULL || right == NULL) {
		return false;
	}
	while (*left != '\0' && *right != '\0') {
		if (*left != *right) {
			return false;
		}
		left++;
		right++;
	}

	return *left == *right;
}

static bool nonempty_safe_name(const char *value)
{
	const char *cursor = value;

	if (cursor == NULL || *cursor == '\0') {
		return false;
	}
	while (*cursor != '\0') {
		if (*cursor == '/' || *cursor == '\\') {
			return false;
		}
		cursor++;
	}

	return true;
}

static const struct lc3_stateful_source_geometry *source_geometry_for(const char *stem)
{
	for (size_t index = 0U; index < sizeof(source_geometries) / sizeof(source_geometries[0]);
	     index++) {
		if (string_equal(stem, source_geometries[index].stem)) {
			return &source_geometries[index];
		}
	}

	return NULL;
}

static const struct lc3_stateful_source_geometry *
source_geometry_for_portable_pcm(const char *portable_pcm_path)
{
	for (size_t index = 0U; index < sizeof(source_geometries) / sizeof(source_geometries[0]);
	     index++) {
		if (string_equal(portable_pcm_path, source_geometries[index].portable_pcm_path)) {
			return &source_geometries[index];
		}
	}

	return NULL;
}

static bool steps_equal(const struct lc3_stateful_step *actual,
			const struct lc3_stateful_step *expected, size_t step_count)
{
	if (actual == NULL || expected == NULL) {
		return false;
	}
	for (size_t index = 0U; index < step_count; index++) {
		if (actual[index].action != expected[index].action ||
		    actual[index].first_sequence != expected[index].first_sequence ||
		    actual[index].count != expected[index].count) {
			return false;
		}
	}

	return true;
}

static bool canonical_recipe_table_is_exact(void)
{
	if (lc3_stateful_recipe_count != sizeof(expected_recipes) / sizeof(expected_recipes[0])) {
		return false;
	}

	for (size_t index = 0U; index < lc3_stateful_recipe_count; index++) {
		const struct lc3_stateful_recipe *actual = &lc3_stateful_recipes[index];
		const struct lc3_stateful_expected_recipe *expected = &expected_recipes[index];

		if (!string_equal(actual->id, expected->id) ||
		    !string_equal(actual->source_stem, expected->source_stem) ||
		    !string_equal(actual->reference_path, expected->reference_path) ||
		    actual->reference_kind != expected->reference_kind ||
		    actual->reference_first_frame != expected->reference_first_frame ||
		    actual->duration_us != expected->duration_us ||
		    actual->frame_bytes != expected->frame_bytes ||
		    actual->samples_per_frame != expected->samples_per_frame ||
		    actual->output_action_count != expected->output_action_count ||
		    actual->valid_frame_count != expected->valid_frame_count ||
		    actual->step_count != expected->step_count ||
		    !steps_equal(actual->steps, expected->steps, expected->step_count)) {
			return false;
		}
	}

	return true;
}

static bool recipe_is_valid(const struct lc3_stateful_recipe *recipe)
{
	const struct lc3_stateful_source_geometry *source;
	const struct lc3_stateful_source_geometry *reference_source;
	uint32_t output_actions = 0U;
	uint32_t valid_frames = 0U;

	if (recipe == NULL || !nonempty_safe_name(recipe->id) ||
	    !nonempty_safe_name(recipe->source_stem) ||
	    !nonempty_safe_name(recipe->reference_path) || recipe->steps == NULL ||
	    recipe->step_count == 0U) {
		return false;
	}

	source = source_geometry_for(recipe->source_stem);
	if (source == NULL || recipe->duration_us != source->duration_us ||
	    recipe->frame_bytes != source->frame_bytes ||
	    recipe->samples_per_frame != source->samples_per_frame) {
		return false;
	}
	if (recipe->reference_kind == LC3_STATEFUL_REFERENCE_PORTABLE_PCM) {
		reference_source = source_geometry_for_portable_pcm(recipe->reference_path);
		if (reference_source == NULL ||
		    reference_source->duration_us != source->duration_us ||
		    reference_source->frame_bytes != source->frame_bytes ||
		    reference_source->samples_per_frame != source->samples_per_frame ||
		    recipe->reference_first_frame >= LC3_STATEFUL_CORPUS_FRAMES ||
		    recipe->valid_frame_count >
			    LC3_STATEFUL_CORPUS_FRAMES - recipe->reference_first_frame) {
			return false;
		}
	} else if (recipe->reference_kind == LC3_STATEFUL_REFERENCE_GENERATED_PCM) {
		if (recipe->reference_first_frame != 0U) {
			return false;
		}
	} else {
		return false;
	}

	for (size_t index = 0U; index < recipe->step_count; index++) {
		const struct lc3_stateful_step *step = &recipe->steps[index];

		if (step->count == 0U ||
		    output_actions > (uint32_t)UINT16_MAX - (uint32_t)step->count) {
			return false;
		}
		output_actions += step->count;

		switch (step->action) {
		case LC3_STATEFUL_ACTION_PLC:
			if (step->first_sequence != 0U) {
				return false;
			}
			break;
		case LC3_STATEFUL_ACTION_CORPUS:
			if (step->first_sequence >= LC3_STATEFUL_CORPUS_FRAMES ||
			    step->count > LC3_STATEFUL_CORPUS_FRAMES - step->first_sequence ||
			    valid_frames > (uint32_t)UINT16_MAX - (uint32_t)step->count) {
				return false;
			}
			valid_frames += step->count;
			break;
		default:
			return false;
		}
	}

	return output_actions == recipe->output_action_count &&
	       valid_frames == recipe->valid_frame_count;
}

bool lc3_stateful_recipes_validate(const struct lc3_stateful_recipe *recipes, size_t recipe_count)
{
	if (recipes == NULL || recipe_count == 0U) {
		return false;
	}

	for (size_t index = 0U; index < recipe_count; index++) {
		if (!recipe_is_valid(&recipes[index])) {
			return false;
		}
		for (size_t prior = 0U; prior < index; prior++) {
			if (string_equal(recipes[index].id, recipes[prior].id)) {
				return false;
			}
		}
	}

	if (recipes == lc3_stateful_recipes && recipe_count == lc3_stateful_recipe_count &&
	    !canonical_recipe_table_is_exact()) {
		return false;
	}

	return true;
}
