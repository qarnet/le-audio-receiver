#!/usr/bin/env python3
"""Exercise fw-build-dongle's public CLI without building firmware."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
FAKE_WEST = """#!/usr/bin/env python3
import json
import os
import sys

with open(os.environ["WEST_CALLS"], "a", encoding="utf-8") as calls:
    calls.write(json.dumps(sys.argv[1:]) + "\\n")
sys.exit(int(os.environ.get("WEST_EXIT", "0")))
"""


class FwBuildDongleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        base = Path(self.tmp.name)
        self.repo = base / "repo"
        scripts = self.repo / "scripts/bin"
        scripts.mkdir(parents=True)
        for name in ("fw-build-dongle", "fw-common.sh"):
            shutil.copy2(ROOT / "scripts/bin" / name, scripts / name)
        (self.repo / "dongle/hci_uart").mkdir(parents=True)
        self.script = scripts / "fw-build-dongle"

        fakebin = base / "fakebin"
        fakebin.mkdir()
        for name in ("bash", "dirname", "python3"):
            binary = shutil.which(name)
            assert binary is not None, f"{name} not found on PATH"
            (fakebin / name).symlink_to(binary)
        self.west = fakebin / "west"
        self.west.write_text(FAKE_WEST, encoding="utf-8")
        self.west.chmod(0o755)
        zephyr = base / "zephyr"
        zephyr.mkdir()
        self.calls = base / "west-calls.jsonl"
        self.env = dict(os.environ)
        self.env.update(
            PATH=str(fakebin),
            ZEPHYR_BASE=str(zephyr),
            WEST_CALLS=str(self.calls),
        )
        self.env.pop("WEST_EXIT", None)

    def run_build(self, *args, env=None, cwd=None):
        return subprocess.run(
            [str(self.script), *args],
            cwd=cwd,
            env=self.env if env is None else env,
            capture_output=True,
            text=True,
            timeout=30,
        )

    def west_calls(self):
        if not self.calls.exists():
            return []
        return [json.loads(line) for line in self.calls.read_text().splitlines()]

    def expected(self, *cmake_args):
        return [
            "build",
            "-b",
            "xiao_nrf54l15/nrf54l15/cpuapp",
            "--no-sysbuild",
            "--pristine",
            "-d",
            str(self.repo / "build/dongle"),
            str(self.repo / "dongle/hci_uart"),
            "--",
            *cmake_args,
        ]

    def test_single_target_build(self):
        result = self.run_build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.west_calls(), [self.expected()])
        self.assertNotIn("Merged hexes written", result.stdout + result.stderr)

    def test_cmake_args_with_and_without_separator(self):
        for prefix in ((), ("--",)):
            with self.subTest(prefix=prefix):
                result = self.run_build(*prefix, "-DFOO=bar", "-DOTHER=two words")
                self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self.west_calls(), [self.expected("-DFOO=bar", "-DOTHER=two words")] * 2
        )

    def test_west_failure_propagates(self):
        env = dict(self.env, WEST_EXIT="37")
        result = self.run_build(env=env)
        self.assertEqual(result.returncode, 37)
        self.assertEqual(self.west_calls(), [self.expected()])
        self.assertNotIn("Merged hexes written", result.stdout + result.stderr)

    def test_missing_zephyr_base_stops_before_west(self):
        env = dict(self.env)
        env.pop("ZEPHYR_BASE")
        result = self.run_build(env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ZEPHYR_BASE not set", result.stderr)
        self.assertEqual(self.west_calls(), [])

    def test_invalid_zephyr_base_stops_before_west(self):
        env = dict(self.env, ZEPHYR_BASE=str(Path(self.tmp.name) / "missing"))
        result = self.run_build(env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not exist", result.stderr)
        self.assertEqual(self.west_calls(), [])

    def test_missing_west_stops_before_build(self):
        self.west.unlink()
        result = self.run_build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("west not found on PATH", result.stderr)
        self.assertEqual(self.west_calls(), [])

    def test_unrelated_working_directory(self):
        elsewhere = Path(self.tmp.name) / "elsewhere"
        elsewhere.mkdir()
        result = self.run_build(cwd=elsewhere)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.west_calls(), [self.expected()])


if __name__ == "__main__":
    unittest.main(verbosity=2)
