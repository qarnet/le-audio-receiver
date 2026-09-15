/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-only diagnostic decoder for portable-oracle-manifest.json corpus data.
 * This program prints one JSON metric record per comparison. The Python
 * wrapper validates fixtures and captures host provenance before invoking it.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lc3.h"
#include "pcm_oracle.h"

#define CORPUS_FRAMES         128U
#define MAX_SAMPLES_PER_FRAME 480U

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
			 const struct pcm_oracle_metrics *metrics)
{
	return printf("{\"record\":\"metric\",\"comparison\":\"%s\",\"stem\":\"%s\","
		      "\"reference_stem\":\"%s\",\"squared_error\":%" PRIu64 ","
		      "\"actual_energy_scaled\":%" PRIu64 ",\"reference_energy_scaled\":%" PRIu64
		      ",\"dot_product_scaled\":%" PRId64 ",\"samples\":%" PRIu32
		      ",\"frames\":%" PRIu32 ",\"max_abs_error\":%" PRIu32 ",\"rms_error\":%" PRIu32
		      ",\"correlation_q15\":%" PRId32 "}\n",
		      comparison, stem, reference_stem, metrics->squared_error,
		      metrics->actual_energy_scaled, metrics->reference_energy_scaled,
		      metrics->dot_product_scaled, metrics->samples, metrics->frames,
		      metrics->max_abs_error, metrics->rms_error, metrics->correlation_q15) < 0
		       ? -1
		       : 0;
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
	int rc = 1;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s FIXTURE_DIRECTORY\n", argv[0]);
		return 1;
	}
	memset(decoded, 0, sizeof(decoded));

	for (size_t index = 0U; index < sizeof(streams) / sizeof(streams[0]); index++) {
		if (decode_stream(argv[1], &streams[index], &decoded[index],
				  &valid_metrics[index]) != 0) {
			goto out;
		}
		if (print_metrics("valid", streams[index].stem, streams[index].stem,
				  &valid_metrics[index]) != 0) {
			fprintf(stderr, "FATAL: valid metric failed for %s\n", streams[index].stem);
			goto out;
		}
	}

	for (size_t index = 0U; index < sizeof(streams) / sizeof(streams[0]); index += 2U) {
		if (compare_frames(&decoded[index], 0U, &decoded[index + 1U], 0U, CORPUS_FRAMES,
				   COMPARISON_ACTUAL, &metrics) != 0 ||
		    print_metrics("channel-swap", streams[index].stem, streams[index + 1U].stem,
				  &metrics) != 0) {
			fprintf(stderr, "FATAL: channel-swap metric failed for %s\n",
				streams[index].stem);
			goto out;
		}
	}

	for (size_t index = 0U; index < sizeof(streams) / sizeof(streams[0]); index++) {
		if (compare_frames(&decoded[index], 1U, &decoded[index], 0U, CORPUS_FRAMES - 1U,
				   COMPARISON_ACTUAL, &metrics) != 0 ||
		    print_metrics("prior-frame-shift", streams[index].stem, streams[index].stem,
				  &metrics) != 0) {
			fprintf(stderr, "FATAL: prior-frame metric failed for %s\n",
				streams[index].stem);
			goto out;
		}
		if (compare_frames(&decoded[index], 0U, &decoded[index], 1U, CORPUS_FRAMES - 1U,
				   COMPARISON_ACTUAL, &metrics) != 0 ||
		    print_metrics("next-frame-shift", streams[index].stem, streams[index].stem,
				  &metrics) != 0) {
			fprintf(stderr, "FATAL: next-frame metric failed for %s\n",
				streams[index].stem);
			goto out;
		}
		if (compare_frames(&decoded[index], 0U, &decoded[index], 0U, CORPUS_FRAMES,
				   COMPARISON_DEAD, &metrics) != 0 ||
		    print_metrics("dead-channel", streams[index].stem, streams[index].stem,
				  &metrics) != 0) {
			fprintf(stderr, "FATAL: dead-channel metric failed for %s\n",
				streams[index].stem);
			goto out;
		}
		if (compare_frames(&decoded[index], 0U, &decoded[index], 0U, CORPUS_FRAMES,
				   COMPARISON_SYNTHETIC, &metrics) != 0 ||
		    print_metrics("low-correlation-synthetic", streams[index].stem,
				  streams[index].stem, &metrics) != 0) {
			fprintf(stderr, "FATAL: synthetic metric failed for %s\n",
				streams[index].stem);
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
