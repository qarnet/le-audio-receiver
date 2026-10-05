"""Native public CLI guards for PB-053 lane; no VM execution."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "scripts"))
from bluez_host_lane import (
    bounded,
    intentional_rejection,
    strict_json,
    validate_collection,
)
from bluez_host_process import run_owned

LANE = REPO / "scripts/bluez_host_lane.py"
HOST = REPO / "scripts/bluez_host_guest.py"
INVENTORY = REPO / "tests/host_bluez/inventory.json"
SHA = "1de02a5083330284600ac740ab21a1f35ff76e5411377beb029ea251d324f276"


class LaneCLITests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.prepared = self.root / "prepared"
        self.prepared.mkdir()
        self.output = self.root / "new-lane"

    def cli(self, expected="0" * 64, output=None):
        return subprocess.run(
            [
                sys.executable,
                str(LANE),
                "--prepared",
                str(self.prepared),
                "--manifest-sha256",
                expected,
                "--output",
                str(output or self.output),
            ],
            capture_output=True,
            text=True,
            timeout=10,
        )

    def test_reviewed_inventory_anchor_is_fixed(self):
        self.assertEqual(hashlib.sha256(INVENTORY.read_bytes()).hexdigest(), SHA)
        forged = self.root / "inventory.json"
        forged.write_text(json.dumps({"required": []}))
        self.assertNotEqual(
            self.cli(hashlib.sha256(forged.read_bytes()).hexdigest()).returncode, 0
        )
        record = json.loads((self.output / "suite-record.json").read_text())
        self.assertEqual(record["inventory"]["sha256"], SHA)
        self.assertTrue(record["descendant_scope"]["ok"])
        self.assertEqual(record["descendant_scope"]["adopted"], [])
        self.assertEqual(set(record["processes"]), {"prepared-runtime"})
        self.assertFalse((self.output / "pytest.log").exists())

    def test_missing_runtime_and_invalid_digest_fail_before_pytest(self):
        for index, digest in enumerate(("bad", "0" * 64, "f" * 64)):
            output = self.root / f"attempt-{index}"
            proc = self.cli(digest, output)
            self.assertNotEqual(proc.returncode, 0, proc.stdout)
            record = json.loads((output / "suite-record.json").read_text())
            self.assertFalse(record["ok"])
            self.assertEqual(set(record["processes"]), {"prepared-runtime"})
            self.assertFalse((output / "pytest.log").exists())
            self.assertFalse((output / "report.xml").exists())

    def test_existing_output_and_nested_prepared_preserve_sentinel(self):
        self.output.mkdir()
        sentinel = self.output / "sentinel"
        sentinel.write_text("unchanged")
        self.assertNotEqual(self.cli().returncode, 0)
        self.assertEqual(sentinel.read_text(), "unchanged")
        nested = self.prepared / "new"
        self.assertNotEqual(self.cli(output=nested).returncode, 0)
        self.assertFalse(nested.exists())

    def test_guest_check_cli_read_only_on_missing_manifest(self):
        proc = subprocess.run(
            [
                sys.executable,
                str(HOST),
                "check",
                "--prepared",
                str(self.prepared),
                "--manifest-sha256",
                "0" * 64,
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(proc.returncode, 0)
        self.assertEqual(list(self.prepared.iterdir()), [])

    def test_bounded_same_fd_rejects_fifo_symlink_oversize(self):
        regular = self.root / "raw"
        regular.write_bytes(b"abc")
        self.assertEqual(bounded(regular, 3), b"abc")
        with self.assertRaises(ValueError):
            bounded(regular, 2)
        (self.root / "link").symlink_to(regular)
        os.mkfifo(self.root / "pipe")
        for path in (self.root / "link", self.root / "pipe"):
            with self.subTest(path=path), self.assertRaises((OSError, ValueError)):
                bounded(path, 3)

    def test_collection_and_accountant_json_require_unique_finite_schema(self):
        required = [["tests.host_bluez.test_host_lane", "test_public_phase[fresh1]"]]
        raw = {
            "pytest_version": "8.4.2",
            "nodeids": [
                "tests/host_bluez/test_host_lane.py::test_public_phase[fresh1]"
            ],
            "discovered_cases": required,
        }
        self.assertEqual(validate_collection(json.dumps(raw).encode(), required), raw)
        for value in (
            b'{"accepted":false,"accepted":true}',
            b'{"accepted":NaN}',
        ):
            with self.assertRaises(ValueError):
                strict_json(value)
        for changed in (
            {**raw, "extra": 1},
            {**raw, "pytest_version": "9.0"},
            {**raw, "nodeids": ["bad", "bad"]},
            {**raw, "nodeids": [42]},
            {**raw, "discovered_cases": [["", "case"]]},
            {**raw, "discovered_cases": [["class", "\n"]]},
            {**raw, "discovered_cases": [["class"]]},
            {**raw, "discovered_cases": [required[0], required[0]]},
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                validate_collection(json.dumps(changed).encode(), required)
        with self.assertRaises(ValueError):
            validate_collection(
                b'{"pytest_version":"8.4.2","nodeids":[],"nodeids":[],"discovered_cases":[]}',
                required,
            )

    def test_actual_accountant_process_outcome_must_be_intentional(self):
        failed = run_owned(
            [sys.executable, "-c", "import sys; sys.exit(1)"],
            self.root / "failed.log",
            2,
        )
        checked = {
            "process": failed,
            "accepted": False,
            "verdict": {"errors": [{"code": "case-coverage"}]},
        }
        self.assertTrue(intentional_rejection(checked, "case-coverage"))
        self.assertFalse(intentional_rejection(checked, "case-outcome"))
        timed = run_owned(
            [sys.executable, "-c", "import time; time.sleep(3)"],
            self.root / "timeout.log",
            0.1,
        )
        self.assertFalse(
            intentional_rejection({**checked, "process": timed}, "case-coverage")
        )
        for name, invalid in (
            ("timed_out", True),
            ("cancelled_signal", 15),
            ("log_limit_exceeded", True),
            ("descendant_cleanup_required", True),
            ("cleanup_errors", ["cleanup failed"]),
            ("error", "spawn failed"),
        ):
            with self.subTest(name=name):
                self.assertFalse(
                    intentional_rejection(
                        {**checked, "process": {**failed, name: invalid}},
                        "case-coverage",
                    )
                )


if __name__ == "__main__":
    unittest.main()
