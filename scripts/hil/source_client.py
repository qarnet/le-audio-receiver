"""HIL1 source fixture client (RH2).

Owns the source side of one run: monotonically unique command IDs under
one safe run ID, compact ASCII JSON command serialization, synchronous
status-response validation, and ``HilRunTracker``-driven asynchronous run
tracking.  ``start()`` registers a bounded stop/idle cleanup before the
START command is written, requires the ACK quickly, drives the tracker to
``scored_complete``, runs the receiver-tail hook while the source stays
active, and then requires the PASS terminal and the final status snapshot.

The source serial is intentionally quiet (no echo, no prompt); every
decoded line is a HIL1 record or an unprefixed line owned by the warning
scan.  Malformed HIL1 lines, unsolicited records, and duplicate or
mismatched responses fail the row.
"""

import json
import inspect
import re
import time

from hil import protocol, rows
from hil.serial_io import SerialConsoleError

# ── frozen source constants (RH1A) ─────────────────────────────────

FIRMWARE_ID = "le-audio-hil-source-rh1"
PROTOCOL_VERSION = 1
SAMPLE_RATE_HZ = 48000

HELLO_CONSTANTS = {
    "sample_rate": SAMPLE_RATE_HZ,
    "frame_samples_48_3_1": 360,
    "octets_48_3_1": 90,
    "sdu_mono_48_3_1": 90,
    "sdu_modeb_48_3_1": 180,
    "frame_samples_48_4_1": 480,
    "octets_48_4_1": 120,
    "sdu_mono_48_4_1": 120,
    "sdu_modeb_48_4_1": 240,
    "preamble_samples": 69120,
    "tail_frames_48_3_1": 667,
    "tail_frames_48_4_1": 500,
    "left_carrier_hz": 997,
    "right_carrier_hz": 1601,
    "default_seed": 1218649181,  # 0x48A31C5D
}

IDENTITY_RE = re.compile(r"^[0-9A-F]{2}(:[0-9A-F]{2}){5}$")

# Bounded timeouts (seconds).
ACK_TIMEOUT = 5.0
SYNC_TIMEOUT = 15.0
SCORED_TIMEOUT = 90.0
TERMINAL_TIMEOUT = 120.0
STATUS_TIMEOUT = 15.0
STOP_TIMEOUT = 10.0
ABORT_TERMINAL_TIMEOUT = 30.0
IDLE_TIMEOUT = 30.0

#: Maximum source command line length (firmware shell capacity is
#: ``len("hil " + json) < 512``).
MAX_COMMAND_LINE = 512


class SourceClientError(Exception):
    """Raised for any source protocol or lifecycle violation."""


class SourceClientCancelled(Exception):
    """Raised when runner cancellation interrupts a bounded source wait."""


def _invoke_segment_hook(hook, segment):
    """Call a lifecycle hook without breaking RH2's zero-argument API.

    RH3 needs the segment number for reconnect evidence.  RH2 callers already
    supplied simple zero-argument hooks, so inspect the callable's binding
    shape before invoking it rather than catching a ``TypeError`` raised by the
    hook body itself.  A new hook may accept ``segment`` (or ``*args``); a
    legacy hook receives no arguments.
    """
    if hook is None:
        return
    try:
        signature = inspect.signature(hook)
    except (TypeError, ValueError):
        # Builtins and some callable objects do not expose a signature. The
        # row-aware form is the only safe invocation for those objects.
        hook(segment)
        return
    try:
        signature.bind(segment)
    except TypeError:
        try:
            signature.bind()
        except TypeError as exc:
            raise SourceClientError(
                "lifecycle hook must accept zero arguments or one segment argument"
            ) from exc
        hook()
        return
    hook(segment)


def is_diagnostic_record(record):
    """True for firmware diagnostics that never belong to a test run.

    Source startup failures use ``boot/boot`` and malformed shell input uses
    ``parse-error/unbound``.  Neither may enter an active run tracker or be
    ignored merely because it arrived outside a synchronous command wait.
    """
    return (
        record.command_id == "boot"
        and record.run_id == "boot"
        or record.command_id == "parse-error"
        and record.run_id == "unbound"
    )


class SourceSnapshot:
    """Immutable validated hello snapshot."""

    __slots__ = (
        "firmware_id",
        "protocol_version",
        "identity",
        "identity_type",
        "active",
        "state",
        "segment",
        "bond_count",
    )

    def __init__(self, **kwargs):
        for name in self.__slots__:
            setattr(self, name, kwargs[name])

    def __eq__(self, other):
        if not isinstance(other, SourceSnapshot):
            return NotImplemented
        return all(getattr(self, n) == getattr(other, n) for n in self.__slots__)

    def __repr__(self):
        return "SourceSnapshot(identity=%r, type=%r, bond_count=%r, state=%r)" % (
            self.identity,
            self.identity_type,
            self.bond_count,
            self.state,
        )


class SourceClient:
    """One source console session under one safe run ID."""

    def __init__(self, console, run_id, cleanup=None, clock=None, cancel=None):
        self.console = console
        self.run_id = run_id
        self._cleanup = cleanup
        self._clock = clock if clock is not None else time.monotonic
        self._cancel = cancel if cancel is not None else lambda: False
        self._cmd_seq = 0
        self._tracker = None
        self._pending_status = None
        self._ack_seen = False
        self._records = []  # parsed HIL1 records for evidence
        self._stop_cleanup_registered = False
        self._idle_complete = False
        self._prestart_cleanup_registered = False

    # ── evidence ────────────────────────────────────────────────────

    def record_lines(self):
        """Raw decoded lines (HIL1 and unprefixed) for evidence."""
        return list(self._records)

    # ── command serialization ───────────────────────────────────────

    def _next_command_id(self):
        self._cmd_seq += 1
        return "cmd-%04d" % self._cmd_seq

    def _command_line(self, command, command_id, **extra):
        payload = {
            "protocol_version": PROTOCOL_VERSION,
            "command": command,
            "command_id": command_id,
            "run_id": self.run_id,
        }
        payload.update(extra)
        body = json.dumps(
            payload, ensure_ascii=True, separators=(",", ":"), sort_keys=False
        )
        line = "hil " + body
        if len(line) >= MAX_COMMAND_LINE:
            raise SourceClientError(
                "source command line exceeds shell capacity (%d bytes)" % len(line)
            )
        return line

    def _send(self, line):
        self.console.write(line + "\n")

    def _check_cancel(self, enabled=True):
        """Raise promptly for a requested runner cancellation.

        Cleanup passes ``enabled=False`` so a cancellation still sends one
        bounded stop and idle sequence rather than abandoning an active source
        run.
        """
        if enabled and self._cancel():
            raise SourceClientCancelled("cancelled during source operation")

    # ── record dispatch ─────────────────────────────────────────────

    def _next_record(self, timeout, check_cancel=True):
        """Read one line; returns ``(record, line)`` where record is a
        HilRecord or None for an unprefixed line.  A malformed HIL1 line
        fails the row."""
        self._check_cancel(check_cancel)
        line = self.console.next_line(timeout)
        self._check_cancel(check_cancel)
        if line is None:
            return None, None
        self._records.append(line)
        try:
            record = protocol.parse_hil1_line(line)
        except protocol.HilProtocolError as exc:
            raise SourceClientError("malformed HIL1 line: %s" % exc) from exc
        if record is not None and is_diagnostic_record(record):
            raise SourceClientError(
                "source diagnostic HIL1 record: command_id=%r run_id=%r"
                % (record.command_id, record.run_id)
            )
        return record, line

    def _dispatch(self, record):
        """Route one parsed record: the pending status response, the
        tracker, or a failure for unsolicited records."""
        if self._tracker is None:
            raise SourceClientError("source record without an active run")
        if record.kind == "status":
            if (
                self._pending_status is not None
                and record.command_id == self._pending_status
            ):
                self._pending_status = None
                return record
            raise SourceClientError("unsolicited source status record")
        if record.kind == "ack" and record.command_id == self._tracker.command_id:
            self._ack_seen = True
        self._tracker.accept(record)
        return None

    def _require_status_ok(self, record, expected):
        data = record.data
        if not isinstance(data, dict):
            raise SourceClientError("status data is not an object")
        if data.get("command") != expected:
            raise SourceClientError(
                "status command mismatch: expected %r, got %r"
                % (expected, data.get("command"))
            )
        if data.get("ok") is not True:
            raise SourceClientError("status not ok: %r" % data.get("error"))
        if data.get("error") != "ok":
            raise SourceClientError("status error not ok: %r" % data.get("error"))

    def _wait_status(self, command_id, expected, timeout, check_cancel=True):
        """Wait for exactly one matching status record for one synchronous
        command; unrelated records route to the tracker or fail.  Returns
        as soon as the matching response arrives; any later records stay
        queued for the next consumer."""
        got = None
        deadline = self._clock() + timeout
        while got is None:
            self._check_cancel(check_cancel)
            remaining = deadline - self._clock()
            record, _line = self._next_record(
                min(0.1, max(0.05, remaining)), check_cancel=check_cancel
            )
            if record is None:
                if remaining <= 0:
                    break
                continue
            if record.kind == "status" and record.command_id == command_id:
                got = record
                continue
            self._dispatch(record)
        if got is None:
            raise SourceClientError("no status response for command %s" % command_id)
        self._require_status_ok(got, expected)
        return got

    # ── synchronous commands ────────────────────────────────────────

    def _sync_command(self, command, expected, timeout, check_cancel=True, **extra):
        self._check_cancel(check_cancel)
        command_id = self._next_command_id()
        self._send(self._command_line(command, command_id, **extra))
        return self._wait_status(
            command_id, expected, timeout, check_cancel=check_cancel
        )

    def register_prestart_cleanup(self):
        """Register one best-effort idle reset before mutable source setup.

        ``configure`` can succeed before a later receiver/source boundary
        fails, even though START never ran. That state must not leak into a
        later row. This callback is registered before configure and becomes a
        no-op once an explicit successful idle completed. START replaces it
        with its stronger stop/terminal/idle cleanup path.
        """
        if self._cleanup is None or self._prestart_cleanup_registered:
            return
        # The clean-state idle completed before configure. A new configure
        # attempt can mutate source state, so it must not suppress cleanup.
        self._idle_complete = False
        self._cleanup.register("source pre-start idle", self._bounded_prestart_idle)
        self._prestart_cleanup_registered = True

    def _bounded_prestart_idle(self):
        if self._idle_complete or self._tracker is not None:
            return
        try:
            self.idle(check_cancel=False)
        except SourceClientError as exc:
            raise SerialConsoleError(
                "source pre-start idle cleanup failed: %s" % exc
            ) from exc

    def hello(self, timeout=SYNC_TIMEOUT):
        """hello: boot/readiness/identity boundary.  Validates the frozen
        identity and constants and returns an immutable snapshot."""
        record = self._sync_command("hello", "hello", timeout)
        data = record.data
        snapshot = SourceSnapshot(
            firmware_id=data.get("firmware_id"),
            protocol_version=data.get("protocol_version"),
            identity=data.get("identity"),
            identity_type=data.get("identity_type"),
            active=data.get("active"),
            state=data.get("state"),
            segment=data.get("segment"),
            bond_count=data.get("bond_count"),
        )
        self._validate_hello(snapshot, data)
        return snapshot

    def _validate_hello(self, snapshot, data):
        if snapshot.firmware_id != FIRMWARE_ID:
            raise SourceClientError(
                "hello firmware id mismatch: %r" % snapshot.firmware_id
            )
        if snapshot.protocol_version != PROTOCOL_VERSION:
            raise SourceClientError(
                "hello protocol version mismatch: %r" % snapshot.protocol_version
            )
        if snapshot.active is not False or snapshot.state != "idle":
            raise SourceClientError(
                "hello not idle: active=%r state=%r" % (snapshot.active, snapshot.state)
            )
        if snapshot.segment != 0:
            raise SourceClientError("hello segment must be 0")
        identity = snapshot.identity
        if not isinstance(identity, str) or not IDENTITY_RE.match(identity):
            raise SourceClientError("hello identity not canonical: %r" % identity)
        if snapshot.identity_type not in ("public", "random"):
            raise SourceClientError(
                "hello identity type not canonical: %r" % snapshot.identity_type
            )
        for key, expected in HELLO_CONSTANTS.items():
            if data.get(key) != expected:
                raise SourceClientError(
                    "hello constant %r mismatch: expected %r, got %r"
                    % (key, expected, data.get(key))
                )

    def idle(self, timeout=IDLE_TIMEOUT, check_cancel=True):
        """idle: reset run state; exact ok response required."""
        self._sync_command("idle", "idle", timeout, check_cancel=check_cancel)
        self._idle_complete = True

    def unpair(self, peer_address, peer_address_type, timeout=SYNC_TIMEOUT):
        """unpair for one exact receiver identity."""
        self._sync_command(
            "unpair",
            "unpair",
            timeout,
            peer_address=peer_address,
            peer_address_type=peer_address_type,
        )

    def configure(
        self,
        *,
        peer_address,
        peer_address_type,
        mode,
        profile,
        scored_sdu_count,
        signal_seed,
        reconnect_policy,
        timeout=SYNC_TIMEOUT,
    ):
        """configure: exact row parameters."""
        self._sync_command(
            "configure",
            "configure",
            timeout,
            peer_address=peer_address,
            peer_address_type=peer_address_type,
            mode=mode,
            profile=profile,
            scored_sdu_count=scored_sdu_count,
            signal_seed=signal_seed,
            reconnect_policy=reconnect_policy,
        )

    def stop(self, timeout=STOP_TIMEOUT, check_cancel=True):
        """stop: bounded stop request; exact ok response required."""
        self._sync_command("stop", "stop", timeout, check_cancel=check_cancel)

    def query_status(self, timeout=STATUS_TIMEOUT):
        """In-run or post-run status query with a command ID distinct from
        the start command; never advances tracked state."""
        command_id = self._next_command_id()
        self._pending_status = command_id
        self._send(self._command_line("status", command_id))
        deadline = self._clock() + timeout
        while True:
            self._check_cancel()
            remaining = deadline - self._clock()
            record, _line = self._next_record(min(0.1, max(0.05, remaining)))
            if record is None:
                if remaining <= 0:
                    break
                continue
            if record.kind == "status" and record.command_id == command_id:
                self._require_status_ok(record, "status")
                self._pending_status = None
                return record.data
            self._dispatch(record)
        raise SourceClientError("status query timed out")

    # ── start / tracking ────────────────────────────────────────────

    def start(
        self,
        row=None,
        before_start_hook=None,
        scored_complete_hook=None,
        segment_teardown_hook=None,
        streaming_hook=None,
        ack_timeout=ACK_TIMEOUT,
        scored_timeout=None,
        terminal_timeout=None,
    ):
        """Start one run: register bounded stop/idle cleanup before the
        START command, require the ACK quickly, drive the tracker to
        scored_complete, run the hook, then require the PASS terminal and
        the final status snapshot. ``before_start_hook`` runs after cleanup
        registration and before START bytes leave host, so capture ownership
        is live before source audio begins. ``row`` is the immutable checked-in row
        contract used to validate source-visible counters. Returns final
        status data."""
        if row is None:
            row = rows.RH2_ROW
        if scored_timeout is None:
            scored_timeout = row.minimum_scored_interval_s + SCORED_TIMEOUT
        if terminal_timeout is None:
            terminal_timeout = row.minimum_scored_interval_s + TERMINAL_TIMEOUT
        self._check_cancel()
        command_id = self._next_command_id()
        # Clean-state preparation deliberately issues idle before configure.
        # That old idle cannot satisfy cleanup for this new active run.
        self._idle_complete = False
        self._tracker = protocol.HilRunTracker(self.run_id, command_id)
        if self._cleanup is not None and not self._stop_cleanup_registered:
            self._cleanup.register("source stop/idle", self._bounded_stop)
            self._stop_cleanup_registered = True
        _invoke_segment_hook(before_start_hook, 0)
        self._send(self._command_line("start", command_id))
        self._wait_ack(command_id, ack_timeout)
        streaming_index = protocol.STATES.index("streaming")
        scored_complete_index = protocol.STATES.index("scored_complete")
        teardown_index = protocol.STATES.index("teardown")
        for segment in range(row.segment_count):
            self._drive_until(
                lambda t: (
                    (t.segment == segment and t.state_index >= streaming_index)
                    or t.terminal is not None
                ),
                scored_timeout,
            )
            if self._tracker.terminal is not None:
                raise SourceClientError(
                    "run terminal arrived before segment %d streaming" % segment
                )
            _invoke_segment_hook(streaming_hook, segment)
            self._drive_until(
                lambda t: (
                    (t.segment == segment and t.state_index >= scored_complete_index)
                    or t.terminal is not None
                ),
                scored_timeout,
            )
            if self._tracker.terminal is not None:
                raise SourceClientError(
                    "run terminal arrived before segment %d scored_complete" % segment
                )
            _invoke_segment_hook(scored_complete_hook, segment)
            self._drive_until(
                lambda t: (
                    (t.segment == segment and t.state_index >= teardown_index)
                    or t.terminal is not None
                ),
                terminal_timeout,
            )
            if self._tracker.terminal is not None:
                raise SourceClientError(
                    "run terminal arrived before segment %d teardown" % segment
                )
            _invoke_segment_hook(segment_teardown_hook, segment)
            if segment < row.segment_count - 1:
                self._drive_until(
                    lambda t: (
                        (t.segment is not None and t.segment == segment + 1)
                        or t.terminal is not None
                    ),
                    terminal_timeout,
                )
                if self._tracker.terminal is not None:
                    raise SourceClientError(
                        "run terminal arrived before reconnect segment %d"
                        % (segment + 1)
                    )
        self._drive_until(lambda t: t.terminal is not None, terminal_timeout)
        if self._tracker.terminal is None:
            raise SourceClientError("run terminal never arrived")
        if self._tracker.terminal.data.get("verdict") != "pass":
            raise SourceClientError(
                "run terminal verdict is not pass: %r"
                % self._tracker.terminal.data.get("verdict")
            )
        status = self.query_status()
        self._validate_final_status(status, row)
        return status

    def _wait_ack(self, command_id, timeout):
        deadline = self._clock() + timeout
        while not self._ack_seen:
            self._check_cancel()
            remaining = deadline - self._clock()
            record, _line = self._next_record(min(0.1, max(0.05, remaining)))
            if record is None:
                if remaining <= 0:
                    break
                continue
            self._dispatch(record)
        if not self._ack_seen:
            raise SourceClientError("start ACK not received")

    def _drive_until(self, predicate, timeout, check_cancel=True):
        deadline = self._clock() + timeout
        while not predicate(self._tracker):
            self._check_cancel(check_cancel)
            remaining = deadline - self._clock()
            record, _line = self._next_record(
                min(0.1, max(0.05, remaining)), check_cancel=check_cancel
            )
            if record is None:
                if remaining <= 0:
                    break
                continue
            self._dispatch(record)
        if not predicate(self._tracker):
            raise SourceClientError("run state deadline exceeded")

    def _validate_final_status(self, data, row):
        errors = []
        if data.get("active") is not False:
            errors.append("active")
        if data.get("state") != "teardown":
            errors.append("state=%r" % data.get("state"))
        if data.get("aborted") is not False:
            errors.append("aborted")
        if data.get("verdict") != "pass":
            errors.append("verdict=%r" % data.get("verdict"))
        if data.get("first_errno") != 0:
            errors.append("first_errno=%r" % data.get("first_errno"))
        if data.get("mode") != row.mode:
            errors.append("mode=%r" % data.get("mode"))
        if data.get("profile") != row.profile:
            errors.append("profile=%r" % data.get("profile"))
        if data.get("reconnect") != row.reconnect_policy:
            errors.append("reconnect=%r" % data.get("reconnect"))
        if data.get("scored_target") != row.scored_sdu_count:
            errors.append("scored_target=%r" % data.get("scored_target"))
        if data.get("segment") != row.segment_count - 1:
            errors.append("segment=%r" % data.get("segment"))
        if data.get("stream_count") != row.stream_count:
            errors.append("stream_count=%r" % data.get("stream_count"))
        streams = data.get("streams")
        if not isinstance(streams, list) or len(streams) != row.stream_count:
            errors.append("streams shape")
        else:
            for index, stream in enumerate(streams):
                if not isinstance(stream, dict):
                    errors.append("stream[%d] not object" % index)
                    continue
                submitted = stream.get("sub")
                # ``sub`` counts total SDUs. Terminal publication follows
                # every segment's preamble, scored, tail cap, and outstanding
                # drain, so it must equal all emitted SDUs. ``sc`` remains
                # exact scored acceptance evidence below.
                if (
                    not isinstance(submitted, int)
                    or submitted != row.expected_submitted_per_stream
                ):
                    errors.append("stream[%d] submitted=%r" % (index, submitted))
                if stream.get("sc") != row.expected_scored_per_stream:
                    errors.append("stream[%d] scored=%r" % (index, stream.get("sc")))
                if stream.get("sf") != 0:
                    errors.append(
                        "stream[%d] send_failures=%r" % (index, stream.get("sf"))
                    )
                if stream.get("out") != 0:
                    errors.append(
                        "stream[%d] outstanding=%r" % (index, stream.get("out"))
                    )
        if data.get("connected") is not False:
            errors.append("connected")
        if data.get("group") is not False:
            errors.append("group")
        # Terminal status is rendered after source cleanup. The production
        # BAP backend clears connection-scoped security state and discovered
        # sink endpoints in reset_segment(), so active-stream evidence owns
        # L2/ASE validation while terminal evidence proves clean release.
        if data.get("security_level") != 0:
            errors.append("security_level=%r" % data.get("security_level"))
        if data.get("security_error") != 0:
            errors.append("security_error=%r" % data.get("security_error"))
        sink_ase_count = data.get("sink_ase_count")
        if sink_ase_count != 0:
            errors.append("sink_ase_count=%r" % sink_ase_count)
        if data.get("bond_count") != 1:
            errors.append("bond_count=%r" % data.get("bond_count"))
        if errors:
            raise SourceClientError(
                "final status snapshot invalid: %s" % "; ".join(errors)
            )

    def validate_active_status(self, data, row, segment):
        """Validate source-visible active-stream shape before a row continues.

        The final snapshot correctly reports zero discovered endpoints because
        source cleanup clears its per-segment endpoint cache. This active
        snapshot is therefore the evidence boundary for expected selected
        streams and discovered sink ASE availability.
        """
        errors = []
        if data.get("active") is not True:
            errors.append("active=%r" % data.get("active"))
        if data.get("state") != "streaming":
            errors.append("state=%r" % data.get("state"))
        if data.get("segment") != segment:
            errors.append("segment=%r" % data.get("segment"))
        if data.get("aborted") is not False:
            errors.append("aborted=%r" % data.get("aborted"))
        if data.get("verdict") != "none":
            errors.append("verdict=%r" % data.get("verdict"))
        if data.get("first_errno") != 0:
            errors.append("first_errno=%r" % data.get("first_errno"))
        if data.get("mode") != row.mode:
            errors.append("mode=%r" % data.get("mode"))
        if data.get("profile") != row.profile:
            errors.append("profile=%r" % data.get("profile"))
        if data.get("reconnect") != row.reconnect_policy:
            errors.append("reconnect=%r" % data.get("reconnect"))
        if data.get("scored_target") != row.scored_sdu_count:
            errors.append("scored_target=%r" % data.get("scored_target"))
        if data.get("stream_count") != row.stream_count:
            errors.append("stream_count=%r" % data.get("stream_count"))
        streams = data.get("streams")
        if not isinstance(streams, list) or len(streams) != row.stream_count:
            errors.append("streams shape")
        else:
            for index, stream in enumerate(streams):
                if not isinstance(stream, dict):
                    errors.append("stream[%d] not object" % index)
                    continue
                if stream.get("sf") != 0:
                    errors.append(
                        "stream[%d] send_failures=%r" % (index, stream.get("sf"))
                    )
                if not isinstance(stream.get("sub"), int) or stream["sub"] < 0:
                    errors.append(
                        "stream[%d] submitted=%r" % (index, stream.get("sub"))
                    )
                if not isinstance(stream.get("sc"), int) or stream["sc"] < 0:
                    errors.append("stream[%d] scored=%r" % (index, stream.get("sc")))
                outstanding = stream.get("out")
                if not isinstance(outstanding, int) or outstanding < 0:
                    errors.append("stream[%d] outstanding=%r" % (index, outstanding))
                callbacks = stream.get("cb")
                if not isinstance(callbacks, int) or callbacks < 0:
                    errors.append("stream[%d] callbacks=%r" % (index, callbacks))
        if data.get("connected") is not True:
            errors.append("connected=%r" % data.get("connected"))
        if data.get("group") is not True:
            errors.append("group=%r" % data.get("group"))
        if (
            not isinstance(data.get("security_level"), int)
            or data.get("security_level") < 2
        ):
            errors.append("security_level=%r" % data.get("security_level"))
        if data.get("security_error") != 0:
            errors.append("security_error=%r" % data.get("security_error"))
        sink_ase_count = data.get("sink_ase_count")
        if not isinstance(sink_ase_count, int) or sink_ase_count < row.stream_count:
            errors.append("sink_ase_count=%r" % sink_ase_count)
        if data.get("bond_count") != 1:
            errors.append("bond_count=%r" % data.get("bond_count"))
        if errors:
            raise SourceClientError(
                "active status snapshot invalid: %s" % "; ".join(errors)
            )

    # ── bounded cleanup ─────────────────────────────────────────────

    def _bounded_stop(self):
        """Cleanup registered before START: stop once, wait bounded for
        the error-abort terminal, then idle.  Any failure is re-raised so
        the runner changes the row verdict to failed."""
        if self._tracker is None or self._idle_complete:
            return
        self._drain_queued_records()
        # A normal terminal may have arrived before a later row boundary
        # failed. It is already inactive, so do not issue an invalid STOP;
        # reset it directly with idle. An unfinished run needs bounded stop,
        # then an abort/error terminal before idle clears its immutable state.
        if self._tracker.terminal is None:
            try:
                self.stop(check_cancel=False)
            except SourceClientError:
                pass  # stop response may race a terminal; continue cleanup
            if self._tracker.terminal is None:
                try:
                    self._drive_until(
                        lambda t: t.terminal is not None,
                        ABORT_TERMINAL_TIMEOUT,
                        check_cancel=False,
                    )
                except SourceClientError:
                    pass
        try:
            self.idle(check_cancel=False)
        except SourceClientError as exc:
            raise SerialConsoleError("source idle cleanup failed: %s" % exc) from exc

    def _drain_queued_records(self):
        """Consume only source records already decoded by the console."""
        while True:
            record, line = self._next_record(0.0, check_cancel=False)
            if line is None:
                return
            if record is not None:
                self._dispatch(record)
