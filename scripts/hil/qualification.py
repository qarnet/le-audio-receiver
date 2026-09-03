"""Strict external capture-qualification schema for MA1 and SA1.

Qualification is human-owned evidence. This module only reads and validates
accepted records. It never writes limits, accepts a fixture, or changes mixer
state. Every referenced file must remain a regular, canonical path outside the
repository and every recorded hash is rechecked before a capture run starts.
"""

import hashlib
import json
import math
import os
import re
import stat
from dataclasses import dataclass
from datetime import datetime, timezone
from types import MappingProxyType

from hil import model


QUALIFICATION_SCHEMA_VERSION = 1
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
UTC_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$")

ROOT_KEYS = frozenset(
    {
        "schema_version",
        "status",
        "capability",
        "fixture_id",
        "capture_identity",
        "fixture_metadata_sha256",
        "qualification_runs",
        "limits",
        "accepted_by",
        "accepted_at_utc",
    }
)
RUN_KEYS = frozenset(
    {
        "duration_seconds",
        "wav_path",
        "wav_sha256",
        "analyzer_result_path",
        "analyzer_result_sha256",
        "timestamp_utc",
        "outcome",
    }
)
LIMIT_KEYS = frozenset({"min", "max"})

# These names are public capture-result metric names. Numeric values belong
# only in accepted external qualifications or synthetic-only test fixtures.
MONO_LIMIT_METRICS = (
    "sample_rate_hz",
    "channel_count",
    "sample_width_bits",
    "frame_count",
    "duration_seconds",
    "scored_start_sample",
    "scored_end_sample",
    "scored_duration_seconds",
    "preamble_correlation",
    "preamble_unique_peak_margin",
    "expected_carrier_power",
    "unexpected_carrier_power",
    "envelope_correlation",
    "lag_samples",
    "lag_drift_samples",
    "peak_normalized",
    "full_scale_count",
    "clipping_count",
    "noise_floor",
    "dc_offset",
    "moving_rms_min",
    "low_energy_window_count",
    "longest_low_energy_run",
    "repeat_variation",
)
STEREO_CHANNEL_METRICS = tuple(
    "%s_%s" % (channel, name)
    for channel in ("left", "right")
    for name in (
        "expected_carrier_power",
        "unexpected_carrier_power",
        "envelope_correlation",
        "lag_samples",
        "lag_drift_samples",
        "peak_normalized",
        "full_scale_count",
        "clipping_count",
        "noise_floor",
        "dc_offset",
        "moving_rms_min",
        "low_energy_window_count",
        "longest_low_energy_run",
        "repeat_variation",
        "continuity",
    )
)
STEREO_LIMIT_METRICS = (
    MONO_LIMIT_METRICS
    + STEREO_CHANNEL_METRICS
    + (
        "channel_map_margin",
        "opposite_channel_leakage",
        "duplication_correlation",
        "level_mismatch",
    )
)

EXACT_RANGE_METRICS = frozenset(
    {
        "sample_rate_hz",
        "channel_count",
        "sample_width_bits",
        "frame_count",
        "duration_seconds",
        "scored_start_sample",
        "scored_end_sample",
        "scored_duration_seconds",
    }
)
MINIMUM_METRICS = frozenset(
    {
        "preamble_correlation",
        "preamble_unique_peak_margin",
        "expected_carrier_power",
        "envelope_correlation",
        "moving_rms_min",
        "channel_map_margin",
    }
    | {
        "%s_%s" % (channel, name)
        for channel in ("left", "right")
        for name in ("expected_carrier_power", "envelope_correlation", "moving_rms_min")
    }
)
MAXIMUM_METRICS = frozenset(
    set(MONO_LIMIT_METRICS + STEREO_LIMIT_METRICS)
    - EXACT_RANGE_METRICS
    - MINIMUM_METRICS
)


class QualificationError(ValueError):
    """Raised when qualification is missing, unsafe, stale, or unaccepted."""


class _DuplicateKeyError(ValueError):
    def __init__(self, key):
        super().__init__(key)
        self.key = key


@dataclass(frozen=True)
class MetricLimit:
    """One qualified numeric lower/upper bound."""

    minimum: object
    maximum: object

    def as_dict(self):
        out = {}
        if self.minimum is not None:
            out["min"] = self.minimum
        if self.maximum is not None:
            out["max"] = self.maximum
        return out


@dataclass(frozen=True)
class QualificationRun:
    """Verified, immutable qualification evidence item."""

    duration_seconds: int
    wav_path: str
    wav_sha256: str
    analyzer_result_path: str
    analyzer_result_sha256: str
    timestamp_utc: str
    outcome: str


@dataclass(frozen=True)
class CaptureQualification:
    """Validated accepted qualification bound to one capture fixture."""

    path: str
    sha256: str
    capability: model.CaptureCapability
    fixture_id: str
    capture_identity: MappingProxyType
    fixture_metadata_sha256: str
    qualification_runs: tuple
    limits: MappingProxyType
    accepted_by: str
    accepted_at_utc: str


def metric_names(capability):
    if capability is model.CaptureCapability.MONO:
        return MONO_LIMIT_METRICS
    if capability is model.CaptureCapability.STEREO:
        return STEREO_LIMIT_METRICS
    raise QualificationError("capture qualification requires mono or stereo capability")


def capture_identity(capture_binding):
    """Canonical binding identity used in qualification and evidence."""
    if not isinstance(capture_binding, model.CaptureBinding):
        raise QualificationError("capture binding is required")
    return {
        "backend": capture_binding.backend,
        "device": capture_binding.device,
        "channels": capture_binding.channels,
        "sample_rate": capture_binding.sample_rate,
        "sample_format": capture_binding.sample_format,
        "udev": dict(capture_binding.udev.values),
        "mixer": {
            "control": capture_binding.mixer.control,
            "volume": capture_binding.mixer.volume,
            "capture_switch": capture_binding.mixer.capture_switch,
            "agc_control": capture_binding.mixer.agc_control,
            "agc": capture_binding.mixer.agc,
        },
    }


def _duplicate_rejecting_object(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise _DuplicateKeyError(key)
        out[key] = value
    return out


def _reject_constant(value):
    raise ValueError("non-finite JSON value %s" % value)


def _canonical_path(path, label):
    if not isinstance(path, str) or not path or not os.path.isabs(path):
        raise QualificationError("%s must be an absolute path" % label)
    if os.path.islink(path):
        raise QualificationError("%s must not be a symlink: %s" % (label, path))
    canonical = os.path.realpath(path)
    if canonical != os.path.normpath(path):
        raise QualificationError("%s must be canonical: %s" % (label, path))
    repo_root = os.path.realpath(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    )
    if canonical == repo_root or canonical.startswith(repo_root + os.sep):
        raise QualificationError("%s must be outside repository: %s" % (label, path))
    try:
        mode = os.stat(canonical).st_mode
    except OSError as exc:
        raise QualificationError(
            "cannot inspect %s %s: %s" % (label, path, exc)
        ) from None
    if not stat.S_ISREG(mode):
        raise QualificationError("%s must be a regular file: %s" % (label, path))
    return canonical


def _read_regular(path, label):
    canonical = _canonical_path(path, label)
    try:
        with open(canonical, "rb") as fh:
            payload = fh.read()
    except OSError as exc:
        raise QualificationError("cannot read %s %s: %s" % (label, path, exc)) from None
    return canonical, payload


def _sha256(payload):
    return hashlib.sha256(payload).hexdigest()


def _require_text(obj, key, where):
    value = obj.get(key)
    if not isinstance(value, str) or not value:
        raise QualificationError("%s must be nonempty text in %s" % (key, where))
    return value


def _require_exact_keys(obj, allowed, where):
    if not isinstance(obj, dict):
        raise QualificationError("%s must be an object" % where)
    unknown = set(obj) - set(allowed)
    missing = set(allowed) - set(obj)
    if unknown:
        raise QualificationError("unknown key %r in %s" % (sorted(unknown)[0], where))
    if missing:
        raise QualificationError("missing key %r in %s" % (sorted(missing)[0], where))


def _is_number(value):
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(value)
    )


def _canonical_utc(value, where):
    if not isinstance(value, str) or not UTC_RE.fullmatch(value):
        raise QualificationError(
            "%s must be canonical UTC YYYY-MM-DDTHH:MM:SSZ" % where
        )
    try:
        parsed = datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(
            tzinfo=timezone.utc
        )
    except ValueError as exc:
        raise QualificationError("%s is not a valid UTC timestamp" % where) from exc
    if parsed.strftime("%Y-%m-%dT%H:%M:%SZ") != value:
        raise QualificationError("%s must be canonical UTC" % where)
    return value


def _require_sha256(value, where):
    if not isinstance(value, str) or not SHA256_RE.fullmatch(value):
        raise QualificationError("%s must be a lowercase SHA-256" % where)
    return value


def _parse_run(item, index):
    where = "qualification_runs[%d]" % index
    _require_exact_keys(item, RUN_KEYS, where)
    duration = item["duration_seconds"]
    if not isinstance(duration, int) or isinstance(duration, bool) or duration != 130:
        raise QualificationError("%s duration_seconds must be exactly 130" % where)
    wav_path, wav_payload = _read_regular(item["wav_path"], where + ".wav_path")
    wav_sha = _require_sha256(item["wav_sha256"], where + ".wav_sha256")
    if _sha256(wav_payload) != wav_sha:
        raise QualificationError("%s WAV SHA-256 mismatch" % where)
    analyzer_path, analyzer_payload = _read_regular(
        item["analyzer_result_path"], where + ".analyzer_result_path"
    )
    analyzer_sha = _require_sha256(
        item["analyzer_result_sha256"], where + ".analyzer_result_sha256"
    )
    if _sha256(analyzer_payload) != analyzer_sha:
        raise QualificationError("%s analyzer result SHA-256 mismatch" % where)
    try:
        analyzer = json.loads(analyzer_payload.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as exc:
        raise QualificationError("%s analyzer result is invalid JSON" % where) from exc
    if not isinstance(analyzer, dict) or analyzer.get("outcome") != "passed":
        raise QualificationError("%s analyzer result is not passed" % where)
    timestamp = _canonical_utc(item["timestamp_utc"], where + ".timestamp_utc")
    if item["outcome"] != "passed":
        raise QualificationError("%s outcome must be passed" % where)
    return QualificationRun(
        duration_seconds=duration,
        wav_path=wav_path,
        wav_sha256=wav_sha,
        analyzer_result_path=analyzer_path,
        analyzer_result_sha256=analyzer_sha,
        timestamp_utc=timestamp,
        outcome="passed",
    )


def _parse_limit(name, item):
    where = "limits.%s" % name
    if not isinstance(item, dict):
        raise QualificationError("%s must be an object" % where)
    unknown = set(item) - LIMIT_KEYS
    if unknown:
        raise QualificationError("unknown key %r in %s" % (sorted(unknown)[0], where))
    if not item:
        raise QualificationError("%s needs min and/or max" % where)
    minimum = item.get("min")
    maximum = item.get("max")
    if minimum is not None and not _is_number(minimum):
        raise QualificationError("%s min must be a finite JSON number" % where)
    if maximum is not None and not _is_number(maximum):
        raise QualificationError("%s max must be a finite JSON number" % where)
    if minimum is None and "min" in item:
        raise QualificationError("%s min must not be null" % where)
    if maximum is None and "max" in item:
        raise QualificationError("%s max must not be null" % where)
    if minimum is not None and maximum is not None and minimum > maximum:
        raise QualificationError("%s min exceeds max" % where)
    if name in EXACT_RANGE_METRICS and (minimum is None or maximum is None):
        raise QualificationError("%s requires explicit min and max" % where)
    if name in MINIMUM_METRICS and (minimum is None or maximum is not None):
        raise QualificationError("%s requires explicit min only" % where)
    if name in MAXIMUM_METRICS and (maximum is None or minimum is not None):
        raise QualificationError("%s requires explicit max only" % where)
    return MetricLimit(minimum=minimum, maximum=maximum)


def load_qualification(path, fixture, binding):
    """Load one accepted external qualification for validated fixture/binding."""
    if fixture.capture_capability is model.CaptureCapability.NONE:
        raise QualificationError("none capture capability cannot use qualification")
    capture_binding = binding.roles.get("capture")
    if not isinstance(capture_binding, model.CaptureBinding):
        raise QualificationError("validated capture binding is required")
    canonical, payload = _read_regular(path, "qualification")
    try:
        text = payload.decode("utf-8")
        obj = json.loads(
            text,
            object_pairs_hook=_duplicate_rejecting_object,
            parse_constant=_reject_constant,
        )
    except _DuplicateKeyError as exc:
        raise QualificationError(
            "duplicate key %r in qualification" % exc.key
        ) from None
    except (UnicodeDecodeError, ValueError, json.JSONDecodeError) as exc:
        raise QualificationError("invalid qualification JSON: %s" % exc) from None
    if not isinstance(obj, dict):
        raise QualificationError("qualification must contain an object")
    canonical_text = (
        json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n"
    )
    if text != canonical_text:
        raise QualificationError(
            "qualification JSON must use canonical sorted encoding"
        )
    _require_exact_keys(obj, ROOT_KEYS, "qualification")
    if obj["schema_version"] != QUALIFICATION_SCHEMA_VERSION or isinstance(
        obj["schema_version"], bool
    ):
        raise QualificationError("qualification schema_version must be 1")
    if obj["status"] != "accepted":
        raise QualificationError("qualification status must be accepted")
    if obj["capability"] != fixture.capture_capability.value:
        raise QualificationError("qualification capability does not match fixture")
    if obj["fixture_id"] != fixture.fixture_id:
        raise QualificationError("qualification fixture_id does not match fixture")
    expected_identity = capture_identity(capture_binding)
    if obj["capture_identity"] != expected_identity:
        raise QualificationError(
            "qualification capture_identity does not match binding"
        )
    metadata_sha = _require_sha256(
        obj["fixture_metadata_sha256"], "qualification.fixture_metadata_sha256"
    )
    if metadata_sha != capture_binding.fixture_metadata.sha256:
        raise QualificationError("qualification fixture metadata SHA-256 drift")
    raw_runs = obj["qualification_runs"]
    if not isinstance(raw_runs, list) or len(raw_runs) < 2:
        raise QualificationError("qualification needs at least two qualification runs")
    parsed_runs = tuple(_parse_run(item, index) for index, item in enumerate(raw_runs))
    limits = obj["limits"]
    if not isinstance(limits, dict):
        raise QualificationError("qualification limits must be an object")
    names = metric_names(fixture.capture_capability)
    if set(limits) != set(names):
        missing = sorted(set(names) - set(limits))
        unknown = sorted(set(limits) - set(names))
        if missing:
            raise QualificationError(
                "qualification limits missing metric %r" % missing[0]
            )
        raise QualificationError("qualification limits unknown metric %r" % unknown[0])
    parsed_limits = {name: _parse_limit(name, limits[name]) for name in names}
    accepted_by = _require_text(obj, "accepted_by", "qualification")
    accepted_at = _canonical_utc(
        obj["accepted_at_utc"], "qualification.accepted_at_utc"
    )
    return CaptureQualification(
        path=canonical,
        sha256=_sha256(payload),
        capability=fixture.capture_capability,
        fixture_id=fixture.fixture_id,
        capture_identity=MappingProxyType(expected_identity),
        fixture_metadata_sha256=metadata_sha,
        qualification_runs=parsed_runs,
        limits=MappingProxyType(parsed_limits),
        accepted_by=accepted_by,
        accepted_at_utc=accepted_at,
    )


def validate_current_qualification(qualification, fixture, binding):
    """Re-open qualification and evidence, rejecting any post-load drift."""
    current = load_qualification(qualification.path, fixture, binding)
    if current.sha256 != qualification.sha256:
        raise QualificationError("qualification file changed after validation")
    return current


def evaluate_limits(metrics, qualification):
    """Return canonical per-metric values, qualified limits, and pass flags."""
    result = {}
    overall = True
    capability = qualification.capability
    capability_value = getattr(capability, "value", capability)
    if capability_value == "mono":
        names = MONO_LIMIT_METRICS
    elif capability_value == "stereo":
        names = STEREO_LIMIT_METRICS
    else:
        raise QualificationError(
            "capture qualification requires mono or stereo capability"
        )
    for name in names:
        limit = qualification.limits[name]
        value = metrics.get(name)
        passed = _is_number(value)
        if passed and limit.minimum is not None and value < limit.minimum:
            passed = False
        if passed and limit.maximum is not None and value > limit.maximum:
            passed = False
        result[name] = {
            "value": value,
            "limit": limit.as_dict(),
            "pass": passed,
        }
        overall = overall and passed
    return result, overall
