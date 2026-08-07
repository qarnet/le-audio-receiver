#!/usr/bin/env python3
"""Unit tests for scripts/bsim_official_smoke_parse.py — evidence-based
acceptance of the official BSim BAP unicast audio smoke (non-hardware).

Covers the parser and the check CLI: success, known teardown after
sufficient progress, teardown too early, unrelated failure, missing
progress, malformed progress, and the exact min-SDU boundary.
"""

import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "scripts"
    ),
)

from bsim_official_smoke_parse import (  # noqa: E402
    evaluate,
    parse_smoke_logs,
)

MIN_SDUS = 100

# Exact marker shape emitted by bap_stream_rx.c log_stream_rx().
PROGRESS = (
    "[%d|%d]: Incoming audio on stream 0x1234 len 40, flags 0x00, seq_num %d and ts %d"
)


def progress_log(n, device="client"):
    """A log proving n valid RX SDUs (progress line only printed for the
    last count — a realistic monotone tail)."""
    lines = []
    for i in range(1, n + 1):
        lines.append(PROGRESS % (i, i, i, i * 1000))
    return "\n".join(lines) + "\n"


TEARDOWN_LINE = "[123.456789] <inf> bap_stream_rx: FAIL: ISO receive lost\n"


class TestParseSmokeLogs(unittest.TestCase):
    def test_progress_counts_extracted(self):
        stats = parse_smoke_logs(progress_log(120), "")
        self.assertEqual(stats["max_valid_rx"], 120)
        self.assertEqual(stats["progress_lines"], 120)
        self.assertTrue(stats["progress_seen"])
        self.assertFalse(stats["malformed"])
        self.assertFalse(stats["teardown_race"])

    def test_server_side_progress_counted(self):
        stats = parse_smoke_logs("", progress_log(100))
        self.assertEqual(stats["max_valid_rx"], 100)
        self.assertTrue(stats["progress_seen"])

    def test_teardown_marker_detected(self):
        stats = parse_smoke_logs(progress_log(100) + TEARDOWN_LINE, "")
        self.assertTrue(stats["teardown_race"])

    def test_empty_logs(self):
        stats = parse_smoke_logs("", "")
        self.assertFalse(stats["progress_seen"])
        self.assertEqual(stats["max_valid_rx"], 0)
        self.assertFalse(stats["teardown_race"])

    def test_malformed_progress_line(self):
        text = "Incoming audio on stream 0x1234 len 40, flags 0x00\n"
        stats = parse_smoke_logs(text, "")
        self.assertTrue(stats["progress_seen"])
        self.assertTrue(stats["malformed"])
        self.assertEqual(stats["max_valid_rx"], 0)

    def test_noise_ignored(self):
        text = "noise [1|1]: something else on stream\n" + PROGRESS % (3, 3, 3, 3000)
        stats = parse_smoke_logs(text, "")
        self.assertEqual(stats["max_valid_rx"], 3)
        self.assertEqual(stats["progress_lines"], 1)


class TestEvaluate(unittest.TestCase):
    def test_full_success_exit_zero(self):
        accepted, reason, stats = evaluate(progress_log(100), "", 0, MIN_SDUS)
        self.assertTrue(accepted, reason)
        self.assertIn("exit 0", reason)
        self.assertEqual(stats["max_valid_rx"], 100)

    def test_known_teardown_after_sufficient_progress(self):
        accepted, reason, _ = evaluate(
            progress_log(100) + TEARDOWN_LINE, "", 1, MIN_SDUS
        )
        self.assertTrue(accepted, reason)
        self.assertIn("teardown disable-race", reason)
        self.assertIn("100 >= 100", reason)

    def test_teardown_too_early_rejected(self):
        accepted, reason, stats = evaluate(
            progress_log(50) + TEARDOWN_LINE, "", 1, MIN_SDUS
        )
        self.assertFalse(accepted)
        self.assertIn("short progress: max valid RX 50 < 100", reason)
        self.assertEqual(stats["max_valid_rx"], 50)

    def test_unrelated_failure_rejected(self):
        accepted, reason, _ = evaluate(
            progress_log(100) + "[123] <err> Could not connect to peer\n",
            "",
            1,
            MIN_SDUS,
        )
        self.assertFalse(accepted)
        self.assertIn("no known teardown marker — unrelated failure", reason)

    def test_missing_progress_rejected(self):
        accepted, reason, _ = evaluate("no progress at all", "", 0, MIN_SDUS)
        self.assertFalse(accepted)
        self.assertIn("no SDU progress markers", reason)

    def test_malformed_progress_rejected(self):
        accepted, reason, _ = evaluate(
            "Incoming audio on stream 0x1234 len 40\n", "", 0, MIN_SDUS
        )
        self.assertFalse(accepted)
        self.assertIn("malformed progress lines", reason)

    def test_exact_min_sdu_boundary_accepted(self):
        accepted, reason, _ = evaluate(progress_log(100), "", 0, MIN_SDUS)
        self.assertTrue(accepted, reason)

    def test_below_min_sdu_rejected_even_exit_zero(self):
        accepted, reason, _ = evaluate(progress_log(99), "", 0, MIN_SDUS)
        self.assertFalse(accepted)
        self.assertIn("short progress", reason)


class TestCheckCli(unittest.TestCase):
    """CLI check path with real temp log files (fixture test, no BSim)."""

    PARSER = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..",
        "..",
        "..",
        "scripts",
        "bsim_official_smoke_parse.py",
    )

    def _run(self, client_text, server_text, smoke_rc, min_sdus=100):
        with tempfile.TemporaryDirectory() as tmp:
            client = os.path.join(tmp, "client.log")
            server = os.path.join(tmp, "server.log")
            with open(client, "w") as fh:
                fh.write(client_text)
            with open(server, "w") as fh:
                fh.write(server_text)
            return subprocess.run(
                [
                    sys.executable,
                    self.PARSER,
                    "check",
                    "--client",
                    client,
                    "--server",
                    server,
                    "--min-sdus",
                    str(min_sdus),
                    "--smoke-rc",
                    str(smoke_rc),
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )

    def test_success_exit_zero(self):
        r = self._run(progress_log(100), "", 0)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertIn("ACCEPTED", r.stdout)
        self.assertIn("max_valid_rx=100", r.stdout)

    def test_teardown_accepted_with_evidence(self):
        r = self._run(progress_log(100) + TEARDOWN_LINE, "", 1)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertIn("ACCEPTED", r.stdout)
        self.assertIn("teardown disable-race", r.stdout)

    def test_teardown_too_early_rejected(self):
        r = self._run(progress_log(40) + TEARDOWN_LINE, "", 1)
        self.assertEqual(1, r.returncode)
        self.assertIn("REJECTED: short progress", r.stderr)

    def test_unrelated_failure_rejected(self):
        r = self._run(progress_log(100) + "[123] <err> boom\n", "", 1)
        self.assertEqual(1, r.returncode)
        self.assertIn("REJECTED: process exit 1", r.stderr)

    def test_missing_client_log_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            server = os.path.join(tmp, "server.log")
            with open(server, "w") as fh:
                fh.write(progress_log(100))
            r = subprocess.run(
                [
                    sys.executable,
                    self.PARSER,
                    "check",
                    "--client",
                    os.path.join(tmp, "missing.log"),
                    "--server",
                    server,
                    "--min-sdus",
                    "100",
                    "--smoke-rc",
                    "0",
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
        self.assertEqual(1, r.returncode)
        self.assertIn("cannot read client log", r.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
