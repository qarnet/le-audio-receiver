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
import textwrap
import unittest
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / "scripts" / "lc3_pcm_calibrate.py"
FIXTURES_DIR = REPO_ROOT / "tests" / "fixtures" / "lc3"
GENERATOR = FIXTURES_DIR / "generate.sh"
STATEFUL_GENERATOR = FIXTURES_DIR / "generate_stateful_references.sh"
SUPPORT_DIR = REPO_ROOT / "tests" / "support"
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


def copy_stateful_generator_repository(repo):
    fixture_dir = repo / "tests" / "fixtures" / "lc3"
    support_dir = repo / "tests" / "support"

    fixture_dir.parent.mkdir(parents=True)
    shutil.copytree(FIXTURES_DIR, fixture_dir)
    support_dir.mkdir(parents=True)
    for name in ("lc3_stateful_recipes.c", "lc3_stateful_recipes.h"):
        shutil.copy2(SUPPORT_DIR / name, support_dir / name)
    return fixture_dir / "generate_stateful_references.sh", fixture_dir


def copy_calibration_repository(repo):
    script = copy_public_script(repo)
    fixture_dir = repo / "tests" / "fixtures" / "lc3"
    support_dir = repo / "tests" / "support"

    fixture_dir.parent.mkdir(parents=True)
    shutil.copytree(FIXTURES_DIR, fixture_dir)
    support_dir.mkdir(parents=True)
    for name in (
        "pcm_oracle.c",
        "pcm_oracle.h",
        "lc3_stateful_recipes.c",
        "lc3_stateful_recipes.h",
    ):
        shutil.copy2(SUPPORT_DIR / name, support_dir / name)
    return script, fixture_dir


def manifest_limits():
    return json.loads(
        (FIXTURES_DIR / "portable-oracle-manifest.json").read_text(encoding="utf-8")
    )["pcm_limits"]


def update_stateful_source_manifest(fixture_dir):
    portable_path = fixture_dir / "portable-oracle-manifest.json"
    stateful_path = fixture_dir / "stateful-reference-manifest.json"
    portable_raw = portable_path.read_bytes()
    stateful = json.loads(stateful_path.read_text(encoding="utf-8"))

    stateful["source_portable_manifest"] = {
        "path": "portable-oracle-manifest.json",
        "size": len(portable_raw),
        "sha256": hashlib.sha256(portable_raw).hexdigest(),
    }
    stateful_path.write_text(json.dumps(stateful), encoding="utf-8")


def update_portable_lc3_hash(fixture_dir, stem):
    portable_path = fixture_dir / "portable-oracle-manifest.json"
    portable = json.loads(portable_path.read_text(encoding="utf-8"))
    stream = next(entry for entry in portable["streams"] if entry["stem"] == stem)
    lc3_path = fixture_dir / stream["lc3"]["path"]

    stream["lc3"]["sha256"] = hashlib.sha256(lc3_path.read_bytes()).hexdigest()
    portable_path.write_text(json.dumps(portable), encoding="utf-8")
    update_stateful_source_manifest(fixture_dir)


def write_fake_stateful_compiler(path, candidate_mode, new_trace_template=None):
    template = "" if new_trace_template is None else str(new_trace_template)

    path.write_text(
        textwrap.dedent(
            f"""\
            #!/usr/bin/env bash
            set -euo pipefail
            output=""
            while [ "$#" -gt 0 ]; do
                if [ "$1" = "-o" ]; then
                    output="$2"
                    shift 2
                else
                    shift
                fi
            done
            cat > "$output" <<'EOF'
            #!/usr/bin/env python3
            import pathlib
            import sys

            source = pathlib.Path(sys.argv[1])
            output = pathlib.Path(sys.argv[2])
            mode = {candidate_mode!r}
            new_trace_template = {template!r}
            for name in (
                "stateful_48k_7p5ms_modea_start_r.pcm",
                "stateful_48k_10ms_skip20_l.pcm",
                "stateful_48k_10ms_loss48x18_r.pcm",
            ):
                source_path = source / name
                if source_path.is_file():
                    data = source_path.read_bytes()
                elif name == "stateful_48k_7p5ms_modea_start_r.pcm" and new_trace_template:
                    data = pathlib.Path(new_trace_template).read_bytes()
                else:
                    raise RuntimeError("missing fake stateful candidate: " + name)
                if mode == "truncate" and name.endswith("loss48x18_r.pcm"):
                    data = data[:-1]
                elif mode == "flip":
                    data = bytes([data[0] ^ 0x01]) + data[1:]
                (output / name).write_bytes(data)
            EOF
            chmod +x "$output"
            """
        ),
        encoding="utf-8",
    )
    path.chmod(0o755)


def write_fake_liblc3(directory):
    liblc3 = directory / "modules" / "lib" / "liblc3"
    include = liblc3 / "include"
    source = liblc3 / "src"

    (directory / ".west").mkdir(parents=True)
    (directory / ".west" / "config").write_text(
        "[manifest]\npath = nrf\nfile = west.yml\n\n[zephyr]\nbase = zephyr\n",
        encoding="utf-8",
    )
    (directory / "nrf").mkdir()
    (directory / "nrf" / "VERSION").write_text("3.3.0\n", encoding="utf-8")
    (directory / "zephyr").mkdir()
    include.mkdir(parents=True)
    source.mkdir()
    (include / "lc3.h").write_text("/* fake */\n", encoding="utf-8")
    for name in calibrate.LC3_SOURCES:
        (source / name).write_text("/* fake */\n", encoding="utf-8")
    return liblc3


def write_pinned_fake_git(
    directory,
    liblc3_revision=calibrate.EXPECTED_LIBLC3_REVISION,
    liblc3_status="",
):
    directory.mkdir()
    git = directory / "git"

    git.write_text(
        textwrap.dedent(
            f"""\
            #!/usr/bin/env bash
            set -euo pipefail
            case " $* " in
              *" config --file "*)
                case "${{!#}}" in
                  manifest.path)
                    printf '%s\\n' nrf
                    ;;
                  manifest.file)
                    printf '%s\\n' west.yml
                    ;;
                  zephyr.base)
                    printf '%s\\n' zephyr
                    ;;
                  *)
                    exit 2
                    ;;
                esac
                ;;
              *" rev-parse HEAD "*)
                case "$2" in
                  */nrf)
                    printf '%s\\n' '{calibrate.EXPECTED_NRF_REVISION}'
                    ;;
                  */zephyr)
                    printf '%s\\n' '{calibrate.EXPECTED_ZEPHYR_REVISION}'
                    ;;
                  */modules/lib/liblc3)
                    printf '%s\\n' '{liblc3_revision}'
                    ;;
                  *)
                    exit 2
                    ;;
                esac
                ;;
              *" status --porcelain "*)
                if [[ "$2" = */modules/lib/liblc3 ]]; then
                  printf '%s\\n' '{liblc3_status}'
                fi
                ;;
              *)
                exit 2
                ;;
            esac
            """
        ),
        encoding="utf-8",
    )
    git.chmod(0o755)
    return directory


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

    def test_stateful_manifest_fields_are_rejected_before_compile_without_output(self):
        cases = (
            (
                "unknown-top-level",
                lambda manifest: manifest.update({"unexpected": 1}),
                "stateful manifest has unknown fields",
            ),
            (
                "missing-reference-hash",
                lambda manifest: manifest["recipes"][0]["reference"].pop("sha256"),
                "is missing fields",
            ),
            (
                "boolean-reference-size",
                lambda manifest: manifest["recipes"][6]["reference"].update(
                    {"size": True}
                ),
                "must be an integer",
            ),
        )
        for name, mutate, expected_error in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                repo = root / "repo"
                script, fixture_dir = copy_calibration_repository(repo)
                stateful_path = fixture_dir / "stateful-reference-manifest.json"
                manifest = json.loads(stateful_path.read_text(encoding="utf-8"))
                output = root / (name + ".json")

                mutate(manifest)
                stateful_path.write_text(json.dumps(manifest), encoding="utf-8")
                result = run_calibrator(
                    script, output, root, {"NCS": str(root / "missing-ncs")}
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)
                self.assertNotIn("liblc3 module not found", result.stderr)
                self.assertFalse(output.exists())

    def test_stateful_source_and_reference_hash_mismatch_fail_closed(self):
        cases = (
            (
                "source-manifest-hash",
                lambda manifest: manifest["source_portable_manifest"].update(
                    {"sha256": "0" * 64}
                ),
                "source portable manifest SHA-256 mismatch",
            ),
            (
                "generated-reference-hash",
                lambda manifest: manifest["recipes"][6]["reference"].update(
                    {"sha256": "0" * 64}
                ),
                "fixture SHA-256 mismatch",
            ),
        )
        for name, mutate, expected_error in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                repo = root / "repo"
                script, fixture_dir = copy_calibration_repository(repo)
                stateful_path = fixture_dir / "stateful-reference-manifest.json"
                manifest = json.loads(stateful_path.read_text(encoding="utf-8"))
                output = root / (name + ".json")

                mutate(manifest)
                stateful_path.write_text(json.dumps(manifest), encoding="utf-8")
                result = run_calibrator(
                    script, output, root, {"NCS": str(root / "missing-ncs")}
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)
                self.assertNotIn("liblc3 module not found", result.stderr)
                self.assertFalse(output.exists())

    def test_payload_identity_failures_are_rejected_before_compile_without_output(self):
        cases = ("duplicate", "channel-overlap")
        for name in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                repo = root / "repo"
                script, fixture_dir = copy_calibration_repository(repo)
                left_path = fixture_dir / "bsim_48k_10ms_120b_l.lc3"
                left = bytearray(left_path.read_bytes())
                output = root / (name + ".json")

                if name == "duplicate":
                    left[120:240] = left[0:120]
                    expected_error = "payload identity is not unique"
                else:
                    right = (fixture_dir / "bsim_48k_10ms_120b_r.lc3").read_bytes()
                    left[0:120] = right[0:120]
                    expected_error = "payload identity channel overlap is not zero"
                left_path.write_bytes(left)
                update_portable_lc3_hash(fixture_dir, "bsim_48k_10ms_120b_l")

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
            "tests/support/lc3_stateful_recipes.c",
            "tests/support/lc3_stateful_recipes.h",
            "tests/fixtures/lc3/stateful-reference-manifest.json",
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

    def test_resolve_ncs_requires_pinned_clean_liblc3_checkout(self):
        manifest = {"liblc3": {"west_revision": calibrate.EXPECTED_LIBLC3_REVISION}}

        with tempfile.TemporaryDirectory() as temp:
            ncs = Path(temp) / "ncs"
            write_fake_liblc3(ncs)

            cases = (
                (
                    "non-git",
                    [
                        subprocess.CompletedProcess(
                            [], 128, stdout="", stderr="not a git repository"
                        )
                    ],
                    "cannot verify liblc3 Git revision",
                ),
                (
                    "wrong-revision",
                    [
                        subprocess.CompletedProcess(
                            [], 0, stdout="0" * 40 + "\n", stderr=""
                        )
                    ],
                    "liblc3 Git revision is not pinned revision",
                ),
                (
                    "dirty",
                    [
                        subprocess.CompletedProcess(
                            [],
                            0,
                            stdout=calibrate.EXPECTED_LIBLC3_REVISION + "\n",
                            stderr="",
                        ),
                        subprocess.CompletedProcess(
                            [], 0, stdout=" M src/lc3.c\n", stderr=""
                        ),
                    ],
                    "liblc3 Git working tree is dirty",
                ),
                (
                    "wrong-ncs",
                    [
                        subprocess.CompletedProcess(
                            [],
                            0,
                            stdout=calibrate.EXPECTED_LIBLC3_REVISION + "\n",
                            stderr="",
                        ),
                        subprocess.CompletedProcess([], 0, stdout="", stderr=""),
                        subprocess.CompletedProcess(
                            [], 0, stdout="0" * 40 + "\n", stderr=""
                        ),
                    ],
                    "NCS nrf Git revision is not pinned revision",
                ),
            )
            for name, results, expected_error in cases:
                with (
                    self.subTest(name=name),
                    mock.patch.dict(os.environ, {"NCS": str(ncs)}),
                    mock.patch.object(calibrate.subprocess, "run", side_effect=results),
                ):
                    with self.assertRaisesRegex(
                        calibrate.CalibrationError, expected_error
                    ):
                        calibrate.resolve_ncs(manifest)

            successful_results = [
                subprocess.CompletedProcess(
                    [],
                    0,
                    stdout=calibrate.EXPECTED_LIBLC3_REVISION + "\n",
                    stderr="",
                ),
                subprocess.CompletedProcess([], 0, stdout="", stderr=""),
                subprocess.CompletedProcess(
                    [], 0, stdout=calibrate.EXPECTED_NRF_REVISION + "\n", stderr=""
                ),
                subprocess.CompletedProcess([], 0, stdout="", stderr=""),
                subprocess.CompletedProcess(
                    [],
                    0,
                    stdout=calibrate.EXPECTED_ZEPHYR_REVISION + "\n",
                    stderr="",
                ),
                subprocess.CompletedProcess([], 0, stdout="", stderr=""),
            ]
            with (
                mock.patch.dict(os.environ, {"NCS": str(ncs)}),
                mock.patch.object(
                    calibrate.subprocess, "run", side_effect=successful_results
                ) as run,
            ):
                _resolved_ncs, resolved_liblc3, revision = calibrate.resolve_ncs(
                    manifest
                )

            self.assertEqual(resolved_liblc3, ncs / "modules" / "lib" / "liblc3")
            self.assertEqual(revision, calibrate.EXPECTED_LIBLC3_REVISION)
            self.assertEqual(run.call_count, 6)

    def test_stateful_recipe_manifest_and_payload_identity_are_exact(self):
        portable_manifest, _fixture_hashes, _manifest_sha256 = calibrate.load_manifest()
        (
            stateful_manifest,
            reference_hashes,
            _stateful_manifest_sha256,
        ) = calibrate.load_stateful_manifest(portable_manifest)
        payload_identity = calibrate.validate_payload_identity(portable_manifest)

        self.assertEqual(
            [entry["id"] for entry in stateful_manifest["recipes"]],
            [entry[0] for entry in calibrate.EXPECTED_STATEFUL_RECIPES],
        )
        self.assertEqual(len(reference_hashes), 8)
        self.assertEqual(
            [record["kind"] for record in reference_hashes],
            [entry[2] for entry in calibrate.EXPECTED_STATEFUL_RECIPES],
        )
        self.assertEqual(
            [record["path"] for record in reference_hashes],
            [entry[3] for entry in calibrate.EXPECTED_STATEFUL_RECIPES],
        )
        self.assertEqual(
            [record["frame_count"] for record in reference_hashes],
            [entry[9] for entry in calibrate.EXPECTED_STATEFUL_RECIPES],
        )
        portable_pcm = {
            stream["stem"]: stream["pcm"] for stream in portable_manifest["streams"]
        }
        for recipe, expected, reference in zip(
            stateful_manifest["recipes"],
            calibrate.EXPECTED_STATEFUL_RECIPES,
            reference_hashes,
        ):
            (
                recipe_id,
                source_stem,
                reference_kind,
                reference_path,
                reference_first_frame,
                duration_us,
                frame_bytes,
                samples_per_frame,
                output_action_count,
                valid_frame_count,
                expected_steps,
            ) = expected
            self.assertEqual(recipe["id"], recipe_id)
            self.assertEqual(recipe["source_stem"], source_stem)
            self.assertEqual(recipe["duration_us"], duration_us)
            self.assertEqual(recipe["frame_bytes"], frame_bytes)
            self.assertEqual(recipe["samples_per_frame"], samples_per_frame)
            self.assertEqual(recipe["output_action_count"], output_action_count)
            self.assertEqual(recipe["valid_frame_count"], valid_frame_count)
            self.assertEqual(
                [
                    (step["action"], step["first_sequence"], step["count"])
                    for step in recipe["steps"]
                ],
                list(expected_steps),
            )
            self.assertEqual(reference["kind"], reference_kind)
            self.assertEqual(reference["path"], reference_path)
            self.assertEqual(reference["first_frame"], reference_first_frame)
            if reference_kind == "portable-pcm":
                self.assertEqual(reference["size"], portable_pcm[source_stem]["size"])
            else:
                self.assertEqual(
                    reference["size"],
                    valid_frame_count * samples_per_frame * 2,
                )
        self.assertEqual(
            payload_identity["streams"],
            [
                {
                    "stem": entry[0],
                    "frame_count": 128,
                    "unique_frame_count": 128,
                }
                for entry in calibrate.EXPECTED_STREAMS
            ],
        )
        self.assertEqual(
            [
                entry["identical_frame_count"]
                for entry in payload_identity["same_duration_channel_overlaps"]
            ],
            [0, 0],
        )

    def test_modea_7p5ms_recipe_expansions_and_reference_are_exact(self):
        portable_manifest, _fixture_hashes, _manifest_sha256 = calibrate.load_manifest()
        _stateful_manifest, reference_hashes, _stateful_manifest_sha256 = (
            calibrate.load_stateful_manifest(portable_manifest)
        )
        left = calibrate.EXPECTED_STATEFUL_RECIPES[4]
        right = calibrate.EXPECTED_STATEFUL_RECIPES[5]

        def expand(steps):
            actions = []
            for action, first_sequence, count in steps:
                if action == "plc":
                    actions.extend((action, None) for _ in range(count))
                else:
                    actions.extend(
                        (action, first_sequence + offset) for offset in range(count)
                    )
            return actions

        self.assertEqual(left[0], "modea_start_7p5ms_l")
        self.assertEqual(right[0], "modea_start_7p5ms_r")
        self.assertEqual(
            expand(left[10]),
            [("plc", None)] * 12 + [("corpus", sequence) for sequence in range(101)],
        )
        self.assertEqual(
            expand(right[10]),
            [("plc", None)] * 10
            + [("corpus", 0)]
            + [("plc", None)] * 2
            + [("corpus", sequence) for sequence in range(1, 101)],
        )
        for recipe in (left, right):
            self.assertEqual(recipe[8], 113)
            self.assertEqual(recipe[9], 101)
            self.assertEqual(
                sum(count for action, _first, count in recipe[10] if action == "plc"),
                12,
            )
        self.assertEqual(right[2], "generated-pcm")
        self.assertEqual(right[3], "stateful_48k_7p5ms_modea_start_r.pcm")
        self.assertEqual(reference_hashes[5]["size"], 72720)
        self.assertEqual(
            reference_hashes[5]["sha256"],
            hashlib.sha256((FIXTURES_DIR / right[3]).read_bytes()).hexdigest(),
        )


class Lc3StatefulRecipeValidatorTests(unittest.TestCase):
    def test_validator_rejects_invalid_recipe_boundaries(self):
        harness_source = textwrap.dedent(
            """\
            #include <stdint.h>

            #include "lc3_stateful_recipes.h"

            #define REQUIRE(condition) \\
                do { \\
                    if (!(condition)) { \\
                        return __LINE__; \\
                    } \\
                } while (0)

            int main(void)
            {
                struct lc3_stateful_recipe recipe;
                struct lc3_stateful_step unknown_action_steps[] = {
                    {LC3_STATEFUL_ACTION_PLC, 0U, 8U},
                    {(enum lc3_stateful_action)99, 0U, 100U},
                };
                struct lc3_stateful_step zero_count_steps[] = {
                    {LC3_STATEFUL_ACTION_PLC, 0U, 8U},
                    {LC3_STATEFUL_ACTION_CORPUS, 0U, 0U},
                };
                struct lc3_stateful_step out_of_range_steps[] = {
                    {LC3_STATEFUL_ACTION_PLC, 0U, 8U},
                    {LC3_STATEFUL_ACTION_CORPUS, 127U, 2U},
                };
                struct lc3_stateful_step overflow_steps[] = {
                    {LC3_STATEFUL_ACTION_PLC, 0U, UINT16_MAX},
                    {LC3_STATEFUL_ACTION_PLC, 0U, 1U},
                };

                REQUIRE(lc3_stateful_recipes_validate(
                    lc3_stateful_recipes, lc3_stateful_recipe_count));
                REQUIRE(!lc3_stateful_recipes_validate(NULL, 1U));
                REQUIRE(!lc3_stateful_recipes_validate(lc3_stateful_recipes, 0U));

                recipe = lc3_stateful_recipes[0];
                recipe.id = NULL;
                REQUIRE(!lc3_stateful_recipes_validate(&recipe, 1U));

                recipe = lc3_stateful_recipes[0];
                recipe.source_stem = NULL;
                REQUIRE(!lc3_stateful_recipes_validate(&recipe, 1U));

                recipe = lc3_stateful_recipes[0];
                recipe.reference_path = NULL;
                REQUIRE(!lc3_stateful_recipes_validate(&recipe, 1U));

                recipe = lc3_stateful_recipes[0];
                recipe.steps = NULL;
                REQUIRE(!lc3_stateful_recipes_validate(&recipe, 1U));

                recipe = lc3_stateful_recipes[0];
                recipe.steps = unknown_action_steps;
                REQUIRE(!lc3_stateful_recipes_validate(&recipe, 1U));

                recipe = lc3_stateful_recipes[0];
                recipe.steps = zero_count_steps;
                REQUIRE(!lc3_stateful_recipes_validate(&recipe, 1U));

                recipe = lc3_stateful_recipes[0];
                recipe.steps = out_of_range_steps;
                REQUIRE(!lc3_stateful_recipes_validate(&recipe, 1U));

                recipe = lc3_stateful_recipes[0];
                recipe.steps = overflow_steps;
                recipe.output_action_count = 0U;
                recipe.valid_frame_count = 0U;
                REQUIRE(!lc3_stateful_recipes_validate(&recipe, 1U));

                recipe = lc3_stateful_recipes[0];
                recipe.source_stem = "not-a-corpus-source";
                REQUIRE(!lc3_stateful_recipes_validate(&recipe, 1U));

                recipe = lc3_stateful_recipes[0];
                recipe.output_action_count--;
                REQUIRE(!lc3_stateful_recipes_validate(&recipe, 1U));

                recipe = lc3_stateful_recipes[0];
                recipe.valid_frame_count--;
                REQUIRE(!lc3_stateful_recipes_validate(&recipe, 1U));

                return 0;
            }
            """
        )

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "stateful_recipe_validator.c"
            binary = root / "stateful_recipe_validator"
            harness.write_text(harness_source, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(SUPPORT_DIR),
                    str(harness),
                    str(SUPPORT_DIR / "lc3_stateful_recipes.c"),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)

            run_result = subprocess.run(
                [str(binary)], capture_output=True, text=True, timeout=30
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)


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

    def test_schema_three_report_contains_stateful_provenance_and_payload_identity(
        self,
    ):
        manifest, fixture_hashes, manifest_sha256 = calibrate.load_manifest()
        (
            stateful_manifest,
            stateful_reference_hashes,
            stateful_manifest_sha256,
        ) = calibrate.load_stateful_manifest(manifest)
        payload_identity = calibrate.validate_payload_identity(manifest)
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
            self.assertEqual(report["schema_version"], 3)
            self.assertEqual(report["pcm_limits"], limits)
            self.assertEqual(report["metrics"], metrics)
            self.assertEqual(report["manifest_sha256"], manifest_sha256)
            self.assertEqual(report["fixture_hashes"], fixture_hashes)
            self.assertEqual(
                report["stateful_manifest_sha256"], stateful_manifest_sha256
            )
            self.assertEqual(
                report["stateful_reference_hashes"], stateful_reference_hashes
            )
            self.assertEqual(report["payload_identity"], payload_identity)
            self.assertEqual(report["ncs_version"], manifest["ncs_version"])
            self.assertEqual(
                report["liblc3"]["semantic_label"],
                stateful_manifest["liblc3"]["semantic_label"],
            )
            run_mock.assert_called_once_with(["cc"], liblc3, limits)

    def test_metric_protocol_requires_exact_order_identity_and_evaluation(self):
        limits = manifest_limits()
        records = complete_metric_records()
        payload = "\n".join(json.dumps(record) for record in records)

        self.assertEqual(calibrate.parse_metric_records(payload, limits), records)

        cases = []
        wrong_count = records[:-1]
        cases.append(("record count", wrong_count, "returned 37 records"))

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

    def test_stateful_metric_protocol_is_exact(self):
        records = calibrate.expected_metric_records()

        self.assertEqual(len(records), 38)
        self.assertEqual(
            records[30:32],
            [
                (
                    "stateful-valid",
                    "modea_start_7p5ms_l",
                    "bsim_48k_7p5ms_90b_l.pcm",
                    101,
                    36360,
                    "pass",
                ),
                (
                    "stateful-valid",
                    "modea_start_7p5ms_r",
                    "stateful_48k_7p5ms_modea_start_r.pcm",
                    101,
                    36360,
                    "pass",
                ),
            ],
        )
        self.assertEqual(
            records[26:34],
            [
                (
                    "stateful-valid",
                    recipe_id,
                    reference_path,
                    valid_frame_count,
                    valid_frame_count * samples_per_frame,
                    "pass",
                )
                for (
                    recipe_id,
                    _source_stem,
                    _reference_kind,
                    reference_path,
                    _reference_first_frame,
                    _duration_us,
                    _frame_bytes,
                    samples_per_frame,
                    _output_action_count,
                    valid_frame_count,
                    _steps,
                ) in calibrate.EXPECTED_STATEFUL_RECIPES
            ],
        )
        self.assertEqual(records[34:], list(calibrate.STATEFUL_MUTATIONS))


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

    def test_stateful_unknown_generator_argument_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            result = run_generator(
                STATEFUL_GENERATOR,
                ["--not-a-generator-mode"],
                temp,
                {"NCS": str(Path(temp) / "missing-ncs")},
            )

            self.assertEqual(result.returncode, 2)
            self.assertIn("Usage:", result.stderr)

    def test_stateful_generator_requires_pinned_liblc3_git_checkout(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            script, _fixture_dir = copy_stateful_generator_repository(root / "repo")
            ncs = root / "ncs"
            write_fake_liblc3(ncs)

            non_git = run_generator(script, [], root, {"NCS": str(ncs)})
            self.assertNotEqual(non_git.returncode, 0)
            self.assertIn("cannot verify liblc3 Git revision", non_git.stderr)

            fake_git = write_pinned_fake_git(
                root / "fake-bin", liblc3_revision="0" * 40
            )
            wrong_revision = run_generator(
                script,
                [],
                root,
                {
                    "NCS": str(ncs),
                    "PATH": str(fake_git) + os.pathsep + os.environ["PATH"],
                },
            )
            self.assertNotEqual(wrong_revision.returncode, 0)
            self.assertIn("liblc3 Git revision is not", wrong_revision.stderr)

            dirty_git = write_pinned_fake_git(
                root / "dirty-fake-bin", liblc3_status="?? injected.c"
            )
            dirty_tree = run_generator(
                script,
                [],
                root,
                {
                    "NCS": str(ncs),
                    "PATH": str(dirty_git) + os.pathsep + os.environ["PATH"],
                },
            )
            self.assertNotEqual(dirty_tree.returncode, 0)
            self.assertIn("liblc3 Git working tree is dirty", dirty_tree.stderr)

    def test_stateful_strict_mode_rejects_changed_candidates_without_copy(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo = root / "repo"
            script, fixture_dir = copy_stateful_generator_repository(repo)
            modea_path = fixture_dir / "stateful_48k_7p5ms_modea_start_r.pcm"
            skip_path = fixture_dir / "stateful_48k_10ms_skip20_l.pcm"
            loss_path = fixture_dir / "stateful_48k_10ms_loss48x18_r.pcm"
            original_modea = modea_path.read_bytes()
            original_skip = skip_path.read_bytes()
            original_loss = loss_path.read_bytes()
            ncs = root / "ncs"
            fake_compiler = root / "fake-cc"

            write_fake_liblc3(ncs)
            write_fake_stateful_compiler(fake_compiler, "flip")
            fake_git = write_pinned_fake_git(root / "fake-bin")
            result = run_generator(
                script,
                [],
                root,
                {
                    "NCS": str(ncs),
                    "CC": str(fake_compiler),
                    "PATH": str(fake_git) + os.pathsep + os.environ["PATH"],
                },
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("fixture SHA-256 mismatch", result.stderr)
            self.assertEqual(modea_path.read_bytes(), original_modea)
            self.assertEqual(skip_path.read_bytes(), original_skip)
            self.assertEqual(loss_path.read_bytes(), original_loss)

    def test_stateful_strict_mode_verifies_all_generated_traces_without_copy(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo = root / "repo"
            script, fixture_dir = copy_stateful_generator_repository(repo)
            generated_paths = [
                fixture_dir / "stateful_48k_7p5ms_modea_start_r.pcm",
                fixture_dir / "stateful_48k_10ms_skip20_l.pcm",
                fixture_dir / "stateful_48k_10ms_loss48x18_r.pcm",
            ]
            originals = {path.name: path.read_bytes() for path in generated_paths}
            ncs = root / "ncs"
            fake_compiler = root / "fake-cc"

            write_fake_liblc3(ncs)
            write_fake_stateful_compiler(fake_compiler, "same")
            fake_git = write_pinned_fake_git(root / "fake-bin")
            result = run_generator(
                script,
                [],
                root,
                {
                    "NCS": str(ncs),
                    "CC": str(fake_compiler),
                    "PATH": str(fake_git) + os.pathsep + os.environ["PATH"],
                },
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "Stateful reference manifest hashes unchanged.", result.stdout
            )
            for path in generated_paths:
                self.assertEqual(path.read_bytes(), originals[path.name])

    def test_stateful_generator_cleans_binary_if_output_temp_creation_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            script, _fixture_dir = copy_stateful_generator_repository(root / "repo")
            ncs = root / "ncs"
            artifact = root / "leaked-stateful-generator"

            write_fake_liblc3(ncs)
            fake_bin = write_pinned_fake_git(root / "fake-bin")
            fake_mktemp = fake_bin / "mktemp"
            fake_mktemp.write_text(
                textwrap.dedent(
                    """\
                    #!/usr/bin/env bash
                    set -euo pipefail
                    if [ "${1:-}" = "-d" ]; then
                        exit 1
                    fi
                    : > "$STATEFUL_MKTEMP_ARTIFACT"
                    printf '%s\\n' "$STATEFUL_MKTEMP_ARTIFACT"
                    """
                ),
                encoding="utf-8",
            )
            fake_mktemp.chmod(0o755)

            result = run_generator(
                script,
                [],
                root,
                {
                    "NCS": str(ncs),
                    "PATH": str(fake_bin) + os.pathsep + os.environ["PATH"],
                    "STATEFUL_MKTEMP_ARTIFACT": str(artifact),
                },
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("temporary output directory creation failed", result.stderr)
            self.assertFalse(artifact.exists())

    def test_stateful_generator_rebase_keeps_source_and_checked_in_trace_protection(
        self,
    ):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            script, fixture_dir = copy_stateful_generator_repository(root / "repo")
            trace = fixture_dir / "stateful_48k_10ms_skip20_l.pcm"
            original = trace.read_bytes()
            damaged = bytes([original[0] ^ 0x01]) + original[1:]
            trace.write_bytes(damaged)

            for arguments in ([], ["--rebase-stateful"]):
                result = run_generator(
                    script,
                    arguments,
                    root,
                    {"NCS": str(root / "missing-ncs")},
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn("fixture SHA-256 mismatch", result.stderr)
                self.assertNotIn("liblc3 module not found", result.stderr)
                self.assertEqual(trace.read_bytes(), damaged)

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            script, fixture_dir = copy_stateful_generator_repository(root / "repo")
            source = fixture_dir / "bsim_48k_10ms_120b_l.lc3"
            original = source.read_bytes()
            damaged = bytes([original[0] ^ 0x01]) + original[1:]
            source.write_bytes(damaged)

            result = run_generator(
                script,
                ["--rebase-stateful"],
                root,
                {"NCS": str(root / "missing-ncs")},
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("fixture SHA-256 mismatch", result.stderr)
            self.assertNotIn("liblc3 module not found", result.stderr)
            self.assertEqual(source.read_bytes(), damaged)

    def test_stateful_rebase_rejects_invalid_candidate_without_partial_copy(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo = root / "repo"
            script, fixture_dir = copy_stateful_generator_repository(repo)
            original_modea = (
                fixture_dir / "stateful_48k_7p5ms_modea_start_r.pcm"
            ).read_bytes()
            original_skip = (
                fixture_dir / "stateful_48k_10ms_skip20_l.pcm"
            ).read_bytes()
            original_loss = (
                fixture_dir / "stateful_48k_10ms_loss48x18_r.pcm"
            ).read_bytes()
            ncs = root / "ncs"
            fake_compiler = root / "fake-cc"

            write_fake_liblc3(ncs)
            write_fake_stateful_compiler(fake_compiler, "truncate")
            fake_git = write_pinned_fake_git(root / "fake-bin")

            result = run_generator(
                script,
                ["--rebase-stateful"],
                root,
                {
                    "NCS": str(ncs),
                    "CC": str(fake_compiler),
                    "PATH": str(fake_git) + os.pathsep + os.environ["PATH"],
                },
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("fixture size mismatch", result.stderr)
            self.assertEqual(
                (fixture_dir / "stateful_48k_7p5ms_modea_start_r.pcm").read_bytes(),
                original_modea,
            )
            self.assertEqual(
                (fixture_dir / "stateful_48k_10ms_skip20_l.pcm").read_bytes(),
                original_skip,
            )
            self.assertEqual(
                (fixture_dir / "stateful_48k_10ms_loss48x18_r.pcm").read_bytes(),
                original_loss,
            )

    def test_stateful_rebase_rejects_missing_existing_generated_trace(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            script, fixture_dir = copy_stateful_generator_repository(root / "repo")
            skip_path = fixture_dir / "stateful_48k_10ms_skip20_l.pcm"

            skip_path.unlink()
            result = run_generator(
                script,
                ["--rebase-stateful"],
                root,
                {"NCS": str(root / "missing-ncs")},
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("fixture is missing", result.stderr)
            self.assertNotIn("liblc3 module not found", result.stderr)
            self.assertFalse(skip_path.exists())

    def test_stateful_rebase_replaces_only_valid_generated_candidates(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo = root / "repo"
            script, fixture_dir = copy_stateful_generator_repository(repo)
            modea_path = fixture_dir / "stateful_48k_7p5ms_modea_start_r.pcm"
            skip_path = fixture_dir / "stateful_48k_10ms_skip20_l.pcm"
            loss_path = fixture_dir / "stateful_48k_10ms_loss48x18_r.pcm"
            original_modea = modea_path.read_bytes()
            original_skip = skip_path.read_bytes()
            original_loss = loss_path.read_bytes()
            portable_manifest = (
                fixture_dir / "portable-oracle-manifest.json"
            ).read_bytes()
            stateful_manifest = (
                fixture_dir / "stateful-reference-manifest.json"
            ).read_bytes()
            ncs = root / "ncs"
            fake_compiler = root / "fake-cc"

            write_fake_liblc3(ncs)
            write_fake_stateful_compiler(fake_compiler, "flip")
            fake_git = write_pinned_fake_git(root / "fake-bin")
            result = run_generator(
                script,
                ["--rebase-stateful"],
                root,
                {
                    "NCS": str(ncs),
                    "CC": str(fake_compiler),
                    "PATH": str(fake_git) + os.pathsep + os.environ["PATH"],
                },
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("REBASE STATEFUL:", result.stderr)
            self.assertEqual(
                modea_path.read_bytes(),
                bytes([original_modea[0] ^ 0x01]) + original_modea[1:],
            )
            self.assertEqual(
                skip_path.read_bytes(),
                bytes([original_skip[0] ^ 0x01]) + original_skip[1:],
            )
            self.assertEqual(
                loss_path.read_bytes(),
                bytes([original_loss[0] ^ 0x01]) + original_loss[1:],
            )
            self.assertEqual(
                (fixture_dir / "portable-oracle-manifest.json").read_bytes(),
                portable_manifest,
            )
            self.assertEqual(
                (fixture_dir / "stateful-reference-manifest.json").read_bytes(),
                stateful_manifest,
            )

    def test_stateful_rebase_adds_missing_generated_trace_transactionally(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo = root / "repo"
            script, fixture_dir = copy_stateful_generator_repository(repo)
            modea_path = fixture_dir / "stateful_48k_7p5ms_modea_start_r.pcm"
            skip_path = fixture_dir / "stateful_48k_10ms_skip20_l.pcm"
            loss_path = fixture_dir / "stateful_48k_10ms_loss48x18_r.pcm"
            template = root / "modea-reference-template.pcm"
            original_modea = modea_path.read_bytes()
            original_skip = skip_path.read_bytes()
            original_loss = loss_path.read_bytes()
            portable_manifest = (
                fixture_dir / "portable-oracle-manifest.json"
            ).read_bytes()
            stateful_manifest = (
                fixture_dir / "stateful-reference-manifest.json"
            ).read_bytes()
            ncs = root / "ncs"
            fake_compiler = root / "fake-cc"

            template.write_bytes(original_modea)
            modea_path.unlink()
            skip_path.chmod(0o640)
            loss_path.chmod(0o600)
            write_fake_liblc3(ncs)
            write_fake_stateful_compiler(fake_compiler, "flip", template)
            fake_git = write_pinned_fake_git(root / "fake-bin")
            result = run_generator(
                script,
                ["--rebase-stateful"],
                root,
                {
                    "NCS": str(ncs),
                    "CC": str(fake_compiler),
                    "PATH": str(fake_git) + os.pathsep + os.environ["PATH"],
                },
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                modea_path.read_bytes(),
                bytes([original_modea[0] ^ 0x01]) + original_modea[1:],
            )
            self.assertEqual(modea_path.stat().st_mode & 0o777, 0o644)
            self.assertEqual(
                skip_path.read_bytes(),
                bytes([original_skip[0] ^ 0x01]) + original_skip[1:],
            )
            self.assertEqual(skip_path.stat().st_mode & 0o777, 0o640)
            self.assertEqual(
                loss_path.read_bytes(),
                bytes([original_loss[0] ^ 0x01]) + original_loss[1:],
            )
            self.assertEqual(loss_path.stat().st_mode & 0o777, 0o600)
            self.assertEqual(
                (fixture_dir / "portable-oracle-manifest.json").read_bytes(),
                portable_manifest,
            )
            self.assertEqual(
                (fixture_dir / "stateful-reference-manifest.json").read_bytes(),
                stateful_manifest,
            )

    def test_stateful_rebase_rolls_back_new_trace_after_later_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo = root / "repo"
            script, fixture_dir = copy_stateful_generator_repository(repo)
            modea_path = fixture_dir / "stateful_48k_7p5ms_modea_start_r.pcm"
            skip_path = fixture_dir / "stateful_48k_10ms_skip20_l.pcm"
            loss_path = fixture_dir / "stateful_48k_10ms_loss48x18_r.pcm"
            template = root / "modea-reference-template.pcm"
            original_modea = modea_path.read_bytes()
            original_skip = skip_path.read_bytes()
            original_loss = loss_path.read_bytes()
            portable_manifest = (
                fixture_dir / "portable-oracle-manifest.json"
            ).read_bytes()
            stateful_manifest = (
                fixture_dir / "stateful-reference-manifest.json"
            ).read_bytes()
            ncs = root / "ncs"
            fake_compiler = root / "fake-cc"
            transaction = script.read_text(encoding="utf-8")
            commit_loop = """\
        for name in names:
            os.replace(staged[name], destination_directory / name)
            committed.append(name)
"""
            injected_commit_loop = """\
        for name in names:
            os.replace(staged[name], destination_directory / name)
            committed.append(name)
            if name == "stateful_48k_7p5ms_modea_start_r.pcm":
                raise OSError("injected post-new-file failure")
"""

            self.assertIn(commit_loop, transaction)
            script.write_text(
                transaction.replace(commit_loop, injected_commit_loop), encoding="utf-8"
            )
            template.write_bytes(original_modea)
            modea_path.unlink()
            skip_path.chmod(0o640)
            loss_path.chmod(0o600)
            write_fake_liblc3(ncs)
            write_fake_stateful_compiler(fake_compiler, "flip", template)
            fake_git = write_pinned_fake_git(root / "fake-bin")
            result = run_generator(
                script,
                ["--rebase-stateful"],
                root,
                {
                    "NCS": str(ncs),
                    "CC": str(fake_compiler),
                    "PATH": str(fake_git) + os.pathsep + os.environ["PATH"],
                },
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("injected post-new-file failure", result.stderr)
            self.assertFalse(modea_path.exists())
            self.assertEqual(skip_path.read_bytes(), original_skip)
            self.assertEqual(skip_path.stat().st_mode & 0o777, 0o640)
            self.assertEqual(loss_path.read_bytes(), original_loss)
            self.assertEqual(loss_path.stat().st_mode & 0o777, 0o600)
            self.assertEqual(
                (fixture_dir / "portable-oracle-manifest.json").read_bytes(),
                portable_manifest,
            )
            self.assertEqual(
                (fixture_dir / "stateful-reference-manifest.json").read_bytes(),
                stateful_manifest,
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
