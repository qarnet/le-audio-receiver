/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_i2s.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/sys/printk.h>

#define I2S_NODE           DT_NODELABEL(i2s0)
#define SAMPLE_RATE        48000
#define BIT_WIDTH          16
#define CHANNELS           2
#define SAMPLES_PER_FRAME  480       /* 10 ms at 48 kHz = 480 samples/channel */
#define BLOCK_SIZE         (SAMPLES_PER_FRAME * CHANNELS * (BIT_WIDTH / 8))  /* 1920 bytes */
#define BLOCK_COUNT        4

K_MEM_SLAB_DEFINE_STATIC(i2s_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static const struct device *i2s_dev;
static bool configured;
static bool started;

int audio_i2s_init(void)
{
	i2s_dev = DEVICE_DT_GET(I2S_NODE);
	if (!device_is_ready(i2s_dev)) {
		printk("I2S device not ready\n");
		return -ENODEV;
	}

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

	int ret = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
	if (ret < 0) {
		printk("I2S configure failed: %d\n", ret);
		return ret;
	}

	configured = true;
	printk("I2S configured (48 kHz, 16-bit, stereo)\n");
	return 0;
}

int audio_i2s_push(const int16_t *stereo_data, size_t sample_count)
{
	if (!configured) {
		return -EIO;
	}

	if (!started) {
		void *block;
		int ret;

		for (int i = 0; i < 2; i++) {
			ret = k_mem_slab_alloc(&i2s_slab, &block, K_NO_WAIT);
			if (ret < 0) {
				return -ENOMEM;
			}
			memset(block, 0, BLOCK_SIZE);
			ret = i2s_write(i2s_dev, block, BLOCK_SIZE);
			if (ret < 0) {
				k_mem_slab_free(&i2s_slab, block);
				return ret;
			}
		}

		ret = k_mem_slab_alloc(&i2s_slab, &block, K_NO_WAIT);
		if (ret < 0) {
			return -ENOMEM;
		}
		size_t bytes = sample_count * sizeof(int16_t);
		if (bytes > BLOCK_SIZE) {
			bytes = BLOCK_SIZE;
		}
		memcpy(block, stereo_data, bytes);
		ret = i2s_write(i2s_dev, block, BLOCK_SIZE);
		if (ret < 0) {
			k_mem_slab_free(&i2s_slab, block);
			return ret;
		}

		ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
		if (ret < 0) {
			printk("I2S trigger START failed: %d\n", ret);
			return ret;
		}

		started = true;
		printk("I2S DMA started\n");
		return 0;
	}

	void *block;
	int ret = k_mem_slab_alloc(&i2s_slab, &block, K_NO_WAIT);
	if (ret < 0) {
		printk("I2S slab exhausted (underrun)\n");
		return -ENOMEM;
	}

	size_t bytes = sample_count * sizeof(int16_t);
	if (bytes > BLOCK_SIZE) {
		bytes = BLOCK_SIZE;
	}

	memcpy(block, stereo_data, bytes);
	ret = i2s_write(i2s_dev, block, BLOCK_SIZE);
	if (ret < 0) {
		printk("I2S write failed: %d\n", ret);
		k_mem_slab_free(&i2s_slab, block);
		return ret;
	}

	return 0;
}

void audio_i2s_stop(void)
{
	if (started) {
		i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
		started = false;
		printk("I2S output stopped\n");
	}
	configured = false;
}
