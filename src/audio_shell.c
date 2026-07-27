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
#include "flpr_ring_mgr.h"
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

static int cmd_flpr_ring_status(const struct shell *sh, size_t argc, char **argv)
{
	struct flpr_ring_status s;
	flpr_ring_mgr_get_status(&s);

	shell_print(sh, "--- FLPR PCM rings ---");
	shell_print(sh, "  Initialized   : %s", s.initialized ? "yes" : "no");
	shell_print(sh, "  Epoch         : %u", s.epoch);
	shell_print(sh, "  Input  (→FLPR): prod=%u cons=%u epoch=%u used=%u space=%u",
		    s.in_producer, s.in_consumer, s.in_epoch, s.in_used, s.in_space);
	shell_print(sh, "  Output (→CPU): prod=%u cons=%u epoch=%u used=%u space=%u",
		    s.out_producer, s.out_consumer, s.out_epoch, s.out_used, s.out_space);

	shell_print(sh, "  Diag (CPUAPP): notify=%u err=%u sem_give=%u sem_take=%u", s.notify_sent,
		    s.notify_err, s.sem_gives, s.sem_takes);

	if (s.flpr_notify_rcv > 0 || s.flpr_worker_wake > 0) {
		shell_print(sh,
			    "  Diag (FLPR):   notif_rcv=%u worker=%u "
			    "cons_ok=%u cons_empty=%u cons_stale=%u "
			    "prod_ok=%u prod_full=%u",
			    s.flpr_notify_rcv, s.flpr_worker_wake, s.flpr_consume_ok,
			    s.flpr_consume_empty, s.flpr_consume_stale, s.flpr_produce_ok,
			    s.flpr_produce_full);
	}

	if (s.test_active) {
		shell_print(sh, "  Test (ACTIVE): sent=%u recv=%u", s.test_blocks_sent,
			    s.test_blocks_recv);
	} else if (s.test_blocks_sent > 0 || s.test_blocks_recv > 0) {
		shell_print(sh,
			    "  Test (done):  sent=%u recv=%u crc_err=%u "
			    "full=%u empty=%u stale=%u",
			    s.test_blocks_sent, s.test_blocks_recv, s.test_crc_errors,
			    s.test_full_events, s.test_empty_events, s.test_stale_events);
	}

	return 0;
}

static int cmd_flpr_ring_test(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t count = 10000; /* default: 10k blocks */

	if (argc >= 2) {
		count = (uint32_t)shell_strtoul(argv[1], 0, NULL);
	}
	if (count == 0) {
		shell_error(sh, "Block count must be > 0");
		return -EINVAL;
	}
	if (count > 1000000) {
		shell_error(sh, "Block count must be ≤ 1,000,000");
		return -EINVAL;
	}

	/* Quick pre-check: ensure rings are ready and FLPR is up. */
	struct flpr_ring_status pre;
	flpr_ring_mgr_get_status(&pre);
	if (!pre.initialized || pre.epoch == 0) {
		shell_error(sh, "Rings not initialized — init/reset first");
		return -EAGAIN;
	}

	shell_print(sh, "Starting ring test: %u blocks (~480 stereo frames each)...", count);

	uint32_t start = k_uptime_get_32();

	/* Run the test. */
	uint32_t timeout_ms = count * 5; /* 5 ms per block budget */
	if (timeout_ms < 30000) {
		timeout_ms = 30000;
	}
	struct flpr_ring_status s;
	int ret = flpr_ring_mgr_test_run(count, timeout_ms, &s);

	uint32_t elapsed = k_uptime_get_32() - start;

	shell_print(sh, "Sent=%u Recv=%u CRC_Err=%u Full=%u Empty=%u Stale=%u FLPR=%u FLPR_Full=%u",
		    s.test_blocks_sent, s.test_blocks_recv, s.test_crc_errors, s.test_full_events,
		    s.test_empty_events, s.test_stale_events, s.test_producer_blocks,
		    s.test_output_full);
	shell_print(sh, "Duration: %u ms", elapsed);

	if (ret == 0 && s.test_blocks_sent == count && s.test_crc_errors == 0) {
		shell_print(sh, "PASS: all %u blocks transferred, zero CRC errors",
			    s.test_blocks_recv);
	} else {
		shell_warn(sh, "FAIL/TIMEOUT: sent=%u/%u recv=%u err=%u", s.test_blocks_sent, count,
			   s.test_blocks_recv, s.test_crc_errors);
	}

	return 0;
}

static int cmd_flpr_ring_reset(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t epoch = 0; /* let coordinated_reset generate one */

	int ret = flpr_ring_mgr_coordinated_reset(epoch, 5000);
	if (ret != 0) {
		shell_error(sh, "Coordinated ring reset failed: %d (FLPR may not have acked)", ret);
		return ret;
	}

	shell_print(sh, "Coordinated ring reset OK");

	return 0;
}

static int cmd_flpr_ring_init(const struct shell *sh, size_t argc, char **argv)
{
	int ret = flpr_ring_mgr_init();
	if (ret == 0) {
		shell_print(sh, "PCM rings initialized.");
	} else {
		shell_error(sh, "Ring init failed: %d", ret);
	}
	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	flpr_ring_cmds, SHELL_CMD_ARG(status, NULL, "PCM ring status.", cmd_flpr_ring_status, 1, 0),
	SHELL_CMD_ARG(init, NULL, "Initialize PCM rings.", cmd_flpr_ring_init, 1, 0),
	SHELL_CMD_ARG(reset, NULL, "Reset PCM rings with new epoch.", cmd_flpr_ring_reset, 1, 0),
	SHELL_CMD_ARG(test, NULL, "Run ring throughput test (default 10k blocks).",
		      cmd_flpr_ring_test, 1, 1),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	flpr_cmds,
	SHELL_CMD_ARG(status, NULL, "FLPR handshake/health status.", cmd_flpr_status, 1, 0),
	SHELL_CMD_ARG(stress, NULL, "Stress test N ping/pong (default 100k, max 1M).",
		      cmd_flpr_stress, 1, 1),
	SHELL_CMD(ring, &flpr_ring_cmds, "PCM ring transport commands.", NULL),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(flpr, &flpr_cmds, "FLPR co-processor commands.", NULL);

#endif /* CONFIG_SOC_NRF54L15 */
