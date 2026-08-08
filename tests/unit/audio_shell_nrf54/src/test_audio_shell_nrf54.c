/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Direct tests of the nRF54L15 FLPR status command bodies in
 * src/audio_shell.c (handshake, ring, offload, runtime, restart)
 * through the real Zephyr dummy backend and shell_execute_cmd().
 * FLPR APIs are mocked; the parseable labels and values are locked
 * against the fields consumed by scripts/flpr_hang_gate.py.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#include "audio_offload.h"
#include "audio_shell_test.h"
#include "fake_flpr_deps.h"
#include "flpr_handshake.h"
#include "flpr_ring_mgr.h"
#include "flpr_runtime.h"

/* ── helpers ─────────────────────────────────────────────────────── */

/* The dummy backend's shell thread runs at the lowest application
 * priority and may not have executed shell_start() before the test
 * thread does; drive the real shell state machine into ACTIVE
 * explicitly.  -ENOTSUP means it is already active. */
static const struct shell *active_shell(void)
{
	const struct shell *sh = shell_backend_dummy_get_ptr();
	int ret = shell_start(sh);

	zassert_true(ret == 0 || ret == -ENOTSUP, "shell_start: %d", ret);
	return sh;
}

static const char *run_cmd(const char *cmd, int *rc)
{
	const struct shell *sh = active_shell();

	shell_backend_dummy_clear_output(sh);
	int ret = shell_execute_cmd(sh, cmd);

	if (rc != NULL) {
		*rc = ret;
	}
	return shell_backend_dummy_get_output(sh, &(size_t){0});
}

static void assert_output_contains(const char *out, const char *needle)
{
	zassert_not_null(strstr(out, needle), "output missing '%s':\n%s", needle, out);
}

static void assert_output_has_line(const char *out, const char *line)
{
	char buf[256];
	size_t len = strlen(line);

	zassert_true(len < sizeof(buf) - 2, "line too long: %s", line);
	snprintf(buf, sizeof(buf), "\r\n%s\r\n", line);
	assert_output_contains(out, buf);
}

/* ── suite ───────────────────────────────────────────────────────── */

ZTEST_SUITE(audio_shell_nrf54, NULL, NULL, NULL, NULL, NULL);

/* flpr status: ready/ACKed/healthy/epoch/errors/TX/RX/loss/order fields. */
ZTEST(audio_shell_nrf54, test_flpr_status_exact_fields)
{
	test_flpr_reset();
	struct flpr_status s = {
		.ready = true,
		.acked = true,
		.healthy = true,
		.epoch = 5,
		.ready_count = 3,
		.reboot_count = 1,
		.err_len = 0,
		.err_version = 1,
		.err_unknown = 0,
		.err_send = 2,
		.tx_seq = 100,
		.tx_acked_seq = 99,
		.rx_seq = 50,
		.rx_last_ms = 7,
		.rx_lost = 1,
		.rx_dup = 2,
		.rx_ooo = 3,
		.rx_missed_total = 4,
	};

	test_flpr_set_status(&s);
	int rc = -1;
	const char *out = run_cmd("flpr status", &rc);

	zassert_equal(rc, 0);
	assert_output_has_line(out, "--- FLPR handshake ---");
	assert_output_has_line(out, "  Ready        : yes");
	assert_output_has_line(out, "  ACKed        : yes");
	assert_output_has_line(out, "  Healthy      : yes");
	assert_output_has_line(out, "  Epoch        : 5 (ready=3 reboot=1)");
	assert_output_has_line(out, "  Errors       : len=0 ver=1 unk=0 send=2");
	assert_output_has_line(out, "  TX seq       : 100 (acked=99)");
	assert_output_has_line(out, "  RX seq       : 50 (last=7 ms)");
	assert_output_has_line(out, "  RX lost      : 1");
	assert_output_has_line(out, "  RX dup       : 2");
	assert_output_has_line(out, "  RX ooo       : 3");
	assert_output_has_line(out, "  RX missed    : 4");
}

/* flpr status: negative flags print "no". */
ZTEST(audio_shell_nrf54, test_flpr_status_not_ready)
{
	test_flpr_reset();
	struct flpr_status s = {0};

	test_flpr_set_status(&s);
	const char *out = run_cmd("flpr status", NULL);

	assert_output_has_line(out, "  Ready        : no");
	assert_output_has_line(out, "  ACKed        : no");
	assert_output_has_line(out, "  Healthy      : no");
}

/* flpr ring status: initialized/epoch, both ring counters, CPUAPP diag. */
ZTEST(audio_shell_nrf54, test_flpr_ring_status_exact_fields)
{
	test_flpr_reset();
	struct flpr_ring_status s = {
		.initialized = true,
		.epoch = 42,
		.in_producer = 1,
		.in_consumer = 2,
		.in_epoch = 42,
		.in_used = 3,
		.in_space = 4,
		.out_producer = 5,
		.out_consumer = 6,
		.out_epoch = 42,
		.out_used = 7,
		.out_space = 8,
		.notify_sent = 9,
		.notify_err = 1,
		.sem_gives = 10,
		.sem_takes = 11,
		.stale_notify = 0,
		.sem_drained = 2,
	};

	test_flpr_set_ring_status(&s);
	test_flpr_set_stall_acked(0);
	int rc = -1;
	const char *out = run_cmd("flpr ring status", &rc);

	zassert_equal(rc, 0);
	assert_output_has_line(out, "--- FLPR PCM rings ---");
	assert_output_has_line(out, "  Initialized   : yes");
	assert_output_has_line(out, "  Epoch         : 42");
	assert_output_has_line(out, "  Input  (\xe2\x86\x92"
				    "FLPR): prod=1 cons=2 epoch=42 used=3 space=4");
	assert_output_has_line(out, "  Output (\xe2\x86\x92"
				    "CPU): prod=5 cons=6 epoch=42 used=7 space=8");
	assert_output_has_line(out,
			       "  Diag (CPUAPP): notify=9 err=1 sem_give=10 sem_take=11 stale=0 "
			       "drained=2");
	/* FLPR diag line is conditional: absent when its counters are zero. */
	zassert_is_null(strstr(out, "Diag (FLPR)"), "unexpected FLPR diag line:\n%s", out);
}

/* flpr ring status: FLPR diagnostics present when reported (the
 * FLPR-reported counters live in the acceptance status). */
ZTEST(audio_shell_nrf54, test_flpr_ring_status_flpr_diag)
{
	test_flpr_reset();
	struct flpr_ring_status s = {
		.initialized = true,
		.epoch = 1,
	};
	struct flpr_acceptance_status as = {
		.flpr_notify_rcv = 3,
		.flpr_worker_wake = 2,
		.flpr_consume_ok = 4,
		.flpr_consume_empty = 0,
		.flpr_consume_stale = 0,
		.flpr_produce_ok = 5,
		.flpr_produce_full = 1,
	};

	test_flpr_set_ring_status(&s);
	test_flpr_set_acceptance_status(&as);
	const char *out = run_cmd("flpr ring status", NULL);

	assert_output_contains(out, "Diag (FLPR):   notif_rcv=3 worker=2 cons_ok=4 cons_empty=0 "
				    "cons_stale=0 prod_ok=5 prod_full=1");
}

/* flpr ring status: test-done and latency sections (the test and
 * latency fields live in the acceptance status). */
ZTEST(audio_shell_nrf54, test_flpr_ring_status_test_and_latency)
{
	test_flpr_reset();
	struct flpr_ring_status s = {
		.initialized = true,
		.epoch = 1,
	};
	struct flpr_acceptance_status as = {
		.test_blocks_sent = 10,
		.test_blocks_recv = 9,
		.test_crc_errors = 1,
		.test_payload_errors = 0,
		.test_full_events = 0,
		.test_empty_events = 0,
		.test_stale_events = 0,
		.test_backpressure = 0,
		.latency_count = 3,
		.latency_sum = 3000,
		.latency_min = 500,
		.latency_max = 1500,
	};

	test_flpr_set_ring_status(&s);
	test_flpr_set_acceptance_status(&as);
	const char *out = run_cmd("flpr ring status", NULL);

	assert_output_has_line(out, "  Test (done):  sent=10 recv=9 crc_err=1 payload_err=0 full=0 "
				    "empty=0 stale=0 backpressure=0");
	assert_output_has_line(
		out, "  Latency: min=500 cyc (500 us) max=1500 cyc (1500 us) avg=1000 cyc "
		     "(1000 us) count=3");
}

/* flpr ring status: stall line decodes mask and duration. */
ZTEST(audio_shell_nrf54, test_flpr_ring_status_stall)
{
	test_flpr_reset();
	struct flpr_ring_status s = {.initialized = true, .epoch = 1};

	test_flpr_set_ring_status(&s);
	/* mask=0x03, duration=2500 ms (FLPR_STALL_DURATION shifts >>8). */
	test_flpr_set_stall_acked((uint32_t)0x03 | (2500U << 8));
	const char *out = run_cmd("flpr ring status", NULL);

	assert_output_has_line(out,
			       "  Stall (last): mask=0x03 (cons_in=1 prod_out=1) duration=2500 ms");
}

/* flpr offload: state/epoch/generation, counters, faults, recovery,
 * probation, runtime restart line, heartbeat dedup, RTT. */
ZTEST(audio_shell_nrf54, test_offload_status_exact_fields)
{
	test_flpr_reset();
	struct audio_offload_status s = {
		.initialized = true,
		.healthy = true,
		.state = AUDIO_OFFLOAD_ACTIVE,
		.epoch = 7,
		.generation = 2,
		.submit_count = 100,
		.success_count = 95,
		.fallback_count = 5,
		.busy_count = 1,
		.timeout_count = 1,
		.full_count = 0,
		.stale_count = 0,
		.seq_fault_count = 0,
		.frame_fault_count = 0,
		.crc_fault_count = 0,
		.payload_fault_count = 0,
		.recovery_attempts = 1,
		.recovery_fail_count = 0,
		.recovery_relapses = 0,
		.max_exhaustion_count = 0,
		.probation_active = false,
		.probation_success = 100,
		.probation_cleared = 1,
		.runtime_restart_count = 1,
		.runtime_restart_fail = 0,
		.runtime_restart_ms = 123,
		.remote_epoch = 8,
		.heartbeat_dedup_count = 2,
		.rtt_min_cycles = 100,
		.rtt_max_cycles = 900,
		.rtt_sum_cycles = 1500,
		.rtt_count = 3,
		.last_error = 0,
	};

	test_flpr_set_offload_status(&s);
	int rc = -1;
	const char *out = run_cmd("flpr offload", &rc);

	zassert_equal(rc, 0);
	assert_output_has_line(out, "--- Audio offload ---");
	assert_output_has_line(out, "  State       : ACTIVE / epoch=7 gen=2");
	assert_output_has_line(out, "  Counters    : submit=100 success=95 fallback=5 busy=1");
	assert_output_has_line(
		out, "  Faults      : timeout=1 full=0 stale=0 seq=0 frame=0 crc=0 payload=0");
	assert_output_has_line(out, "  Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0");
	assert_output_has_line(out, "  Probation   : active=0 success=100 cleared=1");
	assert_output_has_line(out,
			       "  Runtime     : restarts=1 fails=0 last_ms=123 remote_epoch=8");
	assert_output_has_line(out, "  HB dedup    : 2");
	assert_output_has_line(
		out, "  RTT         : min=100 cyc (100 us) max=900 cyc (900 us) avg=500 cyc "
		     "(500 us) n=3");
}

/* flpr offload: ASRC section counters/faults/RTT/cycles (gate-parsed). */
ZTEST(audio_shell_nrf54, test_offload_status_asrc_section)
{
	test_flpr_reset();
	struct audio_offload_asrc_stats as = {
		.submit_count = 50,
		.success_count = 48,
		.fallback_count = 2,
		.timeout_count = 0,
		.full_count = 0,
		.stale_count = 0,
		.seq_fault_count = 0,
		.frame_fault_count = 0,
		.crc_fault_count = 0,
		.state_fault_count = 0,
		.verify_fault_count = 0,
		.rtt_min_cycles = 100,
		.rtt_max_cycles = 300,
		.rtt_sum_cycles = 600,
		.rtt_count = 3,
		.cycles_min = 1000,
		.cycles_max = 3000,
		.cycles_sum = 6000,
		.cycles_count = 3,
	};

	test_flpr_set_asrc_stats(&as);
	const char *out = run_cmd("flpr offload", NULL);

	assert_output_contains(out, "ASRC offload");
	assert_output_has_line(out, "    Counters : submit=50 success=48 fallback=2");
	assert_output_has_line(
		out, "    Faults   : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 state=0 "
		     "verify=0");
	assert_output_has_line(
		out, "    RTT      : min=100 cyc (100 us) max=300 cyc (300 us) avg=200 cyc "
		     "(200 us) n=3");
	assert_output_has_line(out, "    FLPR cyc : min=1000 cyc max=3000 cyc avg=2000 cyc n=3");
}

/* flpr offload: no-init state, no RTT, no runtime/HB lines when empty. */
ZTEST(audio_shell_nrf54, test_offload_status_empty_state)
{
	test_flpr_reset();
	struct audio_offload_status s = {0};

	test_flpr_set_offload_status(&s);
	const char *out = run_cmd("flpr offload", NULL);

	assert_output_has_line(out, "  State       : no-init / epoch=0 gen=0");
	assert_output_has_line(out, "  RTT         : (none)");
	zassert_is_null(strstr(out, "Runtime     :"), "unexpected Runtime line:\n%s", out);
	zassert_is_null(strstr(out, "HB dedup"), "unexpected HB dedup line:\n%s", out);
	zassert_is_null(strstr(out, "Last err"), "unexpected Last err line:\n%s", out);
}

/* flpr offload: last error line when set. */
ZTEST(audio_shell_nrf54, test_offload_status_last_error)
{
	test_flpr_reset();
	struct audio_offload_status s = {
		.initialized = true,
		.last_error = -EIO,
		.last_error_seq = 5,
	};

	test_flpr_set_offload_status(&s);
	const char *out = run_cmd("flpr offload", NULL);

	assert_output_has_line(out, "  Last err    : -5 at seq 5");
}

/* flpr runtime: exact labels for all fields incl. register readbacks. */
ZTEST(audio_shell_nrf54, test_flpr_runtime_status_exact_fields)
{
	test_flpr_reset();
	struct flpr_runtime_status s = {
		.state = FLPR_RUNTIME_BUSY,
		.failed_stage = FLPR_STAGE_CRC_VERIFY,
		.requests = 3,
		.success_count = 2,
		.fail_count = 1,
		.busy_reject = 1,
		.previous_epoch = 4,
		.new_epoch = 5,
		.reload_bytes = 65536,
		.source_crc = 0x12345678,
		.execution_crc = 0x12345678,
		.last_errno = 0,
		.total_duration_ms = 1000,
		.max_duration_ms = 600,
		.readbacks =
			{
				.dmcontrol_after_assert = 0x1,
				.dmcontrol_before_release = 0x2,
				.dmcontrol_after_release = 0x3,
				.initpc_after_set = 0x20030000,
				.cpurun_after_set = 1,
				.cpurun_after_assert = 0,
			},
	};

	test_flpr_set_runtime_status(&s);
	int rc = -1;
	const char *out = run_cmd("flpr runtime", &rc);

	zassert_equal(rc, 0);
	assert_output_has_line(out, "--- FLPR runtime ---");
	assert_output_has_line(out, "  State          : BUSY");
	assert_output_has_line(out, "  Failed stage   : crc_verify");
	assert_output_has_line(out, "  Requests       : 3");
	assert_output_has_line(out, "  Success        : 2");
	assert_output_has_line(out, "  Failed         : 1");
	assert_output_has_line(out, "  Busy reject    : 1");
	assert_output_has_line(out, "  Epoch          : prev=4 new=5");
	assert_output_has_line(out, "  Reload bytes   : 65536");
	assert_output_has_line(out, "  CRC            : src=0x12345678 exec=0x12345678");
	assert_output_has_line(out, "  Last errno     : 0");
	assert_output_has_line(out, "  Duration       : total=1000 ms max=600 ms");
	assert_output_has_line(
		out, "  DMCONTROL      : after_assert=0x00000001 before_release=0x00000002 "
		     "after_release=0x00000003");
	assert_output_has_line(out, "  INITPC         : after_set=0x20030000");
	assert_output_has_line(out, "  CPURUN         : after_assert=0 after_set=1");
}

/* flpr runtime: out-of-range state/stage enums print UNKNOWN/unknown
 * instead of indexing past the string arrays. */
ZTEST(audio_shell_nrf54, test_flpr_runtime_status_out_of_range_enums)
{
	test_flpr_reset();
	struct flpr_runtime_status s = {
		.state = (enum flpr_runtime_state)99,
		.failed_stage = (enum flpr_runtime_stage)99,
	};

	test_flpr_set_runtime_status(&s);
	const char *out = run_cmd("flpr runtime", NULL);

	assert_output_has_line(out, "  State          : UNKNOWN");
	assert_output_has_line(out, "  Failed stage   : unknown");
}

/* flpr restart: rejected while offload healthy, -EBUSY propagated. */
ZTEST(audio_shell_nrf54, test_flpr_restart_busy_when_healthy)
{
	test_flpr_reset();
	test_flpr_set_offload_healthy(true);

	int rc = 0;
	const char *out = run_cmd("flpr restart", &rc);

	zassert_equal(rc, -EBUSY);
	assert_output_contains(out, "cannot restart FLPR");
	zassert_equal(test_flpr_restart_calls(), 0);
}

/* flpr restart: success line matches the flpr_hang_gate.py
 * RE_RUNTIME_RESTART_OK shape (epoch change, crc, total duration). */
ZTEST(audio_shell_nrf54, test_flpr_restart_ok_line)
{
	test_flpr_reset();
	test_flpr_set_offload_healthy(false);
	test_flpr_set_restart_result(0);
	struct flpr_runtime_status s = {
		.previous_epoch = 5,
		.new_epoch = 6,
		.execution_crc = 0x1234abcd,
		.total_duration_ms = 100,
	};

	test_flpr_set_runtime_status(&s);
	int rc = -1;
	const char *out = run_cmd("flpr restart", &rc);

	zassert_equal(rc, 0);
	zassert_equal(test_flpr_restart_calls(), 1);
	assert_output_has_line(out, "FLPR restart OK: epoch 5\xe2\x86\x92"
				    "6 crc=0x1234abcd "
				    "duration=total 100 ms");
}

/* flpr restart: failure propagates the exact negative errno. */
ZTEST(audio_shell_nrf54, test_flpr_restart_failure)
{
	test_flpr_reset();
	test_flpr_set_offload_healthy(false);
	test_flpr_set_restart_result(-EIO);

	int rc = 0;
	const char *out = run_cmd("flpr restart", &rc);

	zassert_equal(rc, -EIO);
	assert_output_contains(out, "FLPR restart FAILED: -5");
	zassert_equal(test_flpr_restart_calls(), 1);
}

/* Wrapper seam: nRF54 command handlers are directly callable and
 * behave identically to the registered dispatch. */
ZTEST(audio_shell_nrf54, test_wrapper_seam_flpr)
{
	test_flpr_reset();
	struct flpr_status s = {.ready = true, .acked = true, .healthy = true, .epoch = 9};

	test_flpr_set_status(&s);
	const struct shell *sh = active_shell();
	int rc = -1;

	shell_backend_dummy_clear_output(sh);
	rc = audio_shell_test_cmd_flpr_status(sh, 1, NULL);
	zassert_equal(rc, 0);

	const char *out = run_cmd("flpr status", NULL);
	assert_output_has_line(out, "  Epoch        : 9 (ready=0 reboot=0)");
}

/* ── FLPR command handlers ─────────────────────────────────────────
 * cmd_flpr_stress / ring test / ring reset / ring init / producer
 * stall / stall_flpr / stall_flpr_ms / ring acceptance / hang —
 * production shell handlers executed through the real shell registry.
 * Outcomes reachable before physical FLPR transport are locked here;
 * long-running successful acceptance stays hardware evidence. */

ZTEST(audio_shell_nrf54, test_flpr_stress_not_ready)
{
	test_flpr_reset();
	struct flpr_status s = {.ready = false, .acked = false};

	test_flpr_set_status(&s);
	int rc = 0;
	const char *out = run_cmd("flpr stress", &rc);

	zassert_equal(rc, -EAGAIN);
	assert_output_contains(out, "FLPR not ready/acked — stress rejected");
}

ZTEST(audio_shell_nrf54, test_flpr_stress_already_active)
{
	test_flpr_reset();
	struct flpr_status s = {.ready = true, .acked = true};

	test_flpr_set_status(&s);
	test_flpr_set_stress_active(true);
	int rc = 0;
	const char *out = run_cmd("flpr stress", &rc);

	zassert_equal(rc, -EBUSY);
	assert_output_contains(out, "Stress already in progress");
}

ZTEST(audio_shell_nrf54, test_flpr_stress_success_summary)
{
	test_flpr_reset();
	struct flpr_status s = {.ready = true, .acked = true};
	struct flpr_status stress = {
		.stress_active = false,
		.stress_sent = 5,
		.stress_recv = 4,
		.stress_timeouts = 1,
		.stress_stale = 0,
		.stress_mismatch = 0,
		.stress_err_send = 0,
	};

	test_flpr_set_status(&s);
	test_flpr_set_stress_snapshot(&stress);
	int rc = 0;
	const char *out = run_cmd("flpr stress 5", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Starting 5 ping/pong stress...");
	assert_output_contains(
		out, "Sent=5 Recv=4 Timeout=1 Stale=0 Mismatch=0 ErrSend=0 (of 5 requested)");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_test_invalid_count)
{
	test_flpr_reset();
	int rc = 0;
	const char *out = run_cmd("flpr ring test 0", &rc);

	zassert_equal(rc, -EINVAL);
	assert_output_contains(out, "Block count must be 1");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_test_uninitialized)
{
	test_flpr_reset();
	struct flpr_ring_status s = {.initialized = false, .epoch = 0};

	test_flpr_set_ring_status(&s);
	int rc = 0;
	const char *out = run_cmd("flpr ring test 10", &rc);

	zassert_equal(rc, -EAGAIN);
	assert_output_contains(out, "Rings not initialized — init/reset first");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_test_delegated_success)
{
	test_flpr_reset();
	struct flpr_ring_status s = {
		.initialized = true,
		.epoch = 7,
	};
	struct flpr_acceptance_status as = {
		.test_blocks_sent = 1,
		.test_blocks_recv = 1,
		.test_crc_errors = 0,
		.test_payload_errors = 0,
		.test_stale_events = 0,
		.test_producer_blocks = 0,
		.test_output_full = 0,
	};

	test_flpr_set_ring_status(&s);
	test_flpr_set_acceptance_status(&as);
	test_flpr_set_ring_test_result(0);
	int rc = 0;
	const char *out = run_cmd("flpr ring test 1", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Starting ring test: 1 blocks");
	assert_output_contains(out, "PASS: all 1 blocks transferred, zero errors");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_test_delegated_failure)
{
	test_flpr_reset();
	struct flpr_ring_status s = {
		.initialized = true,
		.epoch = 7,
	};
	struct flpr_acceptance_status as = {
		.test_blocks_sent = 0,
		.test_blocks_recv = 0,
	};

	test_flpr_set_ring_status(&s);
	test_flpr_set_acceptance_status(&as);
	test_flpr_set_ring_test_result(-EIO);
	int rc = 0;
	const char *out = run_cmd("flpr ring test 1", &rc);

	zassert_equal(rc, 0); /* handler reports failure via FAIL: warn line */
	assert_output_contains(out, "FAIL: sent=0/1 recv=0");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_test_rate_delegated)
{
	test_flpr_reset();
	struct flpr_ring_status s = {
		.initialized = true,
		.epoch = 7,
	};
	struct flpr_acceptance_status as = {
		.test_blocks_sent = 1,
		.test_blocks_recv = 1,
	};

	test_flpr_set_ring_status(&s);
	test_flpr_set_acceptance_status(&as);
	test_flpr_set_ring_test_result(0);
	int rc = 0;
	const char *out = run_cmd("flpr ring test 1 10", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Starting rate-limited ring test: 1 blocks, 10 blk/s");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_reset_success)
{
	test_flpr_reset();
	test_flpr_set_reset_result(0);
	int rc = 0;
	const char *out = run_cmd("flpr ring reset", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Coordinated ring reset OK");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_reset_failure)
{
	test_flpr_reset();
	test_flpr_set_reset_result(-EIO);
	int rc = 0;
	const char *out = run_cmd("flpr ring reset", &rc);

	zassert_equal(rc, -EIO);
	assert_output_contains(out, "Coordinated ring reset failed: -5");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_init_success)
{
	test_flpr_reset();
	test_flpr_set_init_result(0);
	int rc = 0;
	const char *out = run_cmd("flpr ring init", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "PCM rings initialized.");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_init_failure)
{
	test_flpr_reset();
	test_flpr_set_init_result(-ENODEV);
	int rc = 0;
	const char *out = run_cmd("flpr ring init", &rc);

	zassert_equal(rc, -ENODEV);
	assert_output_contains(out, "Ring init failed: -19");
}

ZTEST(audio_shell_nrf54, test_flpr_producer_stall_on)
{
	test_flpr_reset();
	zassert_false(test_flpr_stall_producer_called(), "no stall before command");

	int rc = 0;
	const char *out = run_cmd("flpr ring stall", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Producer stall: ON");
	zassert_true(test_flpr_stall_producer_called(), "stall side effect");
	zassert_true(test_flpr_stall_producer_value(), "stall=true");
}

ZTEST(audio_shell_nrf54, test_flpr_producer_stall_off)
{
	test_flpr_reset();
	int rc = 0;
	const char *out = run_cmd("flpr ring stall off", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "Producer stall: OFF");
	zassert_true(test_flpr_stall_producer_called(), "stall side effect");
	zassert_false(test_flpr_stall_producer_value(), "stall=false");
}

ZTEST(audio_shell_nrf54, test_flpr_stall_flpr_success)
{
	test_flpr_reset();
	test_flpr_set_stall_result(0);
	int rc = 0;
	const char *out = run_cmd("flpr ring stall_flpr 3", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "FLPR stall applied: 0x03 (cons_in=1 prod_out=1)");
}

ZTEST(audio_shell_nrf54, test_flpr_stall_flpr_failure)
{
	test_flpr_reset();
	test_flpr_set_stall_result(-EIO);
	int rc = 0;
	const char *out = run_cmd("flpr ring stall_flpr 1", &rc);

	zassert_equal(rc, -EIO);
	assert_output_contains(out, "FLPR stall failed: -5");
}

ZTEST(audio_shell_nrf54, test_flpr_stall_flpr_ms_usage)
{
	test_flpr_reset();
	int rc = 0;
	const char *out = run_cmd("flpr ring stall_flpr_ms 1", &rc);

	zassert_equal(rc, -EINVAL);
	assert_output_contains(out, "Usage: flpr ring stall_flpr_ms <bits> <duration_ms>");
}

ZTEST(audio_shell_nrf54, test_flpr_stall_flpr_ms_zero_mask_rejected)
{
	test_flpr_reset();
	int rc = 0;
	const char *out = run_cmd("flpr ring stall_flpr_ms 0 100", &rc);

	zassert_equal(rc, -EINVAL);
	assert_output_contains(out, "Timed stall with zero mask rejected");
}

ZTEST(audio_shell_nrf54, test_flpr_stall_flpr_ms_duration_overflow)
{
	test_flpr_reset();
	int rc = 0;
	const char *out = run_cmd("flpr ring stall_flpr_ms 1 16777216", &rc);

	zassert_equal(rc, -EINVAL);
	assert_output_contains(out, "exceeds max 16777215 ms");
}

ZTEST(audio_shell_nrf54, test_flpr_stall_flpr_ms_success)
{
	test_flpr_reset();
	test_flpr_set_stall_timed_result(0);
	int rc = 0;
	const char *out = run_cmd("flpr ring stall_flpr_ms 1 500", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "FLPR timed stall applied: bits=0x01 duration=500 ms");
}

ZTEST(audio_shell_nrf54, test_flpr_stall_flpr_ms_failure)
{
	test_flpr_reset();
	test_flpr_set_stall_timed_result(-EIO);
	int rc = 0;
	const char *out = run_cmd("flpr ring stall_flpr_ms 1 500", &rc);

	zassert_equal(rc, -EIO);
	assert_output_contains(out, "FLPR timed stall failed: -5");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_acceptance_invalid_count)
{
	test_flpr_reset();
	int rc = 0;
	const char *out = run_cmd("flpr ring acceptance 0", &rc);

	zassert_equal(rc, -EINVAL);
	assert_output_contains(out, "Block count must be 1");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_acceptance_uninitialized)
{
	test_flpr_reset();
	struct flpr_ring_status s = {.initialized = false};

	test_flpr_set_ring_status(&s);
	int rc = 0;
	const char *out = run_cmd("flpr ring acceptance 100", &rc);

	zassert_equal(rc, -EAGAIN);
	assert_output_contains(out, "Rings not initialized");
}

ZTEST(audio_shell_nrf54, test_flpr_ring_acceptance_delegated_gate_output)
{
	test_flpr_reset();
	struct flpr_ring_status s = {.initialized = true, .epoch = 1};

	test_flpr_set_ring_status(&s);
	test_flpr_set_gates_result(0);
	int rc = 0;
	const char *out = run_cmd("flpr ring acceptance 100", &rc);

	/* The real shell gate sink (shell_gate_print) forwards the
	 * acceptance module's severity-aware lines byte-identically. */
	zassert_equal(rc, 0);
	assert_output_contains(out, "fake gate line");
}

ZTEST(audio_shell_nrf54, test_flpr_hang_not_ready)
{
	test_flpr_reset();
	struct flpr_status s = {.ready = true, .acked = false};

	test_flpr_set_status(&s);
	int rc = 0;
	const char *out = run_cmd("flpr hang", &rc);

	zassert_equal(rc, -EAGAIN);
	assert_output_contains(out, "FLPR not ready/acked — hang rejected");
}

ZTEST(audio_shell_nrf54, test_flpr_hang_ack_success)
{
	test_flpr_reset();
	struct flpr_status s = {.ready = true, .acked = true};

	test_flpr_set_status(&s);
	test_flpr_set_hang_result(0);
	int rc = 0;
	const char *out = run_cmd("flpr hang", &rc);

	zassert_equal(rc, 0);
	assert_output_contains(out, "FAULT_HANG_ACK received — FLPR hang imminent.");
}

ZTEST(audio_shell_nrf54, test_flpr_hang_failure_errno)
{
	test_flpr_reset();
	struct flpr_status s = {.ready = true, .acked = true};

	test_flpr_set_status(&s);
	test_flpr_set_hang_result(-EIO);
	int rc = 0;
	const char *out = run_cmd("flpr hang", &rc);

	zassert_equal(rc, -EIO);
	assert_output_contains(out, "FAULT_HANG failed: -5 (no ACK)");
}
