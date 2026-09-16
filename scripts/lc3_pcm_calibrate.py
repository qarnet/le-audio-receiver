#!/usr/bin/env python3
"""Capture host-specific diagnostic LC3/PCM comparator measurements.

This command does not select tolerance thresholds. It validates checked-in
fixture integrity before compiling a temporary host calibrator, then writes one
new atomic JSON evidence record outside the repository.
"""

import argparse
import datetime
import hashlib
import json
import os
import platform
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
FIXTURES_DIR = REPO_ROOT / "tests" / "fixtures" / "lc3"
MANIFEST_PATH = FIXTURES_DIR / "portable-oracle-manifest.json"
SUPPORT_DIR = REPO_ROOT / "tests" / "support"
CALIBRATOR_SOURCE = FIXTURES_DIR / "calibrate.c"
ORACLE_SOURCE = SUPPORT_DIR / "pcm_oracle.c"
ORACLE_HEADER = SUPPORT_DIR / "pcm_oracle.h"

EXPECTED_NCS_VERSION = "v3.3.0"
EXPECTED_LIBLC3_LABEL = "1.1.2"
EXPECTED_LIBLC3_REVISION = "48bbd3eacd36e99a57317a0a4867002e0b09e183"
EXPECTED_SOURCE_FORMULA = "bsim_tx_hash_mix_seq_i_ch_v1"
EXPECTED_FLAGS = (
    "-O3",
    "-std=c11",
    "-ffast-math",
    "-Wall",
    "-Wextra",
    "-Wdouble-promotion",
    "-Wvla",
    "-pedantic",
    "-Werror",
)
LC3_SOURCES = (
    "attdet.c",
    "bits.c",
    "bwdet.c",
    "energy.c",
    "lc3.c",
    "ltpf.c",
    "mdct.c",
    "plc.c",
    "sns.c",
    "spec.c",
    "tables.c",
    "tns.c",
)
EXPECTED_STREAMS = (
    ("bsim_48k_10ms_120b_l", 10000, 48000, "left", 120, 480),
    ("bsim_48k_10ms_120b_r", 10000, 48000, "right", 120, 480),
    ("bsim_48k_7p5ms_90b_l", 7500, 48000, "left", 90, 360),
    ("bsim_48k_7p5ms_90b_r", 7500, 48000, "right", 90, 360),
)
MAX_RAW_BYTES = 64 * 1024
MAX_REPORT_BYTES = 256 * 1024
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
GIT_HEAD_RE = re.compile(r"[0-9a-f]{40,64}\Z")
CALIBRATION_INPUTS = (
    ("scripts/lc3_pcm_calibrate.py", REPO_ROOT / "scripts" / "lc3_pcm_calibrate.py"),
    ("tests/fixtures/lc3/calibrate.c", CALIBRATOR_SOURCE),
    ("tests/support/pcm_oracle.c", ORACLE_SOURCE),
    ("tests/support/pcm_oracle.h", ORACLE_HEADER),
)


class CalibrationError(RuntimeError):
    """Expected public command failure with no final output file."""


def _require_keys(value, expected, label):
    if not isinstance(value, dict):
        raise CalibrationError("%s must be an object" % label)
    actual = set(value)
    unknown = sorted(actual - set(expected))
    missing = sorted(set(expected) - actual)
    if unknown:
        raise CalibrationError(
            "%s has unknown fields: %s" % (label, ", ".join(unknown))
        )
    if missing:
        raise CalibrationError("%s is missing fields: %s" % (label, ", ".join(missing)))


def _require_string(value, label):
    if not isinstance(value, str) or not value:
        raise CalibrationError("%s must be a non-empty string" % label)
    return value


def _require_int(value, label, lower=None, upper=None):
    if type(value) is not int:
        raise CalibrationError("%s must be an integer" % label)
    if lower is not None and value < lower:
        raise CalibrationError("%s is below range" % label)
    if upper is not None and value > upper:
        raise CalibrationError("%s is above range" % label)
    return value


def _validate_pcm_limits(value):
    _require_keys(
        value,
        ("max_abs_error", "max_rms_error", "min_correlation_q15"),
        "pcm_limits",
    )
    return {
        "max_abs_error": _require_int(
            value["max_abs_error"], "pcm_limits.max_abs_error", 0, 65535
        ),
        "max_rms_error": _require_int(
            value["max_rms_error"], "pcm_limits.max_rms_error", 0, 65535
        ),
        "min_correlation_q15": _require_int(
            value["min_correlation_q15"],
            "pcm_limits.min_correlation_q15",
            -32768,
            32767,
        ),
    }


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def format_utc_timestamp(captured_at):
    if (
        not isinstance(captured_at, datetime.datetime)
        or captured_at.utcoffset() is None
    ):
        raise CalibrationError("capture timestamp must be timezone-aware")
    return (
        captured_at.astimezone(datetime.timezone.utc)
        .isoformat(timespec="microseconds")
        .replace("+00:00", "Z")
    )


def capture_utc_timestamp():
    return format_utc_timestamp(datetime.datetime.now(datetime.timezone.utc))


def calibration_input_records():
    records = []
    for relative_path, source in CALIBRATION_INPUTS:
        if not source.is_file():
            raise CalibrationError("calibration input missing: %s" % relative_path)
        try:
            records.append(
                {
                    "path": relative_path,
                    "size": source.stat().st_size,
                    "sha256": _sha256(source),
                }
            )
        except OSError as exc:
            raise CalibrationError(
                "cannot hash calibration input %s: %s" % (relative_path, exc)
            ) from exc
    return records


def _validate_binary(entry, label, stem, suffix, expected_size):
    _require_keys(entry, ("path", "size", "sha256"), label)
    filename = "%s%s" % (stem, suffix)
    declared_path = _require_string(entry["path"], label + ".path")
    if declared_path != filename:
        raise CalibrationError("%s.path must be %s" % (label, filename))
    declared_size = _require_int(entry["size"], label + ".size", 1)
    if declared_size != expected_size:
        raise CalibrationError("%s.size does not match corpus geometry" % label)
    declared_hash = _require_string(entry["sha256"], label + ".sha256")
    if SHA256_RE.fullmatch(declared_hash) is None:
        raise CalibrationError("%s.sha256 must be lowercase SHA-256" % label)

    path = (FIXTURES_DIR / declared_path).resolve(strict=False)
    if path.parent != FIXTURES_DIR.resolve() or not path.is_file():
        raise CalibrationError(
            "%s fixture is missing or escapes fixture directory" % label
        )
    actual_size = path.stat().st_size
    if actual_size != declared_size:
        raise CalibrationError("%s fixture size mismatch" % label)
    actual_hash = _sha256(path)
    if actual_hash != declared_hash:
        raise CalibrationError("%s fixture SHA-256 mismatch" % label)
    return {"path": declared_path, "size": actual_size, "sha256": actual_hash}


def load_manifest():
    try:
        raw = MANIFEST_PATH.read_bytes()
    except OSError as exc:
        raise CalibrationError("cannot read manifest: %s" % exc) from exc
    try:
        manifest = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CalibrationError("malformed manifest: %s" % exc) from exc

    _require_keys(
        manifest,
        (
            "schema_version",
            "ncs_version",
            "liblc3",
            "generator_flags",
            "source_formula_identifier",
            "corpus_frame_count",
            "pcm_limits",
            "streams",
        ),
        "manifest",
    )
    if _require_int(manifest["schema_version"], "schema_version") != 2:
        raise CalibrationError("unsupported manifest schema_version")
    if _require_string(manifest["ncs_version"], "ncs_version") != EXPECTED_NCS_VERSION:
        raise CalibrationError("manifest NCS version is not %s" % EXPECTED_NCS_VERSION)
    _require_keys(manifest["liblc3"], ("semantic_label", "west_revision"), "liblc3")
    if (
        _require_string(manifest["liblc3"]["semantic_label"], "liblc3.semantic_label")
        != EXPECTED_LIBLC3_LABEL
    ):
        raise CalibrationError(
            "manifest liblc3 semantic label is not %s" % EXPECTED_LIBLC3_LABEL
        )
    if (
        _require_string(manifest["liblc3"]["west_revision"], "liblc3.west_revision")
        != EXPECTED_LIBLC3_REVISION
    ):
        raise CalibrationError("manifest liblc3 west revision is not pinned revision")

    flags = manifest["generator_flags"]
    if not isinstance(flags, list) or tuple(flags) != EXPECTED_FLAGS:
        raise CalibrationError(
            "manifest generator_flags do not match exact generator flags"
        )
    if (
        _require_string(
            manifest["source_formula_identifier"], "source_formula_identifier"
        )
        != EXPECTED_SOURCE_FORMULA
    ):
        raise CalibrationError("manifest source formula identifier is unknown")
    if _require_int(manifest["corpus_frame_count"], "corpus_frame_count", 1) != 128:
        raise CalibrationError("manifest corpus frame count is not 128")
    pcm_limits = _validate_pcm_limits(manifest["pcm_limits"])
    manifest["pcm_limits"] = pcm_limits

    streams = manifest["streams"]
    if not isinstance(streams, list) or len(streams) != len(EXPECTED_STREAMS):
        raise CalibrationError("manifest must contain exactly four corpus streams")

    fixture_hashes = []
    for index, (entry, expected) in enumerate(zip(streams, EXPECTED_STREAMS)):
        stem, duration_us, frequency_hz, channel, frame_bytes, samples_per_frame = (
            expected
        )
        label = "streams[%d]" % index
        _require_keys(
            entry,
            (
                "stem",
                "duration_us",
                "frequency_hz",
                "channel",
                "frame_bytes",
                "samples_per_frame",
                "frame_count",
                "lc3",
                "pcm",
            ),
            label,
        )
        if _require_string(entry["stem"], label + ".stem") != stem:
            raise CalibrationError(
                "%s stem has unexpected corpus order or value" % label
            )
        if _require_int(entry["duration_us"], label + ".duration_us") != duration_us:
            raise CalibrationError("%s duration does not match corpus geometry" % label)
        if _require_int(entry["frequency_hz"], label + ".frequency_hz") != frequency_hz:
            raise CalibrationError(
                "%s frequency does not match corpus geometry" % label
            )
        if _require_string(entry["channel"], label + ".channel") != channel:
            raise CalibrationError("%s channel does not match corpus geometry" % label)
        if _require_int(entry["frame_bytes"], label + ".frame_bytes") != frame_bytes:
            raise CalibrationError(
                "%s frame bytes do not match corpus geometry" % label
            )
        if (
            _require_int(entry["samples_per_frame"], label + ".samples_per_frame")
            != samples_per_frame
        ):
            raise CalibrationError(
                "%s samples per frame do not match corpus geometry" % label
            )
        if _require_int(entry["frame_count"], label + ".frame_count") != 128:
            raise CalibrationError(
                "%s frame count does not match corpus geometry" % label
            )
        fixture_hashes.append(
            _validate_binary(
                entry["lc3"], label + ".lc3", stem, ".lc3", frame_bytes * 128
            )
        )
        fixture_hashes.append(
            _validate_binary(
                entry["pcm"], label + ".pcm", stem, ".pcm", samples_per_frame * 128 * 2
            )
        )

    return manifest, fixture_hashes, hashlib.sha256(raw).hexdigest()


def validate_output(value):
    if not os.path.isabs(value):
        raise CalibrationError("--output must be an absolute path")
    requested = Path(os.path.abspath(os.path.normpath(value)))
    if os.path.lexists(str(requested)):
        raise CalibrationError("--output already exists: %s" % requested)
    try:
        requested.relative_to(REPO_ROOT)
    except ValueError:
        pass
    else:
        raise CalibrationError("--output must not be inside repository: %s" % requested)

    output = requested.resolve(strict=False)
    if os.path.lexists(str(output)):
        raise CalibrationError("--output already exists: %s" % output)
    try:
        output.relative_to(REPO_ROOT)
    except ValueError:
        pass
    else:
        raise CalibrationError("--output must not be inside repository: %s" % output)
    if not output.parent.is_dir():
        raise CalibrationError(
            "--output parent directory does not exist: %s" % output.parent
        )
    return output


def resolve_ncs(manifest):
    configured = os.environ.get("NCS")
    ncs = (
        Path(configured).expanduser() if configured else Path.home() / "ncs" / "v3.3.0"
    )
    ncs = ncs.resolve(strict=False)
    liblc3 = ncs / "modules" / "lib" / "liblc3"
    if not (liblc3 / "include" / "lc3.h").is_file():
        raise CalibrationError("liblc3 module not found at %s" % liblc3)
    source_dir = liblc3 / "src"
    for source in LC3_SOURCES:
        if not (source_dir / source).is_file():
            raise CalibrationError("liblc3 source missing: %s" % (source_dir / source))

    git_marker = liblc3 / ".git"
    revision = None
    if git_marker.exists() or git_marker.is_symlink():
        try:
            result = subprocess.run(
                ["git", "-C", str(liblc3), "rev-parse", "HEAD"],
                check=False,
                capture_output=True,
                text=True,
            )
        except OSError as exc:
            raise CalibrationError(
                "cannot verify liblc3 Git revision: %s" % exc
            ) from exc
        if result.returncode != 0:
            raise CalibrationError(
                "cannot verify liblc3 Git revision: %s" % result.stderr.strip()
            )
        revision = result.stdout.strip()
        if revision != manifest["liblc3"]["west_revision"]:
            raise CalibrationError("liblc3 Git revision does not match manifest")
    return ncs, liblc3, revision


def _capture(command, label):
    try:
        result = subprocess.run(command, check=False, capture_output=True)
    except OSError as exc:
        raise CalibrationError("cannot run %s: %s" % (label, exc)) from exc
    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", "replace").strip()
        raise CalibrationError("%s failed: %s" % (label, stderr))
    if len(result.stdout) > MAX_RAW_BYTES:
        raise CalibrationError("%s output exceeds bounded evidence limit" % label)
    return result.stdout.decode("utf-8", "replace")


def repository_provenance(head_raw, status_porcelain_v1_raw):
    if not isinstance(head_raw, str):
        raise CalibrationError("repository HEAD must be text")
    head = head_raw.strip()
    if GIT_HEAD_RE.fullmatch(head) is None:
        raise CalibrationError("repository HEAD is not a Git object ID")
    if not isinstance(status_porcelain_v1_raw, str):
        raise CalibrationError("repository status must be text")
    if len(status_porcelain_v1_raw.encode("utf-8")) > MAX_RAW_BYTES:
        raise CalibrationError("repository status exceeds bounded evidence limit")
    return {
        "head": head,
        "status_porcelain_v1_raw": status_porcelain_v1_raw,
    }


def capture_repository_provenance():
    return repository_provenance(
        _capture(["git", "-C", str(REPO_ROOT), "rev-parse", "HEAD"], "repository HEAD"),
        _capture(
            ["git", "-C", str(REPO_ROOT), "status", "--porcelain=v1"],
            "repository status",
        ),
    )


def compiler_argv():
    try:
        command = shlex.split(os.environ.get("CC", "cc"))
    except ValueError as exc:
        raise CalibrationError("invalid CC setting: %s" % exc) from exc
    if not command:
        raise CalibrationError("CC must name a compiler")
    return command


def run_calibrator(compiler, liblc3, pcm_limits):
    for source in (CALIBRATOR_SOURCE, ORACLE_SOURCE):
        if not source.is_file():
            raise CalibrationError("calibration source missing: %s" % source)

    with tempfile.TemporaryDirectory(prefix="lc3-pcm-calibrate-") as temp_dir:
        binary = Path(temp_dir) / "calibrate"
        command = (
            compiler
            + list(EXPECTED_FLAGS)
            + [
                "-I",
                str(liblc3 / "include"),
                "-I",
                str(SUPPORT_DIR),
                str(CALIBRATOR_SOURCE),
                str(ORACLE_SOURCE),
            ]
            + [str(liblc3 / "src" / source) for source in LC3_SOURCES]
            + ["-lm", "-o", str(binary)]
        )
        try:
            compiled = subprocess.run(command, check=False, capture_output=True)
        except OSError as exc:
            raise CalibrationError("cannot run compiler: %s" % exc) from exc
        diagnostics = compiled.stderr.decode("utf-8", "replace")
        if compiled.returncode != 0:
            raise CalibrationError(
                "calibration compile failed:\n%s" % diagnostics.rstrip()
            )
        if diagnostics:
            raise CalibrationError(
                "calibration compile emitted diagnostics:\n%s" % diagnostics.rstrip()
            )

        try:
            ran = subprocess.run(
                [
                    str(binary),
                    str(FIXTURES_DIR),
                    str(pcm_limits["max_abs_error"]),
                    str(pcm_limits["max_rms_error"]),
                    str(pcm_limits["min_correlation_q15"]),
                ],
                check=False,
                capture_output=True,
            )
        except OSError as exc:
            raise CalibrationError("cannot run calibrator: %s" % exc) from exc
        if ran.returncode != 0:
            raise CalibrationError(
                "calibrator failed:\n%s"
                % ran.stderr.decode("utf-8", "replace").rstrip()
            )
        if ran.stderr:
            raise CalibrationError(
                "calibrator emitted stderr:\n%s"
                % ran.stderr.decode("utf-8", "replace").rstrip()
            )
        if len(ran.stdout) > MAX_RAW_BYTES:
            raise CalibrationError(
                "calibrator metric output exceeds bounded evidence limit"
            )
        return command, parse_metric_records(
            ran.stdout.decode("utf-8", "strict"), pcm_limits
        )


def expected_metric_records():
    expected = []
    geometry = {stream[0]: stream for stream in EXPECTED_STREAMS}
    for stem, _duration, _frequency, _channel, _bytes, samples in EXPECTED_STREAMS:
        expected.append(("valid", stem, stem, 128, samples * 128, "pass"))
    for left, right in (
        ("bsim_48k_10ms_120b_l", "bsim_48k_10ms_120b_r"),
        ("bsim_48k_7p5ms_90b_l", "bsim_48k_7p5ms_90b_r"),
    ):
        expected.append(
            ("channel-swap", left, right, 128, geometry[left][5] * 128, "max-error")
        )
    for stem, _duration, _frequency, _channel, _bytes, samples in EXPECTED_STREAMS:
        expected.extend(
            (
                ("prior-frame-shift", stem, stem, 127, samples * 127, "max-error"),
                ("next-frame-shift", stem, stem, 127, samples * 127, "max-error"),
                ("dead-channel", stem, stem, 128, samples * 128, "max-error"),
                (
                    "low-correlation-synthetic",
                    stem,
                    stem,
                    128,
                    samples * 128,
                    "max-error",
                ),
            )
        )
    control_stem = EXPECTED_STREAMS[0][0]
    control_samples = EXPECTED_STREAMS[0][5]
    expected.extend(
        (
            (
                "lc3-byte-corruption",
                control_stem,
                control_stem,
                128,
                control_samples * 128,
                "max-error",
            ),
            (
                "max-error-boundary",
                control_stem,
                control_stem,
                1,
                control_samples,
                "max-error",
            ),
            (
                "rms-error-boundary",
                control_stem,
                control_stem,
                1,
                control_samples,
                "rms-error",
            ),
            (
                "correlation-boundary",
                control_stem,
                control_stem,
                1,
                control_samples,
                "correlation",
            ),
        )
    )
    return expected


def evaluate_metric_record(record, pcm_limits):
    if record["max_abs_error"] > pcm_limits["max_abs_error"]:
        return "max-error"
    if record["rms_error"] > pcm_limits["max_rms_error"]:
        return "rms-error"
    if (
        record["actual_energy_scaled"] == 0
        or record["reference_energy_scaled"] == 0
        or record["correlation_q15"] < pcm_limits["min_correlation_q15"]
    ):
        return "correlation"
    return "pass"


def parse_metric_records(text, pcm_limits):
    expected = expected_metric_records()

    lines = text.splitlines()
    if len(lines) != len(expected):
        raise CalibrationError(
            "calibrator returned %d records, expected %d" % (len(lines), len(expected))
        )
    records = []
    fields = (
        "record",
        "comparison",
        "stem",
        "reference_stem",
        "squared_error",
        "actual_energy_scaled",
        "reference_energy_scaled",
        "dot_product_scaled",
        "samples",
        "frames",
        "max_abs_error",
        "rms_error",
        "correlation_q15",
        "evaluation",
    )
    for index, (line, expectation) in enumerate(zip(lines, expected)):
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            raise CalibrationError(
                "malformed calibrator metric record %d: %s" % (index, exc)
            ) from exc
        _require_keys(record, fields, "metric[%d]" % index)
        comparison, stem, reference_stem, frames, samples, evaluation = expectation
        if record["record"] != "metric":
            raise CalibrationError("metric[%d] record marker is invalid" % index)
        if record["comparison"] != comparison or record["stem"] != stem:
            raise CalibrationError("metric[%d] comparison identity is invalid" % index)
        if record["reference_stem"] != reference_stem:
            raise CalibrationError("metric[%d] reference identity is invalid" % index)
        if (
            _require_string(record["evaluation"], "metric[%d].evaluation" % index)
            != evaluation
        ):
            raise CalibrationError("metric[%d] evaluation is invalid" % index)
        for name in (
            "squared_error",
            "actual_energy_scaled",
            "reference_energy_scaled",
        ):
            _require_int(
                record[name], "metric[%d].%s" % (index, name), 0, (1 << 64) - 1
            )
        _require_int(
            record["dot_product_scaled"],
            "metric[%d].dot_product_scaled" % index,
            -(1 << 63),
            (1 << 63) - 1,
        )
        for name in ("samples", "frames", "max_abs_error", "rms_error"):
            _require_int(
                record[name], "metric[%d].%s" % (index, name), 0, (1 << 32) - 1
            )
        _require_int(
            record["correlation_q15"],
            "metric[%d].correlation_q15" % index,
            -(1 << 31),
            (1 << 31) - 1,
        )
        if record["frames"] != frames or record["samples"] != samples:
            raise CalibrationError("metric[%d] dimensions are invalid" % index)
        observed_evaluation = evaluate_metric_record(record, pcm_limits)
        if record["evaluation"] != observed_evaluation:
            raise CalibrationError(
                "metric[%d] evaluation does not match policy" % index
            )
        records.append(record)
    return records


def write_new_atomic(path, payload):
    temp_name = None
    try:
        descriptor, temp_name = tempfile.mkstemp(
            prefix=".%s." % path.name, dir=str(path.parent)
        )
        with os.fdopen(descriptor, "wb") as destination:
            destination.write(payload)
            destination.flush()
            os.fsync(destination.fileno())
            os.fchmod(destination.fileno(), 0o644)
        try:
            os.link(temp_name, path)
        except FileExistsError as exc:
            raise CalibrationError("--output already exists: %s" % path) from exc
    except OSError as exc:
        raise CalibrationError("atomic output write failed: %s" % exc) from exc
    finally:
        if temp_name is not None:
            try:
                os.unlink(temp_name)
            except FileNotFoundError:
                pass
            except OSError:
                pass


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output", required=True, help="absolute new JSON evidence path"
    )
    args = parser.parse_args(argv)

    try:
        output = validate_output(args.output)
        manifest, fixture_hashes, manifest_sha256 = load_manifest()
        pcm_limits = manifest["pcm_limits"]
        ncs, liblc3, liblc3_revision = resolve_ncs(manifest)
        compiler = compiler_argv()
        captured_at_utc = capture_utc_timestamp()
        repository = capture_repository_provenance()
        calibration_inputs = calibration_input_records()
        lscpu_raw = _capture(["lscpu"], "lscpu")
        uname_raw = _capture(["uname", "-a"], "uname")
        compiler_version_raw = _capture(compiler + ["--version"], "compiler version")
        compile_command, metrics = run_calibrator(compiler, liblc3, pcm_limits)
        report = {
            "schema_version": 2,
            "captured_at_utc": captured_at_utc,
            "repository": repository,
            "ncs_version": manifest["ncs_version"],
            "ncs_path": str(ncs),
            "architecture": platform.machine(),
            "lscpu_raw": lscpu_raw,
            "uname_raw": uname_raw,
            "compiler": {"command": compiler, "version_raw": compiler_version_raw},
            "liblc3": {
                "semantic_label": manifest["liblc3"]["semantic_label"],
                "west_revision": manifest["liblc3"]["west_revision"],
                "observed_git_revision": liblc3_revision,
                "path": str(liblc3),
            },
            "generator_flags": list(EXPECTED_FLAGS),
            "source_formula_identifier": manifest["source_formula_identifier"],
            "corpus_frame_count": manifest["corpus_frame_count"],
            "pcm_limits": pcm_limits,
            "manifest_sha256": manifest_sha256,
            "fixture_hashes": fixture_hashes,
            "calibration_inputs": calibration_inputs,
            "compile_command": compile_command,
            "metrics": metrics,
        }
        payload = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode("utf-8")
        if len(payload) > MAX_REPORT_BYTES:
            raise CalibrationError("calibration report exceeds bounded evidence limit")
        write_new_atomic(output, payload)
    except CalibrationError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 1
    except OSError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 1

    print("Wrote calibration evidence: %s" % output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
