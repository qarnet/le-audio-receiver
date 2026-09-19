/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-only diagnostic decoder for portable-oracle-manifest.json corpus data.
 * This program prints one JSON metric record per comparison. The Python
 * wrapper validates fixtures and captures host provenance before invoking it.
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lc3.h"
#include "lc3_stateful_recipes.h"
#include "pcm_oracle.h"

#define CORPUS_FRAMES         128U
#define MAX_SAMPLES_PER_FRAME 480U
#define MAX_FRAME_BYTES       120U

struct corpus_stream {
	const char *stem;
	int duration_us;
	int frame_bytes;
	int samples_per_frame;
};

struct decoded_stream {
	const struct corpus_stream *stream;
	uint8_t *lc3;
	uint8_t *pcm;
	int16_t *actual;
	size_t lc3_size;
	size_t pcm_size;
};

enum comparison_input {
	COMPARISON_ACTUAL,
	COMPARISON_DEAD,
	COMPARISON_SYNTHETIC,
};

static const struct lc3_stateful_step payload_off_by_one_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 8U},
	{LC3_STATEFUL_ACTION_CORPUS, 1U, 100U},
};

static const struct lc3_stateful_step skip_ignored_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 8U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 100U},
};

static const struct lc3_stateful_step loss_burst_omitted_steps[] = {
	{LC3_STATEFUL_ACTION_PLC, 0U, 8U},
	{LC3_STATEFUL_ACTION_CORPUS, 0U, 82U},
};

static const struct lc3_stateful_recipe stateful_mutations[] = {
	{
		.id = "start8_10ms_l_payload_plus1",
		.source_stem = "bsim_48k_10ms_120b_l",
		.reference_path = "bsim_48k_10ms_120b_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 108U,
		.valid_frame_count = 100U,
		.steps = payload_off_by_one_steps,
		.step_count =
			sizeof(payload_off_by_one_steps) / sizeof(payload_off_by_one_steps[0]),
	},
	{
		.id = "start8_10ms_l_ignore_skip20",
		.source_stem = "bsim_48k_10ms_120b_l",
		.reference_path = "stateful_48k_10ms_skip20_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_GENERATED_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 108U,
		.valid_frame_count = 100U,
		.steps = skip_ignored_steps,
		.step_count = sizeof(skip_ignored_steps) / sizeof(skip_ignored_steps[0]),
	},
	{
		.id = "start8_10ms_r_omit_loss48x18",
		.source_stem = "bsim_48k_10ms_120b_r",
		.reference_path = "stateful_48k_10ms_loss48x18_r.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_GENERATED_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 90U,
		.valid_frame_count = 82U,
		.steps = loss_burst_omitted_steps,
		.step_count =
			sizeof(loss_burst_omitted_steps) / sizeof(loss_burst_omitted_steps[0]),
	},
	{
		.id = "start8_10ms_r_wrong_channel",
		.source_stem = "bsim_48k_10ms_120b_r",
		.reference_path = "bsim_48k_10ms_120b_l.pcm",
		.reference_kind = LC3_STATEFUL_REFERENCE_PORTABLE_PCM,
		.reference_first_frame = 0U,
		.duration_us = 10000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.output_action_count = 108U,
		.valid_frame_count = 100U,
		.steps = skip_ignored_steps,
		.step_count = sizeof(skip_ignored_steps) / sizeof(skip_ignored_steps[0]),
	},
};

static int parse_unsigned_limit(const char *text, uint32_t maximum, uint32_t *value)
{
	char *end = NULL;
	unsigned long long parsed;
	const unsigned char *cursor = (const unsigned char *)text;

	if (text == NULL || *text == '\0' || value == NULL) {
		return -1;
	}
	while (*cursor != '\0') {
		if (*cursor < '0' || *cursor > '9') {
			return -1;
		}
		cursor++;
	}
	errno = 0;
	parsed = strtoull(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || parsed > maximum) {
		return -1;
	}

	*value = (uint32_t)parsed;
	return 0;
}

static int parse_signed_limit(const char *text, int32_t minimum, int32_t maximum, int32_t *value)
{
	char *end = NULL;
	long long parsed;
	const unsigned char *cursor;

	if (text == NULL || *text == '\0' || value == NULL) {
		return -1;
	}
	cursor = (const unsigned char *)text;
	if (*cursor == '-') {
		cursor++;
	}
	if (*cursor == '\0') {
		return -1;
	}
	while (*cursor != '\0') {
		if (*cursor < '0' || *cursor > '9') {
			return -1;
		}
		cursor++;
	}
	errno = 0;
	parsed = strtoll(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || parsed < minimum || parsed > maximum) {
		return -1;
	}

	*value = (int32_t)parsed;
	return 0;
}

static int parse_limits(int argc, char **argv, struct pcm_oracle_limits *limits)
{
	if (argc != 5 || limits == NULL ||
	    parse_unsigned_limit(argv[2], UINT16_MAX, &limits->max_abs_error) != 0 ||
	    parse_unsigned_limit(argv[3], UINT16_MAX, &limits->max_rms_error) != 0 ||
	    parse_signed_limit(argv[4], INT16_MIN, INT16_MAX, &limits->min_correlation_q15) != 0) {
		fprintf(stderr, "FATAL: invalid PCM policy arguments\n");
		return -1;
	}

	limits->min_samples = 0U;
	return 0;
}

static int evaluate_metrics(const struct pcm_oracle_metrics *metrics,
			    const struct pcm_oracle_limits *policy, enum pcm_oracle_result *result)
{
	struct pcm_oracle_limits limits;

	if (metrics == NULL || policy == NULL || result == NULL) {
		return -1;
	}
	limits = *policy;
	limits.min_samples = metrics->samples;
	return pcm_oracle_evaluate(metrics, &limits, result);
}

static char *fixture_path(const char *directory, const char *stem, const char *suffix)
{
	size_t directory_len = strlen(directory);
	size_t stem_len = strlen(stem);
	size_t suffix_len = strlen(suffix);
	char *path;

	if (directory_len > SIZE_MAX - stem_len ||
	    directory_len + stem_len > SIZE_MAX - suffix_len - 2U) {
		return NULL;
	}

	path = malloc(directory_len + stem_len + suffix_len + 2U);
	if (path == NULL) {
		return NULL;
	}
	(void)snprintf(path, directory_len + stem_len + suffix_len + 2U, "%s/%s%s", directory, stem,
		       suffix);
	return path;
}

static int read_file(const char *path, uint8_t **bytes, size_t size)
{
	FILE *file = NULL;
	uint8_t *buffer = NULL;
	int next;
	int rc = -1;

	file = fopen(path, "rb");
	if (file == NULL) {
		fprintf(stderr, "FATAL: cannot open %s\n", path);
		goto out;
	}
	buffer = malloc(size);
	if (buffer == NULL) {
		fprintf(stderr, "FATAL: allocation failed for %s\n", path);
		goto out;
	}
	if (fread(buffer, 1, size, file) != size) {
		fprintf(stderr, "FATAL: short read from %s\n", path);
		goto out;
	}
	next = fgetc(file);
	if (next != EOF) {
		fprintf(stderr, "FATAL: unexpected trailing bytes in %s\n", path);
		goto out;
	}
	if (ferror(file)) {
		fprintf(stderr, "FATAL: read failed for %s\n", path);
		goto out;
	}

	*bytes = buffer;
	buffer = NULL;
	rc = 0;
out:
	free(buffer);
	if (file != NULL && fclose(file) != 0) {
		fprintf(stderr, "FATAL: close failed for %s\n", path);
		rc = -1;
	}
	return rc;
}

static char *fixture_relative_path(const char *directory, const char *relative_path)
{
	size_t directory_len;
	size_t relative_len;
	char *path;

	if (directory == NULL || relative_path == NULL) {
		return NULL;
	}
	directory_len = strlen(directory);
	relative_len = strlen(relative_path);
	if (directory_len > SIZE_MAX - relative_len ||
	    directory_len + relative_len > SIZE_MAX - 2U) {
		return NULL;
	}
	path = malloc(directory_len + relative_len + 2U);
	if (path == NULL) {
		return NULL;
	}
	(void)snprintf(path, directory_len + relative_len + 2U, "%s/%s", directory, relative_path);
	return path;
}

static const struct decoded_stream *find_decoded_stream(const struct decoded_stream *decoded,
							size_t decoded_count, const char *stem)
{
	if (decoded == NULL || stem == NULL) {
		return NULL;
	}
	for (size_t index = 0U; index < decoded_count; index++) {
		if (decoded[index].stream != NULL &&
		    strcmp(decoded[index].stream->stem, stem) == 0) {
			return &decoded[index];
		}
	}

	return NULL;
}

struct stateful_reference {
	const uint8_t *bytes;
	size_t size;
	uint8_t *owned_bytes;
};

static int stream_matches_portable_pcm_path(const struct decoded_stream *decoded,
					    const char *reference_path)
{
	size_t stem_length;

	if (decoded == NULL || decoded->stream == NULL || reference_path == NULL) {
		return 0;
	}
	stem_length = strlen(decoded->stream->stem);
	return strlen(reference_path) == stem_length + strlen(".pcm") &&
	       memcmp(reference_path, decoded->stream->stem, stem_length) == 0 &&
	       strcmp(reference_path + stem_length, ".pcm") == 0;
}

static int load_generated_reference(const char *directory, const struct lc3_stateful_recipe *recipe,
				    struct stateful_reference *reference)
{
	char *path = NULL;
	size_t expected_size;
	int rc = -1;

	if (directory == NULL || recipe == NULL || reference == NULL ||
	    recipe->reference_kind != LC3_STATEFUL_REFERENCE_GENERATED_PCM ||
	    recipe->samples_per_frame == 0U ||
	    (size_t)recipe->valid_frame_count >
		    SIZE_MAX / ((size_t)recipe->samples_per_frame * sizeof(int16_t))) {
		fprintf(stderr, "FATAL: invalid generated reference geometry\n");
		goto out;
	}
	expected_size =
		(size_t)recipe->valid_frame_count * recipe->samples_per_frame * sizeof(int16_t);
	path = fixture_relative_path(directory, recipe->reference_path);
	if (path == NULL) {
		fprintf(stderr, "FATAL: generated reference path allocation failed for %s\n",
			recipe->id);
		goto out;
	}
	if (read_file(path, &reference->owned_bytes, expected_size) != 0) {
		goto out;
	}
	reference->bytes = reference->owned_bytes;
	reference->size = expected_size;
	rc = 0;
out:
	free(path);
	return rc;
}

static int reference_for_recipe(const char *directory, const struct decoded_stream *decoded,
				size_t decoded_count, const struct lc3_stateful_recipe *recipe,
				struct stateful_reference *reference)
{
	size_t expected_size;

	if (directory == NULL || decoded == NULL || recipe == NULL || reference == NULL ||
	    recipe->samples_per_frame == 0U ||
	    (size_t)recipe->valid_frame_count >
		    SIZE_MAX / ((size_t)recipe->samples_per_frame * sizeof(int16_t))) {
		fprintf(stderr, "FATAL: invalid stateful reference geometry\n");
		return -1;
	}
	memset(reference, 0, sizeof(*reference));
	expected_size =
		(size_t)recipe->valid_frame_count * recipe->samples_per_frame * sizeof(int16_t);
	if (recipe->reference_kind == LC3_STATEFUL_REFERENCE_GENERATED_PCM) {
		return load_generated_reference(directory, recipe, reference);
	}
	if (recipe->reference_kind != LC3_STATEFUL_REFERENCE_PORTABLE_PCM) {
		fprintf(stderr, "FATAL: unknown stateful reference kind for %s\n", recipe->id);
		return -1;
	}

	for (size_t index = 0U; index < decoded_count; index++) {
		const struct decoded_stream *candidate = &decoded[index];
		size_t first_offset;

		if (!stream_matches_portable_pcm_path(candidate, recipe->reference_path) ||
		    candidate->stream->samples_per_frame != (int)recipe->samples_per_frame ||
		    (size_t)recipe->reference_first_frame >
			    SIZE_MAX / ((size_t)recipe->samples_per_frame * sizeof(int16_t))) {
			continue;
		}
		first_offset = (size_t)recipe->reference_first_frame * recipe->samples_per_frame *
			       sizeof(int16_t);
		if (first_offset > candidate->pcm_size ||
		    expected_size > candidate->pcm_size - first_offset) {
			fprintf(stderr, "FATAL: portable reference range is invalid for %s\n",
				recipe->id);
			return -1;
		}
		reference->bytes = candidate->pcm + first_offset;
		reference->size = expected_size;
		return 0;
	}

	fprintf(stderr, "FATAL: portable reference is unavailable for %s\n", recipe->id);
	return -1;
}

static int replay_stateful_recipe(const struct decoded_stream *actual_stream,
				  const struct lc3_stateful_recipe *recipe,
				  const uint8_t *reference, size_t reference_size,
				  struct pcm_oracle_metrics *metrics)
{
	lc3_decoder_mem_48k_t decoder_mem;
	lc3_decoder_t decoder;
	struct pcm_oracle oracle;
	int16_t actual[MAX_SAMPLES_PER_FRAME];
	size_t reference_offset = 0U;
	size_t expected_reference_size;

	if (actual_stream == NULL || actual_stream->stream == NULL || recipe == NULL ||
	    reference == NULL || metrics == NULL || !lc3_stateful_recipes_validate(recipe, 1U) ||
	    actual_stream->stream->duration_us != (int)recipe->duration_us ||
	    actual_stream->stream->frame_bytes != (int)recipe->frame_bytes ||
	    actual_stream->stream->samples_per_frame != (int)recipe->samples_per_frame ||
	    recipe->samples_per_frame > MAX_SAMPLES_PER_FRAME ||
	    (size_t)recipe->valid_frame_count >
		    SIZE_MAX / ((size_t)recipe->samples_per_frame * sizeof(int16_t))) {
		fprintf(stderr, "FATAL: invalid stateful replay geometry\n");
		return -1;
	}
	expected_reference_size =
		(size_t)recipe->valid_frame_count * recipe->samples_per_frame * sizeof(int16_t);
	if (reference_size != expected_reference_size) {
		fprintf(stderr, "FATAL: stateful trace size mismatch for %s\n", recipe->id);
		return -1;
	}
	decoder = lc3_setup_decoder((int)recipe->duration_us, 48000, 0, &decoder_mem);
	if (decoder == NULL || pcm_oracle_init(&oracle) != 0) {
		fprintf(stderr, "FATAL: stateful replay setup failed for %s\n", recipe->id);
		return -1;
	}

	for (size_t step_index = 0U; step_index < recipe->step_count; step_index++) {
		const struct lc3_stateful_step *step = &recipe->steps[step_index];

		for (uint16_t action_index = 0U; action_index < step->count; action_index++) {
			int decode_result;

			if (step->action == LC3_STATEFUL_ACTION_PLC) {
				decode_result =
					lc3_decode(decoder, NULL, 0, LC3_PCM_FORMAT_S16, actual, 1);
				if (decode_result != 1) {
					fprintf(stderr, "FATAL: PLC replay failed for %s\n",
						recipe->id);
					return -1;
				}
				continue;
			}

			decode_result = lc3_decode(
				decoder,
				actual_stream->lc3 + (size_t)(step->first_sequence + action_index) *
							     recipe->frame_bytes,
				recipe->frame_bytes, LC3_PCM_FORMAT_S16, actual, 1);
			if (decode_result != 0 ||
			    (size_t)recipe->samples_per_frame * sizeof(int16_t) > reference_size ||
			    reference_offset > reference_size - (size_t)recipe->samples_per_frame *
									sizeof(int16_t) ||
			    pcm_oracle_accumulate(&oracle, actual, 1U, reference + reference_offset,
						  2U, recipe->samples_per_frame) != 0) {
				fprintf(stderr, "FATAL: stateful corpus replay failed for %s\n",
					recipe->id);
				return -1;
			}
			reference_offset += (size_t)recipe->samples_per_frame * sizeof(int16_t);
		}
	}

	if (reference_offset != reference_size || pcm_oracle_finalize(&oracle, metrics) != 0 ||
	    metrics->frames != recipe->valid_frame_count ||
	    metrics->samples != (uint32_t)recipe->valid_frame_count * recipe->samples_per_frame) {
		fprintf(stderr, "FATAL: stateful replay finalization failed for %s\n", recipe->id);
		return -1;
	}
	return 0;
}

static int run_stateful_comparison(const char *directory, const struct decoded_stream *decoded,
				   size_t decoded_count,
				   const struct lc3_stateful_recipe *actual_recipe,
				   const struct lc3_stateful_recipe *reference_recipe,
				   struct pcm_oracle_metrics *metrics)
{
	const struct decoded_stream *actual_stream;
	struct stateful_reference reference = {0};
	int rc = -1;

	if (actual_recipe == NULL || reference_recipe == NULL ||
	    !lc3_stateful_recipes_validate(actual_recipe, 1U) ||
	    !lc3_stateful_recipes_validate(reference_recipe, 1U) ||
	    actual_recipe->samples_per_frame != reference_recipe->samples_per_frame) {
		fprintf(stderr, "FATAL: invalid stateful comparison recipe\n");
		goto out;
	}
	actual_stream = find_decoded_stream(decoded, decoded_count, actual_recipe->source_stem);
	if (actual_stream == NULL) {
		fprintf(stderr, "FATAL: stateful source stream is unavailable for %s\n",
			actual_recipe->id);
		goto out;
	}
	if (reference_for_recipe(directory, decoded, decoded_count, reference_recipe, &reference) !=
		    0 ||
	    replay_stateful_recipe(actual_stream, actual_recipe, reference.bytes, reference.size,
				   metrics) != 0) {
		goto out;
	}
	rc = 0;
out:
	free(reference.owned_bytes);
	return rc;
}

static void free_decoded_stream(struct decoded_stream *decoded)
{
	free(decoded->lc3);
	free(decoded->pcm);
	free(decoded->actual);
	memset(decoded, 0, sizeof(*decoded));
}

static int decode_stream(const char *directory, const struct corpus_stream *stream,
			 struct decoded_stream *decoded, struct pcm_oracle_metrics *valid_metrics)
{
	char *lc3_path = NULL;
	char *pcm_path = NULL;
	lc3_decoder_mem_48k_t decoder_mem;
	lc3_decoder_t decoder;
	struct pcm_oracle oracle;
	size_t sample_count = (size_t)stream->samples_per_frame * CORPUS_FRAMES;
	int rc = -1;

	memset(decoded, 0, sizeof(*decoded));
	decoded->stream = stream;
	decoded->lc3_size = (size_t)stream->frame_bytes * CORPUS_FRAMES;
	decoded->pcm_size = sample_count * sizeof(*decoded->actual);
	lc3_path = fixture_path(directory, stream->stem, ".lc3");
	pcm_path = fixture_path(directory, stream->stem, ".pcm");
	if (lc3_path == NULL || pcm_path == NULL) {
		fprintf(stderr, "FATAL: path allocation failed for %s\n", stream->stem);
		goto out;
	}
	if (read_file(lc3_path, &decoded->lc3, decoded->lc3_size) != 0 ||
	    read_file(pcm_path, &decoded->pcm, decoded->pcm_size) != 0) {
		goto out;
	}
	decoded->actual = calloc(sample_count, sizeof(*decoded->actual));
	if (decoded->actual == NULL) {
		fprintf(stderr, "FATAL: allocation failed for decoded %s\n", stream->stem);
		goto out;
	}

	/* One decoder remains alive for every frame in this stream. */
	decoder = lc3_setup_decoder(stream->duration_us, 48000, 0, &decoder_mem);
	if (decoder == NULL) {
		fprintf(stderr, "FATAL: decoder setup failed for %s\n", stream->stem);
		goto out;
	}
	if (pcm_oracle_init(&oracle) != 0) {
		fprintf(stderr, "FATAL: comparator init failed for %s\n", stream->stem);
		goto out;
	}
	for (uint32_t frame = 0U; frame < CORPUS_FRAMES; frame++) {
		const uint8_t *lc3_frame = decoded->lc3 + (size_t)frame * stream->frame_bytes;
		int16_t *actual = decoded->actual + (size_t)frame * stream->samples_per_frame;
		const uint8_t *reference =
			decoded->pcm + (size_t)frame * stream->samples_per_frame * 2U;

		if (lc3_decode(decoder, lc3_frame, stream->frame_bytes, LC3_PCM_FORMAT_S16, actual,
			       1) != 0) {
			fprintf(stderr, "FATAL: decode failed for %s frame %u\n", stream->stem,
				(unsigned int)frame);
			goto out;
		}
		if (pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U,
					  (uint32_t)stream->samples_per_frame) != 0) {
			fprintf(stderr, "FATAL: comparator accumulation failed for %s frame %u\n",
				stream->stem, (unsigned int)frame);
			goto out;
		}
	}
	if (pcm_oracle_finalize(&oracle, valid_metrics) != 0) {
		fprintf(stderr, "FATAL: comparator finalization failed for %s\n", stream->stem);
		goto out;
	}

	rc = 0;
out:
	free(lc3_path);
	free(pcm_path);
	if (rc != 0) {
		free_decoded_stream(decoded);
	}
	return rc;
}

static int compare_frames(const struct decoded_stream *actual_stream, uint32_t actual_start_frame,
			  const struct decoded_stream *reference_stream,
			  uint32_t reference_start_frame, uint32_t frame_count,
			  enum comparison_input input, struct pcm_oracle_metrics *metrics)
{
	struct pcm_oracle oracle;
	int16_t dead[MAX_SAMPLES_PER_FRAME] = {0};
	int16_t synthetic[MAX_SAMPLES_PER_FRAME];
	uint32_t samples_per_frame = (uint32_t)actual_stream->stream->samples_per_frame;

	if (samples_per_frame != (uint32_t)reference_stream->stream->samples_per_frame ||
	    samples_per_frame > MAX_SAMPLES_PER_FRAME || actual_start_frame > CORPUS_FRAMES ||
	    reference_start_frame > CORPUS_FRAMES ||
	    frame_count > CORPUS_FRAMES - actual_start_frame ||
	    frame_count > CORPUS_FRAMES - reference_start_frame) {
		fprintf(stderr, "FATAL: invalid comparison geometry\n");
		return -1;
	}
	if (pcm_oracle_init(&oracle) != 0) {
		fprintf(stderr, "FATAL: comparator init failed\n");
		return -1;
	}

	for (uint32_t frame = 0U; frame < frame_count; frame++) {
		const int16_t *actual;
		const uint8_t *reference =
			reference_stream->pcm +
			(size_t)(reference_start_frame + frame) * samples_per_frame * 2U;

		switch (input) {
		case COMPARISON_ACTUAL:
			actual = actual_stream->actual +
				 (size_t)(actual_start_frame + frame) * samples_per_frame;
			break;
		case COMPARISON_DEAD:
			actual = dead;
			break;
		case COMPARISON_SYNTHETIC:
			for (uint32_t sample = 0U; sample < samples_per_frame; sample++) {
				uint32_t sequence_sample = frame * samples_per_frame + sample;

				synthetic[sample] =
					(sequence_sample & 1U) == 0U ? INT16_MIN : INT16_MAX;
			}
			actual = synthetic;
			break;
		default:
			fprintf(stderr, "FATAL: invalid comparison input\n");
			return -1;
		}

		if (pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, samples_per_frame) !=
		    0) {
			fprintf(stderr, "FATAL: comparator accumulation failed\n");
			return -1;
		}
	}

	if (pcm_oracle_finalize(&oracle, metrics) != 0) {
		fprintf(stderr, "FATAL: comparator finalization failed\n");
		return -1;
	}
	return 0;
}

static int print_metrics(const char *comparison, const char *stem, const char *reference_stem,
			 const struct pcm_oracle_metrics *metrics,
			 const struct pcm_oracle_limits *policy,
			 enum pcm_oracle_result expected_result)
{
	enum pcm_oracle_result result;
	const char *evaluation;

	if (evaluate_metrics(metrics, policy, &result) != 0) {
		fprintf(stderr, "FATAL: comparator evaluation failed for %s\n", comparison);
		return -1;
	}
	if (result != expected_result) {
		fprintf(stderr, "FATAL: %s evaluation is %s, expected %s\n", comparison,
			pcm_oracle_result_name(result), pcm_oracle_result_name(expected_result));
		return -1;
	}
	evaluation = pcm_oracle_result_name(result);
	return printf("{\"record\":\"metric\",\"comparison\":\"%s\",\"stem\":\"%s\","
		      "\"reference_stem\":\"%s\",\"squared_error\":%" PRIu64 ","
		      "\"actual_energy_scaled\":%" PRIu64 ",\"reference_energy_scaled\":%" PRIu64
		      ",\"dot_product_scaled\":%" PRId64 ",\"samples\":%" PRIu32
		      ",\"frames\":%" PRIu32 ",\"max_abs_error\":%" PRIu32 ",\"rms_error\":%" PRIu32
		      ",\"correlation_q15\":%" PRId32 ",\"evaluation\":\"%s\"}\n",
		      comparison, stem, reference_stem, metrics->squared_error,
		      metrics->actual_energy_scaled, metrics->reference_energy_scaled,
		      metrics->dot_product_scaled, metrics->samples, metrics->frames,
		      metrics->max_abs_error, metrics->rms_error, metrics->correlation_q15,
		      evaluation) < 0
		       ? -1
		       : 0;
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
	uint16_t value = (uint16_t)sample;

	bytes[0] = (uint8_t)(value & 0xFFU);
	bytes[1] = (uint8_t)(value >> 8);
}

static int finalize_control(const int16_t *actual, const uint8_t *reference, uint32_t samples,
			    struct pcm_oracle_metrics *metrics)
{
	struct pcm_oracle oracle;

	if (pcm_oracle_init(&oracle) != 0 ||
	    pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, samples) != 0 ||
	    pcm_oracle_finalize(&oracle, metrics) != 0) {
		fprintf(stderr, "FATAL: control comparator failed\n");
		return -1;
	}
	return 0;
}

static int populate_reference_frame(const struct decoded_stream *stream, int16_t *actual)
{
	uint32_t sample;

	if (stream == NULL || stream->stream == NULL || actual == NULL ||
	    stream->stream->samples_per_frame < 0 ||
	    (uint32_t)stream->stream->samples_per_frame > MAX_SAMPLES_PER_FRAME) {
		fprintf(stderr, "FATAL: invalid control reference geometry\n");
		return -1;
	}

	for (sample = 0U; sample < (uint32_t)stream->stream->samples_per_frame; sample++) {
		actual[sample] = read_le16(stream->pcm + (size_t)sample * 2U);
	}
	return 0;
}

static int inward_sample(int16_t reference, uint32_t difference, int16_t *actual)
{
	int64_t candidate =
		reference >= 0 ? (int64_t)reference - difference : (int64_t)reference + difference;

	if (actual == NULL || candidate < INT16_MIN || candidate > INT16_MAX) {
		return -1;
	}
	*actual = (int16_t)candidate;
	return 0;
}

static int run_lc3_byte_corruption(const struct decoded_stream *stream,
				   struct pcm_oracle_metrics *metrics)
{
	lc3_decoder_mem_48k_t decoder_mem;
	lc3_decoder_t decoder;
	struct pcm_oracle oracle;
	uint8_t corrupted_frame[MAX_FRAME_BYTES];
	int16_t actual[MAX_SAMPLES_PER_FRAME];

	if (stream == NULL || stream->stream == NULL || metrics == NULL ||
	    stream->stream->frame_bytes < 0 ||
	    (uint32_t)stream->stream->frame_bytes > MAX_FRAME_BYTES ||
	    stream->stream->samples_per_frame < 0 ||
	    (uint32_t)stream->stream->samples_per_frame > MAX_SAMPLES_PER_FRAME) {
		fprintf(stderr, "FATAL: invalid byte-corruption geometry\n");
		return -1;
	}
	decoder = lc3_setup_decoder(stream->stream->duration_us, 48000, 0, &decoder_mem);
	if (decoder == NULL || pcm_oracle_init(&oracle) != 0) {
		fprintf(stderr, "FATAL: byte-corruption setup failed\n");
		return -1;
	}

	for (uint32_t frame = 0U; frame < CORPUS_FRAMES; frame++) {
		const uint8_t *lc3_frame =
			stream->lc3 + (size_t)frame * stream->stream->frame_bytes;
		const uint8_t *reference =
			stream->pcm + (size_t)frame * stream->stream->samples_per_frame * 2U;

		if (frame == 0U) {
			memcpy(corrupted_frame, lc3_frame, (size_t)stream->stream->frame_bytes);
			corrupted_frame[0] ^= 0x04U;
			lc3_frame = corrupted_frame;
		}
		if (lc3_decode(decoder, lc3_frame, stream->stream->frame_bytes, LC3_PCM_FORMAT_S16,
			       actual, 1) != 0) {
			fprintf(stderr, "FATAL: byte-corruption decode failed for frame %u\n",
				(unsigned int)frame);
			return -1;
		}
		if (pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U,
					  (uint32_t)stream->stream->samples_per_frame) != 0) {
			fprintf(stderr, "FATAL: byte-corruption comparator accumulation failed\n");
			return -1;
		}
	}
	if (pcm_oracle_finalize(&oracle, metrics) != 0) {
		fprintf(stderr, "FATAL: byte-corruption comparator finalization failed\n");
		return -1;
	}
	return 0;
}

static int run_max_error_boundary(const struct decoded_stream *stream,
				  const struct pcm_oracle_limits *policy,
				  struct pcm_oracle_metrics *metrics)
{
	int16_t actual[MAX_SAMPLES_PER_FRAME];
	uint32_t difference;
	int changed = 0;

	if (stream == NULL || stream->stream == NULL || policy == NULL || metrics == NULL ||
	    stream->stream->samples_per_frame != MAX_SAMPLES_PER_FRAME) {
		fprintf(stderr, "FATAL: invalid maximum-error boundary geometry\n");
		return -1;
	}
	difference = policy->max_abs_error + 1U;
	if (populate_reference_frame(stream, actual) != 0) {
		return -1;
	}
	for (uint32_t sample = 0U; sample < MAX_SAMPLES_PER_FRAME; sample++) {
		int16_t candidate;

		if (inward_sample(actual[sample], difference, &candidate) == 0) {
			actual[sample] = candidate;
			changed = 1;
			break;
		}
	}
	if (!changed ||
	    finalize_control(actual, stream->pcm, MAX_SAMPLES_PER_FRAME, metrics) != 0) {
		fprintf(stderr, "FATAL: maximum-error boundary construction failed\n");
		return -1;
	}
	if (metrics->max_abs_error != difference || metrics->rms_error > policy->max_rms_error) {
		fprintf(stderr, "FATAL: maximum-error boundary metrics are invalid\n");
		return -1;
	}
	return 0;
}

static int run_rms_error_boundary(const struct decoded_stream *stream,
				  const struct pcm_oracle_limits *policy,
				  struct pcm_oracle_metrics *metrics)
{
	int16_t actual[MAX_SAMPLES_PER_FRAME];
	uint32_t difference;

	if (stream == NULL || stream->stream == NULL || policy == NULL || metrics == NULL ||
	    stream->stream->samples_per_frame != MAX_SAMPLES_PER_FRAME) {
		fprintf(stderr, "FATAL: invalid RMS-error boundary geometry\n");
		return -1;
	}
	difference = policy->max_rms_error + 1U;
	if (populate_reference_frame(stream, actual) != 0) {
		return -1;
	}
	for (uint32_t sample = 0U; sample < MAX_SAMPLES_PER_FRAME; sample++) {
		if (inward_sample(actual[sample], difference, &actual[sample]) != 0) {
			fprintf(stderr, "FATAL: RMS-error boundary construction failed\n");
			return -1;
		}
	}
	if (finalize_control(actual, stream->pcm, MAX_SAMPLES_PER_FRAME, metrics) != 0) {
		return -1;
	}
	if (metrics->max_abs_error != difference || metrics->rms_error != difference) {
		fprintf(stderr, "FATAL: RMS-error boundary metrics are invalid\n");
		return -1;
	}
	return 0;
}

static int run_correlation_boundary(struct pcm_oracle_metrics *metrics)
{
	int16_t actual[MAX_SAMPLES_PER_FRAME];
	uint8_t reference[MAX_SAMPLES_PER_FRAME * 2U];

	if (metrics == NULL) {
		fprintf(stderr, "FATAL: correlation boundary metrics are missing\n");
		return -1;
	}
	for (uint32_t sample = 0U; sample < MAX_SAMPLES_PER_FRAME; sample++) {
		int16_t reference_sample = (sample & 1U) == 0U ? 256 : -256;

		write_le16(reference + (size_t)sample * 2U, reference_sample);
		actual[sample] = (int16_t)-reference_sample;
	}
	if (finalize_control(actual, reference, MAX_SAMPLES_PER_FRAME, metrics) != 0) {
		return -1;
	}
	if (metrics->max_abs_error != 512U || metrics->rms_error != 512U ||
	    metrics->correlation_q15 != INT16_MIN) {
		fprintf(stderr, "FATAL: correlation boundary metrics are invalid\n");
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	static const struct corpus_stream streams[] = {
		{"bsim_48k_10ms_120b_l", 10000, 120, 480},
		{"bsim_48k_10ms_120b_r", 10000, 120, 480},
		{"bsim_48k_7p5ms_90b_l", 7500, 90, 360},
		{"bsim_48k_7p5ms_90b_r", 7500, 90, 360},
	};
	struct decoded_stream decoded[sizeof(streams) / sizeof(streams[0])];
	struct pcm_oracle_metrics valid_metrics[sizeof(streams) / sizeof(streams[0])];
	struct pcm_oracle_metrics metrics;
	struct pcm_oracle_limits policy;
	int rc = 1;

	if (argc != 5) {
		fprintf(stderr,
			"Usage: %s FIXTURE_DIRECTORY MAX_ABS_ERROR MAX_RMS_ERROR "
			"MIN_CORRELATION_Q15\n",
			argv[0]);
		return 1;
	}
	if (parse_limits(argc, argv, &policy) != 0) {
		return 1;
	}
	if (!lc3_stateful_recipes_validate(lc3_stateful_recipes, lc3_stateful_recipe_count)) {
		fprintf(stderr, "FATAL: stateful recipe table is invalid\n");
		return 1;
	}
	memset(decoded, 0, sizeof(decoded));

	for (size_t index = 0U; index < sizeof(streams) / sizeof(streams[0]); index++) {
		if (decode_stream(argv[1], &streams[index], &decoded[index],
				  &valid_metrics[index]) != 0) {
			goto out;
		}
		if (print_metrics("valid", streams[index].stem, streams[index].stem,
				  &valid_metrics[index], &policy, PCM_ORACLE_RESULT_PASS) != 0) {
			fprintf(stderr, "FATAL: valid metric failed for %s\n", streams[index].stem);
			goto out;
		}
	}

	for (size_t index = 0U; index < sizeof(streams) / sizeof(streams[0]); index += 2U) {
		if (compare_frames(&decoded[index], 0U, &decoded[index + 1U], 0U, CORPUS_FRAMES,
				   COMPARISON_ACTUAL, &metrics) != 0 ||
		    print_metrics("channel-swap", streams[index].stem, streams[index + 1U].stem,
				  &metrics, &policy, PCM_ORACLE_RESULT_MAX_ERROR) != 0) {
			fprintf(stderr, "FATAL: channel-swap metric failed for %s\n",
				streams[index].stem);
			goto out;
		}
	}

	for (size_t index = 0U; index < sizeof(streams) / sizeof(streams[0]); index++) {
		if (compare_frames(&decoded[index], 1U, &decoded[index], 0U, CORPUS_FRAMES - 1U,
				   COMPARISON_ACTUAL, &metrics) != 0 ||
		    print_metrics("prior-frame-shift", streams[index].stem, streams[index].stem,
				  &metrics, &policy, PCM_ORACLE_RESULT_MAX_ERROR) != 0) {
			fprintf(stderr, "FATAL: prior-frame metric failed for %s\n",
				streams[index].stem);
			goto out;
		}
		if (compare_frames(&decoded[index], 0U, &decoded[index], 1U, CORPUS_FRAMES - 1U,
				   COMPARISON_ACTUAL, &metrics) != 0 ||
		    print_metrics("next-frame-shift", streams[index].stem, streams[index].stem,
				  &metrics, &policy, PCM_ORACLE_RESULT_MAX_ERROR) != 0) {
			fprintf(stderr, "FATAL: next-frame metric failed for %s\n",
				streams[index].stem);
			goto out;
		}
		if (compare_frames(&decoded[index], 0U, &decoded[index], 0U, CORPUS_FRAMES,
				   COMPARISON_DEAD, &metrics) != 0 ||
		    print_metrics("dead-channel", streams[index].stem, streams[index].stem,
				  &metrics, &policy, PCM_ORACLE_RESULT_MAX_ERROR) != 0) {
			fprintf(stderr, "FATAL: dead-channel metric failed for %s\n",
				streams[index].stem);
			goto out;
		}
		if (compare_frames(&decoded[index], 0U, &decoded[index], 0U, CORPUS_FRAMES,
				   COMPARISON_SYNTHETIC, &metrics) != 0 ||
		    print_metrics("low-correlation-synthetic", streams[index].stem,
				  streams[index].stem, &metrics, &policy,
				  PCM_ORACLE_RESULT_MAX_ERROR) != 0) {
			fprintf(stderr, "FATAL: synthetic metric failed for %s\n",
				streams[index].stem);
			goto out;
		}
	}

	if (run_lc3_byte_corruption(&decoded[0], &metrics) != 0 ||
	    print_metrics("lc3-byte-corruption", streams[0].stem, streams[0].stem, &metrics,
			  &policy, PCM_ORACLE_RESULT_MAX_ERROR) != 0) {
		fprintf(stderr, "FATAL: LC3 byte-corruption metric failed\n");
		goto out;
	}
	if (run_max_error_boundary(&decoded[0], &policy, &metrics) != 0 ||
	    print_metrics("max-error-boundary", streams[0].stem, streams[0].stem, &metrics, &policy,
			  PCM_ORACLE_RESULT_MAX_ERROR) != 0) {
		fprintf(stderr, "FATAL: maximum-error boundary metric failed\n");
		goto out;
	}
	if (run_rms_error_boundary(&decoded[0], &policy, &metrics) != 0 ||
	    print_metrics("rms-error-boundary", streams[0].stem, streams[0].stem, &metrics, &policy,
			  PCM_ORACLE_RESULT_RMS_ERROR) != 0) {
		fprintf(stderr, "FATAL: RMS-error boundary metric failed\n");
		goto out;
	}
	if (run_correlation_boundary(&metrics) != 0 ||
	    print_metrics("correlation-boundary", streams[0].stem, streams[0].stem, &metrics,
			  &policy, PCM_ORACLE_RESULT_CORRELATION) != 0) {
		fprintf(stderr, "FATAL: correlation boundary metric failed\n");
		goto out;
	}

	for (size_t index = 0U; index < lc3_stateful_recipe_count; index++) {
		const struct lc3_stateful_recipe *recipe = &lc3_stateful_recipes[index];

		if (run_stateful_comparison(argv[1], decoded, sizeof(decoded) / sizeof(decoded[0]),
					    recipe, recipe, &metrics) != 0 ||
		    print_metrics("stateful-valid", recipe->id, recipe->reference_path, &metrics,
				  &policy, PCM_ORACLE_RESULT_PASS) != 0) {
			fprintf(stderr, "FATAL: stateful valid metric failed for %s\n", recipe->id);
			goto out;
		}
	}

	for (size_t index = 0U; index < sizeof(stateful_mutations) / sizeof(stateful_mutations[0]);
	     index++) {
		static const char *const comparisons[] = {
			"stateful-payload-off-by-one",
			"stateful-skip-ignored",
			"stateful-loss-burst-omitted",
			"stateful-wrong-channel",
		};
		static const uint8_t reference_recipes[] = {0U, 6U, 7U, 0U};
		const struct lc3_stateful_recipe *actual_recipe = &stateful_mutations[index];
		const struct lc3_stateful_recipe *reference_recipe =
			&lc3_stateful_recipes[reference_recipes[index]];

		if (run_stateful_comparison(argv[1], decoded, sizeof(decoded) / sizeof(decoded[0]),
					    actual_recipe, reference_recipe, &metrics) != 0 ||
		    print_metrics(comparisons[index], actual_recipe->id,
				  reference_recipe->reference_path, &metrics, &policy,
				  PCM_ORACLE_RESULT_MAX_ERROR) != 0) {
			fprintf(stderr, "FATAL: stateful mutation metric failed for %s\n",
				comparisons[index]);
			goto out;
		}
	}

	if (fflush(stdout) != 0) {
		fprintf(stderr, "FATAL: stdout flush failed\n");
		goto out;
	}
	rc = 0;
out:
	for (size_t index = 0U; index < sizeof(decoded) / sizeof(decoded[0]); index++) {
		free_decoded_stream(&decoded[index]);
	}
	return rc;
}
