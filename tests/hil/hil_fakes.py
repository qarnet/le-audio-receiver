"""Shared fake lab boundaries for the RH2 fake suite.

Local helpers only: fake command runner, fake serial wires with a
strict DTR/RTS ledger, scripted source/receiver transcripts, a fake
sysfs tree, and binding/fixture/image builders.  Nothing here opens a
real serial port, probe, or process boundary; the canonical inventory
never discovers this file (it is not ``test_*.py``).
"""

import json
import os
import time


class FakeProc:
    """subprocess.CompletedProcess stand-in."""

    def __init__(self, stdout="", stderr="", returncode=0):
        self.stdout = stdout
        self.stderr = stderr
        self.returncode = returncode


class ScriptedRunner:
    """Injected command runner mapping argv (prefix or exact) to scripted
    results; records every call on a shared ledger."""

    def __init__(self, ledger=None):
        self.rules = []  # (matcher, FakeProc)
        self.ledger = ledger if ledger is not None else []

    def script(self, argv, proc, exact=True):
        self.rules.append((list(argv), proc, exact))
        return self

    def script_exit(self, argv, status, stdout="", stderr="", exact=True):
        return self.script(
            argv, FakeProc(stdout=stdout, stderr=stderr, returncode=status), exact
        )

    def __call__(self, argv, timeout, env=None):
        self.ledger.append(
            {"argv": list(argv), "timeout": timeout, "env": dict(env) if env else None}
        )
        for match_argv, proc, exact in self.rules:
            if exact:
                if list(argv) == match_argv:
                    return FakeProc(proc.stdout, proc.stderr, proc.returncode)
            elif argv[: len(match_argv)] == match_argv:
                return FakeProc(proc.stdout, proc.stderr, proc.returncode)
        raise AssertionError("unexpected command: %r" % (argv,))


# ── serial fakes ───────────────────────────────────────────────────


class Wire:
    """One role's scripted serial stream.  Chunks are consumed in order;
    when exhausted, reads idle.  Written bytes are recorded (source is
    quiet; receiver writes are the shell commands)."""

    def __init__(
        self,
        name,
        chunks=(),
        assert_writes=None,
        armed=True,
        pre_mark_chunks=(),
        write_responses=(),
    ):
        self.name = name
        self.chunks = list(chunks)
        self.pre_mark_chunks = list(pre_mark_chunks)
        self.write_responses = list(write_responses)
        self.writes = []
        self.assert_writes = list(assert_writes) if assert_writes else None
        self.closed = False
        # ``armed=False`` holds scripted post-reset bytes until the runner
        # marks its fresh boot edge. It models an idle console before flash
        # without allowing fake timing to race the reader thread.
        self.armed = armed

    def next_chunk(self):
        if self.pre_mark_chunks:
            return self.pre_mark_chunks.pop(0)
        if not self.armed:
            return b""
        if self.chunks:
            return self.chunks.pop(0)
        return b""

    def mark_rx(self):
        self.pre_mark_chunks.clear()
        self.armed = True

    def on_write(self, data):
        self.writes.append(data)
        if self.assert_writes:
            expected = self.assert_writes.pop(0)
            if isinstance(expected, str):
                expected = expected.encode("utf-8")
            if expected != data:
                raise AssertionError(
                    "%s wire write mismatch: expected %r, got %r"
                    % (self.name, expected, data)
                )
        if self.write_responses:
            expected, chunks = self.write_responses.pop(0)
            if isinstance(expected, str):
                expected = expected.encode("utf-8")
            if expected != data:
                raise AssertionError(
                    "%s wire response mismatch: expected %r, got %r"
                    % (self.name, expected, data)
                )
            if isinstance(chunks, bytes):
                self.chunks.append(chunks)
            else:
                self.chunks.extend(chunks)

    def has_data(self):
        return bool(self.chunks)


class FakeSerial:
    """pyserial stand-in recording the exact DTR/RTS state at open and on
    every read, proving no assert pulse and no post-open change."""

    def __init__(self, wire):
        self._wire = wire
        self.port = None
        self.baudrate = None
        self.bytesize = None
        self.parity = None
        self.stopbits = None
        self.timeout = None
        self.xonxoff = None
        self.rtscts = None
        self.dsrdtr = None
        self.dtr = None
        self.rts = None
        self.exclusive = None
        self.is_open = False
        self.dtr_ledger = []
        self.rts_ledger = []
        self.write_calls = []

    def open(self):
        self.is_open = True
        self.dtr_ledger.append(("open", self.dtr))
        self.rts_ledger.append(("open", self.rts))

    def read(self, size):
        # Record the line state before every read: it must stay False
        # after open (no DTR/RTS pulse, no post-open change).
        self.dtr_ledger.append(("read", self.dtr))
        self.rts_ledger.append(("read", self.rts))
        chunk = self._wire.next_chunk()
        if chunk:
            return chunk
        if self._wire.closed or not self.is_open:
            return b""
        time.sleep(0.05)
        return b""

    def write(self, data):
        self.write_calls.append(data)
        self._wire.on_write(data)
        return len(data)

    def close(self):
        self.is_open = False
        self._wire.closed = True

    def mark_rx(self):
        self._wire.mark_rx()

    def reset_input_buffer(self):
        self._wire.pre_mark_chunks.clear()


class FakeWireLedger:
    """Shared event ledger for ordering assertions (console open/ready vs
    flash/reset events)."""

    def __init__(self):
        self.events = []

    def record(self, event, **fields):
        self.events.append({"event": event, **fields})

    def filter(self, event):
        return [e for e in self.events if e["event"] == event]


# ── receiver transcript builder ────────────────────────────────────

RECEIVER_PROMPT = "uart:~$ "


def receiver_prompt_line(terminated=False):
    """Returned shell prompt.

    Zephyr UART shell writes prompt without a newline. ``terminated=True``
    remains available for boundary coverage of terminal-style backends.
    """
    return (RECEIVER_PROMPT + "\r\n") if terminated else RECEIVER_PROMPT


def receiver_command_echo(command):
    return RECEIVER_PROMPT + command + "\r\n"


def receiver_transcript(command, body_lines, prompt_line=True):
    """One prompt-bounded command transcript for the receiver wire."""
    lines = [receiver_command_echo(command)]
    lines.extend(line + "\r\n" for line in body_lines)
    if prompt_line:
        lines.append(receiver_prompt_line(terminated=True))
    return "".join(lines).encode("utf-8")


def receiver_boot_chunks(markers):
    """Build receiver boot bytes with real firmware payload shapes.

    ``runner.RECEIVER_BOOT_MARKERS`` names stable marker substrings, not
    necessarily complete log payloads. In particular, firmware emits
    ``FLPR READY (epoch=..., count=..., ...)`` while ``FLPR READY_ACK sent``
    contains the shorter READY substring. Keep fake bytes representative so
    runner proves an ACK cannot satisfy both boot gates.
    """
    lines = []
    for marker in markers:
        if marker == "FLPR READY":
            marker = "FLPR READY (epoch=1, count=1, new)"
        lines.append(marker + "\r\n")
    return ["".join(lines).encode("utf-8")]


def receiver_stream_summary_line(
    slot=0,
    sdus=12000,
    decoded=12100,
    plc=100,
    decode_err=0,
    i2s_underrun=0,
    stream_reset=0,
    empty_sdu=0,
    rx_valid=12000,
    rx_error=0,
    rx_lost=0,
    rx_unknown=0,
    rx_no_ts=0,
):
    # Default is a healthy full-grammar summary above the frozen transport
    # limits (system-hil-milestones.md, 2026-09-03) for every checked-in row:
    # the largest expected submitted per segment is 12644 (48_4_1 matrix
    # rows, floor 11380), so sdus/rx_valid 12000 passes all of them and the
    # short RH2 row (764) as well. The grammar stays all-or-none: callers
    # must still pass all five RX fields or none (a no-fields line is legal
    # grammar for parser tests and expected failure paths).
    rx_values = (rx_valid, rx_error, rx_lost, rx_unknown, rx_no_ts)
    if (
        rx_valid is None
        or rx_error is None
        or rx_lost is None
        or rx_unknown is None
        or rx_no_ts is None
    ):
        if not all(value is None for value in rx_values):
            raise ValueError("receiver RX status fields must be all set or all omitted")
        rx_suffix = ""
    else:
        rx_values_int = (rx_valid, rx_error, rx_lost, rx_unknown, rx_no_ts)
        if any(value < 0 for value in rx_values_int):
            raise ValueError("receiver RX status fields must be nonnegative")
        rx_suffix = (
            " rx_valid=%d rx_error=%d rx_lost=%d rx_unknown=%d rx_no_ts=%d"
            % rx_values_int
        )
    return (
        "Stream[%d] summary: SDUs=%d decoded=%d plc=%d "
        "decode_err=%d i2s_underrun=%d stream_reset=%d empty_sdu=%d%s\r\n"
        % (
            slot,
            sdus,
            decoded,
            plc,
            decode_err,
            i2s_underrun,
            stream_reset,
            empty_sdu,
            rx_suffix,
        )
    )


def iso_link_quality_transcript(records=None):
    """Build an exact ``bt iso quality`` shell transcript."""
    if records is None:
        records = [{}]
    lines = ["--- ISO link quality ---"]
    for index, record in enumerate(records):
        values = {
            "slot": index,
            "handle": 0x1200 + index,
            "tx_unacked": 0,
            "tx_flushed": 0,
            "tx_last_subevent": 0,
            "retransmitted": 0,
            "crc_error": 0,
            "rx_unreceived": 0,
            "duplicate": 0,
            "iso_interval_1250us": 8,
            "nse": 1,
            "cig_sync_us": 1000,
            "cis_sync_us": 1100,
            "c_max_pdu": 120,
            "c_phy": 2,
            "c_bn": 1,
            "c_flush_1250us": 32,
        }
        values.update(record)
        for field in values:
            value = values[field]
            if (
                not isinstance(value, int)
                or isinstance(value, bool)
                or value < 0
                or (field == "handle" and value > 0xFFFF)
            ):
                raise ValueError("ISO link quality %s must be nonnegative" % field)
            if (
                field
                in (
                    "iso_interval_1250us",
                    "nse",
                    "cig_sync_us",
                    "cis_sync_us",
                    "c_max_pdu",
                    "c_phy",
                    "c_bn",
                    "c_flush_1250us",
                )
                and value == 0
            ):
                raise ValueError("ISO link quality %s must be positive" % field)
        lines.append(
            "  Stream[%d] handle=0x%04X tx_unacked=%d tx_flushed=%d "
            "tx_last_subevent=%d retransmitted=%d crc_error=%d "
            "rx_unreceived=%d duplicate=%d iso_interval_1250us=%d nse=%d "
            "cig_sync_us=%d cis_sync_us=%d c_max_pdu=%d c_phy=%d c_bn=%d "
            "c_flush_1250us=%d"
            % (
                values["slot"],
                values["handle"],
                values["tx_unacked"],
                values["tx_flushed"],
                values["tx_last_subevent"],
                values["retransmitted"],
                values["crc_error"],
                values["rx_unreceived"],
                values["duplicate"],
                values["iso_interval_1250us"],
                values["nse"],
                values["cig_sync_us"],
                values["cis_sync_us"],
                values["c_max_pdu"],
                values["c_phy"],
                values["c_bn"],
                values["c_flush_1250us"],
            )
        )
    return receiver_transcript("bt iso quality", lines)


def audio_status_transcript(decode=0, underrun=0, reset=0):
    return receiver_transcript(
        "audio status",
        [
            "--- Audio status ---",
            "  Decode errors : %d" % decode,
            "  I2S underruns : %d" % underrun,
            "  Stream resets : %d" % reset,
        ],
    )


def audio_perf_transcript(push_failures=0):
    return receiver_transcript(
        "audio perf",
        ["--- Queue ---", "  Push failures : %d" % push_failures],
    )


def flpr_offload_transcript(
    state="ACTIVE",
    submit=150,
    success=150,
    fallback=0,
    busy=0,
    epoch=1,
    gen=1,
    recovery_attempts=0,
    recovery_fail=0,
    relapses=0,
    exhaustion=0,
    probation_active=0,
    probation_success=0,
    probation_cleared=0,
    fault_timeout=0,
    fault_full=0,
    fault_stale=0,
    fault_seq=0,
    fault_frame=0,
    fault_crc=0,
    fault_payload=0,
    runtime_restarts=None,
    runtime_fails=0,
    runtime_last_ms=0,
    remote_epoch=0,
):
    lines = [
        "--- Audio offload ---",
        "  State       : %s / epoch=%d gen=%d" % (state, epoch, gen),
        "  Counters    : submit=%d success=%d fallback=%d busy=%d"
        % (submit, success, fallback, busy),
        "  Recovery    : attempts=%d fail=%d relapses=%d exhaustion=%d"
        % (recovery_attempts, recovery_fail, relapses, exhaustion),
        "  Probation   : active=%d success=%d cleared=%d"
        % (probation_active, probation_success, probation_cleared),
        "  Faults      : timeout=%d full=%d stale=%d seq=%d frame=%d crc=%d payload=%d"
        % (
            fault_timeout,
            fault_full,
            fault_stale,
            fault_seq,
            fault_frame,
            fault_crc,
            fault_payload,
        ),
    ]
    if runtime_restarts is not None:
        lines.append(
            "  Runtime     : restarts=%d fails=%d last_ms=%d remote_epoch=%d"
            % (runtime_restarts, runtime_fails, runtime_last_ms, remote_epoch)
        )
    return receiver_transcript(
        "flpr offload",
        lines,
    )


def flpr_status_transcript(
    ready="yes",
    acked="yes",
    healthy="yes",
    err=0,
    rx_lost=0,
    rx_dup=0,
    rx_ooo=0,
    rx_missed=0,
):
    return receiver_transcript(
        "flpr status",
        [
            "--- FLPR handshake ---",
            "  Ready        : %s" % ready,
            "  ACKed        : %s" % acked,
            "  Healthy      : %s" % healthy,
            "  Epoch        : 1 (ready=1 reboot=0)",
            "  Errors       : len=%d ver=%d unk=%d send=%d" % (err, err, err, err),
            "  TX seq       : 200 (acked=200)",
            "  RX seq       : 200 (last=1 ms)",
            "  RX lost      : %d" % rx_lost,
            "  RX dup       : %d" % rx_dup,
            "  RX ooo       : %d" % rx_ooo,
            "  RX missed    : %d" % rx_missed,
        ],
    )


# ── source HIL1 transcript builder ─────────────────────────────────


def hil_line(record_json):
    return ("HIL1 " + json.dumps(record_json, separators=(",", ":")) + "\r\n").encode(
        "utf-8"
    )


def source_status_data(command, ok=True, error="ok", **overrides):
    data = {
        "command": command,
        "ok": ok,
        "error": error,
        "active": False,
        "state": "idle",
        "segment": 0,
        "aborted": False,
        "cause": "none",
        "stop_requested": False,
        "verdict": "none",
        "first_errno": 0,
        "mode": "mono",
        "profile": "48_4_1",
        "reconnect": "none",
        "scored_target": 120,
        "stream_count": 1,
        "streams": [{"seq": 0, "sub": 0, "sc": 0, "sf": 0, "cb": 0, "out": 0}],
        "connected": False,
        "security_level": 2,
        "security_error": 0,
        "sink_ase_count": 0,
        "group": False,
        "disconnect_reason": 0,
        "first_ascs_code": 0,
        "first_ascs_reason": 0,
        "bond_count": 0,
    }
    data.update(overrides)
    return data


def source_record(kind, command_id, run_id, monotonic_ms, segment, data):
    return {
        "protocol_version": 1,
        "kind": kind,
        "firmware_id": "le-audio-hil-source-rh1",
        "monotonic_ms": monotonic_ms,
        "command_id": command_id,
        "run_id": run_id,
        "segment": segment,
        "data": data,
    }


def source_hello_data(
    identity="DB:A6:0C:05:A2:AA",
    identity_type="random",
    bond_count=0,
    active=False,
    state="idle",
    segment=0,
):
    return {
        "command": "hello",
        "ok": True,
        "error": "ok",
        "firmware_id": "le-audio-hil-source-rh1",
        "protocol_version": 1,
        "identity": identity,
        "identity_type": identity_type,
        "active": active,
        "state": state,
        "segment": segment,
        "bond_count": bond_count,
        "sample_rate": 48000,
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
        "default_seed": 1218649181,
    }


class SourceTranscript:
    """Builds the scripted source wire for the runner's exact command
    sequence and asserts every written command line in order."""

    def __init__(
        self,
        run_id,
        receiver_address="DB:A6:0C:05:A2:AA",
        receiver_type="random",
        hello_bond_after_unpair=0,
    ):
        self.run_id = run_id
        self.receiver_address = receiver_address
        self.receiver_type = receiver_type
        self.hello_bond_after_unpair = hello_bond_after_unpair
        self.chunks = []
        self.expected_writes = []
        self._cmd = 0

    def _next_cmd(self):
        self._cmd += 1
        return "cmd-%04d" % self._cmd

    def _expect(self, command, **extra):
        command_id = self._next_cmd()
        payload = {
            "protocol_version": 1,
            "command": command,
            "command_id": command_id,
            "run_id": self.run_id,
        }
        payload.update(extra)
        self.expected_writes.append(
            "hil " + json.dumps(payload, separators=(",", ":")) + "\n"
        )
        return command_id

    def _status(self, command_id, data):
        self.chunks.append(
            hil_line(source_record("status", command_id, self.run_id, 10, 0, data))
        )

    def _ack(self, command_id):
        self.chunks.append(
            hil_line(
                source_record(
                    "ack",
                    command_id,
                    self.run_id,
                    11,
                    0,
                    {"command": "start", "accepted": True},
                )
            )
        )

    def _state(self, command_id, state, ms, segment=0):
        self.chunks.append(
            hil_line(
                source_record(
                    "state", command_id, self.run_id, ms, segment, {"state": state}
                )
            )
        )

    def _terminal(self, command_id, verdict, ms, segment=0):
        self.chunks.append(
            hil_line(
                source_record(
                    "terminal",
                    command_id,
                    self.run_id,
                    ms,
                    segment,
                    {"verdict": verdict},
                )
            )
        )

    def hello(self, bond_count):
        command_id = self._expect("hello")
        self._status(command_id, source_hello_data(bond_count=bond_count))
        return command_id

    def idle(self):
        command_id = self._expect("idle")
        self._status(command_id, source_status_data("idle"))
        return command_id

    def unpair(self):
        command_id = self._expect(
            "unpair",
            peer_address=self.receiver_address,
            peer_address_type=self.receiver_type,
        )
        self._status(command_id, source_status_data("unpair"))
        return command_id

    def configure(self, row=None):
        if row is None:
            mode = "mono"
            profile = "48_4_1"
            scored_sdu_count = 120
            signal_seed = 1218649181
            reconnect_policy = "none"
        else:
            mode = row.mode
            profile = row.profile
            scored_sdu_count = row.scored_sdu_count
            signal_seed = row.signal_seed
            reconnect_policy = row.reconnect_policy
        command_id = self._expect(
            "configure",
            peer_address=self.receiver_address,
            peer_address_type=self.receiver_type,
            mode=mode,
            profile=profile,
            scored_sdu_count=scored_sdu_count,
            signal_seed=signal_seed,
            reconnect_policy=reconnect_policy,
        )
        self._status(command_id, source_status_data("configure"))
        return command_id

    def start(self, scored=120, row=None, submitted=None, omit_final_status=False):
        if row is None:
            mode = "mono"
            profile = "48_4_1"
            reconnect_policy = "none"
            stream_count = 1
            segment_count = 1
        else:
            mode = row.mode
            profile = row.profile
            reconnect_policy = row.reconnect_policy
            stream_count = row.stream_count
            segment_count = row.segment_count
        if submitted is None:
            submitted = (
                row.expected_submitted_per_segment if row is not None else scored + 30
            )
        command_id = self._expect("start")
        self._ack(command_id)
        ms = 100
        for segment in range(segment_count):
            states = (
                (
                    "idle",
                    "configured",
                    "connecting",
                    "secured",
                    "discovered",
                    "qos",
                    "streaming",
                    "scored_complete",
                )
                if segment == 0
                else (
                    "connecting",
                    "secured",
                    "discovered",
                    "qos",
                    "streaming",
                    "scored_complete",
                )
            )
            for state in states:
                ms += 1
                self._state(command_id, state, ms, segment=segment)
            active_query_id = self._expect("status")
            active_streams = [
                {
                    "seq": 10,
                    "sub": 10,
                    "sc": 0,
                    "sf": 0,
                    "cb": 8,
                    "out": 2,
                }
                for _ in range(stream_count)
            ]
            self._status(
                active_query_id,
                source_status_data(
                    "status",
                    active=True,
                    state="streaming",
                    verdict="none",
                    mode=mode,
                    profile=profile,
                    reconnect=reconnect_policy,
                    scored_target=scored,
                    segment=segment,
                    stream_count=stream_count,
                    streams=active_streams,
                    connected=True,
                    security_level=2,
                    sink_ase_count=stream_count,
                    group=True,
                    bond_count=1,
                ),
            )
            # scored_complete reached; runner owns receiver tail/recovery
            # commands while source remains active.
            ms += 1
            self._state(command_id, "teardown", ms, segment=segment)
        ms += 1
        self._terminal(command_id, "pass", ms, segment=segment_count - 1)
        if not omit_final_status:
            # final status query after terminal
            query_id = self._expect("status")
            streams = [
                {
                    "seq": submitted * segment_count,
                    "sub": submitted * segment_count,
                    "sc": scored * segment_count,
                    "sf": 0,
                    "cb": submitted * segment_count,
                    "out": 0,
                }
                for _ in range(stream_count)
            ]
            self._status(
                query_id,
                source_status_data(
                    "status",
                    active=False,
                    state="teardown",
                    verdict="pass",
                    first_errno=0,
                    mode=mode,
                    profile=profile,
                    reconnect=reconnect_policy,
                    scored_target=scored,
                    segment=segment_count - 1,
                    stream_count=stream_count,
                    streams=streams,
                    connected=False,
                    security_level=0,
                    sink_ase_count=0,
                    bond_count=1,
                ),
            )
        return command_id

    def final_idle(self):
        return self.idle()

    def start_then_stall(self):
        """start ACK plus states through scored_complete only, then the
        cleanup responses (stop status, abort teardown, fail terminal,
        idle status).  The runner's start() must fail on the stall and the
        bounded stop/idle cleanup must consume the remaining records."""
        command_id = self._expect("start")
        self._ack(command_id)
        ms = 100
        for state in (
            "idle",
            "configured",
            "connecting",
            "secured",
            "discovered",
            "qos",
            "streaming",
            "scored_complete",
        ):
            ms += 1
            self._state(command_id, state, ms)
            if state == "streaming":
                active_id = self._expect("status")
                self._status(
                    active_id,
                    source_status_data(
                        "status",
                        active=True,
                        state="streaming",
                        verdict="none",
                        streams=[
                            {
                                "seq": 10,
                                "sub": 10,
                                "sc": 0,
                                "sf": 0,
                                "cb": 8,
                                "out": 2,
                            }
                        ],
                        connected=True,
                        security_level=2,
                        sink_ase_count=1,
                        group=True,
                        bond_count=1,
                    ),
                )
        stop_id = self._expect("stop")
        self._status(stop_id, source_status_data("stop"))
        self._abort(command_id, "stop", ms + 1)
        self._terminal(command_id, "fail", ms + 2)
        idle_id = self._expect("idle")
        self._status(idle_id, source_status_data("idle"))
        return command_id

    def _abort(self, command_id, cause, ms):
        self.chunks.append(
            hil_line(
                source_record(
                    "state",
                    command_id,
                    self.run_id,
                    ms,
                    0,
                    {"state": "teardown", "cause": cause},
                )
            )
        )

    def build(self):
        return list(self.chunks), list(self.expected_writes)


def build_passing_source_wire(run_id):
    """Full passing row transcript: hello(0), idle, unpair, hello(0),
    configure, start, final status, idle."""
    t = SourceTranscript(run_id)
    t.hello(bond_count=0)
    t.idle()
    t.unpair()
    t.hello(bond_count=0)
    t.configure()
    t.start(scored=120, row=None, submitted=764)
    t.final_idle()
    return t


# ── fixture / binding / image builders ─────────────────────────────

FIXTURE_ID = "local-nrf54l15-receiver"

RECEIVER_SERIAL = "PROBE-ABC123"
SOURCE_SERIAL = "J-LINK-0001"


def fixture_json():
    return {
        "schema_version": 1,
        "fixture_id": FIXTURE_ID,
        "capture_capability": "none",
        "roles": {
            "receiver": {
                "kind": "zephyr_dut",
                "board": "nrf54l15dk/nrf54l15/cpuapp",
                "images": ["cpuapp", "flpr"],
            },
            "source": {
                "kind": "zephyr_dut",
                "board": "nrf5340dk/nrf5340/cpuapp",
                "images": ["cpuapp", "cpunet"],
            },
        },
    }


def binding_json():
    return {
        "schema_version": 1,
        "fixture_id": FIXTURE_ID,
        "roles": {
            "receiver": {
                "probe": {"backend": "nrf-probes", "family": "nrf54l"},
                "serial": {"baud": 115200, "dtr": False, "rts": False, "udev": {}},
            },
            "source": {
                "probe": {
                    "backend": "jlink",
                    "family": "nrf53",
                    "udev": {
                        "ID_VENDOR_ID": "1366",
                        "ID_MODEL_ID": "1015",
                        "ID_SERIAL_SHORT": SOURCE_SERIAL,
                    },
                },
                "serial": {"baud": 115200, "dtr": False, "rts": False, "udev": {}},
            },
        },
    }


def write_fixture_binding(directory, fixture=None, binding=None):
    fixture_path = os.path.join(directory, "fixture.json")
    binding_path = os.path.join(directory, "binding.json")
    with open(fixture_path, "w", encoding="utf-8") as fh:
        json.dump(fixture if fixture is not None else fixture_json(), fh, indent=2)
    with open(binding_path, "w", encoding="utf-8") as fh:
        json.dump(binding if binding is not None else binding_json(), fh, indent=2)
    return fixture_path, binding_path


IMAGE_RELS = (
    "build/hil-source/app/zephyr/zephyr.hex",
    "build/hil-source/hci_ipc/zephyr/zephyr.hex",
    "build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex",
    "build/nrf54l15/flpr/zephyr/zephyr.hex",
)


def make_images(repo_root, content=b"fake image bytes\n"):
    for rel in IMAGE_RELS:
        path = os.path.join(repo_root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as fh:
            fh.write(content)


# ── fake sysfs ─────────────────────────────────────────────────────


def build_fake_sysfs(
    root,
    *,
    receiver_tty="ttyACM0",
    source_tty="ttyACM1",
    receiver_usb="1-2",
    source_usb="1-3",
):
    """Real directories under ``root`` mirroring /sys for the two-role
    fixture.  Each tty's DEVPATH walks up to its USB device dir (which
    holds idVendor), so USB-parent cross-wiring checks work on the real
    filesystem."""
    sysfs = os.path.join(root, "sys")
    tty_dir = os.path.join(sysfs, "class", "tty")
    usb_dir = os.path.join(sysfs, "bus", "usb", "devices")
    for name in (receiver_tty, source_tty):
        os.makedirs(os.path.join(tty_dir, name), exist_ok=True)
    layout = {
        receiver_usb: receiver_tty,
        source_usb: source_tty,
    }
    for usb_id, tty in layout.items():
        base = os.path.join(sysfs, "devices", "pci0000:00", "usb1", usb_id)
        os.makedirs(os.path.join(base, "1-1:1.0", "tty", tty), exist_ok=True)
        with open(os.path.join(base, "idVendor"), "w") as fh:
            fh.write("1366\n")
        with open(os.path.join(base, "idProduct"), "w") as fh:
            fh.write("1015\n")
        with open(os.path.join(base, "serial"), "w") as fh:
            fh.write("serial-%s\n" % usb_id)
        os.makedirs(os.path.join(usb_dir, usb_id), exist_ok=True)
    return sysfs


def tty_devpath(usb_id, tty):
    return "/devices/pci0000:00/usb1/%s/1-1:1.0/tty/%s" % (usb_id, tty)


def usb_node(usb_id):
    return "/sys/bus/usb/devices/%s" % usb_id


def tty_node(tty):
    return "/sys/class/tty/%s" % tty


def default_probe_table(rows=None):
    """Plain ``nrf-probes`` table exactly as the real tool renders it:
    column widths are the max cell width per column over the header and
    every row, cells padded left-justified and joined with two spaces."""
    header = ("SERIAL", "PROBE", "TARGET", "DPIDR", "PART", "VARIANT", "NOTE")
    if rows is None:
        rows = [
            (
                "PROBE-ABC123",
                "DAPLink",
                "nRF54L15",
                "0x6ba02477",
                "0x00054b15",
                "BAAA",
                "",
            ),
        ]
    all_rows = [header] + [tuple(r) for r in rows]
    widths = [max(len(row[i]) for row in all_rows) for i in range(len(header))]
    lines = []
    for row in all_rows:
        cells = [cell.ljust(widths[i]) for i, cell in enumerate(row)]
        lines.append("  ".join(cells).rstrip())
    return "\n".join(lines) + "\n"


def fingerprint_output(
    part="0x00005340",
    variant="0x41414141",
    ap2="0x12880000",
    dpidr="0x6ba02477",
    failure=None,
):
    lines = [
        "Info : auto-selecting speed 1000 kHz",
        "FWJ|dpidr|%s" % dpidr,
    ]
    for i in range(4):
        lines.append("FWJ|ap%d|%s" % (i, ap2 if i in (2, 3) else "0x00000000"))
    lines.append("FWJ|part|%s" % part)
    lines.append("FWJ|variant|%s" % variant)
    if failure:
        lines.append(failure)
    return "\n".join(lines) + "\n"
