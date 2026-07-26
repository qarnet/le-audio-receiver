/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_sink.h"
#include "audio_timing.h"
#include "audio_drift.h"
#include "audio_clock_actuator.h"
#include "audio_rate_convert.h"
#include "audio_stats.h"

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
 * Maximum output stereo frames per block: nominal 480 + 1 for a possible
 * sample-insert.  The rate converter normally produces 476 or 477 frames at
 * 47,619 Hz drain; 481 is the ceiling for 48k → 48k plus one insert.
 */
#define MAX_OUTPUT_FRAMES 481
#define BLOCK_SIZE        ((size_t)(MAX_OUTPUT_FRAMES) * CHANNELS * (BIT_WIDTH / 8))
#define BLOCK_COUNT       12

/*
 * Packet-repeat fallback: when the DMA queue drops below DRIFT_THRESHOLD
 * free blocks, duplicate the last decoded frame to prevent a hard cutout
 * while the APLL loop converges.
 *
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
 * Rate converter: maps decoder 48 kHz output to actual I2S drain rate.
 * Configured once in audio_sink_init.
 */
static struct audio_rate_converter rate_ctx;

static void drift_reset(void)
{
	audio_drift_reset();
	audio_clock_actuator_reset();
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

	ret = audio_clock_actuator_init();
	if (ret < 0) {
		LOG_ERR("audio_clock_actuator_init failed: %d", ret);
		return ret;
	}

	/* Initialize platform audio timing measurement.
	 * On nRF54L15 this sets up GRTC + TIMER20 + GPPI for
	 * hardware-timed LRCK frame counting.  Must fail loudly
	 * if required hardware is unavailable.
	 */
	ret = audio_timing_init();
	if (ret < 0) {
		LOG_ERR("audio_timing_init failed: %d", ret);
		return ret;
	}

	/* Only mark configured after all subsystems succeed */
	configured = true;

	LOG_INF("I2S ready (%d kHz nom, %d-bit, stereo, %d blocks, "
		"rate-conv %d→%d Hz)",
		SAMPLE_RATE / 1000, BIT_WIDTH, BLOCK_COUNT, 48000,
		CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ);
	return 0;
}

int audio_sink_push(const int16_t *stereo_data, size_t sample_count)
{
	if (!configured) {
		return -EIO;
	}

	/*
	 * Step 0: rate conversion — compute output frame count for
	 * this input block independent of slab or controller state.
	 */
	size_t base_output = audio_rate_converter_next_frames(&rate_ctx, SAMPLES_PER_FRAME);

	/*
	 * Step 1: per-block PI controller update.
	 * Read slab free count BEFORE allocating the next block so
	 * setpoint meaning stays stable.  The controller combines
	 * PCLK frequency feedforward with buffer-phase PI, applies
	 * the result to the clock actuator, then we check for a
	 * pending sample adjustment.
	 *
	 * Skipped during pre-fill: the DMA is not yet running and
	 * frequency feedforward may not have a measurement.
	 */
	int adj = 0;
	if (started) {
		int slab_free = k_mem_slab_num_free_get(&i2s_slab);
		int32_t ppm = audio_drift_controller_update(slab_free);

		if (ppm != 0) {
			audio_clock_actuator_apply_ppm(ppm);
			LOG_DBG("Drift → %d ppm (free=%d)", ppm, slab_free);
		}
	}
	adj = audio_clock_actuator_consume_sample_adjustment();

	/*
	 * Step 2: apply sample-adjust to output frame count.
	 *   adj  = sample-adjust actuator: +1 = drop, -1 = insert.
	 *   output_frames = base - adj (clamped).
	 */
	size_t output_frames;

	/* +1 (drop) → subtract; -1 (insert) → add */
	if (adj > 0 && (size_t)adj <= base_output) {
		output_frames = base_output - (size_t)adj;
	} else if (adj < 0) {
		output_frames = base_output + (size_t)(-adj);
	} else {
		output_frames = base_output;
	}
	output_frames = CLAMP(output_frames, 1, MAX_OUTPUT_FRAMES);

	size_t out_bytes = output_frames * CHANNELS * (BIT_WIDTH / 8);

	/*
	 * Step 3: allocate slab block and apply nearest-neighbor
	 * stereo resampling.
	 */
	void *block;
	int ret = k_mem_slab_alloc(&i2s_slab, &block, K_NO_WAIT);

	if (ret < 0) {
		LOG_WRN("I2S slab full — dropping frame");
		audio_stats_i2s_underrun();
		return -ENOMEM;
	}

	memset(block, 0, BLOCK_SIZE);

	audio_rate_converter_nearest_stereo(stereo_data, SAMPLES_PER_FRAME, (int16_t *)block,
					    output_frames);

	/*
	 * Step 4: pre-fill or normal queue.
	 */
	if (!started) {
		/* Pre-fill 6 silent blocks (~60 ms) to absorb jitter.
		 * Use the rate converter so the pre-fill matches the
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
	 * Step 5: save frame for packet-repeat, then queue.
	 */
	memcpy(saved_frame, block, out_bytes);
	saved_frame_len = out_bytes;

	ret = i2s_write(i2s_dev, block, out_bytes);
	if (ret < 0) {
		k_mem_slab_free(&i2s_slab, block);
		if (ret == -EIO) {
			/* DMA underrun — restart */
			LOG_WRN("I2S underrun, restarting DMA");
			i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
			audio_stats_stream_reset();
			started = false;
		}
		return ret;
	}

	/*
	 * Step 6: packet-repeat fallback — pad queue when draining.
	 */
	if (k_mem_slab_num_free_get(&i2s_slab) >= DRIFT_THRESHOLD) {
		void *dup;

		if (k_mem_slab_alloc(&i2s_slab, &dup, K_NO_WAIT) == 0) {
			memcpy(dup, saved_frame, saved_frame_len);
			if (i2s_write(i2s_dev, dup, saved_frame_len) < 0) {
				k_mem_slab_free(&i2s_slab, dup);
			}
		}
	}

	return 0;
}

void audio_sink_stop(void)
{
	/* Always reset drift and timing state, even if I2S was never
	 * started — they may carry stale state from a previous session.
	 */
	drift_reset();
	audio_timing_reset();

	if (!started) {
		return;
	}

	/* After a DMA underrun the driver is in I2S_STATE_ERROR with the
	 * nrfx instance de-initialized (i2s_nrfx.c uninit on transfer end).
	 * TRIGGER_DROP in that state calls nrfx_i2s_stop() on a dead
	 * instance → NRFX_ASSERT → kernel panic on the *next* disconnect.
	 * TRIGGER_PREPARE (allowed from ERROR) resets the driver to READY
	 * without touching nrfx, so DROP becomes a pure queue purge.
	 * PREPARE returns -EIO when the driver is not in ERROR — harmless.
	 */
	i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
	i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
	started = false;
}
