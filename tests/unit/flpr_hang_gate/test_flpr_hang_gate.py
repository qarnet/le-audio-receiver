#!/usr/bin/env python3
"""Unit tests for the flpr_hang_gate.py console parser (Phase T6 review-fix).

Proves that RE_RUNTIME_RESTART_OK matches the exact production line
emitted by src/audio_shell.c's `flpr restart` command —

    FLPR restart OK: epoch <old>→<new> crc=0x<8 hex> duration=total <n> ms

— and that unrelated/partial/old drifted forms never produce a false
recovery observation.  The gate script imports pyserial lazily, so this
suite runs on stdlib-only python3.
"""

import os
import sys
import unittest

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "scripts"
    ),
)

from flpr_hang_gate import (  # noqa: E402
    RE_FAULT_HANG_ACK,
    RE_FAULT_HANG_FAIL,
    RE_RUNTIME_RESTART_OK,
)

# Exact production line shape: src/audio_shell.c cmd_flpr_restart prints
#   "FLPR restart OK: epoch %u→%u crc=0x%08x duration=total %u ms"
PRODUCTION_OK = "FLPR restart OK: epoch 5→6 crc=0x1234abcd duration=total 100 ms"


class TestRestartOkParser(unittest.TestCase):
    def test_exact_production_line_extracts_groups(self):
        m = RE_RUNTIME_RESTART_OK.search(PRODUCTION_OK)
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), "5")  # old epoch
        self.assertEqual(m.group(2), "6")  # new epoch
        self.assertEqual(m.group(3), "1234abcd")  # crc
        self.assertEqual(m.group(4), "100")  # duration ms

    def test_multi_digit_values(self):
        line = "FLPR restart OK: epoch 12345→12346 crc=0xDEADBEEF duration=total 987 ms"
        m = RE_RUNTIME_RESTART_OK.search(line)
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), "12345")
        self.assertEqual(m.group(2), "12346")
        self.assertEqual(m.group(3), "DEADBEEF")
        self.assertEqual(m.group(4), "987")

    def test_zero_values(self):
        line = "FLPR restart OK: epoch 0→1 crc=0x00000000 duration=total 0 ms"
        m = RE_RUNTIME_RESTART_OK.search(line)
        self.assertIsNotNone(m)
        self.assertEqual(m.groups(), ("0", "1", "00000000", "0"))

    def test_old_drifted_form_without_epoch_rejected(self):
        # The pre-fix parser matched this stale shape; production has
        # emitted the "epoch" literal since Stage 4A, so a match here
        # would be a false recovery observation.
        stale = "FLPR restart OK: 5→6 crc=0x1234abcd duration=total 100 ms"
        self.assertIsNone(RE_RUNTIME_RESTART_OK.search(stale))

    def test_partial_forms_rejected(self):
        cases = [
            "FLPR restart OK: epoch 5→6",  # no crc/duration
            "FLPR restart OK: epoch 5→6 crc=0x1234abcd",  # no duration
            "FLPR restart OK: epoch 5→6 duration=total 100 ms",  # no crc
            "FLPR restart OK: epoch →6 crc=0x1234abcd duration=total 100 ms",
            "FLPR restart OK: epoch 5→ crc=0x1234abcd duration=total 100 ms",
            "FLPR restart OK: epoch 5→6 crc=0xzzz duration=total 100 ms",
            "FLPR restart OK: epoch 5→6 crc=0x1234abcd duration=total ms",
        ]
        for line in cases:
            self.assertIsNone(RE_RUNTIME_RESTART_OK.search(line), line)

    def test_unrelated_console_text_rejected(self):
        text = (
            "--- Audio offload ---\n"
            "  State       : ACTIVE / epoch=7 gen=2\n"
            "  Runtime     : restarts=1 fails=0 last_ms=123 remote_epoch=8\n"
            "uart:~$ flpr restart\n"
            "FLPR restart: requesting (timeout=10000 ms)...\n"
            "FLPR restart FAILED: -5\n"
        )
        self.assertIsNone(RE_RUNTIME_RESTART_OK.search(text))

    def test_representative_console_text_matches(self):
        text = (
            "uart:~$ flpr restart\n"
            "FLPR restart: requesting (timeout=10000 ms)...\n"
            "FLPR restart OK: epoch 5→6 crc=0x1234abcd duration=total 100 ms\n"
            "uart:~$ flpr offload\n"
            "--- Audio offload ---\n"
            "  State       : ACTIVE / epoch=6 gen=3\n"
        )
        m = RE_RUNTIME_RESTART_OK.search(text)
        self.assertIsNotNone(m)
        self.assertEqual(m.groups(), ("5", "6", "1234abcd", "100"))

    def test_no_restart_line_no_false_observation(self):
        text = (
            "uart:~$ flpr offload\n"
            "--- Audio offload ---\n"
            "  State       : ACTIVE / epoch=6 gen=3\n"
            "  Runtime     : restarts=1 fails=0 last_ms=123 remote_epoch=6\n"
        )
        self.assertIsNone(RE_RUNTIME_RESTART_OK.search(text))


class TestSiblingGateRegexes(unittest.TestCase):
    """The other gate-parsed regexes keep their exact production shapes."""

    def test_fault_hang_ack(self):
        self.assertIsNotNone(RE_FAULT_HANG_ACK.search("FAULT_HANG_ACK received"))
        self.assertIsNone(RE_FAULT_HANG_ACK.search("FAULT_HANG failed: -5 (no ACK)"))

    def test_fault_hang_fail_extracts_errno(self):
        m = RE_FAULT_HANG_FAIL.search("FAULT_HANG failed: -5 (no ACK)")
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), "-5")


if __name__ == "__main__":
    unittest.main()
