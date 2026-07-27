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
 * Maximum output stereo frames per block: nominal 480 + 1 for a possible
 * sample-insert.  The rate converter normally produces 476 or 477 frames at
 * 47,619 Hz drain; 481 is the ceiling for 48k → 48k plus one insert.
 *
 * Phase 5 ASRC worst-case output at 48k→47,619 Hz with ±2 000 ppm is 478
 * frames — well within 481.  Capacity proof: see tests/unit/asrc/.
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
 * Configured once in audio_sink_init.  Used for silence prefill (all paths)
 * and for the SAMPLE_ADJUST legacy resampling path.
 */
static struct audio_rate_converter rate_ctx;

/*
 * ASRC: stateful cross-block linear-interpolation resampler.
 * Used on AUDIO_RESAMPLER_ASRC_LINEAR (nRF54L15) for rate-mismatch +
 * ppm correction, and on AUDIO_RESAMPLER_IDENTITY (nRF5340) for
 * identity passthrough with zero ppm.
 */
static struct audio_asrc asrc_ctx;

/*
 * Sample-adjustment counters for runtime actuator evidence
 * (legacy SAMPLE_ADJUST path only).  Reset per stream in drift_reset().
 */
static uint32_t insert_count;
static uint32_t drop_count;

static void drift_reset(void)
{
	audio_drift_reset();
	audio_clock_actuator_reset();
	audio_asrc_reset(&asrc_ctx);
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
	audio_asrc_init(&asrc_ctx, 48000, CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ);

	ret = audio_clock_actuator_init();
	if (ret < 0) {
		LOG_ERR("audio_clock_actuator_init failed: %d", ret);
		return ret;
	}

	/* Initialize platform audio timing measurement.
	 * On nRF54L15 this sets up GRTC + TIMER20 + GPPI for
	 * hardware-snapshotted PCLK timer ticks against GRTC.
	 * Must fail loudly if required hardware is unavailable.
	 */
	ret = audio_timing_init();
	if (ret < 0) {
		LOG_ERR("audio_timing_init failed: %d", ret);
		return ret;
	}

	/* Only mark configured after all subsystems succeed */
	configured = true;

	LOG_INF("I2S ready (%d kHz nom, %d-bit, stereo, %d blocks)", SAMPLE_RATE / 1000, BIT_WIDTH,
		BLOCK_COUNT);
	return 0;
}

/*
 * Finalize performance measurement for one audio_sink_push call.
 * Called exactly once at every exit point after cycle_start.
 * @p measuring is the snapshot of @c started at entry; when false,
 * this call was a pre-fill push and no cycle data is recorded.
 */
static void perf_finalize_push(bool measuring, uint32_t t0)
{
	if (measuring) {
		audio_perf_cycle_end(t0, AUDIO_PERF_PATH_SINK_PUSH);
	}
}

/*
 * ── Resampler-specific block fill ─────────────────────────────────
 *
 * Three paths selected at compile time via AUDIO_RESAMPLER_*:
 *
 *   ASRC_LINEAR  – ASRC handles fixed rate mismatch + drift ppm
 *                  (nRF54L15 production).
 *   IDENTITY     – ASRC identity passthrough (nRF5340, APLL handles
 *                  drift at the clock).
 *   SAMPLE_ADJUST – Legacy rate converter + discrete sample insert/
 *                    drop (Phase 5 A/B comparison only).
 */

#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR) || defined(CONFIG_AUDIO_RESAMPLER_IDENTITY)

/**
 * ASRC-based push: allocate slab, run ASRC with (or without) ppm,
 * produce output_frames.
 *
 * Returns 0 on success with @p block filled and @p output_frames set.
 * Returns -ENOSPC on output capacity exceeded.
 * Returns slab-allocation errno on allocation failure.
 */
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

#if defined(CONFIG_AUDIO_RESAMPLER_IDENTITY)
	/* APLL handles drift at the clock — ASRC stays identity. */
	(void)ppm;
	int32_t asrc_ppm = 0;
#else
	int32_t asrc_ppm = ppm;
#endif

	size_t consumed, produced;
	uint32_t t_asrc = audio_perf_cycle_start();
	int asrc_ret = audio_asrc_process(&asrc_ctx, stereo_data, input_frames, (int16_t *)*block,
					  MAX_OUTPUT_FRAMES, asrc_ppm, &consumed, &produced);
	audio_perf_cycle_end(t_asrc, AUDIO_PERF_PATH_ASRC);

	if (asrc_ret == 1) {
		LOG_WRN("ASRC capacity exceeded (output_capacity=%u)", (unsigned)MAX_OUTPUT_FRAMES);
		audio_perf_asrc_capacity_failure();
		k_mem_slab_free(&i2s_slab, *block);
		return -ENOSPC;
	}

	*output_frames = produced;
	return 0;
}

#elif defined(CONFIG_AUDIO_RESAMPLER_SAMPLE_ADJUST)

/**
 * Legacy push: rate converter + discrete sample insert/drop.
 */
static int fill_block_legacy(const int16_t *stereo_data, size_t input_frames, int32_t ppm_unused,
			     void **block, size_t *output_frames)
{
	(void)ppm_unused;

	size_t base_output = audio_rate_converter_next_frames(&rate_ctx, input_frames);
	int adj = audio_clock_actuator_consume_sample_adjustment();

	/* Count adjustments for bounded runtime evidence. */
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

	/* Compute output frames: base - adj (clamped). */
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

#endif /* AUDIO_RESAMPLER_SAMPLE_ADJUST */

/*
 * ── audio_sink_push — unified path ────────────────────────────────
 */

int audio_sink_push(const int16_t *stereo_data, size_t sample_count)
{
	int ret;

	if (!configured) {
		return -EIO;
	}

	/* Phase 5.0: only measure steady-state pushes.  Pre-fill is excluded
	 * by snapshotting 'started' into a local bool before the pre-fill
	 * branch even executes.  The bool (not t0==0 sentinel) gates every
	 * call to perf_finalize_push because zero is a valid cycle counter
	 * value.
	 */
	bool measuring = started;
	uint32_t t0 = measuring ? audio_perf_cycle_start() : 0;

	size_t input_frames = sample_count / CHANNELS;

	/*
	 * Step 1: per-block PI controller update.
	 * Read slab free count BEFORE allocating the next block so
	 * setpoint meaning stays stable.  The controller combines
	 * PCLK frequency feedforward with buffer-phase PI, applies
	 * the result to the clock actuator.
	 *
	 * Skipped during pre-fill: the DMA is not yet running and
	 * frequency feedforward may not have a measurement.
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
	 * Step 2: fill the slab block via the selected resampler.
	 */
	void *block = NULL;
	size_t output_frames = 0;

#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR) || defined(CONFIG_AUDIO_RESAMPLER_IDENTITY)
	ret = fill_block_asrc(stereo_data, input_frames, ppm, &block, &output_frames);
#elif defined(CONFIG_AUDIO_RESAMPLER_SAMPLE_ADJUST)
	ret = fill_block_legacy(stereo_data, input_frames, ppm, &block, &output_frames);
#else
#error "No AUDIO_RESAMPLER_* selected"
#endif
	if (ret < 0) {
		perf_finalize_push(measuring, t0);
		return ret;
	}

	size_t out_bytes = output_frames * CHANNELS * (BIT_WIDTH / 8);

	/*
	 * Phase 5.0: sample queue metrics once per steady-state push.
	 */
	if (started) {
		audio_perf_queue_sample(slab_free, output_frames);
	}

	/*
	 * Step 3: pre-fill or normal queue.
	 */
	if (!started) {
		/* Pre-fill 6 silent blocks (~60 ms) to absorb jitter.
		 * Use the rate converter so the pre-fill matches the
		 * HW drain rate exactly.  Pre-fill never advances the
		 * ASRC source phase — it uses a separate rate-converter
		 * remainder.
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
	 * Step 4: save frame for packet-repeat, then queue.
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
		perf_finalize_push(measuring, t0);
		return ret;
	}

	/*
	 * Step 5: packet-repeat fallback — pad queue when draining.
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
	/* Always reset drift, timing, ASRC state, even if I2S was never
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
