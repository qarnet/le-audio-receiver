"""HIL1 source-record protocol parser and run-state tracker (RH0).

Owns protocol syntax and state with no serial I/O.  Raw source records are
one UTF-8 line beginning ``HIL1 `` followed by one JSON object.  Unprefixed
lines return None so warning scanning can own them; a malformed prefixed line
raises ``HilProtocolError`` and is never treated as a normal log line.
"""

import json
from dataclasses import dataclass

PROTOCOL_VERSION = 1

KINDS = ("ack", "state", "terminal", "status")

#: Exact ordered lifecycle states within one run segment.
STATES = (
    "idle",
    "configured",
    "connecting",
    "secured",
    "discovered",
    "qos",
    "streaming",
    "scored_complete",
    "teardown",
)

TERMINAL_VERDICTS = ("pass", "fail")

#: Legal causes for the bounded abort-to-teardown state record.
ABORT_CAUSES = ("stop", "timeout", "error")

REQUIRED_KEYS = (
    "protocol_version",
    "kind",
    "firmware_id",
    "monotonic_ms",
    "command_id",
    "run_id",
    "segment",
    "data",
)


class HilProtocolError(ValueError):
    """Raised for any malformed HIL1 record or invalid protocol transition."""


class _DuplicateKeyError(ValueError):
    """Internal: duplicate JSON object key (carries the offending key)."""

    def __init__(self, key):
        super().__init__(key)
        self.key = key


@dataclass(frozen=True)
class HilRecord:
    """One validated HIL1 record."""

    protocol_version: int
    kind: str
    firmware_id: str
    monotonic_ms: int
    command_id: str
    run_id: str
    segment: int
    data: dict


def _reject_duplicate_keys(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise _DuplicateKeyError(key)
        out[key] = value
    return out


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def parse_hil1_line(line):
    """Parse one raw line.

    Returns a ``HilRecord`` for a well-formed ``HIL1 `` prefixed line, None
    for an unprefixed line (the caller's warning scanner owns those), and
    raises ``HilProtocolError`` for a malformed prefixed line.
    """
    if isinstance(line, bytes):
        if not line.startswith(b"HIL1 "):
            return None
        try:
            line = line.decode("utf-8")
        except UnicodeDecodeError:
            raise HilProtocolError("HIL1 line is not valid UTF-8") from None
    if not isinstance(line, str):
        raise HilProtocolError("HIL1 line must be text")
    if not line.startswith("HIL1 "):
        return None
    body = line[len("HIL1 ") :]
    if not body.strip():
        raise HilProtocolError("empty HIL1 JSON object")
    try:
        obj = json.loads(body, object_pairs_hook=_reject_duplicate_keys)
    except _DuplicateKeyError as exc:
        raise HilProtocolError("duplicate key %r in HIL1 object" % exc.key) from None
    except json.JSONDecodeError as exc:
        raise HilProtocolError("invalid HIL1 JSON: %s" % exc) from None
    if not isinstance(obj, dict):
        raise HilProtocolError("HIL1 object must be a JSON object")
    for key in obj:
        if key not in REQUIRED_KEYS:
            raise HilProtocolError("unknown key %r in HIL1 object" % key)
    for key in REQUIRED_KEYS:
        if key not in obj:
            raise HilProtocolError("missing key %r in HIL1 object" % key)

    protocol_version = obj["protocol_version"]
    if not _is_int(protocol_version) or protocol_version != PROTOCOL_VERSION:
        raise HilProtocolError("protocol_version must be %d" % PROTOCOL_VERSION)
    kind = obj["kind"]
    if not isinstance(kind, str) or kind not in KINDS:
        raise HilProtocolError("unknown HIL1 kind %r" % (kind,))
    firmware_id = obj["firmware_id"]
    if not isinstance(firmware_id, str) or not firmware_id:
        raise HilProtocolError("firmware_id must be a nonempty string")
    command_id = obj["command_id"]
    if not isinstance(command_id, str) or not command_id:
        raise HilProtocolError("command_id must be a nonempty string")
    run_id = obj["run_id"]
    if not isinstance(run_id, str) or not run_id:
        raise HilProtocolError("run_id must be a nonempty string")
    monotonic_ms = obj["monotonic_ms"]
    if not _is_int(monotonic_ms) or monotonic_ms < 0:
        raise HilProtocolError("monotonic_ms must be a nonnegative integer")
    segment = obj["segment"]
    if not _is_int(segment) or segment < 0:
        raise HilProtocolError("segment must be a nonnegative integer")
    data = obj["data"]
    if not isinstance(data, dict):
        raise HilProtocolError("data must be a JSON object")

    if kind == "state":
        keys = set(data.keys())
        if keys == {"state", "cause"}:
            # Bounded abort record: exact teardown state plus a legal cause.
            state = data.get("state")
            if state != "teardown":
                raise HilProtocolError("abort state must be teardown")
            cause = data.get("cause")
            if cause not in ABORT_CAUSES:
                raise HilProtocolError(
                    "abort cause must be stop, timeout or error")
        else:
            # Ordinary state record: exactly the state key.
            if keys != {"state"}:
                raise HilProtocolError(
                    "state record data must contain only the state key")
            state = data.get("state")
            if not isinstance(state, str) or state not in STATES:
                raise HilProtocolError(
                    "unknown state %r in state record" % (state,))
    if kind == "terminal":
        verdict = data.get("verdict")
        if verdict not in TERMINAL_VERDICTS:
            raise HilProtocolError("verdict must be pass or fail in terminal record")
    return HilRecord(
        protocol_version=protocol_version,
        kind=kind,
        firmware_id=firmware_id,
        monotonic_ms=monotonic_ms,
        command_id=command_id,
        run_id=run_id,
        segment=segment,
        data=data,
    )


class HilRunTracker:
    """Tracks one run's protocol state across many HIL1 records.

    ``run_id`` and ``command_id`` are the expected run and the start (async)
    command; asynchronous ``ack``, ``state``, and ``terminal`` records must
    keep the start command ID, while ``status`` is a query response that must
    use a command ID different from the start command ID and never advances
    or rewinds tracked state.

    A bounded abort state record (``{"state":"teardown","cause":...}``) may
    jump the current segment straight to teardown from any accepted
    pre-teardown state; it is terminal for the whole run (no reconnect, no
    pass terminal) and is exposed through ``aborted``/``abort_cause``.
    """

    def __init__(self, run_id, command_id):
        self.run_id = run_id
        self.command_id = command_id
        self.firmware_id = None
        self.segment = None  # current segment number (None before first state)
        self.state_index = -1  # index within STATES of last state record
        self.last_monotonic = -1
        self.terminal = None  # HilRecord once a terminal verdict was accepted
        self.aborted = False  # True once a bounded abort record was accepted
        self.abort_cause = None  # "stop", "timeout", or "error" when aborted

    def accept(self, record):
        """Validate and fold one record into tracked run state.

        Atomic: either the whole transition is committed or
        ``HilProtocolError`` is raised with every tracker field unchanged.
        All validation runs against local proposed values first; the tracker
        is committed once, after every check passed.

        Segment contract: before the first state, ``ack`` and ``status`` must
        use segment 0; once a current segment exists, ``ack``, ``status``,
        and ``terminal`` must use exactly the current segment; state records
        may only move within the current segment or to exactly current+1
        under the teardown/reconnect rules; segment skips and regressions
        fail for every record kind; the terminal segment must equal the
        current segment.  ``status`` is a query response that must use a
        different command ID from the start command and never changes
        lifecycle state.
        """
        if record.run_id != self.run_id:
            raise HilProtocolError("run id mismatch")

        # Firmware and monotonic are part of the proposed transition.
        if self.firmware_id is None:
            proposed_firmware = record.firmware_id
        elif record.firmware_id != self.firmware_id:
            raise HilProtocolError("firmware id mismatch")
        else:
            proposed_firmware = self.firmware_id
        if record.monotonic_ms < self.last_monotonic:
            raise HilProtocolError("monotonic time regression")
        proposed_monotonic = record.monotonic_ms

        # Segment contract applies to every record kind.
        if self.segment is None:
            if record.kind in ("ack", "status") and record.segment != 0:
                raise HilProtocolError("segment must be 0 before first state")
            if record.kind == "terminal":
                raise HilProtocolError("terminal before teardown")
        elif record.kind in ("ack", "status", "terminal"):
            if record.segment != self.segment:
                if record.segment < self.segment:
                    raise HilProtocolError("segment regression")
                raise HilProtocolError("segment skip")

        if record.kind in ("ack", "state", "terminal"):
            if record.command_id != self.command_id:
                raise HilProtocolError("command id mismatch")
        elif record.kind == "status":
            if record.command_id == self.command_id:
                raise HilProtocolError("status must use a different command id")

        if self.terminal is not None:
            if record.kind == "terminal":
                raise HilProtocolError("duplicate terminal")
            if record.kind in ("ack", "state"):
                raise HilProtocolError("post-terminal record")

        proposed_segment = self.segment
        proposed_state_index = self.state_index
        proposed_aborted = self.aborted
        proposed_abort_cause = self.abort_cause
        if record.kind == "state":
            if self._is_abort_record(record):
                (proposed_segment, proposed_state_index, proposed_aborted,
                 proposed_abort_cause) = self._validate_abort(record)
            else:
                proposed_segment, proposed_state_index = self._validate_state(record)
        elif record.kind == "terminal":
            self._validate_terminal(record)
            if self.aborted and record.data.get("verdict") == "pass":
                raise HilProtocolError("pass terminal after abort")

        # Commit once, after every check passed.
        self.firmware_id = proposed_firmware
        self.last_monotonic = proposed_monotonic
        if record.kind == "state":
            self.segment = proposed_segment
            self.state_index = proposed_state_index
            if self._is_abort_record(record):
                self.aborted = proposed_aborted
                self.abort_cause = proposed_abort_cause
        elif record.kind == "terminal":
            self.terminal = record

    @staticmethod
    def _is_abort_record(record):
        """True when the state record carries the abort shape (exact state
        plus cause keys); parse_hil1_line already forced teardown + a legal
        cause for this shape."""
        return set(record.data.keys()) == {"state", "cause"}

    def _validate_abort(self, record):
        """Validate a bounded abort-to-teardown record without mutating.

        Returns the proposed ``(segment, state_index, aborted, abort_cause)``.
        Legal only after the current segment accepted at least its initial
        state, before that segment reached teardown, on the current segment
        and start command ID.  Abort is terminal for the whole run.
        """
        cause = record.data["cause"]
        if self.segment is None:
            raise HilProtocolError("abort before segment start")
        if record.segment != self.segment:
            if record.segment < self.segment:
                raise HilProtocolError("segment regression")
            raise HilProtocolError("segment skip")
        if self.state_index == len(STATES) - 1:
            raise HilProtocolError("abort after teardown")
        if self.aborted:
            raise HilProtocolError("repeat abort")
        return self.segment, len(STATES) - 1, True, cause

    def _validate_state(self, record):
        """Validate a state transition without mutating tracker state.

        Returns the proposed ``(segment, state_index)``.  A state record may
        advance within the current segment, or start the next segment with
        ``connecting`` after the previous segment reached ``teardown``.
        A new segment is rejected after a bounded abort: abort is terminal
        for the whole run.
        """
        state = record.data["state"]
        index = STATES.index(state)
        if self.segment is None:
            if record.segment != 0:
                raise HilProtocolError("segment must start at 0")
            if state != "idle":
                raise HilProtocolError("first state must be idle")
            return 0, index
        if record.segment == self.segment:
            expected = self.state_index + 1
            if index == self.state_index:
                raise HilProtocolError("state repeat")
            if index < self.state_index:
                raise HilProtocolError("state regression")
            if index != expected:
                raise HilProtocolError("state skip")
            return self.segment, index
        if record.segment == self.segment + 1:
            if self.state_index != len(STATES) - 1:
                raise HilProtocolError("new segment before previous segment teardown")
            if self.aborted:
                raise HilProtocolError("reconnect after abort")
            if state != "connecting":
                raise HilProtocolError("new segment must start at connecting")
            return record.segment, index
        if record.segment < self.segment:
            raise HilProtocolError("segment regression")
        raise HilProtocolError("segment skip")

    def _validate_terminal(self, record):
        """Validate a terminal record without mutating tracker state."""
        if self.segment is None or self.state_index != len(STATES) - 1:
            raise HilProtocolError("terminal before teardown")
