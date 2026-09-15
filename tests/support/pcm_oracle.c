/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcm_oracle.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

static int checked_add_u64(uint64_t left, uint64_t right, uint64_t *sum)
{
	if (left > UINT64_MAX - right) {
		return -EOVERFLOW;
	}

	*sum = left + right;
	return 0;
}

static int checked_add_i64(int64_t left, int64_t right, int64_t *sum)
{
	if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right)) {
		return -EOVERFLOW;
	}

	*sum = left + right;
	return 0;
}

static int16_t decode_le16(const uint8_t *bytes)
{
	int32_t value = (int32_t)bytes[0] | ((int32_t)bytes[1] << 8);

	if (value >= 0x8000) {
		value -= 0x10000;
	}

	return (int16_t)value;
}

static uint64_t floor_sqrt_u64(uint64_t value)
{
	uint64_t low = 0U;
	uint64_t high = UINT64_C(1) << 32;

	while (low < high) {
		uint64_t middle = low + (high - low + 1U) / 2U;

		if (middle <= value / middle) {
			low = middle;
		} else {
			high = middle - 1U;
		}
	}

	return low;
}

static uint64_t ceil_sqrt_u64(uint64_t value)
{
	uint64_t floor = floor_sqrt_u64(value);

	if (value == 0U || (floor != 0U && floor == value / floor && value % floor == 0U)) {
		return floor;
	}

	return floor + 1U;
}

int pcm_oracle_init(struct pcm_oracle *oracle)
{
	if (oracle == NULL) {
		return -EINVAL;
	}

	memset(oracle, 0, sizeof(*oracle));
	return 0;
}

int pcm_oracle_accumulate(struct pcm_oracle *oracle, const int16_t *actual, size_t actual_stride,
			  const uint8_t *reference, size_t reference_byte_stride,
			  uint32_t sample_count)
{
	uint64_t squared_error;
	uint64_t actual_energy;
	uint64_t reference_energy;
	int64_t dot_product;
	uint32_t max_abs_error;
	uint32_t sample_index;
	int err;

	if (oracle == NULL || actual == NULL || reference == NULL || sample_count == 0U ||
	    actual_stride == 0U || reference_byte_stride == 0U) {
		return -EINVAL;
	}

	if (oracle->samples > UINT32_MAX - sample_count || oracle->frames == UINT32_MAX) {
		return -EOVERFLOW;
	}

	if ((size_t)(sample_count - 1U) > SIZE_MAX / actual_stride ||
	    (size_t)(sample_count - 1U) > (SIZE_MAX - 1U) / reference_byte_stride) {
		return -EOVERFLOW;
	}

	squared_error = oracle->squared_error;
	actual_energy = oracle->actual_energy_scaled;
	reference_energy = oracle->reference_energy_scaled;
	dot_product = oracle->dot_product_scaled;
	max_abs_error = oracle->max_abs_error;

	for (sample_index = 0U; sample_index < sample_count; sample_index++) {
		size_t actual_offset = (size_t)sample_index * actual_stride;
		size_t reference_offset = (size_t)sample_index * reference_byte_stride;
		int16_t reference_sample = decode_le16(reference + reference_offset);
		int32_t difference = (int32_t)actual[actual_offset] - (int32_t)reference_sample;
		uint32_t absolute_difference =
			(difference < 0) ? (uint32_t)-difference : (uint32_t)difference;
		uint64_t error_square = (uint64_t)absolute_difference * absolute_difference;
		int32_t actual_scaled = (int32_t)actual[actual_offset] / 256;
		int32_t reference_scaled = (int32_t)reference_sample / 256;
		uint64_t actual_square = (uint64_t)(actual_scaled * actual_scaled);
		uint64_t reference_square = (uint64_t)(reference_scaled * reference_scaled);
		int64_t sample_dot = (int64_t)actual_scaled * reference_scaled;

		err = checked_add_u64(squared_error, error_square, &squared_error);
		if (err != 0) {
			return err;
		}
		err = checked_add_u64(actual_energy, actual_square, &actual_energy);
		if (err != 0) {
			return err;
		}
		err = checked_add_u64(reference_energy, reference_square, &reference_energy);
		if (err != 0) {
			return err;
		}
		err = checked_add_i64(dot_product, sample_dot, &dot_product);
		if (err != 0) {
			return err;
		}
		if (absolute_difference > max_abs_error) {
			max_abs_error = absolute_difference;
		}
	}

	oracle->squared_error = squared_error;
	oracle->actual_energy_scaled = actual_energy;
	oracle->reference_energy_scaled = reference_energy;
	oracle->dot_product_scaled = dot_product;
	oracle->samples += sample_count;
	oracle->frames++;
	oracle->max_abs_error = max_abs_error;

	return 0;
}

int pcm_oracle_finalize(const struct pcm_oracle *oracle, struct pcm_oracle_metrics *metrics)
{
	struct pcm_oracle_metrics result;
	uint64_t rms_square;
	uint64_t rms_error;
	uint64_t actual_sqrt;
	uint64_t reference_sqrt;
	uint64_t denominator;
	int64_t numerator;
	int64_t correlation;

	if (oracle == NULL || metrics == NULL) {
		return -EINVAL;
	}

	memset(&result, 0, sizeof(result));
	result.squared_error = oracle->squared_error;
	result.actual_energy_scaled = oracle->actual_energy_scaled;
	result.reference_energy_scaled = oracle->reference_energy_scaled;
	result.dot_product_scaled = oracle->dot_product_scaled;
	result.samples = oracle->samples;
	result.frames = oracle->frames;
	result.max_abs_error = oracle->max_abs_error;

	if (result.samples != 0U) {
		rms_square = result.squared_error / result.samples;
		if (result.squared_error % result.samples != 0U) {
			rms_square++;
		}
		rms_error = ceil_sqrt_u64(rms_square);
		if (rms_error > UINT32_MAX) {
			return -EOVERFLOW;
		}
		result.rms_error = (uint32_t)rms_error;
	}

	if (result.actual_energy_scaled == 0U || result.reference_energy_scaled == 0U) {
		*metrics = result;
		return 0;
	}

	actual_sqrt = floor_sqrt_u64(result.actual_energy_scaled);
	reference_sqrt = floor_sqrt_u64(result.reference_energy_scaled);
	if (actual_sqrt == 0U || reference_sqrt == 0U ||
	    actual_sqrt > UINT64_MAX / reference_sqrt) {
		return -EOVERFLOW;
	}
	denominator = actual_sqrt * reference_sqrt;
	if (denominator > INT64_MAX) {
		return -EOVERFLOW;
	}

	if (result.dot_product_scaled > INT64_MAX / 32768 ||
	    result.dot_product_scaled < INT64_MIN / 32768) {
		return -EOVERFLOW;
	}
	numerator = result.dot_product_scaled * 32768;
	correlation = numerator / (int64_t)denominator;

	if (correlation > INT16_MAX) {
		result.correlation_q15 = INT16_MAX;
	} else if (correlation < INT16_MIN) {
		result.correlation_q15 = INT16_MIN;
	} else {
		result.correlation_q15 = (int32_t)correlation;
	}

	*metrics = result;
	return 0;
}

int pcm_oracle_evaluate(const struct pcm_oracle_metrics *metrics,
			const struct pcm_oracle_limits *limits, enum pcm_oracle_result *result)
{
	enum pcm_oracle_result evaluation;

	if (metrics == NULL || limits == NULL || result == NULL) {
		return -EINVAL;
	}

	if (metrics->samples < limits->min_samples) {
		evaluation = PCM_ORACLE_RESULT_INSUFFICIENT_SAMPLES;
	} else if (metrics->max_abs_error > limits->max_abs_error) {
		evaluation = PCM_ORACLE_RESULT_MAX_ERROR;
	} else if (metrics->rms_error > limits->max_rms_error) {
		evaluation = PCM_ORACLE_RESULT_RMS_ERROR;
	} else if (metrics->actual_energy_scaled == 0U || metrics->reference_energy_scaled == 0U ||
		   metrics->correlation_q15 < limits->min_correlation_q15) {
		evaluation = PCM_ORACLE_RESULT_CORRELATION;
	} else {
		evaluation = PCM_ORACLE_RESULT_PASS;
	}

	*result = evaluation;
	return 0;
}

const char *pcm_oracle_result_name(enum pcm_oracle_result result)
{
	switch (result) {
	case PCM_ORACLE_RESULT_PASS:
		return "pass";
	case PCM_ORACLE_RESULT_INSUFFICIENT_SAMPLES:
		return "insufficient-samples";
	case PCM_ORACLE_RESULT_MAX_ERROR:
		return "max-error";
	case PCM_ORACLE_RESULT_RMS_ERROR:
		return "rms-error";
	case PCM_ORACLE_RESULT_CORRELATION:
		return "correlation";
	default:
		return "invalid";
	}
}
