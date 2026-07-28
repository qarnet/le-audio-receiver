"""
Unit tests for flpr_stall_gate.py — index-based fake serial streams.
"""

import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, "scripts")
from flpr_stall_gate import (
    GateRunner,
    FakeSerial,
    RE_STATE_LINE,
    RE_COUNTERS,
    RE_RECOVERY,
    RE_PROBATION,
    RE_STALL_ACK,
    RE_RECOVERY_OK,
    RE_PROBATION_CLEARED,
)

# ── Test data strings ────────────────────────────────────────────────────

OFFLOAD_ACTIVE_ZERO = """\
--- Audio offload ---
  State       : ACTIVE / epoch=123 gen=4
  Counters    : submit=500 success=500 fallback=0 busy=0
  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
  Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0
  Probation   : active=0 success=0 cleared=0
  RTT         : min=736 cyc (736 us) max=883 cyc (883 us) avg=740 cyc (740 us) n=500
"""

STALL_ACK_CONS1 = "FLPR stall applied: 0x01 (cons_in=1 prod_out=0)"
STALL_ACK_CONS0 = "FLPR stall applied: 0x00 (cons_in=0 prod_out=0)"

RECOVERY_OK_LINE = (
    "[00:22:37.451] audio_offload: offload recovery OK: "
    "epoch=1357451738 gen=5 tries=1 backoff=100 ms"
)

PROBATION_CLEARED_LINE = (
    "audio_offload: offload probation cleared after 100 consecutive successes"
)

OFFLOAD_ACTIVE_PROBATION = """\
--- Audio offload ---
  State       : ACTIVE / epoch=1357451738 gen=6
  Counters    : submit=700 success=700 fallback=0 busy=0
  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
  Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0
  Probation   : active=1 success=50 cleared=0
  RTT         : min=736 cyc (736 us) max=883 cyc (883 us) avg=740 cyc (740 us) n=700
"""

OFFLOAD_ACTIVE_PROBATION_CLEARED = """\
--- Audio offload ---
  State       : ACTIVE / epoch=249136769 gen=83
  Counters    : submit=1050 success=1050 fallback=0 busy=0
  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
  Recovery    : attempts=2 fail=0 relapses=0 exhaustion=0
  Probation   : active=0 success=100 cleared=1
  RTT         : min=736 cyc (736 us) max=883 cyc (883 us) avg=740 cyc (740 us) n=1050
"""

OFFLOAD_ACTIVE_PROBATION_FIRST = """\
--- Audio offload ---
  State       : ACTIVE / epoch=249136769 gen=83
  Counters    : submit=900 success=900 fallback=0 busy=0
  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
  Recovery    : attempts=2 fail=0 relapses=0 exhaustion=0
  Probation   : active=0 success=100 cleared=1
  RTT         : min=736 cyc (736 us) max=883 cyc (883 us) avg=740 cyc (740 us) n=900
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

OFFLOAD_PREPARING = OFFLOAD_ACTIVE_ZERO.replace("ACTIVE", "PREPARING")


def _mono_steady(base, step=0.01):
    """Generator for time.monotonic returning steady increments from base."""
    t = base
    while True:
        yield t
        t += step


class TestRegexParsing(unittest.TestCase):
    """Verify regexes match current shell output format."""

    def test_state_line(self):
        m = RE_STATE_LINE.search(OFFLOAD_ACTIVE_ZERO)
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), "ACTIVE")

    def test_counters(self):
        m = RE_COUNTERS.search(OFFLOAD_ACTIVE_ZERO)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(1)), 500)
        self.assertEqual(int(m.group(2)), 500)

    def test_recovery(self):
        m = RE_RECOVERY.search(OFFLOAD_ACTIVE_ZERO)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(4)), 0)

    def test_probation(self):
        m = RE_PROBATION.search(OFFLOAD_ACTIVE_ZERO)
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

    def test_probation_cleared(self):
        m = RE_PROBATION_CLEARED.search(PROBATION_CLEARED_LINE)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(1)), 100)

    def test_parse_offload_exhaustion(self):
        st = GateRunner.parse_offload(OFFLOAD_ACTIVE_EXHAUSTION)
        self.assertEqual(st["state"], "FALLBACK")
        self.assertEqual(st["exhaustion"], 1)

    def test_parse_offload_probation_cleared(self):
        st = GateRunner.parse_offload(OFFLOAD_ACTIVE_PROBATION_CLEARED)
        self.assertEqual(st["state"], "ACTIVE")
        self.assertEqual(st["probation_cleared"], 1)
        self.assertEqual(st["probation_active"], 0)
        self.assertEqual(st["success"], 1050)


class TestFakeSerial(unittest.TestCase):
    """FakeSerial transport basics."""

    def test_index_based_delivery(self):
        fs = FakeSerial({0: "hello", 2: "world"})
        self.assertEqual(fs.read_all().decode().strip(), "hello")  # call 0
        self.assertEqual(fs.read_all(), b"")  # call 1: empty
        self.assertEqual(fs.read_all().decode().strip(), "world")  # call 2
        self.assertEqual(fs.read_all(), b"")  # call 3: empty

    def test_write_recording(self):
        fs = FakeSerial({})
        fs.write(b"flpr offload\n")
        fs.write(b"flpr ring stall_flpr 1\n")
        self.assertIn("flpr offload", fs.written[0])
        self.assertIn("stall_flpr 1", fs.written[1])


class TestGateSuccess(unittest.TestCase):
    """Full success: stall → recovery → clear → probation_cleared."""

    def _make_schedule(self):
        """Return index-based schedule for the success scenario.

        With mocked sleep (no-op) and monotonic time, read_all() calls:
          0: Step 1 poll → OFFLOAD_ACTIVE_ZERO (ACTIVE)
          1: Step 3 stall ACK → STALL_ACK_CONS1
          2: Step 4 recovery → RECOVERY_OK_LINE
          3: Step 6 clear ACK → STALL_ACK_CONS0
          4: Step 7 drain (empty)
          5: Step 7 poll → first probation-cleared, success=900 → sets pre_clear
          6: Step 7 drain (empty)
          7: Step 7 poll → probation-cleared, success=1050 → Δ≥100 → PASS
        """
        return {
            0: OFFLOAD_ACTIVE_ZERO,
            1: STALL_ACK_CONS1,
            2: RECOVERY_OK_LINE,
            3: STALL_ACK_CONS0,
            5: OFFLOAD_ACTIVE_PROBATION_FIRST,
            7: OFFLOAD_ACTIVE_PROBATION_CLEARED,
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
        self.assertGreater(result.stall_to_clear_ms, 0)
        self.assertLess(result.recovery_to_clear_ms, 1200)
        self.assertIn("flpr ring stall_flpr 1", fake.written)
        self.assertIn("flpr ring stall_flpr 0", fake.written)
        self.assertEqual(result.final_status.get("state"), "ACTIVE")
        self.assertEqual(result.final_status.get("probation_cleared"), 1)


class TestGateMissingStallAck(unittest.TestCase):
    """Gate fails when stall ACK (cons_in=1) never arrives."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_missing_stall_ack(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        # Advance time quickly to hit timeout after step 1
        t = [1000.0 + i * 0.5 for i in range(100)]
        mock_mono.side_effect = t

        # Only step 1 data, no stall ACK
        fake = FakeSerial({0: OFFLOAD_ACTIVE_ZERO})
        runner = GateRunner(fake, total_timeout=5.0, status_interval=0.01)
        result = runner.run()

        self.assertFalse(result.passed)
        self.assertIn("stall ACK", result.error)


class TestGateMissingRecovery(unittest.TestCase):
    """Gate fails when recovery OK line never appears."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_missing_recovery(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        t = [1000.0 + i * 0.5 for i in range(100)]
        mock_mono.side_effect = t

        fake = FakeSerial(
            {
                0: OFFLOAD_ACTIVE_ZERO,
                1: STALL_ACK_CONS1,  # stall ACK but no recovery
            }
        )
        runner = GateRunner(fake, total_timeout=5.0, status_interval=0.01)
        result = runner.run()

        self.assertFalse(result.passed)
        self.assertIn("recovery OK", result.error)


class TestGateExhaustion(unittest.TestCase):
    """Gate fails when max_exhaustion_count > 0."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_exhaustion_detected(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.1)
        mock_mono.side_effect = lambda: next(gen)

        fake = FakeSerial(
            {
                0: OFFLOAD_ACTIVE_ZERO,
                1: STALL_ACK_CONS1,
                2: RECOVERY_OK_LINE,
                3: STALL_ACK_CONS0,
                5: OFFLOAD_ACTIVE_EXHAUSTION,  # exhaustion=1
            }
        )
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        result = runner.run()

        self.assertFalse(result.passed)
        self.assertIn("exhaustion", result.error.lower())


class TestGateClearTooSlow(unittest.TestCase):
    """Clear >1200ms after recovery → failure."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_clear_too_slow(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        # timeline: t=1000 ACTIVE, t=1000.05 stall ACK,
        # t=1000.10 recovery, t=1001.50 clear (>1200ms after recovery)
        times = [
            1000.0,
            1000.02,
            1000.04,
            1000.05,
            1000.06,
            1000.07,
            1001.50,
        ]
        times += [1001.5 + i * 0.1 for i in range(50)]
        mock_mono.side_effect = times

        fake = FakeSerial(
            {
                0: OFFLOAD_ACTIVE_ZERO,
                1: STALL_ACK_CONS1,
                2: RECOVERY_OK_LINE,
                3: STALL_ACK_CONS0,
            }
        )
        runner = GateRunner(fake, total_timeout=5.0, status_interval=0.01)
        result = runner.run()

        self.assertFalse(result.passed)
        self.assertIn("too slow", result.error.lower())


class TestGateNotActiveAtStart(unittest.TestCase):
    """Gate retries until ACTIVE appears."""

    @patch("flpr_stall_gate.time.sleep")
    @patch("flpr_stall_gate.time.monotonic")
    def test_waits_for_active(self, mock_mono, mock_sleep):
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0, 0.02)
        mock_mono.side_effect = lambda: next(gen)

        fake = FakeSerial(
            {
                0: OFFLOAD_PREPARING,  # first poll: PREPARING
                1: OFFLOAD_PREPARING,  # second poll: still PREPARING
                2: OFFLOAD_ACTIVE_ZERO,  # third poll: ACTIVE!
                3: STALL_ACK_CONS1,
                4: RECOVERY_OK_LINE,
                5: STALL_ACK_CONS0,
                7: OFFLOAD_ACTIVE_PROBATION_FIRST,
                9: OFFLOAD_ACTIVE_PROBATION_CLEARED,
            }
        )
        runner = GateRunner(fake, total_timeout=10.0, status_interval=0.01)
        result = runner.run()

        self.assertTrue(
            result.passed, f"Should pass after ACTIVE appears: {result.error}"
        )


if __name__ == "__main__":
    unittest.main()
