#!/usr/bin/env python3
"""Strict parser for the T4 BabbleSim scenario logs.

Parses receiver and client PASS records for one scenario run and asserts exact
transport, lifecycle, count, PLC, decoder-error, routing, and portable PCM
contracts. Receiver PCM metrics are checked against limits independently read
from the checked-in manifest. Client TX FNV remains exact parser-owned evidence.
"""

import argparse
import json
import os
import re
import sys

from lc3_pcm_calibrate import (  # strict fixture validation shared with calibration
    CalibrationError,
    FIXTURES_DIR,
    load_manifest as load_portable_manifest,
    load_stateful_manifest,
)

# ── scenario metadata (versioned data file) ────────────────────────────
# tests/bsim/stage1-scenarios.json is the single source for scenario metadata
# and exact known totals. The portable manifest is sole source for PCM limits.

_SCENARIOS_FILE = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "tests",
    "bsim",
    "stage1-scenarios.json",
)

CHANNEL_MODES = ("mono", "stereo")
KNOWN_KEYS = ("total",)
TRANSPORT_LAYOUTS = ("mono", "stereo-concat")
FNV1A_OFFSET_BASIS = 0x811C9DC5
FNV1A_PRIME = 0x01000193
_HEX_RE = re.compile(r"^0x[0-9A-Fa-f]+$")

# Every production scenario declares each logical stream that can transmit.
# The schema, rather than scenario-name parsing, owns the selected fixtures.
REQUIRED_TRANSPORT_STREAMS = {
    "mono_10ms": {0},
    "mono_7p5ms": {0},
    "modea_10ms": {0, 1},
    "modea_7p5ms": {0, 1},
    "modea_reverse_start_10ms": {0, 1},
    "modeb_10ms": {0},
    "modeb_7p5ms": {0},
    "invalid_sdu_resume_10ms": {0},
    "modea_one_cis_loss_10ms": {0, 1},
    "modea_first_stop_10ms": {0, 1},
    "release_without_disable_10ms": {0},
    "disconnect_streaming_10ms": {0},
    "reconnect_second_stream_10ms": {0, 1},
    "unsupported_source_direction": set(),
    "no_free_sink_slot": set(),
    "invalid_codec_fields": set(),
    "duplicate_release_10ms": {0},
}

# Stage 1 owns a fixed 17-scenario/26-run matrix. Scenario JSON remains the
# single source for transport metadata and known totals, but it must not be
# able to silently add, remove, or reclassify matrix entries.
EXPECTED_SCENARIO_CONTRACTS = {
    "mono_10ms": (2, 1, "mono"),
    "mono_7p5ms": (2, 1, "mono"),
    "modea_10ms": (2, 2, "stereo"),
    "modea_7p5ms": (2, 2, "stereo"),
    "modea_reverse_start_10ms": (2, 2, "stereo"),
    "modeb_10ms": (2, 2, "stereo"),
    "modeb_7p5ms": (2, 2, "stereo"),
    "invalid_sdu_resume_10ms": (2, 1, "mono"),
    "modea_one_cis_loss_10ms": (2, 2, "stereo"),
    "modea_first_stop_10ms": (1, 2, "stereo"),
    "release_without_disable_10ms": (1, 1, "mono"),
    "disconnect_streaming_10ms": (1, 1, "mono"),
    "reconnect_second_stream_10ms": (1, 1, "mono"),
    "unsupported_source_direction": (1, 1, "mono"),
    "no_free_sink_slot": (1, 1, "mono"),
    "invalid_codec_fields": (1, 1, "mono"),
    "duplicate_release_10ms": (1, 1, "mono"),
}

EXPECTED_KNOWN_TOTALS = {
    "mono_10ms": {"total": 108},
    "mono_7p5ms": {"total": 111},
    "modea_10ms": {"total": 216},
    "modea_7p5ms": {"total": 226},
    "modea_reverse_start_10ms": {"total": 216},
    "modeb_10ms": {"total": 216},
    "modeb_7p5ms": {"total": 222},
    "invalid_sdu_resume_10ms": {"total": 108},
    "modea_one_cis_loss_10ms": {"total": 216},
    "modea_first_stop_10ms": {"total": 86},
    "release_without_disable_10ms": {"total": 56},
    "disconnect_streaming_10ms": {"total": 63},
    "reconnect_second_stream_10ms": {"total": 63},
    "unsupported_source_direction": {},
    "no_free_sink_slot": {},
    "invalid_codec_fields": {},
    "duplicate_release_10ms": {"total": 56},
}

# (stream, layout, fixture stems, malformed index or None). Keep this fixed
# contract separate from corpus-manifest validation: a well-formed replacement
# fixture must not silently rebaseline Stage 1 transport evidence.
EXPECTED_TRANSPORTS = {
    "mono_10ms": ((0, "mono", ("bsim_48k_10ms_120b_l",), None),),
    "mono_7p5ms": ((0, "mono", ("bsim_48k_7p5ms_90b_l",), None),),
    "modea_10ms": (
        (0, "mono", ("bsim_48k_10ms_120b_l",), None),
        (1, "mono", ("bsim_48k_10ms_120b_r",), None),
    ),
    "modea_7p5ms": (
        (0, "mono", ("bsim_48k_7p5ms_90b_l",), None),
        (1, "mono", ("bsim_48k_7p5ms_90b_r",), None),
    ),
    "modea_reverse_start_10ms": (
        (0, "mono", ("bsim_48k_10ms_120b_l",), None),
        (1, "mono", ("bsim_48k_10ms_120b_r",), None),
    ),
    "modeb_10ms": (
        (0, "stereo-concat", ("bsim_48k_10ms_120b_l", "bsim_48k_10ms_120b_r"), None),
    ),
    "modeb_7p5ms": (
        (0, "stereo-concat", ("bsim_48k_7p5ms_90b_l", "bsim_48k_7p5ms_90b_r"), None),
    ),
    "invalid_sdu_resume_10ms": ((0, "mono", ("bsim_48k_10ms_120b_l",), 20),),
    "modea_one_cis_loss_10ms": (
        (0, "mono", ("bsim_48k_10ms_120b_l",), None),
        (1, "mono", ("bsim_48k_10ms_120b_r",), None),
    ),
    "modea_first_stop_10ms": (
        (0, "mono", ("bsim_48k_10ms_120b_l",), None),
        (1, "mono", ("bsim_48k_10ms_120b_r",), None),
    ),
    "release_without_disable_10ms": ((0, "mono", ("bsim_48k_10ms_120b_l",), None),),
    "disconnect_streaming_10ms": ((0, "mono", ("bsim_48k_10ms_120b_l",), None),),
    "reconnect_second_stream_10ms": (
        (0, "mono", ("bsim_48k_10ms_120b_l",), None),
        (1, "mono", ("bsim_48k_10ms_120b_l",), None),
    ),
    "unsupported_source_direction": (),
    "no_free_sink_slot": (),
    "invalid_codec_fields": (),
    "duplicate_release_10ms": ((0, "mono", ("bsim_48k_10ms_120b_l",), None),),
}

EXPECTED_RECEIVER_ORACLES = {
    "mono_10ms": (("start8_10ms_l", "start8_10ms_l", "full"),),
    "mono_7p5ms": (("start11_7p5ms_l", "start11_7p5ms_l", "full"),),
    "modea_10ms": (("start8_10ms_l", "start8_10ms_r", "full"),),
    "modea_7p5ms": (("modea_start_7p5ms_l", "modea_start_7p5ms_r", "full"),),
    "modea_reverse_start_10ms": (("start8_10ms_l", "start8_10ms_r", "full"),),
    "modeb_10ms": (("start8_10ms_l", "start8_10ms_r", "full"),),
    "modeb_7p5ms": (("start11_7p5ms_l", "start11_7p5ms_r", "full"),),
    "invalid_sdu_resume_10ms": (("skip20_10ms_l", "skip20_10ms_l", "full"),),
    "modea_one_cis_loss_10ms": (("start8_10ms_l", "loss48x18_10ms_r", "full"),),
    "modea_first_stop_10ms": (("start8_10ms_l", "start8_10ms_r", "prefix"),),
    "release_without_disable_10ms": (("start8_10ms_l", "start8_10ms_l", "prefix"),),
    "disconnect_streaming_10ms": (("start8_10ms_l", "start8_10ms_l", "prefix"),),
    "reconnect_second_stream_10ms": (
        ("start8_10ms_l", "start8_10ms_l", "prefix"),
        ("start7_10ms_l", "start7_10ms_l", "full"),
    ),
    "unsupported_source_direction": (),
    "no_free_sink_slot": (),
    "invalid_codec_fields": (),
    "duplicate_release_10ms": (("start8_10ms_l", "start8_10ms_l", "prefix"),),
}

RECEIVER_ORACLE_COMPLETIONS = ("full", "prefix")
SCENARIO_ROOT_FIELDS = {
    "schema_version",
    "baselined",
    "description",
    "notes",
    "scenarios",
}


class ScenarioDataError(ValueError):
    """Raised when the versioned scenario data file is malformed."""


def load_transport_manifest():
    """Load P0's strict portable corpus manifest for transport derivation."""
    try:
        manifest, _fixture_hashes, _manifest_hash = load_portable_manifest()
    except CalibrationError as exc:
        raise ScenarioDataError("portable LC3 manifest invalid: %s" % exc) from exc
    return manifest


def _fixture_map(manifest):
    return {entry["stem"]: entry for entry in manifest["streams"]}


def _stateful_recipe_map(portable_manifest):
    """Load accepted stateful reference manifest as immutable recipe authority."""
    try:
        stateful_manifest, _reference_hashes, _manifest_hash = load_stateful_manifest(
            portable_manifest
        )
    except CalibrationError as exc:
        raise ScenarioDataError(
            "stateful reference manifest invalid: %s" % exc
        ) from exc
    return {entry["id"]: entry for entry in stateful_manifest["recipes"]}


def _transport_geometry(transport, fixtures, where):
    """Return one shared transport geometry or None for no-audio scenarios."""
    geometry = None
    for entry in transport:
        for stem in entry["fixtures"]:
            fixture = fixtures[stem]
            candidate = (
                fixture["duration_us"],
                fixture["frame_bytes"],
                fixture["samples_per_frame"],
            )
            if geometry is None:
                geometry = candidate
            elif geometry != candidate:
                raise ScenarioDataError(
                    "%s transport fixtures have mixed geometry" % where
                )
    return geometry


def load_scenarios(path=None):
    """Load and validate the versioned scenario data file.

    Returns the parsed JSON object (a dict with a "scenarios" list).
    Raises ScenarioDataError with a clear message on missing file, invalid
    JSON, missing/unknown fields, duplicate scenario names, or invalid
    values — never silently ignores a malformed matrix.
    """
    data_path = path or _SCENARIOS_FILE
    if not os.path.isfile(data_path):
        raise ScenarioDataError("scenario data file not found: %s" % data_path)
    try:
        with open(data_path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError) as exc:
        raise ScenarioDataError("cannot read %s: %s" % (data_path, exc))

    if not isinstance(data, dict) or not isinstance(data.get("scenarios"), list):
        raise ScenarioDataError(
            "%s: must be an object with a 'scenarios' list" % data_path
        )
    unknown_root = sorted(set(data) - SCENARIO_ROOT_FIELDS)
    if unknown_root:
        raise ScenarioDataError(
            "%s: unknown top-level fields %s" % (data_path, ", ".join(unknown_root))
        )
    if data.get("schema_version") != 3:
        raise ScenarioDataError(
            "%s: missing/unsupported schema_version (expected 3)" % data_path
        )
    if not data["scenarios"]:
        raise ScenarioDataError("%s: empty 'scenarios' list" % data_path)

    manifest = load_transport_manifest()
    fixtures = _fixture_map(manifest)
    stateful_recipes = _stateful_recipe_map(manifest)

    seen = set()
    for idx, entry in enumerate(data["scenarios"]):
        where = "%s: scenario[%d]" % (data_path, idx)
        if not isinstance(entry, dict):
            raise ScenarioDataError("%s: not an object" % where)
        allowed_keys = {
            "name",
            "runs",
            "dec_calls",
            "channel_mode",
            "known",
            "transport",
            "receiver_oracle",
        }
        required_keys = allowed_keys - {"known"}
        unknown = sorted(set(entry) - allowed_keys)
        missing = sorted(required_keys - set(entry))
        if unknown:
            raise ScenarioDataError(
                "%s: unknown fields %s" % (where, ", ".join(unknown))
            )
        if missing:
            raise ScenarioDataError(
                "%s: missing fields %s" % (where, ", ".join(missing))
            )
        name = entry.get("name")
        if not isinstance(name, str) or not name:
            raise ScenarioDataError("%s: missing/empty 'name'" % where)
        if name not in EXPECTED_SCENARIO_CONTRACTS:
            raise ScenarioDataError("%s: unknown Stage 1 scenario %r" % (where, name))
        if name in seen:
            raise ScenarioDataError("%s: duplicate scenario name %r" % (where, name))
        seen.add(name)
        runs = entry.get("runs")
        if not isinstance(runs, int) or runs < 1:
            raise ScenarioDataError("%s: 'runs' must be a positive int" % where)
        dec_calls = entry.get("dec_calls")
        if not isinstance(dec_calls, int) or dec_calls < 1:
            raise ScenarioDataError("%s: 'dec_calls' must be a positive int" % where)
        if entry.get("channel_mode") not in CHANNEL_MODES:
            raise ScenarioDataError(
                "%s: 'channel_mode' must be one of %s"
                % (where, ", ".join(CHANNEL_MODES))
            )
        expected_runs, expected_dec_calls, expected_channel_mode = (
            EXPECTED_SCENARIO_CONTRACTS[name]
        )
        if (runs, dec_calls, entry["channel_mode"]) != (
            expected_runs,
            expected_dec_calls,
            expected_channel_mode,
        ):
            raise ScenarioDataError(
                "%s: matrix contract %r != expected %r"
                % (
                    where,
                    (runs, dec_calls, entry["channel_mode"]),
                    (expected_runs, expected_dec_calls, expected_channel_mode),
                )
            )
        known = entry.get("known", {})
        if not isinstance(known, dict):
            raise ScenarioDataError("%s: 'known' must be an object" % where)
        for key, val in known.items():
            if key not in KNOWN_KEYS:
                raise ScenarioDataError("%s: unknown known key %r" % (where, key))
            if key == "total":
                if not isinstance(val, int) or val < 0:
                    raise ScenarioDataError(
                        "%s: known.total must be a non-negative int" % where
                    )
        if known != EXPECTED_KNOWN_TOTALS[name]:
            raise ScenarioDataError(
                "%s: known contract %r != expected %r"
                % (where, known, EXPECTED_KNOWN_TOTALS[name])
            )

        if "transport" not in entry:
            raise ScenarioDataError("%s: missing 'transport'" % where)
        transport = entry["transport"]
        if not isinstance(transport, list) or len(transport) > 2:
            raise ScenarioDataError(
                "%s: 'transport' must be a list of zero to two entries" % where
            )

        transport_streams = set()
        observed_transport = []
        for transport_idx, transport_entry in enumerate(transport):
            transport_where = "%s.transport[%d]" % (where, transport_idx)
            if not isinstance(transport_entry, dict):
                raise ScenarioDataError("%s: not an object" % transport_where)
            allowed_keys = {"stream", "layout", "fixtures", "malformed_at"}
            required_keys = {"stream", "layout", "fixtures"}
            actual_keys = set(transport_entry)
            unknown = sorted(actual_keys - allowed_keys)
            missing = sorted(required_keys - actual_keys)
            if unknown:
                raise ScenarioDataError(
                    "%s: unknown fields %s" % (transport_where, ", ".join(unknown))
                )
            if missing:
                raise ScenarioDataError(
                    "%s: missing fields %s" % (transport_where, ", ".join(missing))
                )

            stream = transport_entry["stream"]
            if type(stream) is not int or stream not in (0, 1):
                raise ScenarioDataError("%s.stream must be 0 or 1" % transport_where)
            if stream in transport_streams:
                raise ScenarioDataError(
                    "%s: duplicate transport stream %d" % (where, stream)
                )
            transport_streams.add(stream)

            layout = transport_entry["layout"]
            if layout not in TRANSPORT_LAYOUTS:
                raise ScenarioDataError(
                    "%s.layout must be one of %s"
                    % (transport_where, ", ".join(TRANSPORT_LAYOUTS))
                )
            fixture_stems = transport_entry["fixtures"]
            if not isinstance(fixture_stems, list) or not all(
                isinstance(stem, str) for stem in fixture_stems
            ):
                raise ScenarioDataError(
                    "%s.fixtures must be a list of strings" % transport_where
                )
            if any(stem not in fixtures for stem in fixture_stems):
                raise ScenarioDataError(
                    "%s.fixtures contains an unknown fixture" % transport_where
                )

            if layout == "mono":
                if len(fixture_stems) != 1:
                    raise ScenarioDataError(
                        "%s mono layout needs one fixture" % transport_where
                    )
            else:
                if len(fixture_stems) != 2:
                    raise ScenarioDataError(
                        "%s stereo-concat layout needs two fixtures" % transport_where
                    )
                left, right = (fixtures[stem] for stem in fixture_stems)
                if left["channel"] != "left" or right["channel"] != "right":
                    raise ScenarioDataError(
                        "%s stereo-concat fixtures must be left then right"
                        % transport_where
                    )
                if (
                    left["duration_us"],
                    left["frequency_hz"],
                    left["frame_bytes"],
                    left["frame_count"],
                ) != (
                    right["duration_us"],
                    right["frequency_hz"],
                    right["frame_bytes"],
                    right["frame_count"],
                ):
                    raise ScenarioDataError(
                        "%s stereo-concat fixtures have different geometry"
                        % transport_where
                    )

            malformed_at = transport_entry.get("malformed_at")
            if "malformed_at" in transport_entry:
                if type(malformed_at) is not int or not 0 <= malformed_at < 128:
                    raise ScenarioDataError(
                        "%s.malformed_at must be an integer from 0 through 127"
                        % transport_where
                    )
                if (
                    name != "invalid_sdu_resume_10ms"
                    or stream != 0
                    or malformed_at != 20
                ):
                    raise ScenarioDataError(
                        "%s.malformed_at is only valid for invalid_sdu_resume_10ms stream 0 at 20"
                        % transport_where
                    )
            observed_transport.append(
                (stream, layout, tuple(fixture_stems), malformed_at)
            )

        if name == "invalid_sdu_resume_10ms" and (
            len(transport) != 1 or "malformed_at" not in transport[0]
        ):
            raise ScenarioDataError(
                "%s: invalid-SDU scenario must declare malformed_at 20" % where
            )
        expected_streams = REQUIRED_TRANSPORT_STREAMS.get(name)
        if expected_streams is not None and transport_streams != expected_streams:
            raise ScenarioDataError(
                "%s: transport streams %s != required %s"
                % (where, sorted(transport_streams), sorted(expected_streams))
            )
        if tuple(observed_transport) != EXPECTED_TRANSPORTS[name]:
            raise ScenarioDataError(
                "%s: transport contract %r != expected %r"
                % (where, tuple(observed_transport), EXPECTED_TRANSPORTS[name])
            )

        receiver_oracle = entry["receiver_oracle"]
        if not isinstance(receiver_oracle, list):
            raise ScenarioDataError("%s.receiver_oracle must be a list" % where)
        expected_oracle = EXPECTED_RECEIVER_ORACLES[name]
        if len(receiver_oracle) != len(expected_oracle):
            raise ScenarioDataError(
                "%s.receiver_oracle length %d != required %d"
                % (where, len(receiver_oracle), len(expected_oracle))
            )

        transport_geometry = _transport_geometry(transport, fixtures, where)
        observed_oracle = []
        for oracle_index, oracle_entry in enumerate(receiver_oracle):
            oracle_where = "%s.receiver_oracle[%d]" % (where, oracle_index)
            if not isinstance(oracle_entry, dict):
                raise ScenarioDataError("%s: not an object" % oracle_where)
            if set(oracle_entry) != {
                "left_recipe",
                "right_recipe",
                "completion",
            }:
                raise ScenarioDataError(
                    "%s: fields must be left_recipe/right_recipe/completion"
                    % oracle_where
                )
            left_recipe = oracle_entry["left_recipe"]
            right_recipe = oracle_entry["right_recipe"]
            completion = oracle_entry["completion"]
            if (
                not isinstance(left_recipe, str)
                or not isinstance(right_recipe, str)
                or left_recipe not in stateful_recipes
                or right_recipe not in stateful_recipes
            ):
                raise ScenarioDataError("%s: unknown recipe id" % oracle_where)
            if completion not in RECEIVER_ORACLE_COMPLETIONS:
                raise ScenarioDataError(
                    "%s: completion must be full or prefix" % oracle_where
                )
            left_geometry = (
                stateful_recipes[left_recipe]["duration_us"],
                stateful_recipes[left_recipe]["frame_bytes"],
                stateful_recipes[left_recipe]["samples_per_frame"],
            )
            right_geometry = (
                stateful_recipes[right_recipe]["duration_us"],
                stateful_recipes[right_recipe]["frame_bytes"],
                stateful_recipes[right_recipe]["samples_per_frame"],
            )
            if (
                transport_geometry is None
                or left_geometry != transport_geometry
                or right_geometry != transport_geometry
            ):
                raise ScenarioDataError(
                    "%s: recipe geometry does not match transport" % oracle_where
                )
            observed_oracle.append((left_recipe, right_recipe, completion))
        if tuple(observed_oracle) != expected_oracle:
            raise ScenarioDataError(
                "%s.receiver_oracle does not match fixed Stage 1 mapping" % where
            )
    if seen != set(EXPECTED_SCENARIO_CONTRACTS):
        raise ScenarioDataError(
            "%s: Stage 1 scenarios %s != required %s"
            % (data_path, sorted(seen), sorted(EXPECTED_SCENARIO_CONTRACTS))
        )
    return data


# Scenario name -> (decoder calls per push, mono or stereo, runs in matrix)
_SCENARIO_DATA = load_scenarios()
_STATEFUL_RECIPES = _stateful_recipe_map(load_transport_manifest())

SCENARIOS = {
    entry["name"]: (entry["dec_calls"], entry["channel_mode"], entry["runs"])
    for entry in _SCENARIO_DATA["scenarios"]
}

TRANSPORT_VALUES = {
    entry["name"]: entry["transport"] for entry in _SCENARIO_DATA["scenarios"]
}

RECEIVER_ORACLE_VALUES = {
    entry["name"]: tuple(
        (oracle["left_recipe"], oracle["right_recipe"], oracle["completion"])
        for oracle in entry["receiver_oracle"]
    )
    for entry in _SCENARIO_DATA["scenarios"]
}

# Scenario name -> exact known totals from the versioned file.
KNOWN_VALUES = {}
for _entry in _SCENARIO_DATA["scenarios"]:
    _known = _entry.get("known") or {}
    _val = {}
    if "total" in _known:
        _val["total"] = _known["total"]
    if _val:
        KNOWN_VALUES[_entry["name"]] = _val


def known_values(scenario):
    """Pinned known values for a scenario (empty dict when none pinned)."""
    return dict(KNOWN_VALUES.get(scenario, {}))


# Scheduled single-CIS losses in the Mode A one-CIS-loss scenario: the
# right stream pauses mid-stream for a bounded window and its CIS loses
# exactly this many events; the receiver emits exactly this many
# post-start concealed pushes and post-start PLC frames (the unaffected
# channel stays valid).
MODEA_LOSS_COUNT = 18

# ── log parsing ──────────────────────────────────────────────────────


class ParseError(Exception):
    pass


def extract_pass_line(path, testid):
    """Return the last 'INFO: <testid>:' line of a BSim log."""
    marker = "INFO: %s:" % testid
    found = None
    try:
        with open(path, "r", errors="replace") as fh:
            for line in fh:
                if marker in line:
                    found = line
    except OSError as exc:
        raise ParseError("cannot read %s: %s" % (path, exc))
    if found is None:
        raise ParseError("no PASS line for %s in %s" % (testid, path))
    return found


_TOKEN_RE = re.compile(r"(\w+)=([-0-9A-Za-z_.xX]+)")

_RECEIVER_BASE_FIELDS = (
    "scenario",
    "seg",
    "after",
    "adv_restart",
    "pacs",
    "obs_ok",
    "obs_rej",
    "obs_dir",
    "obs_code",
    "obs_reason",
    "obs_gate_o",
    "obs_gate_c",
    "obs_mal",
    "obs_blk",
    "obs_stale",
    "obs_rel",
    "obs_disc",
    "obs_rej_code",
    "obs_rej_reason",
    "obs_mts",
    "obs_rel_ss",
    "rel_ss_seq",
    "disc_seq",
    "limmax",
    "limrms",
    "limcorr",
)


def _segment_recipe_id_fields(segment):
    suffix = str(segment)
    return ("lrid" + suffix, "rrid" + suffix)


def _segment_recipe_count_fields(segment):
    suffix = str(segment)
    return (
        "lact" + suffix,
        "lval" + suffix,
        "lplc" + suffix,
        "ract" + suffix,
        "rval" + suffix,
        "rplc" + suffix,
    )


def _segment_metric_fields(segment):
    suffix = str(segment)
    return (
        "pushes" + suffix,
        "samples" + suffix,
        "spc" + suffix,
        "diff" + suffix,
        "lfr" + suffix,
        "lsm" + suffix,
        "lex" + suffix,
        "lmax" + suffix,
        "lsse" + suffix,
        "lrms" + suffix,
        "lcorr" + suffix,
        "lres" + suffix,
        "rfr" + suffix,
        "rsm" + suffix,
        "rex" + suffix,
        "rmax" + suffix,
        "rsse" + suffix,
        "rrms" + suffix,
        "rcorr" + suffix,
        "rres" + suffix,
    )


def _segment_integer_fields(segment):
    return _segment_recipe_count_fields(segment) + _segment_metric_fields(segment)


def _segment_fields(segment):
    return _segment_recipe_id_fields(segment) + _segment_integer_fields(segment)


def _receiver_segment_fields(segment):
    suffix = str(segment)
    return (
        "pushes" + suffix,
        "trans" + suffix,
        "szero" + suffix,
        "splc" + suffix,
        "plc" + suffix,
        "total" + suffix,
        "derr" + suffix,
        "mal" + suffix,
        "samples" + suffix,
        "spc" + suffix,
        "diff" + suffix,
        "lemin" + suffix,
        "lemax" + suffix,
        "remin" + suffix,
        "remax" + suffix,
    ) + _segment_fields(segment)


RECEIVER_REQUIRED_FIELDS = (
    _RECEIVER_BASE_FIELDS + _receiver_segment_fields(1) + _receiver_segment_fields(2)
)
CLIENT_REQUIRED_FIELDS = (
    "scenario",
    "sends0",
    "sends1",
    "cfgrsps",
    "relrsps",
    "disrsps",
    "txc0",
    "txh0",
    "txc1",
    "txh1",
)
RECEIVER_FIELD_SET = frozenset(RECEIVER_REQUIRED_FIELDS)
CLIENT_FIELD_SET = frozenset(CLIENT_REQUIRED_FIELDS)


def parse_tokens(line):
    """Parse key=value tokens from a PASS line into a dict (ints where possible)."""
    out = {}
    for key, val in _TOKEN_RE.findall(line):
        if key in out:
            raise ParseError("duplicate PASS token %s" % key)
        try:
            out[key] = int(val, 16) if val.lower().startswith("0x") else int(val)
        except ValueError:
            out[key] = val
    return out


def parse_receiver_pass(path):
    line = extract_pass_line(path, "le_audio_receiver")
    tokens = parse_tokens(line)
    unknown = sorted(set(tokens) - RECEIVER_FIELD_SET)
    if unknown:
        raise ParseError("receiver PASS unknown fields %s" % unknown)
    missing = [k for k in RECEIVER_REQUIRED_FIELDS if k not in tokens]
    if missing:
        raise ParseError("receiver PASS missing fields %s" % missing)
    string_fields = {"scenario", "lrid1", "rrid1", "lrid2", "rrid2"}
    if any(type(tokens[key]) is not str or not tokens[key] for key in string_fields):
        raise ParseError(
            "receiver PASS recipe/scenario fields must be non-empty strings"
        )
    non_integer = [
        key
        for key in RECEIVER_REQUIRED_FIELDS
        if key not in string_fields and type(tokens[key]) is not int
    ]
    if non_integer:
        raise ParseError("receiver PASS non-integer fields %s" % non_integer)
    return tokens


def parse_client_pass(path):
    line = extract_pass_line(path, "bsim_client")
    tokens = parse_tokens(line)
    unknown = sorted(set(tokens) - CLIENT_FIELD_SET)
    if unknown:
        raise ParseError("client PASS unknown fields %s" % unknown)
    missing = [k for k in CLIENT_REQUIRED_FIELDS if k not in tokens]
    if type(tokens["scenario"]) is not str or not tokens["scenario"]:
        raise ParseError("client PASS scenario must be a non-empty string")
    if missing:
        raise ParseError("client PASS missing fields %s" % missing)
    non_integer = [
        key
        for key in CLIENT_REQUIRED_FIELDS
        if key != "scenario" and type(tokens[key]) is not int
    ]
    if non_integer:
        raise ParseError("client PASS non-integer fields %s" % non_integer)
    return tokens


# ── fault-marker scan ────────────────────────────────────────────────

# Log markers that indicate a semantic decode/lifecycle fault.
FAULT_MARKERS = [
    "ASSERTION FAILURE",
    "FATAL",
    "decode error",
    "decoder not ready",
    "decode failed",
    "zero-energy push after audio started",
    "source-invalid push after valid boundary",
    "push after stop",
    "malformed sample count",
    "Failed to start stream",
    "stream_lifecycle",
]

# Narrow per-scenario allowlist for deliberately exercised negative paths
# that log at WRN/ERR level in the SDK. The receiver warning must end with
# this exact message; nothing else is forgiven.
SCENARIO_ALLOW = {
    "duplicate_release_10ms": re.compile(
        r"<wrn>\s+bt_ascs: Invalid operation in state: releasing\s*$"
    ),
}

_ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
_LOG_LEVEL_RE = re.compile(r"<(?:wrn|err)>")


def _warning_is_allowed(role, scenario, line):
    allowed = SCENARIO_ALLOW.get(scenario)

    return (
        role == "receiver" and allowed is not None and allowed.search(line) is not None
    )


def scan_faults(receiver_path, client_path, scenario):
    """Reject semantic faults and warning/error records from both app logs."""
    hits = []

    for role, path in (("receiver", receiver_path), ("client", client_path)):
        try:
            with open(path, "r", errors="replace") as fh:
                for raw_line in fh:
                    line = _ANSI_ESCAPE_RE.sub("", raw_line).rstrip()
                    if any(marker in line for marker in FAULT_MARKERS):
                        hits.append("%s: %s" % (role, line))
                        continue
                    if _LOG_LEVEL_RE.search(line) and not _warning_is_allowed(
                        role, scenario, line
                    ):
                        hits.append("%s: %s" % (role, line))
        except OSError as exc:
            raise ParseError("cannot read %s log %s: %s" % (role, path, exc))
    if hits:
        raise ParseError("fault markers in app logs: %s" % "; ".join(hits[:5]))


# ── scenario checks ──────────────────────────────────────────────────


def _h(rec, key, scenario, field):
    if key not in rec:
        raise ParseError("%s: missing %s field" % (scenario, field))
    return rec[key]


def _fnv1a_bytes(hash_value, payload):
    for byte in payload:
        hash_value = ((hash_value ^ byte) * FNV1A_PRIME) & 0xFFFFFFFF
    return hash_value


def _transport_fixture_map():
    try:
        return _fixture_map(load_transport_manifest())
    except ScenarioDataError as exc:
        raise ParseError("cannot validate portable LC3 manifest: %s" % exc) from exc


def _fixture_lc3_bytes(fixture):
    path = FIXTURES_DIR / fixture["lc3"]["path"]
    try:
        payload = path.read_bytes()
    except OSError as exc:
        raise ParseError("cannot read LC3 fixture %s: %s" % (path, exc)) from exc
    expected_size = fixture["frame_bytes"] * fixture["frame_count"]
    if len(payload) != expected_size:
        raise ParseError(
            "LC3 fixture size changed after manifest validation: %s" % path
        )
    return payload


def expected_transport_hash(transport, count, fixtures=None):
    """Derive C-contract FNV-1a from validated corpus frames and transport data."""
    if type(count) is not int or count < 0:
        raise ParseError("transport count must be a non-negative integer")
    if fixtures is None:
        fixtures = _transport_fixture_map()

    fixture_entries = [fixtures[stem] for stem in transport["fixtures"]]
    capacity = fixture_entries[0]["frame_count"]
    if count > capacity:
        raise ParseError(
            "transport count %d exceeds corpus capacity %d" % (count, capacity)
        )

    corpus = {entry["stem"]: _fixture_lc3_bytes(entry) for entry in fixture_entries}
    hash_value = FNV1A_OFFSET_BASIS
    malformed_at = transport.get("malformed_at")
    for sequence in range(count):
        payload = b"".join(
            corpus[entry["stem"]][
                sequence * entry["frame_bytes"] : (sequence + 1) * entry["frame_bytes"]
            ]
            for entry in fixture_entries
        )
        if sequence == malformed_at:
            payload = payload[:-1]
        hash_value = _fnv1a_bytes(hash_value, sequence.to_bytes(4, "little"))
        hash_value = _fnv1a_bytes(hash_value, payload)
    return hash_value


def check_transport(scenario, client):
    """Check retained client TX evidence against schema-owned corpus bytes."""
    entries = {entry["stream"]: entry for entry in TRANSPORT_VALUES[scenario]}
    fixtures = _transport_fixture_map()
    errs = []

    for stream in (0, 1):
        sends = client["sends%d" % stream]
        count = client["txc%d" % stream]
        observed_hash = client["txh%d" % stream]
        prefix = "stream %d" % stream

        if (
            type(sends) is not int
            or sends < 0
            or type(count) is not int
            or count < 0
            or type(observed_hash) is not int
            or not 0 <= observed_hash <= 0xFFFFFFFF
        ):
            errs.append("%s TX fields are not unsigned integers" % prefix)
            continue
        if count != sends:
            errs.append(
                "%s txc%d %d != sends%d %d" % (prefix, stream, count, stream, sends)
            )
            continue

        transport = entries.get(stream)
        if transport is None:
            if count != 0 or sends != 0 or observed_hash != FNV1A_OFFSET_BASIS:
                errs.append(
                    "%s without transport must have zero sends/count and offset-basis hash"
                    % prefix
                )
            continue

        try:
            expected_hash = expected_transport_hash(transport, count, fixtures)
        except ParseError as exc:
            errs.append("%s %s" % (prefix, exc))
            continue
        if observed_hash != expected_hash:
            errs.append(
                "%s txh%d 0x%08X != expected 0x%08X"
                % (prefix, stream, observed_hash, expected_hash)
            )

    return errs


PCM_ORACLE_RESULT_PASS = 0
PCM_ORACLE_RESULT_INSUFFICIENT_SAMPLES = 1


def _manifest_pcm_limits():
    """Load immutable PCM limits independently from scenario data."""
    try:
        manifest = load_transport_manifest()
    except ScenarioDataError as exc:
        raise ParseError("cannot validate portable PCM limits: %s" % exc) from exc
    limits = manifest.get("pcm_limits")
    names = ("max_abs_error", "max_rms_error", "min_correlation_q15")
    if not isinstance(limits, dict) or set(limits) != set(names):
        raise ParseError("portable manifest PCM limits malformed")
    if any(type(limits[name]) is not int for name in names):
        raise ParseError("portable manifest PCM limits must be integers")
    return limits


def _check_pcm_limits(record):
    limits = _manifest_pcm_limits()
    expected = {
        "limmax": limits["max_abs_error"],
        "limrms": limits["max_rms_error"],
        "limcorr": limits["min_correlation_q15"],
    }
    errs = []
    for key, value in expected.items():
        observed = record.get(key)
        if type(observed) is not int:
            errs.append("%s missing or non-integer" % key)
        elif observed != value:
            errs.append("%s %s != manifest %s" % (key, observed, value))
    return limits, errs


def _segment_values(record, segment):
    values = {}
    errs = []
    for key in _receiver_segment_fields(segment):
        if key in _segment_recipe_id_fields(segment):
            continue
        value = record.get(key)
        if type(value) is not int:
            errs.append("segment %d %s missing or non-integer" % (segment, key))
        else:
            values[key] = value
    return values, errs


def _scenario_pcm_geometry(scenario):
    entries = TRANSPORT_VALUES[scenario]
    if not entries:
        raise ParseError("%s has no transport PCM geometry" % scenario)
    fixtures = _transport_fixture_map()
    samples = None
    for entry in entries:
        for stem in entry["fixtures"]:
            candidate = fixtures[stem]["samples_per_frame"]
            if samples is None:
                samples = candidate
            elif samples != candidate:
                raise ParseError(
                    "%s transport fixtures have mixed PCM geometry" % scenario
                )
    if samples not in (360, 480):
        raise ParseError(
            "%s transport PCM geometry unsupported: %s" % (scenario, samples)
        )
    return samples


def _recipe_prefix_counts(recipe, actions):
    """Return valid/PLC counts at an exact expanded recipe prefix."""
    if (
        type(actions) is not int
        or actions < 0
        or actions > recipe["output_action_count"]
    ):
        return None

    remaining = actions
    valid = 0
    plc = 0
    for step in recipe["steps"]:
        consumed = min(remaining, step["count"])
        if step["action"] == "corpus":
            valid += consumed
        elif step["action"] == "plc":
            plc += consumed
        else:
            return None
        remaining -= consumed
        if remaining == 0:
            break
    return (valid, plc) if remaining == 0 else None


def _check_recipe_channel(record, segment, values, channel, expected_id, completion):
    suffix = str(segment)
    prefix = "l" if channel == "left" else "r"
    recipe_id = record[prefix + "rid" + suffix]
    recipe = _STATEFUL_RECIPES.get(recipe_id)
    actions = values[prefix + "act" + suffix]
    valid = values[prefix + "val" + suffix]
    plc = values[prefix + "plc" + suffix]
    frames = values[prefix + "fr" + suffix]
    samples = values[prefix + "sm" + suffix]
    excluded = values[prefix + "ex" + suffix]
    pushes = values["pushes" + suffix]
    transients = values["trans" + suffix]
    samples_per_channel = values["spc" + suffix]
    errs = []

    if recipe_id != expected_id or recipe is None:
        return [
            "segment %d %s recipe %s != expected %s"
            % (segment, channel, recipe_id, expected_id)
        ]
    if recipe["samples_per_frame"] != samples_per_channel:
        errs.append(
            "segment %d %s recipe geometry %d != spc %d"
            % (segment, channel, recipe["samples_per_frame"], samples_per_channel)
        )
    prefix_counts = _recipe_prefix_counts(recipe, actions)
    if prefix_counts is None:
        errs.append("segment %d %s recipe action overrun" % (segment, channel))
        return errs
    expected_valid, expected_plc = prefix_counts
    pre_prefix_counts = _recipe_prefix_counts(recipe, transients)
    if pre_prefix_counts is None:
        errs.append("segment %d %s startup recipe prefix overrun" % (segment, channel))
        return errs
    pre_valid, pre_plc = pre_prefix_counts
    if actions != transients + pushes:
        errs.append(
            "segment %d %s recipe actions %d != transients %d + pushes %d"
            % (segment, channel, actions, transients, pushes)
        )
    if valid != expected_valid or plc != expected_plc:
        errs.append(
            "segment %d %s recipe prefix %d/%d != expected %d/%d"
            % (segment, channel, valid, plc, expected_valid, expected_plc)
        )
    if valid != frames:
        errs.append(
            "segment %d %s recipe valid %d != compared frames %d"
            % (segment, channel, valid, frames)
        )
    if plc != actions - valid:
        errs.append(
            "segment %d %s recipe PLC %d != actions %d - valid %d"
            % (segment, channel, plc, actions, valid)
        )
    if pre_valid + pre_plc != transients:
        errs.append(
            "segment %d %s startup recipe prefix %d/%d != transients %d"
            % (segment, channel, pre_valid, pre_plc, transients)
        )
    if excluded != plc - pre_plc:
        errs.append(
            "segment %d %s excluded %d != PLC %d - startup PLC %d"
            % (segment, channel, excluded, plc, pre_plc)
        )
    if valid != pre_valid + pushes - excluded:
        errs.append(
            "segment %d %s valid %d != startup valid %d + pushes %d - excluded %d"
            % (segment, channel, valid, pre_valid, pushes, excluded)
        )
    if plc != pre_plc + excluded:
        errs.append(
            "segment %d %s recipe PLC %d != startup PLC %d + excluded %d"
            % (segment, channel, plc, pre_plc, excluded)
        )
    if samples != frames * samples_per_channel:
        errs.append(
            "segment %d %s samples %d != frames %d * spc %d"
            % (segment, channel, samples, frames, samples_per_channel)
        )
    if completion == "full" and (
        actions != recipe["output_action_count"]
        or valid != recipe["valid_frame_count"]
        or plc != recipe["output_action_count"] - recipe["valid_frame_count"]
    ):
        errs.append(
            "segment %d %s full recipe progress is incomplete" % (segment, channel)
        )
    return errs


def _check_pcm_segment(scenario, record, segment, limits, receiver_oracle):
    """Validate one pushed segment's stateful recipe and numerical record."""
    values, errs = _segment_values(record, segment)
    if errs:
        return errs

    expected_left, expected_right, completion = receiver_oracle
    suffix = str(segment)
    pushes = values["pushes" + suffix]
    configured_samples = values["samples" + suffix]
    samples_per_channel = values["spc" + suffix]
    expected_samples_per_channel = _scenario_pcm_geometry(scenario)

    if pushes <= 0:
        errs.append("segment %d pushes %d <= 0" % (segment, pushes))
    if samples_per_channel != expected_samples_per_channel:
        errs.append(
            "segment %d spc %d != manifest duration geometry %d"
            % (segment, samples_per_channel, expected_samples_per_channel)
        )
    if configured_samples != expected_samples_per_channel * 2:
        errs.append(
            "segment %d samples %d != configured stereo geometry %d"
            % (segment, configured_samples, expected_samples_per_channel * 2)
        )

    errs.extend(
        _check_recipe_channel(
            record, segment, values, "left", expected_left, completion
        )
    )
    errs.extend(
        _check_recipe_channel(
            record, segment, values, "right", expected_right, completion
        )
    )

    for channel, frame, sample, excluded, maximum, squared, rms, corr, result in (
        (
            "left",
            values["lfr" + suffix],
            values["lsm" + suffix],
            values["lex" + suffix],
            values["lmax" + suffix],
            values["lsse" + suffix],
            values["lrms" + suffix],
            values["lcorr" + suffix],
            values["lres" + suffix],
        ),
        (
            "right",
            values["rfr" + suffix],
            values["rsm" + suffix],
            values["rex" + suffix],
            values["rmax" + suffix],
            values["rsse" + suffix],
            values["rrms" + suffix],
            values["rcorr" + suffix],
            values["rres" + suffix],
        ),
    ):
        if frame <= 0 or sample <= 0:
            errs.append(
                "segment %d %s compared frame/sample count is zero" % (segment, channel)
            )
        if excluded < 0 or maximum < 0 or squared < 0 or rms < 0:
            errs.append("segment %d %s PCM metric is negative" % (segment, channel))
        if corr < -32768 or corr > 32767:
            errs.append(
                "segment %d %s correlation %d outside Q15 range"
                % (segment, channel, corr)
            )
        if result != PCM_ORACLE_RESULT_PASS:
            errs.append(
                "segment %d %s PCM result %d != pass" % (segment, channel, result)
            )
        if maximum > limits["max_abs_error"]:
            errs.append(
                "segment %d %s max %d > limit %d"
                % (segment, channel, maximum, limits["max_abs_error"])
            )
        if rms > limits["max_rms_error"]:
            errs.append(
                "segment %d %s rms %d > limit %d"
                % (segment, channel, rms, limits["max_rms_error"])
            )
        if corr < limits["min_correlation_q15"]:
            errs.append(
                "segment %d %s correlation %d < limit %d"
                % (segment, channel, corr, limits["min_correlation_q15"])
            )

    differing = values["diff" + suffix]
    if differing < 0:
        errs.append(
            "segment %d differing samples %d is negative" % (segment, differing)
        )
    elif SCENARIOS[scenario][1] == "mono":
        if differing != 0:
            errs.append(
                "segment %d mono differing samples %d != 0" % (segment, differing)
            )
    elif differing == 0:
        errs.append("segment %d stereo differing samples == 0" % segment)

    if scenario == "modea_one_cis_loss_10ms" and (
        values["ract" + suffix] != 108
        or values["rval" + suffix] != 82
        or values["rplc" + suffix] != 26
        or values["rex" + suffix] != MODEA_LOSS_COUNT
    ):
        errs.append("one-CIS-loss right recipe progress/exclusion mismatch")
    return errs


def _check_absent_segment(record, segment, label):
    """Absent/no-audio PASS segment must expose only none/zero evidence."""
    values, errs = _segment_values(record, segment)
    if errs:
        return errs

    suffix = str(segment)
    for key in _segment_recipe_id_fields(segment):
        if record[key] != "none":
            errs.append("%s %s %s != none" % (label, key, record[key]))
    for key, value in values.items():
        if key in ("lres" + suffix, "rres" + suffix):
            continue
        if value != 0:
            errs.append("%s %s %d != 0" % (label, key, value))
    if values["lres" + suffix] != PCM_ORACLE_RESULT_INSUFFICIENT_SAMPLES:
        errs.append("%s lres%d != insufficient-samples" % (label, segment))
    if values["rres" + suffix] != PCM_ORACLE_RESULT_INSUFFICIENT_SAMPLES:
        errs.append("%s rres%d != insufficient-samples" % (label, segment))
    return errs


def _check_no_audio_pcm(record):
    """No-audio scenarios expose no recipe action or numerical evidence."""
    return _check_absent_segment(record, 1, "no-audio") + _check_absent_segment(
        record, 2, "no-audio"
    )


def check_scenario(scenario, recv, cli, known):
    """Assert full scenario contract. `known` may override known_total only."""
    if scenario not in SCENARIOS:
        raise ParseError("unknown scenario %s" % scenario)
    dec_calls, _channel_mode, _runs = SCENARIOS[scenario]
    normal_audio = (
        "mono_10ms",
        "mono_7p5ms",
        "modea_10ms",
        "modea_7p5ms",
        "modea_reverse_start_10ms",
        "modeb_10ms",
        "modeb_7p5ms",
        "invalid_sdu_resume_10ms",
        "modea_one_cis_loss_10ms",
    )
    no_audio = (
        "unsupported_source_direction",
        "no_free_sink_slot",
        "invalid_codec_fields",
    )

    r = parse_receiver_pass(recv)
    c = parse_client_pass(cli)
    scan_faults(recv, cli, scenario)
    limits, errs = _check_pcm_limits(r)

    errs.extend(check_transport(scenario, c))
    if r["scenario"] != scenario:
        errs.append("receiver scenario %s != %s" % (r["scenario"], scenario))
    if c["scenario"] != scenario:
        errs.append("client scenario %s != %s" % (c["scenario"], scenario))
    if r["pacs"] != 1:
        errs.append("pacs != 1")

    seg = r["seg"]
    after = r["after"]

    if scenario in normal_audio:
        if seg != 1:
            errs.append("seg %d != 1" % seg)
        if r.get("pushes1") != 100:
            errs.append("pushes1 %s != 100" % r.get("pushes1"))
        if after != 0:
            errs.append("after %d != 0" % after)
        if r.get("mal1") != 0:
            errs.append("mal1 %s != 0" % r.get("mal1"))
        if r.get("adv_restart") != 0:
            errs.append("adv_restart %s != 0" % r.get("adv_restart"))
        if scenario == "invalid_sdu_resume_10ms":
            if r.get("derr1") != 1:
                errs.append("derr1 %s != 1 (malformed SDU)" % r.get("derr1"))
            if r.get("obs_mal") != 1:
                errs.append(
                    "obs_mal %s != 1 (malformed-SDU observer)" % r.get("obs_mal")
                )
        else:
            if r.get("derr1") != 0:
                errs.append("derr1 %s != 0" % r.get("derr1"))
            if r.get("obs_mal") != 0:
                errs.append("obs_mal %s != 0" % r.get("obs_mal"))
        if r.get("obs_gate_c", 0) != 0:
            errs.append(
                "obs_gate_c %s != 0 (normal scenario must not close)"
                % r.get("obs_gate_c")
            )
        if r.get("obs_rel", 0) != 0:
            errs.append(
                "obs_rel %s != 0 (normal scenario has no releases)" % r.get("obs_rel")
            )
        if r.get("obs_rel_ss", 0) != 0:
            errs.append(
                "obs_rel_ss %s != 0 (normal scenario has no release sink-stop)"
                % r.get("obs_rel_ss")
            )
        if scenario == "modea_one_cis_loss_10ms":
            if r.get("plc1", 0) != r.get("splc1", 0) + MODEA_LOSS_COUNT:
                errs.append(
                    "post-start PLC (plc1 %s != splc1 %s + %d)"
                    % (r.get("plc1"), r.get("splc1"), MODEA_LOSS_COUNT)
                )
        elif r.get("plc1", 0) != r.get("splc1", 0):
            errs.append(
                "post-start PLC (plc1 %s != splc1 %s)" % (r.get("plc1"), r.get("splc1"))
            )
        expected_total = dec_calls * (r.get("pushes1", 0) + r.get("trans1", 0))
        if r.get("total1", 0) < expected_total:
            errs.append("total1 %s < %d" % (r.get("total1"), expected_total))
        if r.get("obs_mts", 0) != 0:
            errs.append("obs_mts %d != 0 (missing ISO TS flag)" % r.get("obs_mts"))
        if scenario == "invalid_sdu_resume_10ms":
            if c["sends0"] != 101:
                errs.append("client sends0 %d != 101" % c["sends0"])
        elif scenario in ("modea_10ms", "modea_7p5ms", "modea_reverse_start_10ms"):
            if c["sends0"] != 110 or c["sends1"] != 110:
                errs.append(
                    "client sends %d/%d != 110/110" % (c["sends0"], c["sends1"])
                )
        elif scenario == "modea_one_cis_loss_10ms":
            if c["sends0"] != 110 or c["sends1"] != 110:
                errs.append(
                    "client sends %d/%d != 110/110 (loss scenario)"
                    % (c["sends0"], c["sends1"])
                )
        elif c["sends0"] != 100:
            errs.append("client sends0 %d != 100" % c["sends0"])

    elif scenario == "modea_first_stop_10ms":
        if seg != 1:
            errs.append("seg %d != 1" % seg)
        if r.get("pushes1", 0) < 20:
            errs.append("pushes1 %s < 20" % r.get("pushes1"))
        if after != 0:
            errs.append("after %d != 0" % after)
        if r.get("derr1") != 0:
            errs.append("derr1 %s != 0" % r.get("derr1"))
        if r.get("obs_gate_c", -1) != 1:
            errs.append("obs_gate_c %s != 1" % r.get("obs_gate_c", -1))
        if r.get("obs_rel", -1) != 2:
            errs.append(
                "obs_rel %s != 2 (both Mode A slots cleaned)" % r.get("obs_rel", -1)
            )
        if r.get("obs_rel_ss", -1) != 0:
            errs.append(
                "obs_rel_ss %s != 0 (release must not re-close the gate)"
                % r.get("obs_rel_ss", -1)
            )
        if r.get("obs_blk", 0) < 1:
            errs.append("obs_blk < 1 (closed-gate receives expected)")
        if c["sends0"] < 20 or c["sends1"] < 45:
            errs.append("client sends %d/%d unexpected" % (c["sends0"], c["sends1"]))
        if c["relrsps"] < 2:
            errs.append("client release responses %d < 2" % c["relrsps"])

    elif scenario == "release_without_disable_10ms":
        if seg != 1:
            errs.append("seg %d != 1" % seg)
        if r.get("pushes1", 0) < 20:
            errs.append("pushes1 %s < 20" % r.get("pushes1"))
        if after != 0:
            errs.append("after %d != 0" % after)
        if r.get("derr1") != 0:
            errs.append("derr1 %s != 0" % r.get("derr1"))
        if r.get("obs_gate_c", -1) != 1:
            errs.append("obs_gate_c %s != 1" % r.get("obs_gate_c", -1))
        if r.get("obs_rel", -1) != 1:
            errs.append("obs_rel %s != 1" % r.get("obs_rel", -1))
        if r.get("obs_rel_ss", -1) != 1:
            errs.append(
                "obs_rel_ss %s != 1 (release must stop the sink)"
                % r.get("obs_rel_ss", -1)
            )
        if r.get("rel_ss_seq", -1) >= r.get("disc_seq", -1):
            errs.append(
                "release sink-stop seq %s not before disconnect seq %s"
                % (r.get("rel_ss_seq", -1), r.get("disc_seq", -1))
            )
        if r.get("obs_disc", -1) != 1:
            errs.append("obs_disc %s != 1 (disconnect cleanup)" % r.get("obs_disc", -1))
        if c["sends0"] < 20:
            errs.append("client sends0 %d < 20" % c["sends0"])
        if c["relrsps"] != 1:
            errs.append("client release responses %d != 1" % c["relrsps"])

    elif scenario == "disconnect_streaming_10ms":
        if seg != 1:
            errs.append("seg %d != 1" % seg)
        if r.get("pushes1", 0) < 20:
            errs.append("pushes1 %s < 20" % r.get("pushes1"))
        if after != 0:
            errs.append("after %d != 0 (late pushes after disconnect)" % after)
        if r.get("derr1") != 0:
            errs.append("derr1 %s != 0" % r.get("derr1"))
        if r.get("obs_gate_c", -1) != 1:
            errs.append("obs_gate_c %s != 1" % r.get("obs_gate_c", -1))
        if r.get("obs_disc", -1) != 1:
            errs.append("obs_disc %s != 1 (disconnect cleanup)" % r.get("obs_disc", -1))
        if r.get("obs_rel", 0) != 0:
            errs.append("obs_rel %s != 0 (no release expected)" % r.get("obs_rel"))
        if r.get("adv_restart") != 1:
            errs.append("adv_restart != 1 (advertising restart path)")
        if c["sends0"] < 20:
            errs.append("client sends0 %d < 20" % c["sends0"])

    elif scenario == "reconnect_second_stream_10ms":
        if seg != 2:
            errs.append("seg %d != 2" % seg)
        if r.get("pushes1", 0) < 20:
            errs.append("pushes1 %s < 20" % r.get("pushes1"))
        if r.get("pushes2") != 100:
            errs.append("pushes2 %s != 100" % r.get("pushes2"))
        if after != 0:
            errs.append("after %d != 0" % after)
        if r.get("obs_gate_c", -1) != 1:
            errs.append(
                "obs_gate_c %s != 1 (session-1 disconnect close)"
                % r.get("obs_gate_c", -1)
            )
        if r.get("obs_disc", -1) != 1:
            errs.append("obs_disc %s != 1 (disconnect cleanup)" % r.get("obs_disc", -1))
        if r.get("obs_rel", 0) != 0:
            errs.append("obs_rel %s != 0 (no release expected)" % r.get("obs_rel"))
        if r.get("adv_restart") != 1:
            errs.append("adv_restart != 1")
        if r.get("derr2") != 0:
            errs.append("derr2 %s != 0" % r.get("derr2"))
        expected_total = r.get("pushes2", 0) + r.get("trans2", 0)
        if r.get("total2") != expected_total:
            errs.append("total2 %s != %d" % (r.get("total2"), expected_total))
        if c["sends1"] != 100:
            errs.append("client session-2 sends %d != 100" % c["sends1"])

    elif scenario == "duplicate_release_10ms":
        if seg != 1:
            errs.append("seg %d != 1" % seg)
        if r.get("pushes1", 0) < 20:
            errs.append("pushes1 %s < 20" % r.get("pushes1"))
        if after != 0:
            errs.append("after %d != 0" % after)
        if r.get("derr1") != 0:
            errs.append("derr1 %s != 0" % r.get("derr1"))
        if r.get("obs_gate_c", -1) != 1:
            errs.append("obs_gate_c %s != 1" % r.get("obs_gate_c", -1))
        if r.get("obs_rel_ss", -1) != 1:
            errs.append("obs_rel_ss %s != 1" % r.get("obs_rel_ss", -1))
        if r.get("obs_rel", -1) != 2:
            errs.append(
                "obs_rel %s != 2 (duplicate release must not re-clean)"
                % r.get("obs_rel", -1)
            )
        if r.get("obs_disc", 0) < 1:
            errs.append("obs_disc < 1 (disconnect cleanup expected)")
        if r.get("obs_gate_o", 0) != 1:
            errs.append("obs_gate_o %s != 1" % r.get("obs_gate_o"))
        expected_total = r.get("pushes1", 0) + r.get("trans1", 0)
        if r.get("total1", 0) < expected_total:
            errs.append("total1 %s < %d" % (r.get("total1"), expected_total))
        if c["sends0"] < 20:
            errs.append("client sends0 %d < 20" % c["sends0"])
        if c["cfgrsps"] != 2:
            errs.append(
                "client config responses %d != 2 (initial + slot reuse)" % c["cfgrsps"]
            )
        if c["relrsps"] != 3:
            errs.append(
                "client release responses %d != 3 (first + rejected duplicate + reuse)"
                % c["relrsps"]
            )

    elif scenario == "unsupported_source_direction":
        if seg != 0:
            errs.append("seg %d != 0 (no audio expected)" % seg)
        if r.get("pushes1", 0) != 0:
            errs.append("pushes1 %s != 0 (no audio expected)" % r.get("pushes1"))
        if r.get("obs_rej") != 1:
            errs.append("obs_rej %d != 1" % r.get("obs_rej"))
        if r.get("obs_ok") != 0:
            errs.append("obs_ok %d != 0" % r.get("obs_ok"))
        if r.get("obs_dir") != 2:
            errs.append("obs_dir %d != SOURCE(2)" % r.get("obs_dir"))
        if r.get("obs_code") != 0x07:
            errs.append("obs_code 0x%02X != CONF_UNSUPPORTED" % r.get("obs_code"))
        if r.get("obs_reason") != 0:
            errs.append("obs_reason %d != NONE" % r.get("obs_reason"))
        if r.get("obs_gate_c", 0) != 0:
            errs.append("obs_gate_c %s != 0 (no gate close)" % r.get("obs_gate_c"))
        if r.get("obs_rel", 0) != 0:
            errs.append("obs_rel %s != 0 (no release)" % r.get("obs_rel"))
        if c["cfgrsps"] != 1:
            errs.append("client config responses %d != 1" % c["cfgrsps"])
        if c["sends0"] != 0:
            errs.append("client sends0 %d != 0" % c["sends0"])

    elif scenario == "no_free_sink_slot":
        if seg != 0:
            errs.append("seg %d != 0 (no audio expected)" % seg)
        if r.get("pushes1", 0) != 0:
            errs.append("pushes1 %s != 0 (no audio expected)" % r.get("pushes1"))
        if r.get("obs_ok", 0) < 3:
            errs.append("obs_ok %d < 3 (2 initial + 1 reuse)" % r.get("obs_ok"))
        if r.get("obs_rej") != 1:
            errs.append("obs_rej %d != 1" % r.get("obs_rej"))
        if r.get("obs_rej_code", -1) != 0x0D:
            errs.append("obs_rej_code 0x%02X != NO_MEM" % r.get("obs_rej_code", -1))
        if r.get("obs_mts", 0) != 0:
            errs.append("obs_mts %d != 0" % r.get("obs_mts"))
        if r.get("obs_rej_reason", -1) != 0:
            errs.append("obs_rej_reason %d != NONE" % r.get("obs_rej_reason", -1))
        if r.get("obs_rel", -1) != 3:
            errs.append("obs_rel %s != 3 (clean releases)" % r.get("obs_rel", -1))
        if r.get("obs_gate_c", 0) != 0:
            errs.append("obs_gate_c %s != 0 (never streamed)" % r.get("obs_gate_c"))
        if r.get("obs_rel_ss", 0) != 0:
            errs.append("obs_rel_ss %s != 0 (no edge release)" % r.get("obs_rel_ss"))
        if c["cfgrsps"] != 4:
            errs.append("client config responses %d != 4" % c["cfgrsps"])
        if c["relrsps"] != 3:
            errs.append("client release responses %d != 3" % c["relrsps"])

    elif scenario == "invalid_codec_fields":
        if seg != 0:
            errs.append("seg %d != 0 (no audio expected)" % seg)
        if r.get("pushes1", 0) != 0:
            errs.append("pushes1 %s != 0 (no audio expected)" % r.get("pushes1"))
        if r.get("obs_rej", 0) < 9:
            errs.append("obs_rej %d < 9" % r.get("obs_rej"))
        if r.get("obs_ok", 0) != 2:
            errs.append("obs_ok %d != 2 (valid mono + fallback)" % r.get("obs_ok"))
        if r.get("obs_rej_code", -1) != 0x08:
            errs.append(
                "obs_rej_code 0x%02X != CONF_REJECTED" % r.get("obs_rej_code", -1)
            )
        if r.get("obs_rej_reason", -1) != 0x02:
            errs.append("obs_rej_reason %d != CODEC_DATA" % r.get("obs_rej_reason", -1))
        if r.get("obs_rel", -1) != 2:
            errs.append(
                "obs_rel %s != 2 (two accepted configs released)" % r.get("obs_rel", -1)
            )
        if r.get("obs_gate_c", 0) != 0:
            errs.append("obs_gate_c %s != 0 (never streamed)" % r.get("obs_gate_c"))
        if r.get("obs_rel_ss", 0) != 0:
            errs.append("obs_rel_ss %s != 0 (no edge release)" % r.get("obs_rel_ss"))
        if c["cfgrsps"] != 11:
            errs.append(
                "client config responses %d != 11 (9 rejects + 2 accepts)"
                % c["cfgrsps"]
            )

    else:
        errs.append("scenario %s not handled" % scenario)

    if "known_total" in known and known["known_total"] is not None:
        if r.get("total1") != known["known_total"]:
            errs.append(
                "total1 %s != pinned %d" % (r.get("total1"), known["known_total"])
            )

    receiver_oracles = RECEIVER_ORACLE_VALUES[scenario]
    if seg != len(receiver_oracles):
        errs.append(
            "seg %d != receiver_oracle segment count %d" % (seg, len(receiver_oracles))
        )
    for segment, receiver_oracle in enumerate(receiver_oracles, start=1):
        errs.extend(_check_pcm_segment(scenario, r, segment, limits, receiver_oracle))
    for segment in range(len(receiver_oracles) + 1, 3):
        errs.extend(_check_absent_segment(r, segment, "absent"))

    return errs


def check(scenario, recv, cli, known):
    errs = check_scenario(scenario, recv, cli, known)
    if errs:
        raise ParseError("%s: %s" % (scenario, "; ".join(errs)))
    return True


# ── CLI ──────────────────────────────────────────────────────────────


def main(argv):
    ap = argparse.ArgumentParser(description="T4 BSim scenario strict check")
    ap.add_argument(
        "check", nargs="?", help="subcommand placeholder (the runner passes 'check')"
    )
    ap.add_argument("--scenario", required=True)
    ap.add_argument("--receiver", required=True)
    ap.add_argument("--client", required=True)
    ap.add_argument("--known-total", default=None)
    ap.add_argument(
        "--no-known",
        action="store_true",
        help="disable versioned known-total assertions",
    )
    args = ap.parse_args(argv)

    # Known-total precedence: --no-known disables all known assertions;
    # otherwise an explicit --known-total overrides versioned data.
    known = {}
    if not args.no_known:
        versioned = known_values(args.scenario)
        if "total" in versioned:
            known["known_total"] = versioned["total"]
        if args.known_total is not None:
            known["known_total"] = int(args.known_total, 0)

    try:
        check(args.scenario, args.receiver, args.client, known)
    except ParseError as exc:
        print("FAIL: %s" % exc, file=sys.stderr)
        return 1

    r = parse_receiver_pass(args.receiver)
    c = parse_client_pass(args.client)
    print(
        "PASS: %s seg=%d pushes1=%s lmax1=%s lrms1=%s lcorr1=%s "
        "rmax1=%s rrms1=%s rcorr1=%s txc0=%d txh0=0x%08X txc1=%d txh1=0x%08X"
        % (
            args.scenario,
            r.get("seg"),
            r.get("pushes1"),
            r.get("lmax1"),
            r.get("lrms1"),
            r.get("lcorr1"),
            r.get("rmax1"),
            r.get("rrms1"),
            r.get("rcorr1"),
            c["txc0"],
            c["txh0"],
            c["txc1"],
            c["txh1"],
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
