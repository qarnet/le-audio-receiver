/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "pcm_oracle_test_limits.h"
#include "pcm_oracle.h"

ZTEST_SUITE(pcm_oracle, NULL, NULL, NULL, NULL, NULL);

static void write_reference(uint8_t *bytes, size_t byte_stride, const int16_t *samples,
			    size_t count)
{
	for (size_t index = 0U; index < count; index++) {
		uint16_t value = (uint16_t)samples[index];
		size_t offset = index * byte_stride;

		bytes[offset] = (uint8_t)(value & 0xFFU);
		bytes[offset + 1U] = (uint8_t)(value >> 8);
	}
}

static void assert_evaluation(const struct pcm_oracle_metrics *metrics,
			      const struct pcm_oracle_limits *limits,
			      enum pcm_oracle_result expected)
{
	enum pcm_oracle_result result = PCM_ORACLE_RESULT_PASS;

	zassert_ok(pcm_oracle_evaluate(metrics, limits, &result), "evaluation failed");
	zassert_equal(result, expected, "evaluation result");
}

ZTEST(pcm_oracle, test_exact_little_endian_positive_negative_strides)
{
	struct pcm_oracle oracle;
	struct pcm_oracle_metrics metrics;
	const struct pcm_oracle_limits limits = {
		.min_samples = 4U,
		.max_abs_error = 0U,
		.max_rms_error = 0U,
		.min_correlation_q15 = INT16_MAX,
	};
	int16_t actual[] = {256, 11, -512, 11, 768, 11, -1024, 11};
	int16_t expected[] = {256, -512, 768, -1024};
	uint8_t reference[12];

	memset(reference, 0xA5, sizeof(reference));
	write_reference(reference, 3U, expected, ARRAY_SIZE(expected));

	zassert_ok(pcm_oracle_init(&oracle), "init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 2U, reference, 3U, 4U), "accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "finalize");

	zassert_equal(metrics.squared_error, 0U, "zero squared error");
	zassert_equal(metrics.max_abs_error, 0U, "zero maximum error");
	zassert_equal(metrics.rms_error, 0U, "zero RMS error");
	zassert_equal(metrics.actual_energy_scaled, 30U, "actual energy");
	zassert_equal(metrics.reference_energy_scaled, 30U, "reference energy");
	zassert_equal(metrics.dot_product_scaled, 30, "dot product");
	zassert_equal(metrics.correlation_q15, INT16_MAX, "correlation");
	zassert_equal(metrics.samples, 4U, "sample count");
	zassert_equal(metrics.frames, 1U, "frame count");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_PASS);
}

ZTEST(pcm_oracle, test_small_bounded_differences_match_hand_calculation)
{
	struct pcm_oracle oracle;
	struct pcm_oracle_metrics metrics;
	const struct pcm_oracle_limits limits = {
		.min_samples = 4U,
		.max_abs_error = 256U,
		.max_rms_error = 256U,
		.min_correlation_q15 = INT16_MAX,
	};
	int16_t actual[] = {256, 512, -256, -512};
	int16_t expected[] = {0, 256, -512, -256};
	uint8_t reference[8];

	write_reference(reference, 2U, expected, ARRAY_SIZE(expected));
	zassert_ok(pcm_oracle_init(&oracle), "init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, 4U), "accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "finalize");

	zassert_equal(metrics.squared_error, 262144U, "squared error");
	zassert_equal(metrics.actual_energy_scaled, 10U, "actual energy");
	zassert_equal(metrics.reference_energy_scaled, 6U, "reference energy");
	zassert_equal(metrics.dot_product_scaled, 6, "dot product");
	zassert_equal(metrics.max_abs_error, 256U, "maximum error");
	zassert_equal(metrics.rms_error, 256U, "conservative RMS");
	zassert_equal(metrics.correlation_q15, INT16_MAX, "clamped correlation");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_PASS);
}

ZTEST(pcm_oracle, test_outlier_fails_maximum_error_by_name)
{
	struct pcm_oracle oracle;
	struct pcm_oracle_metrics metrics;
	const struct pcm_oracle_limits limits = {
		.min_samples = 3U,
		.max_abs_error = 999U,
		.max_rms_error = UINT32_MAX,
		.min_correlation_q15 = INT32_MIN,
	};
	int16_t actual[] = {0, 1000, 0};
	int16_t expected[] = {0, 0, 0};
	uint8_t reference[6];
	enum pcm_oracle_result result;

	write_reference(reference, 2U, expected, ARRAY_SIZE(expected));
	zassert_ok(pcm_oracle_init(&oracle), "init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, 3U), "accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "finalize");
	zassert_ok(pcm_oracle_evaluate(&metrics, &limits, &result), "evaluate");
	zassert_equal(result, PCM_ORACLE_RESULT_MAX_ERROR, "outlier result");
	zassert_equal(strcmp(pcm_oracle_result_name(result), "max-error"), 0, "outlier name");
}

ZTEST(pcm_oracle, test_distributed_differences_fail_rms_by_name)
{
	struct pcm_oracle oracle;
	struct pcm_oracle_metrics metrics;
	const struct pcm_oracle_limits limits = {
		.min_samples = 4U,
		.max_abs_error = 2U,
		.max_rms_error = 1U,
		.min_correlation_q15 = INT32_MIN,
	};
	int16_t actual[] = {2, 2, 2, 2};
	int16_t expected[] = {0, 0, 0, 0};
	uint8_t reference[8];
	enum pcm_oracle_result result;

	write_reference(reference, 2U, expected, ARRAY_SIZE(expected));
	zassert_ok(pcm_oracle_init(&oracle), "init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, 4U), "accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "finalize");
	zassert_ok(pcm_oracle_evaluate(&metrics, &limits, &result), "evaluate");
	zassert_equal(metrics.rms_error, 2U, "RMS");
	zassert_equal(result, PCM_ORACLE_RESULT_RMS_ERROR, "distributed result");
	zassert_equal(strcmp(pcm_oracle_result_name(result), "rms-error"), 0, "distributed name");
}

ZTEST(pcm_oracle, test_negated_waveform_fails_correlation_by_name)
{
	struct pcm_oracle oracle;
	struct pcm_oracle_metrics metrics;
	const struct pcm_oracle_limits limits = {
		.min_samples = 4U,
		.max_abs_error = UINT32_MAX,
		.max_rms_error = UINT32_MAX,
		.min_correlation_q15 = 0,
	};
	int16_t actual[] = {256, 512, -256, -512};
	int16_t expected[] = {-256, -512, 256, 512};
	uint8_t reference[8];
	enum pcm_oracle_result result;

	write_reference(reference, 2U, expected, ARRAY_SIZE(expected));
	zassert_ok(pcm_oracle_init(&oracle), "init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, 4U), "accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "finalize");
	zassert_ok(pcm_oracle_evaluate(&metrics, &limits, &result), "evaluate");
	zassert_equal(metrics.correlation_q15, INT16_MIN, "negative correlation");
	zassert_equal(result, PCM_ORACLE_RESULT_CORRELATION, "negated result");
	zassert_equal(strcmp(pcm_oracle_result_name(result), "correlation"), 0, "negated name");
}

ZTEST(pcm_oracle, test_dead_zero_channel_fails_correlation)
{
	struct pcm_oracle oracle;
	struct pcm_oracle_metrics metrics;
	const struct pcm_oracle_limits limits = {
		.min_samples = 4U,
		.max_abs_error = UINT32_MAX,
		.max_rms_error = UINT32_MAX,
		.min_correlation_q15 = INT32_MIN,
	};
	int16_t actual[] = {0, 0, 0, 0};
	int16_t expected[] = {256, -256, 512, -512};
	uint8_t reference[8];

	write_reference(reference, 2U, expected, ARRAY_SIZE(expected));
	zassert_ok(pcm_oracle_init(&oracle), "init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, 4U), "accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "finalize");
	zassert_equal(metrics.actual_energy_scaled, 0U, "dead channel energy");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_CORRELATION);
}

ZTEST(pcm_oracle, test_distinct_right_reference_fails_channel_swap)
{
	struct pcm_oracle oracle;
	struct pcm_oracle_metrics metrics;
	const struct pcm_oracle_limits limits = {
		.min_samples = 4U,
		.max_abs_error = UINT32_MAX,
		.max_rms_error = UINT32_MAX,
		.min_correlation_q15 = 0,
	};
	int16_t left[] = {256, 512, -256, -512};
	int16_t right[] = {-256, -512, 256, 512};
	uint8_t right_reference[8];

	write_reference(right_reference, 2U, right, ARRAY_SIZE(right));
	zassert_ok(pcm_oracle_init(&oracle), "init");
	zassert_ok(pcm_oracle_accumulate(&oracle, left, 1U, right_reference, 2U, 4U),
		   "accumulate swapped channels");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "finalize");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_CORRELATION);
}

ZTEST(pcm_oracle, test_prior_and_next_frame_shifts_fail)
{
	const struct pcm_oracle_limits limits = {
		.min_samples = 4U,
		.max_abs_error = 0U,
		.max_rms_error = UINT32_MAX,
		.min_correlation_q15 = INT32_MIN,
	};
	int16_t frames[][2] = {{100, 200}, {300, 400}, {500, 600}};
	uint8_t reference[12];
	struct pcm_oracle prior;
	struct pcm_oracle next;
	struct pcm_oracle_metrics metrics;

	for (size_t index = 0U; index < ARRAY_SIZE(frames); index++) {
		write_reference(reference + index * 4U, 2U, frames[index],
				ARRAY_SIZE(frames[index]));
	}

	zassert_ok(pcm_oracle_init(&prior), "prior init");
	zassert_ok(pcm_oracle_accumulate(&prior, frames[1], 1U, reference, 2U, 2U),
		   "prior frame one");
	zassert_ok(pcm_oracle_accumulate(&prior, frames[2], 1U, reference + 4U, 2U, 2U),
		   "prior frame two");
	zassert_ok(pcm_oracle_finalize(&prior, &metrics), "prior finalize");
	zassert_equal(metrics.frames, 2U, "prior frame count");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_MAX_ERROR);

	zassert_ok(pcm_oracle_init(&next), "next init");
	zassert_ok(pcm_oracle_accumulate(&next, frames[0], 1U, reference + 4U, 2U, 2U),
		   "next frame one");
	zassert_ok(pcm_oracle_accumulate(&next, frames[1], 1U, reference + 8U, 2U, 2U),
		   "next frame two");
	zassert_ok(pcm_oracle_finalize(&next, &metrics), "next finalize");
	zassert_equal(metrics.frames, 2U, "next frame count");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_MAX_ERROR);
}

ZTEST(pcm_oracle, test_invalid_arguments_and_overflow_leave_state_unchanged)
{
	struct pcm_oracle oracle;
	struct pcm_oracle before;
	struct pcm_oracle_metrics metrics;
	struct pcm_oracle_metrics before_metrics;
	const struct pcm_oracle_limits limits = {0};
	int16_t actual[] = {256};
	int16_t expected[] = {256};
	uint8_t reference[2];
	enum pcm_oracle_result result = PCM_ORACLE_RESULT_PASS;

	write_reference(reference, 2U, expected, ARRAY_SIZE(expected));
	zassert_equal(pcm_oracle_init(NULL), -EINVAL, "NULL init");
	zassert_ok(pcm_oracle_init(&oracle), "init");
	before = oracle;
	zassert_equal(pcm_oracle_accumulate(&oracle, NULL, 1U, reference, 2U, 1U), -EINVAL,
		      "NULL actual");
	zassert_mem_equal(&oracle, &before, sizeof(oracle), "NULL actual mutation");
	zassert_equal(pcm_oracle_accumulate(&oracle, actual, 1U, NULL, 2U, 1U), -EINVAL,
		      "NULL reference");
	zassert_mem_equal(&oracle, &before, sizeof(oracle), "NULL reference mutation");
	zassert_equal(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, 0U), -EINVAL,
		      "zero count");
	zassert_mem_equal(&oracle, &before, sizeof(oracle), "zero count mutation");
	zassert_equal(pcm_oracle_accumulate(&oracle, actual, 0U, reference, 2U, 1U), -EINVAL,
		      "zero actual stride");
	zassert_mem_equal(&oracle, &before, sizeof(oracle), "zero actual stride mutation");
	zassert_equal(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 0U, 1U), -EINVAL,
		      "zero reference stride");
	zassert_mem_equal(&oracle, &before, sizeof(oracle), "zero reference stride mutation");

	oracle.samples = UINT32_MAX;
	before = oracle;
	zassert_equal(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, 1U), -EOVERFLOW,
		      "sample counter overflow");
	zassert_mem_equal(&oracle, &before, sizeof(oracle), "sample counter mutation");

	zassert_ok(pcm_oracle_init(&oracle), "frame init");
	oracle.frames = UINT32_MAX;
	before = oracle;
	zassert_equal(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, 1U), -EOVERFLOW,
		      "frame counter overflow");
	zassert_mem_equal(&oracle, &before, sizeof(oracle), "frame counter mutation");

	zassert_ok(pcm_oracle_init(&oracle), "squared error init");
	oracle.squared_error = UINT64_MAX;
	before = oracle;
	actual[0] = 257;
	zassert_equal(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, 1U), -EOVERFLOW,
		      "squared error overflow");
	zassert_mem_equal(&oracle, &before, sizeof(oracle), "squared error mutation");

	zassert_ok(pcm_oracle_init(&oracle), "dot product init");
	oracle.dot_product_scaled = INT64_MAX;
	before = oracle;
	actual[0] = 256;
	zassert_equal(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, 1U), -EOVERFLOW,
		      "dot product overflow");
	zassert_mem_equal(&oracle, &before, sizeof(oracle), "dot product mutation");

	zassert_ok(pcm_oracle_init(&oracle), "finalize overflow init");
	oracle.squared_error = UINT64_MAX;
	oracle.samples = 1U;
	memset(&metrics, 0xA5, sizeof(metrics));
	before_metrics = metrics;
	zassert_equal(pcm_oracle_finalize(&oracle, &metrics), -EOVERFLOW, "finalize overflow");
	zassert_mem_equal(&metrics, &before_metrics, sizeof(metrics), "finalize output mutation");

	zassert_equal(pcm_oracle_evaluate(NULL, &limits, &result), -EINVAL, "NULL metrics");
	zassert_equal(result, PCM_ORACLE_RESULT_PASS, "evaluate result mutation");
	zassert_equal(pcm_oracle_evaluate(&before_metrics, NULL, &result), -EINVAL, "NULL limits");
	zassert_equal(result, PCM_ORACLE_RESULT_PASS, "NULL limits result mutation");
	zassert_equal(pcm_oracle_evaluate(&before_metrics, &limits, NULL), -EINVAL, "NULL result");
	zassert_equal(pcm_oracle_finalize(NULL, &metrics), -EINVAL, "NULL finalize source");
	zassert_equal(pcm_oracle_finalize(&oracle, NULL), -EINVAL, "NULL finalize output");
}

ZTEST(pcm_oracle, test_finalize_denominator_overflow_leaves_metrics_unchanged)
{
	struct pcm_oracle oracle = {
		.actual_energy_scaled = UINT64_MAX,
		.reference_energy_scaled = UINT64_MAX,
	};
	struct pcm_oracle_metrics metrics;
	struct pcm_oracle_metrics before_metrics;

	/* floor_sqrt(UINT64_MAX) is UINT32_MAX, whose square exceeds INT64_MAX. */
	memset(&metrics, 0xA5, sizeof(metrics));
	before_metrics = metrics;
	zassert_equal(pcm_oracle_finalize(&oracle, &metrics), -EOVERFLOW,
		      "correlation denominator overflow");
	zassert_mem_equal(&metrics, &before_metrics, sizeof(metrics), "finalize output mutation");
}

ZTEST(pcm_oracle, test_limit_boundaries_and_precedence)
{
	struct pcm_oracle oracle;
	struct pcm_oracle_metrics metrics;
	struct pcm_oracle_limits limits = {
		.min_samples = 4U,
		.max_abs_error = 256U,
		.max_rms_error = 256U,
		.min_correlation_q15 = INT16_MAX,
	};
	int16_t actual[] = {256, 512, -256, -512};
	int16_t expected[] = {0, 256, -512, -256};
	uint8_t reference[8];
	enum pcm_oracle_result result;

	write_reference(reference, 2U, expected, ARRAY_SIZE(expected));
	zassert_ok(pcm_oracle_init(&oracle), "init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, 4U), "accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "finalize");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_PASS);

	limits.min_samples = 5U;
	limits.max_abs_error = 0U;
	zassert_ok(pcm_oracle_evaluate(&metrics, &limits, &result), "insufficient evaluate");
	zassert_equal(result, PCM_ORACLE_RESULT_INSUFFICIENT_SAMPLES, "precedence");
	zassert_equal(strcmp(pcm_oracle_result_name(result), "insufficient-samples"), 0,
		      "insufficient name");

	limits.min_samples = 4U;
	limits.max_abs_error = 255U;
	limits.max_rms_error = 256U;
	limits.min_correlation_q15 = INT16_MAX;
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_MAX_ERROR);

	limits.max_abs_error = 256U;
	limits.max_rms_error = 255U;
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_RMS_ERROR);

	limits.max_rms_error = 256U;
	limits.min_correlation_q15 = INT16_MAX + 1;
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_CORRELATION);
}

ZTEST(pcm_oracle, test_production_policy_controls)
{
	const struct pcm_oracle_limits limits = {
		.min_samples = 480U,
		.max_abs_error = PB031_PCM_MAX_ABS_ERROR,
		.max_rms_error = PB031_PCM_MAX_RMS_ERROR,
		.min_correlation_q15 = PB031_PCM_MIN_CORRELATION_Q15,
	};
	static int16_t actual[480];
	static uint8_t reference[480 * 2];
	struct pcm_oracle oracle;
	struct pcm_oracle_metrics metrics;

	for (size_t sample = 0U; sample < ARRAY_SIZE(actual); sample++) {
		int16_t expected = 4096;

		actual[sample] = 4096;
		write_reference(reference + sample * 2U, 2U, &expected, 1U);
	}
	actual[0] = (int16_t)(4096 - (int32_t)PB031_PCM_MAX_ABS_ERROR);
	zassert_ok(pcm_oracle_init(&oracle), "maximum boundary init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, ARRAY_SIZE(actual)),
		   "maximum boundary accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "maximum boundary finalize");
	zassert_equal(metrics.max_abs_error, PB031_PCM_MAX_ABS_ERROR, "maximum boundary");
	zassert_true(metrics.rms_error <= PB031_PCM_MAX_RMS_ERROR, "maximum boundary RMS");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_PASS);

	for (size_t sample = 0U; sample < ARRAY_SIZE(actual); sample++) {
		int16_t expected = 4096;

		actual[sample] = (int16_t)(4096 - (int32_t)PB031_PCM_MAX_RMS_ERROR);
		write_reference(reference + sample * 2U, 2U, &expected, 1U);
	}
	zassert_ok(pcm_oracle_init(&oracle), "RMS boundary init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, ARRAY_SIZE(actual)),
		   "RMS boundary accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "RMS boundary finalize");
	zassert_equal(metrics.max_abs_error, PB031_PCM_MAX_RMS_ERROR, "RMS boundary maximum");
	zassert_equal(metrics.rms_error, PB031_PCM_MAX_RMS_ERROR, "RMS boundary");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_PASS);

	for (size_t sample = 0U; sample < ARRAY_SIZE(actual); sample++) {
		int16_t expected = 4096;

		actual[sample] = 4096;
		write_reference(reference + sample * 2U, 2U, &expected, 1U);
	}
	actual[0] = (int16_t)(4096 - ((int32_t)PB031_PCM_MAX_ABS_ERROR + 1));
	zassert_ok(pcm_oracle_init(&oracle), "maximum control init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, ARRAY_SIZE(actual)),
		   "maximum control accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "maximum control finalize");
	zassert_equal(metrics.max_abs_error, PB031_PCM_MAX_ABS_ERROR + 1U, "maximum control value");
	zassert_true(metrics.rms_error <= PB031_PCM_MAX_RMS_ERROR, "maximum control RMS");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_MAX_ERROR);

	for (size_t sample = 0U; sample < ARRAY_SIZE(actual); sample++) {
		int16_t expected = 4096;

		actual[sample] = (int16_t)(4096 - ((int32_t)PB031_PCM_MAX_RMS_ERROR + 1));
		write_reference(reference + sample * 2U, 2U, &expected, 1U);
	}
	zassert_ok(pcm_oracle_init(&oracle), "RMS control init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, ARRAY_SIZE(actual)),
		   "RMS control accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "RMS control finalize");
	zassert_equal(metrics.max_abs_error, PB031_PCM_MAX_RMS_ERROR + 1U, "RMS control maximum");
	zassert_equal(metrics.rms_error, PB031_PCM_MAX_RMS_ERROR + 1U, "RMS control value");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_RMS_ERROR);

	for (size_t sample = 0U; sample < ARRAY_SIZE(actual); sample++) {
		int16_t expected = (sample & 1U) == 0U ? 256 : -256;

		actual[sample] = (int16_t)-expected;
		write_reference(reference + sample * 2U, 2U, &expected, 1U);
	}
	zassert_ok(pcm_oracle_init(&oracle), "correlation control init");
	zassert_ok(pcm_oracle_accumulate(&oracle, actual, 1U, reference, 2U, ARRAY_SIZE(actual)),
		   "correlation control accumulate");
	zassert_ok(pcm_oracle_finalize(&oracle, &metrics), "correlation control finalize");
	zassert_equal(metrics.max_abs_error, 512U, "correlation control maximum");
	zassert_equal(metrics.rms_error, 512U, "correlation control RMS");
	zassert_equal(metrics.correlation_q15, INT16_MIN, "correlation control value");
	zassert_true(metrics.max_abs_error <= PB031_PCM_MAX_ABS_ERROR,
		     "correlation control maximum must pass policy");
	zassert_true(metrics.rms_error <= PB031_PCM_MAX_RMS_ERROR,
		     "correlation control RMS must pass policy");
	assert_evaluation(&metrics, &limits, PCM_ORACLE_RESULT_CORRELATION);
}
