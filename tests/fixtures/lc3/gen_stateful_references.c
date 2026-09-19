/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-only generator for checked-in LC3 decoder-history PCM references.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lc3.h"
#include "lc3_stateful_recipes.h"

#define CORPUS_FRAMES         128U
#define MAX_SAMPLES_PER_FRAME 480U

static char *path_for(const char *directory, const char *name)
{
	size_t directory_length;
	size_t name_length;
	char *path;

	if (directory == NULL || name == NULL) {
		return NULL;
	}
	directory_length = strlen(directory);
	name_length = strlen(name);
	if (directory_length == 0U || name_length == 0U ||
	    directory_length > SIZE_MAX - name_length - 2U) {
		return NULL;
	}

	path = malloc(directory_length + name_length + 2U);
	if (path == NULL) {
		return NULL;
	}
	(void)snprintf(path, directory_length + name_length + 2U, "%s/%s", directory, name);
	return path;
}

static char *source_path_for(const char *directory, const char *stem)
{
	size_t directory_length;
	size_t stem_length;
	char *path;

	if (directory == NULL || stem == NULL) {
		return NULL;
	}
	directory_length = strlen(directory);
	stem_length = strlen(stem);
	if (directory_length == 0U || stem_length == 0U ||
	    directory_length > SIZE_MAX - stem_length - sizeof("/.lc3")) {
		return NULL;
	}

	path = malloc(directory_length + stem_length + sizeof("/.lc3"));
	if (path == NULL) {
		return NULL;
	}
	(void)snprintf(path, directory_length + stem_length + sizeof("/.lc3"), "%s/%s.lc3",
		       directory, stem);
	return path;
}

static int read_exact_file(const char *path, uint8_t **bytes, size_t size)
{
	FILE *file = NULL;
	uint8_t *buffer = NULL;
	int trailing;
	int rc = -1;

	if (path == NULL || bytes == NULL || size == 0U) {
		return -1;
	}
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
	if (fread(buffer, 1U, size, file) != size) {
		fprintf(stderr, "FATAL: short read from %s\n", path);
		goto out;
	}
	trailing = fgetc(file);
	if (trailing != EOF || ferror(file)) {
		fprintf(stderr, "FATAL: invalid trailing data in %s\n", path);
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

static int write_le16(FILE *file, int16_t sample)
{
	uint16_t value = (uint16_t)sample;
	uint8_t bytes[2] = {
		(uint8_t)(value & 0xFFU),
		(uint8_t)(value >> 8),
	};

	return fwrite(bytes, 1U, sizeof(bytes), file) == sizeof(bytes) ? 0 : -1;
}

static int generate_recipe(const char *source_directory, const char *output_directory,
			   const struct lc3_stateful_recipe *recipe)
{
	char *source_path = NULL;
	char *output_path = NULL;
	uint8_t *source = NULL;
	FILE *output = NULL;
	lc3_decoder_mem_48k_t decoder_memory;
	lc3_decoder_t decoder;
	int16_t pcm[MAX_SAMPLES_PER_FRAME];
	size_t source_size;
	size_t expected_trace_size;
	size_t written = 0U;
	int rc = -1;

	if (recipe == NULL || recipe->reference_kind != LC3_STATEFUL_REFERENCE_GENERATED_PCM ||
	    recipe->samples_per_frame == 0U || recipe->samples_per_frame > MAX_SAMPLES_PER_FRAME ||
	    recipe->frame_bytes == 0U ||
	    (size_t)recipe->valid_frame_count >
		    SIZE_MAX / ((size_t)recipe->samples_per_frame * sizeof(int16_t))) {
		fprintf(stderr, "FATAL: invalid recipe geometry\n");
		goto out;
	}
	source_size = (size_t)recipe->frame_bytes * CORPUS_FRAMES;
	expected_trace_size =
		(size_t)recipe->valid_frame_count * recipe->samples_per_frame * sizeof(int16_t);
	source_path = source_path_for(source_directory, recipe->source_stem);
	output_path = path_for(output_directory, recipe->reference_path);
	if (source_path == NULL || output_path == NULL) {
		fprintf(stderr, "FATAL: path allocation failed for %s\n", recipe->id);
		goto out;
	}
	if (read_exact_file(source_path, &source, source_size) != 0) {
		goto out;
	}
	output = fopen(output_path, "wb");
	if (output == NULL) {
		fprintf(stderr, "FATAL: cannot create %s\n", output_path);
		goto out;
	}
	decoder = lc3_setup_decoder((int)recipe->duration_us, 48000, 0, &decoder_memory);
	if (decoder == NULL) {
		fprintf(stderr, "FATAL: decoder setup failed for %s\n", recipe->id);
		goto out;
	}

	for (size_t step_index = 0U; step_index < recipe->step_count; step_index++) {
		const struct lc3_stateful_step *step = &recipe->steps[step_index];

		for (uint16_t action_index = 0U; action_index < step->count; action_index++) {
			int decode_result;

			if (step->action == LC3_STATEFUL_ACTION_PLC) {
				decode_result =
					lc3_decode(decoder, NULL, 0, LC3_PCM_FORMAT_S16, pcm, 1);
				if (decode_result != 1) {
					fprintf(stderr, "FATAL: PLC decode failed for %s\n",
						recipe->id);
					goto out;
				}
				continue;
			}

			decode_result =
				lc3_decode(decoder,
					   source + (size_t)(step->first_sequence + action_index) *
							    recipe->frame_bytes,
					   recipe->frame_bytes, LC3_PCM_FORMAT_S16, pcm, 1);
			if (decode_result != 0) {
				fprintf(stderr, "FATAL: corpus decode failed for %s\n", recipe->id);
				goto out;
			}
			for (uint16_t sample = 0U; sample < recipe->samples_per_frame; sample++) {
				if (write_le16(output, pcm[sample]) != 0) {
					fprintf(stderr, "FATAL: PCM write failed for %s\n",
						recipe->id);
					goto out;
				}
			}
			written += (size_t)recipe->samples_per_frame * sizeof(int16_t);
		}
	}

	if (written != expected_trace_size || fflush(output) != 0) {
		fprintf(stderr, "FATAL: trace size or flush failed for %s\n", recipe->id);
		goto out;
	}
	rc = 0;
out:
	if (output != NULL && fclose(output) != 0) {
		fprintf(stderr, "FATAL: trace close failed for %s\n", recipe->id);
		rc = -1;
	}
	free(source_path);
	free(output_path);
	free(source);
	return rc;
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "Usage: %s SOURCE_DIRECTORY OUTPUT_DIRECTORY\n", argv[0]);
		return 1;
	}
	if (!lc3_stateful_recipes_validate(lc3_stateful_recipes, lc3_stateful_recipe_count)) {
		fprintf(stderr, "FATAL: stateful recipe table is invalid\n");
		return 1;
	}

	for (size_t index = 0U; index < lc3_stateful_recipe_count; index++) {
		if (lc3_stateful_recipes[index].reference_kind !=
		    LC3_STATEFUL_REFERENCE_GENERATED_PCM) {
			continue;
		}
		if (generate_recipe(argv[1], argv[2], &lc3_stateful_recipes[index]) != 0) {
			return 1;
		}
	}

	return 0;
}
