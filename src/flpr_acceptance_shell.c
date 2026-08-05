/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLPR acceptance-harness shell commands.  Compiled only when
 * CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS is enabled (nRF54L15 board conf);
 * normal audio diagnostics never compile this file.
 */

#include <string.h>

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

	shell_print(sh,
		    "  Diag (CPUAPP): notify=%u err=%u sem_give=%u sem_take=%u stale=%u drained=%u",
		    s.notify_sent, s.notify_err, s.sem_gives, s.sem_takes, s.stale_notify,
		    s.sem_drained);

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
			    "  Test (done):  sent=%u recv=%u crc_err=%u payload_err=%u "
			    "full=%u empty=%u stale=%u backpressure=%u",
			    s.test_blocks_sent, s.test_blocks_recv, s.test_crc_errors,
			    s.test_payload_errors, s.test_full_events, s.test_empty_events,
			    s.test_stale_events, s.test_backpressure);
	}

	if (s.latency_count > 0) {
		uint32_t avg_cycles = (uint32_t)(s.latency_sum / s.latency_count);
		uint32_t avg_us = k_cyc_to_us_ceil32(avg_cycles);
		uint32_t min_us = k_cyc_to_us_ceil32(s.latency_min);
		uint32_t max_us = k_cyc_to_us_ceil32(s.latency_max);
		shell_print(sh,
			    "  Latency: min=%u cyc (%u us) max=%u cyc (%u us) "
			    "avg=%u cyc (%u us) count=%u",
			    s.latency_min, min_us, s.latency_max, max_us, avg_cycles, avg_us,
			    s.latency_count);
	}

	/* Timed stall diagnostics (Stage 2). */
	{
		uint32_t acked = flpr_ring_mgr_flpr_stall_acked();
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
	struct flpr_ring_status s;
	int ret;
	if (rate > 0) {
		ret = flpr_ring_mgr_test_run_rate(count, timeout_ms, rate, &s);
	} else {
		ret = flpr_ring_mgr_test_run(count, timeout_ms, &s);
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
	flpr_ring_mgr_stall_producer(stall);
	shell_print(sh, "Producer stall: %s", stall ? "ON" : "OFF");
	return 0;
}

static int cmd_flpr_ring_stall_flpr(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t bits = 0;
	if (argc >= 2) {
		bits = (uint8_t)shell_strtoul(argv[1], 0, NULL);
	}
	int ret = flpr_ring_mgr_flpr_stall(bits, 5000);
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

	int ret = flpr_ring_mgr_flpr_stall_timed(bits, duration_ms, 5000);
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

/* Static scratch for acceptance test — too large for shell thread stack. */
static uint8_t acceptance_buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES];
static struct flpr_ring_status acceptance_rs;

/* Gate helpers use pointer to static struct to avoid stack copies. */
#define ACCEPT_STATUS() (&acceptance_rs)
#define ACCEPT_BUF()    (acceptance_buf)

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

	/* Ensure rings are reset and clean before acceptance. */
	{
		int r = flpr_ring_mgr_coordinated_reset(0, 5000);
		if (r != 0) {
			shell_error(sh, "Coordinated reset failed: %d", r);
			return r;
		}
		/* Clear any producer stall. */
		flpr_ring_mgr_stall_producer(false);
		/* Clear any FLPR stalls. */
		flpr_ring_mgr_flpr_stall(0, 5000);
	}

	bool any_fail = false;
	uint32_t gate1_elapsed = 0; /* stored for throughput report */

#define GATE_HEADER(n, desc) shell_print(sh, "--- Gate %d: %s ---", n, desc)

	/* ── Gate 1: normal loopback ────────────────────────────────────
	 * Timeout derived from measured throughput: ~460 blk/s → ~2.17 ms/blk.
	 * Budget: 3 ms per block + 60 s floor.  100 k → 360 s (≈1.66× actual). */
	GATE_HEADER(1, "normal loopback");
	{
		uint32_t start = k_uptime_get_32();
		/* timeout = count * 3 ms + 60 s floor */
		uint32_t timeout = count * 3 + 60000;
		if (timeout < 60000) {
			timeout = 60000;
		}

		struct flpr_ring_status *s = ACCEPT_STATUS();
		int r = flpr_ring_mgr_test_run(count, timeout, s);
		gate1_elapsed = k_uptime_get_32() - start;

		shell_print(sh, "Sent=%u Recv=%u CRC_Err=%u Pay_Err=%u Stale=%u BP=%u",
			    s->test_blocks_sent, s->test_blocks_recv, s->test_crc_errors,
			    s->test_payload_errors, s->test_stale_events, s->test_backpressure);
		if (s->latency_count > 0) {
			uint32_t avg = (uint32_t)(s->latency_sum / s->latency_count);
			uint32_t tput =
				(s->latency_count * 1000U) / (gate1_elapsed ? gate1_elapsed : 1);
			shell_print(
				sh,
				"Latency: min=%u us max=%u us avg=%u us N=%u  Throughput: %u blk/s",
				k_cyc_to_us_ceil32(s->latency_min),
				k_cyc_to_us_ceil32(s->latency_max), k_cyc_to_us_ceil32(avg),
				s->latency_count, tput);
			shell_print(sh, "FLPR: blk=%u crc=%u cons_ok=%u prod_ok=%u full=%u",
				    s->test_producer_blocks, s->flpr_consume_stale,
				    s->flpr_consume_ok, s->flpr_produce_ok, s->flpr_produce_full);
		}
		if (r == 0 && s->test_blocks_sent == count && s->test_payload_errors == 0 &&
		    s->test_crc_errors == 0 && s->test_stale_events == 0 &&
		    s->test_backpressure == 0) {
			shell_print(sh, "GATE 1 PASS: %u blocks in %u ms", count, gate1_elapsed);
		} else {
			shell_error(
				sh,
				"GATE 1 FAIL: sent=%u recv=%u crc=%u pay=%u stale=%u bp=%u rc=%d",
				s->test_blocks_sent, s->test_blocks_recv, s->test_crc_errors,
				s->test_payload_errors, s->test_stale_events, s->test_backpressure,
				r);
			any_fail = true;
		}
	}

	/* ── Gate 2: CPU producer stall → resume exact ───────────────── */
	GATE_HEADER(2, "CPU producer stall / resume");
	{
		/* Ensure clean state: reset rings + clear stalls. */
		flpr_ring_mgr_stall_producer(false);
		int rr = flpr_ring_mgr_coordinated_reset(0, 5000);
		if (rr != 0) {
			shell_error(sh, "GATE 2 reset fail: %d", rr);
			any_fail = true;
			goto gate2_done;
		}

		flpr_ring_mgr_stall_producer(true);
		uint32_t full_cnt = 0;
		for (uint32_t i = 0; i < 10; i++) {
			flpr_ring_gen_payload(ACCEPT_BUF(), sizeof(acceptance_buf), i);
			if (flpr_ring_mgr_produce_block(ACCEPT_BUF(), FLPR_RING_PAYLOAD_MAX_INPUT,
							i, 0, false) == FLPR_PRODUCE_FULL) {
				full_cnt++;
			}
		}
		flpr_ring_mgr_stall_producer(false);

		struct flpr_ring_status *s = ACCEPT_STATUS();
		flpr_ring_mgr_get_status(s);
		shell_print(sh, "Stall FULL count: %u (expect 10)  Backpressure: %u", full_cnt,
			    s->test_backpressure);
		if (s->test_blocks_sent > 0 || full_cnt != 10) {
			shell_error(sh, "GATE 2 FAIL: sent=%u full=%u", s->test_blocks_sent,
				    full_cnt);
			any_fail = true;
		} else {
			struct flpr_ring_status *s2 = ACCEPT_STATUS();
			int r2 = flpr_ring_mgr_test_run(100, 15000, s2);
			if (r2 == 0 && s2->test_blocks_recv == 100 &&
			    s2->test_payload_errors == 0) {
				shell_print(sh, "GATE 2 PASS");
			} else {
				shell_error(sh, "GATE 2 FAIL: resume rc=%d recv=%u", r2,
					    s2->test_blocks_recv);
				any_fail = true;
			}
		}
	}
gate2_done:

	/* ── Gate 3: FLPR input-consumer stall ─────────────────────────
	 * Fill exactly 4 slots, backpressure on 5th+, resume exact. */
	GATE_HEADER(3, "FLPR input-consumer stall");
	{
		/* Clean reset. */
		flpr_ring_mgr_stall_producer(false);
		flpr_ring_mgr_flpr_stall(0, 5000);
		int r = flpr_ring_mgr_coordinated_reset(0, 5000);
		if (r != 0) {
			shell_error(sh, "GATE 3 reset fail: %d", r);
			any_fail = true;
		} else {
			r = flpr_ring_mgr_flpr_stall(FLPR_STALL_CONSUMER_INPUT, 5000);
			if (r != 0) {
				shell_error(sh, "GATE 3 stall fail: %d", r);
				any_fail = true;
			} else {
				uint32_t ok = 0, full_after = 0;
				for (uint32_t i = 0; i < 10; i++) {
					flpr_ring_gen_payload(ACCEPT_BUF(), sizeof(acceptance_buf),
							      i);
					enum flpr_produce_result pr = flpr_ring_mgr_produce_block(
						ACCEPT_BUF(), FLPR_RING_PAYLOAD_MAX_INPUT, i, 0,
						false);
					if (i < 4 && pr == FLPR_PRODUCE_OK) {
						ok++;
					} else if (i >= 4 && pr == FLPR_PRODUCE_FULL) {
						full_after++;
					}
				}
				shell_print(sh,
					    "Filled: %u (expect 4)  Full-after: %u (expect >=4)",
					    ok, full_after);
				if (ok != 4 || full_after < 2) {
					shell_error(sh, "GATE 3 FAIL: ok=%u full_after=%u", ok,
						    full_after);
					any_fail = true;
				}
				/* Resume FLPR: clear stalls, drain output. */
				flpr_ring_mgr_flpr_stall(0, 5000);
				k_msleep(200);

				/* Wait for FLPR to forward the 4 queued blocks. */
				uint32_t recv = 0;
				uint32_t drain_start = k_uptime_get_32();
				while ((k_uptime_get_32() - drain_start) < 5000 && recv < 4) {
					uint16_t vf;
					if (flpr_ring_mgr_consume_block(NULL, &vf, NULL, NULL,
									NULL) == FLPR_CONSUME_OK) {
						recv++;
					} else {
						flpr_ring_mgr_wait_consume(10);
					}
				}
				shell_print(sh, "Resume: recv %u (expect 4)", recv);
				if (recv == 4) {
					shell_print(sh, "GATE 3 PASS");
				} else {
					shell_error(sh, "GATE 3 FAIL: recv=%u", recv);
					any_fail = true;
				}
			}
		}
	}

	/* ── Gate 4: FLPR output-producer stall ─────────────────────────
	 * Output fills on FLPR side, input never lost, resume exact. */
	GATE_HEADER(4, "FLPR output-producer stall");
	{
		/* Clean reset. */
		flpr_ring_mgr_stall_producer(false);
		flpr_ring_mgr_flpr_stall(0, 5000);
		int r = flpr_ring_mgr_coordinated_reset(0, 5000);
		if (r != 0) {
			shell_error(sh, "GATE 4 reset fail: %d", r);
			any_fail = true;
		} else {
			r = flpr_ring_mgr_flpr_stall(FLPR_STALL_PRODUCER_OUTPUT, 5000);
			if (r != 0) {
				shell_error(sh, "GATE 4 stall fail: %d", r);
				any_fail = true;
			} else {
				/* Send 10 blocks — FLPR processes input (forward to output) but
				 * output is stalled so FLPR backpressures on input ring.
				 * Because FLPR preserves input on output-stall (does not consume),
				 * the input ring fills at 4 slots and subsequent produces return
				 * FULL. Do NOT retry — just count the results. */
				uint32_t ok4 = 0, full4 = 0;
				for (uint32_t i = 0; i < 10; i++) {
					flpr_ring_gen_payload(ACCEPT_BUF(), sizeof(acceptance_buf),
							      i);
					enum flpr_produce_result pr = flpr_ring_mgr_produce_block(
						ACCEPT_BUF(), FLPR_RING_PAYLOAD_MAX_INPUT, i, 0,
						false);
					if (pr == FLPR_PRODUCE_OK) {
						ok4++;
						flpr_ring_mgr_notify_producer();
					} else {
						full4++;
					}
				}
				k_msleep(300);

				struct flpr_ring_status *s = ACCEPT_STATUS();
				flpr_ring_mgr_get_status(s);
				shell_print(sh,
					    "Produced: %u OK / %u FULL (expect 4/6)  FLPR "
					    "output-full: %u",
					    ok4, full4, s->flpr_produce_full);

				/* Resume and drain. */
				flpr_ring_mgr_flpr_stall(0, 5000);
				k_msleep(200);

				uint32_t recv = 0;
				uint32_t drain_start = k_uptime_get_32();
				while ((k_uptime_get_32() - drain_start) < 5000 && recv < 10) {
					uint16_t vf;
					if (flpr_ring_mgr_consume_block(NULL, &vf, NULL, NULL,
									NULL) == FLPR_CONSUME_OK) {
						recv++;
					} else {
						flpr_ring_mgr_wait_consume(10);
					}
				}
				shell_print(sh, "Resume: recv %u (expect 4)", recv);
				if (full4 >= 2 && recv >= 3) {
					shell_print(sh, "GATE 4 PASS");
				} else {
					shell_error(sh, "GATE 4 FAIL: ok=%u full=%u recv=%u", ok4,
						    full4, recv);
					any_fail = true;
				}
			}
		}
	}

	/* ── Gate 5: stale epoch injection, reject, recovery ─────────── */
	GATE_HEADER(5, "stale epoch injection");
	{
		/* Clean reset. */
		flpr_ring_mgr_stall_producer(false);
		flpr_ring_mgr_flpr_stall(0, 5000);
		int r = flpr_ring_mgr_coordinated_reset(0, 5000);
		if (r != 0) {
			shell_error(sh, "GATE 5 reset fail: %d", r);
			any_fail = true;
		} else {
			struct flpr_ring_status *s = ACCEPT_STATUS();
			flpr_ring_mgr_get_status(s);
			uint32_t stale_epoch = s->epoch + 31337;
			if (flpr_ring_mgr_produce_stale_test(stale_epoch) != 0) {
				shell_error(sh, "stale injection fail");
				any_fail = true;
			} else {
				flpr_ring_mgr_consume_block(NULL, NULL, NULL, NULL, NULL);
				k_msleep(50);
				struct flpr_ring_status *s5 = ACCEPT_STATUS();
				flpr_ring_mgr_get_status(s5);
				shell_print(sh, "Stale events: %u (expect 1)",
					    s5->test_stale_events);
				if (s5->test_stale_events == 1) {
					struct flpr_ring_status *s5r = ACCEPT_STATUS();
					int r5r = flpr_ring_mgr_test_run(100, 15000, s5r);
					if (r5r == 0 && s5r->test_blocks_recv == 100) {
						shell_print(sh, "GATE 5 PASS");
					} else {
						shell_error(sh,
							    "GATE 5 FAIL recovery: rc=%d recv=%u",
							    r5r, s5r->test_blocks_recv);
						any_fail = true;
					}
				} else {
					shell_error(sh, "GATE 5 FAIL: stale=%u",
						    s5->test_stale_events);
					any_fail = true;
				}
			}
		}
	}

	/* ── Gate 6: explicit empty ──────────────────────────────────── */
	GATE_HEADER(6, "explicit empty");
	{
		struct flpr_ring_status *s = ACCEPT_STATUS();
		flpr_ring_mgr_get_status(s);
		shell_print(sh, "In used=%u  Out used=%u", s->in_used, s->out_used);
		if (s->in_used == 0 && s->out_used == 0) {
			shell_print(sh, "GATE 6 PASS");
		} else {
			shell_error(sh, "GATE 6 FAIL");
			any_fail = true;
		}
	}

#undef GATE_HEADER

	if (any_fail) {
		shell_print(sh, "ACCEPTANCE FAILED");
		return -1;
	}
	shell_print(sh, "ACCEPTANCE PASSED — all gates clear");
	shell_print(sh, "Gate 1 throughput: %u blk/s",
		    gate1_elapsed > 0 ? (count * 1000U) / gate1_elapsed : 0U);
	return 0;
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

	int ret = flpr_handshake_send_fault_hang(500);
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
