/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_stats.h"
#include "audio_drift.h"
#include "audio_perf.h"
#include "audio_volume.h"
#include "audio_sink.h"

#include <inttypes.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

static const char *perf_path_names[AUDIO_PERF_NUM_PATHS] = {
	[AUDIO_PERF_PATH_ISO_RECV] = "iso_recv",
	[AUDIO_PERF_PATH_LC3_DECODE] = "lc3_decode",
	[AUDIO_PERF_PATH_VOLUME] = "volume",
	[AUDIO_PERF_PATH_SINK_PUSH] = "sink_push",
};

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct audio_stats s = audio_stats_get();

	uint32_t plc_pct = s.total_frames > 0 ? (s.plc_frames * 100U) / s.total_frames : 0U;

	shell_print(sh, "--- Audio status ---");
	shell_print(sh, "  Frames decoded : %u", s.total_frames);
	shell_print(sh, "  PLC frames     : %u (%u%%)", s.plc_frames, plc_pct);
	shell_print(sh, "  Decode errors  : %u", s.decode_errors);
	shell_print(sh, "  I2S underruns  : %u", s.i2s_underruns);
	shell_print(sh, "  Stream resets  : %u", s.stream_resets);
	shell_print(sh, "  Drift state    : %s", audio_drift_state_str());
	shell_print(sh, "  Drift ppm      : %" PRId32, audio_drift_get_ppm());
	shell_print(sh, "  Volume         : %u / 255%s", audio_volume_get(),
		    audio_volume_is_muted() ? " (muted)" : "");

	return 0;
}

static int cmd_reset_stats(const struct shell *sh, size_t argc, char **argv)
{
	audio_stats_reset();
	shell_print(sh, "Stats cleared.");
	return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	audio_sink_stop();
	shell_print(sh, "I2S stopped; drift reset.");
	return 0;
}
static int cmd_perf(const struct shell *sh, size_t argc, char **argv)
{
	struct audio_perf_path_snapshot paths[AUDIO_PERF_NUM_PATHS];
	struct audio_perf_queue_snapshot queue;

	audio_perf_snapshot(paths, &queue);

#if defined(CONFIG_AUDIO_PERF_MEASUREMENT)
	uint32_t deadline_cyc = k_us_to_cyc_ceil32((uint32_t)CONFIG_AUDIO_PERF_DEADLINE_US);
#else
	uint32_t deadline_cyc = 1; /* avoid div0 */
#endif

	shell_print(sh, "--- Performance ---");
	shell_print(sh, "  Path          Count   Avg(cyc)  Avg(us)  Max(cyc)  Max(us)  %%deadline");
	for (int i = 0; i < AUDIO_PERF_NUM_PATHS; i++) {
		uint32_t count = paths[i].count;
		uint32_t max_cyc = paths[i].max_cycles;
		uint32_t avg_cyc = count ? (uint32_t)(paths[i].total_cycles / count) : 0;
		uint32_t avg_us = k_cyc_to_us_ceil32(avg_cyc);
		uint32_t max_us = k_cyc_to_us_ceil32(max_cyc);

		/* Deadline percentage: use uint64_t to avoid overflow in
		 * max_cyc * 100.  Clamp at 999% so a single outlier does not
		 * break the column layout.
		 */
		uint32_t deadline_pct = 0;
		if (deadline_cyc && max_cyc) {
			uint64_t pct = (uint64_t)max_cyc * 100ULL;
			pct = (pct + deadline_cyc - 1U) / deadline_cyc; /* CEIL */
			deadline_pct = pct > 999U ? 999U : (uint32_t)pct;
		}

		shell_print(sh, "  %-12s  %6u  %8u  %7u  %8u  %7u  %7u%%", perf_path_names[i],
			    count, avg_cyc, avg_us, max_cyc, max_us, deadline_pct);
	}

	shell_print(sh, "  Queue:");
	shell_print(sh, "    Slab free     : %u / %u (min/max)", queue.slab_min_free,
		    queue.slab_max_free);
	shell_print(sh, "    Output frames : %u / %u (min/max)", queue.output_frames_min,
		    queue.output_frames_max);
	shell_print(sh, "    Output blocks : %u", queue.output_blocks);
	shell_print(sh, "    Push failures : %u", queue.push_failures);
	shell_print(sh, "    Repeat fb     : %u", queue.repeat_fallback_count);

	return 0;
}
static int cmd_perf_reset(const struct shell *sh, size_t argc, char **argv)
{
	audio_perf_reset();
	shell_print(sh, "Perf counters cleared.");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	audio_cmds, SHELL_CMD_ARG(status, NULL, "Print audio stats and state.", cmd_status, 1, 0),
	SHELL_CMD_ARG(reset - stats, NULL, "Clear all counters.", cmd_reset_stats, 1, 0),
	SHELL_CMD_ARG(perf, NULL, "Print performance instrumentation.", cmd_perf, 1, 0),
	SHELL_CMD_ARG(perf - reset, NULL, "Clear performance counters.", cmd_perf_reset, 1, 0),
	SHELL_CMD_ARG(stop, NULL, "Stop I2S and reset drift.", cmd_stop, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(audio, &audio_cmds, "LE Audio sink commands.", NULL);
