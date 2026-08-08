#!/usr/bin/env python3
"""Shared FLPR offload-status parsing for the stall and hang gates (stdlib).

Single source of truth for the console grammar the two hardware gates parse:
``flpr offload`` status lines (state/counters/recovery/probation/faults),
the runtime-restart line, and the recovery-OK marker.  Both
``scripts/flpr_stall_gate.py`` and ``scripts/flpr_hang_gate.py`` import
this module instead of re-defining the regexes.

``parse_offload_status`` returns one **superset** dict covering the hang
schema (epoch/gen/runtime/heartbeat-dedup included); the stall gate
consumes the fields it needs and ignores the rest.  Command-specific ACK
regexes (timed-stall ACK, FAULT_HANG ACK/fail, ``flpr restart`` OK line)
and the per-gate state machines deliberately stay in their own scripts.
"""

import re

# ── Offload status block grammar (shared) ──────────────────────────────
# Preserved exactly from the pre-R3 gate scripts; the hang variant also
# captures epoch/gen for the epoch-change gate.

RE_STATE_LINE = re.compile(r"State\s*:\s*(\w+)\s*/\s*epoch=(\d+)\s+gen=(\d+)")
RE_COUNTERS = re.compile(
    r"Counters\s*:\s*submit=(\d+)\s+success=(\d+)\s+fallback=(\d+)\s+busy=(\d+)"
)
RE_RECOVERY = re.compile(
    r"Recovery\s*:\s*attempts=(\d+)\s+fail=(\d+)\s+relapses=(\d+)\s+exhaustion=(\d+)"
)
RE_PROBATION = re.compile(
    r"Probation\s*:\s*active=(\d+)\s+success=(\d+)\s+cleared=(\d+)"
)
RE_FAULTS = re.compile(
    r"Faults\s*:\s*timeout=(\d+)\s+full=(\d+)\s+stale=(\d+)\s+seq=(\d+)\s+frame=(\d+)\s+crc=(\d+)\s+payload=(\d+)"
)
RE_RUNTIME = re.compile(
    r"Runtime\s*:\s*restarts=(\d+)\s+fails=(\d+)\s+last_ms=(\d+)\s+remote_epoch=(\d+)"
)
RE_HB_DEDUP = re.compile(r"HB dedup\s*:\s*(\d+)")
RE_RECOVERY_OK = re.compile(r"offload recovery OK:")

# ── Audio-status / audio-perf fault grammar (both gates consume this) ────
# src/audio_shell.c `audio status` prints "Decode errors", "I2S underruns",
# "Stream resets"; `audio perf` prints "Push failures" under its Queue
# section.  A field value is captured only when it parses as a number —
# malformed output (e.g. "I2S underruns : N/A") leaves the field absent
# (None), which gates must treat as missing evidence, never as zero.
RE_AUDIO_STATUS_MARKER = re.compile(r"^--- Audio status ---", re.MULTILINE)
RE_AUDIO_FAULT_FIELD = re.compile(
    r"^\s*(Decode errors|I2S underruns|Stream resets|Push failures)\s*:\s*(\d+)",
    re.MULTILINE,
)
AUDIO_FAULT_FIELDS = (
    "decode_errors",
    "i2s_underruns",
    "stream_resets",
    "push_failures",
)
_AUDIO_FIELD_LABELS = {
    "Decode errors": "decode_errors",
    "I2S underruns": "i2s_underruns",
    "Stream resets": "stream_resets",
    "Push failures": "push_failures",
}


def parse_audio_faults(text):
    """Parse audio fault fields from ``audio status`` / ``audio perf`` output.

    Returns ``{"status_seen": bool, "decode_errors": int|None, ...}`` with
    each fault field set to its LAST occurrence in ``text`` (matching the
    gates' last-block convention — an earlier stale block never overrides a
    fresh one).  A field whose line is absent or unparseable stays None,
    which a gate must reject as missing evidence rather than read as zero.
    """
    result: dict = {"status_seen": bool(RE_AUDIO_STATUS_MARKER.search(text))}
    for field in AUDIO_FAULT_FIELDS:
        result[field] = None
    for m in RE_AUDIO_FAULT_FIELD.finditer(text):
        result[_AUDIO_FIELD_LABELS[m.group(1)]] = int(m.group(2))
    return result


# Superset field order (hang schema; stall consumes a subset).
STATUS_FIELDS = (
    "state",
    "epoch",
    "gen",
    "submit",
    "success",
    "fallback",
    "busy",
    "recovery_attempts",
    "recovery_fail",
    "relapses",
    "exhaustion",
    "probation_active",
    "probation_success",
    "probation_cleared",
    "fault_timeout",
    "fault_full",
    "fault_stale",
    "fault_seq",
    "fault_frame",
    "fault_crc",
    "fault_payload",
    "runtime_restarts",
    "runtime_fails",
    "runtime_last_ms",
    "remote_epoch",
    "hb_dedup",
)


def empty_status():
    """Superset status dict with -1/None sentinels (same shape as before)."""
    result: dict = {field: -1 for field in STATUS_FIELDS}
    result["state"] = None
    return result


def parse_offload_status(text):
    """Parse ``flpr offload`` status output into the superset dict.

    Every field defaults to -1 (state: None) when its line is absent —
    the same sentinel semantics the stall and hang gates relied on.
    """
    result: dict = empty_status()
    m = RE_STATE_LINE.search(text)
    if m:
        result["state"] = m.group(1)
        result["epoch"] = int(m.group(2))
        result["gen"] = int(m.group(3))
    m = RE_COUNTERS.search(text)
    if m:
        result["submit"] = int(m.group(1))
        result["success"] = int(m.group(2))
        result["fallback"] = int(m.group(3))
        result["busy"] = int(m.group(4))
    m = RE_RECOVERY.search(text)
    if m:
        result["recovery_attempts"] = int(m.group(1))
        result["recovery_fail"] = int(m.group(2))
        result["relapses"] = int(m.group(3))
        result["exhaustion"] = int(m.group(4))
    m = RE_PROBATION.search(text)
    if m:
        result["probation_active"] = int(m.group(1))
        result["probation_success"] = int(m.group(2))
        result["probation_cleared"] = int(m.group(3))
    m = RE_FAULTS.search(text)
    if m:
        result["fault_timeout"] = int(m.group(1))
        result["fault_full"] = int(m.group(2))
        result["fault_stale"] = int(m.group(3))
        result["fault_seq"] = int(m.group(4))
        result["fault_frame"] = int(m.group(5))
        result["fault_crc"] = int(m.group(6))
        result["fault_payload"] = int(m.group(7))
    m = RE_RUNTIME.search(text)
    if m:
        result["runtime_restarts"] = int(m.group(1))
        result["runtime_fails"] = int(m.group(2))
        result["runtime_last_ms"] = int(m.group(3))
        result["remote_epoch"] = int(m.group(4))
    m = RE_HB_DEDUP.search(text)
    if m:
        result["hb_dedup"] = int(m.group(1))
    return result
