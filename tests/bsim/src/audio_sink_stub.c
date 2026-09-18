/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM audio sink stub, scenario-aware strict PCM oracle.
 *
 * Keeps public audio_sink.h API exact. Records streamed segments, startup
 * transients, final audio_stats, malformed sample counts, pushes after stop,
 * payload-identified stateful numerical PCM metrics, routing evidence, energy
 * bounds, and configured sample count.
 */

#include "audio_sink.h"
#include "audio_stats.h"
#include "audio_volume.h"
#include "bsim_observer.h"
#include "bsim_pcm_limits.h"
#include "bsim_sink_oracle.h"
#include "bsim_test_helpers.h"
#include "lc3_stateful_recipes.h"
#include "pcm_oracle.h"

#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/pacs.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BSIM_PCM_CORPUS_FRAMES      128U
#define BSIM_PCM_10MS_SAMPLES       480U
#define BSIM_PCM_7P5MS_SAMPLES      360U
#define BSIM_PCM_VOLUME             195U
#define BSIM_PCM_MAX_SAMPLES_PER_CH BSIM_PCM_10MS_SAMPLES
#define BSIM_RECIPE_BINDING_MAX     9U
#define NORMAL_GOAL_PUSHES          100U

static const uint8_t bsim_48k_10ms_120b_l_lc3[] = {
#include "bsim_48k_10ms_120b_l_lc3.inc"
};

static const uint8_t bsim_48k_10ms_120b_r_lc3[] = {
#include "bsim_48k_10ms_120b_r_lc3.inc"
};

static const uint8_t bsim_48k_7p5ms_90b_l_lc3[] = {
#include "bsim_48k_7p5ms_90b_l_lc3.inc"
};

static const uint8_t bsim_48k_7p5ms_90b_r_lc3[] = {
#include "bsim_48k_7p5ms_90b_r_lc3.inc"
};

static const uint8_t bsim_48k_10ms_120b_l_pcm[] = {
#include "bsim_48k_10ms_120b_l_pcm.inc"
};

static const uint8_t bsim_48k_10ms_120b_r_pcm[] = {
#include "bsim_48k_10ms_120b_r_pcm.inc"
};

static const uint8_t bsim_48k_7p5ms_90b_l_pcm[] = {
#include "bsim_48k_7p5ms_90b_l_pcm.inc"
};

static const uint8_t bsim_48k_7p5ms_90b_r_pcm[] = {
#include "bsim_48k_7p5ms_90b_r_pcm.inc"
};

static const uint8_t stateful_48k_10ms_skip20_l_pcm[] = {
#include "stateful_48k_10ms_skip20_l_pcm.inc"
};

static const uint8_t stateful_48k_10ms_loss48x18_r_pcm[] = {
#include "stateful_48k_10ms_loss48x18_r_pcm.inc"
};

static const uint8_t stateful_48k_7p5ms_modea_start_r_pcm[] = {
#include "stateful_48k_7p5ms_modea_start_r_pcm.inc"
};

BUILD_ASSERT(sizeof(bsim_48k_10ms_120b_l_lc3) == BSIM_PCM_CORPUS_FRAMES * 120U,
	     "10 ms left LC3 corpus geometry");
BUILD_ASSERT(sizeof(bsim_48k_10ms_120b_r_lc3) == BSIM_PCM_CORPUS_FRAMES * 120U,
	     "10 ms right LC3 corpus geometry");
BUILD_ASSERT(sizeof(bsim_48k_7p5ms_90b_l_lc3) == BSIM_PCM_CORPUS_FRAMES * 90U,
	     "7.5 ms left LC3 corpus geometry");
BUILD_ASSERT(sizeof(bsim_48k_7p5ms_90b_r_lc3) == BSIM_PCM_CORPUS_FRAMES * 90U,
	     "7.5 ms right LC3 corpus geometry");
BUILD_ASSERT(sizeof(bsim_48k_10ms_120b_l_pcm) ==
		     BSIM_PCM_CORPUS_FRAMES * BSIM_PCM_10MS_SAMPLES * sizeof(int16_t),
	     "10 ms left PCM corpus geometry");
BUILD_ASSERT(sizeof(bsim_48k_10ms_120b_r_pcm) ==
		     BSIM_PCM_CORPUS_FRAMES * BSIM_PCM_10MS_SAMPLES * sizeof(int16_t),
	     "10 ms right PCM corpus geometry");
BUILD_ASSERT(sizeof(bsim_48k_7p5ms_90b_l_pcm) ==
		     BSIM_PCM_CORPUS_FRAMES * BSIM_PCM_7P5MS_SAMPLES * sizeof(int16_t),
	     "7.5 ms left PCM corpus geometry");
BUILD_ASSERT(sizeof(bsim_48k_7p5ms_90b_r_pcm) ==
		     BSIM_PCM_CORPUS_FRAMES * BSIM_PCM_7P5MS_SAMPLES * sizeof(int16_t),
	     "7.5 ms right PCM corpus geometry");
BUILD_ASSERT(sizeof(stateful_48k_10ms_skip20_l_pcm) ==
		     100U * BSIM_PCM_10MS_SAMPLES * sizeof(int16_t),
	     "skip20 stateful PCM geometry");
BUILD_ASSERT(sizeof(stateful_48k_10ms_loss48x18_r_pcm) ==
		     82U * BSIM_PCM_10MS_SAMPLES * sizeof(int16_t),
	     "loss stateful PCM geometry");
BUILD_ASSERT(sizeof(stateful_48k_7p5ms_modea_start_r_pcm) ==
		     101U * BSIM_PCM_7P5MS_SAMPLES * sizeof(int16_t),
	     "Mode A 7.5 ms right stateful PCM geometry");

struct bsim_pcm_source {
	const char *stem;
	const uint8_t *lc3;
	size_t lc3_size;
	const uint8_t *portable_pcm;
	size_t portable_pcm_size;
	uint32_t duration_us;
	uint16_t frame_bytes;
	uint16_t samples_per_frame;
};

struct bsim_pcm_reference {
	const char *path;
	enum lc3_stateful_reference_kind kind;
	const uint8_t *pcm;
	size_t pcm_size;
	uint16_t frame_count;
	uint16_t samples_per_frame;
};

struct bsim_recipe_binding {
	const struct lc3_stateful_recipe *recipe;
	const struct bsim_pcm_source *source;
	const struct bsim_pcm_reference *reference;
};

enum bsim_recipe_completion {
	BSIM_RECIPE_COMPLETION_FULL,
	BSIM_RECIPE_COMPLETION_PREFIX,
};

struct bsim_scenario_recipe {
	enum bsim_sink_scenario scenario;
	uint8_t segment;
	const char *left_recipe_id;
	const char *right_recipe_id;
	enum bsim_recipe_completion completion;
};

struct bsim_recipe_cursor {
	const struct bsim_recipe_binding *binding;
	size_t step_index;
	uint16_t step_offset;
	uint32_t actions;
	uint32_t valid_actions;
	uint32_t plc_actions;
};

struct bsim_prepared_channel {
	enum lc3_stateful_action action;
	const uint8_t *reference;
	bool source_valid;
};

static const struct bsim_pcm_source sources[] = {
	{
		.stem = "bsim_48k_10ms_120b_l",
		.lc3 = bsim_48k_10ms_120b_l_lc3,
		.lc3_size = sizeof(bsim_48k_10ms_120b_l_lc3),
		.portable_pcm = bsim_48k_10ms_120b_l_pcm,
		.portable_pcm_size = sizeof(bsim_48k_10ms_120b_l_pcm),
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = BSIM_PCM_10MS_SAMPLES,
	},
	{
		.stem = "bsim_48k_10ms_120b_r",
		.lc3 = bsim_48k_10ms_120b_r_lc3,
		.lc3_size = sizeof(bsim_48k_10ms_120b_r_lc3),
		.portable_pcm = bsim_48k_10ms_120b_r_pcm,
		.portable_pcm_size = sizeof(bsim_48k_10ms_120b_r_pcm),
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = BSIM_PCM_10MS_SAMPLES,
	},
	{
		.stem = "bsim_48k_7p5ms_90b_l",
		.lc3 = bsim_48k_7p5ms_90b_l_lc3,
		.lc3_size = sizeof(bsim_48k_7p5ms_90b_l_lc3),
		.portable_pcm = bsim_48k_7p5ms_90b_l_pcm,
		.portable_pcm_size = sizeof(bsim_48k_7p5ms_90b_l_pcm),
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = BSIM_PCM_7P5MS_SAMPLES,
	},
	{
		.stem = "bsim_48k_7p5ms_90b_r",
		.lc3 = bsim_48k_7p5ms_90b_r_lc3,
		.lc3_size = sizeof(bsim_48k_7p5ms_90b_r_lc3),
		.portable_pcm = bsim_48k_7p5ms_90b_r_pcm,
		.portable_pcm_size = sizeof(bsim_48k_7p5ms_90b_r_pcm),
		.duration_us = 7500U,
		.frame_bytes = 90U,
		.samples_per_frame = BSIM_PCM_7P5MS_SAMPLES,
	},
};

static const struct bsim_pcm_reference references[] = {
	{
		.path = "bsim_48k_10ms_120b_l.pcm",
		.kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.pcm = bsim_48k_10ms_120b_l_pcm,
		.pcm_size = sizeof(bsim_48k_10ms_120b_l_pcm),
		.frame_count = BSIM_PCM_CORPUS_FRAMES,
		.samples_per_frame = BSIM_PCM_10MS_SAMPLES,
	},
	{
		.path = "bsim_48k_10ms_120b_r.pcm",
		.kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.pcm = bsim_48k_10ms_120b_r_pcm,
		.pcm_size = sizeof(bsim_48k_10ms_120b_r_pcm),
		.frame_count = BSIM_PCM_CORPUS_FRAMES,
		.samples_per_frame = BSIM_PCM_10MS_SAMPLES,
	},
	{
		.path = "bsim_48k_7p5ms_90b_l.pcm",
		.kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.pcm = bsim_48k_7p5ms_90b_l_pcm,
		.pcm_size = sizeof(bsim_48k_7p5ms_90b_l_pcm),
		.frame_count = BSIM_PCM_CORPUS_FRAMES,
		.samples_per_frame = BSIM_PCM_7P5MS_SAMPLES,
	},
	{
		.path = "bsim_48k_7p5ms_90b_r.pcm",
		.kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.pcm = bsim_48k_7p5ms_90b_r_pcm,
		.pcm_size = sizeof(bsim_48k_7p5ms_90b_r_pcm),
		.frame_count = BSIM_PCM_CORPUS_FRAMES,
		.samples_per_frame = BSIM_PCM_7P5MS_SAMPLES,
	},
	{
		.path = "stateful_48k_10ms_skip20_l.pcm",
		.kind = LC3_STATEFUL_REFERENCE_GENERATED_PCM,
		.pcm = stateful_48k_10ms_skip20_l_pcm,
		.pcm_size = sizeof(stateful_48k_10ms_skip20_l_pcm),
		.frame_count = 100U,
		.samples_per_frame = BSIM_PCM_10MS_SAMPLES,
	},
	{
		.path = "stateful_48k_10ms_loss48x18_r.pcm",
		.kind = LC3_STATEFUL_REFERENCE_GENERATED_PCM,
		.pcm = stateful_48k_10ms_loss48x18_r_pcm,
		.pcm_size = sizeof(stateful_48k_10ms_loss48x18_r_pcm),
		.frame_count = 82U,
		.samples_per_frame = BSIM_PCM_10MS_SAMPLES,
	},
	{
		.path = "stateful_48k_7p5ms_modea_start_r.pcm",
		.kind = LC3_STATEFUL_REFERENCE_GENERATED_PCM,
		.pcm = stateful_48k_7p5ms_modea_start_r_pcm,
		.pcm_size = sizeof(stateful_48k_7p5ms_modea_start_r_pcm),
		.frame_count = 101U,
		.samples_per_frame = BSIM_PCM_7P5MS_SAMPLES,
	},
};

static const struct bsim_scenario_recipe scenario_recipes[] = {
	{BSIM_SCN_MONO_10MS, 0U, "start8_10ms_l", "start8_10ms_l", BSIM_RECIPE_COMPLETION_FULL},
	{BSIM_SCN_MONO_7P5MS, 0U, "start11_7p5ms_l", "start11_7p5ms_l",
	 BSIM_RECIPE_COMPLETION_FULL},
	{BSIM_SCN_MODEA_10MS, 0U, "start8_10ms_l", "start8_10ms_r", BSIM_RECIPE_COMPLETION_FULL},
	{BSIM_SCN_MODEA_7P5MS, 0U, "modea_start_7p5ms_l", "modea_start_7p5ms_r",
	 BSIM_RECIPE_COMPLETION_FULL},
	{BSIM_SCN_MODEA_REVERSE_START_10MS, 0U, "start8_10ms_l", "start8_10ms_r",
	 BSIM_RECIPE_COMPLETION_FULL},
	{BSIM_SCN_MODEB_10MS, 0U, "start8_10ms_l", "start8_10ms_r", BSIM_RECIPE_COMPLETION_FULL},
	{BSIM_SCN_MODEB_7P5MS, 0U, "start11_7p5ms_l", "start11_7p5ms_r",
	 BSIM_RECIPE_COMPLETION_FULL},
	{BSIM_SCN_INVALID_SDU_RESUME_10MS, 0U, "skip20_10ms_l", "skip20_10ms_l",
	 BSIM_RECIPE_COMPLETION_FULL},
	{BSIM_SCN_MODEA_ONE_CIS_LOSS_10MS, 0U, "start8_10ms_l", "loss48x18_10ms_r",
	 BSIM_RECIPE_COMPLETION_FULL},
	{BSIM_SCN_MODEA_FIRST_STOP_10MS, 0U, "start8_10ms_l", "start8_10ms_r",
	 BSIM_RECIPE_COMPLETION_PREFIX},
	{BSIM_SCN_RELEASE_WITHOUT_DISABLE_10MS, 0U, "start8_10ms_l", "start8_10ms_l",
	 BSIM_RECIPE_COMPLETION_PREFIX},
	{BSIM_SCN_DISCONNECT_STREAMING_10MS, 0U, "start8_10ms_l", "start8_10ms_l",
	 BSIM_RECIPE_COMPLETION_PREFIX},
	{BSIM_SCN_RECONNECT_SECOND_STREAM_10MS, 0U, "start8_10ms_l", "start8_10ms_l",
	 BSIM_RECIPE_COMPLETION_PREFIX},
	{BSIM_SCN_RECONNECT_SECOND_STREAM_10MS, 1U, "start7_10ms_l", "start7_10ms_l",
	 BSIM_RECIPE_COMPLETION_FULL},
	{BSIM_SCN_DUPLICATE_RELEASE_10MS, 0U, "start8_10ms_l", "start8_10ms_l",
	 BSIM_RECIPE_COMPLETION_PREFIX},
};

static enum bsim_sink_scenario scenario;
static int dec_calls_per_push = 1;

static struct bsim_sink_segment segments[BSIM_SINK_MAX_SEGMENTS];
static struct pcm_oracle l_oracles[BSIM_SINK_MAX_SEGMENTS];
static struct pcm_oracle r_oracles[BSIM_SINK_MAX_SEGMENTS];
static struct bsim_recipe_cursor l_cursors[BSIM_SINK_MAX_SEGMENTS];
static struct bsim_recipe_cursor r_cursors[BSIM_SINK_MAX_SEGMENTS];
static struct bsim_recipe_binding recipe_bindings[BSIM_RECIPE_BINDING_MAX];
static size_t recipe_binding_count;
static bool recipe_bindings_ready;
static int segment_count; /* finalized segments */
static int current_seg;   /* open segment index */
static bool stopped;
static bool accepting;       /* push admission, restored only by open */
static bool boundary_closed; /* first nonzero source-valid push */
static bool goal_finalized;
static uint32_t after_stop_total; /* cumulative, never hidden */
static uint32_t concealed_pushes; /* Mode A one-CIS loss post-boundary pushes */

static uint16_t required_samples = BSIM_PCM_10MS_SAMPLES * 2U;
static uint8_t scaled_reference[BSIM_PCM_MAX_SAMPLES_PER_CH * sizeof(int16_t)];

static bool string_equal(const char *left, const char *right)
{
	return left != NULL && right != NULL && strcmp(left, right) == 0;
}

static bool scenario_expects_no_audio(void)
{
	return scenario == BSIM_SCN_UNSUPPORTED_SOURCE_DIRECTION ||
	       scenario == BSIM_SCN_NO_FREE_SINK_SLOT || scenario == BSIM_SCN_INVALID_CODEC_FIELDS;
}

static bool scenario_is_no_audio(enum bsim_sink_scenario scn)
{
	return scn == BSIM_SCN_UNSUPPORTED_SOURCE_DIRECTION || scn == BSIM_SCN_NO_FREE_SINK_SLOT ||
	       scn == BSIM_SCN_INVALID_CODEC_FIELDS;
}

static bool scenario_uses_stereo_reference(void)
{
	switch (scenario) {
	case BSIM_SCN_MODEA_10MS:
	case BSIM_SCN_MODEA_7P5MS:
	case BSIM_SCN_MODEA_REVERSE_START_10MS:
	case BSIM_SCN_MODEB_10MS:
	case BSIM_SCN_MODEB_7P5MS:
	case BSIM_SCN_MODEA_FIRST_STOP_10MS:
	case BSIM_SCN_MODEA_ONE_CIS_LOSS_10MS:
		return true;
	default:
		return false;
	}
}

static bool seg_finalized(int idx)
{
	return idx >= 0 && idx < segment_count;
}

static struct bsim_sink_segment *cur(void)
{
	return &segments[current_seg];
}

static const struct bsim_pcm_source *source_for_stem(const char *stem)
{
	const struct bsim_pcm_source *match = NULL;

	for (size_t index = 0U; index < ARRAY_SIZE(sources); index++) {
		if (!string_equal(stem, sources[index].stem)) {
			continue;
		}
		if (match != NULL) {
			return NULL;
		}
		match = &sources[index];
	}

	return match;
}

static const struct bsim_pcm_reference *reference_for_path(const char *path)
{
	const struct bsim_pcm_reference *match = NULL;

	for (size_t index = 0U; index < ARRAY_SIZE(references); index++) {
		if (!string_equal(path, references[index].path)) {
			continue;
		}
		if (match != NULL) {
			return NULL;
		}
		match = &references[index];
	}

	return match;
}

static const struct bsim_recipe_binding *recipe_binding_for_id(const char *id)
{
	const struct bsim_recipe_binding *match = NULL;

	for (size_t index = 0U; index < recipe_binding_count; index++) {
		if (!string_equal(id, recipe_bindings[index].recipe->id)) {
			continue;
		}
		if (match != NULL) {
			return NULL;
		}
		match = &recipe_bindings[index];
	}

	return match;
}

static const struct bsim_scenario_recipe *scenario_recipe_for(enum bsim_sink_scenario scn,
							      uint8_t segment)
{
	const struct bsim_scenario_recipe *match = NULL;

	for (size_t index = 0U; index < ARRAY_SIZE(scenario_recipes); index++) {
		const struct bsim_scenario_recipe *candidate = &scenario_recipes[index];

		if (candidate->scenario != scn || candidate->segment != segment) {
			continue;
		}
		if (match != NULL) {
			return NULL;
		}
		match = candidate;
	}

	return match;
}

static bool source_binding_is_valid(const struct lc3_stateful_recipe *recipe,
				    const struct bsim_pcm_source *source,
				    const struct bsim_pcm_reference *reference)
{
	if (recipe == NULL || source == NULL || reference == NULL) {
		return false;
	}

	const size_t source_pcm_size =
		(size_t)BSIM_PCM_CORPUS_FRAMES * recipe->samples_per_frame * sizeof(int16_t);
	const size_t source_lc3_size = (size_t)BSIM_PCM_CORPUS_FRAMES * recipe->frame_bytes;
	const size_t reference_size =
		(size_t)reference->frame_count * recipe->samples_per_frame * sizeof(int16_t);

	if (!string_equal(recipe->source_stem, source->stem) ||
	    !string_equal(recipe->reference_path, reference->path) ||
	    recipe->reference_kind != reference->kind ||
	    recipe->duration_us != source->duration_us ||
	    recipe->frame_bytes != source->frame_bytes ||
	    recipe->samples_per_frame != source->samples_per_frame ||
	    recipe->samples_per_frame != reference->samples_per_frame ||
	    source->lc3_size != source_lc3_size || source->portable_pcm_size != source_pcm_size ||
	    reference->pcm_size != reference_size) {
		return false;
	}

	if (recipe->reference_kind == LC3_STATEFUL_REFERENCE_PORTABLE_PCM) {
		return reference->frame_count == BSIM_PCM_CORPUS_FRAMES &&
		       recipe->reference_first_frame <= reference->frame_count &&
		       recipe->valid_frame_count <=
			       reference->frame_count - recipe->reference_first_frame;
	}

	return recipe->reference_kind == LC3_STATEFUL_REFERENCE_GENERATED_PCM &&
	       recipe->reference_first_frame == 0U &&
	       reference->frame_count == recipe->valid_frame_count;
}

static bool scenario_recipe_bindings_are_valid(void)
{
	for (size_t index = 0U; index < ARRAY_SIZE(scenario_recipes); index++) {
		const struct bsim_scenario_recipe *mapping = &scenario_recipes[index];
		const struct bsim_recipe_binding *left =
			recipe_binding_for_id(mapping->left_recipe_id);
		const struct bsim_recipe_binding *right =
			recipe_binding_for_id(mapping->right_recipe_id);

		if (scenario_is_no_audio(mapping->scenario) || left == NULL || right == NULL ||
		    left->recipe->duration_us != right->recipe->duration_us ||
		    left->recipe->frame_bytes != right->recipe->frame_bytes ||
		    left->recipe->samples_per_frame != right->recipe->samples_per_frame ||
		    (mapping->completion != BSIM_RECIPE_COMPLETION_FULL &&
		     mapping->completion != BSIM_RECIPE_COMPLETION_PREFIX)) {
			return false;
		}

		for (size_t prior = 0U; prior < index; prior++) {
			if (mapping->scenario == scenario_recipes[prior].scenario &&
			    mapping->segment == scenario_recipes[prior].segment) {
				return false;
			}
		}
	}

	for (int scn = BSIM_SCN_MONO_10MS; scn <= BSIM_SCN_DUPLICATE_RELEASE_10MS; scn++) {
		const enum bsim_sink_scenario candidate = (enum bsim_sink_scenario)scn;
		const size_t expected =
			scenario_is_no_audio(candidate)
				? 0U
				: (candidate == BSIM_SCN_RECONNECT_SECOND_STREAM_10MS ? 2U : 1U);
		const size_t count = (scenario_recipe_for(candidate, 0U) != NULL ? 1U : 0U) +
				     (scenario_recipe_for(candidate, 1U) != NULL ? 1U : 0U);

		if (count != expected) {
			return false;
		}
	}

	return true;
}

bool audio_sink_test_recipes_validate(void)
{
	recipe_bindings_ready = false;
	recipe_binding_count = 0U;
	memset(recipe_bindings, 0, sizeof(recipe_bindings));

	if (!lc3_stateful_recipes_validate(lc3_stateful_recipes, lc3_stateful_recipe_count) ||
	    lc3_stateful_recipe_count > ARRAY_SIZE(recipe_bindings)) {
		return false;
	}

	for (size_t index = 0U; index < lc3_stateful_recipe_count; index++) {
		const struct lc3_stateful_recipe *recipe = &lc3_stateful_recipes[index];
		const struct bsim_pcm_source *source = source_for_stem(recipe->source_stem);
		const struct bsim_pcm_reference *reference =
			reference_for_path(recipe->reference_path);

		if (!source_binding_is_valid(recipe, source, reference)) {
			return false;
		}
		for (size_t prior = 0U; prior < index; prior++) {
			if (string_equal(recipe->id, recipe_bindings[prior].recipe->id)) {
				return false;
			}
		}
		recipe_bindings[index] = (struct bsim_recipe_binding){
			.recipe = recipe,
			.source = source,
			.reference = reference,
		};
	}

	recipe_binding_count = lc3_stateful_recipe_count;
	recipe_bindings_ready = scenario_recipe_bindings_are_valid();
	return recipe_bindings_ready;
}

static bool cursor_next(const struct bsim_recipe_cursor *cursor, enum lc3_stateful_action *action,
			uint16_t *sequence)
{
	const struct lc3_stateful_recipe *recipe;
	size_t step_index;
	uint16_t step_offset;

	if (cursor == NULL || cursor->binding == NULL || action == NULL || sequence == NULL) {
		return false;
	}

	recipe = cursor->binding->recipe;
	step_index = cursor->step_index;
	step_offset = cursor->step_offset;
	while (step_index < recipe->step_count && step_offset == recipe->steps[step_index].count) {
		step_index++;
		step_offset = 0U;
	}
	if (step_index >= recipe->step_count) {
		return false;
	}

	*action = recipe->steps[step_index].action;
	*sequence = *action == LC3_STATEFUL_ACTION_CORPUS
			    ? (uint16_t)(recipe->steps[step_index].first_sequence + step_offset)
			    : 0U;
	return true;
}

static void cursor_advance(struct bsim_recipe_cursor *cursor, enum lc3_stateful_action action)
{
	enum lc3_stateful_action expected_action;
	uint16_t ignored_sequence;

	if (!cursor_next(cursor, &expected_action, &ignored_sequence) ||
	    expected_action != action) {
		return;
	}

	cursor->actions++;
	if (action == LC3_STATEFUL_ACTION_CORPUS) {
		cursor->valid_actions++;
	} else {
		cursor->plc_actions++;
	}
	cursor->step_offset++;
	while (cursor->step_index < cursor->binding->recipe->step_count &&
	       cursor->step_offset == cursor->binding->recipe->steps[cursor->step_index].count) {
		cursor->step_index++;
		cursor->step_offset = 0U;
	}
}

static void cursor_sync_segment(int index)
{
	struct bsim_sink_segment *segment = &segments[index];

	segment->l_recipe_actions = l_cursors[index].actions;
	segment->l_recipe_valid = l_cursors[index].valid_actions;
	segment->l_recipe_plc = l_cursors[index].plc_actions;
	segment->r_recipe_actions = r_cursors[index].actions;
	segment->r_recipe_valid = r_cursors[index].valid_actions;
	segment->r_recipe_plc = r_cursors[index].plc_actions;
}

static bool cursor_init(struct bsim_recipe_cursor *cursor, const char *recipe_id)
{
	const struct bsim_recipe_binding *binding = recipe_binding_for_id(recipe_id);

	if (binding == NULL) {
		return false;
	}
	memset(cursor, 0, sizeof(*cursor));
	cursor->binding = binding;
	return true;
}

static bool segment_bind_recipes(int index)
{
	struct bsim_sink_segment *segment = &segments[index];
	const struct bsim_scenario_recipe *mapping = scenario_recipe_for(scenario, (uint8_t)index);

	memset(&l_cursors[index], 0, sizeof(l_cursors[index]));
	memset(&r_cursors[index], 0, sizeof(r_cursors[index]));
	if (mapping == NULL) {
		if (!scenario_expects_no_audio()) {
			return false;
		}
		segment->l_recipe_id = "none";
		segment->r_recipe_id = "none";
		return true;
	}
	if (!recipe_bindings_ready || !cursor_init(&l_cursors[index], mapping->left_recipe_id) ||
	    !cursor_init(&r_cursors[index], mapping->right_recipe_id)) {
		return false;
	}
	segment->l_recipe_id = l_cursors[index].binding->recipe->id;
	segment->r_recipe_id = r_cursors[index].binding->recipe->id;
	cursor_sync_segment(index);
	return true;
}

static int16_t read_le16(const uint8_t *bytes)
{
	int32_t value = (int32_t)bytes[0] | ((int32_t)bytes[1] << 8);

	if (value >= 0x8000) {
		value -= 0x10000;
	}

	return (int16_t)value;
}

static void write_le16(uint8_t *bytes, int16_t sample)
{
	const uint16_t value = (uint16_t)sample;

	bytes[0] = (uint8_t)(value & 0xFFU);
	bytes[1] = (uint8_t)(value >> 8);
}

static void scale_reference_frame(const uint8_t *reference, uint32_t samples_per_channel)
{
	for (uint32_t sample = 0U; sample < samples_per_channel; sample++) {
		const int16_t decoded = read_le16(reference + (size_t)sample * sizeof(int16_t));
		const int16_t scaled = (int16_t)(((int32_t)decoded * BSIM_PCM_VOLUME) / 255);

		write_le16(scaled_reference + (size_t)sample * sizeof(int16_t), scaled);
	}
}

static int identify_payload(const uint8_t *payload, uint16_t payload_len,
			    const struct lc3_stateful_recipe *recipe,
			    const struct bsim_pcm_source **identified_source,
			    uint16_t *identified_frame)
{
	const struct bsim_pcm_source *match_source = NULL;
	uint16_t match_frame = 0U;
	uint32_t matches = 0U;

	if (payload == NULL || recipe == NULL || identified_source == NULL ||
	    identified_frame == NULL) {
		return -EINVAL;
	}

	for (size_t source_index = 0U; source_index < ARRAY_SIZE(sources); source_index++) {
		const struct bsim_pcm_source *source = &sources[source_index];

		if (source->duration_us != recipe->duration_us ||
		    source->frame_bytes != payload_len ||
		    source->samples_per_frame != recipe->samples_per_frame) {
			continue;
		}
		for (uint16_t frame = 0U; frame < BSIM_PCM_CORPUS_FRAMES; frame++) {
			const uint8_t *candidate =
				source->lc3 + (size_t)frame * source->frame_bytes;

			if (memcmp(payload, candidate, payload_len) != 0) {
				continue;
			}
			match_source = source;
			match_frame = frame;
			matches++;
		}
	}

	if (matches != 1U) {
		return -ENOENT;
	}

	*identified_source = match_source;
	*identified_frame = match_frame;
	return 0;
}

static int prepare_channel(const char *channel, struct bsim_recipe_cursor *cursor,
			   const struct bsim_observer_push_half *half, uint32_t samples_per_channel,
			   struct bsim_prepared_channel *prepared)
{
	const struct lc3_stateful_recipe *recipe;
	enum lc3_stateful_action expected_action;
	uint16_t expected_sequence;
	const struct bsim_pcm_source *identified_source = NULL;
	uint16_t identified_frame = 0U;
	uint32_t reference_frame;
	int err;

	if (cursor == NULL || cursor->binding == NULL || half == NULL || prepared == NULL) {
		FAIL("le_audio_receiver: missing %s recipe cursor or observer snapshot\n", channel);
		return -EINVAL;
	}

	recipe = cursor->binding->recipe;
	if (recipe->samples_per_frame != samples_per_channel ||
	    !cursor_next(cursor, &expected_action, &expected_sequence)) {
		FAIL("le_audio_receiver: %s recipe %s geometry/overrun at action %u\n", channel,
		     recipe->id, cursor->actions);
		return -ERANGE;
	}

	memset(prepared, 0, sizeof(*prepared));
	prepared->action = expected_action;
	if (expected_action == LC3_STATEFUL_ACTION_PLC) {
		if (half->source_valid || half->payload_len != 0U) {
			FAIL("le_audio_receiver: %s recipe %s action %u requires PLC metadata\n",
			     channel, recipe->id, cursor->actions);
			return -EINVAL;
		}
		return 0;
	}

	if (expected_action != LC3_STATEFUL_ACTION_CORPUS || !half->source_valid ||
	    half->payload_len != recipe->frame_bytes ||
	    half->payload_len > BSIM_OBSERVER_MAX_PAYLOAD_BYTES) {
		FAIL("le_audio_receiver: %s recipe %s action %u requires %u-byte corpus payload\n",
		     channel, recipe->id, cursor->actions, recipe->frame_bytes);
		return -EINVAL;
	}

	err = identify_payload(half->payload, half->payload_len, recipe, &identified_source,
			       &identified_frame);
	if (err != 0 || identified_source != cursor->binding->source ||
	    identified_frame != expected_sequence) {
		FAIL("le_audio_receiver: %s recipe %s action %u payload mismatch source=%s "
		     "frame=%u "
		     "expected=%s/%u\n",
		     channel, recipe->id, cursor->actions,
		     identified_source != NULL ? identified_source->stem : "unknown",
		     identified_frame, cursor->binding->source->stem, expected_sequence);
		return -EINVAL;
	}

	reference_frame = recipe->reference_kind == LC3_STATEFUL_REFERENCE_PORTABLE_PCM
				  ? (uint32_t)recipe->reference_first_frame + cursor->valid_actions
				  : cursor->valid_actions;
	if (reference_frame >= cursor->binding->reference->frame_count) {
		FAIL("le_audio_receiver: %s recipe %s reference overrun at valid action %u\n",
		     channel, recipe->id, cursor->valid_actions);
		return -ERANGE;
	}

	prepared->source_valid = true;
	prepared->reference = cursor->binding->reference->pcm +
			      (size_t)reference_frame * samples_per_channel * sizeof(int16_t);
	return 0;
}

static int accumulate_prepared_channel(bool right, const int16_t *actual,
				       const struct bsim_prepared_channel *prepared,
				       uint32_t samples_per_channel, bool post_boundary)
{
	struct bsim_sink_segment *segment = cur();
	struct pcm_oracle *oracle = right ? &r_oracles[current_seg] : &l_oracles[current_seg];
	int err;

	if (!prepared->source_valid) {
		if (post_boundary) {
			if (right) {
				segment->r_excluded_frames++;
			} else {
				segment->l_excluded_frames++;
			}
		}
		return 0;
	}

	scale_reference_frame(prepared->reference, samples_per_channel);
	err = pcm_oracle_accumulate(oracle, actual, 2U, scaled_reference, sizeof(int16_t),
				    samples_per_channel);
	if (err != 0) {
		FAIL("le_audio_receiver: PCM %s accumulation failed: %d\n",
		     right ? "right" : "left", err);
	}
	return err;
}

static bool recipe_prefix_counts(const struct lc3_stateful_recipe *recipe, uint32_t actions,
				 uint32_t *valid_actions, uint32_t *plc_actions)
{
	uint32_t remaining = actions;
	uint32_t valid = 0U;
	uint32_t plc = 0U;

	if (recipe == NULL || valid_actions == NULL || plc_actions == NULL ||
	    actions > recipe->output_action_count) {
		return false;
	}

	for (size_t index = 0U; index < recipe->step_count && remaining > 0U; index++) {
		const struct lc3_stateful_step *step = &recipe->steps[index];
		const uint32_t consumed = MIN(remaining, (uint32_t)step->count);

		if (step->action == LC3_STATEFUL_ACTION_CORPUS) {
			valid += consumed;
		} else if (step->action == LC3_STATEFUL_ACTION_PLC) {
			plc += consumed;
		} else {
			return false;
		}
		remaining -= consumed;
	}

	if (remaining != 0U) {
		return false;
	}

	*valid_actions = valid;
	*plc_actions = plc;
	return true;
}

static bool validate_recipe_channel(int index, bool right,
				    const struct bsim_scenario_recipe *mapping)
{
	const struct bsim_sink_segment *segment = &segments[index];
	const struct bsim_recipe_cursor *cursor = right ? &r_cursors[index] : &l_cursors[index];
	const char *expected_id = right ? mapping->right_recipe_id : mapping->left_recipe_id;
	const char *actual_id = right ? segment->r_recipe_id : segment->l_recipe_id;
	const uint32_t actions = right ? segment->r_recipe_actions : segment->l_recipe_actions;
	const uint32_t valid = right ? segment->r_recipe_valid : segment->l_recipe_valid;
	const uint32_t plc = right ? segment->r_recipe_plc : segment->l_recipe_plc;
	const uint32_t excluded = right ? segment->r_excluded_frames : segment->l_excluded_frames;
	const struct pcm_oracle_metrics *metrics =
		right ? &segment->r_metrics : &segment->l_metrics;
	uint32_t expected_valid;
	uint32_t expected_plc;
	uint32_t pre_valid;
	uint32_t pre_plc;

	if (cursor->binding == NULL || !string_equal(actual_id, expected_id) ||
	    !recipe_prefix_counts(cursor->binding->recipe, actions, &expected_valid,
				  &expected_plc) ||
	    !recipe_prefix_counts(cursor->binding->recipe, segment->transients, &pre_valid,
				  &pre_plc) ||
	    valid != expected_valid || plc != expected_plc ||
	    actions != segment->transients + segment->pushes || valid != metrics->frames ||
	    plc != actions - valid || pre_valid + pre_plc != segment->transients ||
	    excluded != plc - pre_plc || valid != pre_valid + segment->pushes - excluded ||
	    plc != pre_plc + excluded ||
	    metrics->samples != metrics->frames * segment->samples_per_channel) {
		FAIL("le_audio_receiver: segment %d %s recipe accounting failed\n", index,
		     right ? "right" : "left");
		return false;
	}

	if (mapping->completion == BSIM_RECIPE_COMPLETION_FULL &&
	    (actions != cursor->binding->recipe->output_action_count ||
	     valid != cursor->binding->recipe->valid_frame_count ||
	     plc != cursor->binding->recipe->output_action_count -
			     cursor->binding->recipe->valid_frame_count)) {
		FAIL("le_audio_receiver: segment %d %s recipe %s incomplete\n", index,
		     right ? "right" : "left", actual_id);
		return false;
	}

	return true;
}

static void segment_finalize(int idx)
{
	struct bsim_sink_segment *segment = &segments[idx];
	const struct pcm_oracle_limits limits = {
		.min_samples = required_samples / 2U,
		.max_abs_error = BSIM_PCM_MAX_ABS_ERROR,
		.max_rms_error = BSIM_PCM_MAX_RMS_ERROR,
		.min_correlation_q15 = BSIM_PCM_MIN_CORRELATION_Q15,
	};
	struct audio_stats stats = audio_stats_get();
	int err;

	cursor_sync_segment(idx);
	segment->total_frames = stats.total_frames;
	segment->plc_frames = stats.plc_frames;
	segment->decode_errors = stats.decode_errors;
	segment->configured_samples = required_samples;
	segment->samples_per_channel = required_samples / 2U;

	err = pcm_oracle_finalize(&l_oracles[idx], &segment->l_metrics);
	if (err == 0) {
		err = pcm_oracle_finalize(&r_oracles[idx], &segment->r_metrics);
	}
	if (err != 0) {
		FAIL("le_audio_receiver: PCM oracle finalize failed: %d\n", err);
		return;
	}

	err = pcm_oracle_evaluate(&segment->l_metrics, &limits, &segment->l_evaluation);
	if (err == 0) {
		err = pcm_oracle_evaluate(&segment->r_metrics, &limits, &segment->r_evaluation);
	}
	if (err != 0) {
		FAIL("le_audio_receiver: PCM oracle evaluation failed: %d\n", err);
		return;
	}

	segment->finalized = true;
	printk("SINK_SEG %d pushes=%u trans=%u szero=%u splc=%u total=%u plc=%u derr=%u "
	       "malformed=%u samples=%u spc=%u diff=%u "
	       "lrid=%s lact=%u lval=%u lplc=%u lfr=%u lsm=%u lex=%u lmax=%u lsse=%llu "
	       "lrms=%u lcorr=%d lres=%s "
	       "rrid=%s ract=%u rval=%u rplc=%u rfr=%u rsm=%u rex=%u rmax=%u rsse=%llu "
	       "rrms=%u rcorr=%d rres=%s lemin=%d lemax=%d remin=%d remax=%d "
	       "limmax=%u limrms=%u limcorr=%d\n",
	       idx, segment->pushes, segment->transients, segment->startup_zero,
	       segment->startup_plc, segment->total_frames, segment->plc_frames,
	       segment->decode_errors, segment->malformed_samples, segment->configured_samples,
	       segment->samples_per_channel, segment->differing_samples, segment->l_recipe_id,
	       segment->l_recipe_actions, segment->l_recipe_valid, segment->l_recipe_plc,
	       segment->l_metrics.frames, segment->l_metrics.samples, segment->l_excluded_frames,
	       segment->l_metrics.max_abs_error,
	       (unsigned long long)segment->l_metrics.squared_error, segment->l_metrics.rms_error,
	       segment->l_metrics.correlation_q15, pcm_oracle_result_name(segment->l_evaluation),
	       segment->r_recipe_id, segment->r_recipe_actions, segment->r_recipe_valid,
	       segment->r_recipe_plc, segment->r_metrics.frames, segment->r_metrics.samples,
	       segment->r_excluded_frames, segment->r_metrics.max_abs_error,
	       (unsigned long long)segment->r_metrics.squared_error, segment->r_metrics.rms_error,
	       segment->r_metrics.correlation_q15, pcm_oracle_result_name(segment->r_evaluation),
	       segment->l_energy_min, segment->l_energy_max, segment->r_energy_min,
	       segment->r_energy_max, BSIM_PCM_MAX_ABS_ERROR, BSIM_PCM_MAX_RMS_ERROR,
	       BSIM_PCM_MIN_CORRELATION_Q15);
}

static bool segment_start(void)
{
	if (current_seg >= 0 && !segments[current_seg].finalized) {
		segment_finalize(current_seg);
		segment_count = current_seg + 1;
	}
	if (current_seg + 1 >= BSIM_SINK_MAX_SEGMENTS) {
		FAIL("le_audio_receiver: too many oracle segments\n");
		return false;
	}
	current_seg++;
	memset(&segments[current_seg], 0, sizeof(segments[current_seg]));
	if (pcm_oracle_init(&l_oracles[current_seg]) != 0 ||
	    pcm_oracle_init(&r_oracles[current_seg]) != 0 || !segment_bind_recipes(current_seg)) {
		FAIL("le_audio_receiver: PCM oracle initialization or recipe binding failed\n");
		return false;
	}
	segments[current_seg].l_energy_min = INT32_MAX;
	segments[current_seg].r_energy_min = INT32_MAX;
	boundary_closed = false;
	return true;
}

static bool segment_has_no_audio(int idx)
{
	const struct bsim_sink_segment *segment = &segments[idx];
	const struct pcm_oracle *left = &l_oracles[idx];
	const struct pcm_oracle *right = &r_oracles[idx];

	return segment->pushes == 0U && segment->transients == 0U && segment->startup_zero == 0U &&
	       segment->l_recipe_actions == 0U && segment->l_recipe_valid == 0U &&
	       segment->l_recipe_plc == 0U && segment->r_recipe_actions == 0U &&
	       segment->r_recipe_valid == 0U && segment->r_recipe_plc == 0U &&
	       segment->l_excluded_frames == 0U && segment->r_excluded_frames == 0U &&
	       left->frames == 0U && left->samples == 0U && right->frames == 0U &&
	       right->samples == 0U;
}

/* Public audio_sink API. */

int audio_sink_init(void)
{
	accepting = false;
	return 0;
}

int audio_sink_stream_open(void)
{
	accepting = true;
	return 0;
}

void audio_sink_stream_close(void)
{
	accepting = false;
}

void audio_sink_stop(void)
{
	if (!stopped) {
		stopped = true;
		if (current_seg >= 0 && !segments[current_seg].finalized) {
			segment_finalize(current_seg);
			segment_count = current_seg + 1;
		}
	}
}

void audio_sink_set_input_frames(uint16_t frames)
{
	uint16_t samples = (uint16_t)(frames * 2U);

	if (samples == 0U) {
		samples = BSIM_PCM_10MS_SAMPLES * 2U;
	}

	if (stopped && current_seg >= 0 && segments[current_seg].finalized) {
		stopped = false;
		goal_finalized = false;
		if (!segment_start()) {
			return;
		}
	} else if (current_seg < 0 || segments[current_seg].finalized) {
		goal_finalized = false;
		if (!segment_start()) {
			return;
		}
	}
	required_samples = samples;
}

void audio_sink_test_begin(enum bsim_sink_scenario scn, int dec_calls)
{
	scenario = scn;
	dec_calls_per_push = dec_calls > 0 ? dec_calls : 1;
	stopped = false;
	after_stop_total = 0U;
	concealed_pushes = 0U;
	segment_count = 0;
	current_seg = -1;
	goal_finalized = false;
	required_samples = BSIM_PCM_10MS_SAMPLES * 2U;
	(void)segment_start();
}

int audio_sink_push(const int16_t *data, size_t sample_count)
{
	struct bsim_observer_push snapshot;
	struct bsim_prepared_channel left;
	struct bsim_prepared_channel right;
	bool src_valid;
	bool concealed;
	bool post_boundary;
	bool transient;
	int32_t l_energy = 0;
	int32_t r_energy = 0;
	int err;

	if (!data || sample_count == 0U || (sample_count & 1U)) {
		return -EINVAL;
	}
	if (sample_count != (size_t)required_samples) {
		cur()->malformed_samples++;
		FAIL("le_audio_receiver: malformed sample count, expected %u got %zu push#%u\n",
		     required_samples, sample_count, (unsigned int)cur()->pushes);
		return -EINVAL;
	}
	if (!accepting) {
		return -EBUSY;
	}
	if (goal_finalized) {
		return 0;
	}
	if (stopped) {
		after_stop_total++;
		FAIL("le_audio_receiver: push after stop, sample_count=%zu push#%u\n", sample_count,
		     (unsigned int)cur()->pushes);
		return -EIO;
	}
	if (!bsim_observer_take_push(&snapshot)) {
		FAIL("le_audio_receiver: missing or stale observer payload snapshot\n");
		return -EINVAL;
	}

	err = prepare_channel("left", &l_cursors[current_seg], &snapshot.left,
			      (uint32_t)(sample_count / 2U), &left);
	if (err == 0) {
		err = prepare_channel("right", &r_cursors[current_seg], &snapshot.right,
				      (uint32_t)(sample_count / 2U), &right);
	}
	if (err != 0) {
		return err;
	}

	for (size_t index = 0U; index < sample_count; index += 2U) {
		const int32_t lv = data[index];
		const int32_t rv = data[index + 1U];

		l_energy += lv < 0 ? -lv : lv;
		r_energy += rv < 0 ? -rv : rv;
	}

	src_valid = snapshot.left.source_valid && snapshot.right.source_valid;
	concealed = scenario == BSIM_SCN_MODEA_ONE_CIS_LOSS_10MS && !src_valid;
	post_boundary = boundary_closed;
	transient = false;
	if (!boundary_closed) {
		if (l_energy + r_energy != 0 && src_valid) {
			boundary_closed = true;
			post_boundary = true;
			cur()->startup_plc = audio_stats_get().plc_frames;
		} else {
			transient = true;
		}
	} else if (concealed) {
		concealed_pushes++;
		if (concealed_pushes > BSIM_MODEA_LOSS_COUNT ||
		    (!snapshot.left.source_valid && !snapshot.right.source_valid)) {
			FAIL("le_audio_receiver: invalid concealed push#%u\n",
			     (unsigned int)cur()->pushes);
			return -EINVAL;
		}
	} else if (!src_valid) {
		FAIL("le_audio_receiver: source-invalid push after valid boundary, push#%u "
		     "immediate FAIL\n",
		     (unsigned int)cur()->pushes);
		return -EINVAL;
	}
	if (post_boundary && l_energy + r_energy == 0) {
		FAIL("le_audio_receiver: zero-energy push after audio started, push#%u immediate "
		     "FAIL\n",
		     (unsigned int)cur()->pushes);
		return -EINVAL;
	}
	if (audio_volume_get() != BSIM_PCM_VOLUME || audio_volume_is_muted()) {
		FAIL("le_audio_receiver: BSim volume drift, volume=%u muted=%u\n",
		     audio_volume_get(), audio_volume_is_muted() ? 1U : 0U);
		return -EINVAL;
	}

	if (post_boundary) {
		if (scenario_uses_stereo_reference()) {
			for (size_t index = 0U; index < sample_count; index += 2U) {
				if (data[index] != data[index + 1U]) {
					cur()->differing_samples++;
				}
			}
		} else {
			for (size_t index = 0U; index < sample_count; index += 2U) {
				if (data[index] != data[index + 1U]) {
					FAIL("le_audio_receiver: mono routing mismatch at push#%u "
					     "sample=%zu\n",
					     (unsigned int)cur()->pushes, index / 2U);
					return -EINVAL;
				}
			}
		}
	}

	err = accumulate_prepared_channel(false, data, &left, (uint32_t)(sample_count / 2U),
					  post_boundary);
	if (err == 0) {
		err = accumulate_prepared_channel(true, data + 1, &right,
						  (uint32_t)(sample_count / 2U), post_boundary);
	}
	if (err != 0) {
		return err;
	}
	cursor_advance(&l_cursors[current_seg], left.action);
	cursor_advance(&r_cursors[current_seg], right.action);
	cursor_sync_segment(current_seg);

	if (transient) {
		if (l_energy + r_energy == 0) {
			cur()->startup_zero++;
		}
		cur()->transients++;
		cur()->startup_plc = audio_stats_get().plc_frames;
		return 0;
	}

	if (l_energy < cur()->l_energy_min) {
		cur()->l_energy_min = l_energy;
	}
	if (l_energy > cur()->l_energy_max) {
		cur()->l_energy_max = l_energy;
	}
	if (r_energy < cur()->r_energy_min) {
		cur()->r_energy_min = r_energy;
	}
	if (r_energy > cur()->r_energy_max) {
		cur()->r_energy_max = r_energy;
	}

	cur()->pushes++;
	if (cur()->pushes >= NORMAL_GOAL_PUSHES && !cur()->finalized) {
		segment_finalize(current_seg);
		segment_count = current_seg + 1;
		goal_finalized = true;
	}

	return 0;
}

bool audio_sink_test_goal_reached(void)
{
	struct bsim_sink_segment *s0;
	struct bsim_sink_segment *s1;

	switch (scenario) {
	case BSIM_SCN_MONO_10MS:
	case BSIM_SCN_MONO_7P5MS:
	case BSIM_SCN_MODEA_10MS:
	case BSIM_SCN_MODEA_7P5MS:
	case BSIM_SCN_MODEA_REVERSE_START_10MS:
	case BSIM_SCN_MODEB_10MS:
	case BSIM_SCN_MODEB_7P5MS:
	case BSIM_SCN_MODEA_ONE_CIS_LOSS_10MS:
	case BSIM_SCN_INVALID_SDU_RESUME_10MS:
		return segment_count >= 1 && segments[0].finalized &&
		       segments[0].pushes == NORMAL_GOAL_PUSHES;

	case BSIM_SCN_MODEA_FIRST_STOP_10MS:
	case BSIM_SCN_RELEASE_WITHOUT_DISABLE_10MS:
	case BSIM_SCN_DISCONNECT_STREAMING_10MS:
	case BSIM_SCN_DUPLICATE_RELEASE_10MS:
		return segment_count >= 1 && segments[0].finalized && segments[0].pushes >= 20U &&
		       after_stop_total == 0U;

	case BSIM_SCN_RECONNECT_SECOND_STREAM_10MS:
		if (segment_count < 2) {
			return false;
		}
		s0 = &segments[0];
		s1 = &segments[1];
		return s0->finalized && s1->finalized && s0->pushes >= 20U &&
		       s1->pushes == NORMAL_GOAL_PUSHES && after_stop_total == 0U;

	case BSIM_SCN_UNSUPPORTED_SOURCE_DIRECTION:
	case BSIM_SCN_NO_FREE_SINK_SLOT:
	case BSIM_SCN_INVALID_CODEC_FIELDS:
		return current_seg >= 0 && segment_has_no_audio(current_seg) &&
		       after_stop_total == 0U;

	default:
		return false;
	}
}

bool audio_sink_test_validate(void)
{
	if (scenario_expects_no_audio()) {
		for (int index = 0; index < segment_count; index++) {
			if (!segment_has_no_audio(index)) {
				FAIL("le_audio_receiver: no-audio scenario segment %d received "
				     "audio\n",
				     index);
				return false;
			}
		}
		if (current_seg >= segment_count && current_seg >= 0 &&
		    !segment_has_no_audio(current_seg)) {
			FAIL("le_audio_receiver: no-audio scenario open segment received audio\n");
			return false;
		}
	}

	for (int index = 0; index < segment_count; index++) {
		struct bsim_sink_segment *segment = &segments[index];
		const struct bsim_scenario_recipe *mapping =
			scenario_recipe_for(scenario, (uint8_t)index);
		const uint32_t expected_dec =
			(uint32_t)dec_calls_per_push * (segment->pushes + segment->transients);
		const uint32_t expected_err =
			(scenario == BSIM_SCN_INVALID_SDU_RESUME_10MS) ? 1U : 0U;

		if (mapping == NULL || !validate_recipe_channel(index, false, mapping) ||
		    !validate_recipe_channel(index, true, mapping)) {
			return false;
		}
		if (segment->malformed_samples != 0U) {
			FAIL("le_audio_receiver: segment %d malformed sample count %u\n", index,
			     segment->malformed_samples);
			return false;
		}
		if (segment->decode_errors != expected_err) {
			FAIL("le_audio_receiver: segment %d decode_errors=%u != expected %u\n",
			     index, segment->decode_errors, expected_err);
			return false;
		}
		if (scenario == BSIM_SCN_MODEA_ONE_CIS_LOSS_10MS) {
			if (segment->plc_frames != segment->startup_plc + BSIM_MODEA_LOSS_COUNT ||
			    concealed_pushes != BSIM_MODEA_LOSS_COUNT) {
				FAIL("le_audio_receiver: segment %d loss PLC accounting failed\n",
				     index);
				return false;
			}
		} else if (segment->plc_frames != segment->startup_plc) {
			FAIL("le_audio_receiver: segment %d plc=%u != startup_plc=%u\n", index,
			     segment->plc_frames, segment->startup_plc);
			return false;
		}
		if (segment->total_frames < expected_dec) {
			FAIL("le_audio_receiver: segment %d total=%u < pushes=%u+transients=%u x "
			     "dec=%d\n",
			     index, segment->total_frames, segment->pushes, segment->transients,
			     dec_calls_per_push);
			return false;
		}
		if (segment->pushes > 0U) {
			if (segment->l_evaluation != PCM_ORACLE_RESULT_PASS ||
			    segment->r_evaluation != PCM_ORACLE_RESULT_PASS) {
				FAIL("le_audio_receiver: segment %d PCM evaluation L=%s R=%s\n",
				     index, pcm_oracle_result_name(segment->l_evaluation),
				     pcm_oracle_result_name(segment->r_evaluation));
				return false;
			}
			if (scenario_uses_stereo_reference() ? segment->differing_samples == 0U
							     : segment->differing_samples != 0U) {
				FAIL("le_audio_receiver: segment %d routing difference count %u "
				     "invalid\n",
				     index, segment->differing_samples);
				return false;
			}
			if (segment->l_energy_max <= 0 || segment->r_energy_max <= 0) {
				FAIL("le_audio_receiver: segment %d dead channel (lmax=%d "
				     "rmax=%d)\n",
				     index, segment->l_energy_max, segment->r_energy_max);
				return false;
			}
		} else if (segment->l_evaluation != PCM_ORACLE_RESULT_INSUFFICIENT_SAMPLES ||
			   segment->r_evaluation != PCM_ORACLE_RESULT_INSUFFICIENT_SAMPLES) {
			FAIL("le_audio_receiver: segment %d no-audio PCM evaluation invalid\n",
			     index);
			return false;
		}

		if (bt_pacs_get_available_contexts(BT_AUDIO_DIR_SINK) ==
		    BT_AUDIO_CONTEXT_TYPE_NONE) {
			FAIL("le_audio_receiver: available sink contexts NONE after connection + "
			     "stream\n");
			return false;
		}
	}

	return true;
}

int audio_sink_test_segment_count(void)
{
	return segment_count;
}

bool audio_sink_test_get_segment(int idx, struct bsim_sink_segment *out)
{
	if (!seg_finalized(idx) || out == NULL) {
		return false;
	}
	*out = segments[idx];
	return true;
}

uint32_t audio_sink_test_after_stop_total(void)
{
	return after_stop_total;
}
