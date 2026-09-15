/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-only incremental integer PCM comparison engine.
 */

#ifndef LE_AUDIO_RECEIVER_PCM_ORACLE_H_
#define LE_AUDIO_RECEIVER_PCM_ORACLE_H_

#include <stddef.h>
#include <stdint.h>

struct pcm_oracle_metrics {
	uint64_t squared_error;
	uint64_t actual_energy_scaled;
	uint64_t reference_energy_scaled;
	int64_t dot_product_scaled;
	uint32_t samples;
	uint32_t frames;
	uint32_t max_abs_error;
	uint32_t rms_error;
	int32_t correlation_q15;
};

struct pcm_oracle_limits {
	uint32_t min_samples;
	uint32_t max_abs_error;
	uint32_t max_rms_error;
	int32_t min_correlation_q15;
};

enum pcm_oracle_result {
	PCM_ORACLE_RESULT_PASS,
	PCM_ORACLE_RESULT_INSUFFICIENT_SAMPLES,
	PCM_ORACLE_RESULT_MAX_ERROR,
	PCM_ORACLE_RESULT_RMS_ERROR,
	PCM_ORACLE_RESULT_CORRELATION,
};

/* Raw incremental accumulators. Derived RMS and correlation values are
 * returned by pcm_oracle_finalize().
 */
struct pcm_oracle {
	uint64_t squared_error;
	uint64_t actual_energy_scaled;
	uint64_t reference_energy_scaled;
	int64_t dot_product_scaled;
	uint32_t samples;
	uint32_t frames;
	uint32_t max_abs_error;
};

int pcm_oracle_init(struct pcm_oracle *oracle);

/* actual_stride is measured in int16_t samples. reference_byte_stride is
 * measured in bytes. A successful call adds one frame.
 */
int pcm_oracle_accumulate(struct pcm_oracle *oracle, const int16_t *actual, size_t actual_stride,
			  const uint8_t *reference, size_t reference_byte_stride,
			  uint32_t sample_count);

int pcm_oracle_finalize(const struct pcm_oracle *oracle, struct pcm_oracle_metrics *metrics);

int pcm_oracle_evaluate(const struct pcm_oracle_metrics *metrics,
			const struct pcm_oracle_limits *limits, enum pcm_oracle_result *result);

const char *pcm_oracle_result_name(enum pcm_oracle_result result);

#endif /* LE_AUDIO_RECEIVER_PCM_ORACLE_H_ */
