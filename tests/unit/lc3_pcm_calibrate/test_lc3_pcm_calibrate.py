#!/usr/bin/env python3
"""Public-boundary tests for LC3 calibration and corpus generation tools.

These tests deliberately stop before direct liblc3 compilation. They exercise
real output-path, manifest, and fixture-file validation, and verify failures
never leave final evidence or copied fixtures behind.
"""

import datetime
import hashlib
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / "scripts" / "lc3_pcm_calibrate.py"
FIXTURES_DIR = REPO_ROOT / "tests" / "fixtures" / "lc3"
GENERATOR = FIXTURES_DIR / "generate.sh"
_SPEC = importlib.util.spec_from_file_location("lc3_pcm_calibrate", SCRIPT)
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("cannot load lc3_pcm_calibrate.py")
calibrate = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(calibrate)


def run_calibrator(script, output, cwd, extra_env=None):
    environment = dict(os.environ)
    if extra_env:
        environment.update(extra_env)
    return subprocess.run(
        [sys.executable, str(script), "--output", str(output)],
        cwd=str(cwd),
        env=environment,
        capture_output=True,
        text=True,
        timeout=30,
    )


def run_generator(script, arguments, cwd, extra_env=None):
    environment = dict(os.environ)
    if extra_env:
        environment.update(extra_env)
    return subprocess.run(
        ["bash", str(script)] + list(arguments),
        cwd=str(cwd),
        env=environment,
        capture_output=True,
        text=True,
        timeout=30,
    )


def copy_public_script(repo):
    script = repo / "scripts" / "lc3_pcm_calibrate.py"
    script.parent.mkdir(parents=True)
    shutil.copy2(SCRIPT, script)
    return script


def copy_fixture_generator(destination):
    shutil.copytree(FIXTURES_DIR, destination)
    return destination / "generate.sh"


class Lc3PcmCalibrateCliTests(unittest.TestCase):
    def test_relative_output_is_rejected_before_validation(self):
        with tempfile.TemporaryDirectory() as temp:
            result = run_calibrator(SCRIPT, "relative.json", temp)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("--output must be an absolute path", result.stderr)
            self.assertFalse((Path(temp) / "relative.json").exists())

    def test_existing_output_is_rejected_without_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "existing.json"
            output.write_bytes(b"existing evidence\n")

            result = run_calibrator(SCRIPT, output, temp)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("--output already exists", result.stderr)
            self.assertEqual(output.read_bytes(), b"existing evidence\n")

    def test_dangling_output_symlink_is_rejected_without_following_target(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            output = root / "dangling.json"
            output.symlink_to(root / "missing-target.json")

            result = run_calibrator(SCRIPT, output, root)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("--output already exists", result.stderr)
            self.assertTrue(os.path.lexists(output))

    def test_repository_contained_output_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp) / "repo"
            script = copy_public_script(repo)
            output = repo / "evidence.json"

            result = run_calibrator(script, output, temp)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("must not be inside repository", result.stderr)
            self.assertFalse(output.exists())

    def test_unknown_manifest_field_is_rejected_without_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo = root / "repo"
            script = copy_public_script(repo)
            manifest = (
                repo / "tests" / "fixtures" / "lc3" / "portable-oracle-manifest.json"
            )
            manifest.parent.mkdir(parents=True)
            manifest.write_text(
                '{"schema_version": 1, "unknown": true}\n', encoding="utf-8"
            )
            output = root / "rejected.json"

            result = run_calibrator(script, output, root)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unknown fields", result.stderr)
            self.assertFalse(output.exists())

    def test_validation_failure_after_fixture_hashing_leaves_no_output(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "not-written.json"
            missing_ncs = Path(temp) / "missing-ncs"

            result = run_calibrator(SCRIPT, output, temp, {"NCS": str(missing_ncs)})

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("liblc3 module not found", result.stderr)
            self.assertFalse(output.exists())


class Lc3PcmCalibrateProvenanceTests(unittest.TestCase):
    def test_timestamp_is_utc_and_deterministic_for_given_time(self):
        captured_at = datetime.datetime(
            2026,
            9,
            13,
            12,
            34,
            56,
            123456,
            tzinfo=datetime.timezone(datetime.timedelta(hours=2)),
        )

        self.assertEqual(
            calibrate.format_utc_timestamp(captured_at),
            "2026-09-13T10:34:56.123456Z",
        )
        with self.assertRaises(calibrate.CalibrationError):
            calibrate.format_utc_timestamp(captured_at.replace(tzinfo=None))

    def test_repository_provenance_preserves_raw_dirty_status(self):
        status = " M scripts/lc3_pcm_calibrate.py\n?? tests/support/pcm_oracle.c\n"

        provenance = calibrate.repository_provenance("a" * 40 + "\n", status)

        self.assertEqual(
            provenance,
            {
                "head": "a" * 40,
                "status_porcelain_v1_raw": status,
            },
        )
        self.assertNotIn("clean", provenance)
        with self.assertRaises(calibrate.CalibrationError):
            calibrate.repository_provenance("not-a-git-head", status)

    def test_calibration_input_records_have_stable_complete_order(self):
        records = calibrate.calibration_input_records()
        expected_paths = [
            "scripts/lc3_pcm_calibrate.py",
            "tests/fixtures/lc3/calibrate.c",
            "tests/support/pcm_oracle.c",
            "tests/support/pcm_oracle.h",
        ]

        self.assertEqual([record["path"] for record in records], expected_paths)
        for record in records:
            path = REPO_ROOT / record["path"]
            self.assertEqual(set(record), {"path", "size", "sha256"})
            self.assertEqual(record["size"], path.stat().st_size)
            self.assertEqual(
                record["sha256"],
                hashlib.sha256(path.read_bytes()).hexdigest(),
            )


class Lc3FixtureGeneratorTests(unittest.TestCase):
    def test_unknown_generator_argument_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            result = run_generator(
                GENERATOR,
                ["--not-a-generator-mode"],
                temp,
                {"NCS": str(Path(temp) / "missing-ncs")},
            )

            self.assertEqual(result.returncode, 2)
            self.assertIn("Usage:", result.stderr)

    def test_malformed_manifest_fails_before_compiler_lookup(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fixture_dir = root / "fixtures"
            script = copy_fixture_generator(fixture_dir)
            manifest = fixture_dir / "portable-oracle-manifest.json"
            manifest.write_text("{ malformed\n", encoding="utf-8")

            result = run_generator(
                script,
                [],
                root,
                {"NCS": str(root / "missing-ncs")},
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("malformed portable manifest", result.stderr)
            self.assertNotIn("liblc3 module not found", result.stderr)

    def test_checked_in_hash_mismatch_fails_even_in_rebase_mode(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fixture_dir = root / "fixtures"
            script = copy_fixture_generator(fixture_dir)
            corrupted = fixture_dir / "bsim_48k_10ms_120b_l.pcm"
            original = corrupted.read_bytes()
            damaged = bytes([original[0] ^ 0x01]) + original[1:]
            corrupted.write_bytes(damaged)

            for arguments in ([], ["--rebase-portable"]):
                result = run_generator(
                    script,
                    arguments,
                    root,
                    {"NCS": str(root / "missing-ncs")},
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn("fixture SHA-256 mismatch", result.stderr)
                self.assertNotIn("liblc3 module not found", result.stderr)
                self.assertEqual(corrupted.read_bytes(), damaged)


if __name__ == "__main__":
    unittest.main(verbosity=2)
