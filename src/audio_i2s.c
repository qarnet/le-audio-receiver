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
#include <nrfx_clock_hfclkaudio.h>

#define I2S_NODE           DT_NODELABEL(i2s0)
#define SAMPLE_RATE        48000
#define BIT_WIDTH          16
#define CHANNELS           2
#define SAMPLES_PER_FRAME  480
#define BLOCK_SIZE         (SAMPLES_PER_FRAME * CHANNELS * (BIT_WIDTH / 8))
#define BLOCK_COUNT        12

/* APLL register values for nRF5340 HFCLKAUDIO (12.288 MHz band) */
#define APLL_FREQ_CENTER   0x9BA6U
#define APLL_FREQ_MIN      0x8FD8U
#define APLL_FREQ_MAX      0xA774U

/*
 * Event-driven APLL drift correction.
 *
 * Underrun  → I2S consuming too fast → reduce APLL frequency (slow I2S down)
 * Slab full → I2S consuming too slow → raise APLL frequency (speed I2S up)
 *
 * One underrun event applies APLL_STEP_UNDERRUN register steps. Each step is
 * ~40.7 Hz = ~3.3 ppm. 5 steps ≈ 16.5 ppm per event. At 100 ppm true offset,
 * ~6 underrun cycles bring the clock into range. apll_freq is NOT reset on
 * stream restart — only on disconnect — so corrections accumulate across
 * BLE ISO retransmit gaps.
 */
#define APLL_STEP_UNDERRUN  5
#define APLL_STEP_OVERRUN   5

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

/*
 * apll_freq persists across stream restarts (underrun → re-arm) so that
 * corrections accumulate. Only reset to center on full disconnect.
 */
static uint16_t apll_freq = APLL_FREQ_CENTER;

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
		printk("I2S device not ready\n");
		return -ENODEV;
	}

	int ret = i2s_do_configure();
	if (ret < 0) {
		printk("I2S configure failed: %d\n", ret);
		return ret;
	}

	configured = true;
	printk("I2S ready (%d kHz, %d-bit, stereo, %d blocks)\n",
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
		/* Slab full: I2S consuming too slowly → speed up APLL */
		uint16_t f = MIN(apll_freq + APLL_STEP_OVERRUN, APLL_FREQ_MAX);

		if (f != apll_freq) {
			apll_freq = f;
			nrfx_clock_hfclkaudio_config_set(apll_freq);
			printk("APLL: slab full → 0x%04X\n", apll_freq);
		}
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

		/* Apply retained APLL correction immediately */
		nrfx_clock_hfclkaudio_config_set(apll_freq);

		started = true;
		printk("I2S DMA started (APLL=0x%04X)\n", apll_freq);
		return 0;
	}

	memcpy(saved_frame, block, BLOCK_SIZE);

	ret = i2s_write(i2s_dev, block, BLOCK_SIZE);
	if (ret < 0) {
		k_mem_slab_free(&i2s_slab, block);
		if (ret == -EIO) {
			/* Underrun: I2S consuming too fast → slow APLL down */
			uint16_t f = MAX(apll_freq - APLL_STEP_UNDERRUN, APLL_FREQ_MIN);

			if (f != apll_freq) {
				apll_freq = f;
				printk("APLL: underrun → 0x%04X\n", apll_freq);
			}
			i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
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

	/* Reset APLL on disconnect — new connection may be a different device */
	apll_freq = APLL_FREQ_CENTER;
	nrfx_clock_hfclkaudio_config_set(apll_freq);
}
