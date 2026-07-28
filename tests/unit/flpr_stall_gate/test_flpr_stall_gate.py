"""
Unit tests for flpr_stall_gate.py — index-based fake serial streams.

v2: deterministic 80ms hold (zero reads during hold — no index shift),
    no recovery-text gate, baseline captured immediately after clear ACK.
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
    RE_STALL_ACK,
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

STALL_ACK_CONS1 = "FLPR stall applied: 0x01 (cons_in=1 prod_out=0)"
STALL_ACK_CONS0 = "FLPR stall applied: 0x00 (cons_in=0 prod_out=0)"

RECOVERY_OK_LINE = (
    "[00:22:37.451] audio_offload: offload recovery OK: "
    "epoch=1357451738 gen=5 tries=1 backoff=100 ms"
)

# Baseline captured immediately after clear ACK — fallback already > 0
# (blocks that faulted during the 80ms stall window)
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

    def test_stall_ack_cons1(self):
        m = RE_STALL_ACK.search(STALL_ACK_CONS1)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(2)), 1)

    def test_stall_ack_cons0(self):
        m = RE_STALL_ACK.search(STALL_ACK_CONS0)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(2)), 0)

    def test_recovery_ok(self):
        self.assertTrue(RE_RECOVERY_OK.search(RECOVERY_OK_LINE))

    def test_parse_offload_exhaustion(self):
        st = GateRunner.parse_offload(OFFLOAD_ACTIVE_EXHAUSTION)
        self.assertEqual(st["state"], "FALLBACK")
        self.assertEqual(st["exhaustion"], 1)

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
        fs.write(b"flpr ring stall_flpr 1\n")
        self.assertIn("flpr offload", fs.written[0])
        self.assertIn("stall_flpr 1", fs.written[1])


class TestGateSuccess(unittest.TestCase):
    """Full success: ACTIVE+success≥500 → stall → 80ms hold → clear
    → baseline → evidence (probation cleared, fallback>0, +100 success)."""

    def _make_schedule(self):
        """Deterministic schedule for v2 (zero reads during hold).

        read_all() calls:
          #0: Step 1 → OFFLOAD_ACTIVE_START (ACTIVE, success=500) → break
          #1: Step 3 iter 1 → RECOVERY_OK_LINE (no stall ACK)
          #2: Step 3 iter 2 → STALL_ACK_CONS1 (stall ACK found! break)
         Hold loop: zero reads.
          #3: Step 6 iter 1 → STALL_ACK_CONS0 (clear ACK found! break)
          #4: Step 7 → OFFLOAD_BASELINE (success=530, fallback=30)
          #5: Step 8 drain → empty
          #6: Step 8 status poll → OFFLOAD_EVIDENCE → GATE PASS
        """
        return {
            0: OFFLOAD_ACTIVE_START,
            1: RECOVERY_OK_LINE,
            2: STALL_ACK_CONS1,
            3: STALL_ACK_CONS0,
            4: OFFLOAD_BASELINE,
            6: OFFLOAD_EVIDENCE,
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
        self.assertGreaterEqual(
            result.stall_to_clear_ms,
            80,
            f"stall→clear={result.stall_to_clear_ms}ms < 80ms",
        )
        self.assertIn("flpr ring stall_flpr 1", fake.written)
        self.assertIn("flpr ring stall_flpr 0", fake.written)
        self.assertEqual(result.final_status.get("state"), "ACTIVE")
        self.assertEqual(result.final_status.get("probation_cleared"), 1)
        self.assertEqual(result.final_status.get("fallback"), 30)
        self.assertEqual(result.baseline_success, 530)
        self.assertGreaterEqual(
            result.final_status.get("success", -1), result.baseline_success + 100
        )

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_first_recovery_timestamp_captured(self, mock_mono, mock_sleep):
        """Recovery OK text from accumulated buffer is captured after hold."""
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        fake = FakeSerial(self._make_schedule())
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        result = runner.run()

        self.assertTrue(result.passed)
        self.assertGreater(
            result.first_recovery_time,
            0,
            "Should capture recovery timestamp from accumulated buffer",
        )
        self.assertGreater(result.stall_to_clear_ms, 0)

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_clear_not_blocked_by_missing_recovery(self, mock_mono, mock_sleep):
        """When recovery OK text never appears, clear still fires after
        80ms hold — gate proceeds with evidence check."""
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        # Schedule without RECOVERY_OK_LINE — stall ACK still at index 2
        fake = FakeSerial(
            {
                0: OFFLOAD_ACTIVE_START,
                2: STALL_ACK_CONS1,  # Step 3 finds stall ACK on second read
                3: STALL_ACK_CONS0,
                4: OFFLOAD_BASELINE,
                6: OFFLOAD_EVIDENCE,
            }
        )
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        result = runner.run()

        self.assertTrue(
            result.passed, f"Should pass even without recovery text: {result.error}"
        )
        self.assertEqual(
            result.first_recovery_time, 0.0, "No recovery timestamp when text missing"
        )
        self.assertGreaterEqual(result.stall_to_clear_ms, 80)


class TestGateMissingStallAck(unittest.TestCase):
    """Gate fails when stall ACK (cons_in=1) never arrives."""

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
        self.assertIn("stall ACK", result.error)


class TestGateExhaustion(unittest.TestCase):
    """Gate fails when max_exhaustion_count > 0."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_exhaustion_detected(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        fake = FakeSerial(
            {
                0: OFFLOAD_ACTIVE_START,
                1: RECOVERY_OK_LINE,
                2: STALL_ACK_CONS1,
                3: STALL_ACK_CONS0,
                4: OFFLOAD_BASELINE,
                6: OFFLOAD_ACTIVE_EXHAUSTION,  # exhaustion=1 → fail
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
                1: RECOVERY_OK_LINE,
                2: STALL_ACK_CONS1,
                3: STALL_ACK_CONS0,
                4: OFFLOAD_BASELINE_NO_FALLBACK,  # fallback=0
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
    """Gate retries until ACTIVE+success≥500 appears."""

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
                3: RECOVERY_OK_LINE,
                4: STALL_ACK_CONS1,
                5: STALL_ACK_CONS0,
                6: OFFLOAD_BASELINE,
                8: OFFLOAD_EVIDENCE,
            }
        )
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        result = runner.run()

        self.assertTrue(
            result.passed, f"Should pass after ACTIVE appears: {result.error}"
        )


class TestGateBaselineCapturedImmediately(unittest.TestCase):
    """Baseline success captured right after clear ACK, not after probation."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_baseline_before_proof(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        fake = FakeSerial(
            {
                0: OFFLOAD_ACTIVE_START,
                1: RECOVERY_OK_LINE,
                2: STALL_ACK_CONS1,
                3: STALL_ACK_CONS0,
                4: OFFLOAD_BASELINE,  # baseline=530, fallback=30
                6: OFFLOAD_EVIDENCE,  # success=700 >= 530+100
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


class TestGateClearAckCorrectIndex(unittest.TestCase):
    """Clear ACK searched via finditer — finds cons_in=0 even when
    cons_in=1 ACK is also in accumulated text."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_clear_ack_found_after_stall_ack(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        fake = FakeSerial(
            {
                0: OFFLOAD_ACTIVE_START,
                1: RECOVERY_OK_LINE,
                2: STALL_ACK_CONS1,
                3: STALL_ACK_CONS0,  # both ACKs present in accumulated text
                4: OFFLOAD_BASELINE,
                6: OFFLOAD_EVIDENCE,
            }
        )
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        result = runner.run()

        self.assertTrue(
            result.passed, f"Should find clear ACK via finditer: {result.error}"
        )


if __name__ == "__main__":
    unittest.main()
