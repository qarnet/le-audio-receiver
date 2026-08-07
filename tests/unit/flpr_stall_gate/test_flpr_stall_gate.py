"""
Unit tests for flpr_stall_gate.py — index-based fake serial streams.

v3: timed 60ms auto-clear stall (single command, single ACK, no hold loop).
"""

import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, "scripts")
from flpr_stall_gate import (
    GateRunner,
    FakeSerial,
    StallGateError,
    RE_STATE_LINE,
    RE_COUNTERS,
    RE_RECOVERY,
    RE_PROBATION,
    RE_STALL_TIMED_ACK,
    RE_RECOVERY_OK,
)

# ── Test data strings ────────────────────────────────────────────────────

OFFLOAD_ACTIVE_START = """\
--- Audio offload ---
  State       : ACTIVE / epoch=123 gen=4
  Counters    : submit=500 success=500 fallback=0 busy=0
  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
  Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0
  Probation   : active=0 success=0 cleared=0
  RTT         : min=736 cyc (736 us) max=883 cyc (883 us) avg=740 cyc (740 us) n=500
"""

OFFLOAD_PREPARING = OFFLOAD_ACTIVE_START.replace("ACTIVE", "PREPARING")

OFFLOAD_START_LOW_SUCCESS = """\
--- Audio offload ---
  State       : ACTIVE / epoch=123 gen=4
  Counters    : submit=200 success=200 fallback=0 busy=0
  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
  Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0
  Probation   : active=0 success=0 cleared=0
  RTT         : min=736 cyc (736 us) max=883 cyc (883 us) avg=740 cyc (740 us) n=200
"""

# v3 timed stall ACK format
TIMED_STALL_ACK = "FLPR timed stall applied: bits=0x01 duration=60 ms"

RECOVERY_OK_LINE = (
    "[00:22:37.451] audio_offload: offload recovery OK: "
    "epoch=1357451738 gen=5 tries=1 backoff=100 ms"
)

# Baseline captured in Step 4 (immediately after timed stall ACK).
# fallback already > 0 from blocks that faulted during the 60ms stall window.
OFFLOAD_BASELINE = """\
--- Audio offload ---
  State       : ACTIVE / epoch=1357451738 gen=6
  Counters    : submit=560 success=530 fallback=30 busy=0
  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
  Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0
  Probation   : active=1 success=0 cleared=0
  RTT         : min=736 cyc (736 us) max=883 cyc (883 us) avg=740 cyc (740 us) n=560
"""

# Evidence: recovery_attempts=2>=1, state ACTIVE, probation_cleared=1,
# exhaustion=0, fallback=30>0, success=700 >= baseline(530)+100=630
OFFLOAD_EVIDENCE = """\
--- Audio offload ---
  State       : ACTIVE / epoch=249136769 gen=83
  Counters    : submit=730 success=700 fallback=30 busy=0
  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
  Recovery    : attempts=2 fail=0 relapses=0 exhaustion=0
  Probation   : active=0 success=100 cleared=1
  RTT         : min=736 cyc (736 us) max=883 cyc (883 us) avg=740 cyc (740 us) n=730
"""

OFFLOAD_ACTIVE_EXHAUSTION = """\
--- Audio offload ---
  State       : FALLBACK / epoch=861464814 gen=12
  Counters    : submit=2000 success=500 fallback=1500 busy=0
  Faults      : timeout=6 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
  Recovery    : attempts=5 fail=1 relapses=5 exhaustion=1
  Probation   : active=1 success=0 cleared=0
  RTT         : min=733 cyc (733 us) max=888 cyc (888 us) avg=740 cyc (740 us) n=500
"""

# Baseline with fallback=0 — stall produced no fault evidence
OFFLOAD_BASELINE_NO_FALLBACK = """\
--- Audio offload ---
  State       : ACTIVE / epoch=1357451738 gen=6
  Counters    : submit=560 success=560 fallback=0 busy=0
  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
  Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0
  Probation   : active=0 success=0 cleared=0
  RTT         : min=736 cyc (736 us) max=883 cyc (883 us) avg=740 cyc (740 us) n=560
"""

# Real `audio status` output shape (src/audio_shell.c cmd_status).
AUDIO_STATUS_GOOD = """\
--- Audio status ---
  Frames decoded : 730
  PLC frames     : 42 (5%)
  Decode errors  : 0
  I2S underruns  : 0
  Stream resets  : 0
  Drift state    : locked
  Drift ppm      : 0
  Resampler      : ASRC linear
  Volume         : 255 / 255
"""

# Real `audio perf` output shape (src/audio_shell.c cmd_perf).
AUDIO_PERF_GOOD = """\
--- Performance ---
  Path          Count   Avg(cyc)  Avg(us)  Max(cyc)  Max(us)  %deadline
  iso_recv          730       120      120       240      240   0.0%
  Queue:
    Slab free     : 4 / 16 (min/max)
    Output frames : 0 / 4 (min/max)
    Output blocks : 730
    Push failures : 0
    Repeat fb     : 0
    ASRC cap fail : 0
"""


def _audio_status_fault(field, value):
    return AUDIO_STATUS_GOOD.replace("%s  : 0" % field, "%s  : %d" % (field, value))


AUDIO_STATUS_DECODE_ERR = _audio_status_fault("Decode errors", 3)
AUDIO_STATUS_I2S_UNDERRUN = _audio_status_fault("I2S underruns", 1)
AUDIO_STATUS_STREAM_RESET = _audio_status_fault("Stream resets", 1)
AUDIO_PERF_PUSH_FAIL = AUDIO_PERF_GOOD.replace("Push failures : 0", "Push failures : 2")
AUDIO_STATUS_MALFORMED = AUDIO_STATUS_GOOD.replace(
    "I2S underruns  : 0", "I2S underruns  : N/A"
)


def _mono_steady(base, step=0.02):
    """Generator for time.monotonic returning steady increments from base."""
    t = base
    while True:
        yield t
        t += step


class TestRegexParsing(unittest.TestCase):
    """Verify regexes match current shell output format."""

    def test_state_line(self):
        m = RE_STATE_LINE.search(OFFLOAD_ACTIVE_START)
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), "ACTIVE")

    def test_counters(self):
        m = RE_COUNTERS.search(OFFLOAD_ACTIVE_START)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(1)), 500)
        self.assertEqual(int(m.group(2)), 500)
        self.assertEqual(int(m.group(3)), 0)

    def test_counters_fallback(self):
        m = RE_COUNTERS.search(OFFLOAD_BASELINE)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(3)), 30)

    def test_recovery(self):
        m = RE_RECOVERY.search(OFFLOAD_ACTIVE_START)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(4)), 0)

    def test_probation(self):
        m = RE_PROBATION.search(OFFLOAD_ACTIVE_START)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(3)), 0)

    def test_stall_timed_ack(self):
        m = RE_STALL_TIMED_ACK.search(TIMED_STALL_ACK)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(1), 16), 0x01)
        self.assertEqual(int(m.group(2)), 60)

    def test_recovery_ok(self):
        self.assertTrue(RE_RECOVERY_OK.search(RECOVERY_OK_LINE))

    def test_parse_offload_exhaustion(self):
        st = GateRunner.parse_offload(OFFLOAD_ACTIVE_EXHAUSTION)
        self.assertEqual(st["state"], "FALLBACK")
        self.assertEqual(st["exhaustion"], 1)
        # R3 migration (obsolete gate child): max-exhaustion Recovery line
        # without Counters/Probation still parses attempts + exhaustion.
        st2 = GateRunner.parse_offload(
            "State       : FALLBACK / epoch=12345 gen=3\n"
            "Recovery    : attempts=10 fail=2 relapses=1 exhaustion=1\n"
            "Probation   : active=1 success=0 cleared=0\n"
        )
        self.assertEqual(st2["recovery_attempts"], 10)
        self.assertEqual(st2["exhaustion"], 1)

    def test_parse_offload_evidence(self):
        st = GateRunner.parse_offload(OFFLOAD_EVIDENCE)
        self.assertEqual(st["state"], "ACTIVE")
        self.assertEqual(st["probation_cleared"], 1)
        self.assertEqual(st["probation_active"], 0)
        self.assertEqual(st["success"], 700)
        self.assertEqual(st["fallback"], 30)
        self.assertEqual(st["recovery_attempts"], 2)

    def test_parse_offload_baseline(self):
        st = GateRunner.parse_offload(OFFLOAD_BASELINE)
        self.assertEqual(st["success"], 530)
        self.assertEqual(st["fallback"], 30)

    # ── R3 migration from the retired tests/unit/gate child ────────────
    # These cases come from the obsolete gate suite; equivalent assertions
    # were merged, unique inputs preserved.

    def test_parse_offload_active(self):
        st = GateRunner.parse_offload(
            "State       : ACTIVE / epoch=12345 gen=3\n"
            "Counters    : submit=2000 success=1500 fallback=5 busy=10\n"
            "Recovery    : attempts=3 fail=0 relapses=0 exhaustion=0\n"
            "Probation   : active=0 success=200 cleared=1\n"
        )
        self.assertEqual(st["state"], "ACTIVE")
        self.assertEqual(st["success"], 1500)
        self.assertEqual(st["fallback"], 5)
        self.assertEqual(st["recovery_attempts"], 3)
        self.assertEqual(st["exhaustion"], 0)
        self.assertEqual(st["probation_cleared"], 1)
        self.assertEqual(st["probation_active"], 0)

    def test_parse_offload_fallback(self):
        st = GateRunner.parse_offload(
            "State       : FALLBACK / epoch=12345 gen=3\n"
            "Counters    : submit=500 success=400 fallback=12 busy=3\n"
            "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
            "Probation   : active=1 success=50 cleared=0\n"
        )
        self.assertEqual(st["state"], "FALLBACK")
        self.assertEqual(st["fallback"], 12)
        self.assertEqual(st["probation_active"], 1)

    def test_parse_offload_faults(self):
        st = GateRunner.parse_offload(
            "Faults      : timeout=0 full=1 stale=0 seq=0 frame=0 crc=0 payload=0\n"
        )
        self.assertEqual(st["fault_full"], 1)
        self.assertEqual(st["fault_timeout"], 0)

    def test_parse_offload_zero_faults(self):
        st = GateRunner.parse_offload(
            "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
        )
        for key in (
            "fault_timeout",
            "fault_full",
            "fault_stale",
            "fault_seq",
            "fault_frame",
            "fault_crc",
            "fault_payload",
        ):
            self.assertEqual(st[key], 0)

    def test_timed_stall_ack_multiple(self):
        text = (
            "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
            "another line\n"
        )
        m = RE_STALL_TIMED_ACK.search(text)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(1), 16), 0x01)
        self.assertEqual(int(m.group(2)), 60)

    def test_timed_ack_wrong_duration_rejected(self):
        text = "FLPR timed stall applied: bits=0x01 duration=999 ms (cons_in=1 prod_out=0)\n"
        m = RE_STALL_TIMED_ACK.search(text)
        self.assertIsNotNone(m)
        mask_val = int(m.group(1), 16)
        dur_val = int(m.group(2))
        self.assertFalse(mask_val == 0x01 and dur_val == 60)

    def test_timeout_full_pass_gate(self):
        """timeout=1 + full=1 are valid stall evidence; NOT integrity faults."""
        st = GateRunner.parse_offload(
            "Faults      : timeout=1 full=1 stale=0 seq=0 frame=0 crc=0 payload=0\n"
        )
        self.assertEqual(st["fault_timeout"], 1)
        self.assertEqual(st["fault_full"], 1)
        integrity_faults = (
            st["fault_stale"]
            or st["fault_seq"]
            or st["fault_frame"]
            or st["fault_crc"]
            or st["fault_payload"]
        )
        self.assertFalse(integrity_faults)

    def test_integrity_faults_rejected(self):
        """Each integrity fault (stale, seq, frame, crc, payload) is detected."""
        cases = [
            (
                "stale=1",
                "Faults      : timeout=0 full=0 stale=1 seq=0 frame=0 crc=0 payload=0\n",
            ),
            (
                "seq=1",
                "Faults      : timeout=0 full=0 stale=0 seq=1 frame=0 crc=0 payload=0\n",
            ),
            (
                "frame=1",
                "Faults      : timeout=0 full=0 stale=0 seq=0 frame=1 crc=0 payload=0\n",
            ),
            (
                "crc=1",
                "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=1 payload=0\n",
            ),
            (
                "payload=1",
                "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=1\n",
            ),
        ]
        for label, text in cases:
            st = GateRunner.parse_offload(text)
            integrity = (
                st["fault_stale"]
                or st["fault_seq"]
                or st["fault_frame"]
                or st["fault_crc"]
                or st["fault_payload"]
            )
            self.assertTrue(integrity, "%s must be detected as integrity fault" % label)
        # Comprehensive: timeout+full are fine; seq/frame/crc/payload all 1
        # must still trip the integrity rejection.
        st = GateRunner.parse_offload(
            "Faults      : timeout=1 full=1 stale=0 seq=1 frame=1 crc=1 payload=1\n"
        )
        integrity = (
            st["fault_stale"]
            or st["fault_seq"]
            or st["fault_frame"]
            or st["fault_crc"]
            or st["fault_payload"]
        )
        self.assertTrue(integrity)


class TestFakeSerial(unittest.TestCase):
    """FakeSerial transport basics."""

    def test_index_based_delivery(self):
        fs = FakeSerial({0: "hello", 2: "world"})
        self.assertEqual(fs.read_all().decode().strip(), "hello")
        self.assertEqual(fs.read_all(), b"")
        self.assertEqual(fs.read_all().decode().strip(), "world")
        self.assertEqual(fs.read_all(), b"")

    def test_write_recording(self):
        fs = FakeSerial({})
        fs.write(b"flpr offload\n")
        fs.write(b"flpr ring stall_flpr_ms 1 60\n")
        self.assertIn("flpr offload", fs.written[0])
        self.assertIn("stall_flpr_ms 1 60", fs.written[1])


class TestGateSuccess(unittest.TestCase):
    """Full success: ACTIVE+success>=500 → timed stall → ACK
    → baseline → fallback evidence → probation cleared."""

    def _make_schedule(self):
        """Deterministic schedule for v3 timed stall.

        read_all() calls (0-indexed schedule keys):
          #0: Step 1 → OFFLOAD_ACTIVE_START (ACTIVE, success=500) → break
          #1: Step 3 iter 1 → TIMED_STALL_ACK → break
          #2: Step 4 → OFFLOAD_BASELINE (success=530, fallback=30)
          #3: Step 5 first read → (empty / discard)
          #4: Step 5 second read → OFFLOAD_BASELINE (fallback=30>0 → break)
          #5: Step 6 first read → (empty / discard)
          #6: Step 6 second read → OFFLOAD_EVIDENCE → GATE PASS
          #7: Step 7 drain → OFFLOAD_EVIDENCE (final offload status)
          #8: Step 7 drain → AUDIO_STATUS_GOOD
          #9: Step 7 drain → AUDIO_PERF_GOOD
        """
        return {
            0: OFFLOAD_ACTIVE_START,
            1: TIMED_STALL_ACK,
            2: OFFLOAD_BASELINE,
            4: OFFLOAD_BASELINE,
            6: OFFLOAD_EVIDENCE,
            7: OFFLOAD_EVIDENCE,
            8: AUDIO_STATUS_GOOD,
            9: AUDIO_PERF_GOOD,
        }

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_success_path(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        fake = FakeSerial(self._make_schedule())
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        result = runner.run()

        self.assertTrue(result.passed, f"Gate should pass: {result.error}")
        self.assertEqual(result.error, "")
        self.assertGreater(result.ack_time, 0, "timed stall ACK timestamp captured")
        self.assertGreater(result.first_fallback_time, 0, "fallback timestamp captured")
        self.assertIn("flpr ring stall_flpr_ms 1 60", fake.written)
        self.assertEqual(result.final_status.get("state"), "ACTIVE")
        self.assertEqual(result.final_status.get("probation_cleared"), 1)
        self.assertEqual(result.final_status.get("fallback"), 30)
        self.assertEqual(result.baseline_success, 530)
        self.assertGreaterEqual(
            result.final_status.get("success", -1), result.baseline_success + 100
        )


class TestGateMissingStallAck(unittest.TestCase):
    """Gate fails when timed stall ACK never arrives."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_missing_stall_ack(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        t = [1000.0 + i * 0.5 for i in range(100)]
        mock_mono.side_effect = t

        fake = FakeSerial({0: OFFLOAD_ACTIVE_START})
        runner = GateRunner(fake, total_timeout=5.0, status_interval=0.01)
        result = runner.run()

        self.assertFalse(result.passed)
        self.assertIn("timed stall ACK", result.error)


class TestGateExhaustion(unittest.TestCase):
    """Gate fails when max_exhaustion_count > 0 in Step 6 evidence poll."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_exhaustion_detected(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        fake = FakeSerial(
            {
                0: OFFLOAD_ACTIVE_START,
                1: TIMED_STALL_ACK,
                2: OFFLOAD_BASELINE,  # Step 4 baseline
                4: OFFLOAD_BASELINE,  # Step 5 fallback evidence
                6: OFFLOAD_ACTIVE_EXHAUSTION,  # Step 6: exhaustion=1 → fail
            }
        )
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        result = runner.run()

        self.assertFalse(result.passed)
        self.assertIn("exhaustion", result.error.lower())


class TestGateNoFallback(unittest.TestCase):
    """Gate fails when stall produces no fallback evidence (stall ineffective)."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_no_fallback_times_out(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        t = [1000.0 + i * 0.10 for i in range(200)]  # fast timeout
        mock_mono.side_effect = t

        fake = FakeSerial(
            {
                0: OFFLOAD_ACTIVE_START,
                1: TIMED_STALL_ACK,
                2: OFFLOAD_BASELINE_NO_FALLBACK,  # fallback=0
                # Polls keep returning no-fallback → timeout
            }
        )
        runner = GateRunner(fake, total_timeout=3.0, status_interval=0.01)
        result = runner.run()

        self.assertFalse(result.passed)
        self.assertIn("Timeout", result.error)


class TestGateLowStartSuccess(unittest.TestCase):
    """Gate fails when initial success < 500 (not enough streaming blocks)."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_low_success_at_start(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        t = [1000.0 + i * 0.5 for i in range(50)]
        mock_mono.side_effect = t

        fake = FakeSerial({0: OFFLOAD_START_LOW_SUCCESS})  # success=200
        runner = GateRunner(fake, total_timeout=3.0, status_interval=0.01)
        result = runner.run()

        self.assertFalse(result.passed)
        self.assertIn("success", result.error.lower())


class TestGateNotActiveAtStart(unittest.TestCase):
    """Gate retries until ACTIVE+success>=500 appears."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_waits_for_active(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        fake = FakeSerial(
            {
                0: OFFLOAD_PREPARING,
                1: OFFLOAD_PREPARING,
                2: OFFLOAD_ACTIVE_START,  # third poll: ACTIVE + success=500
                3: TIMED_STALL_ACK,  # Step 3 finds ACK on first try
                4: OFFLOAD_BASELINE,  # Step 4 baseline
                6: OFFLOAD_BASELINE,  # Step 5 fallback evidence
                8: OFFLOAD_EVIDENCE,  # Step 6 evidence → pass
                9: OFFLOAD_EVIDENCE,  # Step 7 final offload
                10: AUDIO_STATUS_GOOD,  # Step 7 audio status
                11: AUDIO_PERF_GOOD,  # Step 7 audio perf
            }
        )
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        result = runner.run()

        self.assertTrue(
            result.passed, f"Should pass after ACTIVE appears: {result.error}"
        )


class TestGateBaselineCapturedImmediately(unittest.TestCase):
    """Baseline success captured right after timed stall ACK (Step 4),
    not after probation."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_baseline_before_proof(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        # Schedule OFFLOAD_BASELINE (fallback=30) at Step 4 baseline read,
        # and OFFLOAD_EVIDENCE (success=700) at Step 6 evidence read.
        fake = FakeSerial(
            {
                0: OFFLOAD_ACTIVE_START,
                1: TIMED_STALL_ACK,
                2: OFFLOAD_BASELINE,  # baseline=530, fallback=30
                4: OFFLOAD_BASELINE,  # Step 5 fallback evidence
                6: OFFLOAD_EVIDENCE,  # success=700 >= 530+100
                7: OFFLOAD_EVIDENCE,  # Step 7 final offload
                8: AUDIO_STATUS_GOOD,  # Step 7 audio status
                9: AUDIO_PERF_GOOD,  # Step 7 audio perf
            }
        )
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        result = runner.run()

        self.assertTrue(result.passed)
        self.assertEqual(
            result.baseline_success, 530, "Baseline should be 530 from OFFLOAD_BASELINE"
        )
        self.assertGreaterEqual(
            result.final_status.get("success", -1), result.baseline_success + 100
        )


class TestGateMigratedRunnerBehavior(unittest.TestCase):
    """R3: runner-level cases migrated from the retired tests/unit/gate
    child (unique transports preserved; equivalent cases merged)."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_gate_timeout_on_stale_state(self, mock_mono, mock_sleep):
        """Gate times out when state never becomes ACTIVE with success>=500."""
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        schedule = {
            0: "State       : PREPARING / epoch=1 gen=1\nCounters    : submit=0 success=0 fallback=0 busy=0\n",
            1: "State       : PREPARING / epoch=1 gen=1\nCounters    : submit=0 success=0 fallback=0 busy=0\n",
        }
        runner = GateRunner(
            FakeSerial(schedule), total_timeout=0.5, status_interval=0.1
        )
        result = runner.run()
        self.assertFalse(result.passed)
        self.assertTrue("ACTIVE" in result.error or "Timeout" in result.error)

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_gate_success_with_timeout_fault(self, mock_mono, mock_sleep):
        """Full gate run passes even with timeout=1 (expected stall evidence)."""
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        class TimeoutFaultTransport(FakeSerial):
            def __init__(self):
                super().__init__({})
                self._idx = 0

            def read_all(self) -> bytes:
                self._idx += 1
                if self._idx <= 2:
                    return (
                        "State       : ACTIVE / epoch=1 gen=1\n"
                        "Counters    : submit=1000 success=600 fallback=0 busy=0\n"
                        "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                        "Probation   : active=0 success=0 cleared=0\n"
                        "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                    ).encode("utf-8")
                elif self._idx == 3:
                    return (
                        "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
                    ).encode("utf-8")
                elif self._idx == 4:
                    return (
                        "State       : ACTIVE / epoch=1 gen=1\n"
                        "Counters    : submit=1000 success=600 fallback=5 busy=0\n"
                        "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                        "Probation   : active=0 success=0 cleared=0\n"
                    ).encode("utf-8")
                elif self._idx == 9:
                    # Step 7 final drain: audio status block.
                    return AUDIO_STATUS_GOOD.encode("utf-8")
                elif self._idx == 10:
                    # Step 7 final drain: audio perf block.
                    return AUDIO_PERF_GOOD.encode("utf-8")
                else:
                    return (
                        "State       : ACTIVE / epoch=1 gen=1\n"
                        "Counters    : submit=1200 success=750 fallback=5 busy=0\n"
                        "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                        "Probation   : active=0 success=100 cleared=1\n"
                        "Faults      : timeout=1 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                    ).encode("utf-8")

        runner = GateRunner(
            TimeoutFaultTransport(), total_timeout=5.0, status_interval=0.05
        )
        result = runner.run()
        self.assertTrue(result.passed, f"Gate must PASS with timeout=1: {result.error}")

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_seq_fault_fails_gate(self, mock_mono, mock_sleep):
        """Gate must fail when seq=1 (integrity fault)."""
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        class SeqFaultTransport(FakeSerial):
            def __init__(self):
                super().__init__({})
                self._idx = 0

            def read_all(self) -> bytes:
                self._idx += 1
                if self._idx <= 2:
                    return (
                        "State       : ACTIVE / epoch=1 gen=1\n"
                        "Counters    : submit=1000 success=600 fallback=0 busy=0\n"
                        "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                        "Probation   : active=0 success=0 cleared=0\n"
                        "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                    ).encode("utf-8")
                elif self._idx == 3:
                    return (
                        "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
                    ).encode("utf-8")
                elif self._idx == 4:
                    return (
                        "State       : ACTIVE / epoch=1 gen=1\n"
                        "Counters    : submit=1000 success=600 fallback=5 busy=0\n"
                        "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                        "Probation   : active=0 success=0 cleared=0\n"
                    ).encode("utf-8")
                else:
                    return (
                        "State       : ACTIVE / epoch=1 gen=1\n"
                        "Counters    : submit=1200 success=750 fallback=5 busy=0\n"
                        "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                        "Probation   : active=0 success=100 cleared=1\n"
                        "Faults      : timeout=0 full=0 stale=0 seq=1 frame=0 crc=0 payload=0\n"
                    ).encode("utf-8")

        runner = GateRunner(
            SeqFaultTransport(), total_timeout=5.0, status_interval=0.05
        )
        result = runner.run()
        self.assertFalse(result.passed, "Gate must FAIL with seq=1 integrity fault")


class TestGateFinalAudioFaults(unittest.TestCase):
    """Step 7 final `audio status`/`audio perf` fault fields must each be
    present and exactly zero; the final status commands are parsed, not
    merely sent."""

    def _run_with_audio(
        self, audio_status=AUDIO_STATUS_GOOD, audio_perf=AUDIO_PERF_GOOD
    ):
        schedule = {
            0: OFFLOAD_ACTIVE_START,
            1: TIMED_STALL_ACK,
            2: OFFLOAD_BASELINE,
            4: OFFLOAD_BASELINE,
            6: OFFLOAD_EVIDENCE,
            7: OFFLOAD_EVIDENCE,  # Step 7 final offload
        }
        if audio_status is not None:
            schedule[8] = audio_status
        if audio_perf is not None:
            schedule[9] = audio_perf
        fake = FakeSerial(schedule)
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        with (
            patch("flpr_stall_gate.time.sleep") as mock_sleep,
            patch("flpr_stall_gate.time.monotonic") as mock_mono,
        ):
            mock_sleep.return_value = None
            gen = _mono_steady(1000.0, 0.02)
            mock_mono.side_effect = lambda: next(gen)
            return runner.run()

    def test_clean_audio_faults_pass(self):
        result = self._run_with_audio()
        self.assertTrue(result.passed, result.error)
        self.assertEqual(result.audio_status["decode_errors"], 0)
        self.assertEqual(result.audio_status["push_failures"], 0)
        # Final offload status parsed from the final responses, not stale.
        self.assertEqual(result.final_status.get("state"), "ACTIVE")
        self.assertEqual(result.final_status.get("success"), 700)

    def test_decode_errors_nonzero_fails(self):
        result = self._run_with_audio(audio_status=AUDIO_STATUS_DECODE_ERR)
        self.assertFalse(result.passed)
        self.assertIn("decode_errors=3", result.error)

    def test_i2s_underruns_nonzero_fails(self):
        result = self._run_with_audio(audio_status=AUDIO_STATUS_I2S_UNDERRUN)
        self.assertFalse(result.passed)
        self.assertIn("i2s_underruns=1", result.error)

    def test_stream_resets_nonzero_fails(self):
        result = self._run_with_audio(audio_status=AUDIO_STATUS_STREAM_RESET)
        self.assertFalse(result.passed)
        self.assertIn("stream_resets=1", result.error)

    def test_push_failures_nonzero_fails(self):
        result = self._run_with_audio(audio_perf=AUDIO_PERF_PUSH_FAIL)
        self.assertFalse(result.passed)
        self.assertIn("push_failures=2", result.error)

    def test_malformed_field_is_missing_evidence(self):
        result = self._run_with_audio(audio_status=AUDIO_STATUS_MALFORMED)
        self.assertFalse(result.passed)
        self.assertIn("i2s_underruns missing", result.error)

    def test_missing_audio_status_block_fails(self):
        result = self._run_with_audio(audio_status=None)
        self.assertFalse(result.passed)
        self.assertIn("'audio status' block missing", result.error)

    def test_partial_output_missing_perf_fails(self):
        # audio status present but audio perf absent → push failures field
        # is missing evidence, never zero.
        result = self._run_with_audio(audio_perf=None)
        self.assertFalse(result.passed)
        self.assertIn("push_failures missing", result.error)

    def test_stale_faulty_audio_from_mid_stream_ignored(self):
        # A faulty audio block that lands in the Step 5 region is discarded
        # by the buffer clear before the final command batch; the fresh
        # Step 7 blocks decide, so the gate passes.
        schedule = {
            0: OFFLOAD_ACTIVE_START,
            1: TIMED_STALL_ACK,
            2: OFFLOAD_BASELINE,
            4: OFFLOAD_BASELINE,
            5: AUDIO_STATUS_DECODE_ERR,  # stale mid-stream block
            6: OFFLOAD_EVIDENCE,
            7: OFFLOAD_EVIDENCE,
            8: AUDIO_STATUS_GOOD,
            9: AUDIO_PERF_GOOD,
        }
        fake = FakeSerial(schedule)
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        with (
            patch("flpr_stall_gate.time.sleep") as mock_sleep,
            patch("flpr_stall_gate.time.monotonic") as mock_mono,
        ):
            mock_sleep.return_value = None
            gen = _mono_steady(1000.0, 0.02)
            mock_mono.side_effect = lambda: next(gen)
            result = runner.run()
        self.assertTrue(result.passed, result.error)


if __name__ == "__main__":
    unittest.main()
