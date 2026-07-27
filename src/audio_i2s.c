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

#define I2S_NODE          DT_ALIAS(i2s_audio)
#define SAMPLE_RATE       48000
#define BIT_WIDTH         16
#define CHANNELS          2
#define SAMPLES_PER_FRAME 480

/*
 * Maximum output stereo frames per block.
 * ASRC worst-case at 48k→47 619 Hz with −2 000 ppm is 478 frames
 * — well within 481.  Capacity proof: tests/unit/asrc/.
 */
#define MAX_OUTPUT_FRAMES 481
#define BLOCK_SIZE        ((size_t)(MAX_OUTPUT_FRAMES) * CHANNELS * (BIT_WIDTH / 8))
#define BLOCK_COUNT       12

/*
 * Packet-repeat fallback: when the DMA queue drops below DRIFT_THRESHOLD
 * free blocks, duplicate the last decoded frame to prevent a hard cutout.
 * Normal operating point after pre-fill: 5 free (7 of 12 blocks in DMA).
 * Natural jitter reaches 6. Trigger at 8 = only 4 blocks remain in DMA.
 */
#define DRIFT_THRESHOLD (BLOCK_COUNT - 4)

K_MEM_SLAB_DEFINE_STATIC(i2s_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static const struct device *i2s_dev;
static bool configured;
static bool started;

/* Saved last-output frame for packet-repeat (variable length). */
static int16_t saved_frame[MAX_OUTPUT_FRAMES * CHANNELS];
static size_t saved_frame_len; /* bytes */

/*
 * Rate converter: used for silence prefill (all paths) and for the
 * SAMPLE_ADJUST legacy resampling path.  Reset on stop.
 */
static struct audio_rate_converter rate_ctx;

/*
 * ASRC: stateful cross-block linear-interpolation resampler.
 * Only compiled/used on CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR.
 */
#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
static struct audio_asrc asrc_ctx;
#endif

/*
 * Sample-adjustment counters (legacy SAMPLE_ADJUST path only).
 */
static uint32_t insert_count;
static uint32_t drop_count;

static void drift_reset(void)
{
	audio_drift_reset();
	audio_clock_actuator_reset();
#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
	audio_asrc_reset(&asrc_ctx);
#endif
	audio_rate_converter_init(&rate_ctx, 48000, CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ);
	insert_count = 0;
	drop_count = 0;
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

/* ── input validation (shared) ───────────────────────────────────── */

/**
 * Validate the contract of audio_sink_push before any state or slab
 * allocation.  Returns 0 on success, -EINVAL on violation.
 */
static int validate_push_input(const int16_t *stereo_data, size_t sample_count)
{
	if (!stereo_data) {
		return -EINVAL;
	}
	if (sample_count == 0) {
		return -EINVAL;
	}
	/* Must be interleaved stereo → even number of int16_t values. */
	if (sample_count & 1u) {
		return -EINVAL;
	}
	return 0;
}

/* ── resampler-specific block fill ───────────────────────────────── */

#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)

static int fill_block_asrc(const int16_t *stereo_data, size_t input_frames, int32_t ppm,
			   void **block, size_t *output_frames)
{
	int ret = k_mem_slab_alloc(&i2s_slab, block, K_NO_WAIT);

	if (ret < 0) {
		LOG_WRN("I2S slab full — dropping frame");
		audio_stats_i2s_underrun();
		return ret;
	}

	memset(*block, 0, BLOCK_SIZE);

	size_t consumed, produced;
	uint32_t t_asrc = audio_perf_cycle_start();
	int asrc_ret = audio_asrc_process(&asrc_ctx, stereo_data, input_frames, (int16_t *)*block,
					  MAX_OUTPUT_FRAMES, ppm, &consumed, &produced);
	audio_perf_cycle_end(t_asrc, AUDIO_PERF_PATH_ASRC);

	if (asrc_ret != 0) {
		if (asrc_ret == 1) {
			/* Capacity exhausted — state unchanged. */
			LOG_WRN("ASRC capacity exceeded");
			audio_perf_asrc_capacity_failure();
		} else {
			/* asrc_ret < 0 → internal error, state unchanged. */
			LOG_WRN("ASRC error %d", asrc_ret);
		}
		k_mem_slab_free(&i2s_slab, *block);
		return (asrc_ret == 1) ? -ENOSPC : -EIO;
	}

	/* Success: consumed == input_frames, produced > 0. */
	*output_frames = produced;
	return 0;
}

#elif defined(CONFIG_AUDIO_RESAMPLER_SAMPLE_ADJUST)

static int fill_block_legacy(const int16_t *stereo_data, size_t input_frames, int32_t ppm_unused,
			     void **block, size_t *output_frames)
{
	(void)ppm_unused;

	size_t base_output = audio_rate_converter_next_frames(&rate_ctx, input_frames);
	int adj = audio_clock_actuator_consume_sample_adjustment();

	if (adj == -1) {
		insert_count++;
	} else if (adj == +1) {
		drop_count++;
	}

	if (adj != 0) {
		uint32_t total = insert_count + drop_count;

		if (total == 1) {
			LOG_INF("First sample adjustment: %s (ins=%u drops=%u)",
				adj == 1 ? "drop" : "insert", insert_count, drop_count);
		} else if (total % 500 == 0) {
			LOG_INF("Sample adjustments: ins=%u drops=%u (total=%u)", insert_count,
				drop_count, total);
		}
	}

	size_t out_frames;

	if (adj > 0 && (size_t)adj <= base_output) {
		out_frames = base_output - (size_t)adj;
	} else if (adj < 0) {
		out_frames = base_output + (size_t)(-adj);
	} else {
		out_frames = base_output;
	}
	out_frames = CLAMP(out_frames, 1, MAX_OUTPUT_FRAMES);

	int ret = k_mem_slab_alloc(&i2s_slab, block, K_NO_WAIT);

	if (ret < 0) {
		LOG_WRN("I2S slab full — dropping frame");
		audio_stats_i2s_underrun();
		return ret;
	}

	memset(*block, 0, BLOCK_SIZE);

	audio_rate_converter_nearest_stereo(stereo_data, input_frames, (int16_t *)*block,
					    out_frames);
	*output_frames = out_frames;
	return 0;
}

#else /* AUDIO_RESAMPLER_IDENTITY */

/**
 * Identity path: memcpy the input directly into the slab block.
 * No rate conversion, no sample adjustment — APLL handles drift
 * at the clock.
 */
static int fill_block_identity(const int16_t *stereo_data, size_t input_frames, int32_t ppm_unused,
			       void **block, size_t *output_frames)
{
	(void)ppm_unused;

	if (input_frames > MAX_OUTPUT_FRAMES) {
		return -ENOSPC;
	}

	int ret = k_mem_slab_alloc(&i2s_slab, block, K_NO_WAIT);

	if (ret < 0) {
		LOG_WRN("I2S slab full — dropping frame");
		audio_stats_i2s_underrun();
		return ret;
	}

	size_t bytes = input_frames * CHANNELS * (BIT_WIDTH / 8);

	memcpy(*block, stereo_data, bytes);
	*output_frames = input_frames;
	return 0;
}

#endif /* AUDIO_RESAMPLER_IDENTITY */

/* ── audio_sink_push ──────────────────────────────────────────────── */

int audio_sink_push(const int16_t *stereo_data, size_t sample_count)
{
	int ret;

	/* Validate contract before touching any state. */
	ret = validate_push_input(stereo_data, sample_count);
	if (ret < 0) {
		return ret;
	}

	if (!configured) {
		return -EIO;
	}

	size_t input_frames = sample_count / CHANNELS;

	/* Perf measurement: only steady-state pushes. */
	bool measuring = started;
	uint32_t t0 = measuring ? audio_perf_cycle_start() : 0;

	/*
	 * PI controller update.  Skipped during pre-fill.
	 */
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

	/*
	 * Fill the slab block via the selected resampler.
	 */
	void *block = NULL;
	size_t output_frames = 0;

#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
	ret = fill_block_asrc(stereo_data, input_frames, ppm, &block, &output_frames);
#elif defined(CONFIG_AUDIO_RESAMPLER_SAMPLE_ADJUST)
	ret = fill_block_legacy(stereo_data, input_frames, ppm, &block, &output_frames);
#else
	ret = fill_block_identity(stereo_data, input_frames, ppm, &block, &output_frames);
#endif
	if (ret < 0) {
		perf_finalize_push(measuring, t0);
		return ret;
	}

	size_t out_bytes = output_frames * CHANNELS * (BIT_WIDTH / 8);

	if (started) {
		audio_perf_queue_sample(slab_free, output_frames);
	}

	/*
	 * Pre-fill or normal queue.
	 */
	if (!started) {
		/* Pre-fill 6 silent blocks (~60 ms) to absorb jitter.
		 * The rate converter (separate from ASRC) matches the
		 * HW drain rate exactly.
		 */
		for (int pre = 0; pre < 6; pre++) {
			size_t pre_frames =
				audio_rate_converter_next_frames(&rate_ctx, SAMPLES_PER_FRAME);
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

	/*
	 * Normal queue: save frame for packet-repeat, then push.
	 */
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

	/*
	 * Packet-repeat fallback — pad queue when draining.
	 */
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
