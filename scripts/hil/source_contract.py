"""Read public HIL-source signal constants from authoritative source headers.

Capture analysis deliberately avoids private waveform hashes. Values come from
``hil/source/core/hil_source_types.h``, which is source fixture public contract.
Import fails closed if any required public constant drifts or becomes malformed.
"""

import os
import re
from types import MappingProxyType


class SourceContractError(ValueError):
    """Raised when public source signal constants cannot be read exactly."""


REQUIRED_CONSTANTS = (
    "HIL_SOURCE_SAMPLE_RATE_HZ",
    "HIL_SOURCE_LEFT_CARRIER_HZ",
    "HIL_SOURCE_LEFT_PHASE_STEP",
    "HIL_SOURCE_RIGHT_CARRIER_HZ",
    "HIL_SOURCE_RIGHT_PHASE_STEP",
    "HIL_SOURCE_AMPLITUDE_FULL",
    "HIL_SOURCE_AMPLITUDE_LOW",
    "HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES",
    "HIL_SOURCE_TRANSITION_RAMP_SAMPLES",
    "HIL_SOURCE_PREAMBLE_SEGMENT_SAMPLES",
    "HIL_SOURCE_PREAMBLE_SEGMENTS",
    "HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES",
    "HIL_SOURCE_TAIL_MIN_US",
    "HIL_SOURCE_DEFAULT_SIGNAL_SEED",
    "HIL_SOURCE_LEFT_PRNG_MASK",
    "HIL_SOURCE_RIGHT_PRNG_MASK",
    "HIL_SOURCE_PRNG_ZERO_REPLACEMENT",
)
SOURCE_SIGNAL_PATH = os.path.join("hil", "source", "core", "hil_source_signal.c")
DEFINE_RE = re.compile(r"^\s*#define\s+(HIL_SOURCE_[A-Z0-9_]+)\s+([^/\s]+)")


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def header_path():
    return os.path.join(repo_root(), "hil", "source", "core", "hil_source_types.h")


def signal_path():
    return os.path.join(repo_root(), SOURCE_SIGNAL_PATH)


def _parse_uint(text, name):
    value = text.rstrip("ULul")
    try:
        parsed = int(value, 0)
    except ValueError as exc:
        raise SourceContractError(
            "invalid public source constant %s=%r" % (name, text)
        ) from exc
    if parsed < 0 or parsed > 0xFFFFFFFF:
        raise SourceContractError(
            "public source constant out of uint32 range: %s" % name
        )
    return parsed


def load_public_constants(path=None):
    """Load all required source constants from the checked-in public header."""
    source_path = path if path is not None else header_path()
    try:
        with open(source_path, "r", encoding="utf-8") as fh:
            lines = fh.readlines()
    except OSError as exc:
        raise SourceContractError(
            "cannot read source contract %s: %s" % (source_path, exc)
        ) from None
    values = {}
    for line in lines:
        match = DEFINE_RE.match(line)
        if match is None:
            continue
        name, raw = match.groups()
        if name in REQUIRED_CONSTANTS:
            if name in values:
                raise SourceContractError("duplicate public source constant %s" % name)
            values[name] = _parse_uint(raw, name)
    missing = [name for name in REQUIRED_CONSTANTS if name not in values]
    if missing:
        raise SourceContractError("missing public source constant %s" % missing[0])
    if (
        values["HIL_SOURCE_PREAMBLE_SEGMENT_SAMPLES"]
        * values["HIL_SOURCE_PREAMBLE_SEGMENTS"]
        != values["HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES"]
    ):
        raise SourceContractError("public preamble constants are inconsistent")
    return MappingProxyType(values)


def verify_public_signal_contract():
    """Fail closed unless public generator retains documented signal behavior."""
    source_path = signal_path()
    try:
        with open(source_path, "r", encoding="utf-8") as fh:
            source = fh.read()
    except OSError as exc:
        raise SourceContractError(
            "cannot read public source signal contract %s: %s" % (source_path, exc)
        ) from None
    required_fragments = (
        "(segment == 1U || segment == 5U)",
        "(segment == 3U || segment == 5U)",
        "ch->prng = hil_xorshift32(ch->prng); /* first update */",
        "HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES",
        "HIL_SOURCE_TRANSITION_RAMP_SAMPLES",
        "amp = 0; /* tail: exact PCM zero */",
    )
    for fragment in required_fragments:
        if fragment not in source:
            raise SourceContractError(
                "public source signal contract drift: missing %r" % fragment
            )


PUBLIC_CONSTANTS = load_public_constants()
verify_public_signal_contract()
