/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_stats.h"
#include "audio_drift.h"
#include "audio_perf.h"
#include "audio_volume.h"
#include "audio_sink.h"

#if defined(CONFIG_SOC_NRF54L15)
#include "flpr_handshake.h"
#endif

#include <inttypes.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>

#if defined(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR)
#define RESAMPLER_NAME "ASRC linear"
#elif defined(CONFIG_AUDIO_RESAMPLER_IDENTITY)
#define RESAMPLER_NAME "identity"
#else
#define RESAMPLER_NAME "none"
#endif

static const char *perf_path_names[AUDIO_PERF_NUM_PATHS] = {
	[AUDIO_PERF_PATH_ISO_RECV] = "iso_recv", [AUDIO_PERF_PATH_LC3_DECODE] = "lc3_decode",
	[AUDIO_PERF_PATH_VOLUME] = "volume",     [AUDIO_PERF_PATH_SINK_PUSH] = "sink_push",
	[AUDIO_PERF_PATH_ASRC] = "asrc",
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
	shell_print(sh, "  Resampler      : %s", RESAMPLER_NAME);
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

#if !defined(CONFIG_AUDIO_PERF_MEASUREMENT)
	uint32_t deadline_us = 1; /* avoid div0 */
#else
	uint32_t deadline_us = (uint32_t)CONFIG_AUDIO_PERF_DEADLINE_US;
#endif

	shell_print(sh, "--- Performance ---");
	shell_print(sh, "  Path          Count   Avg(cyc)  Avg(us)  Max(cyc)  Max(us)  %%deadline");
	for (int i = 0; i < AUDIO_PERF_NUM_PATHS; i++) {
		uint32_t count = paths[i].count;
		uint32_t max_cyc = paths[i].max_cycles;
		uint32_t avg_cyc = count ? (uint32_t)(paths[i].total_cycles / count) : 0;
		uint32_t avg_us = k_cyc_to_us_ceil32(avg_cyc);
		uint32_t max_us = k_cyc_to_us_ceil32(max_cyc);

		/* Deadline percentage with single-decimal precision
		 * using integer arithmetic: permille = (max_us * 1000) / deadline_us.
		 * Display as permille/10 . permille%10 %.
		 */
		uint32_t permille = 0;
		if (deadline_us && max_us) {
			permille = (uint32_t)(((uint64_t)max_us * 1000ULL) / deadline_us);
		}

		shell_print(sh, "  %-12s  %6u  %8u  %7u  %8u  %7u  %5u.%01u%%", perf_path_names[i],
			    count, avg_cyc, avg_us, max_cyc, max_us, permille / 10, permille % 10);
	}

	shell_print(sh, "  Queue:");
	shell_print(sh, "    Slab free     : %u / %u (min/max)", queue.slab_min_free,
		    queue.slab_max_free);
	shell_print(sh, "    Output frames : %u / %u (min/max)", queue.output_frames_min,
		    queue.output_frames_max);
	shell_print(sh, "    Output blocks : %u", queue.output_blocks);
	shell_print(sh, "    Push failures : %u", queue.push_failures);
	shell_print(sh, "    Repeat fb     : %u", queue.repeat_fallback_count);
	shell_print(sh, "    ASRC cap fail : %u", queue.asrc_capacity_failures);

	return 0;
}
static int cmd_perf_reset(const struct shell *sh, size_t argc, char **argv)
{
	audio_perf_reset();
	shell_print(sh, "Perf counters cleared.");
	return 0;
}

/* bt unpair — test-only, no confirmation, clears all bonds. */
static int cmd_bt_unpair(const struct shell *sh, size_t argc, char **argv)
{
	int ret = bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
	if (ret == 0) {
		shell_print(sh, "All bonds cleared.");
	} else {
		shell_error(sh, "bt_unpair failed: %d", ret);
	}
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

SHELL_STATIC_SUBCMD_SET_CREATE(
	bt_cmds,
	SHELL_CMD_ARG(unpair, NULL, "Clear all Bluetooth bonds (test-only, no confirmation).",
		      cmd_bt_unpair, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(bt, &bt_cmds, "Bluetooth test commands.", NULL);

/* ── bt unpair command (both targets) ──────────────────────────── */

/* ── FLPR shell commands (nRF54L15 only) ───────────────────────── */

#if defined(CONFIG_SOC_NRF54L15)

static int cmd_flpr_status(const struct shell *sh, size_t argc, char **argv)
{
	struct flpr_status s;
	flpr_handshake_get_status(&s);

	shell_print(sh, "--- FLPR handshake ---");
	shell_print(sh, "  Ready        : %s", s.ready ? "yes" : "no");
	shell_print(sh, "  ACKed        : %s", s.acked ? "yes" : "no");
	shell_print(sh, "  Healthy      : %s", s.healthy ? "yes" : "no");
	shell_print(sh, "  Epoch        : %u (ready=%u reboot=%u)", s.epoch, s.ready_count,
		    s.reboot_count);
	shell_print(sh, "  Errors       : len=%u ver=%u unk=%u send=%u", s.err_len, s.err_version,
		    s.err_unknown, s.err_send);
	shell_print(sh, "  TX seq       : %u (acked=%u)", s.tx_seq, s.tx_acked_seq);
	shell_print(sh, "  RX seq       : %u (last=%u ms)", s.rx_seq, s.rx_last_ms);
	shell_print(sh, "  RX lost      : %u", s.rx_lost);
	shell_print(sh, "  RX dup       : %u", s.rx_dup);
	shell_print(sh, "  RX ooo       : %u", s.rx_ooo);
	shell_print(sh, "  RX missed    : %u", s.rx_missed_total);

	if (s.stress_active) {
		shell_print(sh, "  Stress (ACTIVE): count=%u sent=%u recv=%u timeout=%u",
			    s.stress_count, s.stress_sent, s.stress_recv, s.stress_timeouts);
	} else if (s.stress_count > 0) {
		shell_print(sh,
			    "  Stress (done):  count=%u sent=%u recv=%u timeout=%u "
			    "stale=%u mismatch=%u errsend=%u",
			    s.stress_count, s.stress_sent, s.stress_recv, s.stress_timeouts,
			    s.stress_stale, s.stress_mismatch, s.stress_err_send);
	}

	return 0;
}

static int cmd_flpr_stress(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t count = FLPR_STRESS_DEFAULT_COUNT;

	if (argc >= 2) {
		count = (uint32_t)shell_strtoul(argv[1], 0, NULL);
	}

	/* Quick pre-check: reject if not ready or acked. */
	struct flpr_status pre;
	flpr_handshake_get_status(&pre);
	if (!pre.ready || !pre.acked) {
		shell_error(sh, "FLPR not ready/acked — stress rejected");
		return -EAGAIN;
	}
	if (pre.stress_active) {
		shell_error(sh, "Stress already in progress");
		return -EBUSY;
	}

	shell_print(sh, "Starting %u ping/pong stress...", count);

	struct flpr_status s;
	flpr_handshake_stress(count, &s);

	shell_print(sh,
		    "Sent=%u Recv=%u Timeout=%u Stale=%u Mismatch=%u ErrSend=%u "
		    "(of %u requested)",
		    s.stress_sent, s.stress_recv, s.stress_timeouts, s.stress_stale,
		    s.stress_mismatch, s.stress_err_send, count);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(flpr_cmds,
			       SHELL_CMD_ARG(status, NULL, "FLPR handshake/health status.",
					     cmd_flpr_status, 1, 0),
			       SHELL_CMD_ARG(stress, NULL,
					     "Stress test N ping/pong (default 100k, max 1M).",
					     cmd_flpr_stress, 1, 1),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(flpr, &flpr_cmds, "FLPR co-processor commands.", NULL);

#endif /* CONFIG_SOC_NRF54L15 */
