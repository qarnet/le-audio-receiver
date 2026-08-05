/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_offload.h"
#include "flpr_handshake.h"
#include "flpr_runtime.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

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

/* ── Offload status command (Phase 6 Stage 2) ──────────────────── */

static const char *offload_state_str(enum audio_offload_state st)
{
	switch (st) {
	case AUDIO_OFFLOAD_STOPPED:
		return "STOPPED";
	case AUDIO_OFFLOAD_PREPARING:
		return "PREPARING";
	case AUDIO_OFFLOAD_ACTIVE:
		return "ACTIVE";
	case AUDIO_OFFLOAD_FALLBACK:
		return "FALLBACK";
	case AUDIO_OFFLOAD_RECOVERING:
		return "RECOVERING";
	default:
		return "UNKNOWN";
	}
}

static int cmd_offload_status(const struct shell *sh, size_t argc, char **argv)
{
	(void)argc;
	(void)argv;

	struct audio_offload_status s;
	audio_offload_get_status(&s);

	shell_print(sh, "--- Audio offload ---");
	shell_print(sh, "  State       : %s / epoch=%u gen=%u",
		    s.initialized ? offload_state_str(s.state) : "no-init", s.epoch, s.generation);
	shell_print(sh, "  Counters    : submit=%u success=%u fallback=%u busy=%u", s.submit_count,
		    s.success_count, s.fallback_count, s.busy_count);
	shell_print(sh,
		    "  Faults      : timeout=%u full=%u stale=%u seq=%u frame=%u crc=%u payload=%u",
		    s.timeout_count, s.full_count, s.stale_count, s.seq_fault_count,
		    s.frame_fault_count, s.crc_fault_count, s.payload_fault_count);
	shell_print(sh, "  Recovery    : attempts=%u fail=%u relapses=%u exhaustion=%u",
		    s.recovery_attempts, s.recovery_fail_count, s.recovery_relapses,
		    s.max_exhaustion_count);
	shell_print(sh, "  Probation   : active=%u success=%u cleared=%u",
		    (unsigned)s.probation_active, s.probation_success, s.probation_cleared);

	/* Stage 4B: runtime restart + heartbeat supervisor */
	if (s.runtime_restart_count > 0 || s.runtime_restart_fail > 0) {
		shell_print(sh, "  Runtime     : restarts=%u fails=%u last_ms=%u remote_epoch=%u",
			    s.runtime_restart_count, s.runtime_restart_fail, s.runtime_restart_ms,
			    s.remote_epoch);
	}
	if (s.heartbeat_dedup_count > 0) {
		shell_print(sh, "  HB dedup    : %u", s.heartbeat_dedup_count);
	}

	if (s.rtt_count > 0) {
		uint32_t avg_cyc = (uint32_t)(s.rtt_sum_cycles / s.rtt_count);
		shell_print(sh,
			    "  RTT         : min=%u cyc (%u us) max=%u cyc (%u us) avg=%u cyc (%u "
			    "us) n=%u",
			    s.rtt_min_cycles, k_cyc_to_us_ceil32(s.rtt_min_cycles),
			    s.rtt_max_cycles, k_cyc_to_us_ceil32(s.rtt_max_cycles), avg_cyc,
			    k_cyc_to_us_ceil32(avg_cyc), s.rtt_count);
	} else {
		shell_print(sh, "  RTT         : (none)");
	}

	if (s.last_error != 0) {
		shell_print(sh, "  Last err    : %d at seq %u", s.last_error, s.last_error_seq);
	}

	/* ── ASRC stats ─────────────────────────────────────────── */
#if defined(CONFIG_AUDIO_OFFLOAD_ASRC)
	{
		struct audio_offload_asrc_stats as;
		audio_offload_get_asrc_stats(&as);
		shell_print(sh, "  ── ASRC offload ──");
		shell_print(sh, "    Counters : submit=%u success=%u fallback=%u", as.submit_count,
			    as.success_count, as.fallback_count);
		shell_print(sh,
			    "    Faults   : timeout=%u full=%u stale=%u seq=%u frame=%u crc=%u "
			    "state=%u verify=%u",
			    as.timeout_count, as.full_count, as.stale_count, as.seq_fault_count,
			    as.frame_fault_count, as.crc_fault_count, as.state_fault_count,
			    as.verify_fault_count);
		if (as.rtt_count > 0) {
			uint32_t avg_cyc = (uint32_t)(as.rtt_sum_cycles / as.rtt_count);
			shell_print(sh,
				    "    RTT      : min=%u cyc (%u us) max=%u cyc (%u us) avg=%u "
				    "cyc (%u us) n=%u",
				    as.rtt_min_cycles, k_cyc_to_us_ceil32(as.rtt_min_cycles),
				    as.rtt_max_cycles, k_cyc_to_us_ceil32(as.rtt_max_cycles),
				    avg_cyc, k_cyc_to_us_ceil32(avg_cyc), as.rtt_count);
		}
		if (as.cycles_count > 0) {
			uint32_t avg_cyc = (uint32_t)(as.cycles_sum / as.cycles_count);
			shell_print(sh, "    FLPR cyc : min=%u cyc max=%u cyc avg=%u cyc n=%u",
				    as.cycles_min, as.cycles_max, avg_cyc, as.cycles_count);
		}
	}
#endif

	return 0;
}

/* ── FLPR runtime status command ──────────────────── */

/* Bounded conversion helpers: out-of-range enum values print UNKNOWN
 * instead of indexing past a string array. */
static const char *flpr_runtime_state_str(enum flpr_runtime_state st)
{
	switch (st) {
	case FLPR_RUNTIME_IDLE:
		return "IDLE";
	case FLPR_RUNTIME_BUSY:
		return "BUSY";
	case FLPR_RUNTIME_UNAVAILABLE:
		return "UNAVAILABLE";
	default:
		return "UNKNOWN";
	}
}

static const char *flpr_runtime_stage_str(enum flpr_runtime_stage st)
{
	switch (st) {
	case FLPR_STAGE_DISCONNECT:
		return "disconnect";
	case FLPR_STAGE_STOP:
		return "stop";
	case FLPR_STAGE_ASSERT_RESET:
		return "assert_reset";
	case FLPR_STAGE_COPY:
		return "copy";
	case FLPR_STAGE_FLUSH_BARRIER:
		return "flush_barrier";
	case FLPR_STAGE_CRC_VERIFY:
		return "crc_verify";
	case FLPR_STAGE_INITPC:
		return "initpc";
	case FLPR_STAGE_RECONNECT:
		return "reconnect";
	case FLPR_STAGE_START_CPURUN:
		return "start_cpurun";
	case FLPR_STAGE_RELEASE_RESET:
		return "release_reset";
	case FLPR_STAGE_WAIT_BOUND:
		return "wait_bound";
	case FLPR_STAGE_WAIT_READY:
		return "wait_ready";
	case FLPR_STAGE_SUCCESS:
		return "success";
	default:
		return "unknown";
	}
}

static int cmd_flpr_runtime_status(const struct shell *sh, size_t argc, char **argv)
{
	struct flpr_runtime_status s;
	flpr_runtime_get_status(&s);

	shell_print(sh, "--- FLPR runtime ---");
	shell_print(sh, "  State          : %s", flpr_runtime_state_str(s.state));
	shell_print(sh, "  Failed stage   : %s", flpr_runtime_stage_str(s.failed_stage));
	shell_print(sh, "  Requests       : %u", s.requests);
	shell_print(sh, "  Success        : %u", s.success_count);
	shell_print(sh, "  Failed         : %u", s.fail_count);
	shell_print(sh, "  Busy reject    : %u", s.busy_reject);
	shell_print(sh, "  Epoch          : prev=%u new=%u", s.previous_epoch, s.new_epoch);
	shell_print(sh, "  Reload bytes   : %u", s.reload_bytes);
	shell_print(sh, "  CRC            : src=0x%08x exec=0x%08x", s.source_crc, s.execution_crc);
	shell_print(sh, "  Last errno     : %d", s.last_errno);
	shell_print(sh, "  Duration       : total=%u ms max=%u ms", s.total_duration_ms,
		    s.max_duration_ms);
	shell_print(
		sh,
		"  DMCONTROL      : after_assert=0x%08x before_release=0x%08x after_release=0x%08x",
		s.readbacks.dmcontrol_after_assert, s.readbacks.dmcontrol_before_release,
		s.readbacks.dmcontrol_after_release);
	shell_print(sh, "  INITPC         : after_set=0x%08x", s.readbacks.initpc_after_set);
	shell_print(sh, "  CPURUN         : after_assert=%u after_set=%u",
		    s.readbacks.cpurun_after_assert, s.readbacks.cpurun_after_set);

	return 0;
}

static int cmd_flpr_restart(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t timeout_ms = 10000; /* default 10 s */

	if (argc >= 2) {
		timeout_ms = (uint32_t)shell_strtoul(argv[1], 0, NULL);
	}

	/* Shell retains audio-active guard externally.
	 * Do not restart FLPR while audio stream is active — the recovery
	 * worker handles active-stream FLPR faults. */
	if (audio_offload_is_healthy()) {
		shell_error(sh, "Offload is ACTIVE — cannot restart FLPR. "
				"Use 'flpr hang' to inject a fault and trigger auto-recovery.");
		return -EBUSY;
	}

	shell_print(sh, "FLPR restart: requesting (timeout=%u ms)...", timeout_ms);

	int ret = flpr_runtime_restart(timeout_ms);
	if (ret == 0) {
		struct flpr_runtime_status s;
		flpr_runtime_get_status(&s);
		shell_print(sh, "FLPR restart OK: epoch %u→%u crc=0x%08x duration=total %u ms",
			    s.previous_epoch, s.new_epoch, s.execution_crc, s.total_duration_ms);
	} else {
		shell_error(sh, "FLPR restart FAILED: %d", ret);
	}

	return ret;
}

/* Cross-TU section-based subcommand registration (NCS v3.3.0 shell.h
 * SHELL_SUBCMD_SET_CREATE/SHELL_SUBCMD_ADD).  The acceptance-harness TU
 * (flpr_acceptance_shell.c) adds `ring`, `stress`, and `hang` to this set
 * when CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS is enabled. */
SHELL_SUBCMD_SET_CREATE(flpr_cmds, (flpr));

SHELL_CMD_REGISTER(flpr, &flpr_cmds, "FLPR co-processor commands.", NULL);

SHELL_SUBCMD_ADD((flpr), status, NULL, "FLPR handshake/health status.", cmd_flpr_status, 1, 0);
SHELL_SUBCMD_ADD((flpr), offload, NULL, "Audio offload status (Phase 6 Stage 2).",
		 cmd_offload_status, 1, 0);
SHELL_SUBCMD_ADD((flpr), runtime, NULL, "FLPR runtime restart manager status.",
		 cmd_flpr_runtime_status, 1, 0);
SHELL_SUBCMD_ADD((flpr), restart, NULL, "Restart FLPR co-processor. [timeout_ms default 10000].",
		 cmd_flpr_restart, 1, 1);

#if defined(AUDIO_SHELL_TEST)
/* GCOVR_EXCL_START — test seam wrappers, absent from production builds */
/*
 * Narrow test seams for tests/unit/audio_shell_nrf54.  Expose otherwise-
 * static command handlers to the test suite so real production command
 * bodies are invoked directly.  Never compiled into production firmware.
 */

int audio_shell_test_cmd_flpr_status(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_flpr_status(sh, argc, argv);
}

int audio_shell_test_cmd_offload_status(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_offload_status(sh, argc, argv);
}

int audio_shell_test_cmd_flpr_runtime_status(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_flpr_runtime_status(sh, argc, argv);
}

int audio_shell_test_cmd_flpr_restart(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_flpr_restart(sh, argc, argv);
}

/* GCOVR_EXCL_STOP */
#endif /* AUDIO_SHELL_TEST */
