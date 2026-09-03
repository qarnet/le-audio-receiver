"""Frozen RH2/RH3 HIL row definitions and row-level invariants.

This module is deliberately host-only.  It owns no serial port, process, or
fixture state.  The runner consumes immutable :class:`RowSpec` values so the
same checked-in contract drives direct row execution, the RH3 healthy matrix,
and fake-lab tests.
"""

from dataclasses import dataclass
from types import MappingProxyType
from typing import Optional


SIGNAL_SEED = 1218649181  # 0x48A31C5D, source HELLO default

MODE_STREAM_COUNTS = {
    "mono": 1,
    "mode_a": 2,
    "mode_b": 1,
}

PROFILE_FRAME_US = {
    "48_4_1": 10000,
    "48_3_1": 7500,
}

PROFILE_PREAMBLE_SDUS = {
    "48_4_1": 144,
    "48_3_1": 192,
}

PROFILE_TAIL_SDUS = {
    "48_4_1": 500,
    "48_3_1": 667,
}


class RowSpecError(ValueError):
    """Raised when a checked-in HIL row violates frozen source contracts."""


@dataclass(frozen=True)
class RowSpec:
    """One source-to-receiver HIL row.

    ``state`` describes pre-row bond preparation. ``fresh`` means both sides
    are cleared through public commands. ``preserved`` requires exactly one
    already-established pair bond on both sides and performs no destructive
    reset. ``fault`` identifies a named injection window, not a warning
    bypass: fault rows still validate all final counters and raw logs.
    """

    name: str
    state: str
    mode: str
    profile: str
    scored_sdu_count: int
    signal_seed: int = SIGNAL_SEED
    reconnect_policy: str = "none"
    fault: Optional[str] = None

    def __post_init__(self):
        if not isinstance(self.name, str) or not self.name:
            raise RowSpecError("row name must be a nonempty string")
        if self.state not in ("fresh", "preserved"):
            raise RowSpecError("row %s state must be fresh or preserved" % self.name)
        if self.mode not in MODE_STREAM_COUNTS:
            raise RowSpecError(
                "row %s has unsupported mode %r" % (self.name, self.mode)
            )
        if self.profile not in PROFILE_FRAME_US:
            raise RowSpecError(
                "row %s has unsupported profile %r" % (self.name, self.profile)
            )
        if (
            not isinstance(self.scored_sdu_count, int)
            or isinstance(self.scored_sdu_count, bool)
            or not 1 <= self.scored_sdu_count <= 20000
        ):
            raise RowSpecError("row %s scored SDUs must be 1..20000" % self.name)
        if (
            not isinstance(self.signal_seed, int)
            or isinstance(self.signal_seed, bool)
            or not 1 <= self.signal_seed <= 0xFFFFFFFF
        ):
            raise RowSpecError(
                "row %s signal seed must be a nonzero uint32" % self.name
            )
        if self.reconnect_policy not in ("none", "once"):
            raise RowSpecError(
                "row %s reconnect policy must be none or once" % self.name
            )
        if self.fault not in (None, "hang", "stall"):
            raise RowSpecError("row %s fault must be hang, stall, or None" % self.name)
        if self.fault is not None and self.profile != "48_4_1":
            raise RowSpecError(
                "row %s fault injection requires 48_4_1 FLPR offload" % self.name
            )

    @property
    def stream_count(self):
        return MODE_STREAM_COUNTS[self.mode]

    @property
    def segment_count(self):
        return 2 if self.reconnect_policy == "once" else 1

    @property
    def expected_scored_per_stream(self):
        return self.scored_sdu_count * self.segment_count

    @property
    def expected_submitted_per_segment(self):
        """Expected source terminal ``sub`` counter per segment.

        Firmware increments ``sub`` for every preamble, scored, and tail SDU
        before it publishes its terminal snapshot. ``sc`` separately proves
        the exact scored target.
        """
        return (
            PROFILE_PREAMBLE_SDUS[self.profile]
            + self.scored_sdu_count
            + PROFILE_TAIL_SDUS[self.profile]
        )

    @property
    def expected_submitted_per_stream(self):
        """Expected source terminal ``sub`` counter per stream.

        Source emits ``scored_complete`` at tail entry, then keeps streaming
        valid tail SDUs until every per-segment cap drains and terminal status
        becomes immutable.
        """
        return self.segment_count * self.expected_submitted_per_segment

    @property
    def minimum_scored_interval_s(self):
        return self.scored_sdu_count * PROFILE_FRAME_US[self.profile] / 1_000_000

    @property
    def healthy(self):
        return self.fault is None


# RH2 is intentionally short.  It remains an orchestration witness and is not
# part of the RH3 duration-acceptance matrix.
RH2_ROW = RowSpec(
    name="rh2.short_mono_48_4_1",
    state="fresh",
    mode="mono",
    profile="48_4_1",
    scored_sdu_count=120,
)


# Mandatory RH3 healthy matrix, frozen directly from
# docs/development/system-hil-milestones.md (standing decision 2026-09-03:
# 10 ms rows only; 7.5 ms is diagnostic-only until RH3-7p5 closes it).
# Fault rows are separate because named recovery windows must never weaken
# healthy-row warning rules.
RH3_HEALTHY_ROWS = (
    RowSpec("rh3.fresh_mono_48_4_1", "fresh", "mono", "48_4_1", 12000),
    RowSpec("rh3.fresh_mode_a_48_4_1", "fresh", "mode_a", "48_4_1", 12000),
    RowSpec("rh3.fresh_mode_b_48_4_1", "fresh", "mode_b", "48_4_1", 12000),
    RowSpec("rh3.preserved_mode_b_48_4_1", "preserved", "mode_b", "48_4_1", 12000),
)

# 7.5 ms diagnostic rows (RH3-7p5 named open question).  Selectable for
# single diagnostic runs but NOT part of the mandatory matrix: hardware
# evidence (H40/H42) shows near-total non-valid delivery at 48_3_1, and the
# standing decision is that 7.5 ms must work or must not be supported.
RH3_7P5_DIAGNOSTIC_ROWS = (
    RowSpec("rh3.fresh_mono_48_3_1", "fresh", "mono", "48_3_1", 16000),
    RowSpec("rh3.fresh_mode_a_48_3_1", "fresh", "mode_a", "48_3_1", 16000),
    RowSpec("rh3.fresh_mode_b_48_3_1", "fresh", "mode_b", "48_3_1", 16000),
)


# Explicit follow-on rows.  They are intentionally not included in the
# mandatory healthy matrix returned above: milestone policy requires healthy
# rows to pass before destructive FLPR fault injection starts.
RH3_RECONNECT_ROW = RowSpec(
    "rh3.reconnect_mode_b_48_4_1",
    "preserved",
    "mode_b",
    "48_4_1",
    12000,
    reconnect_policy="once",
)

RH3_HANG_ROW = RowSpec(
    "rh3.flpr_hang_mode_b_48_4_1",
    "preserved",
    "mode_b",
    "48_4_1",
    12000,
    fault="hang",
)

RH3_STALL_ROW = RowSpec(
    "rh3.flpr_stall_mode_b_48_4_1",
    "preserved",
    "mode_b",
    "48_4_1",
    12000,
    fault="stall",
)


# One RH3 pass always proves every healthy row before the preserved-bond
# reconnect and named FLPR recovery rows. This fixed tuple prevents matrix
# callers from adding, removing, or reordering rows.
RH3_PASS_ROWS = RH3_HEALTHY_ROWS + (
    RH3_RECONNECT_ROW,
    RH3_HANG_ROW,
    RH3_STALL_ROW,
)
RH3_PASS_COUNT = 2


def rh3_schedule():
    """Return fixed two-pass ``(pass_index, row_index, row)`` schedule."""
    return tuple(
        (pass_index, row_index, row)
        for pass_index in range(1, RH3_PASS_COUNT + 1)
        for row_index, row in enumerate(RH3_PASS_ROWS, start=1)
    )


ALL_ROWS = (
    RH2_ROW,
    *RH3_HEALTHY_ROWS,
    *RH3_7P5_DIAGNOSTIC_ROWS,
    RH3_RECONNECT_ROW,
    RH3_HANG_ROW,
    RH3_STALL_ROW,
)

ROW_BY_NAME = MappingProxyType({row.name: row for row in ALL_ROWS})

if len(ROW_BY_NAME) != len(ALL_ROWS):
    raise RowSpecError("checked-in HIL row names must be unique")


def get_row(name):
    """Return one checked-in row by its stable evidence/JUnit name."""
    try:
        return ROW_BY_NAME[name]
    except (KeyError, TypeError) as exc:
        raise RowSpecError("unknown HIL row %r" % (name,)) from exc


def row_names():
    """Return every valid direct-run row name in deterministic order."""
    return tuple(row.name for row in ALL_ROWS)


def rh3_rows(include_faults=False):
    """Return mandatory RH3 rows, optionally followed by recovery rows."""
    if include_faults:
        return RH3_PASS_ROWS
    return RH3_HEALTHY_ROWS
