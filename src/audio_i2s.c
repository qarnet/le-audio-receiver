/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_sink.h"
#include "audio_timing.h"
#include "audio_drift.h"
#include "audio_clock_actuator.h"
#include "audio_rate_convert.h"
#include "audio_asrc.h"
#include "audio_stats.h"
#include "audio_perf.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(audio_i2s, LOG_LEVEL_INF);

/* ── resampler ↔ actuator build-time verification ────────────────── */

#if defined(CONFIG_AUDIO_RESAMPLER_IDENTITY)
BUILD_ASSERT(IS_ENABLED(CONFIG_AUDIO_CLOCK_ACTUATOR_APLL),
	     "IDENTITY resampler requires APLL actuator");
#elif defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
BUILD_ASSERT(IS_ENABLED(CONFIG_AUDIO_CLOCK_ACTUATOR_NONE),
	     "ASRC_LINEAR resampler requires NONE actuator");
#endif

#define I2S_NODE    DT_ALIAS(i2s_audio)
#define SAMPLE_RATE 48000
#define BIT_WIDTH   16
#define CHANNELS    2

/*
 * Supported input frame count per push call.
 * Must match the decoder output (48 kHz LC3, 10 ms frame).
 */
#define INPUT_FRAMES 480

/*
 * Maximum output stereo frames per block.
 * ASRC worst-case at 48k→47 619 Hz with −2 000 ppm fits
 * within 481.  Capacity proof: tests/unit/asrc/.
 */
#define MAX_OUTPUT_FRAMES 481
#define BLOCK_SIZE        ((size_t)(MAX_OUTPUT_FRAMES) * CHANNELS * (BIT_WIDTH / 8))
#define BLOCK_COUNT       12

#define DRIFT_THRESHOLD (BLOCK_COUNT - 4)

K_MEM_SLAB_DEFINE_STATIC(i2s_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static const struct device *i2s_dev;
static bool configured;
static bool started;

static int16_t saved_frame[MAX_OUTPUT_FRAMES * CHANNELS];
static size_t saved_frame_len;

static struct audio_rate_converter rate_ctx;

#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
static struct audio_asrc asrc_ctx;
static int16_t asrc_prev_l;
static int16_t asrc_prev_r;
static bool asrc_prev_valid;
#endif

static void drift_reset(void)
{
	audio_drift_reset();
	audio_clock_actuator_reset();
#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
	audio_asrc_reset(&asrc_ctx);
	asrc_prev_valid = false;
#endif
	audio_rate_converter_init(&rate_ctx, 48000, CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ);
}

static int i2s_do_configure(void)
{
	struct i2s_config cfg = {
		.word_size = BIT_WIDTH,
		.channels = CHANNELS,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = SAMPLE_RATE,
		.mem_slab = &i2s_slab,
		.block_size = BLOCK_SIZE,
		.timeout = 0,
	};

	return i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
}

int audio_sink_init(void)
{
	i2s_dev = DEVICE_DT_GET(I2S_NODE);
	if (!device_is_ready(i2s_dev)) {
		LOG_ERR("I2S device not ready");
		return -ENODEV;
	}

	int ret = i2s_do_configure();

	if (ret < 0) {
		LOG_ERR("I2S configure failed: %d", ret);
		return ret;
	}

	audio_rate_converter_init(&rate_ctx, 48000, CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ);

#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
	ret = audio_asrc_init(&asrc_ctx, 48000, CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ);
	if (ret < 0) {
		LOG_ERR("audio_asrc_init failed: %d", ret);
		return ret;
	}
	asrc_prev_valid = false;
#endif

	ret = audio_clock_actuator_init();
	if (ret < 0) {
		LOG_ERR("audio_clock_actuator_init failed: %d", ret);
		return ret;
	}

	ret = audio_timing_init();
	if (ret < 0) {
		LOG_ERR("audio_timing_init failed: %d", ret);
		return ret;
	}

	configured = true;

	LOG_INF("I2S ready (%d kHz nom, %d-bit, stereo, %d blocks)", SAMPLE_RATE / 1000, BIT_WIDTH,
		BLOCK_COUNT);
	return 0;
}

static void perf_finalize_push(bool measuring, uint32_t t0)
{
	if (measuring) {
		audio_perf_cycle_end(t0, AUDIO_PERF_PATH_SINK_PUSH);
	}
}

static int validate_push_input(const int16_t *stereo_data, size_t sample_count)
{
	if (!stereo_data) {
		return -EINVAL;
	}
	if (sample_count == 0) {
		return -EINVAL;
	}
	if (sample_count & 1u) {
		return -EINVAL;
	}
	/* Strict frame count: the decoder always produces INPUT_FRAMES
	 * stereo frames per SDU.  This contract is required for slab
	 * sizing and capacity proofs.
	 */
	if (sample_count / CHANNELS != INPUT_FRAMES) {
		return -EINVAL;
	}
	return 0;
}

/* ── resampler-specific block fill ───────────────────────────────── */

#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)

static int fill_block_asrc(const int16_t *stereo_data, int32_t ppm, void **block,
			   size_t *output_frames)
{
	int ret = k_mem_slab_alloc(&i2s_slab, block, K_NO_WAIT);

	if (ret < 0) {
		LOG_WRN("I2S slab full — dropping frame");
		audio_stats_i2s_underrun();
		return ret;
	}

	memset(*block, 0, BLOCK_SIZE);

	size_t consumed, produced;
	int16_t next_l, next_r;
	uint32_t t_asrc = audio_perf_cycle_start();
	int asrc_ret = audio_asrc_process(&asrc_ctx, stereo_data, INPUT_FRAMES, (int16_t *)*block,
					  MAX_OUTPUT_FRAMES, ppm, asrc_prev_l, asrc_prev_r,
					  asrc_prev_valid, &consumed, &produced, &next_l, &next_r);
	audio_perf_cycle_end(t_asrc, AUDIO_PERF_PATH_ASRC);

	if (asrc_ret != 0) {
		if (asrc_ret == 1) {
			LOG_WRN("ASRC capacity exceeded");
			audio_perf_asrc_capacity_failure();
		} else {
			LOG_WRN("ASRC error %d", asrc_ret);
		}
		k_mem_slab_free(&i2s_slab, *block);
		return (asrc_ret == 1) ? -ENOSPC : -EIO;
	}

	asrc_prev_l = next_l;
	asrc_prev_r = next_r;
	asrc_prev_valid = true;

	*output_frames = produced;
	return 0;
}

#else /* AUDIO_RESAMPLER_IDENTITY */

static int fill_block_identity(const int16_t *stereo_data, int32_t ppm_unused, void **block,
			       size_t *output_frames)
{
	(void)ppm_unused;

	int ret = k_mem_slab_alloc(&i2s_slab, block, K_NO_WAIT);

	if (ret < 0) {
		LOG_WRN("I2S slab full — dropping frame");
		audio_stats_i2s_underrun();
		return ret;
	}

	size_t bytes = INPUT_FRAMES * CHANNELS * (BIT_WIDTH / 8);

	memcpy(*block, stereo_data, bytes);
	*output_frames = INPUT_FRAMES;
	return 0;
}

#endif

/* ── audio_sink_push ──────────────────────────────────────────────── */

int audio_sink_push(const int16_t *stereo_data, size_t sample_count)
{
	int ret;

	ret = validate_push_input(stereo_data, sample_count);
	if (ret < 0) {
		return ret;
	}

	if (!configured) {
		return -EIO;
	}

	bool measuring = started;
	uint32_t t0 = measuring ? audio_perf_cycle_start() : 0;

	int32_t ppm = 0;
	int slab_free = 0;

	if (started) {
		slab_free = k_mem_slab_num_free_get(&i2s_slab);
		ppm = audio_drift_controller_update(slab_free);

		if (ppm != 0) {
			audio_clock_actuator_apply_ppm(ppm);
			LOG_DBG("Drift → %d ppm (free=%d)", ppm, slab_free);
		}
	}

	void *block = NULL;
	size_t output_frames = 0;

#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
	ret = fill_block_asrc(stereo_data, ppm, &block, &output_frames);
#else
	ret = fill_block_identity(stereo_data, ppm, &block, &output_frames);
#endif
	if (ret < 0) {
		perf_finalize_push(measuring, t0);
		return ret;
	}

	size_t out_bytes = output_frames * CHANNELS * (BIT_WIDTH / 8);

	if (started) {
		audio_perf_queue_sample(slab_free, output_frames);
	}

	if (!started) {
		/* Pre-fill 6 blocks of silence to absorb jitter. */
		for (int pre = 0; pre < 6; pre++) {
			size_t pre_frames =
				audio_rate_converter_next_frames(&rate_ctx, INPUT_FRAMES);
			size_t pre_bytes = pre_frames * CHANNELS * (BIT_WIDTH / 8);
			void *sil;

			if (k_mem_slab_alloc(&i2s_slab, &sil, K_NO_WAIT) == 0) {
				memset(sil, 0, pre_bytes);
				if (i2s_write(i2s_dev, sil, pre_bytes) < 0) {
					k_mem_slab_free(&i2s_slab, sil);
				}
			}
		}

		ret = i2s_write(i2s_dev, block, out_bytes);
		if (ret < 0) {
			k_mem_slab_free(&i2s_slab, block);
			return ret;
		}

		ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
		if (ret < 0) {
			return ret;
		}

		LOG_INF("I2S DMA started");
		started = true;
		return 0;
	}

	memcpy(saved_frame, block, out_bytes);
	saved_frame_len = out_bytes;

	ret = i2s_write(i2s_dev, block, out_bytes);
	if (ret < 0) {
		k_mem_slab_free(&i2s_slab, block);
		if (ret == -EIO) {
			LOG_WRN("I2S underrun, restarting DMA");
			i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
			audio_stats_stream_reset();
			started = false;
		}
		perf_finalize_push(measuring, t0);
		return ret;
	}

	if (k_mem_slab_num_free_get(&i2s_slab) >= DRIFT_THRESHOLD) {
		void *dup;

		if (k_mem_slab_alloc(&i2s_slab, &dup, K_NO_WAIT) == 0) {
			memcpy(dup, saved_frame, saved_frame_len);
			if (i2s_write(i2s_dev, dup, saved_frame_len) < 0) {
				k_mem_slab_free(&i2s_slab, dup);
			}
		}
		audio_perf_repeat_fallback();
	}

	perf_finalize_push(measuring, t0);
	return 0;
}

void audio_sink_stop(void)
{
	drift_reset();
	audio_timing_reset();

	if (!started) {
		return;
	}

	i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
	i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
	started = false;
}
