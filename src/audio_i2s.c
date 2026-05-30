/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_i2s.h"
#include "audio_drift.h"
#include "audio_stats.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <hal/nrf_clock.h>
#if NRF_CLOCK_HAS_HFCLKAUDIO
#include <nrfx_clock_hfclkaudio.h>
#endif

LOG_MODULE_REGISTER(audio_i2s, LOG_LEVEL_INF);

#define I2S_NODE           DT_NODELABEL(i2s0)
#define SAMPLE_RATE        48000
#define BIT_WIDTH          16
#define CHANNELS           2
#define SAMPLES_PER_FRAME  480
#define BLOCK_SIZE         (SAMPLES_PER_FRAME * CHANNELS * (BIT_WIDTH / 8))
#define BLOCK_COUNT        12

/*
 * Packet-repeat fallback: when the DMA queue drops below DRIFT_THRESHOLD
 * free blocks, duplicate the last decoded frame to prevent a hard cutout
 * while the APLL loop converges.
 *
 * Normal operating point after pre-fill: 5 free (7 of 12 blocks in DMA).
 * Natural jitter reaches 6. Trigger at 8 = only 4 blocks remain in DMA.
 */
#define DRIFT_THRESHOLD    (BLOCK_COUNT - 4)

K_MEM_SLAB_DEFINE_STATIC(i2s_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static const struct device *i2s_dev;
static bool configured;
static bool started;
static int16_t saved_frame[BLOCK_SIZE / sizeof(int16_t)];

void audio_i2s_sdu_ref_update(uint32_t sdu_ref_us)
{
	uint16_t new_freq = audio_drift_update(sdu_ref_us);

#if NRF_CLOCK_HAS_HFCLKAUDIO
	if (new_freq != 0) {
		nrfx_clock_hfclkaudio_config_set(new_freq);
		LOG_DBG("APLL → 0x%04X", new_freq);
	}
#else
	ARG_UNUSED(new_freq);
#endif
}

static void drift_reset(void)
{
	audio_drift_reset();
#if NRF_CLOCK_HAS_HFCLKAUDIO
	nrfx_clock_hfclkaudio_config_set(AUDIO_DRIFT_APLL_CENTER);
#endif
}

static int i2s_do_configure(void)
{
	struct i2s_config cfg = {
		.word_size       = BIT_WIDTH,
		.channels        = CHANNELS,
		.format          = I2S_FMT_DATA_FORMAT_I2S,
		.options         = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq  = SAMPLE_RATE,
		.mem_slab        = &i2s_slab,
		.block_size      = BLOCK_SIZE,
		.timeout         = 0,
	};

	return i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
}

int audio_i2s_init(void)
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

	configured = true;
	LOG_INF("I2S ready (%d kHz, %d-bit, stereo, %d blocks)",
		SAMPLE_RATE / 1000, BIT_WIDTH, BLOCK_COUNT);
	return 0;
}

int audio_i2s_push(const int16_t *stereo_data, size_t sample_count)
{
	if (!configured) {
		return -EIO;
	}

	size_t bytes = sample_count * sizeof(int16_t);

	if (bytes > BLOCK_SIZE) {
		bytes = BLOCK_SIZE;
	}

	void *block;
	int ret = k_mem_slab_alloc(&i2s_slab, &block, K_NO_WAIT);

	if (ret < 0) {
		LOG_WRN("I2S slab full — dropping frame");
		audio_stats_i2s_underrun();
		return -ENOMEM;
	}

	memset(block, 0, BLOCK_SIZE);
	memcpy(block, stereo_data, bytes);

	if (!started) {
		/* Pre-fill 6 silent blocks (~60 ms) to absorb jitter */
		for (int pre = 0; pre < 6; pre++) {
			void *sil;

			if (k_mem_slab_alloc(&i2s_slab, &sil, K_NO_WAIT) == 0) {
				memset(sil, 0, BLOCK_SIZE);
				if (i2s_write(i2s_dev, sil, BLOCK_SIZE) < 0) {
					k_mem_slab_free(&i2s_slab, sil);
				}
			}
		}

		ret = i2s_write(i2s_dev, block, BLOCK_SIZE);
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

	memcpy(saved_frame, block, BLOCK_SIZE);

	ret = i2s_write(i2s_dev, block, BLOCK_SIZE);
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

	/* Packet-repeat fallback: pad queue when it is draining */
	if (k_mem_slab_num_free_get(&i2s_slab) >= DRIFT_THRESHOLD) {
		void *dup;

		if (k_mem_slab_alloc(&i2s_slab, &dup, K_NO_WAIT) == 0) {
			memcpy(dup, saved_frame, BLOCK_SIZE);
			if (i2s_write(i2s_dev, dup, BLOCK_SIZE) < 0) {
				k_mem_slab_free(&i2s_slab, dup);
			}
		}
	}

	return 0;
}

void audio_i2s_stop(void)
{
	if (!started) {
		return;
	}

	i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
	started = false;
	drift_reset();
}
