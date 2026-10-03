#!/usr/bin/env python3
"""Exercise real encoded H4 traffic through the portable bridge parser."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class H4TrafficTests(unittest.TestCase):
    def test_encoded_traffic_fragmentation_errors_and_reset(self):
        with tempfile.TemporaryDirectory() as tmp:
            executable = Path(tmp) / "h4-traffic-test"
            source = ROOT / "dongle/hci_uart/src"
            subprocess.run(
                [
                    os.environ.get("CC", "cc"),
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(source),
                    str(source / "h4_rx.c"),
                    str(Path(__file__).with_name("traffic.c")),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("H4 traffic PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
