#!/usr/bin/env python3
"""Public-boundary tests for LC3 calibration and corpus generation tools.

These tests deliberately stop before direct liblc3 compilation. They exercise
real output-path, manifest, and fixture-file validation, and verify failures
never leave final evidence or copied fixtures behind.
"""

import datetime
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


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


def copy_calibration_repository(repo):
    script = copy_public_script(repo)
    fixture_dir = repo / "tests" / "fixtures" / "lc3"

    fixture_dir.parent.mkdir(parents=True)
    shutil.copytree(FIXTURES_DIR, fixture_dir)
    return script, fixture_dir


def manifest_limits():
    return json.loads(
        (FIXTURES_DIR / "portable-oracle-manifest.json").read_text(encoding="utf-8")
    )["pcm_limits"]


def complete_metric_records():
    limits = manifest_limits()
    records = []
    for (
        comparison,
        stem,
        reference_stem,
        frames,
        samples,
        evaluation,
    ) in calibrate.expected_metric_records():
        record = {
            "record": "metric",
            "comparison": comparison,
            "stem": stem,
            "reference_stem": reference_stem,
            "squared_error": 0,
            "actual_energy_scaled": 1,
            "reference_energy_scaled": 1,
            "dot_product_scaled": 1,
            "samples": samples,
            "frames": frames,
            "max_abs_error": 0,
            "rms_error": 0,
            "correlation_q15": 32767,
            "evaluation": evaluation,
        }
        if evaluation == "max-error":
            record["max_abs_error"] = limits["max_abs_error"] + 1
            record["squared_error"] = record["max_abs_error"] ** 2
        elif evaluation == "rms-error":
            record["max_abs_error"] = limits["max_rms_error"] + 1
            record["rms_error"] = limits["max_rms_error"] + 1
            record["squared_error"] = record["rms_error"] ** 2 * samples
        elif evaluation == "correlation":
            record["max_abs_error"] = limits["max_rms_error"]
            record["rms_error"] = limits["max_rms_error"]
            record["squared_error"] = record["rms_error"] ** 2 * samples
            record["dot_product_scaled"] = -1
            record["correlation_q15"] = -32768
        records.append(record)
    return records


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

    def test_schema_one_manifest_is_rejected_before_compile_without_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo = root / "repo"
            script, fixture_dir = copy_calibration_repository(repo)
            manifest_path = fixture_dir / "portable-oracle-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            output = root / "rejected.json"

            manifest["schema_version"] = 1
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            result = run_calibrator(
                script, output, root, {"NCS": str(root / "missing-ncs")}
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unsupported manifest schema_version", result.stderr)
            self.assertNotIn("liblc3 module not found", result.stderr)
            self.assertFalse(output.exists())

    def test_invalid_pcm_limit_fields_are_rejected_before_compile_without_output(self):
        cases = (
            ("missing", lambda limits: limits.pop("max_abs_error"), "missing fields"),
            ("unknown", lambda limits: limits.update({"unknown": 1}), "unknown fields"),
            (
                "boolean",
                lambda limits: limits.update({"max_abs_error": True}),
                "pcm_limits.max_abs_error must be an integer",
            ),
            (
                "non-integer",
                lambda limits: limits.update({"max_rms_error": "512"}),
                "pcm_limits.max_rms_error must be an integer",
            ),
            (
                "negative-maximum",
                lambda limits: limits.update({"max_abs_error": -1}),
                "pcm_limits.max_abs_error is below range",
            ),
            (
                "maximum-too-large",
                lambda limits: limits.update({"max_rms_error": 65536}),
                "pcm_limits.max_rms_error is above range",
            ),
            (
                "correlation-too-small",
                lambda limits: limits.update({"min_correlation_q15": -32769}),
                "pcm_limits.min_correlation_q15 is below range",
            ),
            (
                "correlation-too-large",
                lambda limits: limits.update({"min_correlation_q15": 32768}),
                "pcm_limits.min_correlation_q15 is above range",
            ),
        )
        for name, mutate, expected_error in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                repo = root / "repo"
                script, fixture_dir = copy_calibration_repository(repo)
                manifest_path = fixture_dir / "portable-oracle-manifest.json"
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                output = root / (name + ".json")

                mutate(manifest["pcm_limits"])
                manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
                result = run_calibrator(
                    script, output, root, {"NCS": str(root / "missing-ncs")}
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)
                self.assertNotIn("liblc3 module not found", result.stderr)
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


class Lc3PcmCalibrateProtocolTests(unittest.TestCase):
    def test_exact_manifest_policy_reaches_calibrator_argv(self):
        limits = manifest_limits()
        commands = []

        def fake_run(command, **_kwargs):
            commands.append(command)
            return mock.Mock(returncode=0, stderr=b"", stdout=b"")

        with (
            mock.patch.object(calibrate.subprocess, "run", side_effect=fake_run),
            mock.patch.object(calibrate, "parse_metric_records", return_value=[]),
        ):
            _command, metrics = calibrate.run_calibrator(
                ["cc"], Path("/tmp/liblc3"), limits
            )

        self.assertEqual(metrics, [])
        self.assertEqual(len(commands), 2)
        self.assertEqual(commands[1][1], str(FIXTURES_DIR))
        self.assertEqual(
            commands[1][-3:],
            [
                str(limits["max_abs_error"]),
                str(limits["max_rms_error"]),
                str(limits["min_correlation_q15"]),
            ],
        )

    def test_schema_two_report_contains_exact_manifest_policy(self):
        manifest, fixture_hashes, manifest_sha256 = calibrate.load_manifest()
        limits = manifest["pcm_limits"]
        metrics = complete_metric_records()
        repository = {"head": "a" * 40, "status_porcelain_v1_raw": ""}

        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "report.json"
            liblc3 = Path("/tmp/liblc3")
            with (
                mock.patch.object(
                    calibrate,
                    "resolve_ncs",
                    return_value=(Path("/tmp/ncs"), liblc3, "revision"),
                ),
                mock.patch.object(calibrate, "compiler_argv", return_value=["cc"]),
                mock.patch.object(
                    calibrate,
                    "capture_utc_timestamp",
                    return_value="2026-09-16T00:00:00.000000Z",
                ),
                mock.patch.object(
                    calibrate, "capture_repository_provenance", return_value=repository
                ),
                mock.patch.object(
                    calibrate, "calibration_input_records", return_value=[]
                ),
                mock.patch.object(calibrate, "_capture", return_value="raw\n"),
                mock.patch.object(
                    calibrate,
                    "run_calibrator",
                    return_value=(["cc", "compiled"], metrics),
                ) as run_mock,
            ):
                self.assertEqual(calibrate.main(["--output", str(output)]), 0)

            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["schema_version"], 2)
            self.assertEqual(report["pcm_limits"], limits)
            self.assertEqual(report["metrics"], metrics)
            self.assertEqual(report["manifest_sha256"], manifest_sha256)
            self.assertEqual(report["fixture_hashes"], fixture_hashes)
            self.assertEqual(report["ncs_version"], manifest["ncs_version"])
            run_mock.assert_called_once_with(["cc"], liblc3, limits)

    def test_metric_protocol_requires_exact_order_identity_and_evaluation(self):
        limits = manifest_limits()
        records = complete_metric_records()
        payload = "\n".join(json.dumps(record) for record in records)

        self.assertEqual(calibrate.parse_metric_records(payload, limits), records)

        cases = []
        wrong_count = records[:-1]
        cases.append(("record count", wrong_count, "returned 25 records"))

        wrong_order = json.loads(json.dumps(records))
        wrong_order[0], wrong_order[1] = wrong_order[1], wrong_order[0]
        cases.append(("record order", wrong_order, "comparison identity is invalid"))

        wrong_identity = json.loads(json.dumps(records))
        wrong_identity[0]["stem"] = "wrong-stem"
        cases.append(
            ("record identity", wrong_identity, "comparison identity is invalid")
        )

        wrong_evaluation = json.loads(json.dumps(records))
        wrong_evaluation[0]["evaluation"] = "max-error"
        cases.append(("claimed evaluation", wrong_evaluation, "evaluation is invalid"))

        inconsistent_evaluation = json.loads(json.dumps(records))
        inconsistent = inconsistent_evaluation[4]
        inconsistent["squared_error"] = 0
        inconsistent["actual_energy_scaled"] = 1
        inconsistent["reference_energy_scaled"] = 1
        inconsistent["dot_product_scaled"] = 1
        inconsistent["max_abs_error"] = 0
        inconsistent["rms_error"] = 0
        inconsistent["correlation_q15"] = 32767
        cases.append(
            (
                "numerically inconsistent evaluation",
                inconsistent_evaluation,
                "evaluation does not match policy",
            )
        )

        for name, invalid_records, expected_error in cases:
            with self.subTest(name=name):
                invalid_payload = "\n".join(
                    json.dumps(record) for record in invalid_records
                )
                with self.assertRaisesRegex(calibrate.CalibrationError, expected_error):
                    calibrate.parse_metric_records(invalid_payload, limits)


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

    def test_schema_one_and_invalid_pcm_limits_fail_before_compiler_lookup(self):
        cases = (
            (
                "schema-one",
                lambda manifest: manifest.update({"schema_version": 1}),
                "schema_version",
            ),
            (
                "missing-policy",
                lambda manifest: manifest.pop("pcm_limits"),
                "manifest is missing fields",
            ),
            (
                "unknown-policy",
                lambda manifest: manifest["pcm_limits"].update({"unknown": 1}),
                "manifest.pcm_limits has unknown fields",
            ),
            (
                "boolean-policy",
                lambda manifest: manifest["pcm_limits"].update({"max_abs_error": True}),
                "manifest.pcm_limits.max_abs_error must be an integer",
            ),
            (
                "non-integer-policy",
                lambda manifest: manifest["pcm_limits"].update(
                    {"max_rms_error": "512"}
                ),
                "manifest.pcm_limits.max_rms_error must be an integer",
            ),
            (
                "out-of-range-policy",
                lambda manifest: manifest["pcm_limits"].update(
                    {"min_correlation_q15": 32768}
                ),
                "manifest.pcm_limits.min_correlation_q15 is outside range",
            ),
        )
        for name, mutate, expected_error in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                fixture_dir = root / "fixtures"
                script = copy_fixture_generator(fixture_dir)
                manifest_path = fixture_dir / "portable-oracle-manifest.json"
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

                mutate(manifest)
                manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
                result = run_generator(
                    script, [], root, {"NCS": str(root / "missing-ncs")}
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)
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
