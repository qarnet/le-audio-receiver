/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLPR acceptance-harness shell commands.  Compiled only when
 * CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS is enabled (nRF54L15 board conf);
 * normal audio diagnostics never compile this file.
 *
 * R8: parsing/printing/registration only.  The acceptance orchestration
 * and state (ring test, stalls, stale produce, stress, fault hang, gates
 * 1–6) live in src/flpr_acceptance.c; this file validates arguments,
 * pre-checks readiness, calls the acceptance module, and prints the
 * results byte-identically to the R4 behavior.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "flpr_acceptance.h"
#include "flpr_handshake.h"
#include "flpr_ring.h"
#include "flpr_ring_mgr.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

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
	if (flpr_acceptance_stress_active()) {
		shell_error(sh, "Stress already in progress");
		return -EBUSY;
	}

	shell_print(sh, "Starting %u ping/pong stress...", count);

	struct flpr_status s;
	flpr_acceptance_stress(count, &s);

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
	struct flpr_acceptance_status as;
	flpr_acceptance_get_status(&as);

	shell_print(sh, "--- FLPR PCM rings ---");
	shell_print(sh, "  Initialized   : %s", s.initialized ? "yes" : "no");
	shell_print(sh, "  Epoch         : %u", s.epoch);
	shell_print(sh, "  Input  (→FLPR): prod=%u cons=%u epoch=%u used=%u space=%u",
		    s.in_producer, s.in_consumer, s.in_epoch, s.in_used, s.in_space);
	shell_print(sh, "  Output (→CPU): prod=%u cons=%u epoch=%u used=%u space=%u",
		    s.out_producer, s.out_consumer, s.out_epoch, s.out_used, s.out_space);

	shell_print(sh,
		    "  Diag (CPUAPP): notify=%u err=%u sem_give=%u sem_take=%u stale=%u drained=%u",
		    s.notify_sent, s.notify_err, s.sem_gives, s.sem_takes, s.stale_notify,
		    s.sem_drained);

	if (as.flpr_notify_rcv > 0 || as.flpr_worker_wake > 0) {
		shell_print(sh,
			    "  Diag (FLPR):   notif_rcv=%u worker=%u "
			    "cons_ok=%u cons_empty=%u cons_stale=%u "
			    "prod_ok=%u prod_full=%u",
			    as.flpr_notify_rcv, as.flpr_worker_wake, as.flpr_consume_ok,
			    as.flpr_consume_empty, as.flpr_consume_stale, as.flpr_produce_ok,
			    as.flpr_produce_full);
	}

	if (as.test_active) {
		shell_print(sh, "  Test (ACTIVE): sent=%u recv=%u", as.test_blocks_sent,
			    as.test_blocks_recv);
	} else if (as.test_blocks_sent > 0 || as.test_blocks_recv > 0) {
		shell_print(sh,
			    "  Test (done):  sent=%u recv=%u crc_err=%u payload_err=%u "
			    "full=%u empty=%u stale=%u backpressure=%u",
			    as.test_blocks_sent, as.test_blocks_recv, as.test_crc_errors,
			    as.test_payload_errors, as.test_full_events, as.test_empty_events,
			    as.test_stale_events, as.test_backpressure);
	}

	if (as.latency_count > 0) {
		uint32_t avg_cycles = (uint32_t)(as.latency_sum / as.latency_count);
		uint32_t avg_us = k_cyc_to_us_ceil32(avg_cycles);
		uint32_t min_us = k_cyc_to_us_ceil32(as.latency_min);
		uint32_t max_us = k_cyc_to_us_ceil32(as.latency_max);
		shell_print(sh,
			    "  Latency: min=%u cyc (%u us) max=%u cyc (%u us) "
			    "avg=%u cyc (%u us) count=%u",
			    as.latency_min, min_us, as.latency_max, max_us, avg_cycles, avg_us,
			    as.latency_count);
	}

	/* Timed stall diagnostics (Stage 2). */
	{
		uint32_t acked = flpr_acceptance_flpr_stall_acked();
		uint8_t mask = FLPR_STALL_MASK(acked);
		uint32_t dur = FLPR_STALL_DURATION(acked);
		if (mask != 0 || dur > 0) {
			shell_print(sh,
				    "  Stall (last): mask=0x%02x (cons_in=%d prod_out=%d) "
				    "duration=%u ms",
				    mask, (mask & 0x01) ? 1 : 0, (mask & 0x02) ? 1 : 0, dur);
		}
	}

	return 0;
}

static int cmd_flpr_ring_test(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t count = 10000; /* default: 10k blocks */
	uint32_t rate = 0;      /* 0 = unlimited */

	if (argc >= 2) {
		count = (uint32_t)shell_strtoul(argv[1], 0, NULL);
	}
	if (argc >= 3) {
		rate = (uint32_t)shell_strtoul(argv[2], 0, NULL);
	}
	if (count == 0 || count > 1000000) {
		shell_error(sh, "Block count must be 1–1,000,000");
		return -EINVAL;
	}

	/* Quick pre-check: ensure rings are ready and FLPR is up. */
	struct flpr_ring_status pre;
	flpr_ring_mgr_get_status(&pre);
	if (!pre.initialized || pre.epoch == 0) {
		shell_error(sh, "Rings not initialized — init/reset first");
		return -EAGAIN;
	}

	if (rate > 0) {
		shell_print(sh, "Starting rate-limited ring test: %u blocks, %u blk/s", count,
			    rate);
	} else {
		shell_print(sh, "Starting ring test: %u blocks (~480 stereo frames each)...",
			    count);
	}

	uint32_t start = k_uptime_get_32();

	/* Run the test. */
	uint32_t timeout_ms = count * 5; /* 5 ms per block budget */
	if (timeout_ms < 30000) {
		timeout_ms = 30000;
	}
	/* Rate-limited tests get proportionally more time. */
	if (rate > 0 && rate < 1000) {
		uint32_t rate_timeout = (count * 1000U) / rate + 30000;
		if (rate_timeout > timeout_ms) {
			timeout_ms = rate_timeout;
		}
	}
	struct flpr_acceptance_status s;
	int ret;
	if (rate > 0) {
		ret = flpr_acceptance_test_run_rate(count, timeout_ms, rate, &s);
	} else {
		ret = flpr_acceptance_test_run(count, timeout_ms, &s);
	}

	uint32_t elapsed = k_uptime_get_32() - start;

	shell_print(sh,
		    "Sent=%u Recv=%u CRC_Err=%u Payload_Err=%u Full=%u Empty=%u Stale=%u "
		    "Backpressure=%u FLPR=%u FLPR_Full=%u",
		    s.test_blocks_sent, s.test_blocks_recv, s.test_crc_errors,
		    s.test_payload_errors, s.test_full_events, s.test_empty_events,
		    s.test_stale_events, s.test_backpressure, s.test_producer_blocks,
		    s.test_output_full);
	shell_print(sh, "Duration: %u ms", elapsed);

	/* Latency if measured. */
	if (s.latency_count > 0) {
		uint32_t avg_cycles = (s.latency_sum > 0 && s.latency_count > 0)
					      ? (uint32_t)(s.latency_sum / s.latency_count)
					      : 0;
		uint32_t avg_us = k_cyc_to_us_ceil32(avg_cycles);
		uint32_t min_us = k_cyc_to_us_ceil32(s.latency_min);
		uint32_t max_us = k_cyc_to_us_ceil32(s.latency_max);
		shell_print(sh, "Latency: min=%u us  max=%u us  avg=%u us  count=%u", min_us,
			    max_us, avg_us, s.latency_count);
	}

	if (ret == 0 && s.test_blocks_sent == count && s.test_payload_errors == 0 &&
	    s.test_crc_errors == 0 && s.test_stale_events == 0) {
		shell_print(sh, "PASS: all %u blocks transferred, zero errors", s.test_blocks_recv);
	} else {
		shell_warn(sh, "FAIL: sent=%u/%u recv=%u crc=%u payload=%u stale=%u",
			   s.test_blocks_sent, count, s.test_blocks_recv, s.test_crc_errors,
			   s.test_payload_errors, s.test_stale_events);
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

static int cmd_flpr_ring_stall_producer(const struct shell *sh, size_t argc, char **argv)
{
	bool stall = true;
	if (argc >= 2) {
		if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0) {
			stall = false;
		}
	}
	flpr_acceptance_stall_producer(stall);
	shell_print(sh, "Producer stall: %s", stall ? "ON" : "OFF");
	return 0;
}

static int cmd_flpr_ring_stall_flpr(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t bits = 0;
	if (argc >= 2) {
		bits = (uint8_t)shell_strtoul(argv[1], 0, NULL);
	}
	int ret = flpr_acceptance_flpr_stall(bits, 5000);
	if (ret == 0) {
		shell_print(sh, "FLPR stall applied: 0x%02x (cons_in=%d prod_out=%d)", bits,
			    (bits & 0x01) ? 1 : 0, (bits & 0x02) ? 1 : 0);
	} else {
		shell_error(sh, "FLPR stall failed: %d", ret);
	}
	return ret;
}

static int cmd_flpr_ring_stall_flpr_ms(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3) {
		shell_error(sh, "Usage: flpr ring stall_flpr_ms <bits> <duration_ms>");
		return -EINVAL;
	}

	uint8_t bits = (uint8_t)shell_strtoul(argv[1], 0, NULL);
	uint32_t duration_ms = (uint32_t)shell_strtoul(argv[2], 0, NULL);

	if (bits == 0 && duration_ms > 0) {
		shell_error(sh, "Timed stall with zero mask rejected");
		return -EINVAL;
	}
	if (duration_ms > 0x00FFFFFFUL) {
		shell_error(sh, "Duration %u exceeds max %u ms", duration_ms,
			    (unsigned)0x00FFFFFFUL);
		return -EINVAL;
	}

	int ret = flpr_acceptance_flpr_stall_timed(bits, duration_ms, 5000);
	if (ret == 0) {
		shell_print(sh,
			    "FLPR timed stall applied: bits=0x%02x duration=%u ms "
			    "(cons_in=%d prod_out=%d)",
			    bits, duration_ms, (bits & 0x01) ? 1 : 0, (bits & 0x02) ? 1 : 0);
	} else {
		shell_error(sh, "FLPR timed stall failed: %d", ret);
	}
	return ret;
}

/* Gate output sink context + forward declaration (defined below). */
struct shell_gate_ctx {
	const struct shell *sh;
};

static void shell_gate_print(void *ctx, enum flpr_acceptance_print_level lvl, const char *line);

static int cmd_flpr_ring_acceptance(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t count = 100000;
	if (argc >= 2) {
		count = (uint32_t)shell_strtoul(argv[1], 0, NULL);
	}
	if (count == 0 || count > 10000000) {
		shell_error(sh, "Block count must be 1–10,000,000");
		return -EINVAL;
	}

	struct flpr_ring_status pre;
	flpr_ring_mgr_get_status(&pre);
	if (!pre.initialized) {
		shell_error(
			sh,
			"Rings not initialized — 'flpr ring init' then 'flpr ring reset' first");
		return -EAGAIN;
	}

	/* R8: gate orchestration (1–6) moved to the acceptance module;
	 * the shell only forwards output through the severity-aware sink
	 * (byte-identical lines including error/warn coloring). */
	struct shell_gate_ctx ctx = {
		.sh = sh,
	};

	return flpr_acceptance_run_gates(count, &ctx, shell_gate_print);
}

/* Gate output sink: the acceptance module formats complete lines;
 * map severity to shell_print/warn/error with "%s" (no double
 * formatting — the line is a plain argument). */
static void shell_gate_print(void *ctx, enum flpr_acceptance_print_level lvl, const char *line)
{
	const struct shell *sh = ((struct shell_gate_ctx *)ctx)->sh;

	switch (lvl) {
	case FLPR_ACC_PRINT_ERROR:
		shell_error(sh, "%s", line);
		break;
	case FLPR_ACC_PRINT_WARN:
		shell_warn(sh, "%s", line);
		break;
	case FLPR_ACC_PRINT_NORMAL:
	default:
		shell_print(sh, "%s", line);
		break;
	}
}

/* flpr hang — inject FLPR hang (test-only, local-UART). */
static int cmd_flpr_hang(const struct shell *sh, size_t argc, char **argv)
{
	(void)argc;
	(void)argv;

	/* Quick pre-check: FLPR must be ready and acked. */
	struct flpr_status pre;
	flpr_handshake_get_status(&pre);
	if (!pre.ready || !pre.acked) {
		shell_error(sh, "FLPR not ready/acked — hang rejected");
		return -EAGAIN;
	}

	shell_print(sh, "Sending FAULT_HANG to FLPR (timeout 500ms)...");

	int ret = flpr_acceptance_send_fault_hang(500);
	if (ret == 0) {
		shell_print(sh, "FAULT_HANG_ACK received — FLPR hang imminent.");
		shell_print(sh, "FLPR has disabled IRQs and is spinning forever.");
		shell_print(sh, "Heartbeat will go unhealthy within 5 s.");
		shell_print(sh, "Recovery will auto-trigger via heartbeat supervisor.");
	} else {
		shell_error(sh, "FAULT_HANG failed: %d (no ACK)", ret);
	}

	return ret;
}

/* Cross-TU section-based registration: this TU owns the nested
 * `flpr ring` set and the parent `ring` entry, plus the root-level
 * acceptance commands `stress` and `hang`.  The `flpr` root and its
 * production diagnostics are registered in src/flpr_shell.c. */
SHELL_SUBCMD_SET_CREATE(flpr_ring_cmds, (flpr, ring));

SHELL_SUBCMD_ADD((flpr, ring), status, NULL, "PCM ring status.", cmd_flpr_ring_status, 1, 0);
SHELL_SUBCMD_ADD((flpr, ring), init, NULL, "Initialize PCM rings.", cmd_flpr_ring_init, 1, 0);
SHELL_SUBCMD_ADD((flpr, ring), reset, NULL, "Reset PCM rings with new epoch.", cmd_flpr_ring_reset,
		 1, 0);
SHELL_SUBCMD_ADD((flpr, ring), test, NULL, "Ring test: <blocks> [rate_blk_per_s]",
		 cmd_flpr_ring_test, 1, 2);
SHELL_SUBCMD_ADD((flpr, ring), acceptance, NULL,
		 "Run full acceptance: loopback, stalls, stale, empty.", cmd_flpr_ring_acceptance,
		 1, 1);
SHELL_SUBCMD_ADD((flpr, ring), stall, NULL,
		 "CPU-side producer stall: on|off. Blocks produce_block as FULL.",
		 cmd_flpr_ring_stall_producer, 1, 1);
SHELL_SUBCMD_ADD((flpr, ring), stall_flpr, NULL,
		 "FLPR-side stall: <bits> (0x01=cons_input 0x02=prod_output 0=clear).",
		 cmd_flpr_ring_stall_flpr, 1, 1);
SHELL_SUBCMD_ADD((flpr, ring), stall_flpr_ms, NULL,
		 "FLPR timed stall: <bits> <duration_ms> (auto-clear after duration).",
		 cmd_flpr_ring_stall_flpr_ms, 2, 1);

SHELL_SUBCMD_ADD((flpr), ring, &flpr_ring_cmds, "PCM ring transport commands.", NULL, 0, 0);
SHELL_SUBCMD_ADD((flpr), stress, NULL, "Stress test N ping/pong (default 100k, max 1M).",
		 cmd_flpr_stress, 1, 1);
SHELL_SUBCMD_ADD((flpr), hang, NULL,
		 "Inject FLPR hang (test-only). Sends FAULT_HANG, waits 500ms for ACK. "
		 "FLPR ACKs then disables IRQs and spins — halts ring+heartbeat. "
		 "Recovery via heartbeat supervisor + runtime restart.",
		 cmd_flpr_hang, 1, 0);

#if defined(AUDIO_SHELL_TEST)
/* GCOVR_EXCL_START — test seam wrapper, absent from production builds */
/*
 * Narrow test seam for tests/unit/audio_shell_nrf54.  Exposes the
 * otherwise-static ring-status handler so the real production command
 * body is invoked directly.  Never compiled into production firmware.
 */

int audio_shell_test_cmd_flpr_ring_status(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_flpr_ring_status(sh, argc, argv);
}

/* GCOVR_EXCL_STOP */
#endif /* AUDIO_SHELL_TEST */
