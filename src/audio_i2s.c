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
#include "audio_offload.h"
#include "audio_stats.h"
#include "audio_perf.h"

#if defined(AUDIO_I2S_NATIVE_TEST)
/* Test-owned hook header (tests/unit/audio_i2s_common/).  Only included
 * when the test-only compile definition AUDIO_I2S_NATIVE_TEST is present;
 * production firmware never defines it.
 */
#include "audio_i2s_test_hook.h"
#endif

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
 * Input frame count per push call — dynamic, set by audio_sink_set_input_frames().
 * Defaults to 480 (10 ms @ 48 kHz).  For 7.5 ms frames the decoder produces 360.
 * Only 360 and 480 are supported: any other value (including 0) safely
 * resets to 480 so the identity path can never copy more than
 * 480 * 2 * 2 = 1920 bytes into the fixed 481-frame (1924-byte) slab block.
 */
#define INPUT_FRAMES_10MS 480
#define INPUT_FRAMES_7MS5 360
static uint16_t input_frames = INPUT_FRAMES_10MS;

/* ── Stream admission / drain state ────────────────────────────────
 * One short mutex + condvar serialize push admission, stop drain, and
 * the open waiter (R1).  The mutex is held only around state changes;
 * never across allocation, decode, offload, I2S, or Bluetooth calls.
 *
 *   stream_accepting  push admission open (BAP gate closed→open only)
 *   active_pushes     admitted pushes currently running (drained by stop)
 *   stop_callers      overlapping stop callers; owner finalizes once
 *   stop_finalizing   owner between drain and finalization; late joiners
 *                     wait on this instead of re-running the reset
 */
static K_MUTEX_DEFINE(stream_mutex);
static K_CONDVAR_DEFINE(stream_condvar);
static bool stream_accepting;
static uint32_t active_pushes;
static uint32_t stop_callers;
static bool stop_finalizing;
#if defined(AUDIO_I2S_NATIVE_TEST)
/* Test-only waiter observability: stop callers currently blocked in the
 * finalization condvar wait (protected by stream_mutex).  Absent from
 * production builds (compile-time guarded like every other test hook). */
static uint32_t stop_waiters;
#endif

void audio_sink_set_input_frames(uint16_t frames)
{
	uint16_t v = (frames == INPUT_FRAMES_7MS5 || frames == INPUT_FRAMES_10MS)
			     ? frames
			     : INPUT_FRAMES_10MS;

	k_mutex_lock(&stream_mutex, K_FOREVER);
	input_frames = v;
	k_mutex_unlock(&stream_mutex);
}

/*
 * Maximum output stereo frames per block.
 * ASRC worst-case at 48k→47 619 Hz with −2 000 ppm fits
 * within 481.  Capacity proof: tests/unit/asrc/.
 */
#define MAX_OUTPUT_FRAMES 481
#define BLOCK_SIZE        ((size_t)(MAX_OUTPUT_FRAMES) * CHANNELS * (BIT_WIDTH / 8))
#define BLOCK_COUNT       16

#define DRIFT_THRESHOLD (BLOCK_COUNT - 4)

/* Startup pre-fill depth: fourteen distinct silence blocks plus the first
 * data block (15 total).  Fifteen 7.5 ms blocks provide a 112.5 ms reservoir
 * (about 150 ms at 10 ms), so short controller callback gaps at PipeWire
 * suspend cannot drain nrfx I2S into ERROR before ASCS Disable arrives.  Slab
 * capacity and the nrfx TX queue depth both support 15 startup blocks
 * (compile-time proven below).
 */
#define STARTUP_SILENCE_BLOCKS 14
#define STARTUP_TOTAL_BLOCKS   (STARTUP_SILENCE_BLOCKS + 1)

BUILD_ASSERT(STARTUP_TOTAL_BLOCKS <= BLOCK_COUNT, "startup pre-fill exceeds slab block capacity");
BUILD_ASSERT(STARTUP_TOTAL_BLOCKS < BLOCK_COUNT,
	     "startup pre-fill reserves one slab block for first post-START push");
#if !defined(AUDIO_I2S_NATIVE_TEST)
BUILD_ASSERT(STARTUP_TOTAL_BLOCKS <= CONFIG_I2S_NRFX_TX_BLOCK_COUNT,
	     "startup pre-fill exceeds nrfx I2S TX queue depth");
#endif

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
static uint32_t offload_sequence; /* monotonic per-stereo-block counter */
#endif

static void drift_reset(void)
{
	audio_drift_reset();
	audio_clock_actuator_reset();
#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
	audio_asrc_reset(&asrc_ctx);
	asrc_prev_valid = false;
	offload_sequence = 0;
#endif
	/* Saved-frame state must not leak across stream stop: the repeat
	 * fallback may only re-queue a frame from the current stream.
	 */
	saved_frame_len = 0;
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
		/* Board-selected bounded wait for a full TX queue.  The default
		 * stays nonblocking for nRF5340; nRF54L15 uses a finite wait. */
		.timeout = CONFIG_AUDIO_I2S_WRITE_TIMEOUT_MS,
	};

	return i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
}

int audio_sink_init(void)
{
	/* Idempotent re-initialization: a configured sink — possibly with an
	 * active DMA queue, drift/ASRC continuity, and negotiated input frame
	 * selection — must never be re-initialized.  An accidental repeated
	 * call returns success immediately without touching device-ready,
	 * configure, dependency init, started, saved frame, input frame
	 * selection, ASRC/offload state, slab ownership, or the I2S queue,
	 * and without issuing any DROP/PREPARE (stream control belongs to
	 * audio_sink_stop()).
	 *
	 * R1 repair: `configured` is published and checked at the API
	 * boundaries under the stream mutex (the idempotence read here and
	 * the successful publication at function end), matching the
	 * push/stop/open admission checks.  Init stays boot-owned: no
	 * parallel first-initialization support, no mutex held across the
	 * device/dependency calls below.
	 */
	k_mutex_lock(&stream_mutex, K_FOREVER);
	bool already_configured = configured;

	k_mutex_unlock(&stream_mutex);
	if (already_configured) {
		return 0;
	}

	/* First initialization always starts from clean not-started/saved
	 * state, so a failed attempt leaves configured false and permits a
	 * later retry that performs the full normal init exactly once.
	 */
	started = false;
	saved_frame_len = 0;

	i2s_dev = DEVICE_DT_GET(I2S_NODE);
#if defined(AUDIO_I2S_NATIVE_TEST)
	if (!audio_i2s_test_device_is_ready(i2s_dev)) {
#else
	if (!device_is_ready(i2s_dev)) {
#endif
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

	/* Publish the successful state under the stream mutex at function
	 * end: configured=true with the default closed admission (only
	 * audio_sink_stream_open() — the BAP gate closed→open transition —
	 * restores it).  All failed paths above left configured=false. */
	k_mutex_lock(&stream_mutex, K_FOREVER);
	configured = true;
	stream_accepting = false;
	k_mutex_unlock(&stream_mutex);

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

/* ── resampler-specific block fill ───────────────────────────────── */

static int alloc_main_block(bool stream_started, void **block)
{
	k_timeout_t timeout = K_NO_WAIT;
	bool waits = stream_started && CONFIG_AUDIO_I2S_WRITE_TIMEOUT_MS > 0;

	if (waits) {
		timeout = K_MSEC(CONFIG_AUDIO_I2S_WRITE_TIMEOUT_MS);
	}

#if defined(AUDIO_I2S_NATIVE_TEST)
	audio_i2s_test_note_main_slab_alloc(waits);
#endif

	int ret = k_mem_slab_alloc(&i2s_slab, block, timeout);

	if (ret < 0) {
		LOG_WRN("I2S slab full — dropping frame");
		audio_stats_i2s_underrun();
	}

	return ret;
}

#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)

static int fill_block_asrc(uint16_t input_frames_snapshot, const int16_t *stereo_data, int32_t ppm,
			   bool stream_started, void **block, size_t *output_frames)
{
	int ret = alloc_main_block(stream_started, block);

	if (ret < 0) {
		return ret;
	}

	memset(*block, 0, BLOCK_SIZE);

	/* ── Export current CPU ASRC state for offload ─────────── */
	struct audio_asrc_state cpu_state;
	audio_asrc_state_export(&asrc_ctx, asrc_prev_l, asrc_prev_r, asrc_prev_valid, &cpu_state);

	bool used_offload = false;
	size_t produced = 0;
	int16_t next_l = 0, next_r = 0;

#if defined(CONFIG_AUDIO_OFFLOAD_ASRC)
	/* ── Try FLPR ASRC offload ──────────────────────────────── */
	struct audio_offload_asrc_result off_result;
	memset(&off_result, 0, sizeof(off_result));

	int off_ret = audio_offload_process_asrc(stereo_data, input_frames_snapshot,
						 offload_sequence, ppm, &cpu_state,
						 (int16_t *)*block, MAX_OUTPUT_FRAMES, &off_result);

	/* Only a validated round trip with output in [1, 481] is usable.
	 * Zero-frame (valid FLPR error response) and oversized outputs must
	 * not be treated as usable; both fall back to CPU ASRC from the
	 * unchanged exported pre-state.
	 */
	if (off_ret == 0 && off_result.output_frames >= 1 &&
	    off_result.output_frames <= MAX_OUTPUT_FRAMES) {
		/* Transactionally import post-state into temp context. */
		struct audio_asrc temp_ctx;
		int16_t tmp_prev_l, tmp_prev_r;
		bool tmp_prev_valid;
		int imp_ret = audio_asrc_state_import(&temp_ctx, &off_result.post_state,
						      &tmp_prev_l, &tmp_prev_r, &tmp_prev_valid);

		if (imp_ret == 0) {
			/* State update: overwrite asrc_ctx + prev from the
			 * validated offload result. */
			memcpy(&asrc_ctx, &temp_ctx, sizeof(asrc_ctx));
			asrc_prev_l = tmp_prev_l;
			asrc_prev_r = tmp_prev_r;
			asrc_prev_valid = tmp_prev_valid;
			used_offload = true;
			produced = off_result.output_frames;
		} else {
			/* Post-state import rejected — fall through to cpu ASRC. */
			LOG_WRN("ASRC offload post-state import rejected, falling back to cpu");
		}
	}
	/* Any offload fault (EAGAIN/EINVAL/etc.), invalid frame range, or
	 * import rejection falls through to cpu ASRC from the unchanged
	 * pre-state.  The CPU run overwrites any untrusted offload output. */
#endif /* CONFIG_AUDIO_OFFLOAD_ASRC */

	if (!used_offload) {
		/* ── CPU fallback: run ASRC from unchanged pre-state ── */
		size_t consumed;
		uint32_t t_asrc = audio_perf_cycle_start();
		int asrc_ret = audio_asrc_process(&asrc_ctx, stereo_data, input_frames_snapshot,
						  (int16_t *)*block, MAX_OUTPUT_FRAMES, ppm,
						  asrc_prev_l, asrc_prev_r, asrc_prev_valid,
						  &consumed, &produced, &next_l, &next_r);
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

		/* Nominal success must still produce a usable frame count.
		 * produced 0 or > 481 cannot be written to I2S.
		 */
		if (produced < 1 || produced > MAX_OUTPUT_FRAMES) {
			LOG_WRN("ASRC produced %zu frames — invalid", produced);
			k_mem_slab_free(&i2s_slab, *block);
			return -ENOSPC;
		}

		asrc_prev_l = next_l;
		asrc_prev_r = next_r;
		asrc_prev_valid = true;
	}

	offload_sequence++;
	*output_frames = produced;
	return 0;
}

#else /* AUDIO_RESAMPLER_IDENTITY */

static int fill_block_identity(uint16_t input_frames_snapshot, const int16_t *stereo_data,
			       int32_t ppm_unused, bool stream_started, void **block,
			       size_t *output_frames)
{
	(void)ppm_unused;

	int ret = alloc_main_block(stream_started, block);

	if (ret < 0) {
		return ret;
	}

	size_t bytes = (size_t)input_frames_snapshot * CHANNELS * (BIT_WIDTH / 8);

	memcpy(*block, stereo_data, bytes);
	*output_frames = input_frames_snapshot;
	return 0;
}

#endif

/* ── audio_sink_push ──────────────────────────────────────────────── */

/*
 * Admitted-push body.  Runs entirely without the stream mutex: drift,
 * slab allocation/free, ASRC/offload, and i2s_write/trigger may block or
 * take long paths, and stop waits for the drain instead of racing them.
 * Every return passes through the common exit in audio_sink_push() so
 * active_pushes is decremented exactly once per admitted call.
 */
static int do_push(uint16_t input_frames_snapshot, const int16_t *stereo_data, size_t sample_count)
{
	int ret;

	bool stream_started = started;
	bool measuring = stream_started;
	uint32_t t0 = measuring ? audio_perf_cycle_start() : 0;

	int32_t ppm = 0;
	int slab_free = 0;

	if (stream_started) {
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
	ret = fill_block_asrc(input_frames_snapshot, stereo_data, ppm, stream_started, &block,
			      &output_frames);
#else
	ret = fill_block_identity(input_frames_snapshot, stereo_data, ppm, stream_started, &block,
				  &output_frames);
#endif
	if (ret < 0) {
		perf_finalize_push(measuring, t0);
		return ret;
	}

	/* Output bounds: only [1, MAX_OUTPUT_FRAMES] may be written to I2S.
	 * Anything else is a broken fill result; release the caller-owned
	 * block and fail without touching the driver.
	 */
	if (output_frames < 1 || output_frames > MAX_OUTPUT_FRAMES) {
		k_mem_slab_free(&i2s_slab, block);
		perf_finalize_push(measuring, t0);
		return -ENOSPC;
	}

	size_t out_bytes = output_frames * CHANNELS * (BIT_WIDTH / 8);

	if (stream_started) {
		audio_perf_queue_sample(slab_free, output_frames);
	}

	if (!stream_started) {
		/* Transactional startup: queue fourteen distinct silence blocks,
		 * then the data block, then START.  On any allocation/write/
		 * START failure: return the exact primary failure, free every
		 * caller-owned block (failed write or never submitted), and
		 * DROP-purge previously queued driver-owned blocks.  START is
		 * never issued after an incomplete pre-fill.
		 */
		for (int pre = 0; pre < STARTUP_SILENCE_BLOCKS; pre++) {
			size_t pre_frames =
				audio_rate_converter_next_frames(&rate_ctx, input_frames_snapshot);

			if (pre_frames < 1 || pre_frames > MAX_OUTPUT_FRAMES) {
				/* Converter produced an impossible frame count:
				 * never write beyond slab capacity.
				 */
				k_mem_slab_free(&i2s_slab, block);
				i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
				perf_finalize_push(measuring, t0);
				return -ENOSPC;
			}

			size_t pre_bytes = pre_frames * CHANNELS * (BIT_WIDTH / 8);
			void *sil;

			if (k_mem_slab_alloc(&i2s_slab, &sil, K_NO_WAIT) < 0) {
				k_mem_slab_free(&i2s_slab, block);
				i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
				perf_finalize_push(measuring, t0);
				return -ENOMEM;
			}

			memset(sil, 0, pre_bytes);
			ret = i2s_write(i2s_dev, sil, pre_bytes);
			if (ret < 0) {
				audio_perf_i2s_write_failure(ret);
				/* Failed write never took ownership. */
				k_mem_slab_free(&i2s_slab, sil);
				k_mem_slab_free(&i2s_slab, block);
				i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
				perf_finalize_push(measuring, t0);
				return ret;
			}
		}

		ret = i2s_write(i2s_dev, block, out_bytes);
		if (ret < 0) {
			audio_perf_i2s_write_failure(ret);
			k_mem_slab_free(&i2s_slab, block);
			i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
			perf_finalize_push(measuring, t0);
			return ret;
		}

		ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
		if (ret < 0) {
			/* All 15 blocks are driver-owned now; purge via DROP,
			 * never free them directly.
			 */
			i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
			perf_finalize_push(measuring, t0);
			return ret;
		}

		audio_perf_i2s_dma_started();
		LOG_INF("I2S DMA started");
		started = true;
		return 0;
	}

	memcpy(saved_frame, block, out_bytes);
	saved_frame_len = out_bytes;

	uint32_t write_start = audio_perf_i2s_write_start();
	ret = i2s_write(i2s_dev, block, out_bytes);
	audio_perf_i2s_write_end(write_start, ret == 0);
	if (ret < 0) {
		audio_perf_i2s_write_failure(ret);
		k_mem_slab_free(&i2s_slab, block);
		if (ret == -EIO) {
			LOG_WRN("I2S underrun, restarting DMA");
			audio_stats_i2s_underrun();
			i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
			audio_stats_stream_reset();
			audio_perf_i2s_dma_restart();
			started = false;
		}
		perf_finalize_push(measuring, t0);
		return ret;
	}

	if (saved_frame_len > 0 && k_mem_slab_num_free_get(&i2s_slab) >= DRIFT_THRESHOLD) {
		void *dup;

#if defined(AUDIO_I2S_NATIVE_TEST)
		if (!audio_i2s_test_inject_slab_alloc_failure() &&
		    k_mem_slab_alloc(&i2s_slab, &dup, K_NO_WAIT) == 0) {
#else
		if (k_mem_slab_alloc(&i2s_slab, &dup, K_NO_WAIT) == 0) {
#endif
			memcpy(dup, saved_frame, saved_frame_len);
			uint32_t repeat_start = audio_perf_i2s_write_start();
			int repeat_ret = i2s_write(i2s_dev, dup, saved_frame_len);
			audio_perf_i2s_write_end(repeat_start, repeat_ret == 0);
			if (repeat_ret < 0) {
				audio_perf_i2s_write_failure(repeat_ret);
				k_mem_slab_free(&i2s_slab, dup);
			}
		}
		audio_perf_repeat_fallback();
	}

	perf_finalize_push(measuring, t0);
	return 0;
}

int audio_sink_push(const int16_t *stereo_data, size_t sample_count)
{
	/* Basic validation first (null / empty / odd), preserving the
	 * malformed-input -EINVAL precedence over configured/open checks. */
	if (!stereo_data || sample_count == 0 || (sample_count & 1u)) {
		return -EINVAL;
	}

	/* Admission + one local input-frame snapshot under the stream mutex.
	 * The exact sample-count validation runs against the snapshot BEFORE
	 * the configured/open checks (same -EINVAL precedence as before);
	 * fill/prefill/rate conversion use only the snapshot afterwards, so
	 * even an unexpected setter call cannot change a block mid-push. */
	k_mutex_lock(&stream_mutex, K_FOREVER);

	uint16_t snapshot = input_frames;

	if (sample_count / CHANNELS != (size_t)snapshot) {
		k_mutex_unlock(&stream_mutex);
		return -EINVAL;
	}
	if (!configured) {
		k_mutex_unlock(&stream_mutex);
		return -EIO;
	}
	if (!stream_accepting) {
		k_mutex_unlock(&stream_mutex);
		return -EBUSY;
	}
	active_pushes++;
	k_mutex_unlock(&stream_mutex);

	int ret = do_push(snapshot, stereo_data, sample_count);

	/* One common exit: every admitted push decrements exactly once; the
	 * transition to zero broadcasts so a draining stop can proceed. */
	k_mutex_lock(&stream_mutex, K_FOREVER);
	__ASSERT(active_pushes > 0, "push exit without admission");
	active_pushes--;
	if (active_pushes == 0) {
		k_condvar_broadcast(&stream_condvar);
	}
	k_mutex_unlock(&stream_mutex);

	return ret;
}

void audio_sink_stop(void)
{
	k_mutex_lock(&stream_mutex, K_FOREVER);

	/* Rule 3: close admission, claim the owner role from the
	 * pre-increment caller count, and set finalizing before waiting. */
	stream_accepting = false;

	bool owner = (stop_callers == 0);

	stop_callers++;
	if (owner) {
		stop_finalizing = true;
	}

	if (owner) {
		/* Rule 5: wait for every admitted push to fully exit (the
		 * condition wait releases the mutex).  No timeout-and-proceed:
		 * proceeding to DROP while a push still runs recreates the
		 * corruption this phase removes. */
		while (active_pushes != 0) {
			k_condvar_wait(&stream_condvar, &stream_mutex, K_FOREVER);
		}

		/* Snapshot/clear started while protected, then run the
		 * existing drift/timing reset and PREPARE/DROP outside the
		 * mutex (they may take long paths). */
		bool was_started = started;

		started = false;
		k_mutex_unlock(&stream_mutex);

		drift_reset();
		audio_timing_reset();

		if (was_started) {
			i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
			i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
		}

		k_mutex_lock(&stream_mutex, K_FOREVER);
		stop_finalizing = false;

		/* Wake every overlapping non-owner joiner that slept on the
		 * finalization flag BEFORE the caller-count decrement: with
		 * one sleeping non-owner the owner's decrement takes the
		 * cohort 2→1 and the last-caller broadcast below never
		 * fires, so without this broadcast that joiner would sleep
		 * forever.  The last-caller broadcast below remains for
		 * audio_sink_stream_open() waiters. */
		k_condvar_broadcast(&stream_condvar);
	} else {
		/* Rule 4: an early joiner waits for the owner's finalization;
		 * a late joiner arriving after it skips the loop. */
		while (stop_finalizing) {
#if defined(AUDIO_I2S_NATIVE_TEST)
			/* Test-only observability: a joiner about to sleep on
			 * the finalization condvar (stream_mutex held here and
			 * re-held when the wait returns). */
			stop_waiters++;
#endif
			k_condvar_wait(&stream_condvar, &stream_mutex, K_FOREVER);
#if defined(AUDIO_I2S_NATIVE_TEST)
			stop_waiters--;
#endif
		}
	}

	/* Every caller decrements exactly once; the last caller broadcasts
	 * so a waiting open() cannot sleep past the cohort completion. */
	stop_callers--;
	if (stop_callers == 0) {
		k_condvar_broadcast(&stream_condvar);
	}
	k_mutex_unlock(&stream_mutex);
}

int audio_sink_stream_open(void)
{
	k_mutex_lock(&stream_mutex, K_FOREVER);

	if (!configured) {
		k_mutex_unlock(&stream_mutex);
		return -EIO;
	}

	/* Wait for any overlapping stop cohort to fully finish (including
	 * the owner's DROP/reset, which runs before it decrements), so a
	 * reopen can never land between drain completion and DROP/reset. */
	while (stop_callers != 0) {
		k_condvar_wait(&stream_condvar, &stream_mutex, K_FOREVER);
	}

	stream_accepting = true;
	k_mutex_unlock(&stream_mutex);
	return 0;
}

void audio_sink_stream_close(void)
{
	k_mutex_lock(&stream_mutex, K_FOREVER);
	stream_accepting = false;
	k_mutex_unlock(&stream_mutex);
}

/* ── Narrow test hooks (AUDIO_I2S_NATIVE_TEST only) ────────────────
 *
 * Compiled only into test builds that define AUDIO_I2S_NATIVE_TEST via
 * test CMake compile definitions.  Production firmware never defines the
 * macro, so none of these symbols or branches exist in production images
 * and there is no production runtime overhead.
 *
 * Device-readiness and repeat-fallback slab-allocation failure are
 * test-controlled; module-state snapshots/reset and the slab accessor
 * read production module-static state directly.
 */
#if defined(AUDIO_I2S_NATIVE_TEST)
/* GCOVR_EXCL_START — test-only helpers, absent from production builds */
static bool test_device_ready = true;
static bool test_inject_slab_alloc_fail;
static struct k_sem *test_main_slab_alloc_entered;
static bool test_last_main_slab_alloc_waited;

bool audio_i2s_test_device_is_ready(const struct device *dev)
{
	(void)dev;
	return test_device_ready;
}

void audio_i2s_test_set_device_ready(bool ready)
{
	test_device_ready = ready;
}

bool audio_i2s_test_inject_slab_alloc_failure(void)
{
	return test_inject_slab_alloc_fail;
}

void audio_i2s_test_set_slab_alloc_failure(bool fail)
{
	test_inject_slab_alloc_fail = fail;
}

void audio_i2s_test_arm_main_slab_alloc(struct k_sem *entered)
{
	test_main_slab_alloc_entered = entered;
	test_last_main_slab_alloc_waited = false;
}

bool audio_i2s_test_last_main_slab_alloc_waited(void)
{
	return test_last_main_slab_alloc_waited;
}

void audio_i2s_test_note_main_slab_alloc(bool waits)
{
	test_last_main_slab_alloc_waited = waits;

	struct k_sem *entered = test_main_slab_alloc_entered;
	test_main_slab_alloc_entered = NULL;
	if (entered != NULL) {
		k_sem_give(entered);
	}
}

void audio_i2s_test_reset_module_state(void)
{
	k_mutex_lock(&stream_mutex, K_FOREVER);
	configured = false;
	started = false;
	input_frames = INPUT_FRAMES_10MS;
	saved_frame_len = 0;
	stream_accepting = false;
	active_pushes = 0;
	stop_callers = 0;
	stop_finalizing = false;
	stop_waiters = 0;
	test_main_slab_alloc_entered = NULL;
	test_last_main_slab_alloc_waited = false;
	k_mutex_unlock(&stream_mutex);
	memset(&rate_ctx, 0, sizeof(rate_ctx));
#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
	memset(&asrc_ctx, 0, sizeof(asrc_ctx));
	asrc_prev_l = 0;
	asrc_prev_r = 0;
	asrc_prev_valid = false;
	offload_sequence = 0;
#endif
}

bool audio_i2s_test_is_configured(void)
{
	return configured;
}

bool audio_i2s_test_is_started(void)
{
	return started;
}

/* Lock-protected admission/drain snapshots (R1 concurrency tests). */
bool audio_i2s_test_is_accepting(void)
{
	k_mutex_lock(&stream_mutex, K_FOREVER);
	bool v = stream_accepting;
	k_mutex_unlock(&stream_mutex);
	return v;
}

uint32_t audio_i2s_test_active_pushes(void)
{
	k_mutex_lock(&stream_mutex, K_FOREVER);
	uint32_t v = active_pushes;
	k_mutex_unlock(&stream_mutex);
	return v;
}

uint32_t audio_i2s_test_stop_callers(void)
{
	k_mutex_lock(&stream_mutex, K_FOREVER);
	uint32_t v = stop_callers;
	k_mutex_unlock(&stream_mutex);
	return v;
}

bool audio_i2s_test_stop_finalizing(void)
{
	k_mutex_lock(&stream_mutex, K_FOREVER);
	bool v = stop_finalizing;
	k_mutex_unlock(&stream_mutex);
	return v;
}

/* Lock-protected count of stop callers currently sleeping in the
 * finalization condvar wait (deterministic non-owner-wakeup proof). */
uint32_t audio_i2s_test_stop_waiters(void)
{
	k_mutex_lock(&stream_mutex, K_FOREVER);
	uint32_t v = stop_waiters;
	k_mutex_unlock(&stream_mutex);
	return v;
}

uint16_t audio_i2s_test_input_frames(void)
{
	return input_frames;
}

size_t audio_i2s_test_saved_frame_len(void)
{
	return saved_frame_len;
}

uint32_t audio_i2s_test_offload_sequence(void)
{
#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
	return offload_sequence;
#else
	return 0;
#endif
}

int16_t audio_i2s_test_asrc_prev_l(void)
{
#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
	return asrc_prev_l;
#else
	return 0;
#endif
}

int16_t audio_i2s_test_asrc_prev_r(void)
{
#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
	return asrc_prev_r;
#else
	return 0;
#endif
}

bool audio_i2s_test_asrc_prev_valid(void)
{
#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
	return asrc_prev_valid;
#else
	return false;
#endif
}

struct k_mem_slab *audio_i2s_test_get_slab(void)
{
	return &i2s_slab;
}
/* GCOVR_EXCL_STOP */

#endif /* AUDIO_I2S_NATIVE_TEST */
