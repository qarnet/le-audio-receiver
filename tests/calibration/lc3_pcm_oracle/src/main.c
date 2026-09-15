/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * PB-031 ARM calibration image. This is diagnostic-only and has no
 * production receiver dependencies.
 */

#include <zephyr/kernel.h>
#include <zephyr/debug/thread_analyzer.h>
#include <zephyr/sys/printk.h>

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <lc3.h>

#include "pb031_arm_build_info.h"
#include "pcm_oracle.h"

#define CORPUS_FRAMES         128U
#define MAX_SAMPLES_PER_FRAME 480U
#define METRIC_RECORD_COUNT   22U
#define OUTPUT_LINE_SIZE      768U

struct corpus_stream {
	const char *stem;
	uint32_t duration_us;
	uint32_t frequency_hz;
	uint32_t frame_bytes;
	uint32_t samples_per_frame;
	uint32_t frame_count;
	const uint8_t *lc3;
	size_t lc3_size;
	const uint8_t *pcm;
	size_t pcm_size;
};

struct calibration_failure {
	const char *stage;
	const char *stream;
	int code;
};

enum comparison_input {
	COMPARISON_DEAD,
	COMPARISON_SYNTHETIC,
};

static const uint8_t bsim_48k_10ms_120b_l_lc3[] = {
#include "bsim_48k_10ms_120b_l_lc3.inc"
};

static const uint8_t bsim_48k_10ms_120b_l_pcm[] = {
#include "bsim_48k_10ms_120b_l_pcm.inc"
};

static const uint8_t bsim_48k_10ms_120b_r_lc3[] = {
#include "bsim_48k_10ms_120b_r_lc3.inc"
};

static const uint8_t bsim_48k_10ms_120b_r_pcm[] = {
#include "bsim_48k_10ms_120b_r_pcm.inc"
};

static const uint8_t bsim_48k_7p5ms_90b_l_lc3[] = {
#include "bsim_48k_7p5ms_90b_l_lc3.inc"
};

static const uint8_t bsim_48k_7p5ms_90b_l_pcm[] = {
#include "bsim_48k_7p5ms_90b_l_pcm.inc"
};

static const uint8_t bsim_48k_7p5ms_90b_r_lc3[] = {
#include "bsim_48k_7p5ms_90b_r_lc3.inc"
};

static const uint8_t bsim_48k_7p5ms_90b_r_pcm[] = {
#include "bsim_48k_7p5ms_90b_r_pcm.inc"
};

_Static_assert(sizeof(bsim_48k_10ms_120b_l_lc3) == CORPUS_FRAMES * 120U,
	       "10 ms left LC3 geometry mismatch");
_Static_assert(sizeof(bsim_48k_10ms_120b_l_pcm) == CORPUS_FRAMES * 480U * 2U,
	       "10 ms left PCM geometry mismatch");
_Static_assert(sizeof(bsim_48k_10ms_120b_r_lc3) == CORPUS_FRAMES * 120U,
	       "10 ms right LC3 geometry mismatch");
_Static_assert(sizeof(bsim_48k_10ms_120b_r_pcm) == CORPUS_FRAMES * 480U * 2U,
	       "10 ms right PCM geometry mismatch");
_Static_assert(sizeof(bsim_48k_7p5ms_90b_l_lc3) == CORPUS_FRAMES * 90U,
	       "7.5 ms left LC3 geometry mismatch");
_Static_assert(sizeof(bsim_48k_7p5ms_90b_l_pcm) == CORPUS_FRAMES * 360U * 2U,
	       "7.5 ms left PCM geometry mismatch");
_Static_assert(sizeof(bsim_48k_7p5ms_90b_r_lc3) == CORPUS_FRAMES * 90U,
	       "7.5 ms right LC3 geometry mismatch");
_Static_assert(sizeof(bsim_48k_7p5ms_90b_r_pcm) == CORPUS_FRAMES * 360U * 2U,
	       "7.5 ms right PCM geometry mismatch");

static const struct corpus_stream streams[] = {
	{
		.stem = "bsim_48k_10ms_120b_l",
		.duration_us = 10000U,
		.frequency_hz = 48000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.frame_count = CORPUS_FRAMES,
		.lc3 = bsim_48k_10ms_120b_l_lc3,
		.lc3_size = sizeof(bsim_48k_10ms_120b_l_lc3),
		.pcm = bsim_48k_10ms_120b_l_pcm,
		.pcm_size = sizeof(bsim_48k_10ms_120b_l_pcm),
	},
	{
		.stem = "bsim_48k_10ms_120b_r",
		.duration_us = 10000U,
		.frequency_hz = 48000U,
		.frame_bytes = 120U,
		.samples_per_frame = 480U,
		.frame_count = CORPUS_FRAMES,
		.lc3 = bsim_48k_10ms_120b_r_lc3,
		.lc3_size = sizeof(bsim_48k_10ms_120b_r_lc3),
		.pcm = bsim_48k_10ms_120b_r_pcm,
		.pcm_size = sizeof(bsim_48k_10ms_120b_r_pcm),
	},
	{
		.stem = "bsim_48k_7p5ms_90b_l",
		.duration_us = 7500U,
		.frequency_hz = 48000U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
		.frame_count = CORPUS_FRAMES,
		.lc3 = bsim_48k_7p5ms_90b_l_lc3,
		.lc3_size = sizeof(bsim_48k_7p5ms_90b_l_lc3),
		.pcm = bsim_48k_7p5ms_90b_l_pcm,
		.pcm_size = sizeof(bsim_48k_7p5ms_90b_l_pcm),
	},
	{
		.stem = "bsim_48k_7p5ms_90b_r",
		.duration_us = 7500U,
		.frequency_hz = 48000U,
		.frame_bytes = 90U,
		.samples_per_frame = 360U,
		.frame_count = CORPUS_FRAMES,
		.lc3 = bsim_48k_7p5ms_90b_r_lc3,
		.lc3_size = sizeof(bsim_48k_7p5ms_90b_r_lc3),
		.pcm = bsim_48k_7p5ms_90b_r_pcm,
		.pcm_size = sizeof(bsim_48k_7p5ms_90b_r_pcm),
	},
};

static lc3_decoder_mem_48k_t decoder_memory;
static int16_t frame_buffer[MAX_SAMPLES_PER_FRAME];
static char output_line[OUTPUT_LINE_SIZE];

static int set_failure(struct calibration_failure *failure, const char *stage, const char *stream,
		       int code)
{
	failure->stage = stage;
	failure->stream = stream;
	failure->code = code;

	return code;
}

static int validate_stream(const struct corpus_stream *stream)
{
	size_t expected_lc3_size;
	size_t expected_pcm_size;

	if (stream == NULL || stream->stem == NULL || stream->lc3 == NULL || stream->pcm == NULL ||
	    stream->frame_count != CORPUS_FRAMES || stream->frequency_hz != 48000U) {
		return -EINVAL;
	}

	if (stream->duration_us == 10000U) {
		if (stream->frame_bytes != 120U || stream->samples_per_frame != 480U) {
			return -EINVAL;
		}
	} else if (stream->duration_us == 7500U) {
		if (stream->frame_bytes != 90U || stream->samples_per_frame != 360U) {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	expected_lc3_size = (size_t)stream->frame_count * stream->frame_bytes;
	expected_pcm_size = (size_t)stream->frame_count * stream->samples_per_frame * 2U;
	if (stream->lc3_size != expected_lc3_size || stream->pcm_size != expected_pcm_size) {
		return -EINVAL;
	}

	return 0;
}

static int setup_decoder(const struct corpus_stream *stream, lc3_decoder_t *decoder)
{
	memset(&decoder_memory, 0, sizeof(decoder_memory));
	*decoder = lc3_setup_decoder((int)stream->duration_us, (int)stream->frequency_hz, 0,
				     &decoder_memory);

	return (*decoder == NULL) ? -EINVAL : 0;
}

static int run_decoded_comparison(const struct corpus_stream *actual_stream,
				  uint32_t actual_start_frame,
				  const struct corpus_stream *reference_stream,
				  uint32_t reference_start_frame, uint32_t frame_count,
				  struct pcm_oracle_metrics *metrics,
				  struct calibration_failure *failure)
{
	lc3_decoder_t decoder;
	struct pcm_oracle oracle;
	uint32_t actual_end_frame;
	uint32_t frame;
	int err;

	if (actual_stream == NULL || reference_stream == NULL || metrics == NULL ||
	    actual_stream->samples_per_frame != reference_stream->samples_per_frame ||
	    actual_start_frame > actual_stream->frame_count ||
	    reference_start_frame > reference_stream->frame_count ||
	    frame_count > actual_stream->frame_count - actual_start_frame ||
	    frame_count > reference_stream->frame_count - reference_start_frame) {
		return set_failure(failure, "comparison-geometry", "none", -EINVAL);
	}

	err = setup_decoder(actual_stream, &decoder);
	if (err != 0) {
		return set_failure(failure, "decoder-setup", actual_stream->stem, err);
	}
	err = pcm_oracle_init(&oracle);
	if (err != 0) {
		return set_failure(failure, "comparator-init", actual_stream->stem, err);
	}

	actual_end_frame = actual_start_frame + frame_count;
	for (frame = 0U; frame < actual_end_frame; frame++) {
		const uint8_t *lc3_frame =
			actual_stream->lc3 + (size_t)frame * actual_stream->frame_bytes;

		err = lc3_decode(decoder, lc3_frame, (int)actual_stream->frame_bytes,
				 LC3_PCM_FORMAT_S16, frame_buffer, 1);
		if (err != 0) {
			return set_failure(failure, "decode", actual_stream->stem, err);
		}
		if (frame >= actual_start_frame) {
			const uint8_t *reference =
				reference_stream->pcm +
				(size_t)(reference_start_frame + frame - actual_start_frame) *
					reference_stream->samples_per_frame * 2U;

			err = pcm_oracle_accumulate(&oracle, frame_buffer, 1U, reference, 2U,
						    actual_stream->samples_per_frame);
			if (err != 0) {
				return set_failure(failure, "accumulate", actual_stream->stem, err);
			}
		}
	}

	err = pcm_oracle_finalize(&oracle, metrics);
	if (err != 0) {
		return set_failure(failure, "finalize", actual_stream->stem, err);
	}

	return 0;
}

static int run_static_comparison(const struct corpus_stream *stream, enum comparison_input input,
				 struct pcm_oracle_metrics *metrics,
				 struct calibration_failure *failure)
{
	struct pcm_oracle oracle;
	uint32_t frame;
	int err;

	if (stream == NULL || metrics == NULL) {
		return set_failure(failure, "comparison-geometry", "none", -EINVAL);
	}
	err = pcm_oracle_init(&oracle);
	if (err != 0) {
		return set_failure(failure, "comparator-init", stream->stem, err);
	}

	for (frame = 0U; frame < stream->frame_count; frame++) {
		const uint8_t *reference =
			stream->pcm + (size_t)frame * stream->samples_per_frame * 2U;

		if (input == COMPARISON_DEAD) {
			memset(frame_buffer, 0,
			       stream->samples_per_frame * sizeof(frame_buffer[0]));
		} else {
			uint32_t sample;

			for (sample = 0U; sample < stream->samples_per_frame; sample++) {
				uint32_t corpus_sample = frame * stream->samples_per_frame + sample;

				frame_buffer[sample] =
					(corpus_sample & 1U) == 0U ? INT16_MIN : INT16_MAX;
			}
		}

		err = pcm_oracle_accumulate(&oracle, frame_buffer, 1U, reference, 2U,
					    stream->samples_per_frame);
		if (err != 0) {
			return set_failure(failure, "accumulate", stream->stem, err);
		}
	}

	err = pcm_oracle_finalize(&oracle, metrics);
	if (err != 0) {
		return set_failure(failure, "finalize", stream->stem, err);
	}

	return 0;
}

static int emit_line(const char *line)
{
	printk("%s\n", line);
	return 0;
}

static int emit_begin(void)
{
	int written = snprintf(output_line, sizeof(output_line),
			       "PB031_ARM_BEGIN schema=1 manifest_sha256=%s ncs=%s liblc3=%s",
			       PB031_MANIFEST_SHA256, PB031_NCS_VERSION, PB031_LIBLC3_REVISION);

	if (written < 0 || (size_t)written >= sizeof(output_line)) {
		return -ENOSPC;
	}

	return emit_line(output_line);
}

static int emit_source(void)
{
	int written =
		snprintf(output_line, sizeof(output_line),
			 "PB031_ARM_SOURCE main_c_sha256=%s pcm_oracle_c_sha256=%s "
			 "pcm_oracle_h_sha256=%s",
			 PB031_MAIN_C_SHA256, PB031_PCM_ORACLE_C_SHA256, PB031_PCM_ORACLE_H_SHA256);

	if (written < 0 || (size_t)written >= sizeof(output_line)) {
		return -ENOSPC;
	}

	return emit_line(output_line);
}

static int emit_metric(const char *comparison, const struct corpus_stream *stream,
		       const struct corpus_stream *reference_stream,
		       const struct pcm_oracle_metrics *metrics)
{
	int written = snprintf(
		output_line, sizeof(output_line),
		"PB031_METRIC {\"record\":\"metric\",\"comparison\":\"%s\","
		"\"stem\":\"%s\",\"reference_stem\":\"%s\","
		"\"squared_error\":%" PRIu64 ",\"actual_energy_scaled\":%" PRIu64
		",\"reference_energy_scaled\":%" PRIu64 ",\"dot_product_scaled\":%" PRId64
		",\"samples\":%" PRIu32 ",\"frames\":%" PRIu32 ",\"max_abs_error\":%" PRIu32
		",\"rms_error\":%" PRIu32 ",\"correlation_q15\":%" PRId32 "}",
		comparison, stream->stem, reference_stream->stem, metrics->squared_error,
		metrics->actual_energy_scaled, metrics->reference_energy_scaled,
		metrics->dot_product_scaled, metrics->samples, metrics->frames,
		metrics->max_abs_error, metrics->rms_error, metrics->correlation_q15);

	if (written < 0 || (size_t)written >= sizeof(output_line)) {
		return -ENOSPC;
	}

	return emit_line(output_line);
}

static void emit_failure(const struct calibration_failure *failure)
{
	int written = snprintf(output_line, sizeof(output_line),
			       "PB031_ARM_FAIL stage=%s stream=%s code=%d", failure->stage,
			       failure->stream, failure->code);

	if (written < 0 || (size_t)written >= sizeof(output_line)) {
		(void)emit_line("PB031_ARM_FAIL stage=output stream=none code=-28");
		return;
	}

	(void)emit_line(output_line);
}

static int emit_record(const char *comparison, const struct corpus_stream *stream,
		       const struct corpus_stream *reference_stream,
		       const struct pcm_oracle_metrics *metrics, uint32_t *metric_count,
		       struct calibration_failure *failure)
{
	int err = emit_metric(comparison, stream, reference_stream, metrics);

	if (err != 0) {
		return set_failure(failure, "metric-format", stream->stem, err);
	}

	(*metric_count)++;
	return 0;
}

int main(void)
{
	struct calibration_failure failure = {
		.stage = "init",
		.stream = "none",
		.code = -EIO,
	};
	struct pcm_oracle_metrics metrics;
	uint32_t metric_count = 0U;
	size_t index;
	int err;

	err = emit_begin();
	if (err != 0) {
		(void)set_failure(&failure, "begin", "none", err);
		goto fail;
	}
	err = emit_source();
	if (err != 0) {
		(void)set_failure(&failure, "source", "none", err);
		goto fail;
	}

	for (index = 0U; index < ARRAY_SIZE(streams); index++) {
		err = validate_stream(&streams[index]);
		if (err != 0) {
			(void)set_failure(&failure, "geometry", streams[index].stem, err);
			goto fail;
		}
	}

	for (index = 0U; index < ARRAY_SIZE(streams); index++) {
		err = run_decoded_comparison(&streams[index], 0U, &streams[index], 0U,
					     CORPUS_FRAMES, &metrics, &failure);
		if (err != 0) {
			goto fail;
		}
		err = emit_record("valid", &streams[index], &streams[index], &metrics,
				  &metric_count, &failure);
		if (err != 0) {
			goto fail;
		}
	}

	for (index = 0U; index < ARRAY_SIZE(streams); index += 2U) {
		err = run_decoded_comparison(&streams[index], 0U, &streams[index + 1U], 0U,
					     CORPUS_FRAMES, &metrics, &failure);
		if (err != 0) {
			goto fail;
		}
		err = emit_record("channel-swap", &streams[index], &streams[index + 1U], &metrics,
				  &metric_count, &failure);
		if (err != 0) {
			goto fail;
		}
	}

	for (index = 0U; index < ARRAY_SIZE(streams); index++) {
		err = run_decoded_comparison(&streams[index], 1U, &streams[index], 0U,
					     CORPUS_FRAMES - 1U, &metrics, &failure);
		if (err != 0) {
			goto fail;
		}
		err = emit_record("prior-frame-shift", &streams[index], &streams[index], &metrics,
				  &metric_count, &failure);
		if (err != 0) {
			goto fail;
		}

		err = run_decoded_comparison(&streams[index], 0U, &streams[index], 1U,
					     CORPUS_FRAMES - 1U, &metrics, &failure);
		if (err != 0) {
			goto fail;
		}
		err = emit_record("next-frame-shift", &streams[index], &streams[index], &metrics,
				  &metric_count, &failure);
		if (err != 0) {
			goto fail;
		}

		err = run_static_comparison(&streams[index], COMPARISON_DEAD, &metrics, &failure);
		if (err != 0) {
			goto fail;
		}
		err = emit_record("dead-channel", &streams[index], &streams[index], &metrics,
				  &metric_count, &failure);
		if (err != 0) {
			goto fail;
		}

		err = run_static_comparison(&streams[index], COMPARISON_SYNTHETIC, &metrics,
					    &failure);
		if (err != 0) {
			goto fail;
		}
		err = emit_record("low-correlation-synthetic", &streams[index], &streams[index],
				  &metrics, &metric_count, &failure);
		if (err != 0) {
			goto fail;
		}
	}

	if (metric_count != METRIC_RECORD_COUNT) {
		(void)set_failure(&failure, "metric-count", "none", -EIO);
		goto fail;
	}

	thread_analyzer_print(0U);
	printk("PB031_ARM_PASS metrics=22\n");
	return 0;

fail:
	emit_failure(&failure);
	return 0;
}
