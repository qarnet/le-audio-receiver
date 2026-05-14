/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_i2s.h"

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

#define I2S_NODE           DT_ALIAS(i2s_audio)
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

/* ── APLL state-machine drift compensation ──────────────────────────────
 *
 * Principle: the ISO controller provides a reference timestamp (sdu_ref_us)
 * every frame. Consecutive timestamps should advance by exactly
 * CONFIG_AUDIO_FRAME_DURATION_US. Any systematic deviation indicates a
 * frequency offset between the local HFCLKAUDIO and the BLE controller clock.
 *
 * Over a DRIFT_MEAS_PERIOD_US window we measure the actual elapsed sdu_ref
 * time vs the expected 100 ms. The error drives an APLL register adjustment.
 *
 * States:
 *   DRIFT_INIT  — waiting for first valid timestamp
 *   DRIFT_CALIB — measuring and correcting; transitions to LOCKED when
 *                 error < DRIFT_ERR_THRESH_LOCK
 *   DRIFT_LOCKED — fine maintenance corrections; resets to CALIB on large error
 *
 * Formula (from NCS audio reference): APLL_FREQ_ADJ(err_us) = -(err_us*1000)/331
 * One register step ≈ 3.3 ppm. Positive err → increase APLL freq (speed up).
 */
#if NRF_CLOCK_HAS_HFCLKAUDIO

#define APLL_FREQ_CENTER        0x9BA6U
#define APLL_FREQ_MIN           0x8FD8U
#define APLL_FREQ_MAX           0xA774U
#define DRIFT_MEAS_PERIOD_US    100000U
#define DRIFT_ERR_THRESH_LOCK   16
#define DRIFT_ERR_THRESH_UNLOCK 32
#define APLL_FREQ_ADJ(err_us)   (-((int32_t)(err_us) * 1000) / 331)

enum drift_state {
	DRIFT_INIT,
	DRIFT_CALIB,
	DRIFT_LOCKED,
};

static struct {
	enum drift_state state;
	uint32_t meas_start_us;
	uint16_t center_freq;
	uint16_t apll_freq;
} drift = {
	.state = DRIFT_INIT,
	.center_freq = APLL_FREQ_CENTER,
	.apll_freq = APLL_FREQ_CENTER,
};

static void drift_reset(void)
{
	drift.state = DRIFT_INIT;
	drift.center_freq = APLL_FREQ_CENTER;
	drift.apll_freq = APLL_FREQ_CENTER;
	nrfx_clock_hfclkaudio_config_set(APLL_FREQ_CENTER);
}

void audio_i2s_sdu_ref_update(uint32_t sdu_ref_us)
{
	if (sdu_ref_us == 0) {
		return;
	}

	switch (drift.state) {
	case DRIFT_INIT:
		drift.meas_start_us = sdu_ref_us;
		drift.state = DRIFT_CALIB;
		break;

	case DRIFT_CALIB: {
		uint32_t elapsed = sdu_ref_us - drift.meas_start_us;

		if (elapsed < DRIFT_MEAS_PERIOD_US) {
			break;
		}
		if (elapsed > 3 * DRIFT_MEAS_PERIOD_US) {
			/* Gap in stream — restart measurement window */
			drift.meas_start_us = sdu_ref_us;
			break;
		}

		int32_t err_us = (int32_t)DRIFT_MEAS_PERIOD_US - (int32_t)elapsed;
		int32_t adj = APLL_FREQ_ADJ(err_us);

		drift.center_freq = (uint16_t)CLAMP((int32_t)APLL_FREQ_CENTER + adj,
						    (int32_t)APLL_FREQ_MIN,
						    (int32_t)APLL_FREQ_MAX);
		drift.apll_freq = drift.center_freq;
		nrfx_clock_hfclkaudio_config_set(drift.apll_freq);
		LOG_INF("APLL calib: err=%d us → 0x%04X", err_us, drift.apll_freq);

		drift.meas_start_us = sdu_ref_us;

		if (abs(err_us) <= DRIFT_ERR_THRESH_LOCK) {
			drift.state = DRIFT_LOCKED;
			LOG_INF("APLL locked at 0x%04X", drift.apll_freq);
		}
		break;
	}

	case DRIFT_LOCKED: {
		uint32_t elapsed = sdu_ref_us - drift.meas_start_us;

		if (elapsed < DRIFT_MEAS_PERIOD_US) {
			break;
		}
		if (elapsed > 3 * DRIFT_MEAS_PERIOD_US) {
			drift.state = DRIFT_CALIB;
			drift.meas_start_us = sdu_ref_us;
			LOG_WRN("APLL: gap detected, back to calib");
			break;
		}

		int32_t err_us = (int32_t)DRIFT_MEAS_PERIOD_US - (int32_t)elapsed;
		/* Halved correction magnitude to avoid oscillation when locked */
		int32_t adj = APLL_FREQ_ADJ(err_us / 2);

		drift.apll_freq = (uint16_t)CLAMP((int32_t)drift.center_freq + adj,
						  (int32_t)APLL_FREQ_MIN,
						  (int32_t)APLL_FREQ_MAX);
		nrfx_clock_hfclkaudio_config_set(drift.apll_freq);

		drift.meas_start_us = sdu_ref_us;

		if (abs(err_us) > DRIFT_ERR_THRESH_UNLOCK) {
			drift.state = DRIFT_CALIB;
			drift.center_freq = APLL_FREQ_CENTER;
			LOG_WRN("APLL lost lock (err=%d us), recalibrating", err_us);
		}
		break;
	}
	}
}

#else /* !NRF_CLOCK_HAS_HFCLKAUDIO */

void audio_i2s_sdu_ref_update(uint32_t sdu_ref_us)
{
	ARG_UNUSED(sdu_ref_us);
}

static void drift_reset(void) {}

#endif /* NRF_CLOCK_HAS_HFCLKAUDIO */

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

#if NRF_CLOCK_HAS_HFCLKAUDIO
		nrfx_clock_hfclkaudio_config_set(drift.apll_freq);
		LOG_INF("I2S DMA started (APLL=0x%04X)", drift.apll_freq);
#else
		LOG_INF("I2S DMA started");
#endif
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
