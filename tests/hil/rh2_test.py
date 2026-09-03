#!/usr/bin/env python3
"""RH2 fake-lab tests: no hardware, no live probe/serial/flash.

All orchestration behavior is proven against fake command runners, fake
sysfs trees, and scripted serial wires.  Every test asserts public
boundaries: filesystem evidence, process argv/status, serial transcripts,
exit codes, JUnit/result behavior, and the fake event ledger ordering
visible at the boundary.  This file is NOT discovered by the canonical
inventory (it does not match the ``test_*.py`` naming under
``tests/unit/`` or ``scripts/``).

Run with:

    nix develop --command pytest -q tests/hil/rh2_test.py
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from types import MappingProxyType

_REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(_REPO, "scripts"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import hil_fakes  # noqa: E402
from hil import (
    cli,
    discovery,
    evidence,
    lifecycle,
    model,
    receiver,
    rows,
    runner,
    serial_io,
    source_client,
)  # noqa: E402
from hil.discovery import HilDiscoveryError  # noqa: E402
from hil.evidence import EvidenceError  # noqa: E402
from hil.runner import (  # noqa: E402
    CommandCancelled,
    HilRunnerError,
    Runner,
    RunnerCancelled,
    RunnerDeps,
)
from hil.serial_io import SerialConsole, SerialConsoleError  # noqa: E402
from hil.source_client import SourceClient, SourceClientError  # noqa: E402

FIXTURE_ID = hil_fakes.FIXTURE_ID
RUN_ID = "rh2-0001"
HANG_RECOVERY_WARNING = (
    "<wrn> audio_offload: offload: heartbeat supervisor → RECOVERING"
)


# ── tiny fakes ─────────────────────────────────────────────────────


class LineConsole:
    """In-memory console for source-client tests (no threads)."""

    def __init__(self, lines):
        self.lines = list(lines)
        self.writes = []

    def next_line(self, timeout):
        if self.lines:
            return self.lines.pop(0)
        return None

    def write(self, data):
        self.writes.append(data)


class ControlledClock:
    """Deterministic clock/sleep pair for bounded runner polling tests."""

    def __init__(self):
        self.now = 0.0
        self.sleeps = []

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.now += seconds


class _ReleaseWire(hil_fakes.Wire):
    """Wire that lets a test release reader bytes after a runner scan mark."""

    def __init__(self, name):
        super().__init__(name)
        self._chunks_lock = threading.Lock()

    def release(self, chunks):
        with self._chunks_lock:
            self.chunks.extend(chunks)

    def next_chunk(self):
        with self._chunks_lock:
            return super().next_chunk()


class _ReleaseAfterScanClock:
    """Release raw RX bytes on first clock read, then return scripted time."""

    def __init__(self, console, wire, chunks, values):
        if not values:
            raise ValueError("clock needs at least one value")
        self._console = console
        self._wire = wire
        self._chunks = list(chunks)
        self._values = list(values)
        self._last = self._values[-1]
        self._released = False

    def __call__(self):
        if not self._released:
            self._released = True
            before = self._console.bytes_received()
            self._wire.release(self._chunks)
            expected = before + sum(len(chunk) for chunk in self._chunks)
            deadline = time.monotonic() + 2.0
            while (
                self._console.bytes_received() < expected
                and time.monotonic() < deadline
            ):
                time.sleep(0.01)
            if self._console.bytes_received() < expected:
                raise AssertionError("reader did not retain released raw evidence")
        if self._values:
            self._last = self._values.pop(0)
        return self._last


class _GateProbe:
    """Lock wrapper proving close waits for the real reader gate."""

    def __init__(self):
        self._lock = threading.Lock()
        self.close_attempted = threading.Event()

    def acquire(self, *args, **kwargs):
        if threading.current_thread().name == "hil-close-race":
            self.close_attempted.set()
        return self._lock.acquire(*args, **kwargs)

    def release(self):
        self._lock.release()

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, exc_type, exc, tb):
        del exc_type, exc, tb
        self.release()
        return False


class _BlockedReadSerial(hil_fakes.FakeSerial):
    """Fake backend that exposes descriptor-close/read overlap."""

    def __init__(self, wire, wait_timeout=None, arm_immediately=False, payload=b""):
        super().__init__(wire)
        self._race_armed = arm_immediately
        self._race_consumed = False
        self._wait_timeout = wait_timeout
        self._payload = payload
        self._read_active = False
        self.read_started = threading.Event()
        self.release_read = threading.Event()
        self.close_called = threading.Event()
        self.close_during_read = False

    def arm_race(self):
        self._race_armed = True
        self._race_consumed = False
        self.read_started.clear()
        self.release_read.clear()

    def read(self, size):
        if not self._race_armed or self._race_consumed:
            return super().read(size)
        del size
        self._race_consumed = True
        self.dtr_ledger.append(("read", self.dtr))
        self.rts_ledger.append(("read", self.rts))
        self._read_active = True
        self.read_started.set()
        try:
            self.release_read.wait(self._wait_timeout)
            if not self.is_open:
                raise TypeError("'NoneType' object cannot be interpreted as an integer")
            return self._payload
        finally:
            self._read_active = False

    def close(self):
        self.close_during_read = self.close_during_read or self._read_active
        self.close_called.set()
        if self._read_active:
            # Old unsynchronized close() must not leave this regression test
            # thread blocked forever after it has exposed the race.
            self.release_read.set()
        super().close()


def fake_resolution():
    """Canned FixtureResolution for runner tests."""
    receiver_serial = discovery.SerialIdentity(
        "receiver",
        "/dev/ttyACM0",
        115200,
        MappingProxyType(
            {
                "ID_SERIAL_SHORT": hil_fakes.RECEIVER_SERIAL,
                "DEVPATH": hil_fakes.tty_devpath("1-2", "ttyACM0"),
            }
        ),
        usb_parent="/sys/devices/pci0000:00/usb1/1-2",
    )
    source_serial = discovery.SerialIdentity(
        "source",
        "/dev/ttyACM1",
        115200,
        MappingProxyType(
            {
                "ID_SERIAL_SHORT": hil_fakes.SOURCE_SERIAL,
                "DEVPATH": hil_fakes.tty_devpath("1-3", "ttyACM1"),
            }
        ),
        usb_parent="/sys/devices/pci0000:00/usb1/1-3",
    )
    receiver_probe = discovery.ProbeIdentity(
        "receiver",
        "nrf-probes",
        "nrf54l",
        hil_fakes.RECEIVER_SERIAL,
        "nRF54L15",
        "0x6ba02477",
        "0x00054b15",
        "BAAA",
    )
    source_probe = discovery.ProbeIdentity(
        "source",
        "jlink",
        "nrf53",
        hil_fakes.SOURCE_SERIAL,
        "nRF5340",
        "0x6ba02477",
        "0x00005340",
        "AAAA",
    )
    raw = {
        "nrf-probes": {
            "argv": ["nrf-probes"],
            "stdout": hil_fakes.default_probe_table(),
            "stderr": "",
            "status": 0,
        },
        "receiver-udev": {
            "ttyACM0": dict(receiver_serial.properties),
        },
        "source-udev": {
            "ttyACM1": dict(source_serial.properties),
        },
        "source-jlink-fingerprint": {
            "argv": ["openocd"],
            "output": hil_fakes.fingerprint_output(),
            "status": 0,
        },
    }
    return discovery.FixtureResolution(
        roles=MappingProxyType(
            {
                "receiver": discovery.RoleIdentity(
                    "receiver", receiver_probe, receiver_serial
                ),
                "source": discovery.RoleIdentity("source", source_probe, source_serial),
            }
        ),
        raw=MappingProxyType(raw),
    )


def make_runner_deps(
    receiver_wire,
    source_wire,
    ledger=None,
    run_cmd=None,
    cancel=None,
    environment="default",
    clock=None,
    sleep=None,
    repo_root=None,
):
    """RunnerDeps with scripted discovery, serial factory, and runner."""
    scripted = run_cmd if run_cmd is not None else hil_fakes.ScriptedRunner(ledger)
    # Production console readers open before reset. Keep normal scripted boot
    # and response bytes behind their fresh-RX marks, so a fast fake reader
    # cannot consume post-reset output before the runner reaches flash.
    receiver_wire.armed = False
    source_wire.armed = False

    def factory(role, path, baud, evidence_path, dtr, rts):
        wire = receiver_wire if role == "receiver" else source_wire
        console = SerialConsole(
            role,
            path,
            baud,
            evidence_path,
            serial_class=lambda: hil_fakes.FakeSerial(wire),
            dtr=dtr,
            rts=rts,
        )
        if ledger is not None:
            ledger.append(
                {
                    "event": "console-open",
                    "role": role,
                    "path": path,
                    "dtr": dtr,
                    "rts": rts,
                }
            )
            event_ledger = ledger
            original_open = console.open

            def open_and_record_ready():
                original_open()
                if not console.wait_reader_ready(10.0):
                    raise AssertionError("%s reader never became ready" % role)
                event_ledger.append({"event": "reader-ready", "role": role})

            console.open = open_and_record_ready
        return console

    return RunnerDeps(
        run_cmd=scripted,
        discover=lambda binding, sysfs_root=None, run_cmd=None: fake_resolution(),
        serial_factory=factory,
        clock=clock if clock is not None else time.monotonic,
        sleep=sleep if sleep is not None else time.sleep,
        cancel=cancel if cancel is not None else lambda: False,
        environment=(
            None
            if environment is None
            else (lambda argv, status: {"injected": True})
            if environment == "default"
            else environment
        ),
        repo_root=repo_root,
        boot_timeout=2.0,
        summary_timeout=2.0,
    )


def _receiver_passing_wire(
    run_id,
    offload_snapshots=None,
    summary_lines=None,
    iso_link_quality_records=None,
    post_stop_offload=None,
    hci_trace=False,
    hci_trace_lines=None,
    sdc_trace=False,
    sdc_trace_lines=None,
):
    """Scripted receiver wire for a full passing row."""
    if offload_snapshots is None:
        offload_snapshots = [hil_fakes.flpr_offload_transcript()]
    else:
        offload_snapshots = list(offload_snapshots)
    if not offload_snapshots:
        raise ValueError("at least one offload snapshot is required")
    if summary_lines is None:
        summary_lines = [hil_fakes.receiver_stream_summary_line()]
    else:
        summary_lines = list(summary_lines)
    if iso_link_quality_records is None:
        iso_link_quality_records = [{} for _ in summary_lines]
    else:
        iso_link_quality_records = list(iso_link_quality_records)
    if post_stop_offload is None:
        active = receiver.parse_offload_status(offload_snapshots[-1].decode("utf-8"))
        completed = max(active.get("submit", 0), active.get("success", 0), 0) + 1
        post_stop_offload = hil_fakes.flpr_offload_transcript(
            state="STOPPED", submit=completed, success=completed, epoch=0, gen=2
        )
    assert_writes = (
        [
            "bt identity\r",
            "bt unpair\r",
            "bt bonds\r",
            "bt identity\r",
        ]
        + ["flpr offload\r"] * len(offload_snapshots)
        + [
            "bt iso quality\r",
            "audio status\r",
            "audio perf\r",
            "flpr offload\r",
            "flpr status\r",
        ]
    )
    chunks = []
    chunks += hil_fakes.receiver_boot_chunks(list(runner.RECEIVER_BOOT_MARKERS))
    chunks.append(
        hil_fakes.receiver_transcript(
            "bt identity",
            ["Identity: DB:A6:0C:05:A2:AA (random)"],
        )
    )
    chunks.append(
        hil_fakes.receiver_transcript(
            "bt unpair",
            ["Pairing reset complete: bonds cleared; BONDING advertising active."],
        )
    )
    chunks.append(hil_fakes.receiver_transcript("bt bonds", ["Bond count: 0"]))
    chunks.append(
        hil_fakes.receiver_transcript(
            "bt identity",
            ["Identity: DB:A6:0C:05:A2:AA (random)"],
        )
    )
    if hci_trace:
        if hci_trace_lines is None:
            hci_trace_lines = [
                "uart:~$ --- 2 messages dropped ---",
                "<inf> bt_bap: HCI remove ISO path trace armed: core_id=7 driver_id=9 core_level=4 driver_level=4",
                "<dbg> bt_hci_core: Sending command 0x206f (buf 0x123) to driver",
                "<dbg> bt_sdc_hci_driver: Command Complete (0x206f) status: 0x00, ncmd: 1, len 6",
                "<dbg> bt_sdc_hci_driver: Command Status (0x206f) status: 0x00",
                "<dbg> bt_hci_core: opcode 0x206f status 0x00 Success buf 0x123",
            ]
        chunks.append(
            "".join(line + "\r\n" for line in hci_trace_lines).encode("utf-8")
        )
    if sdc_trace:
        if sdc_trace_lines is None:
            sdc_trace_lines = [
                "uart:~$ --- 2 messages dropped ---",
                "<inf> bt_bap: SDC LE Remove ISO Data Path trace armed",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path hci_internal_cmd_put entry",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path trace entry",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path trace return: status=0x00",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path multithreading_lock_release entry",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path multithreading_lock_release return",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path k_work_submit_to_queue entry",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=1",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path work state snapshot scheduled: status=1",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path work state snapshot: busy=0x4 mpsl_state=PENDING",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path hci_internal_msg_get entry",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path hci_internal_msg_get return: status=0",
                "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path completion: status=0x00",
            ]
        chunks.append(
            "".join(line + "\r\n" for line in sdc_trace_lines).encode("utf-8")
        )
    chunks.extend(offload_snapshots)
    chunks.append(hil_fakes.iso_link_quality_transcript(iso_link_quality_records))
    chunks.extend(summary.encode("utf-8") for summary in summary_lines)
    chunks.append(hil_fakes.audio_status_transcript())
    chunks.append(hil_fakes.audio_perf_transcript())
    chunks.append(post_stop_offload)
    chunks.append(hil_fakes.flpr_status_transcript())
    return hil_fakes.Wire("receiver", chunks=chunks, assert_writes=assert_writes)


def _receiver_recovery_wire(
    run_id,
    row,
    fault,
    warning_before=None,
    warning_inside=None,
    warning_after=None,
    warning_crossing_start=None,
    warning_crossing_end=None,
):
    """Full fake receiver session for one RH3 named FLPR recovery row."""
    assert row.fault == fault
    assert row.profile == "48_4_1"
    baseline_success = runner.FAULT_BASELINE_SUCCESS[fault]
    baseline = dict(
        state="ACTIVE",
        submit=baseline_success,
        success=baseline_success,
        fallback=0,
        epoch=10,
        gen=10,
        runtime_restarts=0,
    )
    if fault == "hang":
        recovered = dict(
            state="ACTIVE",
            submit=baseline_success + 250,
            success=baseline_success + 200,
            fallback=1,
            epoch=11,
            gen=11,
            recovery_attempts=1,
            probation_cleared=1,
            fault_timeout=1,
            runtime_restarts=1,
            remote_epoch=11,
        )
        ack_body = ["FAULT_HANG_ACK received — FLPR hang imminent."]
        fault_command = "flpr hang"
    else:
        recovered = dict(
            state="ACTIVE",
            submit=baseline_success + 250,
            success=baseline_success + 200,
            fallback=1,
            epoch=11,
            gen=11,
            recovery_attempts=1,
            probation_cleared=1,
            fault_timeout=1,
            runtime_restarts=0,
        )
        ack_body = [
            "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)"
        ]
        fault_command = "flpr ring stall_flpr_ms 1 60"
    assert_writes = [
        "bt identity\r",
        "bt bonds\r",
        "bt identity\r",
        "flpr offload\r",
        fault_command + "\r",
        "flpr offload\r",
        "bt iso quality\r",
        "audio status\r",
        "audio perf\r",
        "flpr offload\r",
        "flpr status\r",
    ]

    def warning_bytes(warning):
        if warning is None:
            return b""
        return warning.encode("utf-8") + b"\r\n"

    def warning_before_prompt(transcript, warning):
        if warning is None:
            return transcript
        prompt = hil_fakes.receiver_prompt_line(terminated=True).encode("utf-8")
        assert transcript.endswith(prompt)
        return transcript[: -len(prompt)] + warning_bytes(warning) + prompt

    baseline_response = hil_fakes.flpr_offload_transcript(**baseline)
    fault_response = hil_fakes.receiver_transcript(fault_command, ack_body)
    recovery_wire = dict(recovered)
    if fault == "stall":
        recovery_wire["runtime_restarts"] = None
    recovery_response = hil_fakes.flpr_offload_transcript(**recovery_wire)
    post_stop = dict(recovered)
    post_stop.update(
        state="STOPPED",
        submit=recovered["submit"],
        success=recovered["submit"],
        epoch=0,
        gen=12,
    )
    post_stop_wire = dict(post_stop)
    if fault == "stall":
        post_stop_wire["runtime_restarts"] = None
    post_stop_response = hil_fakes.flpr_offload_transcript(**post_stop_wire)
    if warning_crossing_start is not None:
        crossing = warning_crossing_start.encode("utf-8")
        # Keep one complete log line spanning the raw start cursor. The first
        # byte arrives after baseline response but before fault command; rest
        # arrives only when the command is written.
        baseline_response += crossing[:1]
        fault_response = crossing[1:] + b"\r\n" + fault_response
    baseline_response += warning_bytes(warning_before)
    fault_response = warning_before_prompt(fault_response, warning_inside)

    audio_status_response = warning_bytes(warning_after)
    iso_link_quality_response = hil_fakes.iso_link_quality_transcript()
    if warning_crossing_end is not None:
        crossing = warning_crossing_end.encode("utf-8")
        recovery_response += crossing[:1]
        iso_link_quality_response = crossing[1:] + b"\r\n" + iso_link_quality_response
    audio_status_response += hil_fakes.audio_status_transcript()
    flpr_status_response = hil_fakes.flpr_status_transcript()
    summary_response = hil_fakes.receiver_stream_summary_line().encode("utf-8")
    write_responses = [
        (
            "bt identity\r",
            hil_fakes.receiver_transcript(
                "bt identity", ["Identity: DB:A6:0C:05:A2:AA (random)"]
            ),
        ),
        ("bt bonds\r", hil_fakes.receiver_transcript("bt bonds", ["Bond count: 1"])),
        (
            "bt identity\r",
            hil_fakes.receiver_transcript(
                "bt identity", ["Identity: DB:A6:0C:05:A2:AA (random)"]
            ),
        ),
        ("flpr offload\r", baseline_response),
        (fault_command + "\r", fault_response),
        ("flpr offload\r", recovery_response),
        ("bt iso quality\r", iso_link_quality_response + summary_response),
        ("audio status\r", audio_status_response),
        ("audio perf\r", hil_fakes.audio_perf_transcript()),
        ("flpr offload\r", post_stop_response),
        ("flpr status\r", flpr_status_response),
    ]
    return hil_fakes.Wire(
        "receiver",
        chunks=hil_fakes.receiver_boot_chunks(list(runner.RECEIVER_BOOT_MARKERS)),
        assert_writes=assert_writes,
        write_responses=write_responses,
    )


def _source_wire_for_row(run_id, row, initial_bond_count=0):
    """Script exact source control wire for fresh/preserved and row shape."""
    transcript = hil_fakes.SourceTranscript(run_id)
    transcript.hello(bond_count=initial_bond_count)
    if row.state == "fresh":
        transcript.idle()
        transcript.unpair()
        transcript.hello(bond_count=0)
    else:
        # Runner proves source preserved-bond state with a second HELLO.
        transcript.hello(bond_count=1)
    transcript.configure(row)
    transcript.start(scored=row.scored_sdu_count, row=row)
    transcript.final_idle()
    chunks, writes = transcript.build()
    return hil_fakes.Wire("source", chunks=chunks, assert_writes=writes)


def _source_wire_with_deferred_cleanup(transcript):
    """Hold stop/idle responses until their bounded commands are written."""
    cleanup_chunks = transcript.chunks[-4:]
    del transcript.chunks[-4:]
    chunks, writes = transcript.build()
    return hil_fakes.Wire(
        "source",
        chunks=chunks,
        assert_writes=writes,
        write_responses=[(write, []) for write in writes[:-2]]
        + [
            (writes[-2], cleanup_chunks[:3]),
            (writes[-1], cleanup_chunks[3:]),
        ],
    )


def _run_harness(
    td,
    run_id=RUN_ID,
    run_cmd=None,
    receiver_wire=None,
    source_wire=None,
    cancel=None,
    environment="default",
    ledger=None,
    engine_setup=None,
    row=None,
    clock=None,
    sleep=None,
    hci_remove_iso_path_trace=False,
    sdc_hci_remove_iso_path_trace=False,
):
    """Full runner harness in a temp dir; returns the outcome tuple and
    key paths."""
    out_root = os.path.join(td, "out")
    os.makedirs(out_root)
    cfg = os.path.join(td, "cfg")
    os.makedirs(cfg)
    fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
    repo_fake = os.path.join(td, "repo")
    os.makedirs(repo_fake)
    hil_fakes.make_images(repo_fake)
    junit = os.path.join(out_root, "%s.junit.xml" % run_id)
    if run_cmd is None:
        run_cmd = hil_fakes.ScriptedRunner(ledger)
        run_cmd.script_exit(["lsof", "--", "/dev/ttyACM1"], 1)
        run_cmd.script(
            ["fw-flash-hil-source"],
            hil_fakes.FakeProc(
                stdout="program %s verify\nreset run\n" % "net", stderr=""
            ),
        )
        run_cmd.script(["fw-flash-54l15"], hil_fakes.FakeProc(stdout="ok\n", stderr=""))
    if row is None:
        row = rows.RH2_ROW
    if receiver_wire is None:
        receiver_wire = _receiver_passing_wire(run_id)
    if source_wire is None:
        source_t = hil_fakes.build_passing_source_wire(run_id)
        src_chunks, src_writes = source_t.build()
        source_wire = hil_fakes.Wire(
            "source", chunks=src_chunks, assert_writes=src_writes
        )
    deps = make_runner_deps(
        receiver_wire,
        source_wire,
        ledger=ledger,
        run_cmd=run_cmd,
        cancel=cancel,
        environment=environment,
        clock=clock,
        sleep=sleep,
        repo_root=repo_fake,
    )
    engine = Runner(deps)
    if engine_setup is not None:
        engine_setup(engine)
    result = engine.run(
        fixture_path,
        binding_path,
        out_root,
        run_id,
        junit,
        argv=["hil-runner.py", "run"],
        status=0,
        row=row,
        hci_remove_iso_path_trace=hci_remove_iso_path_trace,
        sdc_hci_remove_iso_path_trace=sdc_hci_remove_iso_path_trace,
    )
    return result, out_root, run_id, junit, fixture_path, binding_path, engine


# ── serial console ─────────────────────────────────────────────────


class TestSerialConsole(unittest.TestCase):
    def test_open_applies_binding_line_state_without_postopen_change(self):
        wire = hil_fakes.Wire("receiver")
        with tempfile.TemporaryDirectory() as td:
            evidence_path = os.path.join(td, "receiver-console.bin")
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                evidence_path,
                serial_class=lambda: hil_fakes.FakeSerial(wire),
                dtr=True,
                rts=False,
            )
            console.open()
            console.wait_reader_ready(5.0)
            # Reader polls; allow a few reads to record line state.
            time.sleep(0.15)
            console.close()
            fake = None  # not directly reachable; assert via wire-free path
            del fake
            # The FakeSerial dtr/rts ledger is not directly reachable, so
            # re-prove through a second open/read cycle with direct access.
        # Direct FakeSerial access variant:
        wire2 = hil_fakes.Wire("receiver")
        fake_ser = hil_fakes.FakeSerial(wire2)
        with tempfile.TemporaryDirectory() as td:
            evidence_path = os.path.join(td, "receiver-console.bin")
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                evidence_path,
                serial_class=lambda: fake_ser,
                dtr=True,
                rts=False,
            )
            console.open()
            console.wait_reader_ready(5.0)
            time.sleep(0.15)
            console.close()
        self.assertTrue(fake_ser.is_open is False)
        for _when, dtr in fake_ser.dtr_ledger:
            self.assertTrue(dtr, "binding DTR state must survive every read")
        for _when, rts in fake_ser.rts_ledger:
            self.assertFalse(rts, "binding RTS state must survive every read")
        self.assertIn(("open", True), fake_ser.dtr_ledger)
        self.assertIn(("open", False), fake_ser.rts_ledger)

    def test_decode_error_retains_raw_bytes_and_fails(self):
        wire = hil_fakes.Wire(
            "receiver", chunks=[b"ok line\r\n", b"\xff\xfe bad\r\n", b"tail line\r\n"]
        )
        with tempfile.TemporaryDirectory() as td:
            evidence_path = os.path.join(td, "receiver-console.bin")
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                evidence_path,
                serial_class=lambda: hil_fakes.FakeSerial(wire),
            )
            console.open()
            try:
                # The fake reader consumes chunks asynchronously. Wait until
                # it has published the intended invalid-byte state, then prove
                # an already queued valid line cannot conceal that failure.
                deadline = time.monotonic() + 5.0
                while console.decode_error() is None and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertIsNotNone(console.decode_error())
                with self.assertRaises(SerialConsoleError):
                    console.wait_line(lambda l: l == "ok line", 5.0)
            finally:
                console.close()
            with open(evidence_path, "rb") as fh:
                raw = fh.read()
            self.assertIn(b"\xff\xfe", raw, "undecodable bytes remain retained")

    def test_write_retains_exact_encoded_tx_bytes(self):
        wire = hil_fakes.Wire("receiver")
        fake_ser = hil_fakes.FakeSerial(wire)
        payload = "hé →\r\n"
        with tempfile.TemporaryDirectory() as td:
            evidence_path = os.path.join(td, "receiver-console.bin")
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                evidence_path,
                serial_class=lambda: fake_ser,
            )
            console.open()
            try:
                self.assertEqual(
                    console.tx_evidence_path, serial_io.tx_evidence_path(evidence_path)
                )
                console.write(payload)
            finally:
                console.close()
            with open(console.tx_evidence_path, "rb") as fh:
                self.assertEqual(fh.read(), payload.encode("utf-8"))
        self.assertEqual(wire.writes, [payload.encode("utf-8")])

    def test_write_failure_retains_attempted_tx_bytes(self):
        class RaisingWriteSerial(hil_fakes.FakeSerial):
            def write(self, data):
                self.write_calls.append(data)
                raise OSError("injected serial write failure")

        wire = hil_fakes.Wire("receiver")
        fake_ser = RaisingWriteSerial(wire)
        payload = "é\r\n"
        with tempfile.TemporaryDirectory() as td:
            evidence_path = os.path.join(td, "receiver-console.bin")
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                evidence_path,
                serial_class=lambda: fake_ser,
            )
            console.open()
            try:
                with self.assertRaises(SerialConsoleError):
                    console.write(payload)
            finally:
                console.close()
            with open(console.tx_evidence_path, "rb") as fh:
                self.assertEqual(fh.read(), payload.encode("utf-8"))
        self.assertEqual(fake_ser.write_calls, [payload.encode("utf-8")])

    def test_short_write_fails_after_retaining_attempted_tx_bytes(self):
        class ShortWriteSerial(hil_fakes.FakeSerial):
            def write(self, data):
                self.write_calls.append(data)
                return len(data) - 1

        wire = hil_fakes.Wire("receiver")
        fake_ser = ShortWriteSerial(wire)
        payload = "short\r\n"
        with tempfile.TemporaryDirectory() as td:
            evidence_path = os.path.join(td, "receiver-console.bin")
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                evidence_path,
                serial_class=lambda: fake_ser,
            )
            console.open()
            try:
                with self.assertRaises(SerialConsoleError):
                    console.write(payload)
            finally:
                console.close()
            with open(console.tx_evidence_path, "rb") as fh:
                self.assertEqual(fh.read(), payload.encode("utf-8"))
        self.assertEqual(fake_ser.write_calls, [payload.encode("utf-8")])

    def test_rx_offset_tracks_retained_bytes_without_consuming_lines(self):
        payload = b"first \xe2\x86\x92 line\r\nsecond line\r\n"
        wire = hil_fakes.Wire("receiver", chunks=[payload])
        with tempfile.TemporaryDirectory() as td:
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                os.path.join(td, "receiver-console.bin"),
                serial_class=lambda: hil_fakes.FakeSerial(wire),
            )
            console.open()
            deadline = time.monotonic() + 2.0
            while console.rx_offset() != len(payload) and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertEqual(console.rx_offset(), len(payload))
            self.assertEqual(console.next_line(1.0), "first → line")
            self.assertEqual(console.next_line(1.0), "second line")
            self.assertEqual(console.rx_offset(), len(payload))
            console.close()
            with open(os.path.join(td, "receiver-console.bin"), "rb") as fh:
                self.assertEqual(fh.read(), payload)

    def test_open_failure_raises_and_closes_evidence(self):
        class BoomSerial:
            def open(self):
                raise OSError("injected open failure")

        with tempfile.TemporaryDirectory() as td:
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                os.path.join(td, "receiver-console.bin"),
                serial_class=lambda: BoomSerial(),
            )
            with self.assertRaises(SerialConsoleError):
                console.open()
            # Evidence handle released: no reader, no error state.
            self.assertIsNone(console.read_error())

    def test_exclusive_setup_failure_stops_before_port_open(self):
        class ExclusiveFailureSerial(hil_fakes.FakeSerial):
            def __init__(self, wire):
                self._initializing = True
                super().__init__(wire)
                self._initializing = False

            @property
            def exclusive(self):
                return False

            @exclusive.setter
            def exclusive(self, value):
                del value
                if self._initializing:
                    return
                raise OSError("exclusive unsupported")

        wire = hil_fakes.Wire("receiver")
        fake = ExclusiveFailureSerial(wire)
        with tempfile.TemporaryDirectory() as td:
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                os.path.join(td, "receiver-console.bin"),
                serial_class=lambda: fake,
            )
            with self.assertRaises(SerialConsoleError) as ctx:
                console.open()
        self.assertIn("exclusive", str(ctx.exception))
        self.assertFalse(fake.is_open)

    def test_close_with_blocking_reader_is_bounded(self):
        wire = hil_fakes.Wire("receiver")
        with tempfile.TemporaryDirectory() as td:
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                os.path.join(td, "receiver-console.bin"),
                serial_class=lambda: hil_fakes.FakeSerial(wire),
            )
            console.open()
            console.wait_reader_ready(5.0)
            start = time.monotonic()
            console.close()  # must not hang: fake close unblocks the reader
            self.assertLess(time.monotonic() - start, 5.0)
            # Raw evidence file retains whatever arrived.
            self.assertTrue(os.path.isfile(os.path.join(td, "receiver-console.bin")))

    def test_close_serializes_descriptor_close_with_gated_read(self):
        wire = hil_fakes.Wire("receiver")
        fake_ser = _BlockedReadSerial(
            wire, arm_immediately=True, payload=b"blocked line\r\n"
        )
        gate = _GateProbe()
        close_errors = []
        with tempfile.TemporaryDirectory() as td:
            evidence_path = os.path.join(td, "receiver-console.bin")
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                evidence_path,
                serial_class=lambda: fake_ser,
            )
            # Use production reader and close paths with an observed wrapper
            # around their existing gate, not a direct helper invocation.
            console._pump_gate = gate
            console.open()
            self.assertTrue(fake_ser.read_started.wait(2.0))

            def close_console():
                try:
                    console.close()
                except BaseException as exc:  # noqa: BLE001 - surface below
                    close_errors.append(exc)

            close_thread = threading.Thread(
                target=close_console, name="hil-close-race", daemon=True
            )
            close_thread.start()
            try:
                self.assertTrue(
                    gate.close_attempted.wait(2.0),
                    "close must acquire the reader gate before descriptor close",
                )
                self.assertFalse(fake_ser.close_called.is_set())
                self.assertFalse(fake_ser.close_during_read)
            finally:
                fake_ser.release_read.set()
                close_thread.join(2.0)

            self.assertFalse(close_thread.is_alive())
            self.assertEqual(close_errors, [])
            self.assertFalse(fake_ser.close_during_read)
            self.assertIsNone(console.read_error())
            with open(evidence_path, "rb") as fh:
                self.assertIn(b"blocked line\r\n", fh.read())

    def test_reader_ready_wait_uses_bounded_polling(self):
        wire = hil_fakes.Wire("receiver")
        with tempfile.TemporaryDirectory() as td:
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                os.path.join(td, "receiver-console.bin"),
                serial_class=lambda: hil_fakes.FakeSerial(wire),
            )
            console.open()
            self.assertTrue(console.wait_reader_ready(1.0))
            console.close()

    def test_reader_failure_surfaces_to_waiter(self):
        class FailingReadSerial(hil_fakes.FakeSerial):
            def read(self, size):
                del size
                raise OSError("injected reader failure")

        with tempfile.TemporaryDirectory() as td:
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                os.path.join(td, "receiver-console.bin"),
                serial_class=lambda: FailingReadSerial(hil_fakes.Wire("receiver")),
            )
            console.open()
            with self.assertRaises(SerialConsoleError):
                console.wait_line(lambda _line: True, 1.0)
            console.close()

    def test_mark_rx_requires_input_purge_support(self):
        class NoInputPurgeSerial(hil_fakes.FakeSerial):
            def __getattribute__(self, name):
                if name == "reset_input_buffer":
                    raise AttributeError(name)
                return super().__getattribute__(name)

        wire = hil_fakes.Wire("receiver")
        with tempfile.TemporaryDirectory() as td:
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                os.path.join(td, "receiver-console.bin"),
                serial_class=lambda: NoInputPurgeSerial(wire),
            )
            console.open()
            with self.assertRaises(SerialConsoleError) as ctx:
                console.mark_rx()
            console.close()
        self.assertIn("cannot purge input", str(ctx.exception))


class TestDefaultCommandRunner(unittest.TestCase):
    def test_cancelled_child_returns_partial_output(self):
        start = time.monotonic()

        with self.assertRaises(CommandCancelled) as ctx:
            runner.default_run_cmd(
                [
                    sys.executable,
                    "-c",
                    "import time; print('child started', flush=True); time.sleep(60)",
                ],
                timeout=5.0,
                cancel=lambda: time.monotonic() - start >= 0.25,
            )

        self.assertLess(time.monotonic() - start, 6.0)
        self.assertNotEqual(ctx.exception.completed_process.returncode, 0)
        self.assertIn("child started", ctx.exception.completed_process.stdout)


# ── source client ──────────────────────────────────────────────────


def _status_line(command_id, run_id, data):
    rec = hil_fakes.source_record("status", command_id, run_id, 10, 0, data)
    return "HIL1 " + json.dumps(rec, separators=(",", ":")) + "\n"


def _line_for(command_id, run_id, data, kind="status"):
    rec = hil_fakes.source_record(kind, command_id, run_id, 10, 0, data)
    return "HIL1 " + json.dumps(rec, separators=(",", ":")) + "\n"


class TestSourceClient(unittest.TestCase):
    def _client(self, lines, run_id=RUN_ID):
        console = LineConsole(lines)
        client = SourceClient(console, run_id)
        return client, console

    def test_hello_exact_command_and_constants(self):
        command_id = "cmd-0001"
        data = hil_fakes.source_hello_data(bond_count=0)
        client, console = self._client([_status_line(command_id, RUN_ID, data)])
        snap = client.hello()
        self.assertEqual(
            console.writes[0],
            'hil {"protocol_version":1,"command":"hello","command_id":"cmd-0001",'
            '"run_id":"rh2-0001"}\n',
        )
        self.assertEqual(snap.identity, "DB:A6:0C:05:A2:AA")
        self.assertEqual(snap.identity_type, "random")
        self.assertEqual(snap.bond_count, 0)
        self.assertEqual(snap.state, "idle")

    def test_hello_wrong_firmware_id_fails(self):
        data = hil_fakes.source_hello_data()
        data["firmware_id"] = "other"
        client, _ = self._client([_status_line("cmd-0001", RUN_ID, data)])
        with self.assertRaises(SourceClientError):
            client.hello()

    def test_hello_wrong_constant_fails(self):
        data = hil_fakes.source_hello_data()
        data["sample_rate"] = 44100
        client, _ = self._client([_status_line("cmd-0001", RUN_ID, data)])
        with self.assertRaises(SourceClientError) as ctx:
            client.hello()
        self.assertIn("sample_rate", str(ctx.exception))

    def test_hello_noncanonical_identity_fails(self):
        data = hil_fakes.source_hello_data(identity="zz:xx:yy:00:00:00")
        client, _ = self._client([_status_line("cmd-0001", RUN_ID, data)])
        with self.assertRaises(SourceClientError):
            client.hello()

    def test_command_line_length_limit(self):
        long_run = "r" * 500
        client, console = self._client([])
        client.run_id = long_run
        with self.assertRaises(SourceClientError) as ctx:
            client.configure(
                peer_address="DB:A6:0C:05:A2:AA",
                peer_address_type="random",
                mode="mono",
                profile="48_4_1",
                scored_sdu_count=120,
                signal_seed=1218649181,
                reconnect_policy="none",
            )
        self.assertIn("shell capacity", str(ctx.exception))
        self.assertEqual(console.writes, [])

    def test_command_line_exact_shell_boundary(self):
        client, _console = self._client([])
        # Command serialization itself accepts the full parser maximum of
        # 511 bytes and rejects 512. Use a legal extra payload key only for
        # this pure host-side length boundary test.
        empty_pad = client._command_line("hello", "cmd-0001", pad="")
        padding = "x" * (511 - len(empty_pad))
        line = client._command_line("hello", "cmd-0001", pad=padding)
        self.assertEqual(len(line), 511)
        with self.assertRaises(SourceClientError):
            client._command_line("hello", "cmd-0001", pad=padding + "x")

    def test_unpair_exact_fields(self):
        command_id = "cmd-0001"
        data = hil_fakes.source_status_data("unpair")
        client, console = self._client([_status_line(command_id, RUN_ID, data)])
        client.unpair("DB:A6:0C:05:A2:AA", "random")
        self.assertEqual(
            console.writes[0],
            'hil {"protocol_version":1,"command":"unpair","command_id":"cmd-0001",'
            '"run_id":"rh2-0001","peer_address":"DB:A6:0C:05:A2:AA",'
            '"peer_address_type":"random"}\n',
        )

    def test_unsolicited_record_fails_sync(self):
        data = hil_fakes.source_status_data("idle")
        unsolicited = _status_line(
            "cmd-9999", RUN_ID, hil_fakes.source_status_data("status")
        )
        client, _ = self._client([unsolicited, _status_line("cmd-0001", RUN_ID, data)])
        with self.assertRaises(SourceClientError) as ctx:
            client.idle()
        self.assertIn("active run", str(ctx.exception))

    def test_malformed_hil1_fails(self):
        data = hil_fakes.source_status_data("idle")
        client, _ = self._client(
            ["HIL1 not json\n", _status_line("cmd-0001", RUN_ID, data)]
        )
        with self.assertRaises(SourceClientError) as ctx:
            client.idle()
        self.assertIn("malformed HIL1", str(ctx.exception))

    def test_source_boot_diagnostic_record_fails_preflight(self):
        diagnostic = _line_for(
            "boot",
            "boot",
            {"command": "status", "ok": False, "error": "failed"},
        )
        client, _ = self._client([diagnostic])
        with self.assertRaises(SourceClientError) as ctx:
            client.hello()
        self.assertIn("diagnostic HIL1", str(ctx.exception))

    def test_source_unbound_parse_error_record_fails_preflight(self):
        diagnostic = _line_for(
            "parse-error",
            "unbound",
            {
                "command": "status",
                "ok": False,
                "error": "parse_error",
            },
        )
        client, _ = self._client([diagnostic])
        with self.assertRaises(SourceClientError) as ctx:
            client.hello()
        self.assertIn("diagnostic HIL1", str(ctx.exception))

    def test_status_not_ok_fails(self):
        data = hil_fakes.source_status_data("idle", ok=False, error="failed")
        client, _ = self._client([_status_line("cmd-0001", RUN_ID, data)])
        with self.assertRaises(SourceClientError) as ctx:
            client.idle()
        self.assertIn("not ok", str(ctx.exception))

    def test_start_full_pass_and_final_counters(self):
        command_id = "cmd-0001"
        lines = []
        lines.append(
            _line_for(command_id, RUN_ID, {"command": "start", "accepted": True}, "ack")
        )
        ms = 100
        for state in source_client.protocol.STATES:
            lines.append(_line_for(command_id, RUN_ID, {"state": state}, "state"))
        lines.append(_line_for(command_id, RUN_ID, {"verdict": "pass"}, "terminal"))
        final = hil_fakes.source_status_data(
            "status",
            active=False,
            state="teardown",
            verdict="pass",
            first_errno=0,
            streams=[{"seq": 764, "sub": 764, "sc": 120, "sf": 0, "cb": 764, "out": 0}],
            connected=False,
            security_level=0,
            sink_ase_count=0,
            bond_count=1,
        )
        lines.append(_status_line("cmd-0002", RUN_ID, final))
        client, console = self._client(lines)
        hook = []
        status = client.start(scored_complete_hook=lambda: hook.append(True))
        self.assertEqual(hook, [True])
        self.assertEqual(status["verdict"], "pass")
        self.assertEqual(status["streams"][0]["sc"], 120)
        self.assertEqual(
            console.writes[0],
            'hil {"protocol_version":1,"command":"start","command_id":"cmd-0001",'
            '"run_id":"rh2-0001"}\n',
        )
        self.assertEqual(
            console.writes[1],
            'hil {"protocol_version":1,"command":"status","command_id":"cmd-0002",'
            '"run_id":"rh2-0001"}\n',
            "final status uses a command ID distinct from start",
        )

    def test_active_status_requires_selected_stream_security_and_endpoint_evidence(
        self,
    ):
        row = rows.RH2_ROW
        client, _ = self._client([])
        active = hil_fakes.source_status_data(
            "status",
            active=True,
            state="streaming",
            verdict="none",
            mode=row.mode,
            profile=row.profile,
            reconnect=row.reconnect_policy,
            scored_target=row.scored_sdu_count,
            stream_count=row.stream_count,
            streams=[{"seq": 1, "sub": 1, "sc": 0, "sf": 0, "cb": 1, "out": 1}],
            connected=True,
            security_level=2,
            sink_ase_count=row.stream_count,
            group=True,
            bond_count=1,
        )
        client.validate_active_status(active, row, 0)

        active["sink_ase_count"] = 0
        with self.assertRaises(SourceClientError) as ctx:
            client.validate_active_status(active, row, 0)
        self.assertIn("sink_ase_count=0", str(ctx.exception))

    def test_final_status_requires_exact_total_and_cleanup_shape(self):
        row = rows.RH2_ROW
        client, _ = self._client([])
        terminal = hil_fakes.source_status_data(
            "status",
            active=False,
            state="teardown",
            verdict="pass",
            mode=row.mode,
            profile=row.profile,
            reconnect=row.reconnect_policy,
            scored_target=row.scored_sdu_count,
            stream_count=row.stream_count,
            streams=[
                {
                    "seq": row.expected_submitted_per_stream,
                    "sub": row.expected_submitted_per_stream,
                    "sc": row.expected_scored_per_stream,
                    "sf": 0,
                    "cb": row.expected_submitted_per_stream,
                    "out": 0,
                }
            ],
            connected=False,
            security_level=0,
            sink_ase_count=0,
            group=False,
            bond_count=1,
        )
        client._validate_final_status(terminal, row)

        terminal["streams"][0]["sub"] -= 1
        with self.assertRaises(SourceClientError) as ctx:
            client._validate_final_status(terminal, row)
        self.assertIn("submitted=", str(ctx.exception))

    def test_start_terminal_fail_rejected(self):
        command_id = "cmd-0001"
        lines = [
            _line_for(command_id, RUN_ID, {"command": "start", "accepted": True}, "ack")
        ]
        for state in source_client.protocol.STATES:
            lines.append(_line_for(command_id, RUN_ID, {"state": state}, "state"))
        lines.append(_line_for(command_id, RUN_ID, {"verdict": "fail"}, "terminal"))
        client, _ = self._client(lines)
        with self.assertRaises(SourceClientError) as ctx:
            client.start()
        self.assertIn("not pass", str(ctx.exception))

    def test_bounded_stop_cleanup_runs_idle(self):
        # A started client whose run never reaches terminal: start() fails
        # on the stall, and the bounded stop/idle cleanup must then run on
        # the scripted wire.
        command_id = "cmd-0001"
        lines = [
            _line_for(
                command_id, RUN_ID, {"command": "start", "accepted": True}, "ack"
            ),
            _line_for(command_id, RUN_ID, {"state": "idle"}, "state"),
        ]
        stack = lifecycle.CleanupStack()

        cleanup_lines = [
            _status_line("cmd-0002", RUN_ID, hil_fakes.source_status_data("stop")),
            _line_for(
                command_id, RUN_ID, {"state": "teardown", "cause": "stop"}, "state"
            ),
            _line_for(command_id, RUN_ID, {"verdict": "fail"}, "terminal"),
            _status_line("cmd-0003", RUN_ID, hil_fakes.source_status_data("idle")),
        ]

        class DeferredCleanupConsole(LineConsole):
            def write(self, data):
                super().write(data)
                command = json.loads(data[4:])["command"]
                if command == "stop":
                    self.lines.extend(cleanup_lines[:3])
                elif command == "idle":
                    self.lines.extend(cleanup_lines[3:])

        console = DeferredCleanupConsole(lines)
        client = SourceClient(console, RUN_ID)
        client._cleanup = stack
        with self.assertRaises(SourceClientError):
            client.start(scored_timeout=0.0)
        stack.close()
        commands = [w.split('"command":"')[1].split('"')[0] for w in console.writes]
        self.assertEqual(commands, ["start", "stop", "idle"])

    def test_bounded_stop_drain_rejects_queued_source_diagnostic(self):
        command_id = "cmd-0001"
        lines = [
            _line_for(command_id, RUN_ID, {"command": "start", "accepted": True}, "ack")
        ]
        for state in source_client.protocol.STATES[:-1]:
            lines.append(_line_for(command_id, RUN_ID, {"state": state}, "state"))
        lines.append(
            _line_for(
                "parse-error",
                "unbound",
                {"command": "status", "ok": False, "error": "parse_error"},
            )
        )
        stack = lifecycle.CleanupStack()
        client, console = self._client(lines)
        client._cleanup = stack
        client._tracker = source_client.protocol.HilRunTracker(RUN_ID, command_id)
        stack.register("source stop/idle", client._bounded_stop)

        with self.assertRaises(lifecycle.CleanupFailure) as ctx:
            stack.close()

        self.assertEqual(ctx.exception.failures[0][0], "source stop/idle")
        self.assertIn(
            "source diagnostic HIL1 record", str(ctx.exception.failures[0][1])
        )
        self.assertEqual(console.writes, [])

    def test_source_wait_polls_cancellation_before_deadline(self):
        console = LineConsole([])
        client = SourceClient(console, RUN_ID, cancel=lambda: True)
        with self.assertRaises(source_client.SourceClientCancelled):
            client.hello(timeout=60.0)
        self.assertEqual(console.writes, [])


# ── receiver parsing ───────────────────────────────────────────────


def _sdc_trace_raw(schedule_line=None, snapshot_line=None):
    lines = [
        b"SDC LE Remove ISO Data Path trace armed",
        b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry",
        b"SDC LE Remove ISO Data Path trace entry",
        b"SDC LE Remove ISO Data Path trace return: status=0x00",
        b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0",
        b"SDC LE Remove ISO Data Path multithreading_lock_release entry",
        b"SDC LE Remove ISO Data Path multithreading_lock_release return",
        b"SDC LE Remove ISO Data Path k_work_submit_to_queue entry",
        b"SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=1",
    ]
    if schedule_line is not None:
        lines.append(schedule_line)
    if snapshot_line is not None:
        lines.append(snapshot_line)
    lines.extend(
        [
            b"SDC LE Remove ISO Data Path hci_internal_msg_get entry",
            b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=0",
            b"SDC LE Remove ISO Data Path completion: status=0x00",
        ]
    )
    return b"\r\n".join(lines) + b"\r\n"


def _sdc_receive_disposition_trace_raw(
    fetch=False,
    allocation=None,
    fetch_status=0,
    fetch_msg_type=8,
    include_fetch_completion=True,
):
    lines = [
        b"SDC LE Remove ISO Data Path trace armed",
        b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry",
        b"SDC LE Remove ISO Data Path trace entry",
        b"SDC LE Remove ISO Data Path trace return: status=0x00",
        b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0",
        b"SDC LE Remove ISO Data Path multithreading_lock_release entry",
        b"SDC LE Remove ISO Data Path multithreading_lock_release return",
        b"SDC LE Remove ISO Data Path k_work_submit_to_queue entry",
        b"SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=1",
        b"SDC LE Remove ISO Data Path receive disposition trace armed: queue_is_mpsl=1",
    ]
    if fetch:
        fetch_msg_type_text = "na" if fetch_status != 0 else str(fetch_msg_type)
        lines.extend(
            [
                b"SDC LE Remove ISO Data Path receive disposition fetch entry",
                b"SDC LE Remove ISO Data Path hci_internal_msg_get entry",
                (
                    b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=%d"
                    % fetch_status
                ),
                b"SDC LE Remove ISO Data Path receive disposition fetch return: "
                + (
                    b"status=%d msg_type=%s"
                    % (fetch_status, fetch_msg_type_text.encode("ascii"))
                ),
            ]
        )
        if include_fetch_completion:
            lines.append(b"SDC LE Remove ISO Data Path completion: status=0x00")
    if allocation is None:
        allocation = (
            b"SDC LE Remove ISO Data Path receive disposition allocation: "
            b"kind=rx type=32 buffer_available=0 target_busy=0x1"
        )
    if allocation is not False:
        lines.append(allocation)
    return b"\r\n".join(lines) + b"\r\n"


def _sdc_iso_rx_lifetime_trace_raw():
    lifetime_arm = (
        b"SDC LE Remove ISO Data Path ISO RX lifetime trace armed: capacity=3"
    )
    disable_snapshot = (
        b"SDC LE Remove ISO Data Path ISO RX lifetime snapshot: reason=disable "
        b"capacity=3 outstanding=0 high_water=0 allocations=0 final_unrefs=0 "
        b"callbacks_active=0 callbacks_total=0"
    )
    disable_disposition = (
        b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
        b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
        b"tx_notify_flush_returned=0 host_returned=0 "
        b"app_callback_seen=0 "
        b"unclassified=0"
    )
    unavailable_snapshot = (
        b"SDC LE Remove ISO Data Path ISO RX lifetime snapshot: reason=unavailable "
        b"capacity=3 outstanding=3 high_water=3 allocations=3 final_unrefs=0 "
        b"callbacks_active=0 callbacks_total=4"
    )
    unavailable_disposition = (
        b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
        b"reason=unavailable undispatched=1 host_dispatched=0 tx_notify_flush_entered=1 "
        b"tx_notify_flush_returned=1 host_returned=0 "
        b"app_callback_seen=0 "
        b"unclassified=0"
    )
    first_free = (
        b"SDC LE Remove ISO Data Path ISO RX lifetime first free after unavailable: "
        b"outstanding_before=3 callbacks_active=0 allocations=3 final_unrefs=1"
    )
    allocation = (
        b"SDC LE Remove ISO Data Path receive disposition allocation: "
        b"kind=rx type=32 buffer_available=0 target_busy=0x1"
    )
    receive = _sdc_receive_disposition_trace_raw(allocation=allocation)
    receive = receive.replace(
        allocation + b"\r\n",
        unavailable_snapshot
        + b"\r\n"
        + unavailable_disposition
        + b"\r\n"
        + allocation
        + b"\r\n"
        + first_free
        + b"\r\n",
        1,
    )
    return (
        lifetime_arm
        + b"\r\n"
        + disable_snapshot
        + b"\r\n"
        + disable_disposition
        + b"\r\n"
        + receive
    )


def _sdc_iso_rx_lifetime_semaphore_trace_raw():
    raw = _sdc_iso_rx_lifetime_trace_raw()
    raw = raw.replace(
        b" tx_notify_flush_entered=0 tx_notify_flush_returned=0",
        b" tx_notify_flush_entered=0 tx_notify_flush_semaphore_entered=0 "
        b"tx_notify_flush_semaphore_returned=1 tx_notify_flush_returned=0",
    )
    return raw.replace(
        b" undispatched=1 host_dispatched=0 tx_notify_flush_entered=1 "
        b"tx_notify_flush_returned=1 host_returned=0",
        b" undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
        b"tx_notify_flush_semaphore_entered=1 tx_notify_flush_semaphore_returned=0 "
        b"tx_notify_flush_returned=0 host_returned=0",
    )


def _sdc_iso_rx_lifetime_pend_trace_raw():
    raw = _sdc_iso_rx_lifetime_semaphore_trace_raw()
    raw = raw.replace(
        b"tx_notify_flush_semaphore_entered=0 tx_notify_flush_semaphore_returned=1",
        b"tx_notify_flush_semaphore_entered=0 "
        b"tx_notify_flush_semaphore_pend_entered=0 "
        b"tx_notify_flush_semaphore_pend_returned=0 "
        b"tx_notify_flush_semaphore_returned=1",
    )
    return raw.replace(
        b"tx_notify_flush_semaphore_entered=1 tx_notify_flush_semaphore_returned=0",
        b"tx_notify_flush_semaphore_entered=1 "
        b"tx_notify_flush_semaphore_pend_entered=1 "
        b"tx_notify_flush_semaphore_pend_returned=0 "
        b"tx_notify_flush_semaphore_returned=0",
    )


def _sdc_iso_rx_lifetime_sched_give_trace_raw():
    raw = _sdc_iso_rx_lifetime_pend_trace_raw()
    raw = raw.replace(
        b"tx_notify_flush_semaphore_pend_entered=0 "
        b"tx_notify_flush_semaphore_pend_returned=0",
        b"tx_notify_flush_semaphore_pend_entered=0 "
        b"tx_notify_flush_semaphore_pend_thread_marked_pending=0 "
        b"tx_notify_flush_semaphore_give_entered=0 "
        b"tx_notify_flush_semaphore_pend_returned=0",
        1,
    )
    return raw.replace(
        b"tx_notify_flush_semaphore_entered=1 "
        b"tx_notify_flush_semaphore_pend_entered=1 "
        b"tx_notify_flush_semaphore_pend_returned=0 "
        b"tx_notify_flush_semaphore_returned=0",
        b"tx_notify_flush_semaphore_entered=0 "
        b"tx_notify_flush_semaphore_pend_entered=0 "
        b"tx_notify_flush_semaphore_pend_thread_marked_pending=0 "
        b"tx_notify_flush_semaphore_give_entered=1 "
        b"tx_notify_flush_semaphore_pend_returned=0 "
        b"tx_notify_flush_semaphore_returned=0",
        1,
    )


def _sdc_scheduler_trace_raw(
    msg_get_between_unlock=False,
    extended=False,
    state_line=None,
    post_unlock_yield=False,
    yield_state_line=None,
):
    scheduler_arm = b"SDC LE Remove ISO Data Path scheduler unlock trace armed"
    if extended:
        scheduler_arm += b": queue_is_mpsl=1"
    lines = [
        b"SDC LE Remove ISO Data Path trace armed",
        b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry",
        b"SDC LE Remove ISO Data Path trace entry",
        b"SDC LE Remove ISO Data Path trace return: status=0x00",
        b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0",
        b"SDC LE Remove ISO Data Path multithreading_lock_release entry",
        b"SDC LE Remove ISO Data Path multithreading_lock_release return",
        b"SDC LE Remove ISO Data Path k_work_submit_to_queue entry",
        b"SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=1",
        scheduler_arm,
        b"SDC LE Remove ISO Data Path k_sched_unlock entry",
    ]
    msg_get = [
        b"SDC LE Remove ISO Data Path hci_internal_msg_get entry",
        b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=0",
        b"SDC LE Remove ISO Data Path completion: status=0x00",
    ]
    if msg_get_between_unlock:
        lines.extend(msg_get)
    if state_line is not None:
        lines.append(state_line)
    lines.append(b"SDC LE Remove ISO Data Path k_sched_unlock return")
    if post_unlock_yield:
        if yield_state_line is None:
            yield_state_line = (
                b"SDC LE Remove ISO Data Path post-unlock yield state: "
                b"busy=0x4 mpsl_state=PENDING resumed_current_is_sender=1 "
                b"mpsl_switches_during_yield=1"
            )
        lines.extend(
            [
                b"SDC LE Remove ISO Data Path post-unlock yield trace armed",
                b"SDC LE Remove ISO Data Path post-unlock yield entry",
                yield_state_line,
                b"SDC LE Remove ISO Data Path post-unlock yield return",
            ]
        )
    if not msg_get_between_unlock:
        lines.extend(msg_get)
    return b"\r\n".join(lines) + b"\r\n"


class TestReceiverParsing(unittest.TestCase):
    def test_identity_parsing(self):
        parsed = receiver.parse_identity("Identity: DB:A6:0C:05:A2:AA (random)")
        self.assertEqual(parsed["address"], "DB:A6:0C:05:A2:AA")
        self.assertEqual(parsed["address_type"], "random")
        self.assertIsNone(receiver.parse_identity("Identity: xx (public)"))
        self.assertIsNone(receiver.parse_identity("some other line"))

    def test_bond_count_parsing(self):
        self.assertEqual(receiver.parse_bond_count("Bond count: 0"), 0)
        self.assertEqual(receiver.parse_bond_count("Bond count: 12"), 12)
        self.assertIsNone(receiver.parse_bond_count("Bond count: none"))

    def test_hci_trace_parser_retains_offsets_and_prearm_drop(self):
        drop_line = b"uart:~$ --- 276 messages dropped ---"
        raw = (
            drop_line + b"\r\n"
            b"<inf> bt_bap: HCI remove ISO path trace armed: core_id=7 "
            b"driver_id=9 core_level=4 driver_level=4\r\n"
            b"<dbg> bt_hci_core: Sending command 0x206f (buf 0x123) to driver\r\n"
            b"<dbg> bt_sdc_hci_driver: Command Complete (0x206f) status: 0x00, "
            b"ncmd: 1, len 6\r\n"
            b"<dbg> bt_sdc_hci_driver: Command Status (0x206f) status: 0x00\r\n"
            b"<dbg> bt_hci_core: opcode 0x206f status 0x00 Success buf 0x123\r\n"
        )

        parsed = receiver.parse_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["dropped_messages"][0]["count"], 276)
        self.assertEqual(parsed["dropped_messages"][0]["classification"], "before_arm")
        self.assertEqual(parsed["dropped_messages"][0]["line"], drop_line.decode())
        drop_start = raw.index(drop_line)
        drop_end = drop_start + len(drop_line) + len(b"\r\n")
        self.assertEqual(parsed["dropped_messages"][0]["start_offset"], drop_start)
        self.assertEqual(parsed["dropped_messages"][0]["end_offset"], drop_end)
        self.assertEqual(raw[drop_start:drop_end], drop_line + b"\r\n")
        self.assertEqual(parsed["arm_markers"][0]["core_id"], 7)
        self.assertEqual(
            parsed["arm_markers"][0]["line"],
            "<inf> bt_bap: HCI remove ISO path trace armed: core_id=7 "
            "driver_id=9 core_level=4 driver_level=4",
        )
        self.assertEqual(parsed["arm_markers"][0]["start_offset"], raw.index(b"<inf>"))
        self.assertEqual(len(parsed["core_sends"]), 1)
        self.assertEqual(parsed["core_sends"][0]["classification"], "after_arm")
        self.assertEqual(len(parsed["driver_command_complete"]), 1)
        self.assertEqual(
            parsed["driver_command_complete"][0]["classification"], "after_arm"
        )
        self.assertEqual(len(parsed["driver_command_status"]), 1)
        self.assertEqual(
            parsed["driver_command_status"][0]["classification"], "after_arm"
        )
        self.assertEqual(len(parsed["core_completion_done"]), 1)
        self.assertEqual(
            parsed["core_completion_done"][0]["classification"], "after_arm"
        )

    def test_hci_trace_parser_parses_prompt_prefixed_postarm_drop(self):
        arm = (
            b"<inf> bt_bap: HCI remove ISO path trace armed: core_id=7 "
            b"driver_id=9 core_level=4 driver_level=4\r\n"
        )
        drop_line = b"uart:~$ --- 1 messages dropped ---"
        send = b"<dbg> bt_hci_core: Sending command 0x206f (buf 0x123) to driver\r\n"

        parsed = receiver.parse_hci_remove_iso_path_trace(
            arm + drop_line + b"\r\n" + send
        )

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["dropped_messages"][0]["count"], 1)
        self.assertEqual(parsed["dropped_messages"][0]["classification"], "after_arm")
        self.assertEqual(parsed["dropped_messages"][0]["line"], drop_line.decode())
        self.assertIn("dropped messages after arm: 1", parsed["validation_errors"])

    def test_hci_trace_parser_rejects_foreign_prefixed_drop(self):
        arm = (
            b"<inf> bt_bap: HCI remove ISO path trace armed: core_id=7 "
            b"driver_id=9 core_level=4 driver_level=4\r\n"
        )
        foreign_line = b"foreign: --- 1 messages dropped ---"
        send = b"<dbg> bt_hci_core: Sending command 0x206f (buf 0x123) to driver\r\n"

        parsed = receiver.parse_hci_remove_iso_path_trace(
            arm + foreign_line + b"\r\n" + send
        )

        self.assertEqual(parsed["dropped_messages"], [])
        self.assertIn(
            "malformed dropped-message record: %s" % foreign_line.decode(),
            parsed["parser_errors"],
        )

    def test_hci_trace_parser_requires_postarm_send_and_retains_prearm_record(self):
        prearm = b"<dbg> bt_hci_core: Sending command 0x206f (buf 0x123) to driver\r\n"
        arm = (
            b"<inf> bt_bap: HCI remove ISO path trace armed: core_id=7 "
            b"driver_id=9 core_level=4 driver_level=4\r\n"
        )
        raw = prearm + arm

        parsed = receiver.parse_hci_remove_iso_path_trace(raw)

        self.assertEqual(len(parsed["core_sends"]), 1)
        self.assertEqual(parsed["core_sends"][0]["classification"], "before_arm")
        self.assertEqual(parsed["core_sends"][0]["line"], prearm[:-2].decode())
        self.assertEqual(parsed["core_sends"][0]["start_offset"], 0)
        self.assertEqual(parsed["core_sends"][0]["end_offset"], len(prearm))
        self.assertIn(
            "missing post-arm bt_hci_core 0x206f send record",
            parsed["validation_errors"],
        )

    def test_hci_trace_parser_retains_timeout_and_parser_error(self):
        raw = (
            b"HCI remove ISO path trace armed: core_id=x driver_id=9 "
            b"core_level=4 driver_level=4\n"
            b"Controller unresponsive, command opcode 0x206f timeout with err -11\n"
        )

        parsed = receiver.parse_hci_remove_iso_path_trace(raw)

        self.assertTrue(parsed["parser_errors"])
        self.assertTrue(parsed["validation_errors"])
        self.assertEqual(len(parsed["host_timeout_fatal"]), 1)

    def test_sdc_receive_disposition_retained_iso_no_buffer_is_valid_evidence(self):
        raw = _sdc_receive_disposition_trace_raw()

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        self.assertEqual(
            parsed["receive_disposition_outcome"], "retained_iso_buffer_unavailable"
        )
        self.assertEqual(len(parsed["receive_disposition_arm_markers"]), 1)
        self.assertEqual(
            parsed["receive_disposition_arm_markers"][0]["classification"],
            "after_arm",
        )
        self.assertEqual(
            parsed["receive_disposition_arm_markers"][0]["queue_is_mpsl"], 1
        )
        self.assertEqual(parsed["receive_disposition_fetch_entries"], [])
        self.assertEqual(parsed["receive_disposition_fetch_returns"], [])
        self.assertEqual(len(parsed["receive_disposition_allocations"]), 1)
        allocation = parsed["receive_disposition_allocations"][0]
        self.assertEqual(allocation["classification"], "after_arm")
        self.assertEqual(allocation["kind"], "rx")
        self.assertEqual(allocation["type"], 32)
        self.assertFalse(allocation["buffer_available"])
        self.assertEqual(allocation["target_busy"], 0x1)
        allocation_line = (
            b"SDC LE Remove ISO Data Path receive disposition allocation: "
            b"kind=rx type=32 buffer_available=0 target_busy=0x1"
        )
        allocation_start = raw.index(allocation_line)
        self.assertEqual(allocation["start_offset"], allocation_start)
        self.assertEqual(
            allocation["end_offset"], allocation_start + len(allocation_line) + 2
        )
        self.assertEqual(allocation["line"], allocation_line.decode())

    def test_sdc_receive_disposition_fetch_path_keeps_existing_completion_requirements(
        self,
    ):
        raw = _sdc_receive_disposition_trace_raw(
            fetch=True,
            fetch_msg_type=4,
            allocation=(
                b"SDC LE Remove ISO Data Path receive disposition allocation: "
                b"kind=evt evt=0x0e discardable=0 buffer_available=1 target_busy=0x1"
            ),
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(len(parsed["receive_disposition_fetch_entries"]), 1)
        self.assertEqual(len(parsed["receive_disposition_fetch_returns"]), 1)
        fetch_return = parsed["receive_disposition_fetch_returns"][0]
        self.assertEqual(fetch_return["status"], 0)
        self.assertEqual(fetch_return["msg_type"], 4)
        allocation = parsed["receive_disposition_allocations"][0]
        self.assertEqual(allocation["kind"], "evt")
        self.assertEqual(allocation["evt"], 0x0E)
        self.assertFalse(allocation["discardable"])
        self.assertTrue(allocation["buffer_available"])
        self.assertIsNone(parsed["receive_disposition_outcome"])

        missing_completion = raw.replace(
            b"SDC LE Remove ISO Data Path completion: status=0x00\r\n", b"", 1
        )
        failed = receiver.parse_sdc_hci_remove_iso_path_trace(missing_completion)
        self.assertTrue(
            any(
                "missing post-arm SDC completion" in error
                for error in failed["validation_errors"]
            )
        )

    def test_sdc_receive_disposition_fetch_error_is_bounded_outcome(self):
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_receive_disposition_trace_raw(
                fetch=True,
                fetch_status=-5,
                include_fetch_completion=False,
                allocation=False,
            )
        )

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["receive_disposition_outcome"], "fetch_error")
        self.assertEqual(parsed["receive_disposition_fetch_returns"][0]["status"], -5)
        self.assertEqual(parsed["receive_disposition_allocations"], [])
        self.assertEqual(parsed["sdc_completions"], [])

    def test_sdc_receive_disposition_fetched_buffer_unavailable_is_bounded_outcome(
        self,
    ):
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_receive_disposition_trace_raw(
                fetch=True,
                include_fetch_completion=False,
            )
        )

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(
            parsed["receive_disposition_outcome"], "fetched_buffer_unavailable"
        )
        self.assertEqual(parsed["receive_disposition_fetch_returns"][0]["msg_type"], 8)
        self.assertEqual(len(parsed["receive_disposition_allocations"]), 1)
        self.assertEqual(parsed["sdc_completions"], [])

        with_completion = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_receive_disposition_trace_raw(fetch=True)
        )
        self.assertEqual(with_completion["parser_errors"], [])
        self.assertEqual(with_completion["validation_errors"], [])
        self.assertEqual(
            with_completion["receive_disposition_outcome"],
            "fetched_buffer_unavailable",
        )

    def test_sdc_receive_disposition_fetched_evt_buffer_unavailable_with_completion(
        self,
    ):
        allocation = (
            b"SDC LE Remove ISO Data Path receive disposition allocation: "
            b"kind=evt evt=0x0e discardable=0 buffer_available=0 target_busy=0x1"
        )
        raw = _sdc_receive_disposition_trace_raw(
            fetch=True,
            fetch_msg_type=4,
            allocation=allocation,
        )
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(
            parsed["receive_disposition_outcome"], "fetched_buffer_unavailable"
        )
        self.assertEqual(len(parsed["receive_disposition_fetch_returns"]), 1)
        fetch_return = parsed["receive_disposition_fetch_returns"][0]
        self.assertEqual(fetch_return["status"], 0)
        self.assertEqual(fetch_return["msg_type"], 4)
        self.assertEqual(len(parsed["sdc_completions"]), 1)
        self.assertEqual(parsed["sdc_completions"][0]["status"], 0)
        self.assertEqual(len(parsed["receive_disposition_allocations"]), 1)
        allocation_record = parsed["receive_disposition_allocations"][0]
        self.assertEqual(allocation_record["kind"], "evt")
        self.assertEqual(allocation_record["evt"], 0x0E)
        self.assertFalse(allocation_record["buffer_available"])
        self.assertEqual(allocation_record["target_busy"], 0x1)
        self.assertEqual(
            allocation_record["line"],
            allocation.decode(),
        )
        allocation_start = raw.index(allocation)
        self.assertEqual(allocation_record["start_offset"], allocation_start)
        self.assertEqual(
            allocation_record["end_offset"], allocation_start + len(allocation) + 2
        )

        completion = b"SDC LE Remove ISO Data Path completion: status=0x00"
        fetch_return = (
            b"SDC LE Remove ISO Data Path receive disposition fetch return: "
            b"status=0 msg_type=4"
        )
        before_fetch_return = receiver.parse_sdc_hci_remove_iso_path_trace(
            raw.replace(
                fetch_return + b"\r\n" + completion + b"\r\n",
                completion + b"\r\n" + fetch_return + b"\r\n",
                1,
            )
        )
        self.assertIn(
            "fetched buffer unavailable completion must follow scoped receive fetch return",
            before_fetch_return["validation_errors"],
        )

        after_allocation = receiver.parse_sdc_hci_remove_iso_path_trace(
            raw.replace(
                completion + b"\r\n" + allocation + b"\r\n",
                allocation + b"\r\n" + completion + b"\r\n",
                1,
            )
        )
        self.assertIn(
            "fetched buffer unavailable completion must precede scoped allocation",
            after_allocation["validation_errors"],
        )

    def test_sdc_receive_disposition_retained_buffer_rejects_old_completion_path(self):
        allocation = (
            b"SDC LE Remove ISO Data Path receive disposition allocation: "
            b"kind=rx type=32 buffer_available=0 target_busy=0x1\r\n"
        )
        old_path = (
            b"SDC LE Remove ISO Data Path hci_internal_msg_get entry\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=0\r\n"
            b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
        )
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_receive_disposition_trace_raw().replace(
                allocation, old_path + allocation
            )
        )

        self.assertIsNone(parsed["receive_disposition_outcome"])
        self.assertIn(
            "retained ISO buffer unavailable must not have hci_internal_msg_get",
            parsed["validation_errors"],
        )
        self.assertIn(
            "retained ISO buffer unavailable must not have completion",
            parsed["validation_errors"],
        )

    def test_sdc_receive_disposition_validates_data_and_iso_mapping(self):
        data_allocation = (
            b"SDC LE Remove ISO Data Path receive disposition allocation: "
            b"kind=rx type=8 buffer_available=1 target_busy=0x1"
        )
        valid = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_receive_disposition_trace_raw(
                fetch=True,
                fetch_msg_type=2,
                allocation=data_allocation,
            )
        )
        self.assertEqual(valid["validation_errors"], [])
        self.assertIsNone(valid["receive_disposition_outcome"])

        mismatched = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_receive_disposition_trace_raw(
                fetch=True,
                fetch_msg_type=2,
                allocation=(
                    b"SDC LE Remove ISO Data Path receive disposition allocation: "
                    b"kind=rx type=32 buffer_available=1 target_busy=0x1"
                ),
            )
        )
        self.assertIn(
            "receive disposition fetch/allocation type mismatch: msg_type=2 kind=rx",
            mismatched["validation_errors"],
        )

    def test_sdc_receive_disposition_rejects_malformed_duplicate_and_missing_records(
        self,
    ):
        valid = _sdc_receive_disposition_trace_raw()
        malformed = valid.replace(
            b"kind=rx type=32 buffer_available=0 target_busy=0x1",
            b"kind=rx type=bad buffer_available=0 target_busy=0x1",
            1,
        )
        malformed_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(malformed)
        self.assertTrue(
            any(
                "malformed receive disposition allocation marker" in error
                for error in malformed_parsed["parser_errors"]
            )
        )

        duplicate_arm = valid.replace(
            b"SDC LE Remove ISO Data Path receive disposition trace armed: queue_is_mpsl=1\r\n",
            b"SDC LE Remove ISO Data Path receive disposition trace armed: queue_is_mpsl=1\r\n"
            b"SDC LE Remove ISO Data Path receive disposition trace armed: queue_is_mpsl=1\r\n",
            1,
        )
        duplicate_arm_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            duplicate_arm
        )
        self.assertIn(
            "duplicate receive disposition arm marker",
            duplicate_arm_parsed["validation_errors"],
        )

        allocation = (
            b"SDC LE Remove ISO Data Path receive disposition allocation: "
            b"kind=rx type=32 buffer_available=0 target_busy=0x1\r\n"
        )
        duplicate_allocation = valid.replace(
            allocation,
            allocation + allocation,
            1,
        )
        duplicate_allocation_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            duplicate_allocation
        )
        self.assertIn(
            "duplicate receive disposition allocation marker",
            duplicate_allocation_parsed["validation_errors"],
        )

        missing_allocation = valid.replace(allocation, b"", 1)
        missing_allocation_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            missing_allocation
        )
        self.assertIn(
            "expected exactly one post-arm receive disposition allocation, found 0",
            missing_allocation_parsed["validation_errors"],
        )

    def test_sdc_iso_rx_lifetime_parser_accepts_valid_markers_and_offsets(self):
        raw = _sdc_iso_rx_lifetime_trace_raw()

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        self.assertEqual(len(parsed["iso_rx_lifetime_arm_markers"]), 1)
        self.assertEqual(parsed["iso_rx_lifetime_arm_markers"][0]["capacity"], 3)
        self.assertEqual(
            parsed["iso_rx_lifetime_arm_markers"][0]["classification"],
            "before_arm",
        )
        self.assertEqual(len(parsed["iso_rx_lifetime_snapshots"]), 2)
        self.assertEqual(
            [record["reason"] for record in parsed["iso_rx_lifetime_snapshots"]],
            ["disable", "unavailable"],
        )
        self.assertEqual(
            parsed["iso_rx_lifetime_snapshots"][0]["classification"], "before_arm"
        )
        self.assertEqual(
            parsed["iso_rx_lifetime_snapshots"][1]["classification"], "after_arm"
        )
        unavailable = parsed["iso_rx_lifetime_snapshots"][1]
        self.assertEqual(unavailable["capacity"], 3)
        self.assertEqual(unavailable["outstanding"], 3)
        self.assertEqual(unavailable["high_water"], 3)
        self.assertEqual(unavailable["allocations"], 3)
        self.assertEqual(unavailable["final_unrefs"], 0)
        self.assertEqual(unavailable["callbacks_total"], 4)
        disposition_records = parsed["iso_rx_lifetime_disposition_snapshots"]
        self.assertEqual(len(disposition_records), 2)
        self.assertEqual(
            [record["reason"] for record in disposition_records],
            ["disable", "unavailable"],
        )
        self.assertEqual(disposition_records[0]["classification"], "before_arm")
        self.assertEqual(disposition_records[1]["classification"], "after_arm")
        self.assertEqual(disposition_records[0]["undispatched"], 0)
        self.assertEqual(disposition_records[0]["host_dispatched"], 0)
        self.assertEqual(disposition_records[0]["tx_notify_flush_entered"], 0)
        self.assertIsNone(disposition_records[0]["tx_notify_flush_semaphore_entered"])
        self.assertIsNone(
            disposition_records[0]["tx_notify_flush_semaphore_pend_entered"]
        )
        self.assertIsNone(
            disposition_records[0][
                "tx_notify_flush_semaphore_pend_thread_marked_pending"
            ]
        )
        self.assertIsNone(
            disposition_records[0]["tx_notify_flush_semaphore_give_entered"]
        )
        self.assertIsNone(
            disposition_records[0]["tx_notify_flush_semaphore_pend_returned"]
        )
        self.assertIsNone(disposition_records[0]["tx_notify_flush_semaphore_returned"])
        self.assertEqual(disposition_records[0]["tx_notify_flush_returned"], 0)
        self.assertEqual(disposition_records[0]["host_returned"], 0)
        self.assertEqual(disposition_records[0]["app_callback_seen"], 0)
        self.assertEqual(disposition_records[0]["unclassified"], 0)
        self.assertEqual(disposition_records[1]["undispatched"], 1)
        self.assertEqual(disposition_records[1]["host_dispatched"], 0)
        self.assertEqual(disposition_records[1]["tx_notify_flush_entered"], 1)
        self.assertIsNone(disposition_records[1]["tx_notify_flush_semaphore_entered"])
        self.assertIsNone(
            disposition_records[1]["tx_notify_flush_semaphore_pend_entered"]
        )
        self.assertIsNone(
            disposition_records[1][
                "tx_notify_flush_semaphore_pend_thread_marked_pending"
            ]
        )
        self.assertIsNone(
            disposition_records[1]["tx_notify_flush_semaphore_give_entered"]
        )
        self.assertIsNone(
            disposition_records[1]["tx_notify_flush_semaphore_pend_returned"]
        )
        self.assertIsNone(disposition_records[1]["tx_notify_flush_semaphore_returned"])
        self.assertEqual(disposition_records[1]["tx_notify_flush_returned"], 1)
        self.assertEqual(disposition_records[1]["host_returned"], 0)
        self.assertEqual(disposition_records[1]["app_callback_seen"], 0)
        self.assertEqual(disposition_records[1]["unclassified"], 0)
        disposition_lines = (
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_returned=0 host_returned=0 "
            b"app_callback_seen=0 "
            b"unclassified=0",
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
            b"reason=unavailable undispatched=1 host_dispatched=0 tx_notify_flush_entered=1 "
            b"tx_notify_flush_returned=1 host_returned=0 "
            b"app_callback_seen=0 "
            b"unclassified=0",
        )
        for record, disposition_line in zip(disposition_records, disposition_lines):
            disposition_start = raw.index(disposition_line)
            self.assertEqual(record["start_offset"], disposition_start)
            self.assertEqual(
                record["end_offset"], disposition_start + len(disposition_line) + 2
            )
            self.assertEqual(record["line"], disposition_line.decode())
            self.assertIsNone(
                record["tx_notify_flush_semaphore_pend_thread_marked_pending"]
            )
            self.assertIsNone(record["tx_notify_flush_semaphore_give_entered"])

        schema19_raw = _sdc_iso_rx_lifetime_sched_give_trace_raw()
        schema19 = receiver.parse_sdc_hci_remove_iso_path_trace(schema19_raw)
        self.assertEqual(schema19["parser_errors"], [])
        self.assertEqual(schema19["validation_errors"], [])
        self.assertEqual(schema19["schema_version"], 19)
        schema19_records = schema19["iso_rx_lifetime_disposition_snapshots"]
        self.assertEqual(
            [
                record["tx_notify_flush_semaphore_pend_thread_marked_pending"]
                for record in schema19_records
            ],
            [0, 0],
        )
        self.assertEqual(
            [
                record["tx_notify_flush_semaphore_give_entered"]
                for record in schema19_records
            ],
            [0, 1],
        )
        schema19_lines = (
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_semaphore_entered=0 tx_notify_flush_semaphore_pend_entered=0 "
            b"tx_notify_flush_semaphore_pend_thread_marked_pending=0 "
            b"tx_notify_flush_semaphore_give_entered=0 "
            b"tx_notify_flush_semaphore_pend_returned=0 "
            b"tx_notify_flush_semaphore_returned=1 tx_notify_flush_returned=0 "
            b"host_returned=0 app_callback_seen=0 unclassified=0",
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
            b"reason=unavailable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_semaphore_entered=0 tx_notify_flush_semaphore_pend_entered=0 "
            b"tx_notify_flush_semaphore_pend_thread_marked_pending=0 "
            b"tx_notify_flush_semaphore_give_entered=1 "
            b"tx_notify_flush_semaphore_pend_returned=0 "
            b"tx_notify_flush_semaphore_returned=0 tx_notify_flush_returned=0 "
            b"host_returned=0 app_callback_seen=0 unclassified=0",
        )
        for record, disposition_line in zip(schema19_records, schema19_lines):
            disposition_start = schema19_raw.index(disposition_line)
            self.assertEqual(record["start_offset"], disposition_start)
            self.assertEqual(
                record["end_offset"], disposition_start + len(disposition_line) + 2
            )
            self.assertEqual(record["line"], disposition_line.decode())
        schema15_raw = raw
        for field in (
            b"tx_notify_flush_entered",
            b"tx_notify_flush_returned",
        ):
            for value in (b"0", b"1"):
                schema15_raw = schema15_raw.replace(b" " + field + b"=" + value, b"")
        schema15 = receiver.parse_sdc_hci_remove_iso_path_trace(schema15_raw)
        self.assertEqual(schema15["parser_errors"], [])
        self.assertEqual(schema15["validation_errors"], [])
        self.assertTrue(
            all(
                record["host_returned"] == 0
                and record["tx_notify_flush_entered"] is None
                and record["tx_notify_flush_semaphore_entered"] is None
                and record["tx_notify_flush_semaphore_pend_entered"] is None
                and record["tx_notify_flush_semaphore_pend_thread_marked_pending"]
                is None
                and record["tx_notify_flush_semaphore_give_entered"] is None
                and record["tx_notify_flush_semaphore_pend_returned"] is None
                and record["tx_notify_flush_semaphore_returned"] is None
                and record["tx_notify_flush_returned"] is None
                for record in schema15["iso_rx_lifetime_disposition_snapshots"]
            )
        )
        schema14_raw = schema15_raw.replace(b" host_returned=0", b"")
        schema14 = receiver.parse_sdc_hci_remove_iso_path_trace(schema14_raw)
        self.assertEqual(schema14["parser_errors"], [])
        self.assertEqual(schema14["validation_errors"], [])
        self.assertTrue(
            all(
                record["host_returned"] is None
                and record["tx_notify_flush_entered"] is None
                and record["tx_notify_flush_semaphore_entered"] is None
                and record["tx_notify_flush_semaphore_pend_entered"] is None
                and record["tx_notify_flush_semaphore_pend_thread_marked_pending"]
                is None
                and record["tx_notify_flush_semaphore_give_entered"] is None
                and record["tx_notify_flush_semaphore_pend_returned"] is None
                and record["tx_notify_flush_semaphore_returned"] is None
                and record["tx_notify_flush_returned"] is None
                for record in schema14["iso_rx_lifetime_disposition_snapshots"]
            )
        )
        first_free = parsed["iso_rx_lifetime_first_free_after_unavailable"][0]
        self.assertEqual(first_free["classification"], "after_arm")
        self.assertEqual(first_free["outstanding_before"], 3)
        self.assertEqual(first_free["final_unrefs"], 1)
        first_free_start = raw.index(
            b"SDC LE Remove ISO Data Path ISO RX lifetime first free after unavailable"
        )
        self.assertEqual(first_free["start_offset"], first_free_start)
        self.assertEqual(
            first_free["end_offset"],
            first_free_start + len(raw[first_free_start:].split(b"\r\n", 1)[0]) + 2,
        )

    def test_sdc_iso_rx_lifetime_semaphore_parser_accepts_schema17_and_offsets(self):
        raw = _sdc_iso_rx_lifetime_semaphore_trace_raw()

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        disposition_records = parsed["iso_rx_lifetime_disposition_snapshots"]
        self.assertEqual(len(disposition_records), 2)
        self.assertEqual(
            [record["reason"] for record in disposition_records],
            ["disable", "unavailable"],
        )
        self.assertEqual(disposition_records[0]["tx_notify_flush_semaphore_entered"], 0)
        self.assertIsNone(
            disposition_records[0]["tx_notify_flush_semaphore_pend_entered"]
        )
        self.assertIsNone(
            disposition_records[0][
                "tx_notify_flush_semaphore_pend_thread_marked_pending"
            ]
        )
        self.assertIsNone(
            disposition_records[0]["tx_notify_flush_semaphore_give_entered"]
        )
        self.assertIsNone(
            disposition_records[0]["tx_notify_flush_semaphore_pend_returned"]
        )
        self.assertEqual(
            disposition_records[0]["tx_notify_flush_semaphore_returned"], 1
        )
        self.assertEqual(disposition_records[1]["tx_notify_flush_semaphore_entered"], 1)
        self.assertIsNone(
            disposition_records[1]["tx_notify_flush_semaphore_pend_entered"]
        )
        self.assertIsNone(
            disposition_records[1][
                "tx_notify_flush_semaphore_pend_thread_marked_pending"
            ]
        )
        self.assertIsNone(
            disposition_records[1]["tx_notify_flush_semaphore_give_entered"]
        )
        self.assertIsNone(
            disposition_records[1]["tx_notify_flush_semaphore_pend_returned"]
        )
        self.assertEqual(
            disposition_records[1]["tx_notify_flush_semaphore_returned"], 0
        )

        disposition_lines = (
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_semaphore_entered=0 tx_notify_flush_semaphore_returned=1 "
            b"tx_notify_flush_returned=0 host_returned=0 app_callback_seen=0 "
            b"unclassified=0",
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
            b"reason=unavailable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_semaphore_entered=1 tx_notify_flush_semaphore_returned=0 "
            b"tx_notify_flush_returned=0 host_returned=0 app_callback_seen=0 "
            b"unclassified=0",
        )
        for record, disposition_line in zip(disposition_records, disposition_lines):
            disposition_start = raw.index(disposition_line)
            self.assertEqual(record["start_offset"], disposition_start)
            self.assertEqual(
                record["end_offset"], disposition_start + len(disposition_line) + 2
            )
            self.assertEqual(record["line"], disposition_line.decode())

    def test_sdc_iso_rx_lifetime_pend_parser_accepts_schema18_and_offsets(self):
        raw = _sdc_iso_rx_lifetime_pend_trace_raw()

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        disposition_records = parsed["iso_rx_lifetime_disposition_snapshots"]
        self.assertEqual(len(disposition_records), 2)
        self.assertEqual(
            [record["reason"] for record in disposition_records],
            ["disable", "unavailable"],
        )
        self.assertEqual(
            disposition_records[0]["tx_notify_flush_semaphore_pend_entered"], 0
        )
        self.assertIsNone(
            disposition_records[0][
                "tx_notify_flush_semaphore_pend_thread_marked_pending"
            ]
        )
        self.assertIsNone(
            disposition_records[0]["tx_notify_flush_semaphore_give_entered"]
        )
        self.assertEqual(
            disposition_records[0]["tx_notify_flush_semaphore_pend_returned"], 0
        )
        self.assertEqual(
            disposition_records[1]["tx_notify_flush_semaphore_pend_entered"], 1
        )
        self.assertIsNone(
            disposition_records[1][
                "tx_notify_flush_semaphore_pend_thread_marked_pending"
            ]
        )
        self.assertIsNone(
            disposition_records[1]["tx_notify_flush_semaphore_give_entered"]
        )
        self.assertEqual(
            disposition_records[1]["tx_notify_flush_semaphore_pend_returned"], 0
        )

        disposition_lines = (
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_semaphore_entered=0 "
            b"tx_notify_flush_semaphore_pend_entered=0 "
            b"tx_notify_flush_semaphore_pend_returned=0 "
            b"tx_notify_flush_semaphore_returned=1 tx_notify_flush_returned=0 "
            b"host_returned=0 app_callback_seen=0 unclassified=0",
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
            b"reason=unavailable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_semaphore_entered=1 "
            b"tx_notify_flush_semaphore_pend_entered=1 "
            b"tx_notify_flush_semaphore_pend_returned=0 "
            b"tx_notify_flush_semaphore_returned=0 tx_notify_flush_returned=0 "
            b"host_returned=0 app_callback_seen=0 unclassified=0",
        )
        for record, disposition_line in zip(disposition_records, disposition_lines):
            disposition_start = raw.index(disposition_line)
            self.assertEqual(record["start_offset"], disposition_start)
            self.assertEqual(
                record["end_offset"], disposition_start + len(disposition_line) + 2
            )
            self.assertEqual(record["line"], disposition_line.decode())

    def test_sdc_iso_rx_lifetime_pend_parser_rejects_malformed_bounds_and_order(self):
        valid = _sdc_iso_rx_lifetime_pend_trace_raw()

        malformed_count = valid.replace(
            b"tx_notify_flush_semaphore_pend_entered=1",
            b"tx_notify_flush_semaphore_pend_entered=bad",
            1,
        )
        malformed_count_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            malformed_count
        )
        self.assertTrue(
            any(
                "malformed ISO RX lifetime disposition snapshot" in error
                for error in malformed_count_parsed["parser_errors"]
            )
        )

        reordered = valid.replace(
            b"tx_notify_flush_semaphore_pend_entered=1 "
            b"tx_notify_flush_semaphore_pend_returned=0",
            b"tx_notify_flush_semaphore_pend_returned=0 "
            b"tx_notify_flush_semaphore_pend_entered=1",
            1,
        )
        reordered_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(reordered)
        self.assertTrue(
            any(
                "malformed ISO RX lifetime disposition snapshot" in error
                for error in reordered_parsed["parser_errors"]
            )
        )

        over_capacity = valid.replace(
            b"tx_notify_flush_semaphore_pend_entered=1",
            b"tx_notify_flush_semaphore_pend_entered=4",
            1,
        )
        over_capacity_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            over_capacity
        )
        self.assertIn(
            "ISO RX lifetime disposition tx_notify_flush_semaphore_pend_entered exceeds arm capacity: 4",
            over_capacity_parsed["validation_errors"],
        )

        over_capacity_sum = valid.replace(
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_semaphore_entered=0 "
            b"tx_notify_flush_semaphore_pend_entered=0 "
            b"tx_notify_flush_semaphore_pend_returned=0 "
            b"tx_notify_flush_semaphore_returned=1 tx_notify_flush_returned=0 "
            b"host_returned=0 app_callback_seen=0 unclassified=0",
            b"reason=disable undispatched=1 host_dispatched=1 tx_notify_flush_entered=1 "
            b"tx_notify_flush_semaphore_entered=1 "
            b"tx_notify_flush_semaphore_pend_entered=1 "
            b"tx_notify_flush_semaphore_pend_returned=1 "
            b"tx_notify_flush_semaphore_returned=1 tx_notify_flush_returned=1 "
            b"host_returned=0 app_callback_seen=0 unclassified=0",
            1,
        )
        over_capacity_sum_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            over_capacity_sum
        )
        self.assertIn(
            "ISO RX lifetime disposition count sum exceeds arm capacity",
            over_capacity_sum_parsed["validation_errors"],
        )

        schema19_valid = _sdc_iso_rx_lifetime_sched_give_trace_raw()
        schema19_malformed_count = schema19_valid.replace(
            b"tx_notify_flush_semaphore_give_entered=1",
            b"tx_notify_flush_semaphore_give_entered=bad",
            1,
        )
        schema19_malformed_count_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            schema19_malformed_count
        )
        self.assertTrue(
            any(
                "malformed ISO RX lifetime disposition snapshot" in error
                for error in schema19_malformed_count_parsed["parser_errors"]
            )
        )

        schema19_reordered = schema19_valid.replace(
            b"tx_notify_flush_semaphore_pend_thread_marked_pending=0 "
            b"tx_notify_flush_semaphore_give_entered=1",
            b"tx_notify_flush_semaphore_give_entered=1 "
            b"tx_notify_flush_semaphore_pend_thread_marked_pending=0",
            1,
        )
        schema19_reordered_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            schema19_reordered
        )
        self.assertTrue(
            any(
                "malformed ISO RX lifetime disposition snapshot" in error
                for error in schema19_reordered_parsed["parser_errors"]
            )
        )

        schema19_over_capacity = schema19_valid.replace(
            b"tx_notify_flush_semaphore_pend_thread_marked_pending=0",
            b"tx_notify_flush_semaphore_pend_thread_marked_pending=4",
            1,
        )
        schema19_over_capacity_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            schema19_over_capacity
        )
        self.assertIn(
            "ISO RX lifetime disposition tx_notify_flush_semaphore_pend_thread_marked_pending exceeds arm capacity: 4",
            schema19_over_capacity_parsed["validation_errors"],
        )

        schema19_over_capacity_sum = schema19_valid.replace(
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_semaphore_entered=0 "
            b"tx_notify_flush_semaphore_pend_entered=0 "
            b"tx_notify_flush_semaphore_pend_thread_marked_pending=0 "
            b"tx_notify_flush_semaphore_give_entered=0 "
            b"tx_notify_flush_semaphore_pend_returned=0 "
            b"tx_notify_flush_semaphore_returned=1 tx_notify_flush_returned=0 "
            b"host_returned=0 app_callback_seen=0 unclassified=0",
            b"reason=disable undispatched=1 host_dispatched=1 tx_notify_flush_entered=1 "
            b"tx_notify_flush_semaphore_entered=1 "
            b"tx_notify_flush_semaphore_pend_entered=1 "
            b"tx_notify_flush_semaphore_pend_thread_marked_pending=1 "
            b"tx_notify_flush_semaphore_give_entered=1 "
            b"tx_notify_flush_semaphore_pend_returned=1 "
            b"tx_notify_flush_semaphore_returned=1 tx_notify_flush_returned=1 "
            b"host_returned=0 app_callback_seen=0 unclassified=0",
            1,
        )
        schema19_over_capacity_sum_parsed = (
            receiver.parse_sdc_hci_remove_iso_path_trace(schema19_over_capacity_sum)
        )
        self.assertIn(
            "ISO RX lifetime disposition count sum exceeds arm capacity",
            schema19_over_capacity_sum_parsed["validation_errors"],
        )

    def test_sdc_iso_rx_lifetime_semaphore_parser_rejects_malformed_bounds_and_order(
        self,
    ):
        valid = _sdc_iso_rx_lifetime_semaphore_trace_raw()
        disable_line = (
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_semaphore_entered=0 tx_notify_flush_semaphore_returned=1 "
            b"tx_notify_flush_returned=0 host_returned=0 app_callback_seen=0 "
            b"unclassified=0"
        )
        disable_snapshot = (
            b"SDC LE Remove ISO Data Path ISO RX lifetime snapshot: reason=disable "
            b"capacity=3 outstanding=0 high_water=0 allocations=0 final_unrefs=0 "
            b"callbacks_active=0 callbacks_total=0"
        )

        malformed_count = valid.replace(
            b"tx_notify_flush_semaphore_entered=0",
            b"tx_notify_flush_semaphore_entered=bad",
            1,
        )
        malformed_count_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            malformed_count
        )
        self.assertTrue(
            any(
                "malformed ISO RX lifetime disposition snapshot" in error
                for error in malformed_count_parsed["parser_errors"]
            )
        )

        duplicate_prefix = valid.replace(
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=",
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason="
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=",
            1,
        )
        duplicate_prefix_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            duplicate_prefix
        )
        self.assertTrue(
            any(
                "malformed ISO RX lifetime disposition snapshot" in error
                for error in duplicate_prefix_parsed["parser_errors"]
            )
        )

        over_capacity = valid.replace(
            b"tx_notify_flush_semaphore_entered=0",
            b"tx_notify_flush_semaphore_entered=4",
            1,
        )
        over_capacity_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            over_capacity
        )
        self.assertIn(
            "ISO RX lifetime disposition tx_notify_flush_semaphore_entered exceeds arm capacity: 4",
            over_capacity_parsed["validation_errors"],
        )

        over_capacity_sum = valid.replace(
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_semaphore_entered=0 tx_notify_flush_semaphore_returned=1 "
            b"tx_notify_flush_returned=0 host_returned=0 app_callback_seen=0 unclassified=0",
            b"reason=disable undispatched=1 host_dispatched=1 tx_notify_flush_entered=1 "
            b"tx_notify_flush_semaphore_entered=1 tx_notify_flush_semaphore_returned=1 "
            b"tx_notify_flush_returned=1 host_returned=0 app_callback_seen=0 unclassified=0",
            1,
        )
        over_capacity_sum_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            over_capacity_sum
        )
        self.assertIn(
            "ISO RX lifetime disposition count sum exceeds arm capacity",
            over_capacity_sum_parsed["validation_errors"],
        )

        field_order = valid.replace(
            b"tx_notify_flush_semaphore_entered=1 tx_notify_flush_semaphore_returned=0",
            b"tx_notify_flush_semaphore_returned=0 tx_notify_flush_semaphore_entered=1",
            1,
        )
        field_order_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(field_order)
        self.assertTrue(
            any(
                "malformed ISO RX lifetime disposition snapshot" in error
                for error in field_order_parsed["parser_errors"]
            )
        )

        parent_order = valid.replace(
            disable_snapshot + b"\r\n" + disable_line,
            disable_line + b"\r\n" + disable_snapshot,
            1,
        )
        parent_order_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(parent_order)
        self.assertIn(
            "ISO RX lifetime disposition snapshot reason=disable must follow matching "
            "lifetime snapshot",
            parent_order_parsed["validation_errors"],
        )

    def test_sdc_iso_rx_lifetime_parser_rejects_malformed_and_duplicate_markers(self):
        valid = _sdc_iso_rx_lifetime_trace_raw()
        malformed = valid.replace(b"outstanding=3", b"outstanding=bad", 1)
        malformed_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(malformed)
        self.assertTrue(
            any(
                "malformed ISO RX lifetime snapshot marker" in error
                for error in malformed_parsed["parser_errors"]
            )
        )

        lifetime_arm = (
            b"SDC LE Remove ISO Data Path ISO RX lifetime trace armed: capacity=3\r\n"
        )
        duplicate_arm = valid.replace(lifetime_arm, lifetime_arm + lifetime_arm, 1)
        duplicate_arm_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            duplicate_arm
        )
        self.assertIn(
            "duplicate ISO RX lifetime arm marker",
            duplicate_arm_parsed["validation_errors"],
        )

        first_free = (
            b"SDC LE Remove ISO Data Path ISO RX lifetime first free after unavailable: "
            b"outstanding_before=3 callbacks_active=0 allocations=3 final_unrefs=1\r\n"
        )
        duplicate_first_free = valid.replace(first_free, first_free + first_free, 1)
        duplicate_first_free_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            duplicate_first_free
        )
        self.assertIn(
            "duplicate ISO RX lifetime first-free marker",
            duplicate_first_free_parsed["validation_errors"],
        )

        disposition = (
            b"SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: "
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_returned=0 host_returned=0 "
            b"app_callback_seen=0 "
            b"unclassified=0\r\n"
        )
        malformed_disposition = valid.replace(
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_returned=0 host_returned=0",
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_returned=0 host_returned=bad",
            1,
        )
        malformed_disposition_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            malformed_disposition
        )
        self.assertTrue(
            any(
                "malformed ISO RX lifetime disposition snapshot marker" in error
                for error in malformed_disposition_parsed["parser_errors"]
            )
        )

        malformed_new_disposition = valid.replace(
            b"tx_notify_flush_entered=0",
            b"tx_notify_flush_entered=bad",
            1,
        )
        malformed_new_disposition_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            malformed_new_disposition
        )
        self.assertTrue(
            any(
                "malformed ISO RX lifetime disposition snapshot" in error
                for error in malformed_new_disposition_parsed["parser_errors"]
            )
        )

        over_capacity_new_field = valid.replace(
            b"tx_notify_flush_entered=0",
            b"tx_notify_flush_entered=4",
            1,
        )
        over_capacity_new_field_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            over_capacity_new_field
        )
        self.assertTrue(
            any(
                "ISO RX lifetime disposition tx_notify_flush_entered exceeds arm capacity"
                in error
                for error in over_capacity_new_field_parsed["validation_errors"]
            )
        )

        duplicate_disposition = valid.replace(disposition, disposition + disposition, 1)
        duplicate_disposition_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            duplicate_disposition
        )
        self.assertIn(
            "duplicate ISO RX lifetime disposition snapshot reason=disable",
            duplicate_disposition_parsed["validation_errors"],
        )

        missing_parent = valid.replace(
            b"SDC LE Remove ISO Data Path ISO RX lifetime snapshot: reason=disable "
            b"capacity=3 outstanding=0 high_water=0 allocations=0 final_unrefs=0 "
            b"callbacks_active=0 callbacks_total=0\r\n",
            b"",
            1,
        )
        missing_parent_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            missing_parent
        )
        self.assertTrue(
            any(
                "without matching lifetime snapshot" in error
                for error in missing_parent_parsed["validation_errors"]
            )
        )

        over_capacity = valid.replace(
            b"reason=disable undispatched=0",
            b"reason=disable undispatched=4",
            1,
        )
        over_capacity_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            over_capacity
        )
        self.assertTrue(
            any(
                "ISO RX lifetime disposition undispatched exceeds arm capacity" in error
                for error in over_capacity_parsed["validation_errors"]
            )
        )

        over_capacity_sum = valid.replace(
            b"reason=disable undispatched=0 host_dispatched=0 tx_notify_flush_entered=0 "
            b"tx_notify_flush_returned=0 host_returned=0 "
            b"app_callback_seen=0 unclassified=0",
            b"reason=disable undispatched=1 host_dispatched=1 tx_notify_flush_entered=1 "
            b"tx_notify_flush_returned=1 host_returned=0 "
            b"app_callback_seen=1 unclassified=0",
            1,
        )
        over_capacity_sum_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            over_capacity_sum
        )
        self.assertIn(
            "ISO RX lifetime disposition count sum exceeds arm capacity",
            over_capacity_sum_parsed["validation_errors"],
        )

        schema15_valid = valid
        for field in (
            b"tx_notify_flush_entered",
            b"tx_notify_flush_returned",
        ):
            for value in (b"0", b"1"):
                schema15_valid = schema15_valid.replace(
                    b" " + field + b"=" + value, b""
                )
        schema15_valid_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            schema15_valid
        )
        self.assertEqual(schema15_valid_parsed["parser_errors"], [])
        self.assertEqual(schema15_valid_parsed["validation_errors"], [])
        schema14_valid = schema15_valid.replace(b" host_returned=0", b"")
        schema14_valid_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            schema14_valid
        )
        self.assertEqual(schema14_valid_parsed["parser_errors"], [])
        self.assertEqual(schema14_valid_parsed["validation_errors"], [])

    def test_sdc_iso_rx_lifetime_parser_preserves_legacy_no_lifetime_compatibility(
        self,
    ):
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(_sdc_trace_raw())

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        self.assertEqual(parsed["iso_rx_lifetime_arm_markers"], [])
        self.assertEqual(parsed["iso_rx_lifetime_snapshots"], [])
        self.assertEqual(parsed["iso_rx_lifetime_disposition_snapshots"], [])
        self.assertEqual(parsed["iso_rx_lifetime_first_free_after_unavailable"], [])

    def test_sdc_trace_parser_happy_path_retains_offsets_and_prearm_drop(self):
        drop_line = b"uart:~$ --- 276 messages dropped ---"
        arm = b"<inf> bt_bap: SDC LE Remove ISO Data Path trace armed"
        cmd_put_entry = (
            b"<inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry"
        )
        entry = b"<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path trace entry"
        ret = (
            b"<inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path trace return: status=0x00"
        )
        cmd_put_return = (
            b"<inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0"
        )
        lock_release_entry = (
            b"<inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path multithreading_lock_release entry"
        )
        lock_release_return = (
            b"<inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path multithreading_lock_release return"
        )
        work_submit_entry = (
            b"<inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path k_work_submit_to_queue entry"
        )
        work_submit_return = (
            b"<inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=1"
        )
        msg_get_entry = (
            b"<inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path hci_internal_msg_get entry"
        )
        msg_get_return = (
            b"<inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=0"
        )
        completion = (
            b"<inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path completion: status=0x00"
        )
        raw = (
            b"\r\n".join(
                (
                    drop_line,
                    arm,
                    cmd_put_entry,
                    entry,
                    ret,
                    cmd_put_return,
                    lock_release_entry,
                    lock_release_return,
                    work_submit_entry,
                    work_submit_return,
                    msg_get_entry,
                    msg_get_return,
                    completion,
                )
            )
            + b"\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        self.assertIsNone(parsed["receive_disposition_outcome"])
        self.assertEqual(parsed["dropped_messages"][0]["classification"], "before_arm")
        self.assertEqual(parsed["dropped_messages"][0]["count"], 276)
        self.assertEqual(parsed["arm_markers"][0]["classification"], "before_arm")
        self.assertEqual(parsed["cmd_put_entries"][0]["classification"], "after_arm")
        self.assertEqual(parsed["cmd_put_entries"][0]["line"], cmd_put_entry.decode())
        self.assertEqual(parsed["cmd_put_returns"][0]["classification"], "after_arm")
        self.assertEqual(parsed["cmd_put_returns"][0]["status"], 0)
        self.assertEqual(parsed["sdc_entries"][0]["classification"], "after_arm")
        self.assertEqual(parsed["sdc_returns"][0]["classification"], "after_arm")
        self.assertEqual(parsed["sdc_returns"][0]["status"], 0)
        self.assertEqual(
            parsed["lock_release_entries"][0]["classification"], "after_arm"
        )
        self.assertEqual(
            parsed["lock_release_returns"][0]["classification"], "after_arm"
        )
        self.assertEqual(
            parsed["work_submit_entries"][0]["classification"], "after_arm"
        )
        self.assertEqual(
            parsed["work_submit_returns"][0]["classification"], "after_arm"
        )
        self.assertEqual(parsed["work_submit_returns"][0]["status"], 1)
        self.assertEqual(parsed["msg_get_entries"][0]["classification"], "after_arm")
        self.assertEqual(parsed["msg_get_returns"][0]["classification"], "after_arm")
        self.assertEqual(parsed["msg_get_returns"][0]["status"], 0)
        self.assertEqual(parsed["sdc_completions"][0]["classification"], "after_arm")
        self.assertEqual(parsed["sdc_completions"][0]["status"], 0)
        cmd_put_entry_start = raw.index(cmd_put_entry)
        self.assertEqual(
            parsed["cmd_put_entries"][0]["start_offset"], cmd_put_entry_start
        )
        self.assertEqual(
            parsed["cmd_put_entries"][0]["end_offset"],
            cmd_put_entry_start + len(cmd_put_entry) + 2,
        )
        entry_start = raw.index(entry)
        self.assertEqual(parsed["sdc_entries"][0]["start_offset"], entry_start)
        self.assertEqual(
            parsed["sdc_entries"][0]["end_offset"], entry_start + len(entry) + 2
        )
        self.assertEqual(parsed["sdc_entries"][0]["line"], entry.decode())
        work_submit_entry_start = raw.index(work_submit_entry)
        self.assertEqual(
            parsed["work_submit_entries"][0]["start_offset"], work_submit_entry_start
        )
        self.assertEqual(
            parsed["work_submit_entries"][0]["end_offset"],
            work_submit_entry_start + len(work_submit_entry) + 2,
        )
        self.assertEqual(
            parsed["work_submit_entries"][0]["line"], work_submit_entry.decode()
        )
        msg_get_entry_start = raw.index(msg_get_entry)
        self.assertEqual(
            parsed["msg_get_entries"][0]["start_offset"], msg_get_entry_start
        )
        self.assertEqual(
            parsed["msg_get_entries"][0]["end_offset"],
            msg_get_entry_start + len(msg_get_entry) + 2,
        )
        self.assertEqual(parsed["msg_get_entries"][0]["line"], msg_get_entry.decode())

    def test_sdc_trace_parser_accepts_one_prompt_prefixed_logger_envelope(self):
        arm = (
            b"uart:~$ [00:31:40.598,486] <inf> bt_bap: "
            b"SDC LE Remove ISO Data Path trace armed"
        )
        cmd_put_entry = (
            b"uart:~$ [00:31:40.598,694] <inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry"
        )
        entry = (
            b"uart:~$ [00:31:40.598,704] <inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path trace entry"
        )
        ret = (
            b"uart:~$ [00:31:40.598,714] <inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path trace return: status=0x00"
        )
        cmd_put_return = (
            b"uart:~$ [00:31:40.598,719] <inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0"
        )
        lock_release_entry = (
            b"uart:~$ [00:31:40.598,720] <inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path multithreading_lock_release entry"
        )
        lock_release_return = (
            b"uart:~$ [00:31:40.598,720] <inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path multithreading_lock_release return"
        )
        work_submit_entry = (
            b"uart:~$ [00:31:40.598,720] <inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path k_work_submit_to_queue entry"
        )
        work_submit_return = (
            b"uart:~$ [00:31:40.598,720] <inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=1"
        )
        msg_get_entry = (
            b"uart:~$ [00:31:40.598,721] <inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path hci_internal_msg_get entry"
        )
        msg_get_return = (
            b"uart:~$ [00:31:40.598,722] <inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=0"
        )
        completion = (
            b"uart:~$ [00:31:40.598,724] <inf> sdc_hci_remove_iso_path_trace: "
            b"SDC LE Remove ISO Data Path completion: status=0x00"
        )
        raw = (
            b"\r\n".join(
                (
                    arm,
                    cmd_put_entry,
                    entry,
                    ret,
                    cmd_put_return,
                    lock_release_entry,
                    lock_release_return,
                    work_submit_entry,
                    work_submit_return,
                    msg_get_entry,
                    msg_get_return,
                    completion,
                )
            )
            + b"\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["arm_markers"][0]["classification"], "before_arm")
        self.assertEqual(parsed["cmd_put_entries"][0]["classification"], "after_arm")
        self.assertEqual(parsed["cmd_put_returns"][0]["classification"], "after_arm")
        self.assertEqual(parsed["sdc_entries"][0]["classification"], "after_arm")
        self.assertEqual(parsed["sdc_returns"][0]["classification"], "after_arm")
        self.assertEqual(
            parsed["lock_release_entries"][0]["classification"], "after_arm"
        )
        self.assertEqual(
            parsed["lock_release_returns"][0]["classification"], "after_arm"
        )
        self.assertEqual(
            parsed["work_submit_entries"][0]["classification"], "after_arm"
        )
        self.assertEqual(
            parsed["work_submit_returns"][0]["classification"], "after_arm"
        )
        self.assertEqual(parsed["sdc_completions"][0]["classification"], "after_arm")

        for records, line in (
            (parsed["arm_markers"], arm),
            (parsed["cmd_put_entries"], cmd_put_entry),
            (parsed["sdc_entries"], entry),
            (parsed["sdc_returns"], ret),
            (parsed["cmd_put_returns"], cmd_put_return),
            (parsed["lock_release_entries"], lock_release_entry),
            (parsed["lock_release_returns"], lock_release_return),
            (parsed["work_submit_entries"], work_submit_entry),
            (parsed["work_submit_returns"], work_submit_return),
            (parsed["msg_get_entries"], msg_get_entry),
            (parsed["msg_get_returns"], msg_get_return),
            (parsed["sdc_completions"], completion),
        ):
            record = records[0]
            start = raw.index(line)
            self.assertEqual(record["start_offset"], start)
            self.assertEqual(record["end_offset"], start + len(line) + 2)
            self.assertEqual(record["line"], line.decode())
            self.assertTrue(record["line"].startswith(receiver.RECEIVER_PROMPT))

        self.assertEqual(parsed["cmd_put_returns"][0]["status"], 0)
        self.assertEqual(parsed["work_submit_returns"][0]["status"], 1)
        self.assertEqual(parsed["msg_get_returns"][0]["status"], 0)

    def test_sdc_trace_parser_allows_msg_get_before_work_submit_return(self):
        raw = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0\r\n"
            b"SDC LE Remove ISO Data Path multithreading_lock_release entry\r\n"
            b"SDC LE Remove ISO Data Path multithreading_lock_release return\r\n"
            b"SDC LE Remove ISO Data Path k_work_submit_to_queue entry\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_msg_get entry\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=0\r\n"
            b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
            b"SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=2\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["work_submit_returns"][0]["status"], 2)

    def test_sdc_trace_parser_validates_scheduler_unlock_sequence(self):
        raw = _sdc_scheduler_trace_raw()

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        self.assertEqual(len(parsed["scheduler_unlock_arm_markers"]), 1)
        self.assertNotIn("queue_is_mpsl", parsed["scheduler_unlock_arm_markers"][0])
        self.assertEqual(len(parsed["scheduler_unlock_entries"]), 1)
        self.assertEqual(len(parsed["scheduler_unlock_returns"]), 1)
        self.assertEqual(parsed["snapshot_schedule_markers"], [])
        self.assertEqual(parsed["snapshot_markers"], [])
        self.assertLess(
            parsed["scheduler_unlock_arm_markers"][0]["start_offset"],
            parsed["scheduler_unlock_entries"][0]["start_offset"],
        )
        self.assertLess(
            parsed["scheduler_unlock_entries"][0]["start_offset"],
            parsed["scheduler_unlock_returns"][0]["start_offset"],
        )

    def test_sdc_trace_parser_allows_msg_get_between_scheduler_unlock_markers(self):
        raw = _sdc_scheduler_trace_raw(msg_get_between_unlock=True)

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertLess(
            parsed["scheduler_unlock_entries"][0]["start_offset"],
            parsed["msg_get_entries"][0]["start_offset"],
        )
        self.assertLess(
            parsed["msg_get_returns"][0]["start_offset"],
            parsed["scheduler_unlock_returns"][0]["start_offset"],
        )

    def test_sdc_trace_parser_validates_extended_scheduler_unlock_state(self):
        state = (
            b"SDC LE Remove ISO Data Path k_sched_unlock state: "
            b"busy=0x4 mpsl_state=PENDING current_is_sender=1 "
            b"mpsl_is_current=0 sender_prio=-10 current_prio=-10 mpsl_prio=-6"
        )
        raw = _sdc_scheduler_trace_raw(extended=True, state_line=state)

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        self.assertEqual(parsed["scheduler_unlock_arm_markers"][0]["queue_is_mpsl"], 1)
        self.assertEqual(len(parsed["scheduler_unlock_state_markers"]), 1)
        state_record = parsed["scheduler_unlock_state_markers"][0]
        self.assertEqual(state_record["classification"], "after_arm")
        self.assertEqual(state_record["busy"], 0x4)
        self.assertEqual(state_record["mpsl_state"], "PENDING")
        self.assertTrue(state_record["current_is_sender"])
        self.assertFalse(state_record["mpsl_is_current"])
        self.assertEqual(state_record["sender_prio"], -10)
        self.assertEqual(state_record["current_prio"], -10)
        self.assertEqual(state_record["mpsl_prio"], -6)
        state_start = raw.index(state)
        self.assertEqual(state_record["start_offset"], state_start)
        self.assertEqual(state_record["end_offset"], state_start + len(state) + 2)
        self.assertLess(
            parsed["scheduler_unlock_entries"][0]["start_offset"],
            state_record["start_offset"],
        )
        self.assertLess(
            state_record["start_offset"],
            parsed["scheduler_unlock_returns"][0]["start_offset"],
        )

    def test_sdc_trace_parser_validates_v9_scheduler_unlock_state(self):
        state = (
            b"SDC LE Remove ISO Data Path k_sched_unlock state: "
            b"busy=0x4 mpsl_state=PENDING resumed_current_is_sender=1 "
            b"mpsl_switches_during_unlock=1 sender_prio=-10 "
            b"resumed_current_prio=-10 mpsl_prio=-6"
        )
        raw = _sdc_scheduler_trace_raw(extended=True, state_line=state)

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        state_record = parsed["scheduler_unlock_state_markers"][0]
        self.assertEqual(state_record["busy"], 0x4)
        self.assertEqual(state_record["mpsl_state"], "PENDING")
        self.assertTrue(state_record["resumed_current_is_sender"])
        self.assertEqual(state_record["mpsl_switches_during_unlock"], 1)
        self.assertEqual(state_record["sender_prio"], -10)
        self.assertEqual(state_record["resumed_current_prio"], -10)
        self.assertEqual(state_record["mpsl_prio"], -6)
        for field in ("current_is_sender", "mpsl_is_current", "current_prio"):
            self.assertNotIn(field, state_record)
        state_start = raw.index(state)
        self.assertEqual(state_record["start_offset"], state_start)
        self.assertEqual(state_record["end_offset"], state_start + len(state) + 2)

    def test_sdc_trace_parser_validates_v10_post_unlock_yield_state_and_offsets(self):
        unlock_state = (
            b"SDC LE Remove ISO Data Path k_sched_unlock state: "
            b"busy=0x4 mpsl_state=queued resumed_current_is_sender=1 "
            b"mpsl_switches_during_unlock=0 sender_prio=-1 "
            b"resumed_current_prio=-1 mpsl_prio=-10"
        )
        yield_state = (
            b"SDC LE Remove ISO Data Path post-unlock yield state: "
            b"busy=0x4 mpsl_state=queued resumed_current_is_sender=1 "
            b"mpsl_switches_during_yield=1"
        )
        raw = _sdc_scheduler_trace_raw(
            extended=True,
            state_line=unlock_state,
            post_unlock_yield=True,
            yield_state_line=yield_state,
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        self.assertEqual(len(parsed["scheduler_yield_arm_markers"]), 1)
        self.assertEqual(len(parsed["scheduler_yield_entries"]), 1)
        self.assertEqual(len(parsed["scheduler_yield_state_markers"]), 1)
        self.assertEqual(len(parsed["scheduler_yield_returns"]), 1)
        state_record = parsed["scheduler_yield_state_markers"][0]
        self.assertEqual(state_record["classification"], "after_arm")
        self.assertEqual(state_record["busy"], 0x4)
        self.assertEqual(state_record["mpsl_state"], "queued")
        self.assertTrue(state_record["resumed_current_is_sender"])
        self.assertEqual(state_record["mpsl_switches_during_yield"], 1)
        state_start = raw.index(yield_state)
        self.assertEqual(state_record["start_offset"], state_start)
        self.assertEqual(state_record["end_offset"], state_start + len(yield_state) + 2)
        self.assertLess(
            parsed["scheduler_unlock_returns"][0]["start_offset"],
            parsed["scheduler_yield_arm_markers"][0]["start_offset"],
        )
        self.assertLess(
            parsed["scheduler_yield_arm_markers"][0]["start_offset"],
            parsed["scheduler_yield_entries"][0]["start_offset"],
        )
        self.assertLess(
            parsed["scheduler_yield_entries"][0]["start_offset"],
            state_record["start_offset"],
        )
        self.assertLess(
            state_record["start_offset"],
            parsed["scheduler_yield_returns"][0]["start_offset"],
        )

    def test_sdc_trace_parser_rejects_malformed_v10_post_unlock_yield_markers(self):
        yield_state = (
            b"SDC LE Remove ISO Data Path post-unlock yield state: "
            b"busy=0x4 mpsl_state=PENDING resumed_current_is_sender=1 "
            b"mpsl_switches_during_yield=1"
        )
        valid = _sdc_scheduler_trace_raw(
            extended=True, post_unlock_yield=True, yield_state_line=yield_state
        )
        malformed_forms = (
            (
                b"SDC LE Remove ISO Data Path post-unlock yield trace armed",
                b"SDC LE Remove ISO Data Path post-unlock yield trace armed: bad",
                "malformed scheduler yield arm marker",
            ),
            (
                b"SDC LE Remove ISO Data Path post-unlock yield entry",
                b"SDC LE Remove ISO Data Path post-unlock yield entry: bad",
                "malformed scheduler yield entry marker",
            ),
            (
                b"busy=0x4",
                b"busy=4",
                "malformed scheduler yield state marker",
            ),
            (
                b"mpsl_switches_during_yield=1",
                b"mpsl_switches_during_yield=-1",
                "malformed scheduler yield state marker",
            ),
            (
                b"SDC LE Remove ISO Data Path post-unlock yield return",
                b"SDC LE Remove ISO Data Path post-unlock yield return: bad",
                "malformed scheduler yield return marker",
            ),
        )

        for marker, malformed, expected in malformed_forms:
            parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
                valid.replace(marker, malformed, 1)
            )
            self.assertTrue(
                any(expected in error for error in parsed["parser_errors"]), malformed
            )

    def test_sdc_trace_parser_requires_v10_post_unlock_yield_records(self):
        valid = _sdc_scheduler_trace_raw(extended=True, post_unlock_yield=True)
        records = (
            (
                b"SDC LE Remove ISO Data Path post-unlock yield entry\r\n",
                "missing post-arm scheduler yield entry marker",
            ),
            (
                b"SDC LE Remove ISO Data Path post-unlock yield state: "
                b"busy=0x4 mpsl_state=PENDING resumed_current_is_sender=1 "
                b"mpsl_switches_during_yield=1\r\n",
                "missing post-arm scheduler yield state marker",
            ),
            (
                b"SDC LE Remove ISO Data Path post-unlock yield return\r\n",
                "missing post-arm scheduler yield return marker",
            ),
        )

        for marker, expected in records:
            parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
                valid.replace(marker, b"", 1)
            )
            self.assertIn(expected, parsed["validation_errors"])

    def test_sdc_trace_parser_accepts_rh3_28_v9_raw_without_yield_requirements(self):
        state = (
            b"SDC LE Remove ISO Data Path k_sched_unlock state: "
            b"busy=0x4 mpsl_state=queued resumed_current_is_sender=1 "
            b"mpsl_switches_during_unlock=0 sender_prio=-1 "
            b"resumed_current_prio=-1 mpsl_prio=-10"
        )
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_scheduler_trace_raw(extended=True, state_line=state)
        )

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        self.assertEqual(parsed["scheduler_yield_arm_markers"], [])
        self.assertEqual(parsed["scheduler_yield_entries"], [])
        self.assertEqual(parsed["scheduler_yield_state_markers"], [])
        self.assertEqual(parsed["scheduler_yield_returns"], [])

    def test_sdc_trace_parser_rejects_malformed_v9_scheduler_unlock_fields(self):
        state = (
            b"SDC LE Remove ISO Data Path k_sched_unlock state: "
            b"busy=0x4 mpsl_state=PENDING resumed_current_is_sender=1 "
            b"mpsl_switches_during_unlock=1 sender_prio=-10 "
            b"resumed_current_prio=-10 mpsl_prio=-6"
        )
        valid = _sdc_scheduler_trace_raw(extended=True, state_line=state)
        malformed_forms = (
            (b"busy=0x4", b"busy=4"),
            (b"mpsl_state=PENDING", b"mpsl_state=RUN-NING"),
            (b"resumed_current_is_sender=1", b"resumed_current_is_sender=2"),
            (b"mpsl_switches_during_unlock=1", b"mpsl_switches_during_unlock=-1"),
            (b"sender_prio=-10", b"sender_prio=bad"),
            (b"resumed_current_prio=-10", b"resumed_current_prio=-"),
            (b"mpsl_prio=-6", b"mpsl_prio=-6 suffix"),
        )

        for marker, malformed in malformed_forms:
            parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
                valid.replace(marker, malformed, 1)
            )
            self.assertTrue(
                any(
                    "malformed scheduler unlock state marker" in error
                    for error in parsed["parser_errors"]
                ),
                malformed,
            )

    def test_sdc_trace_parser_accepts_legacy_v7_scheduler_unlock_state(self):
        state = (
            b"SDC LE Remove ISO Data Path k_sched_unlock state: "
            b"busy=0x4 mpsl_state=PENDING"
        )
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_scheduler_trace_raw(extended=True, state_line=state)
        )

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        state_record = parsed["scheduler_unlock_state_markers"][0]
        self.assertEqual(state_record["busy"], 0x4)
        self.assertEqual(state_record["mpsl_state"], "PENDING")
        for field in (
            "current_is_sender",
            "mpsl_is_current",
            "sender_prio",
            "current_prio",
            "mpsl_prio",
        ):
            self.assertNotIn(field, state_record)

    def test_sdc_trace_parser_accepts_current_rh3_27_legacy_state(self):
        raw = (
            b"\r\n".join(
                (
                    b"uart:~$ [00:42:29.033,576] <inf> bt_bap: "
                    b"SDC LE Remove ISO Data Path trace armed",
                    b"uart:~$ [00:42:29.033,800] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry",
                    b"uart:~$ [00:42:29.033,807] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path trace entry",
                    b"uart:~$ [00:42:29.033,817] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path trace return: status=0x00",
                    b"uart:~$ [00:42:29.033,823] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0",
                    b"uart:~$ [00:42:29.033,828] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path multithreading_lock_release entry",
                    b"uart:~$ [00:42:29.033,835] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path multithreading_lock_release return",
                    b"uart:~$ [00:42:29.033,840] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path k_work_submit_to_queue entry",
                    b"uart:~$ [00:42:29.033,859] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=1",
                    b"uart:~$ [00:42:29.033,864] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path scheduler unlock trace armed: queue_is_mpsl=1",
                    b"uart:~$ [00:42:29.033,873] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path k_sched_unlock entry",
                    b"uart:~$ [00:42:29.033,910] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path k_sched_unlock state: busy=0x4 mpsl_state=queued",
                    b"uart:~$ [00:42:29.033,915] <inf> sdc_hci_remove_iso_path_trace: "
                    b"SDC LE Remove ISO Data Path k_sched_unlock return",
                )
            )
            + b"\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["schema_version"], 19)
        state_record = parsed["scheduler_unlock_state_markers"][0]
        self.assertEqual(state_record["busy"], 0x4)
        self.assertEqual(state_record["mpsl_state"], "queued")
        self.assertNotIn("resumed_current_is_sender", state_record)

    def test_sdc_trace_parser_accepts_unavailable_mpsl_priority(self):
        state = (
            b"SDC LE Remove ISO Data Path k_sched_unlock state: "
            b"busy=0x4 mpsl_state=unavailable current_is_sender=1 "
            b"mpsl_is_current=0 sender_prio=7 current_prio=-3 mpsl_prio=unavailable"
        )
        raw = _sdc_scheduler_trace_raw(extended=True, state_line=state)
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        state_record = parsed["scheduler_unlock_state_markers"][0]
        self.assertTrue(state_record["current_is_sender"])
        self.assertFalse(state_record["mpsl_is_current"])
        self.assertEqual(state_record["sender_prio"], 7)
        self.assertEqual(state_record["current_prio"], -3)
        self.assertIsNone(state_record["mpsl_prio"])
        state_start = raw.index(state)
        self.assertEqual(state_record["start_offset"], state_start)
        self.assertEqual(state_record["end_offset"], state_start + len(state) + 2)

    def test_sdc_trace_parser_rejects_extended_scheduler_unlock_forms(self):
        state = (
            b"SDC LE Remove ISO Data Path k_sched_unlock state: "
            b"busy=0x4 mpsl_state=PENDING current_is_sender=1 "
            b"mpsl_is_current=0 sender_prio=-10 current_prio=-10 mpsl_prio=-6"
        )
        valid = _sdc_scheduler_trace_raw(extended=True, state_line=state)
        malformed_forms = (
            (
                b"queue_is_mpsl=1",
                b"queue_is_mpsl=2",
                "malformed scheduler unlock arm marker",
            ),
            (
                b"busy=0x4",
                b"busy=4",
                "malformed scheduler unlock state marker",
            ),
            (
                b"mpsl_state=PENDING",
                b"mpsl_state=RUN-NING",
                "malformed scheduler unlock state marker",
            ),
            (
                b"current_is_sender=1",
                b"current_is_sender=2",
                "malformed scheduler unlock state marker",
            ),
            (
                b"mpsl_is_current=0",
                b"mpsl_is_current=true",
                "malformed scheduler unlock state marker",
            ),
            (
                b"sender_prio=-10",
                b"sender_prio=bad",
                "malformed scheduler unlock state marker",
            ),
            (
                b"current_prio=-10",
                b"current_prio=-",
                "malformed scheduler unlock state marker",
            ),
            (
                b"mpsl_prio=-6",
                b"mpsl_prio=-6 suffix",
                "malformed scheduler unlock state marker",
            ),
        )

        for marker, malformed, expected in malformed_forms:
            parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
                valid.replace(marker, malformed, 1)
            )
            self.assertTrue(any(expected in error for error in parsed["parser_errors"]))

    def test_sdc_trace_parser_rejects_scheduler_unlock_state_contract_errors(self):
        state = (
            b"SDC LE Remove ISO Data Path k_sched_unlock state: "
            b"busy=0x4 mpsl_state=PENDING"
        )
        valid = _sdc_scheduler_trace_raw(extended=True, state_line=state)

        queue_false = valid.replace(b"queue_is_mpsl=1", b"queue_is_mpsl=0", 1)
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(queue_false)
        self.assertEqual(parsed["parser_errors"], [])
        self.assertTrue(
            any(
                "scheduler unlock arm queue_is_mpsl must be 1" in error
                for error in parsed["validation_errors"]
            )
        )
        self.assertEqual(parsed["scheduler_unlock_arm_markers"][0]["queue_is_mpsl"], 0)

        missing_state = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_scheduler_trace_raw(extended=True)
        )
        self.assertIn(
            "missing post-arm scheduler unlock state marker",
            missing_state["validation_errors"],
        )

        duplicate_state = valid.replace(
            state + b"\r\n", state + b"\r\n" + state + b"\r\n", 1
        )
        duplicate = receiver.parse_sdc_hci_remove_iso_path_trace(duplicate_state)
        self.assertIn(
            "duplicate scheduler unlock state marker",
            duplicate["validation_errors"],
        )

        prearm = receiver.parse_sdc_hci_remove_iso_path_trace(state + b"\r\n" + valid)
        self.assertIn(
            "scheduler unlock state marker before trace arm",
            prearm["validation_errors"],
        )

        legacy_state = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_scheduler_trace_raw(state_line=state)
        )
        self.assertIn(
            "scheduler unlock state marker without extended scheduler unlock arm",
            legacy_state["validation_errors"],
        )

        arm = (
            b"SDC LE Remove ISO Data Path scheduler unlock trace armed: queue_is_mpsl=1"
        )
        entry = b"SDC LE Remove ISO Data Path k_sched_unlock entry"
        unlock_return = b"SDC LE Remove ISO Data Path k_sched_unlock return"
        before_entry = valid.replace(state + b"\r\n", b"", 1).replace(
            arm + b"\r\n" + entry + b"\r\n",
            arm + b"\r\n" + state + b"\r\n" + entry + b"\r\n",
            1,
        )
        before_entry_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(before_entry)
        self.assertIn(
            "out-of-order scheduler unlock state marker before entry",
            before_entry_parsed["validation_errors"],
        )
        after_return = valid.replace(state + b"\r\n", b"", 1).replace(
            unlock_return + b"\r\n",
            unlock_return + b"\r\n" + state + b"\r\n",
            1,
        )
        after_return_parsed = receiver.parse_sdc_hci_remove_iso_path_trace(after_return)
        self.assertIn(
            "out-of-order scheduler unlock state marker after return",
            after_return_parsed["validation_errors"],
        )

    def test_sdc_trace_parser_rejects_scheduler_unlock_marker_forms(self):
        valid = _sdc_scheduler_trace_raw()
        replacements = (
            (
                b"SDC LE Remove ISO Data Path scheduler unlock trace armed",
                b"SDC LE Remove ISO Data Path scheduler unlock trace armed: bad",
                "malformed scheduler unlock arm marker",
            ),
            (
                b"SDC LE Remove ISO Data Path k_sched_unlock entry",
                b"SDC LE Remove ISO Data Path k_sched_unlock entry: bad",
                "malformed scheduler unlock entry marker",
            ),
            (
                b"SDC LE Remove ISO Data Path k_sched_unlock return",
                b"SDC LE Remove ISO Data Path k_sched_unlock return: bad",
                "malformed scheduler unlock return marker",
            ),
        )

        for marker, malformed, expected in replacements:
            parsed = receiver.parse_sdc_hci_remove_iso_path_trace(
                valid.replace(marker, malformed, 1)
            )
            self.assertTrue(any(expected in error for error in parsed["parser_errors"]))

    def test_sdc_trace_parser_rejects_scheduler_unlock_duplicates_and_order(self):
        arm = b"SDC LE Remove ISO Data Path scheduler unlock trace armed"
        entry = b"SDC LE Remove ISO Data Path k_sched_unlock entry"
        unlock_return = b"SDC LE Remove ISO Data Path k_sched_unlock return"
        valid = _sdc_scheduler_trace_raw()

        duplicate_arm = valid.replace(arm + b"\r\n", arm + b"\r\n" + arm + b"\r\n", 1)
        self.assertIn(
            "duplicate scheduler unlock arm marker",
            receiver.parse_sdc_hci_remove_iso_path_trace(duplicate_arm)[
                "validation_errors"
            ],
        )
        duplicate_entry = valid.replace(
            entry + b"\r\n", entry + b"\r\n" + entry + b"\r\n", 1
        )
        self.assertIn(
            "duplicate scheduler unlock entry marker",
            receiver.parse_sdc_hci_remove_iso_path_trace(duplicate_entry)[
                "validation_errors"
            ],
        )
        duplicate_return = valid.replace(
            unlock_return + b"\r\n",
            unlock_return + b"\r\n" + unlock_return + b"\r\n",
            1,
        )
        self.assertIn(
            "duplicate scheduler unlock return marker",
            receiver.parse_sdc_hci_remove_iso_path_trace(duplicate_return)[
                "validation_errors"
            ],
        )

        arm_before_work_return = valid.replace(
            b"SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=1\r\n"
            + arm
            + b"\r\n",
            arm
            + b"\r\n"
            + b"SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=1\r\n",
            1,
        )
        self.assertTrue(
            any(
                "expected work_submit return before scheduler unlock arm" in error
                for error in receiver.parse_sdc_hci_remove_iso_path_trace(
                    arm_before_work_return
                )["validation_errors"]
            )
        )

        entry_before_arm = valid.replace(
            arm + b"\r\n" + entry + b"\r\n",
            entry + b"\r\n" + arm + b"\r\n",
            1,
        )
        self.assertTrue(
            any(
                "scheduler unlock entry before scheduler unlock arm" in error
                for error in receiver.parse_sdc_hci_remove_iso_path_trace(
                    entry_before_arm
                )["validation_errors"]
            )
        )

        return_before_entry = valid.replace(
            entry + b"\r\n" + unlock_return + b"\r\n",
            unlock_return + b"\r\n" + entry + b"\r\n",
            1,
        )
        self.assertTrue(
            any(
                "scheduler unlock return without scheduler unlock entry" in error
                for error in receiver.parse_sdc_hci_remove_iso_path_trace(
                    return_before_entry
                )["validation_errors"]
            )
        )

    def test_sdc_trace_parser_rejects_scheduler_unlock_missing_markers(self):
        valid = _sdc_scheduler_trace_raw()
        arm = b"SDC LE Remove ISO Data Path scheduler unlock trace armed\r\n"
        entry = b"SDC LE Remove ISO Data Path k_sched_unlock entry\r\n"
        unlock_return = b"SDC LE Remove ISO Data Path k_sched_unlock return\r\n"

        missing_entry = valid.replace(entry, b"", 1)
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(missing_entry)
        self.assertIn(
            "missing post-arm scheduler unlock entry marker",
            parsed["validation_errors"],
        )
        self.assertIn(
            "scheduler unlock return without scheduler unlock entry",
            parsed["validation_errors"],
        )

        missing_return = valid.replace(unlock_return, b"", 1)
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(missing_return)
        self.assertIn(
            "missing post-arm scheduler unlock return marker",
            parsed["validation_errors"],
        )

        arm_only = valid.replace(entry, b"", 1).replace(unlock_return, b"", 1)
        self.assertIn(
            "missing post-arm scheduler unlock entry marker",
            receiver.parse_sdc_hci_remove_iso_path_trace(arm_only)["validation_errors"],
        )
        self.assertIn(
            "missing post-arm scheduler unlock return marker",
            receiver.parse_sdc_hci_remove_iso_path_trace(arm_only)["validation_errors"],
        )

        no_scheduler_arm = valid.replace(arm, b"", 1)
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(no_scheduler_arm)
        self.assertIn(
            "scheduler unlock entry without arm marker", parsed["validation_errors"]
        )

    def test_sdc_trace_parser_validates_work_state_snapshot_and_offsets(self):
        schedule = (
            b"SDC LE Remove ISO Data Path work state snapshot scheduled: status=1"
        )
        snapshot = b"SDC LE Remove ISO Data Path work state snapshot: busy=0x4 mpsl_state=PENDING"
        raw = _sdc_trace_raw(schedule, snapshot)

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["snapshot_schema_version"], 1)
        self.assertEqual(parsed["snapshot_schedule_markers"][0]["status"], 1)
        self.assertEqual(parsed["snapshot_markers"][0]["busy"], 0x4)
        self.assertEqual(parsed["snapshot_markers"][0]["mpsl_state"], "PENDING")
        snapshot_start = raw.index(snapshot)
        self.assertEqual(parsed["snapshot_markers"][0]["start_offset"], snapshot_start)
        self.assertEqual(
            parsed["snapshot_markers"][0]["end_offset"],
            snapshot_start + len(snapshot) + 2,
        )

    def test_sdc_trace_parser_accepts_running_work_state_without_claiming_causality(
        self,
    ):
        raw = _sdc_trace_raw(
            b"SDC LE Remove ISO Data Path work state snapshot scheduled: status=1",
            b"SDC LE Remove ISO Data Path work state snapshot: busy=0x1 mpsl_state=RUNNING",
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["snapshot_markers"][0]["busy"], 0x1)
        self.assertEqual(parsed["snapshot_markers"][0]["mpsl_state"], "RUNNING")
        self.assertNotIn("work_state", parsed["snapshot_markers"][0])

    def test_sdc_trace_parser_rejects_snapshot_marker_forms(self):
        schedule = (
            b"SDC LE Remove ISO Data Path work state snapshot scheduled: status=1"
        )
        malformed_schedule = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_trace_raw(
                b"SDC LE Remove ISO Data Path work state snapshot scheduled: status=bad",
                None,
            )
        )
        self.assertTrue(
            any(
                "malformed work state snapshot schedule marker" in error
                for error in malformed_schedule["parser_errors"]
            )
        )

        malformed_snapshot = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_trace_raw(
                schedule,
                b"SDC LE Remove ISO Data Path work state snapshot: busy=nothex mpsl_state=PENDING",
            )
        )
        self.assertTrue(
            any(
                "malformed work state snapshot marker" in error
                for error in malformed_snapshot["parser_errors"]
            )
        )

    def test_sdc_trace_parser_rejects_snapshot_duplicate_and_order(self):
        schedule = (
            b"SDC LE Remove ISO Data Path work state snapshot scheduled: status=1"
        )
        snapshot = b"SDC LE Remove ISO Data Path work state snapshot: busy=0x4 mpsl_state=PENDING"
        duplicate_schedule = _sdc_trace_raw(schedule, snapshot).replace(
            schedule + b"\r\n", schedule + b"\r\n" + schedule + b"\r\n", 1
        )
        parsed_duplicate_schedule = receiver.parse_sdc_hci_remove_iso_path_trace(
            duplicate_schedule
        )
        self.assertIn(
            "duplicate work state snapshot schedule marker",
            parsed_duplicate_schedule["validation_errors"],
        )

        duplicate_snapshot = _sdc_trace_raw(schedule, snapshot).replace(
            snapshot + b"\r\n", snapshot + b"\r\n" + snapshot + b"\r\n", 1
        )
        parsed_duplicate_snapshot = receiver.parse_sdc_hci_remove_iso_path_trace(
            duplicate_snapshot
        )
        self.assertIn(
            "duplicate work state snapshot marker",
            parsed_duplicate_snapshot["validation_errors"],
        )

        ordered = _sdc_trace_raw(schedule, snapshot).splitlines(keepends=True)
        schedule_index = ordered.index(schedule + b"\r\n")
        work_return_index = ordered.index(
            b"SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=1\r\n"
        )
        ordered[schedule_index], ordered[work_return_index] = (
            ordered[work_return_index],
            ordered[schedule_index],
        )
        parsed_schedule_order = receiver.parse_sdc_hci_remove_iso_path_trace(
            b"".join(ordered)
        )
        self.assertTrue(
            any(
                "out-of-order post-arm trace event: expected work_submit return before work state snapshot schedule"
                in error
                for error in parsed_schedule_order["validation_errors"]
            )
        )

        ordered = _sdc_trace_raw(schedule, snapshot).splitlines(keepends=True)
        schedule_index = ordered.index(schedule + b"\r\n")
        snapshot_index = ordered.index(snapshot + b"\r\n")
        ordered[schedule_index], ordered[snapshot_index] = (
            ordered[snapshot_index],
            ordered[schedule_index],
        )
        parsed_snapshot_order = receiver.parse_sdc_hci_remove_iso_path_trace(
            b"".join(ordered)
        )
        self.assertIn(
            "out-of-order post-arm trace event: expected work state snapshot schedule before work state snapshot",
            parsed_snapshot_order["validation_errors"],
        )

    def test_sdc_trace_parser_rejects_negative_or_missing_snapshot(self):
        missing = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_trace_raw(
                b"SDC LE Remove ISO Data Path work state snapshot scheduled: status=1",
                None,
            )
        )
        self.assertIn(
            "missing post-arm work state snapshot marker after successful scheduling",
            missing["validation_errors"],
        )

        negative = receiver.parse_sdc_hci_remove_iso_path_trace(
            _sdc_trace_raw(
                b"SDC LE Remove ISO Data Path work state snapshot scheduled: status=-5",
                None,
            )
        )
        self.assertIn(
            "negative post-arm work state snapshot schedule status: -5",
            negative["validation_errors"],
        )
        self.assertIn(
            "work state snapshot unavailable after scheduling failure",
            negative["validation_errors"],
        )

    def test_sdc_trace_parser_accepts_legacy_trace_without_snapshot_markers(self):
        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(_sdc_trace_raw())

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["validation_errors"], [])
        self.assertEqual(parsed["snapshot_schedule_markers"], [])
        self.assertEqual(parsed["snapshot_markers"], [])
        self.assertEqual(parsed["scheduler_unlock_arm_markers"], [])
        self.assertEqual(parsed["scheduler_unlock_entries"], [])
        self.assertEqual(parsed["scheduler_unlock_returns"], [])

    def test_sdc_trace_parser_fails_closed_for_scheduling_state(self):
        prefix = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0\r\n"
        )
        missing = receiver.parse_sdc_hci_remove_iso_path_trace(
            prefix
            + b"SDC LE Remove ISO Data Path hci_internal_msg_get entry\r\n"
            + b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=0\r\n"
            + b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
        )
        self.assertIn(
            "missing post-arm lock_release entry marker", missing["validation_errors"]
        )
        self.assertIn(
            "missing post-arm work_submit entry marker", missing["validation_errors"]
        )
        self.assertIn(
            "out-of-order post-arm msg_get entry before work_submit entry",
            missing["validation_errors"],
        )

        malformed = receiver.parse_sdc_hci_remove_iso_path_trace(
            prefix
            + b"SDC LE Remove ISO Data Path multithreading_lock_release entry: bad\r\n"
        )
        self.assertTrue(
            any(
                "malformed lock_release entry marker" in error
                for error in malformed["parser_errors"]
            )
        )

        nonzero = receiver.parse_sdc_hci_remove_iso_path_trace(
            prefix
            + b"SDC LE Remove ISO Data Path multithreading_lock_release entry\r\n"
            + b"SDC LE Remove ISO Data Path multithreading_lock_release return\r\n"
            + b"SDC LE Remove ISO Data Path k_work_submit_to_queue entry\r\n"
            + b"SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=-11\r\n"
        )
        self.assertEqual(nonzero["work_submit_returns"][0]["status"], -11)
        self.assertIn(
            "invalid post-arm k_work_submit_to_queue return status: -11",
            nonzero["validation_errors"],
        )

    def test_sdc_trace_parser_requires_msg_get_entry(self):
        raw = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=0\r\n"
            b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(parsed["msg_get_entries"], [])
        self.assertEqual(parsed["msg_get_returns"][0]["status"], 0)
        self.assertIn(
            "missing post-arm msg_get entry marker", parsed["validation_errors"]
        )
        self.assertIn(
            "out-of-order post-arm msg_get return before entry",
            parsed["validation_errors"],
        )

    def test_sdc_trace_parser_requires_msg_get_return(self):
        raw = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_msg_get entry\r\n"
            b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(len(parsed["msg_get_entries"]), 1)
        self.assertEqual(parsed["msg_get_returns"], [])
        self.assertIn("missing post-arm msg_get return", parsed["validation_errors"])

    def test_sdc_trace_parser_rejects_msg_get_return_forms(self):
        prefix = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_msg_get entry\r\n"
        )

        nonzero = receiver.parse_sdc_hci_remove_iso_path_trace(
            prefix
            + b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=-5\r\n"
            + b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
        )
        self.assertEqual(nonzero["parser_errors"], [])
        self.assertEqual(nonzero["msg_get_returns"][0]["status"], -5)
        self.assertIn(
            "nonzero post-arm msg_get return status: -5",
            nonzero["validation_errors"],
        )

        malformed = receiver.parse_sdc_hci_remove_iso_path_trace(
            prefix
            + b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=bad\r\n"
            + b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
        )
        self.assertTrue(
            any(
                "malformed msg_get return marker" in error
                for error in malformed["parser_errors"]
            )
        )

        malformed_entry = receiver.parse_sdc_hci_remove_iso_path_trace(
            prefix.replace(
                b"SDC LE Remove ISO Data Path hci_internal_msg_get entry",
                b"SDC LE Remove ISO Data Path hci_internal_msg_get entry: unexpected",
            )
        )
        self.assertTrue(
            any(
                "malformed msg_get entry marker" in error
                for error in malformed_entry["parser_errors"]
            )
        )

    def test_sdc_trace_parser_rejects_completion_before_msg_get_return(self):
        raw = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_msg_get entry\r\n"
            b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_msg_get return: status=0\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertIn(
            "out-of-order post-arm SDC completion before msg_get return",
            parsed["validation_errors"],
        )

    def test_sdc_trace_parser_rejects_repeated_prompt(self):
        repeated_prompt = (
            b"uart:~$ uart:~$ [00:31:40.598,486] <inf> bt_bap: "
            b"SDC LE Remove ISO Data Path trace armed\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(repeated_prompt)

        self.assertEqual(parsed["arm_markers"], [])
        self.assertTrue(
            any("malformed arm marker" in error for error in parsed["parser_errors"])
        )
        self.assertIn("missing arm marker", parsed["validation_errors"])

    def test_sdc_trace_parser_rejects_postarm_drop(self):
        raw = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"uart:~$ --- 1 messages dropped ---\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertIn("dropped messages after arm: 1", parsed["validation_errors"])
        self.assertEqual(parsed["dropped_messages"][0]["classification"], "after_arm")

    def test_sdc_trace_parser_requires_entry_and_return(self):
        arm = b"SDC LE Remove ISO Data Path trace armed\r\n"
        missing_entry = receiver.parse_sdc_hci_remove_iso_path_trace(arm)
        self.assertIn(
            "missing post-arm SDC entry marker", missing_entry["validation_errors"]
        )
        self.assertIn(
            "missing post-arm cmd_put entry marker", missing_entry["validation_errors"]
        )

        missing_return = receiver.parse_sdc_hci_remove_iso_path_trace(
            arm + b"SDC LE Remove ISO Data Path trace entry\r\n"
        )
        self.assertIn(
            "missing post-arm SDC return", missing_return["validation_errors"]
        )

    def test_sdc_trace_parser_requires_cmd_put_return(self):
        raw = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
            b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertEqual(parsed["parser_errors"], [])
        self.assertEqual(len(parsed["cmd_put_entries"]), 1)
        self.assertEqual(parsed["cmd_put_returns"], [])
        self.assertIn("missing post-arm cmd_put return", parsed["validation_errors"])

    def test_sdc_trace_parser_rejects_cmd_put_return_forms(self):
        prefix = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
        )
        nonzero = receiver.parse_sdc_hci_remove_iso_path_trace(
            prefix
            + b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=-11\r\n"
            + b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
        )
        self.assertEqual(nonzero["parser_errors"], [])
        self.assertEqual(nonzero["cmd_put_returns"][0]["status"], -11)
        self.assertIn(
            "nonzero post-arm cmd_put return status: -11",
            nonzero["validation_errors"],
        )

        malformed = receiver.parse_sdc_hci_remove_iso_path_trace(
            prefix
            + b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0x00\r\n"
            + b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
        )
        self.assertTrue(
            any(
                "malformed cmd_put return marker" in error
                for error in malformed["parser_errors"]
            )
        )

        malformed_entry = receiver.parse_sdc_hci_remove_iso_path_trace(
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry: unexpected\r\n"
        )
        self.assertTrue(
            any(
                "malformed cmd_put entry marker" in error
                for error in malformed_entry["parser_errors"]
            )
        )

        order_prefix = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path hci_internal_cmd_put entry\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
        )
        out_of_order = receiver.parse_sdc_hci_remove_iso_path_trace(
            order_prefix
            + b"SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=0\r\n"
            + b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
            + b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
        )
        self.assertEqual(out_of_order["parser_errors"], [])
        self.assertIn(
            "out-of-order post-arm cmd_put return before SDC return",
            out_of_order["validation_errors"],
        )

    def test_sdc_trace_parser_requires_completion_after_return(self):
        raw = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertIn("missing post-arm SDC completion", parsed["validation_errors"])

    def test_sdc_trace_parser_rejects_completion_before_return(self):
        raw = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path completion: status=0x00\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
        )

        parsed = receiver.parse_sdc_hci_remove_iso_path_trace(raw)

        self.assertIn(
            "out-of-order post-arm SDC completion before return",
            parsed["validation_errors"],
        )

    def test_sdc_trace_parser_rejects_nonzero_or_malformed_completion(self):
        base = (
            b"SDC LE Remove ISO Data Path trace armed\r\n"
            b"SDC LE Remove ISO Data Path trace entry\r\n"
            b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n"
        )
        nonzero = receiver.parse_sdc_hci_remove_iso_path_trace(
            base + b"SDC LE Remove ISO Data Path completion: status=0x0c\r\n"
        )
        self.assertIn(
            "nonzero post-arm SDC completion status: 0x0c",
            nonzero["validation_errors"],
        )

        malformed = receiver.parse_sdc_hci_remove_iso_path_trace(
            base + b"SDC LE Remove ISO Data Path completion: status=bad\r\n"
        )
        self.assertTrue(
            any(
                "malformed completion marker" in error
                for error in malformed["parser_errors"]
            )
        )

    def test_sdc_trace_parser_rejects_out_of_order_and_nonzero_return(self):
        arm = b"SDC LE Remove ISO Data Path trace armed\r\n"
        entry = b"SDC LE Remove ISO Data Path trace entry\r\n"
        out_of_order = receiver.parse_sdc_hci_remove_iso_path_trace(
            arm + b"SDC LE Remove ISO Data Path trace return: status=0x00\r\n" + entry
        )
        self.assertIn(
            "out-of-order post-arm SDC return before entry",
            out_of_order["validation_errors"],
        )

        nonzero = receiver.parse_sdc_hci_remove_iso_path_trace(
            arm + entry + b"SDC LE Remove ISO Data Path trace return: status=0x0c\r\n"
        )
        self.assertIn(
            "nonzero post-arm SDC return status: 0x0c",
            nonzero["validation_errors"],
        )

    def test_sdc_trace_parser_rejects_malformed_and_duplicate_arm(self):
        malformed = receiver.parse_sdc_hci_remove_iso_path_trace(
            b"SDC LE Remove ISO Data Path trace armed: unexpected\r\n"
        )
        self.assertIn("malformed arm marker", malformed["parser_errors"][0])
        self.assertIn("missing arm marker", malformed["validation_errors"])

        arm = b"SDC LE Remove ISO Data Path trace armed\r\n"
        duplicate = receiver.parse_sdc_hci_remove_iso_path_trace(arm + arm)
        self.assertIn("duplicate arm marker", duplicate["validation_errors"])

    def test_stream_summary(self):
        line = (
            "Stream[0] summary: SDUs=150 decoded=150 plc=0 decode_err=0 "
            "i2s_underrun=0 stream_reset=0 empty_sdu=0"
        )
        parsed = receiver.parse_stream_summary(
            line + "\n" + line.replace("SDUs=150", "SDUs=200")
        )
        self.assertEqual(len(parsed), 2)
        self.assertEqual(parsed[0]["sdus"], 150)
        self.assertEqual(parsed[1]["sdus"], 200)
        for key in ("rx_valid", "rx_error", "rx_lost", "rx_unknown", "rx_no_ts"):
            self.assertIsNone(parsed[0][key])

    def test_stream_summary_extended_status_suffix(self):
        line = hil_fakes.receiver_stream_summary_line(
            rx_valid=123,
            rx_error=4,
            rx_lost=5,
            rx_unknown=6,
            rx_no_ts=7,
        )
        parsed = receiver.parse_stream_summary(line)
        self.assertEqual(
            {
                key: parsed[0][key]
                for key in ("rx_valid", "rx_error", "rx_lost", "rx_unknown", "rx_no_ts")
            },
            {
                "rx_valid": 123,
                "rx_error": 4,
                "rx_lost": 5,
                "rx_unknown": 6,
                "rx_no_ts": 7,
            },
        )

    def test_stream_summary_partial_status_suffix_no_match(self):
        line = (
            "Stream[0] summary: SDUs=150 decoded=150 plc=0 decode_err=0 "
            "i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=150 rx_error=1"
        )
        self.assertEqual(receiver.parse_stream_summary(line), [])

    def test_stream_summary_reordered_status_suffix_no_match(self):
        line = (
            "Stream[0] summary: SDUs=150 decoded=150 plc=0 decode_err=0 "
            "i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_error=1 rx_valid=150 "
            "rx_lost=0 rx_unknown=0 rx_no_ts=0"
        )
        self.assertEqual(receiver.parse_stream_summary(line), [])

    def test_fake_stream_summary_status_arguments_are_all_or_none_nonnegative(self):
        # Default is a healthy full summary (extended fields present); the
        # all-or-none grammar still forbids partial RX fields and negatives.
        self.assertIn("rx_valid=", hil_fakes.receiver_stream_summary_line())
        with self.assertRaises(ValueError):
            hil_fakes.receiver_stream_summary_line(
                rx_valid=1,
                rx_error=None,
                rx_lost=None,
                rx_unknown=None,
                rx_no_ts=None,
            )
        with self.assertRaises(ValueError):
            hil_fakes.receiver_stream_summary_line(
                rx_valid=-1, rx_error=0, rx_lost=0, rx_unknown=0, rx_no_ts=0
            )

    def test_stream_summary_with_log_prefix(self):
        line = (
            "[00:00:12] <inf> bt_bap: Stream[0] summary: SDUs=150 decoded=150 "
            "plc=0 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0"
        )
        self.assertEqual(receiver.parse_stream_summary(line)[0]["sdus"], 150)

    def test_stream_summary_missing_field_no_match(self):
        self.assertEqual(
            receiver.parse_stream_summary("Stream[0] summary: SDUs=150 decoded=150"), []
        )

    def test_stream_summary_malformed_number_no_match(self):
        self.assertEqual(
            receiver.parse_stream_summary(
                "Stream[0] summary: SDUs=N/A decoded=150 plc=0 decode_err=0 "
                "i2s_underrun=0 stream_reset=0 empty_sdu=0"
            ),
            [],
        )

    def test_iso_link_quality_parser_exact_output(self):
        text = hil_fakes.iso_link_quality_transcript(
            [
                {
                    "slot": 0,
                    "handle": 0x1234,
                    "tx_unacked": 1,
                    "tx_flushed": 2,
                    "tx_last_subevent": 3,
                    "retransmitted": 4,
                    "crc_error": 5,
                    "rx_unreceived": 6,
                    "duplicate": 7,
                    "iso_interval_1250us": 8,
                    "nse": 1,
                    "cig_sync_us": 1000,
                    "cis_sync_us": 1100,
                    "c_max_pdu": 120,
                    "c_phy": 2,
                    "c_bn": 1,
                    "c_flush_1250us": 32,
                },
                {
                    "slot": 1,
                    "handle": 0xABCD,
                    "tx_unacked": 8,
                    "tx_flushed": 9,
                    "tx_last_subevent": 10,
                    "retransmitted": 11,
                    "crc_error": 12,
                    "rx_unreceived": 13,
                    "duplicate": 14,
                    "iso_interval_1250us": 10,
                    "nse": 2,
                    "cig_sync_us": 1200,
                    "cis_sync_us": 1300,
                    "c_max_pdu": 240,
                    "c_phy": 2,
                    "c_bn": 2,
                    "c_flush_1250us": 40,
                },
            ]
        ).decode("utf-8")
        parsed = receiver.parse_iso_link_quality(text)

        self.assertEqual(
            parsed,
            {
                "header_seen": True,
                "malformed": False,
                "streams": [
                    {
                        "slot": 0,
                        "handle": 0x1234,
                        "tx_unacked": 1,
                        "tx_flushed": 2,
                        "tx_last_subevent": 3,
                        "retransmitted": 4,
                        "crc_error": 5,
                        "rx_unreceived": 6,
                        "duplicate": 7,
                        "iso_interval_1250us": 8,
                        "nse": 1,
                        "cig_sync_us": 1000,
                        "cis_sync_us": 1100,
                        "c_max_pdu": 120,
                        "c_phy": 2,
                        "c_bn": 1,
                        "c_flush_1250us": 32,
                    },
                    {
                        "slot": 1,
                        "handle": 0xABCD,
                        "tx_unacked": 8,
                        "tx_flushed": 9,
                        "tx_last_subevent": 10,
                        "retransmitted": 11,
                        "crc_error": 12,
                        "rx_unreceived": 13,
                        "duplicate": 14,
                        "iso_interval_1250us": 10,
                        "nse": 2,
                        "cig_sync_us": 1200,
                        "cis_sync_us": 1300,
                        "c_max_pdu": 240,
                        "c_phy": 2,
                        "c_bn": 2,
                        "c_flush_1250us": 40,
                    },
                ],
            },
        )
        self.assertEqual(receiver.validate_iso_link_quality(parsed, 2), [])

    def test_iso_link_quality_parser_ignores_physical_preheader_and_prefixed_logs(self):
        text = hil_fakes.iso_link_quality_transcript(
            [
                {
                    "slot": 0,
                    "handle": 0x0001,
                    "tx_last_subevent": 14323,
                    "rx_unreceived": 14317,
                },
                {
                    "slot": 1,
                    "handle": 0x0006,
                    "tx_last_subevent": 14250,
                    "rx_unreceived": 14241,
                },
            ]
        ).decode("utf-8")
        text = text.replace(
            "uart:~$ bt iso quality\r\n--- ISO link quality ---\r\n",
            "uart:~$ bt iso quality\r\n"
            "uart:~$ [00:31:30.816,893] <inf> bt_bap: "
            "Stream[0] started: CIG 0 CIS 0\r\n"
            "--- ISO link quality ---\r\n"
            "[00:31:30.817,004] <inf> bt_bap: "
            "Stream[1] started: CIG 0 CIS 1\r\n",
        )

        parsed = receiver.parse_iso_link_quality(text)

        self.assertTrue(parsed["header_seen"])
        self.assertFalse(parsed["malformed"])
        self.assertEqual([record["slot"] for record in parsed["streams"]], [0, 1])
        self.assertEqual(receiver.validate_iso_link_quality(parsed, 2), [])

    def test_iso_link_quality_parser_rejects_unindented_or_truncated_candidates(self):
        complete = (
            "  Stream[0] handle=0x1234 tx_unacked=0 tx_flushed=0 "
            "tx_last_subevent=0 retransmitted=0 crc_error=0 "
            "rx_unreceived=0 duplicate=0 iso_interval_1250us=8 nse=1 "
            "cig_sync_us=1000 cis_sync_us=1100 c_max_pdu=120 c_phy=2 c_bn=1 "
            "c_flush_1250us=32"
        )
        lines = (
            complete[2:],
            complete.replace("c_flush_1250us=32", "c_flush_1250us="),
        )
        for line in lines:
            with self.subTest(line=line):
                parsed = receiver.parse_iso_link_quality(
                    "--- ISO link quality ---\n" + line
                )
                self.assertTrue(parsed["malformed"])
                self.assertEqual(parsed["streams"], [])

    def test_iso_link_quality_validator_rejects_malformed_missing_and_duplicate(self):
        malformed = receiver.parse_iso_link_quality(
            "--- ISO link quality ---\n  Stream[0] handle=0x1234 tx_unacked=0\n"
        )
        self.assertTrue(
            any(
                "grammar malformed" in error
                for error in receiver.validate_iso_link_quality(malformed, 1)
            )
        )

        missing = receiver.parse_iso_link_quality(
            "  Stream[0] handle=0x1234 tx_unacked=0 tx_flushed=0 "
            "tx_last_subevent=0 retransmitted=0 crc_error=0 rx_unreceived=0 duplicate=0 "
            "iso_interval_1250us=8 nse=1 cig_sync_us=1000 cis_sync_us=1100 "
            "c_max_pdu=120 c_phy=2 c_bn=1 c_flush_1250us=32\n"
        )
        self.assertIn(
            "ISO link quality header missing",
            receiver.validate_iso_link_quality(missing, 1),
        )

        duplicate = receiver.parse_iso_link_quality(
            hil_fakes.iso_link_quality_transcript([{"slot": 0}, {"slot": 0}]).decode(
                "utf-8"
            )
        )
        errors = receiver.validate_iso_link_quality(duplicate, 2)
        self.assertIn("ISO link quality duplicate slot 0", errors)
        self.assertIn("ISO link quality missing slot(s): [1]", errors)

        zero_selected = receiver.parse_iso_link_quality(
            hil_fakes.iso_link_quality_transcript()
            .decode("utf-8")
            .replace("c_phy=2", "c_phy=0")
        )
        self.assertIn(
            "ISO link quality c_phy malformed",
            receiver.validate_iso_link_quality(zero_selected, 1),
        )

    def test_flpr_handshake_block(self):
        text = hil_fakes.flpr_status_transcript().decode("utf-8")
        hs = receiver.parse_flpr_handshake(text)
        self.assertTrue(hs["header_seen"])
        self.assertTrue(hs["ready"])
        self.assertTrue(hs["acked"])
        self.assertTrue(hs["healthy"])
        self.assertEqual(hs["err_len"], 0)
        self.assertEqual(hs["rx_lost"], 0)

    def test_flpr_handshake_missing_line_is_none(self):
        text = "--- FLPR handshake ---\n  Ready        : yes\n"
        hs = receiver.parse_flpr_handshake(text)
        self.assertTrue(hs["ready"])
        self.assertIsNone(hs["acked"], "missing evidence is None, never zero")

    def test_validate_receiver_blocks_missing_fails(self):
        errors = receiver.validate_receiver_blocks(
            {
                "decode_errors": None,
                "i2s_underruns": 0,
                "stream_resets": 0,
                "push_failures": 0,
            },
            receiver.parse_offload_status(""),
            receiver.parse_flpr_handshake(""),
        )
        self.assertTrue(errors, "missing evidence must fail")

    def test_validate_receiver_blocks_healthy(self):
        faults = {
            "status_seen": True,
            "decode_errors": 0,
            "i2s_underruns": 0,
            "stream_resets": 0,
            "push_failures": 0,
        }
        offload = receiver.parse_offload_status(
            hil_fakes.flpr_offload_transcript().decode("utf-8")
        )
        handshake = receiver.parse_flpr_handshake(
            hil_fakes.flpr_status_transcript().decode("utf-8")
        )
        self.assertEqual(
            receiver.validate_receiver_blocks(faults, offload, handshake), []
        )

    def test_validate_receiver_blocks_bad_counters_fail(self):
        faults = {
            "status_seen": True,
            "decode_errors": 3,
            "i2s_underruns": 0,
            "stream_resets": 0,
            "push_failures": 0,
        }
        offload = receiver.parse_offload_status(
            hil_fakes.flpr_offload_transcript().decode("utf-8")
        )
        handshake = receiver.parse_flpr_handshake(
            hil_fakes.flpr_status_transcript().decode("utf-8")
        )
        errors = receiver.validate_receiver_blocks(faults, offload, handshake)
        self.assertTrue(any("Decode errors" in e for e in errors))

    def test_validate_receiver_blocks_moving_single_pending_requires_proof(self):
        faults = {
            "status_seen": True,
            "decode_errors": 0,
            "i2s_underruns": 0,
            "stream_resets": 0,
            "push_failures": 0,
        }
        offload = receiver.parse_offload_status(
            hil_fakes.flpr_offload_transcript(submit=151, success=150).decode("utf-8")
        )
        handshake = receiver.parse_flpr_handshake(
            hil_fakes.flpr_status_transcript().decode("utf-8")
        )
        errors = receiver.validate_receiver_blocks(faults, offload, handshake)
        self.assertIn("offload submit/success mismatch", errors)
        self.assertEqual(
            receiver.validate_receiver_blocks(
                faults,
                offload,
                handshake,
                allow_moving_single_pending=True,
            ),
            [],
        )
        offload["submit"] = 152
        errors = receiver.validate_receiver_blocks(
            faults,
            offload,
            handshake,
            allow_moving_single_pending=True,
        )
        self.assertIn("offload submit/success mismatch", errors)

    def test_warning_signatures(self):
        for signature in (
            "LOG_WRN something",
            "LOG_ERR boom",
            "[00:00:01] <wrn> bt_bap: warning",
            "[00:00:01] <err> bt_bap: failure",
            "FATAL ERROR",
            "assertion failed",
            "fault in handler",
            "stack overflow",
            "i2s_nrfx: Next buffers not supplied on time",
            "Cannot write in state",
            "ISO sequence discontinuity",
            "unexpected recovery",
        ):
            self.assertEqual(receiver.scan_warnings([signature]), [signature])
        self.assertEqual(receiver.scan_warnings(["normal line"]), [])

    def test_shell_error_signatures(self):
        self.assertEqual(
            receiver.scan_shell_errors(
                "uart:~$ audio status\nerror: unavailable\nuart:~$ "
            ),
            ["error: unavailable"],
        )
        self.assertEqual(
            receiver.scan_shell_errors("uart:~$ bad\nbad: command not found\nuart:~$ "),
            ["bad: command not found"],
        )

    def test_command_receiver_transcript_bounded(self):
        wire = hil_fakes.Wire(
            "receiver",
            chunks=[
                hil_fakes.receiver_transcript("audio status", ["--- Audio status ---"])
            ],
        )
        with tempfile.TemporaryDirectory() as td:
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                os.path.join(td, "receiver-console.bin"),
                serial_class=lambda: hil_fakes.FakeSerial(wire),
            )
            console.open()
            transcript = receiver.run_receiver_command(
                console, "audio status", timeout=5.0
            )
            console.close()
        self.assertIn("--- Audio status ---", transcript)
        self.assertIn("uart:~$ ", transcript)

    def test_command_receiver_accepts_unterminated_zephyr_prompt(self):
        wire = hil_fakes.Wire(
            "receiver",
            chunks=[
                (
                    hil_fakes.receiver_command_echo("audio status")
                    + "--- Audio status ---\r\n"
                    + hil_fakes.RECEIVER_PROMPT
                ).encode("utf-8")
            ],
        )
        with tempfile.TemporaryDirectory() as td:
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                os.path.join(td, "receiver-console.bin"),
                serial_class=lambda: hil_fakes.FakeSerial(wire),
            )
            console.open()
            transcript = receiver.run_receiver_command(
                console, "audio status", timeout=5.0
            )
            console.close()
        self.assertTrue(transcript.endswith(hil_fakes.RECEIVER_PROMPT))

    def test_command_receiver_accepts_colored_real_zephyr_transcript(self):
        # Real receiver build enables CONFIG_SHELL_VT100_COMMANDS and
        # CONFIG_SHELL_VT100_COLORS. Zephyr wraps output and returned prompt
        # with CSI color sequences, while typed command echo stays plain.
        green = "\x1b[1;32m"
        reset = "\x1b[m"
        wire = hil_fakes.Wire(
            "receiver",
            chunks=[
                (
                    green
                    + hil_fakes.RECEIVER_PROMPT
                    + reset
                    + "audio status\r\n"
                    + green
                    + "--- Audio status ---"
                    + reset
                    + "\r\n"
                    + green
                    + hil_fakes.RECEIVER_PROMPT
                    + reset
                ).encode("utf-8")
            ],
        )
        with tempfile.TemporaryDirectory() as td:
            evidence_path = os.path.join(td, "receiver-console.bin")
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                evidence_path,
                serial_class=lambda: hil_fakes.FakeSerial(wire),
            )
            console.open()
            transcript = receiver.run_receiver_command(
                console, "audio status", timeout=5.0
            )
            console.close()
            with open(evidence_path, "rb") as fh:
                raw = fh.read()
        self.assertIn("--- Audio status ---", transcript)
        self.assertTrue(transcript.endswith(hil_fakes.RECEIVER_PROMPT))
        self.assertNotIn("\x1b[", transcript)
        self.assertIn(green.encode("utf-8"), raw)

    def test_command_receiver_cancellation_is_distinct_from_timeout(self):
        wire = hil_fakes.Wire("receiver")
        with tempfile.TemporaryDirectory() as td:
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                os.path.join(td, "receiver-console.bin"),
                serial_class=lambda: hil_fakes.FakeSerial(wire),
            )
            console.open()
            with self.assertRaises(serial_io.SerialConsoleCancelled):
                receiver.run_receiver_command(
                    console, "audio status", timeout=5.0, cancel=lambda: True
                )
            console.close()

    def test_colored_receiver_parsers_ignore_presentation_codes(self):
        green = "\x1b[1;32m"
        reset = "\x1b[m"
        text = "\n".join(
            (
                green + "Identity: DB:A6:0C:05:A2:AA (random)" + reset,
                green + "--- Audio status ---" + reset,
                green + "  Decode errors : 0" + reset,
                green + "  I2S underruns : 0" + reset,
                green + "  Stream resets : 0" + reset,
                green + "Stream[0] summary: SDUs=120 decoded=120 plc=0 decode_err=0 "
                "i2s_underrun=0 stream_reset=0 empty_sdu=0" + reset,
            )
        )
        self.assertEqual(receiver.parse_identity(text)["address_type"], "random")
        self.assertEqual(receiver.parse_audio_faults(text)["decode_errors"], 0)
        self.assertEqual(receiver.parse_stream_summary(text)[0]["sdus"], 120)

    def test_command_receiver_timeout_fails(self):
        wire = hil_fakes.Wire("receiver")  # no data at all
        with tempfile.TemporaryDirectory() as td:
            console = SerialConsole(
                "receiver",
                "/dev/ttyACM0",
                115200,
                os.path.join(td, "receiver-console.bin"),
                serial_class=lambda: hil_fakes.FakeSerial(wire),
            )
            console.open()
            with self.assertRaises(receiver.ReceiverError):
                receiver.run_receiver_command(console, "audio status", timeout=0.5)
            console.close()


# ── discovery ──────────────────────────────────────────────────────


def _discovery_runner(fake_sysfs, **scripts):
    """Scripted runner for one discovery test; ``scripts`` maps argv
    tuples to FakeProc."""
    runner = hil_fakes.ScriptedRunner()
    for argv, proc in scripts.items():
        runner.script(list(argv), proc)
    return runner


def _udev_proc(props):
    lines = ["%s=%s" % (k, v) for k, v in sorted(props.items())]
    return hil_fakes.FakeProc(stdout="\n".join(lines) + "\n")


def _script_discovery(
    fake_sysfs,
    runner,
    receiver_rows=None,
    find_token=None,
    openocd_proc=None,
    usb_props=None,
    tty_props=None,
):
    """Script every discovery boundary; udevadm node paths are the real
    (absolute) paths under the fake sysfs root."""
    sysfs = os.path.abspath(fake_sysfs)
    usb_root = os.path.join(sysfs, "bus", "usb", "devices")
    tty_root = os.path.join(sysfs, "class", "tty")
    runner.script(
        ["nrf-probes"],
        hil_fakes.FakeProc(stdout=hil_fakes.default_probe_table(receiver_rows)),
    )
    runner.script(
        ["nrf-probes", "--find", "nrf54l"],
        hil_fakes.FakeProc(stdout=(find_token or hil_fakes.RECEIVER_SERIAL) + "\n"),
    )
    usb_props = usb_props or {
        "1-2": {
            "ID_VENDOR_ID": "1234",
            "ID_MODEL_ID": "5678",
            "ID_SERIAL_SHORT": "SOME-OTHER",
            "DEVPATH": "/devices/pci0000:00/usb1/1-2",
        },
        "1-3": {
            "ID_VENDOR_ID": "1366",
            "ID_MODEL_ID": "1015",
            "ID_SERIAL_SHORT": hil_fakes.SOURCE_SERIAL,
            "DEVPATH": "/devices/pci0000:00/usb1/1-3",
        },
    }
    for usb_id, props in sorted(usb_props.items()):
        runner.script(
            [
                "udevadm",
                "info",
                "--query=property",
                "--path",
                os.path.join(usb_root, usb_id),
            ],
            _udev_proc(props),
        )
    tty_props = tty_props or {
        "ttyACM0": {
            "ID_SERIAL_SHORT": hil_fakes.RECEIVER_SERIAL,
            "DEVPATH": hil_fakes.tty_devpath("1-2", "ttyACM0"),
        },
        "ttyACM1": {
            "ID_SERIAL_SHORT": hil_fakes.SOURCE_SERIAL,
            "DEVPATH": hil_fakes.tty_devpath("1-3", "ttyACM1"),
        },
    }
    for tty, props in sorted(tty_props.items()):
        runner.script(
            [
                "udevadm",
                "info",
                "--query=property",
                "--path",
                os.path.join(tty_root, tty),
            ],
            _udev_proc(props),
        )
    runner.script(
        [hil_fakes.SOURCE_SERIAL, "jlink", "1-3"],
        hil_fakes.FakeProc(stdout="placeholder"),  # replaced below
    )
    openocd = openocd_proc or hil_fakes.FakeProc(stdout=hil_fakes.fingerprint_output())
    runner.rules[-1] = (
        ["openocd", "-f", "interface/jlink.cfg"],
        openocd,
        False,  # prefix match
    )
    return runner


def _load_binding(tmpdir):
    cfg = os.path.join(tmpdir, "cfg")
    os.makedirs(cfg)
    fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
    fixture = model.load_logical_fixture(fixture_path)
    binding = model.load_physical_binding(binding_path, fixture)
    return binding


class TestStreamTransportLimits(unittest.TestCase):
    """Frozen receiver transport limits (system-hil-milestones.md, 2026-09-03).

    Pure public-boundary tests of receiver.validate_stream_transport plus the
    runner session-end composition. No serial, no processes.
    """

    EXPECTED_SUBMITTED_10MS = 12644  # 144 preamble + 12000 scored + 500 tail
    EXPECTED_SUBMITTED_7P5 = 16859  # 192 + 16000 + 667

    @staticmethod
    def _summary(**overrides):
        base = {
            "slot": 0,
            "sdus": 12200,
            "decoded": 12350,
            "plc": 150,
            "decode_err": 0,
            "i2s_underrun": 0,
            "stream_reset": 0,
            "empty_sdu": 0,
            "rx_valid": 12200,
            "rx_error": 0,
            "rx_lost": 30,
            "rx_unknown": 0,
            "rx_no_ts": 2,
        }
        base.update(overrides)
        return base

    def test_healthy_summary_passes(self):
        summary = self._summary()
        self.assertEqual(
            receiver.validate_stream_transport(summary, self.EXPECTED_SUBMITTED_10MS),
            [],
        )

    def test_h42_real_failure_numbers_violate_both_limits(self):
        # Exact H42 receiver summary: rx_valid=24 of 16859 submitted,
        # plc=38924 of decoded=38972. Both the delivery floor and the
        # concealment ceiling must fire.
        h42 = self._summary(
            sdus=24,
            decoded=38972,
            plc=38924,
            rx_valid=24,
            rx_lost=19462,
            rx_no_ts=12,
        )
        violations = receiver.validate_stream_transport(
            h42, self.EXPECTED_SUBMITTED_7P5
        )
        joined = " | ".join(violations)
        self.assertIn("rx_valid=24 below floor", joined)
        self.assertIn("90% of 16859", joined)
        self.assertIn("plc=38924 above ceiling", joined)
        self.assertIn("decoded=38972", joined)

    def test_missing_extended_fields_fail_closed(self):
        summary = self._summary(rx_valid=None)
        violations = receiver.validate_stream_transport(
            summary, self.EXPECTED_SUBMITTED_10MS
        )
        self.assertTrue(any("rx_valid missing" in v for v in violations), violations)

    def test_rx_valid_boundary_exact_floor_passes_one_below_fails(self):
        # 90% of 12644 = 11379.6 (float floor): rx_valid=11380 passes,
        # 11379 fails. Float comparison is intentionally conservative.
        self.assertEqual(
            receiver.validate_stream_transport(
                self._summary(rx_valid=11380), self.EXPECTED_SUBMITTED_10MS
            ),
            [],
        )
        violations = receiver.validate_stream_transport(
            self._summary(rx_valid=11379), self.EXPECTED_SUBMITTED_10MS
        )
        self.assertTrue(
            any("rx_valid=11379 below floor" in v for v in violations),
            violations,
        )

    def test_plc_boundary_exact_ceiling_passes_one_above_fails(self):
        # 5% of decoded=16400 = 820 -> plc=820 passes, 821 fails.
        summary_pass = self._summary(decoded=16400, plc=820)
        self.assertEqual(
            receiver.validate_stream_transport(
                summary_pass, self.EXPECTED_SUBMITTED_10MS
            ),
            [],
        )
        violations = receiver.validate_stream_transport(
            self._summary(decoded=16400, plc=821), self.EXPECTED_SUBMITTED_10MS
        )
        self.assertTrue(
            any("plc=821 above ceiling" in v for v in violations), violations
        )

    def test_each_zero_field_violation_is_named(self):
        for field in ("rx_error", "rx_unknown", "empty_sdu"):
            violations = receiver.validate_stream_transport(
                self._summary(**{field: 3}), self.EXPECTED_SUBMITTED_10MS
            )
            self.assertTrue(
                any("%s=3 (must be 0)" % field in v for v in violations),
                (field, violations),
            )

    def test_rx_lost_and_rx_no_ts_are_record_only(self):
        # Large record-only values must never gate a healthy delivery.
        summary = self._summary(rx_lost=19000, rx_no_ts=900)
        self.assertEqual(
            receiver.validate_stream_transport(summary, self.EXPECTED_SUBMITTED_10MS),
            [],
        )

    def test_zero_decoded_does_not_add_extra_plc_rule(self):
        # decoded=0 must not crash or fire a plc rule (no zero-division, no
        # invented concealment metric). Delivery health is separately gated
        # by the rx_valid floor (proved by the H42 test).
        summary = self._summary(rx_valid=12000, decoded=0, plc=0)
        self.assertEqual(
            receiver.validate_stream_transport(summary, self.EXPECTED_SUBMITTED_10MS),
            [],
        )


class TestDiscovery(unittest.TestCase):
    def test_fingerprint_jlink_uses_current_port_commands_and_valid_markers(self):
        runner = hil_fakes.ScriptedRunner()
        runner.script(
            ["openocd", "-f", "interface/jlink.cfg"],
            hil_fakes.FakeProc(stdout=hil_fakes.fingerprint_output()),
            exact=False,
        )

        argv, _raw, markers, failures, status = discovery.fingerprint_jlink(
            runner, hil_fakes.SOURCE_SERIAL
        )

        self.assertEqual(argv[10], "gdb port disabled")
        self.assertEqual(argv[12], "tcl port disabled")
        self.assertEqual(argv[14], "telnet port disabled")
        self.assertFalse(
            any(
                legacy in element
                for element in argv
                for legacy in ("gdb_port", "tcl_port", "telnet_port")
            )
        )
        self.assertEqual(status, 0)
        self.assertEqual(failures, [])
        self.assertEqual(
            markers,
            {
                "dpidr": "0x6ba02477",
                "ap0": "0x00000000",
                "ap1": "0x00000000",
                "ap2": "0x12880000",
                "ap3": "0x12880000",
                "part": "0x00005340",
                "variant": "0x41414141",
            },
        )

    def test_unique_receiver_and_source_resolve(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            _script_discovery(sysfs, runner)
            resolution = discovery.resolve_fixture(
                binding, run_cmd=runner, sysfs_root=sysfs
            )
            rec = resolution.roles["receiver"]
            src = resolution.roles["source"]
            self.assertEqual(rec.probe.serial, hil_fakes.RECEIVER_SERIAL)
            self.assertEqual(rec.probe.part, "0x00054b15")
            self.assertEqual(rec.probe.target, "nRF54L15")
            self.assertEqual(rec.serial.path, "/dev/ttyACM0")
            self.assertEqual(src.probe.backend, "jlink")
            self.assertEqual(src.probe.serial, hil_fakes.SOURCE_SERIAL)
            self.assertEqual(src.probe.part, "0x00005340")
            self.assertEqual(src.serial.path, "/dev/ttyACM1")
            self.assertNotEqual(rec.serial.usb_parent, src.serial.usb_parent)
            # Raw identity evidence retained.
            self.assertIn("nrf-probes", resolution.raw)
            self.assertIn("source-jlink-fingerprint", resolution.raw)

    def test_runner_records_discovery_commands_in_ledger(self):
        # Production discovery is routed through Runner._command so identity
        # probes carry argv/status/duration provenance in commands.jsonl.
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            command_runner = hil_fakes.ScriptedRunner()
            _script_discovery(sysfs, command_runner)
            deps = RunnerDeps(run_cmd=command_runner, sysfs_root=sysfs)
            engine = Runner(deps)
            engine._run_dir = td
            resolution = deps.resolve(binding, run_cmd=engine._discovery_command)
            self.assertEqual(
                resolution.roles["source"].probe.serial, hil_fakes.SOURCE_SERIAL
            )
            argv0 = [record["argv"][0] for record in engine.commands]
            self.assertIn("nrf-probes", argv0)
            self.assertIn("udevadm", argv0)
            self.assertIn("openocd", argv0)
            for record in engine.commands:
                self.assertIn("start_utc", record)
                self.assertIn("end_utc", record)
                self.assertIn("duration_s", record)
                self.assertEqual(record["status"], 0)

    def test_injected_discovery_receives_runner_command_ledger(self):
        with tempfile.TemporaryDirectory() as td:
            observed = {}
            command_runner = hil_fakes.ScriptedRunner()
            command_runner.script(["nrf-probes"], hil_fakes.FakeProc(stdout="probe\n"))

            def injected(_binding, sysfs_root=None, run_cmd=None):
                del sysfs_root
                observed["proc"] = run_cmd(["nrf-probes"], 1)
                return fake_resolution()

            binding = _load_binding(td)
            deps = RunnerDeps(run_cmd=command_runner, discover=injected)
            engine = Runner(deps)
            engine._run_dir = td
            deps.resolve(binding, run_cmd=engine._discovery_command)
            self.assertEqual(observed["proc"].stdout, "probe\n")
            self.assertEqual(engine.commands[0]["argv"], ["nrf-probes"])

    def test_default_environment_probes_are_ledgered(self):
        with tempfile.TemporaryDirectory() as td:
            calls = []

            def environment_probe(argv, timeout, env=None):
                del timeout, env
                calls.append(list(argv))
                if argv == ["lsof", "--", "/dev/ttyACM1"]:
                    return hil_fakes.FakeProc(returncode=1)
                if argv == ["fw-flash-hil-source"]:
                    return hil_fakes.FakeProc(stdout="ok\n")
                if argv == ["fw-flash-54l15"]:
                    return hil_fakes.FakeProc(stdout="ok\n")
                if argv[:2] == ["git", "-C"] and argv[-2:] == ["rev-parse", "HEAD"]:
                    return hil_fakes.FakeProc(stdout="deadbeef\n")
                if argv[:2] == ["git", "-C"] and argv[-2:] == ["status", "--porcelain"]:
                    return hil_fakes.FakeProc(stdout="")
                if argv in (
                    ["python3", "--version"],
                    ["pytest", "--version"],
                    ["python3", "-c", "import serial; print(serial.VERSION)"],
                    ["openocd", "--version"],
                    ["nrf-probes", "--help"],
                    ["west", "--version"],
                ):
                    return hil_fakes.FakeProc(stdout="tool 1\n")
                raise AssertionError("unexpected command: %r" % (argv,))

            receiver_wire = _receiver_passing_wire(RUN_ID)
            source_t = hil_fakes.build_passing_source_wire(RUN_ID)
            src_chunks, src_writes = source_t.build()
            source_wire = hil_fakes.Wire(
                "source", chunks=src_chunks, assert_writes=src_writes
            )
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                run_cmd=environment_probe,
                environment=None,
            )
            self.assertEqual(result[0], "passed")
            with open(os.path.join(out_root, run_id, "commands.jsonl")) as fh:
                records = [json.loads(line) for line in fh if line.strip()]
            argv_records = [record["argv"] for record in records]
            self.assertTrue(
                any(
                    record[:2] == ["git", "-C"] and record[-2:] == ["rev-parse", "HEAD"]
                    for record in argv_records
                )
            )
            self.assertIn(["west", "--version"], argv_records)

    def test_source_probe_udev_property_drift_fails(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            command_runner = hil_fakes.ScriptedRunner()
            _script_discovery(
                sysfs,
                command_runner,
                usb_props={
                    "1-2": {
                        "ID_VENDOR_ID": "1234",
                        "ID_MODEL_ID": "5678",
                        "ID_SERIAL_SHORT": "SOME-OTHER",
                        "DEVPATH": "/devices/pci0000:00/usb1/1-2",
                    },
                    "1-3": {
                        "ID_VENDOR_ID": "1366",
                        "ID_MODEL_ID": "DIFFERENT",
                        "ID_SERIAL_SHORT": hil_fakes.SOURCE_SERIAL,
                        "DEVPATH": "/devices/pci0000:00/usb1/1-3",
                    },
                },
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(
                    binding, run_cmd=command_runner, sysfs_root=sysfs
                )
            self.assertIn("source J-Link ambiguity", str(ctx.exception))

    def test_missing_tty_fails(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            _script_discovery(
                sysfs,
                runner,
                tty_props={
                    "ttyACM0": {
                        "ID_SERIAL_SHORT": hil_fakes.RECEIVER_SERIAL,
                        "DEVPATH": hil_fakes.tty_devpath("1-2", "ttyACM0"),
                    },
                    # ttyACM1 present but serial does not match source.
                    "ttyACM1": {
                        "ID_SERIAL_SHORT": "OTHER",
                        "DEVPATH": hil_fakes.tty_devpath("1-3", "ttyACM1"),
                    },
                },
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(binding, run_cmd=runner, sysfs_root=sysfs)
            self.assertIn("serial", str(ctx.exception))

    def test_duplicate_tty_fails(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            os.makedirs(os.path.join(sysfs, "class", "tty", "ttyACM2"))
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            _script_discovery(
                sysfs,
                runner,
                tty_props={
                    # Two ttys both carry the receiver probe serial.
                    "ttyACM0": {
                        "ID_SERIAL_SHORT": hil_fakes.RECEIVER_SERIAL,
                        "DEVPATH": hil_fakes.tty_devpath("1-2", "ttyACM0"),
                    },
                    "ttyACM2": {
                        "ID_SERIAL_SHORT": hil_fakes.RECEIVER_SERIAL,
                        "DEVPATH": hil_fakes.tty_devpath("1-4", "ttyACM2"),
                    },
                    "ttyACM1": {
                        "ID_SERIAL_SHORT": hil_fakes.SOURCE_SERIAL,
                        "DEVPATH": hil_fakes.tty_devpath("1-3", "ttyACM1"),
                    },
                },
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(binding, run_cmd=runner, sysfs_root=sysfs)
            self.assertIn("exactly one tty", str(ctx.exception))

    def test_wrong_receiver_part_fails(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            _script_discovery(
                sysfs,
                runner,
                receiver_rows=[
                    (
                        "PROBE-ABC123",
                        "DAPLink",
                        "nRF54L15",
                        "0x6ba02477",
                        "0x00005415",
                        "BAAA",
                        "",
                    ),
                ],
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(binding, run_cmd=runner, sysfs_root=sysfs)
            self.assertIn("PART drift", str(ctx.exception))

    def test_receiver_variant_missing_fails(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            _script_discovery(
                sysfs,
                runner,
                receiver_rows=[
                    (
                        "PROBE-ABC123",
                        "DAPLink",
                        "nRF54L15",
                        "0x6ba02477",
                        "0x00054b15",
                        "-",
                        "",
                    ),
                ],
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(binding, run_cmd=runner, sysfs_root=sysfs)
            self.assertIn("VARIANT", str(ctx.exception))

    def test_malformed_nrf_probes_table_fails(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            _script_discovery(sysfs, runner)
            runner.rules[0] = (
                ["nrf-probes"],
                hil_fakes.FakeProc(stdout="SERIAL  TARGET\nABC  nRF54L15\n"),
                True,
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(binding, run_cmd=runner, sysfs_root=sysfs)
            self.assertIn("malformed nrf-probes header", str(ctx.exception))

    def test_source_jlink_ambiguity_fails(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            _script_discovery(
                sysfs,
                runner,
                usb_props={
                    "1-2": {
                        "ID_VENDOR_ID": "1366",
                        "ID_MODEL_ID": "1015",
                        "ID_SERIAL_SHORT": hil_fakes.SOURCE_SERIAL,
                        "DEVPATH": "/devices/pci0000:00/usb1/1-2",
                    },
                    "1-3": {
                        "ID_VENDOR_ID": "1366",
                        "ID_MODEL_ID": "1015",
                        "ID_SERIAL_SHORT": hil_fakes.SOURCE_SERIAL,
                        "DEVPATH": "/devices/pci0000:00/usb1/1-3",
                    },
                },
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(binding, run_cmd=runner, sysfs_root=sysfs)
            self.assertIn("ambiguity", str(ctx.exception))

    def test_source_jlink_wrong_part_fails(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            _script_discovery(
                sysfs,
                runner,
                openocd_proc=hil_fakes.FakeProc(
                    stdout=hil_fakes.fingerprint_output(part="0x00005415")
                ),
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(binding, run_cmd=runner, sysfs_root=sysfs)
            self.assertIn("PART drift", str(ctx.exception))

    def test_source_jlink_openocd_warning_fails(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            _script_discovery(
                sysfs,
                runner,
                openocd_proc=hil_fakes.FakeProc(
                    stdout=hil_fakes.fingerprint_output(
                        failure="Error: target not halted"
                    )
                ),
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(binding, run_cmd=runner, sysfs_root=sysfs)
            self.assertIn("OpenOCD failure", str(ctx.exception))

    def test_source_jlink_openocd_nonzero_fails(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            _script_discovery(
                sysfs,
                runner,
                openocd_proc=hil_fakes.FakeProc(
                    stdout=hil_fakes.fingerprint_output(), returncode=7
                ),
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(binding, run_cmd=runner, sysfs_root=sysfs)
            self.assertIn("OpenOCD failure", str(ctx.exception))

    def test_discovery_failure_retains_partial_raw_evidence(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            command_runner = hil_fakes.ScriptedRunner()
            _script_discovery(
                sysfs,
                command_runner,
                openocd_proc=hil_fakes.FakeProc(
                    stdout=hil_fakes.fingerprint_output(), returncode=7
                ),
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(
                    binding, run_cmd=command_runner, sysfs_root=sysfs
                )
            self.assertIn("nrf-probes", ctx.exception.raw)
            self.assertIn("nrf-probes-find", ctx.exception.raw)
            self.assertIn("source-jlink-fingerprint", ctx.exception.raw)
            self.assertEqual(ctx.exception.raw["source-jlink-fingerprint"]["status"], 7)

    def test_shared_tty_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            sysfs = os.path.abspath(sysfs)
            usb_root = os.path.join(sysfs, "bus", "usb", "devices")
            tty_root = os.path.join(sysfs, "class", "tty")
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            # ttyACM0 carries BOTH probe serials: receiver correlation
            # matches it and source correlation matches it too, so both
            # roles resolve to the same tty and discovery must reject.
            runner.script(
                ["nrf-probes"],
                hil_fakes.FakeProc(stdout=hil_fakes.default_probe_table()),
            )
            runner.script(
                ["nrf-probes", "--find", "nrf54l"],
                hil_fakes.FakeProc(stdout="%s\n" % hil_fakes.RECEIVER_SERIAL),
            )
            runner.script(
                [
                    "udevadm",
                    "info",
                    "--query=property",
                    "--path",
                    os.path.join(usb_root, "1-2"),
                ],
                _udev_proc(
                    {
                        "ID_VENDOR_ID": "1234",
                        "ID_MODEL_ID": "5678",
                        "ID_SERIAL_SHORT": "OTHER",
                        "DEVPATH": "/devices/pci0000:00/usb1/1-2",
                    }
                ),
            )
            runner.script(
                [
                    "udevadm",
                    "info",
                    "--query=property",
                    "--path",
                    os.path.join(usb_root, "1-3"),
                ],
                _udev_proc(
                    {
                        "ID_VENDOR_ID": "1366",
                        "ID_MODEL_ID": "1015",
                        "ID_SERIAL_SHORT": hil_fakes.SOURCE_SERIAL,
                        "DEVPATH": "/devices/pci0000:00/usb1/1-3",
                    }
                ),
            )
            runner.script(
                ["openocd", "-f", "interface/jlink.cfg"],
                hil_fakes.FakeProc(stdout=hil_fakes.fingerprint_output()),
                exact=False,
            )
            both = {
                "ID_SERIAL_SHORT": hil_fakes.RECEIVER_SERIAL,
                "DEVPATH": hil_fakes.tty_devpath("1-2", "ttyACM0"),
            }
            runner.script(
                [
                    "udevadm",
                    "info",
                    "--query=property",
                    "--path",
                    os.path.join(tty_root, "ttyACM0"),
                ],
                _udev_proc(both),
            )
            runner.script(
                [
                    "udevadm",
                    "info",
                    "--query=property",
                    "--path",
                    os.path.join(tty_root, "ttyACM1"),
                ],
                _udev_proc({**both, "ID_SERIAL_SHORT": hil_fakes.SOURCE_SERIAL}),
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(binding, run_cmd=runner, sysfs_root=sysfs)
            self.assertIn("same", str(ctx.exception))

    def test_shared_usb_parent_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            sysfs = hil_fakes.build_fake_sysfs(td)
            binding = _load_binding(td)
            runner = hil_fakes.ScriptedRunner()
            _script_discovery(
                sysfs,
                runner,
                tty_props={
                    # Both ttys hang off the same USB parent device.
                    "ttyACM0": {
                        "ID_SERIAL_SHORT": hil_fakes.RECEIVER_SERIAL,
                        "DEVPATH": hil_fakes.tty_devpath("1-2", "ttyACM0"),
                    },
                    "ttyACM1": {
                        "ID_SERIAL_SHORT": hil_fakes.SOURCE_SERIAL,
                        "DEVPATH": hil_fakes.tty_devpath("1-2", "ttyACM1"),
                    },
                },
            )
            with self.assertRaises(HilDiscoveryError) as ctx:
                discovery.resolve_fixture(binding, run_cmd=runner, sysfs_root=sysfs)
            self.assertIn("USB parent", str(ctx.exception))


# ── fw-flash-hil-source helper ─────────────────────────────────────

FAKE_OPENOCD = """#!/usr/bin/env bash
printf '%s\\0' "$@" > "${FAKE_OPENOCD_ARGV_FILE:?}"
printf 'openocd stdout\\n'
printf 'openocd stderr\\n' >&2
exit "${FAKE_OPENOCD_STATUS:-0}"
"""

FAKE_WEST = """#!/usr/bin/env bash
exit 0
"""

FAKE_NRF_PROBES = """#!/usr/bin/env bash
printf '%s\\n' "${FAKE_NRF_PROBES_SERIAL:?}"
"""


class FlashHelperHarness:
    def __init__(self, tmpdir, artifacts=True, serial="J-LINK-0001"):
        self.tmpdir = tmpdir
        self.repo = os.path.join(tmpdir, "repo")
        bin_dir = os.path.join(self.repo, "scripts", "bin")
        os.makedirs(bin_dir)
        shutil.copy(
            os.path.join(_REPO, "scripts", "bin", "fw-flash-hil-source"),
            os.path.join(bin_dir, "fw-flash-hil-source"),
        )
        shutil.copy(
            os.path.join(_REPO, "scripts", "bin", "fw-common.sh"),
            os.path.join(bin_dir, "fw-common.sh"),
        )
        shutil.copy(
            os.path.join(_REPO, "scripts", "bin", "fw-flash-54l15"),
            os.path.join(bin_dir, "fw-flash-54l15"),
        )
        if artifacts:
            for rel in (
                "build/hil-source/app/zephyr/zephyr.hex",
                "build/hil-source/hci_ipc/zephyr/zephyr.hex",
            ):
                path = os.path.join(self.repo, rel)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w") as fh:
                    fh.write("hex\n")
        self.fakebin = os.path.join(tmpdir, "fakebin")
        os.makedirs(self.fakebin)
        with open(os.path.join(self.fakebin, "openocd"), "w") as fh:
            fh.write(FAKE_OPENOCD)
        os.chmod(os.path.join(self.fakebin, "openocd"), 0o755)
        with open(os.path.join(self.fakebin, "west"), "w") as fh:
            fh.write(FAKE_WEST)
        os.chmod(os.path.join(self.fakebin, "west"), 0o755)
        with open(os.path.join(self.fakebin, "nrf-probes"), "w") as fh:
            fh.write(FAKE_NRF_PROBES)
        os.chmod(os.path.join(self.fakebin, "nrf-probes"), 0o755)
        self.argv_file = os.path.join(tmpdir, "openocd.argv")
        self.env = dict(os.environ)
        self.env["PATH"] = self.fakebin + os.pathsep + self.env["PATH"]
        self.env["ZEPHYR_BASE"] = os.path.join(tmpdir, "zephyrbase")
        os.makedirs(self.env["ZEPHYR_BASE"])
        self.env["FAKE_OPENOCD_ARGV_FILE"] = self.argv_file
        self.env["FAKE_NRF_PROBES_SERIAL"] = "PROBE-ABC123"
        self.env.pop("FAKE_OPENOCD_STATUS", None)
        self.serial = serial

    def run(self, env_extra=None, status_env=None):
        env = dict(self.env)
        if env_extra:
            env.update(env_extra)
        if status_env is not None:
            env["FAKE_OPENOCD_STATUS"] = str(status_env)
        env["FW_HIL_SOURCE_JLINK_SERIAL"] = self.serial
        return subprocess.run(
            [os.path.join(self.repo, "scripts", "bin", "fw-flash-hil-source")],
            env=env,
            capture_output=True,
            text=True,
            timeout=30,
        )

    def run_receiver_flash(self, env_extra=None, status_env=None):
        env = dict(self.env)
        if env_extra:
            env.update(env_extra)
        if status_env is not None:
            env["FAKE_OPENOCD_STATUS"] = str(status_env)
        return subprocess.run(
            [os.path.join(self.repo, "scripts", "bin", "fw-flash-54l15")],
            env=env,
            capture_output=True,
            text=True,
            timeout=30,
        )


def _nul_args(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as fh:
        return [a.decode("utf-8") for a in fh.read().split(b"\0") if a]


class TestFlashHelper(unittest.TestCase):
    def test_exact_argv_cpunet_first_verify_both_reset(self):
        with tempfile.TemporaryDirectory() as td:
            h = FlashHelperHarness(td)
            r = h.run()
            self.assertEqual(0, r.returncode, r.stderr)
            args = _nul_args(h.argv_file)
            net_hex = os.path.join(
                h.repo, "build", "hil-source", "hci_ipc", "zephyr", "zephyr.hex"
            )
            app_hex = os.path.join(
                h.repo, "build", "hil-source", "app", "zephyr", "zephyr.hex"
            )
            expected = [
                "-f",
                "interface/jlink.cfg",
                "-c",
                "transport select swd",
                "-c",
                "adapter speed 2000",
                "-c",
                "adapter serial J-LINK-0001",
                "-f",
                "target/nordic/nrf53.cfg",
                "-c",
                "init",
                "-c",
                "targets nrf53.cpunet",
                "-c",
                "program %s verify" % net_hex,
                "-c",
                "targets nrf53.cpuapp",
                "-c",
                "program %s verify" % app_hex,
                "-c",
                "reset run",
                "-c",
                "shutdown",
            ]
            self.assertEqual(args, expected)
            self.assertLess(
                args.index("targets nrf53.cpunet"),
                args.index("targets nrf53.cpuapp"),
                "cpunet programmed before cpuapp",
            )
            self.assertIn("adapter serial J-LINK-0001", args)
            self.assertNotIn("nrf-probes", args)

    def test_invalid_serial_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            h = FlashHelperHarness(td, serial="bad serial/with/slashes")
            r = h.run()
            self.assertNotEqual(0, r.returncode)
            self.assertIn("Invalid FW_HIL_SOURCE_JLINK_SERIAL", r.stderr)
            self.assertFalse(
                os.path.exists(h.argv_file), "openocd must not run on invalid serial"
            )

    def test_receiver_resolved_serial_bypasses_auto_detection(self):
        with tempfile.TemporaryDirectory() as td:
            h = FlashHelperHarness(td)
            for rel in (
                "build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex",
                "build/nrf54l15/flpr/zephyr/zephyr.hex",
            ):
                path = os.path.join(h.repo, rel)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write("hex\\n")
            r = h.run_receiver_flash(
                env_extra={"FW_NRF54L15_PROBE_SERIAL": "RECEIVER-0001"}
            )
            self.assertEqual(0, r.returncode, r.stderr)
            args = _nul_args(h.argv_file)
            self.assertIn("adapter serial RECEIVER-0001", args)
            self.assertIn("runner-resolved nRF54L15 identity", r.stdout)

    def test_receiver_invalid_resolved_serial_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            h = FlashHelperHarness(td)
            for rel in (
                "build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex",
                "build/nrf54l15/flpr/zephyr/zephyr.hex",
            ):
                path = os.path.join(h.repo, rel)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write("hex\\n")
            r = h.run_receiver_flash(
                env_extra={"FW_NRF54L15_PROBE_SERIAL": "bad serial/with/slashes"}
            )
            self.assertNotEqual(0, r.returncode)
            self.assertIn("Invalid FW_NRF54L15_PROBE_SERIAL", r.stderr)
            self.assertFalse(os.path.exists(h.argv_file))

    def test_receiver_artifact_overrides_are_pairwise_safe_and_preserve_order(self):
        with tempfile.TemporaryDirectory() as td:
            h = FlashHelperHarness(td)
            stage = os.path.join(td, "stage")
            os.makedirs(stage)
            cpu = os.path.join(stage, "cpuapp.hex")
            flpr = os.path.join(stage, "flpr.hex")
            for path, contents in ((cpu, "cpu\n"), (flpr, "flpr\n")):
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write(contents)
            r = h.run_receiver_flash(
                env_extra={
                    "FW_NRF54L15_CPUAPP_HEX": cpu,
                    "FW_NRF54L15_FLPR_HEX": flpr,
                    "FW_NRF54L15_PROBE_SERIAL": "RECEIVER-0001",
                }
            )
            self.assertEqual(r.returncode, 0, r.stderr)
            args = _nul_args(h.argv_file)
            self.assertLess(
                args.index("nrf54l-load %s" % cpu), args.index("nrf54l-load %s" % flpr)
            )
            self.assertIn("verify_image %s" % cpu, args)
            self.assertIn("verify_image %s" % flpr, args)

            os.unlink(h.argv_file)
            r = h.run_receiver_flash(env_extra={"FW_NRF54L15_CPUAPP_HEX": cpu})
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("must be supplied together", r.stderr)
            self.assertFalse(os.path.exists(h.argv_file))

            os.unlink(h.argv_file) if os.path.exists(h.argv_file) else None
            inside_repo = os.path.join(h.repo, "bad.hex")
            with open(inside_repo, "w", encoding="utf-8") as fh:
                fh.write("bad\n")
            r = h.run_receiver_flash(
                env_extra={
                    "FW_NRF54L15_CPUAPP_HEX": inside_repo,
                    "FW_NRF54L15_FLPR_HEX": flpr,
                }
            )
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("outside repository", r.stderr)
            self.assertFalse(os.path.exists(h.argv_file))

    def test_source_artifact_overrides_are_pairwise_safe_and_preserve_order(self):
        with tempfile.TemporaryDirectory() as td:
            h = FlashHelperHarness(td)
            stage = os.path.join(td, "stage")
            os.makedirs(stage)
            app = os.path.join(stage, "cpuapp.hex")
            net = os.path.join(stage, "cpunet.hex")
            for path, contents in ((app, "app\n"), (net, "net\n")):
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write(contents)
            r = h.run(
                env_extra={
                    "FW_HIL_SOURCE_CPUAPP_HEX": app,
                    "FW_HIL_SOURCE_CPUNET_HEX": net,
                }
            )
            self.assertEqual(r.returncode, 0, r.stderr)
            args = _nul_args(h.argv_file)
            self.assertLess(
                args.index("program %s verify" % net),
                args.index("program %s verify" % app),
            )

            os.unlink(h.argv_file)
            r = h.run(env_extra={"FW_HIL_SOURCE_CPUAPP_HEX": app})
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("must be supplied together", r.stderr)
            self.assertFalse(os.path.exists(h.argv_file))

    def test_missing_serial_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            h = FlashHelperHarness(td, serial="")
            r = h.run()
            self.assertNotEqual(0, r.returncode)
            self.assertIn("required", r.stderr)

    def test_missing_artifacts_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            h = FlashHelperHarness(td, artifacts=False)
            r = h.run()
            self.assertNotEqual(0, r.returncode)
            self.assertIn("No build artifacts", r.stderr)
            self.assertFalse(os.path.exists(h.argv_file))

    def test_missing_dev_shell_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            h = FlashHelperHarness(td)
            env = dict(h.env)
            env.pop("ZEPHYR_BASE", None)
            # Prepend an empty bin dir (no west) but keep bash resolvable.
            env["PATH"] = os.path.join(td, "emptybin") + os.pathsep + env["PATH"]
            os.makedirs(os.path.join(td, "emptybin"), exist_ok=True)
            env["FW_HIL_SOURCE_JLINK_SERIAL"] = h.serial
            r = subprocess.run(
                [os.path.join(h.repo, "scripts", "bin", "fw-flash-hil-source")],
                env=env,
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertNotEqual(0, r.returncode)
            self.assertIn("firmware tool error", r.stderr)
            self.assertFalse(os.path.exists(h.argv_file))

    def test_openocd_nonzero_propagated(self):
        with tempfile.TemporaryDirectory() as td:
            h = FlashHelperHarness(td)
            r = h.run(status_env=3)
            self.assertEqual(3, r.returncode, "openocd exit status propagates")
            self.assertTrue(os.path.exists(h.argv_file))


# ── runner: passing row ────────────────────────────────────────────


class TestRunnerPassingRow(unittest.TestCase):
    @staticmethod
    def _source_commands(wire):
        return [
            json.loads(write.decode("utf-8")[4:])["command"] for write in wire.writes
        ]

    def test_default_direct_run_sends_no_hci_trace_commands(self):
        receiver_wire = _receiver_passing_wire(RUN_ID)
        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td, receiver_wire=receiver_wire
            )
            self.assertEqual(result[0], "passed", result)
            commands = [write.decode("utf-8").strip() for write in receiver_wire.writes]
            self.assertNotIn("log enable dbg bt_hci_core bt_sdc_hci_driver", commands)
            self.assertNotIn("log status", commands)
            self.assertFalse(
                os.path.exists(
                    os.path.join(out_root, run_id, "hci-remove-iso-path-trace.json")
                )
            )
            with open(
                os.path.join(out_root, run_id, "summary.json"), encoding="utf-8"
            ) as fh:
                summary = json.load(fh)
            self.assertNotIn("hci_remove_iso_path_trace", summary)

    def test_opt_in_hci_trace_uses_raw_callback_evidence_after_source_teardown(self):
        receiver_wire = _receiver_passing_wire(RUN_ID, hci_trace=True)
        source_transcript = hil_fakes.build_passing_source_wire(RUN_ID)
        source_chunks, source_writes = source_transcript.build()
        source_wire = hil_fakes.Wire(
            "source", chunks=source_chunks, assert_writes=source_writes
        )

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                hci_remove_iso_path_trace=True,
            )
            self.assertEqual(result[0], "passed", result)
            receiver_commands = [
                write.decode("utf-8").strip() for write in receiver_wire.writes
            ]
            self.assertNotIn(
                "log enable dbg bt_hci_core bt_sdc_hci_driver", receiver_commands
            )
            self.assertNotIn("log status", receiver_commands)
            source_commands = self._source_commands(source_wire)
            self.assertIn("configure", source_commands)
            self.assertIn("start", source_commands)
            self.assertEqual(source_commands[-1], "idle")

            run_dir = os.path.join(out_root, run_id)
            with open(
                os.path.join(run_dir, "hci-remove-iso-path-trace.json"),
                encoding="utf-8",
            ) as fh:
                trace = json.load(fh)
            self.assertEqual(trace["schema_version"], 1)
            self.assertEqual(trace["validation_errors"], [])
            self.assertEqual(
                trace["dropped_messages"][0]["classification"], "before_arm"
            )
            self.assertEqual(trace["dropped_messages"][0]["count"], 2)
            self.assertEqual(
                trace["dropped_messages"][0]["line"],
                "uart:~$ --- 2 messages dropped ---",
            )
            self.assertEqual(len(trace["arm_markers"]), 1)
            self.assertEqual(len(trace["core_sends"]), 1)
            self.assertEqual(len(trace["driver_command_complete"]), 1)
            self.assertEqual(len(trace["driver_command_status"]), 1)
            self.assertEqual(len(trace["core_completion_done"]), 1)
            with open(os.path.join(run_dir, "summary.json"), encoding="utf-8") as fh:
                summary = json.load(fh)
            self.assertEqual(summary["hci_remove_iso_path_trace"], trace)

    def test_hci_trace_evidence_validation_fails_after_ordinary_row_path(self):
        armed = (
            "<inf> bt_bap: HCI remove ISO path trace armed: core_id=7 "
            "driver_id=9 core_level=4 driver_level=4"
        )
        send = "<dbg> bt_hci_core: Sending command 0x206f (buf 0x123) to driver"
        cases = {
            "missing arm": [send],
            "malformed arm": [
                "HCI remove ISO path trace armed: core_id=bad driver_id=9 "
                "core_level=4 driver_level=4",
                send,
            ],
            "duplicate arm": [armed, armed, send],
            "invalid IDs and levels": [
                "HCI remove ISO path trace armed: core_id=-1 driver_id=9 "
                "core_level=3 driver_level=4",
                send,
            ],
            "arm failure": [
                "HCI remove ISO path trace arm failed: err=-34 core_id=7 "
                "driver_id=9 core_level=3 driver_level=4",
                armed,
                send,
            ],
            "post-arm drop": [armed, "uart:~$ --- 1 messages dropped ---", send],
            "missing core send": [armed],
        }
        for name, trace_lines in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                receiver_wire = _receiver_passing_wire(
                    RUN_ID, hci_trace=True, hci_trace_lines=trace_lines
                )
                source_transcript = hil_fakes.build_passing_source_wire(RUN_ID)
                source_chunks, source_writes = source_transcript.build()
                source_wire = hil_fakes.Wire(
                    "source", chunks=source_chunks, assert_writes=source_writes
                )
                result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                    td,
                    receiver_wire=receiver_wire,
                    source_wire=source_wire,
                    hci_remove_iso_path_trace=True,
                )
                self.assertEqual(result[0], "failed", result)
                self.assertEqual(result[1], "hci remove iso path trace evidence")
                self.assertIn("configure", self._source_commands(source_wire))
                self.assertIn("start", self._source_commands(source_wire))
                receiver_commands = [
                    write.decode("utf-8").strip() for write in receiver_wire.writes
                ]
                self.assertNotIn(
                    "log enable dbg bt_hci_core bt_sdc_hci_driver", receiver_commands
                )
                self.assertNotIn("log status", receiver_commands)
                with open(
                    os.path.join(out_root, run_id, "hci-remove-iso-path-trace.json"),
                    encoding="utf-8",
                ) as fh:
                    trace = json.load(fh)
                self.assertTrue(trace["validation_errors"])
                if name == "post-arm drop":
                    self.assertEqual(trace["parser_errors"], [])
                    self.assertEqual(
                        trace["dropped_messages"][0]["line"],
                        "uart:~$ --- 1 messages dropped ---",
                    )
                    self.assertIn(
                        "dropped messages after arm: 1",
                        trace["validation_errors"],
                    )

    def test_hci_trace_validation_does_not_override_ordinary_log_scan_failure(self):
        trace_lines = [
            "<dbg> bt_hci_core: Sending command 0x206f (buf 0x123) to driver",
            (
                "<inf> bt_bap: HCI remove ISO path trace armed: core_id=7 "
                "driver_id=9 core_level=4 driver_level=4"
            ),
        ]
        receiver_wire = _receiver_passing_wire(
            RUN_ID, hci_trace=True, hci_trace_lines=trace_lines
        )
        receiver_wire.chunks.insert(0, b"<wrn> stale receiver warning\r\n")
        source_transcript = hil_fakes.build_passing_source_wire(RUN_ID)
        source_chunks, source_writes = source_transcript.build()
        source_wire = hil_fakes.Wire(
            "source", chunks=source_chunks, assert_writes=source_writes
        )

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                hci_remove_iso_path_trace=True,
            )

            self.assertEqual(result[0], "failed", result)
            self.assertEqual(result[1], "log scan")
            self.assertIn("configure", self._source_commands(source_wire))
            self.assertIn("start", self._source_commands(source_wire))
            with open(
                os.path.join(out_root, run_id, "hci-remove-iso-path-trace.json"),
                encoding="utf-8",
            ) as fh:
                trace = json.load(fh)
            self.assertIn(
                "missing post-arm bt_hci_core 0x206f send record",
                trace["validation_errors"],
            )

    def test_hci_trace_arm_failure_warning_uses_trace_boundary(self):
        arm_failure = (
            "<err> bt_bap: HCI remove ISO path trace arm failed: err=-34 "
            "core_id=7 driver_id=9 core_level=3 driver_level=4"
        )
        receiver_wire = _receiver_passing_wire(
            RUN_ID, hci_trace=True, hci_trace_lines=[arm_failure]
        )
        source_transcript = hil_fakes.build_passing_source_wire(RUN_ID)
        source_chunks, source_writes = source_transcript.build()
        source_wire = hil_fakes.Wire(
            "source", chunks=source_chunks, assert_writes=source_writes
        )

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                hci_remove_iso_path_trace=True,
            )

            self.assertEqual(result[0], "failed", result)
            self.assertEqual(result[1], "hci remove iso path trace evidence")
            self.assertIn("configure", self._source_commands(source_wire))
            self.assertIn("start", self._source_commands(source_wire))
            with open(
                os.path.join(out_root, run_id, "hci-remove-iso-path-trace.json"),
                encoding="utf-8",
            ) as fh:
                trace = json.load(fh)
            self.assertEqual(len(trace["arm_failure_markers"]), 1)
            self.assertEqual(trace["arm_failure_markers"][0]["line"], arm_failure)
            self.assertIn("arm-failure marker present", trace["validation_errors"])
            self.assertTrue(trace["validation_errors"])

    def test_opt_in_sdc_trace_writes_separate_artifact_and_summary(self):
        receiver_wire = _receiver_passing_wire(RUN_ID, sdc_trace=True)
        source_transcript = hil_fakes.build_passing_source_wire(RUN_ID)
        source_chunks, source_writes = source_transcript.build()
        source_wire = hil_fakes.Wire(
            "source", chunks=source_chunks, assert_writes=source_writes
        )

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                sdc_hci_remove_iso_path_trace=True,
            )

            self.assertEqual(result[0], "passed", result)
            run_dir = os.path.join(out_root, run_id)
            self.assertFalse(
                os.path.exists(os.path.join(run_dir, "hci-remove-iso-path-trace.json"))
            )
            with open(
                os.path.join(run_dir, "sdc-hci-remove-iso-path-trace.json"),
                encoding="utf-8",
            ) as fh:
                trace = json.load(fh)
            self.assertEqual(trace["schema_version"], 19)
            self.assertEqual(trace["validation_errors"], [])
            self.assertEqual(len(trace["cmd_put_entries"]), 1)
            self.assertEqual(len(trace["cmd_put_returns"]), 1)
            self.assertEqual(trace["cmd_put_returns"][0]["status"], 0)
            self.assertEqual(len(trace["sdc_entries"]), 1)
            self.assertEqual(trace["sdc_returns"][0]["status"], 0)
            self.assertEqual(len(trace["lock_release_entries"]), 1)
            self.assertEqual(len(trace["lock_release_returns"]), 1)
            self.assertEqual(len(trace["work_submit_entries"]), 1)
            self.assertEqual(trace["work_submit_returns"][0]["status"], 1)
            self.assertEqual(len(trace["snapshot_schedule_markers"]), 1)
            self.assertEqual(trace["snapshot_schedule_markers"][0]["status"], 1)
            self.assertEqual(len(trace["snapshot_markers"]), 1)
            self.assertEqual(trace["snapshot_markers"][0]["busy"], 0x4)
            self.assertEqual(trace["snapshot_markers"][0]["mpsl_state"], "PENDING")
            self.assertEqual(len(trace["msg_get_entries"]), 1)
            self.assertEqual(len(trace["msg_get_returns"]), 1)
            self.assertEqual(trace["msg_get_returns"][0]["status"], 0)
            self.assertEqual(len(trace["sdc_completions"]), 1)
            self.assertEqual(trace["sdc_completions"][0]["status"], 0)
            with open(os.path.join(run_dir, "summary.json"), encoding="utf-8") as fh:
                summary = json.load(fh)
            self.assertEqual(summary["sdc_hci_remove_iso_path_trace"], trace)
            self.assertNotIn("hci_remove_iso_path_trace", summary)

    def test_sdc_trace_validation_keeps_failed_artifact_and_boundary(self):
        trace_lines = [
            "<inf> bt_bap: SDC LE Remove ISO Data Path trace armed",
            "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path trace entry",
            "<inf> sdc_hci_remove_iso_path_trace: SDC LE Remove ISO Data Path trace return: status=0x00",
        ]
        receiver_wire = _receiver_passing_wire(
            RUN_ID, sdc_trace=True, sdc_trace_lines=trace_lines
        )
        source_transcript = hil_fakes.build_passing_source_wire(RUN_ID)
        source_chunks, source_writes = source_transcript.build()
        source_wire = hil_fakes.Wire(
            "source", chunks=source_chunks, assert_writes=source_writes
        )

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                sdc_hci_remove_iso_path_trace=True,
            )

            self.assertEqual(result[0], "failed", result)
            self.assertEqual(result[1], "sdc hci remove iso path trace evidence")
            with open(
                os.path.join(out_root, run_id, "sdc-hci-remove-iso-path-trace.json"),
                encoding="utf-8",
            ) as fh:
                trace = json.load(fh)
            self.assertIn("missing post-arm SDC completion", trace["validation_errors"])
            self.assertFalse(
                os.path.exists(
                    os.path.join(out_root, run_id, "hci-remove-iso-path-trace.json")
                )
            )

    def test_environment_records_local_ncs_version(self):
        with tempfile.TemporaryDirectory() as td:
            ncs = os.path.join(td, "ncs")
            os.makedirs(os.path.join(ncs, "nrf"))
            with open(os.path.join(ncs, "nrf", "VERSION"), "w", encoding="utf-8") as fh:
                fh.write("3.3.0\n")
            old = os.environ.get("NCS_BASE")
            os.environ["NCS_BASE"] = ncs
            try:
                captured = evidence.capture_environment(td, ["hil-runner.py"], 0)
            finally:
                if old is None:
                    os.environ.pop("NCS_BASE", None)
                else:
                    os.environ["NCS_BASE"] = old
            self.assertEqual(captured["ncs"]["path"], ncs)
            self.assertEqual(captured["ncs"]["version"], "3.3.0")

    def test_runner_rejects_console_decode_fault(self):
        with tempfile.TemporaryDirectory() as td:
            wire = _receiver_passing_wire(RUN_ID)
            wire.chunks.insert(0, b"\xff\xfe invalid receiver bytes\r\n")
            result, _out, _run_id, _junit, _fixture, _binding, _engine = _run_harness(
                td, receiver_wire=wire
            )
            self.assertEqual(result[0], "failed")
            self.assertIn("serial", result[1])

    def test_runner_clean_row_ignores_expected_close_race(self):
        with tempfile.TemporaryDirectory() as td:
            receiver_wire = _receiver_passing_wire(RUN_ID)
            race_serial = {}

            def arm_close_race(engine):
                original_factory = engine.deps.serial_factory

                def factory(role, path, baud, evidence_path, dtr, rts):
                    console = original_factory(
                        role, path, baud, evidence_path, dtr, rts
                    )
                    if role == "receiver":
                        fake_ser = _BlockedReadSerial(
                            receiver_wire,
                            wait_timeout=serial_io.READ_TIMEOUT * 2,
                        )
                        console._serial_class = lambda fake_ser=fake_ser: fake_ser
                        race_serial["fake"] = fake_ser
                    return console

                engine.deps.serial_factory = factory
                original_step = engine._step_run_row

                def run_row(*args, **kwargs):
                    result = original_step(*args, **kwargs)
                    fake_ser = race_serial["fake"]
                    fake_ser.arm_race()
                    if not fake_ser.read_started.wait(2.0):
                        raise AssertionError("close-race reader never became blocked")
                    return result

                engine._step_run_row = run_row

            result, _out, _run_id, _junit, _fixture, _binding, engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                engine_setup=arm_close_race,
            )
            self.assertEqual(result[0], "passed", result)
            self.assertFalse(race_serial["fake"].close_during_read)
            self.assertIsNone(engine._receiver_console.read_error())

    def test_runner_rejects_shell_error_in_raw_log(self):
        with tempfile.TemporaryDirectory() as td:
            wire = _receiver_passing_wire(RUN_ID)
            wire.chunks.insert(0, b"error: stale shell failure\r\n")
            result, _out, _run_id, _junit, _fixture, _binding, _engine = _run_harness(
                td, receiver_wire=wire
            )
            self.assertEqual(result[0], "failed")
            self.assertIn("log scan", result[1])

    def test_healthy_row_keeps_hang_recovery_warning_as_log_failure(self):
        with tempfile.TemporaryDirectory() as td:
            wire = _receiver_passing_wire(RUN_ID)
            wire.chunks.insert(0, HANG_RECOVERY_WARNING.encode("utf-8") + b"\r\n")
            result, _out, _run_id, _junit, _fixture, _binding, _engine = _run_harness(
                td, receiver_wire=wire
            )
            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "log scan")

    def test_runner_rejects_invalid_trailing_receiver_raw_bytes(self):
        # A byte after final newline/prompt cannot enter SerialConsole's
        # complete-line parser. Raw-log reconciliation must still fail rather
        # than silently replacing invalid UTF-8 after reader cleanup.
        with tempfile.TemporaryDirectory() as td:

            def inject_after_close(engine):
                original_factory = engine.deps.serial_factory

                def factory(role, path, baud, evidence_path, dtr, rts):
                    console = original_factory(
                        role, path, baud, evidence_path, dtr, rts
                    )
                    if role == "receiver":
                        original_close = console.close

                        def close_with_invalid_tail():
                            original_close()
                            with open(evidence_path, "ab") as fh:
                                fh.write(b"\xff")

                        console.close = close_with_invalid_tail
                    return console

                engine.deps.serial_factory = factory

            result, _out, _run_id, _junit, _fixture, _binding, _engine = _run_harness(
                td, engine_setup=inject_after_close
            )
            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "log scan")

    def test_runner_rejects_invalid_trailing_receiver_raw_bytes(self):
        # A byte after the final newline/prompt cannot enter SerialConsole's
        # complete-line parser. Raw-log reconciliation must still fail rather
        # than silently replacing invalid UTF-8 after reader cleanup.
        with tempfile.TemporaryDirectory() as td:

            def inject_after_close(engine):
                original_factory = engine.deps.serial_factory

                def factory(role, path, baud, evidence_path, dtr, rts):
                    console = original_factory(
                        role, path, baud, evidence_path, dtr, rts
                    )
                    if role == "receiver":
                        original_close = console.close

                        def close_with_invalid_tail():
                            original_close()
                            with open(evidence_path, "ab") as fh:
                                fh.write(b"\xff")

                        console.close = close_with_invalid_tail
                    return console

                engine.deps.serial_factory = factory

            result, _out, _run_id, _junit, _fixture, _binding, _engine = _run_harness(
                td, engine_setup=inject_after_close
            )
            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "log scan")

    def test_runner_rejects_late_unconsumed_source_diagnostic(self):
        # The source reader can receive a fatal boot/parse diagnostic after
        # normal response records have already satisfied the protocol client.
        # Raw-log reconciliation must still fail the public row instead of
        # leaving that HIL1 record unread in the console queue.
        with tempfile.TemporaryDirectory() as td:
            t = hil_fakes.build_passing_source_wire(RUN_ID)
            chunks, writes = t.build()
            diagnostic = hil_fakes.hil_line(
                hil_fakes.source_record(
                    "status",
                    "boot",
                    "boot",
                    999,
                    0,
                    {"command": "status", "ok": False, "error": "failed"},
                )
            )
            source_wire = hil_fakes.Wire(
                "source", chunks=chunks + [diagnostic], assert_writes=writes
            )
            result, _out, _run_id, _junit, _fixture, _binding, _engine = _run_harness(
                td, source_wire=source_wire
            )
            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "log scan")

    def test_passing_row_emits_all_evidence_and_no_leaks(self):
        with tempfile.TemporaryDirectory() as td:
            ledger = []
            result, out_root, run_id, junit, _f, _b, engine = _run_harness(
                td, ledger=ledger
            )
            outcome, boundary, cleanup = result
            self.assertEqual(
                outcome, "passed", "boundary=%s cleanup=%s" % (boundary, cleanup)
            )
            run_dir = os.path.join(out_root, run_id)
            for name in (
                "fixture.json",
                "binding.json",
                "environment.json",
                "identity.json",
                "nrf-probes.txt",
                "nrf-probes-find.txt",
                "receiver-udev.txt",
                "source-udev.txt",
                "source-probe-udev.txt",
                "source-jlink.txt",
                "images.json",
                "source-flash.log",
                "receiver-flash.log",
                "source-console.bin",
                "receiver-console.bin",
                "source-console-tx.bin",
                "receiver-console-tx.bin",
                "source-records.jsonl",
                "receiver-status.txt",
                "receiver-active-status.txt",
                "receiver-post-stop-status.txt",
                "commands.jsonl",
                "result.json",
                "junit.xml",
                "MANIFEST.md",
                "SHA256SUMS",
                "summary.json",
            ):
                self.assertTrue(os.path.isfile(os.path.join(run_dir, name)), name)
            with open(os.path.join(run_dir, "result.json")) as fh:
                result_json = json.load(fh)
            self.assertEqual(result_json["outcome"], "passed")
            with open(os.path.join(run_dir, "MANIFEST.md")) as fh:
                manifest = fh.read()
            self.assertIn("outcome: passed", manifest)
            self.assertIn("  source-console-tx.bin", manifest)
            self.assertIn("  receiver-console-tx.bin", manifest)
            self.assertNotIn("TRANSPORT_RUNTIME_ACCEPTED", manifest)
            with open(os.path.join(run_dir, "junit.xml")) as fh:
                junit_text = fh.read()
            self.assertIn(
                "testsuites" if "<testsuites" in junit_text else "testsuite", junit_text
            )
            self.assertNotIn("<failure", junit_text)
            self.assertIn("rh2.short_mono_48_4_1", junit_text)
            with open(os.path.join(run_dir, "SHA256SUMS")) as fh:
                sums = fh.read()
            self.assertIn("  junit.xml", sums)
            self.assertIn("  result.json", sums)
            self.assertIn("  commands.jsonl", sums)
            self.assertIn("  source-records.jsonl", sums)
            self.assertIn("  source-console-tx.bin", sums)
            self.assertIn("  receiver-console-tx.bin", sums)
            with open(os.path.join(run_dir, "images.json")) as fh:
                images = json.load(fh)["images"]
            self.assertEqual(len(images), 4)
            self.assertEqual(
                [img["logical_image"] for img in images],
                ["source-app", "source-cpunet", "receiver-cpuapp", "receiver-flpr"],
            )
            # Lock released, console threads joined, no lingering state.
            self.assertEqual(os.listdir(os.path.join(out_root, ".locks")), [])
            self.assertFalse(engine._run_dir is None)
            # commands.jsonl: lsof + both flash helpers, in order.
            with open(os.path.join(run_dir, "commands.jsonl")) as fh:
                commands = [json.loads(line) for line in fh if line.strip()]
            argv0 = [c["argv"][0] for c in commands]
            self.assertIn("lsof", argv0)
            self.assertIn("fw-flash-hil-source", argv0)
            self.assertIn("fw-flash-54l15", argv0)
            source_flash = next(
                command
                for command in commands
                if command["argv"] == ["fw-flash-hil-source"]
            )
            receiver_flash = next(
                command for command in commands if command["argv"] == ["fw-flash-54l15"]
            )
            self.assertEqual(
                source_flash["env"],
                {"FW_HIL_SOURCE_JLINK_SERIAL": hil_fakes.SOURCE_SERIAL},
            )
            self.assertEqual(
                receiver_flash["env"],
                {"FW_NRF54L15_PROBE_SERIAL": hil_fakes.RECEIVER_SERIAL},
            )
            with open(os.path.join(run_dir, "commands.jsonl")) as fh:
                self.assertEqual(sum(1 for _ in fh), len(commands))

    def test_extended_stream_summary_is_retained_in_summary_json(self):
        receiver_wire = _receiver_passing_wire(RUN_ID)
        receiver_wire.chunks[-5] = hil_fakes.receiver_stream_summary_line(
            rx_valid=700,
            rx_error=0,
            rx_lost=0,
            rx_unknown=0,
            rx_no_ts=0,
        ).encode("utf-8")

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td, receiver_wire=receiver_wire
            )
            self.assertEqual(result[0], "passed", result)
            with open(
                os.path.join(out_root, run_id, "summary.json"), encoding="utf-8"
            ) as fh:
                summary = json.load(fh)
            last = summary["receiver_streams"][0]["last"]
            self.assertEqual(
                {
                    key: last[key]
                    for key in (
                        "rx_valid",
                        "rx_error",
                        "rx_lost",
                        "rx_unknown",
                        "rx_no_ts",
                    )
                },
                {
                    "rx_valid": 700,
                    "rx_error": 0,
                    "rx_lost": 0,
                    "rx_unknown": 0,
                    "rx_no_ts": 0,
                },
            )

    def test_mode_a_retains_complete_structured_stream_summaries(self):
        # Healthy-above-floor values so the row passes the frozen transport
        # limits while retaining exact structured fields (mode_a 48_4_1
        # expected submitted per segment = 12644, floor 11380).
        row = rows.RH3_HEALTHY_ROWS[1]
        summaries = [
            hil_fakes.receiver_stream_summary_line(
                slot=0,
                sdus=11900,
                decoded=11950,
                plc=150,
                rx_valid=11900,
                rx_error=0,
                rx_lost=14300,
                rx_unknown=0,
                rx_no_ts=50,
            ),
            hil_fakes.receiver_stream_summary_line(
                slot=1,
                sdus=11901,
                decoded=11951,
                plc=151,
                rx_valid=11901,
                rx_error=0,
                rx_lost=14390,
                rx_unknown=0,
                rx_no_ts=14390,
            ),
        ]
        receiver_wire = _receiver_passing_wire(RUN_ID, summary_lines=summaries)
        source_wire = _source_wire_for_row(RUN_ID, row)

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )
            self.assertEqual(result[0], "passed", result)
            with open(
                os.path.join(out_root, run_id, "summary.json"), encoding="utf-8"
            ) as fh:
                summary = json.load(fh)
            streams = summary["receiver_streams"][0]["streams"]
            self.assertEqual([stream["slot"] for stream in streams], [0, 1])
            self.assertEqual(
                [
                    {
                        key: stream[key]
                        for key in (
                            "rx_valid",
                            "rx_error",
                            "rx_lost",
                            "rx_unknown",
                            "rx_no_ts",
                        )
                    }
                    for stream in streams
                ],
                [
                    {
                        "rx_valid": 11900,
                        "rx_error": 0,
                        "rx_lost": 14300,
                        "rx_unknown": 0,
                        "rx_no_ts": 50,
                    },
                    {
                        "rx_valid": 11901,
                        "rx_error": 0,
                        "rx_lost": 14390,
                        "rx_unknown": 0,
                        "rx_no_ts": 14390,
                    },
                ],
            )
            self.assertEqual(summary["receiver_streams"][0]["last"]["slot"], 1)

    def test_mode_a_retains_two_iso_link_quality_snapshots_with_nonzero_evidence(self):
        row = rows.RH3_HEALTHY_ROWS[1]
        quality_records = [
            {
                "slot": 0,
                "handle": 0x1234,
                "crc_error": 5,
                "rx_unreceived": 6,
                "iso_interval_1250us": 8,
                "nse": 1,
                "cig_sync_us": 1000,
                "cis_sync_us": 1100,
                "c_max_pdu": 120,
                "c_phy": 2,
                "c_bn": 1,
                "c_flush_1250us": 32,
            },
            {
                "slot": 1,
                "handle": 0xABCD,
                "crc_error": 7,
                "rx_unreceived": 8,
                "iso_interval_1250us": 8,
                "nse": 2,
                "cig_sync_us": 1200,
                "cis_sync_us": 1300,
                "c_max_pdu": 240,
                "c_phy": 2,
                "c_bn": 2,
                "c_flush_1250us": 40,
            },
        ]
        receiver_wire = _receiver_passing_wire(
            RUN_ID,
            summary_lines=[
                hil_fakes.receiver_stream_summary_line(slot=0),
                hil_fakes.receiver_stream_summary_line(slot=1),
            ],
            iso_link_quality_records=quality_records,
        )
        source_wire = _source_wire_for_row(RUN_ID, row)

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )
            self.assertEqual(result[0], "passed", result)
            with open(
                os.path.join(out_root, run_id, "summary.json"), encoding="utf-8"
            ) as fh:
                summary = json.load(fh)
            quality = summary["receiver_tail"][0]["receiver"]["iso_link_quality"]
            self.assertTrue(quality["header_seen"])
            self.assertEqual([record["slot"] for record in quality["streams"]], [0, 1])
            self.assertEqual(
                [record["handle"] for record in quality["streams"]], [0x1234, 0xABCD]
            )
            self.assertEqual(
                [record["crc_error"] for record in quality["streams"]], [5, 7]
            )
            self.assertEqual(
                [record["rx_unreceived"] for record in quality["streams"]], [6, 8]
            )
            self.assertEqual(
                [record["iso_interval_1250us"] for record in quality["streams"]], [8, 8]
            )
            self.assertEqual(
                [record["c_max_pdu"] for record in quality["streams"]], [120, 240]
            )
            self.assertEqual(
                [record["c_flush_1250us"] for record in quality["streams"]], [32, 40]
            )
            receiver_commands = [
                write.decode("utf-8").strip() for write in receiver_wire.writes
            ]
            active_offload = receiver_commands.index("flpr offload")
            iso_quality = receiver_commands.index("bt iso quality")
            post_stop_offload = len(receiver_commands) - 2
            self.assertLess(
                active_offload,
                iso_quality,
            )
            self.assertLess(
                iso_quality,
                receiver_commands.index("audio status"),
            )
            self.assertLess(receiver_commands.index("audio status"), post_stop_offload)
            with open(
                os.path.join(out_root, run_id, "receiver-status.txt"), encoding="utf-8"
            ) as fh:
                evidence_text = fh.read()
            self.assertIn("# bt iso quality", evidence_text)
            self.assertNotIn("# flpr offload", evidence_text)
            with open(
                os.path.join(out_root, run_id, "receiver-active-status.txt"),
                encoding="utf-8",
            ) as fh:
                active_evidence = fh.read()
            self.assertIn("# flpr offload (active poll 1)", active_evidence)
            with open(
                os.path.join(out_root, run_id, "receiver-post-stop-status.txt"),
                encoding="utf-8",
            ) as fh:
                post_stop_evidence = fh.read()
            self.assertIn("# audio status", post_stop_evidence)
            self.assertIn("# flpr offload", post_stop_evidence)

    def test_mode_a_structured_evidence_follows_active_tail_post_stop_lifecycle(self):
        row = rows.RH3_HEALTHY_ROWS[1]
        receiver_wire = _receiver_passing_wire(
            RUN_ID,
            summary_lines=[
                hil_fakes.receiver_stream_summary_line(slot=0),
                hil_fakes.receiver_stream_summary_line(slot=1),
            ],
        )
        source_wire = _source_wire_for_row(RUN_ID, row)

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )
            self.assertEqual(result[0], "passed", result)
            with open(
                os.path.join(out_root, run_id, "summary.json"), encoding="utf-8"
            ) as fh:
                summary = json.load(fh)

            active = summary["source_active"][0]["receiver_offload"]
            self.assertEqual(active["offload"]["state"], "ACTIVE")
            iso = summary["receiver_tail"][0]["receiver"]["iso_link_quality"]
            self.assertEqual([record["slot"] for record in iso["streams"]], [0, 1])
            self.assertEqual(
                [(record["nse"], record["c_phy"]) for record in iso["streams"]],
                [(1, 2), (1, 2)],
            )
            self.assertEqual(
                [record["c_flush_1250us"] for record in iso["streams"]], [32, 32]
            )
            post_stop = summary["receiver_streams"][0]["post_stop"]
            self.assertEqual(post_stop["offload"]["state"], "STOPPED")
            self.assertEqual(
                post_stop["offload"]["submit"], post_stop["offload"]["success"]
            )
            self.assertGreater(
                post_stop["offload"]["success"], active["offload"]["success"]
            )

            commands = [write.decode("utf-8").strip() for write in receiver_wire.writes]
            self.assertLess(
                commands.index("flpr offload"), commands.index("bt iso quality")
            )
            self.assertLess(
                commands.index("bt iso quality"), commands.index("audio status")
            )
            self.assertLess(
                commands.index("audio status"),
                max(
                    index
                    for index, command in enumerate(commands)
                    if command == "flpr offload"
                ),
            )
            active_evidence = os.path.join(
                out_root, run_id, "receiver-active-status.txt"
            )
            tail_evidence = os.path.join(out_root, run_id, "receiver-status.txt")
            post_evidence = os.path.join(
                out_root, run_id, "receiver-post-stop-status.txt"
            )
            with open(active_evidence, encoding="utf-8") as fh:
                active_text = fh.read()
            with open(tail_evidence, encoding="utf-8") as fh:
                tail_text = fh.read()
            with open(post_evidence, encoding="utf-8") as fh:
                post_text = fh.read()
            self.assertIn("# flpr offload", active_text)
            self.assertIn("# bt iso quality", tail_text)
            self.assertNotIn("# flpr offload", tail_text)
            self.assertIn("# audio status", post_text)
            self.assertIn("# flpr offload", post_text)

    def test_mode_a_truncated_iso_link_quality_field_fails_receiver_tail(self):
        row = rows.RH3_HEALTHY_ROWS[1]
        receiver_wire = _receiver_passing_wire(
            RUN_ID,
            summary_lines=[
                hil_fakes.receiver_stream_summary_line(slot=0),
                hil_fakes.receiver_stream_summary_line(slot=1),
            ],
            iso_link_quality_records=[{"slot": 0}, {"slot": 1}],
        )
        quality_index = next(
            index
            for index, chunk in enumerate(receiver_wire.chunks)
            if b"bt iso quality" in chunk
        )
        receiver_wire.chunks[quality_index] = receiver_wire.chunks[
            quality_index
        ].replace(b" c_flush_1250us=32", b"", 1)
        source_wire = _source_wire_for_row(RUN_ID, row)

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )
            self.assertEqual(result[0], "failed", result)
            self.assertEqual(result[1], "receiver tail")
            with open(
                os.path.join(out_root, run_id, "result.json"), encoding="utf-8"
            ) as fh:
                result_json = json.load(fh)
            self.assertIn(
                "ISO link quality grammar malformed",
                result_json["failure_detail"],
            )

    def test_mode_a_later_stream_summary_fault_fails_session_end(self):
        row = rows.RH3_HEALTHY_ROWS[1]
        receiver_wire = _receiver_passing_wire(
            RUN_ID,
            summary_lines=[
                hil_fakes.receiver_stream_summary_line(slot=0),
                hil_fakes.receiver_stream_summary_line(slot=1, decode_err=1),
            ],
        )
        source_wire = _source_wire_for_row(RUN_ID, row)

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )
            self.assertEqual(result[0], "failed", result)
            self.assertEqual(result[1], "session end")
            with open(
                os.path.join(out_root, run_id, "result.json"), encoding="utf-8"
            ) as fh:
                result_json = json.load(fh)
            self.assertIn("slot 1", result_json["failure_detail"])
            self.assertIn("decode_err=1", result_json["failure_detail"])

    def test_mode_a_duplicate_stream_summary_slot_fails_session_end(self):
        row = rows.RH3_HEALTHY_ROWS[1]
        receiver_wire = _receiver_passing_wire(
            RUN_ID,
            summary_lines=[
                hil_fakes.receiver_stream_summary_line(slot=0),
                hil_fakes.receiver_stream_summary_line(slot=0, sdus=151),
            ],
        )
        source_wire = _source_wire_for_row(RUN_ID, row)

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )
            self.assertEqual(result[0], "failed", result)
            self.assertEqual(result[1], "session end")
            with open(
                os.path.join(out_root, run_id, "result.json"), encoding="utf-8"
            ) as fh:
                result_json = json.load(fh)
            self.assertIn("duplicate", result_json["failure_detail"])
            self.assertIn("slot 0", result_json["failure_detail"])

    def test_healthy_10ms_active_capture_settles_one_live_flpr_submit(self):
        row = rows.RH3_HEALTHY_ROWS[0]
        transient = hil_fakes.flpr_offload_transcript(submit=151, success=150)
        settled = hil_fakes.flpr_offload_transcript(submit=151, success=151)
        receiver_wire = _receiver_passing_wire(
            RUN_ID, offload_snapshots=[transient, settled]
        )
        source_wire = _source_wire_for_row(RUN_ID, row)
        clock = ControlledClock()

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
                clock=clock,
                sleep=clock.sleep,
            )

            self.assertEqual(result[0], "passed", result)
            receiver_commands = [
                write.decode("utf-8").strip() for write in receiver_wire.writes
            ]
            self.assertEqual(
                receiver_commands[-5:],
                [
                    "bt iso quality",
                    "audio status",
                    "audio perf",
                    "flpr offload",
                    "flpr status",
                ],
            )
            run_dir = os.path.join(out_root, run_id)
            with open(
                os.path.join(run_dir, "receiver-active-status.txt"), encoding="utf-8"
            ) as fh:
                evidence_text = fh.read()
            self.assertIn("# flpr offload (settle retry 1)", evidence_text)
            transient_text = "submit=151 success=150"
            settled_text = "submit=151 success=151"
            self.assertLess(
                evidence_text.index(transient_text), evidence_text.index(settled_text)
            )
            self.assertEqual(evidence_text.count(transient_text), 1)
            self.assertEqual(evidence_text.count(settled_text), 1)
            with open(os.path.join(run_dir, "summary.json"), encoding="utf-8") as fh:
                summary = json.load(fh)
            offload = summary["source_active"][0]["receiver_offload"]["offload"]
            self.assertEqual(offload["submit"], 151)
            self.assertEqual(offload["success"], 151)
            self.assertEqual(
                summary["source_active"][0]["receiver_offload"]["offload_settle"][
                    "outcome"
                ],
                "equal",
            )

    def test_healthy_10ms_active_capture_moving_single_pending_passes_with_progress_proof(
        self,
    ):
        row = rows.RH3_HEALTHY_ROWS[0]
        initial = hil_fakes.flpr_offload_transcript(submit=151, success=150)
        moving = hil_fakes.flpr_offload_transcript(submit=181, success=180)
        receiver_wire = _receiver_passing_wire(
            RUN_ID, offload_snapshots=[initial, moving]
        )
        source_wire = _source_wire_for_row(RUN_ID, row)
        clock = ControlledClock()

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
                clock=clock,
                sleep=clock.sleep,
            )

            self.assertEqual(result[0], "passed", result)
            receiver_commands = [
                write.decode("utf-8").strip() for write in receiver_wire.writes
            ]
            self.assertEqual(receiver_commands.count("flpr offload"), 3)
            run_dir = os.path.join(out_root, run_id)
            with open(
                os.path.join(run_dir, "receiver-active-status.txt"), encoding="utf-8"
            ) as fh:
                evidence_text = fh.read()
            self.assertEqual(evidence_text.count("settle retry"), 1)
            with open(os.path.join(run_dir, "summary.json"), encoding="utf-8") as fh:
                summary = json.load(fh)
            receiver_active = summary["source_active"][0]["receiver_offload"]
            self.assertEqual(receiver_active["offload"]["submit"], 181)
            self.assertEqual(receiver_active["offload"]["success"], 180)
            self.assertEqual(
                receiver_active["offload_settle"]["outcome"],
                "moving_single_pending",
            )
            self.assertEqual(receiver_active["offload_settle"]["retries"], 1)
            self.assertEqual(
                receiver_active["offload_settle"]["initial"],
                {"submit": 151, "success": 150},
            )
            self.assertEqual(
                receiver_active["offload_settle"]["final"],
                {"submit": 181, "success": 180},
            )

    def test_healthy_10ms_active_capture_persistent_live_submit_fails_bounded(self):
        row = rows.RH3_HEALTHY_ROWS[0]
        pending = hil_fakes.flpr_offload_transcript(submit=151, success=150)
        retry_capacity = (
            int(
                runner.RECEIVER_OFFLOAD_SETTLE_TIMEOUT
                / runner.RECEIVER_OFFLOAD_SETTLE_POLL_INTERVAL
            )
            + 4
        )
        settle_snapshot_count = (
            int(
                runner.RECEIVER_OFFLOAD_SETTLE_TIMEOUT
                / runner.RECEIVER_OFFLOAD_SETTLE_POLL_INTERVAL
            )
            + 2
        )
        receiver_wire = _receiver_passing_wire(
            RUN_ID, offload_snapshots=[pending] * settle_snapshot_count
        )
        source_wire = _source_wire_for_row(RUN_ID, row)
        clock = ControlledClock()

        with tempfile.TemporaryDirectory() as td:
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
                clock=clock,
                sleep=clock.sleep,
            )

            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "receiver active")
            self.assertGreaterEqual(clock.now, runner.RECEIVER_OFFLOAD_SETTLE_TIMEOUT)
            self.assertTrue(clock.sleeps)
            self.assertTrue(
                all(
                    seconds <= runner.RECEIVER_OFFLOAD_SETTLE_POLL_INTERVAL
                    for seconds in clock.sleeps
                )
            )
            receiver_commands = [
                write.decode("utf-8").strip() for write in receiver_wire.writes
            ]
            self.assertGreater(receiver_commands.count("flpr offload"), 1)
            self.assertLessEqual(
                receiver_commands.count("flpr offload"), retry_capacity + 1
            )
            run_dir = os.path.join(out_root, run_id)
            with open(os.path.join(run_dir, "result.json"), encoding="utf-8") as fh:
                result_json = json.load(fh)
            self.assertIn(
                "offload submit/success mismatch", result_json["failure_detail"]
            )
            with open(
                os.path.join(run_dir, "receiver-active-status.txt"), encoding="utf-8"
            ) as fh:
                evidence_text = fh.read()
            self.assertIn("# flpr offload (settle retry 1)", evidence_text)
            self.assertIn("submit=151 success=150", evidence_text)

    def test_command_execution_exception_is_retained_in_evidence(self):
        with tempfile.TemporaryDirectory() as td:

            class ExplodingRunner:
                def __call__(self, argv, timeout, env=None):
                    del argv, timeout, env
                    raise OSError("injected process launch failure")

            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td, run_cmd=ExplodingRunner()
            )
            self.assertEqual(result[0], "failed")
            with open(os.path.join(out_root, run_id, "commands.jsonl")) as fh:
                commands = [json.loads(line) for line in fh if line.strip()]
            self.assertEqual(commands[0]["argv"], ["lsof", "--", "/dev/ttyACM1"])
            self.assertIsNone(commands[0]["status"])
            self.assertIn("injected process launch failure", commands[0]["error"])

    def test_commands_ledger_failure_releases_fixture_and_consoles(self):
        # commands.jsonl comes after CleanupStack.close(). A failed evidence
        # write changes verdict, but cannot strand fixture lock or serial IO.
        with tempfile.TemporaryDirectory() as td:
            receiver_wire = _receiver_passing_wire(RUN_ID)
            source_t = hil_fakes.build_passing_source_wire(RUN_ID)
            src_chunks, src_writes = source_t.build()
            source_wire = hil_fakes.Wire(
                "source", chunks=src_chunks, assert_writes=src_writes
            )

            def fail_commands_ledger(engine):
                original = engine._write_jsonl

                def fail(name, records):
                    if name == "commands.jsonl":
                        raise OSError("injected commands ledger failure")
                    return original(name, records)

                engine._write_jsonl = fail

            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                engine_setup=fail_commands_ledger,
            )
            outcome, boundary, cleanup = result
            self.assertEqual(outcome, "failed")
            self.assertEqual(boundary, "evidence commands")
            self.assertEqual(cleanup, [])
            self.assertTrue(receiver_wire.closed)
            self.assertTrue(source_wire.closed)
            self.assertEqual(os.listdir(os.path.join(out_root, ".locks")), [])
            with open(os.path.join(out_root, run_id, "result.json")) as fh:
                self.assertEqual(json.load(fh)["outcome"], "failed")

    def test_finalization_failure_returns_failed_and_corrects_result(self):
        # Metadata finalization happens after result/JUnit payload writes. A
        # failure must return nonzero and replace writable payload verdicts
        # with failed, never leave evidence claiming an accepted pass.
        with tempfile.TemporaryDirectory() as td:

            def fail_finalization(engine):
                original = runner.finalize_evidence

                def fail_once(*args, **kwargs):
                    del args, kwargs
                    raise evidence.EvidenceError("injected manifest failure")

                runner.finalize_evidence = fail_once
                real_finalize = engine._finalize

                def restore_after_finalize(*args, **kwargs):
                    try:
                        return real_finalize(*args, **kwargs)
                    finally:
                        runner.finalize_evidence = original

                engine._finalize = restore_after_finalize

            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td, engine_setup=fail_finalization
            )
            self.assertEqual(result[0], "failed")
            self.assertIn("evidence finalization", result[1])
            with open(os.path.join(out_root, run_id, "result.json")) as fh:
                self.assertEqual(json.load(fh)["outcome"], "failed")
            with open(os.path.join(out_root, run_id, "junit.xml")) as fh:
                self.assertIn("<failure", fh.read())

    def test_readers_armed_before_first_flash(self):
        with tempfile.TemporaryDirectory() as td:
            ledger = []
            result, _out, _rid, _j, _f, _b, _e = _run_harness(td, ledger=ledger)
            self.assertEqual(result[0], "passed")
            opens = [e for e in ledger if e.get("event") == "console-open"]
            readies = [e for e in ledger if e.get("event") == "reader-ready"]
            flashes = [
                e
                for e in ledger
                if "argv" in e
                and e["argv"]
                and e["argv"][0] in ("fw-flash-hil-source", "fw-flash-54l15")
            ]
            self.assertEqual(len(opens), 2)
            self.assertEqual(len(readies), 2)
            self.assertEqual(len(flashes), 2)
            all_ready = max(ledger.index(e) for e in readies)
            first_flash = min(ledger.index(e) for e in flashes)
            self.assertLess(
                all_ready,
                first_flash,
                "both consoles must be reader-ready before either flash",
            )

    def test_boot_accepts_async_flpr_markers_after_platform_markers(self):
        # platform_init() logs offload/runtime readiness synchronously. FLPR
        # READY/ACK arrive later over IPC, potentially after advertising, so
        # boot tracks every required marker without imposing a false total
        # order on independent boot paths.
        with tempfile.TemporaryDirectory() as td:
            wire = _receiver_passing_wire(RUN_ID)
            wire.chunks[0] = hil_fakes.receiver_boot_chunks(
                [
                    "BLE ready",
                    "settings_load() OK",
                    "I2S ready",
                    "offload init OK (rings deferred, FLPR not yet ready: -11)",
                    "FLPR runtime init OK: vpr=0x1 src=0x2(3) exec=0x4(5)",
                    'Advertising as "LE Audio Receiver"',
                    "FLPR READY",
                    "FLPR READY_ACK sent",
                ]
            )[0]
            result, _out, _run_id, _junit, _fixture, _binding, _engine = _run_harness(
                td, receiver_wire=wire
            )
            self.assertEqual(result[0], "passed", result[1])

    def test_flpr_ready_ack_cannot_satisfy_ready_marker(self):
        self.assertFalse(
            runner._receiver_boot_marker_matches("FLPR READY", "FLPR READY_ACK sent")
        )

    def test_preflash_boot_banner_cannot_satisfy_fresh_boot(self):
        # A previous boot can leave ordinary marker text in the UART FIFO
        # while consoles are armed. mark_rx() must purge it before receiver
        # reset, leaving only the intentionally incomplete fresh marker set.
        with tempfile.TemporaryDirectory() as td:
            receiver_wire = hil_fakes.Wire(
                "receiver",
                pre_mark_chunks=hil_fakes.receiver_boot_chunks(
                    list(runner.RECEIVER_BOOT_MARKERS)
                ),
                chunks=hil_fakes.receiver_boot_chunks(["BLE ready"]),
            )
            source_t = hil_fakes.build_passing_source_wire(RUN_ID)
            src_chunks, src_writes = source_t.build()
            source_wire = hil_fakes.Wire(
                "source", chunks=src_chunks, assert_writes=src_writes
            )
            result, _out, _run_id, _junit, _fixture, _binding, _engine = _run_harness(
                td, receiver_wire=receiver_wire, source_wire=source_wire
            )
            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "boot")

    def test_receiver_nonzero_bond_count_fails_clean_state(self):
        with tempfile.TemporaryDirectory() as td:
            wire = _receiver_passing_wire(RUN_ID)
            wire.chunks[3] = hil_fakes.receiver_transcript(
                "bt bonds", ["Bond count: 1"]
            )
            result, _out, _run_id, _junit, _fixture, _binding, _engine = _run_harness(
                td, receiver_wire=wire
            )
            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "clean state")
        self.assertTrue(
            runner._receiver_boot_marker_matches(
                "FLPR READY", "FLPR READY (epoch=1, count=1, new)"
            )
        )

    def test_no_hardware_flag_no_probe_or_flash_commands(self):
        # With discovery and serial injected and only lsof/flash scripted,
        # any unexpected command (nrf-probes, udevadm, openocd, btattach)
        # would raise.  The passing row proves no such command executed.
        self.assertNotEqual(os.environ.get("HIL_RUN_HARDWARE"), "1")
        with tempfile.TemporaryDirectory() as td:
            result, _out, _rid, _j, _f, _b, _e = _run_harness(td)
            self.assertEqual(result[0], "passed")


class TestSessionEndRawEvidenceFallback(unittest.TestCase):
    @staticmethod
    def _open_console(td):
        wire = _ReleaseWire("receiver")
        console = SerialConsole(
            "receiver",
            "/dev/ttyACM0",
            115200,
            os.path.join(td, "receiver-console.bin"),
            serial_class=lambda: hil_fakes.FakeSerial(wire),
        )
        console.open()
        if not console.wait_reader_ready(2.0):
            console.close()
            raise AssertionError("receiver reader did not become ready")
        return wire, console

    @staticmethod
    def _engine(clock):
        return Runner(
            RunnerDeps(clock=clock, sleep=lambda _seconds: None, summary_timeout=2.0)
        )

    def test_session_end_raw_evidence_fallback_rescues_late_summary(self):
        # FakeSerial publishes each complete line in the same reader
        # transaction that appends it to raw evidence. A full harness wire
        # cannot therefore retain a complete summary without also making it
        # available to the live queue. This direct unit seam uses the real
        # SerialConsole, releases bytes after the scan mark, then advances the
        # injected runner clock through the deadline before live consumption.
        with tempfile.TemporaryDirectory() as td:
            wire, console = self._open_console(td)
            try:
                summary_line = hil_fakes.receiver_stream_summary_line(
                    sdus=764, decoded=777, plc=13, rx_valid=764
                ).encode("utf-8")
                clock = _ReleaseAfterScanClock(
                    console, wire, [summary_line], [0.0, 3.0]
                )

                summary = self._engine(clock)._step_session_end(
                    console, object(), rows.RH2_ROW, 0
                )

                self.assertEqual(summary["segment"], 0)
                self.assertEqual(summary["last"]["rx_valid"], 764)
                self.assertEqual(summary["last"]["plc"], 13)
            finally:
                console.close()

    def test_session_end_raw_evidence_fallback_enforces_transport_limits(self):
        row = rows.RH3_7P5_DIAGNOSTIC_ROWS[0]
        with tempfile.TemporaryDirectory() as td:
            wire, console = self._open_console(td)
            try:
                summary_line = hil_fakes.receiver_stream_summary_line(
                    sdus=24,
                    decoded=38972,
                    plc=38924,
                    rx_valid=24,
                ).encode("utf-8")
                release = _ReleaseAfterScanClock(console, wire, [summary_line], [0.0])
                engine = self._engine(ControlledClock())

                def timeout_live_wait(*_args):
                    release()
                    return None

                engine._wait_console_line = timeout_live_wait
                with self.assertRaises(HilRunnerError) as ctx:
                    engine._step_session_end(console, object(), row, 0)

                self.assertEqual(ctx.exception.boundary, "session end")
                self.assertIn("rx_valid=24 below floor", ctx.exception.message)
                self.assertIn("plc=38924 above ceiling", ctx.exception.message)
            finally:
                console.close()

    def test_session_end_raw_evidence_fallback_proves_missing_summary(self):
        with tempfile.TemporaryDirectory() as td:
            wire, console = self._open_console(td)
            try:
                clock = _ReleaseAfterScanClock(console, wire, [], [0.0, 3.0])
                with self.assertRaises(HilRunnerError) as ctx:
                    self._engine(clock)._step_session_end(
                        console, object(), rows.RH2_ROW, 0
                    )

                self.assertEqual(ctx.exception.boundary, "session end")
                self.assertIn(
                    "missing receiver stream summary slot(s)", ctx.exception.message
                )
                self.assertIn(
                    "raw evidence scan also missing them after 0 bytes",
                    ctx.exception.message,
                )
            finally:
                console.close()

    def test_session_end_raw_evidence_fallback_scopes_reconnect_segments(self):
        row = rows.RH3_RECONNECT_ROW
        with tempfile.TemporaryDirectory() as td:
            wire, console = self._open_console(td)
            try:
                first_clock = _ReleaseAfterScanClock(
                    console,
                    wire,
                    [hil_fakes.receiver_stream_summary_line().encode("utf-8")],
                    [0.0, 0.0, 0.0, 0.0],
                )
                engine = self._engine(first_clock)

                first = engine._step_session_end(console, object(), row, 0)

                self.assertEqual(first["last"]["slot"], 0)
                self.assertEqual(console.drain_lines(), [])
                second_offset = console.bytes_received()
                full_text, _decode_failed = console.raw_text_since(0)
                self.assertEqual(len(receiver.parse_stream_summary(full_text)), 1)

                engine.deps.clock = _ReleaseAfterScanClock(
                    console,
                    wire,
                    [b"segment 1 teardown pending\r\n"],
                    [0.0, 3.0],
                )
                with self.assertRaises(HilRunnerError) as ctx:
                    engine._step_session_end(console, object(), row, 1)

                self.assertIn(
                    "missing receiver stream summary slot(s)", ctx.exception.message
                )
                self.assertIn("raw evidence scan also missing", ctx.exception.message)
                segment_text, _decode_failed = console.raw_text_since(second_offset)
                self.assertIn("segment 1 teardown pending", segment_text)
                self.assertEqual(receiver.parse_stream_summary(segment_text), [])
            finally:
                console.close()


class TestRh3Rows(unittest.TestCase):
    def test_cli_exposes_all_checked_in_rows_and_defaults_to_rh2(self):
        parser = cli.build_parser()
        base = [
            "run",
            "--fixture",
            "f",
            "--binding",
            "b",
            "--output-root",
            "o",
            "--run-id",
            "r",
            "--junit",
            "j",
        ]
        self.assertEqual(cli._selected_row(parser.parse_args(base).row), rows.RH2_ROW)
        for row in rows.ALL_ROWS:
            args = parser.parse_args(base + ["--row", row.name])
            self.assertEqual(cli._selected_row(args.row), row)

    def test_reconnect_row_uses_two_segments_and_row_junit_name(self):
        row = rows.RH3_RECONNECT_ROW
        with tempfile.TemporaryDirectory() as td:
            receiver_wire = hil_fakes.Wire(
                "receiver",
                chunks=(
                    hil_fakes.receiver_boot_chunks(list(runner.RECEIVER_BOOT_MARKERS))
                    + [
                        hil_fakes.receiver_transcript(
                            "bt identity", ["Identity: DB:A6:0C:05:A2:AA (random)"]
                        ),
                        hil_fakes.receiver_transcript("bt bonds", ["Bond count: 1"]),
                        hil_fakes.receiver_transcript(
                            "bt identity", ["Identity: DB:A6:0C:05:A2:AA (random)"]
                        ),
                        hil_fakes.flpr_offload_transcript(),
                        hil_fakes.iso_link_quality_transcript(),
                        hil_fakes.receiver_stream_summary_line().encode("utf-8"),
                        hil_fakes.audio_status_transcript(),
                        hil_fakes.audio_perf_transcript(),
                        hil_fakes.flpr_offload_transcript(
                            state="STOPPED", submit=151, success=151, epoch=0, gen=2
                        ),
                        hil_fakes.flpr_status_transcript(),
                        hil_fakes.flpr_offload_transcript(),
                        hil_fakes.iso_link_quality_transcript(),
                        hil_fakes.receiver_stream_summary_line().encode("utf-8"),
                        hil_fakes.audio_status_transcript(),
                        hil_fakes.audio_perf_transcript(),
                        hil_fakes.flpr_offload_transcript(
                            state="STOPPED", submit=151, success=151, epoch=0, gen=3
                        ),
                        hil_fakes.flpr_status_transcript(),
                    ]
                ),
                assert_writes=[
                    "bt identity\r",
                    "bt bonds\r",
                    "bt identity\r",
                    "flpr offload\r",
                    "bt iso quality\r",
                    "audio status\r",
                    "audio perf\r",
                    "flpr offload\r",
                    "flpr status\r",
                    "flpr offload\r",
                    "bt iso quality\r",
                    "audio status\r",
                    "audio perf\r",
                    "flpr offload\r",
                    "flpr status\r",
                ],
            )
            source_wire = _source_wire_for_row(RUN_ID, row, initial_bond_count=1)
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )
            self.assertEqual(result[0], "passed", result)
            with open(
                os.path.join(out_root, run_id, "row.json"), encoding="utf-8"
            ) as fh:
                row_json = json.load(fh)
            self.assertEqual(row_json["name"], row.name)
            self.assertEqual(row_json["segment_count"], 2)
            self.assertEqual(row_json["expected_scored_per_stream"], 24000)
            with open(
                os.path.join(out_root, run_id, "summary.json"), encoding="utf-8"
            ) as fh:
                summary = json.load(fh)
            self.assertEqual(len(summary["receiver_streams"]), 2)

    def test_hang_recovery_row_runner_owned_uart_and_evidence(self):
        row = rows.RH3_HANG_ROW
        with tempfile.TemporaryDirectory() as td:
            receiver_wire = _receiver_recovery_wire(
                RUN_ID, row, "hang", warning_inside=HANG_RECOVERY_WARNING
            )
            source_wire = _source_wire_for_row(RUN_ID, row, initial_bond_count=1)
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )
            self.assertEqual(result[0], "passed", result)
            run_dir = os.path.join(out_root, run_id)
            for name in ("fault-baseline.json", "recovery-window.txt", "recovery.json"):
                self.assertTrue(os.path.isfile(os.path.join(run_dir, name)), name)
            with open(os.path.join(run_dir, "recovery.json"), encoding="utf-8") as fh:
                recovery_file = json.load(fh)
            with open(os.path.join(run_dir, "summary.json"), encoding="utf-8") as fh:
                summary = json.load(fh)
            recovery = summary["recovery"]
            self.assertEqual(recovery["fault"], "hang")
            self.assertEqual(recovery["ack"], "FAULT_HANG_ACK")
            self.assertEqual(recovery["baseline"]["runtime_restarts"], 0)
            self.assertEqual(recovery["window_final"]["runtime_restarts"], 1)
            active = summary["source_active"][0]["receiver_offload"]["offload"]
            post_stop = summary["receiver_streams"][0]["post_stop"]["offload"]
            self.assertEqual(post_stop["state"], "STOPPED")
            self.assertEqual(post_stop["submit"], post_stop["success"])
            self.assertGreaterEqual(post_stop["success"], active["success"])
            for field in (
                "fallback",
                "recovery_attempts",
                "fault_timeout",
                "runtime_restarts",
            ):
                self.assertEqual(post_stop[field], active[field], field)
            raw_window = recovery["raw_window"]
            self.assertEqual(recovery_file["raw_window"], raw_window)
            self.assertIsInstance(raw_window["start_offset"], int)
            self.assertIsInstance(raw_window["end_offset"], int)
            self.assertGreaterEqual(raw_window["start_offset"], 0)
            self.assertLessEqual(raw_window["start_offset"], raw_window["end_offset"])
            with open(os.path.join(run_dir, "receiver-console.bin"), "rb") as fh:
                raw = fh.read()
            self.assertEqual(
                raw_window["start_offset"], raw.index(b"uart:~$ flpr hang\r\n")
            )
            self.assertEqual(
                raw_window["end_offset"], raw.index(b"uart:~$ bt iso quality\r\n")
            )
            self.assertIn(HANG_RECOVERY_WARNING.encode("utf-8"), raw)
            with open(
                os.path.join(run_dir, "recovery-window.txt"), encoding="utf-8"
            ) as fh:
                self.assertIn(HANG_RECOVERY_WARNING, fh.read())
            with open(
                os.path.join(run_dir, "receiver-status.txt"), encoding="utf-8"
            ) as fh:
                self.assertNotIn("settle retry", fh.read())
            self.assertIn(b"flpr hang\r", receiver_wire.writes)

    def test_hang_recovery_warning_before_or_after_window_fails_log_scan(self):
        row = rows.RH3_HANG_ROW
        for position in ("before", "after"):
            with self.subTest(position=position), tempfile.TemporaryDirectory() as td:
                kwargs = {"warning_%s" % position: HANG_RECOVERY_WARNING}
                receiver_wire = _receiver_recovery_wire(RUN_ID, row, "hang", **kwargs)
                source_wire = _source_wire_for_row(RUN_ID, row, initial_bond_count=1)
                result, _out, _run_id, _junit, _f, _b, _engine = _run_harness(
                    td,
                    receiver_wire=receiver_wire,
                    source_wire=source_wire,
                    row=row,
                )
                self.assertEqual(result[0], "failed")
                self.assertEqual(result[1], "log scan")

    def test_hang_recovery_warning_crossing_window_boundary_fails_log_scan(self):
        row = rows.RH3_HANG_ROW
        for boundary in ("start", "end"):
            with self.subTest(boundary=boundary), tempfile.TemporaryDirectory() as td:
                receiver_wire = _receiver_recovery_wire(
                    RUN_ID,
                    row,
                    "hang",
                    **{"warning_crossing_%s" % boundary: HANG_RECOVERY_WARNING},
                )
                source_wire = _source_wire_for_row(RUN_ID, row, initial_bond_count=1)
                result, _out, _run_id, _junit, _f, _b, _engine = _run_harness(
                    td,
                    receiver_wire=receiver_wire,
                    source_wire=source_wire,
                    row=row,
                )
                self.assertEqual(result[0], "failed")
                self.assertEqual(result[1], "log scan")

    def test_hang_recovery_unrelated_warning_inside_window_fails_log_scan(self):
        row = rows.RH3_HANG_ROW
        with tempfile.TemporaryDirectory() as td:
            receiver_wire = _receiver_recovery_wire(
                RUN_ID,
                row,
                "hang",
                warning_inside="<wrn> audio_offload: unrelated warning",
            )
            source_wire = _source_wire_for_row(RUN_ID, row, initial_bond_count=1)
            result, _out, _run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )
            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "log scan")

    def test_stall_row_does_not_exempt_hang_recovery_warning(self):
        row = rows.RH3_STALL_ROW
        with tempfile.TemporaryDirectory() as td:
            receiver_wire = _receiver_recovery_wire(
                RUN_ID, row, "stall", warning_inside=HANG_RECOVERY_WARNING
            )
            source_wire = _source_wire_for_row(RUN_ID, row, initial_bond_count=1)
            result, _out, _run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )
            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "log scan")

    def test_hang_recovery_malformed_raw_window_fails_log_scan(self):
        row = rows.RH3_HANG_ROW
        with tempfile.TemporaryDirectory() as td:
            receiver_wire = _receiver_recovery_wire(RUN_ID, row, "hang")
            source_wire = _source_wire_for_row(RUN_ID, row, initial_bond_count=1)

            def malformed_console_offset(engine):
                original_factory = engine.deps.serial_factory

                def factory(role, path, baud, evidence_path, dtr, rts):
                    console = original_factory(
                        role, path, baud, evidence_path, dtr, rts
                    )
                    if role == "receiver":
                        offsets = iter((-1, 0))
                        console.rx_offset = lambda: next(offsets)
                    return console

                engine.deps.serial_factory = factory

            result, _out, _run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
                engine_setup=malformed_console_offset,
            )
            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "log scan")

    def test_stall_recovery_row_runner_owned_uart_and_evidence(self):
        row = rows.RH3_STALL_ROW
        with tempfile.TemporaryDirectory() as td:
            receiver_wire = _receiver_recovery_wire(RUN_ID, row, "stall")
            source_wire = _source_wire_for_row(RUN_ID, row, initial_bond_count=1)
            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )
            self.assertEqual(result[0], "passed", result)
            with open(
                os.path.join(out_root, run_id, "summary.json"), encoding="utf-8"
            ) as fh:
                summary = json.load(fh)
            recovery = summary["recovery"]
            self.assertEqual(recovery["fault"], "stall")
            self.assertEqual(recovery["ack"], "TIMED_STALL_ACK")
            self.assertEqual(recovery["baseline"]["runtime_restarts"], 0)
            self.assertEqual(recovery["window_final"]["runtime_restarts"], 0)
            active = summary["source_active"][0]["receiver_offload"]["offload"]
            post_stop = summary["receiver_streams"][0]["post_stop"]["offload"]
            self.assertEqual(post_stop["state"], "STOPPED")
            self.assertEqual(post_stop["submit"], post_stop["success"])
            self.assertGreaterEqual(post_stop["success"], active["success"])
            for field in (
                "fallback",
                "recovery_attempts",
                "fault_timeout",
            ):
                self.assertEqual(post_stop[field], active[field], field)
            self.assertEqual(post_stop["runtime_restarts"], -1)
            normalized_post_stop = receiver.normalized_recovery_baseline(post_stop)
            self.assertEqual(active["runtime_restarts"], 0)
            for field in ("runtime_restarts", "runtime_fails", "hb_dedup"):
                self.assertEqual(normalized_post_stop[field], active[field], field)
            self.assertIn(b"flpr ring stall_flpr_ms 1 60\r", receiver_wire.writes)


class TestRh3ReceiverRecoveryValidation(unittest.TestCase):
    @staticmethod
    def _baseline():
        return {
            "recovery_attempts": 0,
            "recovery_fail": 0,
            "relapses": 0,
            "exhaustion": 0,
            "probation_cleared": 0,
            "runtime_restarts": 0,
            "epoch": 10,
            "success": 1000,
            "fallback": 0,
        }

    @staticmethod
    def _offload(**overrides):
        result = {
            "state": "ACTIVE",
            "epoch": 11,
            "submit": 1200,
            "success": 1150,
            "fallback": 1,
            "busy": 0,
            "recovery_attempts": 1,
            "recovery_fail": 0,
            "relapses": 0,
            "exhaustion": 0,
            "probation_active": 0,
            "probation_success": 0,
            "probation_cleared": 1,
            "fault_timeout": 1,
            "fault_full": 0,
            "fault_stale": 0,
            "fault_seq": 0,
            "fault_frame": 0,
            "fault_crc": 0,
            "fault_payload": 0,
            "runtime_restarts": 1,
            "runtime_fails": 0,
            "hb_dedup": 0,
        }
        result.update(overrides)
        return result

    @staticmethod
    def _handshake():
        return {
            "ready": True,
            "acked": True,
            "healthy": True,
            "err_len": 0,
            "err_ver": 0,
            "err_unk": 0,
            "err_send": 0,
            "rx_lost": 0,
            "rx_dup": 0,
            "rx_ooo": 0,
            "rx_missed": 0,
        }

    @staticmethod
    def _audio():
        return {
            "decode_errors": 0,
            "i2s_underruns": 0,
            "stream_resets": 0,
            "push_failures": 0,
        }

    @staticmethod
    def _healthy_active():
        return receiver.parse_offload_status(
            hil_fakes.flpr_offload_transcript().decode("utf-8")
        )

    @staticmethod
    def _healthy_post_stop(**overrides):
        values = {
            "state": "STOPPED",
            "submit": 151,
            "success": 151,
            "epoch": 0,
            "gen": 2,
        }
        values.update(overrides)
        return receiver.parse_offload_status(
            hil_fakes.flpr_offload_transcript(**values).decode("utf-8")
        )

    def test_receiver_lifecycle_validator_accepts_healthy_active_to_stopped(self):
        self.assertEqual(
            receiver.validate_receiver_lifecycle_blocks(
                self._audio(),
                self._healthy_active(),
                self._healthy_post_stop(),
                self._handshake(),
            ),
            [],
        )

    def test_receiver_lifecycle_validator_rejects_final_active_state(self):
        errors = receiver.validate_receiver_lifecycle_blocks(
            self._audio(),
            self._healthy_active(),
            self._healthy_post_stop(state="ACTIVE"),
            self._handshake(),
        )
        self.assertIn("post-stop offload state='ACTIVE'", errors)

    def test_receiver_lifecycle_validator_rejects_missing_final_progress(self):
        errors = receiver.validate_receiver_lifecycle_blocks(
            self._audio(),
            self._healthy_active(),
            self._healthy_post_stop(submit=150, success=150),
            self._handshake(),
        )
        self.assertIn("post-stop offload success did not advance", errors)

    def test_receiver_lifecycle_validator_rejects_final_fallback(self):
        errors = receiver.validate_receiver_lifecycle_blocks(
            self._audio(),
            self._healthy_active(),
            self._healthy_post_stop(fallback=1),
            self._handshake(),
        )
        self.assertIn("post-stop offload fallback=1", errors)

    def test_receiver_lifecycle_validator_preserves_recovery_terminal_counters(self):
        recovery = {
            "fault": "hang",
            "ack": "FAULT_HANG_ACK",
            "baseline": self._baseline(),
        }
        active = self._offload(probation_success=100)
        post = dict(active)
        post.update(
            state="STOPPED",
            success=active["submit"],
            epoch=0,
            gen=12,
            probation_success=0,
        )
        self.assertEqual(
            receiver.validate_receiver_lifecycle_blocks(
                self._audio(), active, post, self._handshake(), recovery=recovery
            ),
            [],
        )
        post["probation_cleared"] += 1
        errors = receiver.validate_receiver_lifecycle_blocks(
            self._audio(), active, post, self._handshake(), recovery=recovery
        )
        self.assertIn("post-stop offload probation_cleared changed", errors)

    def test_stall_recovery_accepts_omitted_runtime_and_rejects_submit_or_busy_change(
        self,
    ):
        recovery = {
            "fault": "stall",
            "ack": "TIMED_STALL_ACK",
            "baseline": self._baseline(),
        }
        active = self._offload(runtime_restarts=0, runtime_fails=0, hb_dedup=0)
        post = receiver.parse_offload_status(
            hil_fakes.flpr_offload_transcript(
                state="STOPPED",
                submit=active["submit"],
                success=active["submit"],
                fallback=active["fallback"],
                busy=active["busy"],
                epoch=0,
                gen=12,
                recovery_attempts=active["recovery_attempts"],
                recovery_fail=active["recovery_fail"],
                relapses=active["relapses"],
                exhaustion=active["exhaustion"],
                probation_active=active["probation_active"],
                probation_success=active["probation_success"],
                probation_cleared=active["probation_cleared"],
                fault_timeout=active["fault_timeout"],
                fault_full=active["fault_full"],
                fault_stale=active["fault_stale"],
                fault_seq=active["fault_seq"],
                fault_frame=active["fault_frame"],
                fault_crc=active["fault_crc"],
                fault_payload=active["fault_payload"],
                runtime_restarts=None,
            ).decode("utf-8")
        )
        self.assertEqual(post["runtime_restarts"], -1)
        self.assertEqual(
            receiver.validate_receiver_lifecycle_blocks(
                self._audio(),
                active,
                post,
                self._handshake(),
                recovery=recovery,
            ),
            [],
        )

        post["submit"] = active["submit"] - 1
        post["success"] = post["submit"]
        errors = receiver.validate_receiver_lifecycle_blocks(
            self._audio(),
            active,
            post,
            self._handshake(),
            recovery=recovery,
        )
        self.assertIn("post-stop offload submit regressed", errors)

        post["submit"] = active["submit"]
        post["success"] = active["submit"]
        post["busy"] = active["busy"] + 1
        errors = receiver.validate_receiver_lifecycle_blocks(
            self._audio(),
            active,
            post,
            self._handshake(),
            recovery=recovery,
        )
        self.assertIn("post-stop offload busy changed", errors)

    def test_hang_named_transport_fault_allowed_but_integrity_fault_rejected(self):
        recovery = {
            "fault": "hang",
            "ack": "FAULT_HANG_ACK",
            "baseline": self._baseline(),
        }
        self.assertEqual(
            receiver.validate_receiver_blocks(
                self._audio(), self._offload(), self._handshake(), recovery=recovery
            ),
            [],
        )
        errors = receiver.validate_receiver_blocks(
            self._audio(),
            self._offload(fault_crc=1),
            self._handshake(),
            recovery=recovery,
        )
        self.assertIn("offload fault_crc=1", errors)

    def test_stall_requires_no_runtime_restart(self):
        recovery = {
            "fault": "stall",
            "ack": "TIMED_STALL_ACK",
            "baseline": self._baseline(),
        }
        errors = receiver.validate_receiver_blocks(
            self._audio(),
            self._offload(runtime_restarts=1),
            self._handshake(),
            recovery=recovery,
        )
        self.assertIn("stall runtime restart changed", errors)

    def test_missing_runtime_line_normalizes_to_zero_baseline(self):
        baseline = receiver.normalized_recovery_baseline(
            receiver.parse_offload_status(
                """--- Audio offload ---
  State       : ACTIVE / epoch=10 gen=10
  Counters    : submit=1000 success=1000 fallback=0 busy=0
  Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0
  Probation   : active=0 success=0 cleared=0
  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
"""
            )
        )
        self.assertEqual(baseline["runtime_restarts"], 0)
        self.assertEqual(baseline["runtime_fails"], 0)

    def test_fault_ack_parser_is_exact(self):
        self.assertEqual(
            receiver.parse_fault_ack(
                "FAULT_HANG_ACK received — FLPR hang imminent.", "hang"
            ),
            "FAULT_HANG_ACK",
        )
        self.assertEqual(
            receiver.parse_fault_ack(
                "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)",
                "stall",
            ),
            "TIMED_STALL_ACK",
        )
        self.assertIsNone(receiver.parse_fault_ack("duration=60", "stall"))

    def test_fault_row_log_scan_rejects_recovery_warning_outside_command_window(self):
        # Generic warning matching remains strict. Only scan_raw_warnings()
        # can exempt one documented hang payload in its raw-byte window.
        self.assertEqual(
            receiver.scan_warnings(
                ["<wrn> audio_offload: offload: heartbeat supervisor → RECOVERING"]
            ),
            ["<wrn> audio_offload: offload: heartbeat supervisor → RECOVERING"],
        )

    def test_hang_raw_log_scanner_accepts_only_documented_payloads_inside_window(self):
        payloads = (
            "offload: heartbeat supervisor → RECOVERING",
            "offload recovery: handshake unhealthy, escalating to runtime restart",
            "offload recovery: short ring reset failed (-116), escalating to runtime restart",
        )
        for payload in payloads:
            with self.subTest(payload=payload):
                line = "<wrn> audio_offload: %s\r\n" % payload
                raw = b"prelude\r\n" + line.encode("utf-8") + b"tail\r\n"
                start_offset = len(b"prelude\r\n")
                end_offset = start_offset + len(line.encode("utf-8"))
                self.assertEqual(
                    receiver.scan_raw_warnings(
                        raw,
                        fault="hang",
                        recovery={
                            "raw_window": {
                                "start_offset": start_offset,
                                "end_offset": end_offset,
                            }
                        },
                    ),
                    [],
                )
        error_line = (
            "<err> audio_offload: offload: heartbeat supervisor → RECOVERING\r\n"
        )
        self.assertEqual(
            receiver.scan_raw_warnings(
                error_line.encode("utf-8"),
                fault="hang",
                recovery={
                    "raw_window": {
                        "start_offset": 0,
                        "end_offset": len(error_line.encode("utf-8")),
                    }
                },
            ),
            [error_line.rstrip("\r\n")],
        )

    def test_hang_raw_log_scanner_rejects_malformed_utf8_despite_valid_window(self):
        raw = HANG_RECOVERY_WARNING.encode("utf-8") + b"\r\n\xff"
        with self.assertRaises(UnicodeDecodeError):
            receiver.scan_raw_warnings(
                raw,
                fault="hang",
                recovery={
                    "raw_window": {
                        "start_offset": 0,
                        "end_offset": len(raw),
                    }
                },
            )

    def test_hang_raw_log_scanner_rejects_invalid_raw_window_shape(self):
        raw = HANG_RECOVERY_WARNING.encode("utf-8") + b"\r\n"
        for recovery in (
            {},
            {"start_offset": 0, "end_offset": len(raw) + 1},
            {"start_offset": "0", "end_offset": len(raw)},
            {"start_offset": 0, "end_offset": len(raw), "extra": 1},
        ):
            with self.subTest(recovery=recovery):
                with self.assertRaises(receiver.ReceiverError):
                    receiver.scan_raw_warnings(
                        raw,
                        fault="hang",
                        recovery=(
                            recovery if not recovery else {"raw_window": recovery}
                        ),
                    )


# ── runner: failure paths ──────────────────────────────────────────


class TestRunnerFailures(unittest.TestCase):
    def _fail_harness(self, mutate):
        """Run the harness with a mutated scripted runner and return the
        result tuple plus run_dir."""
        td = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, td, ignore_errors=True)
        out_root = os.path.join(td, "out")
        os.makedirs(out_root)
        cfg = os.path.join(td, "cfg")
        os.makedirs(cfg)
        fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
        repo_fake = os.path.join(td, "repo")
        os.makedirs(repo_fake)
        hil_fakes.make_images(repo_fake)
        junit = os.path.join(out_root, "%s.junit.xml" % RUN_ID)
        run_cmd = hil_fakes.ScriptedRunner()
        run_cmd.script_exit(["lsof", "--", "/dev/ttyACM1"], 1)
        run_cmd.script(
            ["fw-flash-hil-source"],
            hil_fakes.FakeProc(stdout="program verify\nreset run\n"),
        )
        run_cmd.script(["fw-flash-54l15"], hil_fakes.FakeProc(stdout="ok\n"))
        mutate(run_cmd)
        receiver_wire = _receiver_passing_wire(RUN_ID)
        source_t = hil_fakes.build_passing_source_wire(RUN_ID)
        src_chunks, src_writes = source_t.build()
        source_wire = hil_fakes.Wire(
            "source", chunks=src_chunks, assert_writes=src_writes
        )
        deps = make_runner_deps(
            receiver_wire,
            source_wire,
            run_cmd=run_cmd,
            repo_root=repo_fake,
        )
        engine = Runner(deps)
        result = engine.run(
            fixture_path,
            binding_path,
            out_root,
            RUN_ID,
            junit,
            argv=["hil-runner.py", "run"],
            status=0,
        )
        return result, os.path.join(out_root, RUN_ID)

    def test_flash_nonzero_fails_with_evidence(self):
        def mutate(run_cmd):
            run_cmd.rules[1] = (
                ["fw-flash-hil-source"],
                hil_fakes.FakeProc(stdout="", stderr="flash boom", returncode=1),
                True,
            )

        result, run_dir = self._fail_harness(mutate)
        self.assertEqual(result[0], "failed")
        self.assertIn("flash source", result[1])
        self.assertTrue(os.path.isfile(os.path.join(run_dir, "source-flash.log")))
        with open(os.path.join(run_dir, "junit.xml")) as fh:
            self.assertIn("flash source", fh.read())
        with open(os.path.join(run_dir, "MANIFEST.md")) as fh:
            self.assertIn("outcome: failed", fh.read())

    def test_discovery_failure_retains_partial_identity_evidence(self):
        with tempfile.TemporaryDirectory() as td:
            out_root = os.path.join(td, "out")
            os.makedirs(out_root)
            cfg = os.path.join(td, "cfg")
            os.makedirs(cfg)
            fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
            junit = os.path.join(out_root, "%s.junit.xml" % RUN_ID)

            def fail_discovery(_binding, sysfs_root=None, run_cmd=None):
                del sysfs_root, run_cmd
                exc = HilDiscoveryError("injected identity drift")
                exc.raw = MappingProxyType(
                    {
                        "nrf-probes": {
                            "argv": ["nrf-probes"],
                            "stdout": "probe table\n",
                            "stderr": "",
                            "status": 0,
                        }
                    }
                )
                raise exc

            deps = RunnerDeps(
                discover=fail_discovery,
                environment=lambda argv, status: {"argv": argv, "status": status},
            )
            result = Runner(deps).run(
                fixture_path,
                binding_path,
                out_root,
                RUN_ID,
                junit,
                argv=["hil-runner.py", "run"],
            )
            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "setup")
            run_dir = os.path.join(out_root, RUN_ID)
            with open(os.path.join(run_dir, "identity.json")) as fh:
                identity = json.load(fh)
            self.assertFalse(identity["resolved"])
            with open(os.path.join(run_dir, "nrf-probes.txt")) as fh:
                self.assertIn("probe table", fh.read())
            with open(os.path.join(run_dir, "MANIFEST.md")) as fh:
                self.assertIn("outcome: failed", fh.read())

    def test_flash_cancellation_retains_partial_flash_log(self):
        with tempfile.TemporaryDirectory() as td:
            out_root = os.path.join(td, "out")
            os.makedirs(out_root)
            cfg = os.path.join(td, "cfg")
            os.makedirs(cfg)
            fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
            repo_fake = os.path.join(td, "repo")
            os.makedirs(repo_fake)
            hil_fakes.make_images(repo_fake)
            junit = os.path.join(out_root, "%s.junit.xml" % RUN_ID)

            def cancel_flash(argv, timeout, env=None):
                del timeout, env
                if argv == ["lsof", "--", "/dev/ttyACM1"]:
                    return hil_fakes.FakeProc(returncode=1)
                if argv == ["fw-flash-hil-source"]:
                    raise CommandCancelled(
                        "cancelled during command",
                        subprocess.CompletedProcess(
                            argv, -15, "partial flash\n", "stopped\n"
                        ),
                    )
                raise AssertionError("unexpected command: %r" % (argv,))

            receiver_wire = _receiver_passing_wire(RUN_ID)
            source_wire = hil_fakes.Wire("source")
            deps = make_runner_deps(
                receiver_wire,
                source_wire,
                run_cmd=cancel_flash,
                repo_root=repo_fake,
            )
            result = Runner(deps).run(
                fixture_path,
                binding_path,
                out_root,
                RUN_ID,
                junit,
                argv=["hil-runner.py", "run"],
                status=0,
            )

            self.assertEqual(result[0], "cancelled", result)
            run_dir = os.path.join(out_root, RUN_ID)
            with open(
                os.path.join(run_dir, "source-flash.log"), encoding="utf-8"
            ) as fh:
                flash_log = fh.read()
            self.assertIn("partial flash", flash_log)
            self.assertIn("stopped", flash_log)
            with open(os.path.join(run_dir, "commands.jsonl"), encoding="utf-8") as fh:
                commands = [json.loads(line) for line in fh if line.strip()]
            flash = next(
                record
                for record in commands
                if record["argv"] == ["fw-flash-hil-source"]
            )
            self.assertEqual(flash["status"], -15)
            self.assertIn("partial flash", flash["stdout"])

    def test_preflight_holder_fails_before_serial(self):
        def mutate(run_cmd):
            run_cmd.rules[0] = (
                ["lsof", "--", "/dev/ttyACM1"],
                hil_fakes.FakeProc(stdout="btattach 1234\n", returncode=0),
                True,
            )

        result, run_dir = self._fail_harness(mutate)
        self.assertEqual(result[0], "failed")
        self.assertIn("preflight tty", result[1])
        # No serial console evidence: preflight fails before open.
        self.assertFalse(os.path.isfile(os.path.join(run_dir, "receiver-console.bin")))

    def test_boot_marker_timeout_fails(self):
        def mutate(run_cmd):
            pass  # wires still deliver; force a boot timeout below

        with tempfile.TemporaryDirectory() as td:
            out_root = os.path.join(td, "out")
            os.makedirs(out_root)
            cfg = os.path.join(td, "cfg")
            os.makedirs(cfg)
            fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
            repo_fake = os.path.join(td, "repo")
            os.makedirs(repo_fake)
            hil_fakes.make_images(repo_fake)
            junit = os.path.join(out_root, "%s.junit.xml" % RUN_ID)
            run_cmd = hil_fakes.ScriptedRunner()
            run_cmd.script_exit(["lsof", "--", "/dev/ttyACM1"], 1)
            run_cmd.script(
                ["fw-flash-hil-source"],
                hil_fakes.FakeProc(stdout="ok\n"),
            )
            run_cmd.script(["fw-flash-54l15"], hil_fakes.FakeProc(stdout="ok\n"))
            # Receiver wire: only the first boot marker, then silence.
            receiver_wire = hil_fakes.Wire(
                "receiver", chunks=hil_fakes.receiver_boot_chunks(["BLE ready"])
            )
            source_t = hil_fakes.build_passing_source_wire(RUN_ID)
            src_chunks, src_writes = source_t.build()
            source_wire = hil_fakes.Wire(
                "source", chunks=src_chunks, assert_writes=src_writes
            )
            deps = make_runner_deps(
                receiver_wire,
                source_wire,
                run_cmd=run_cmd,
                repo_root=repo_fake,
            )
            engine = Runner(deps)
            result = engine.run(
                fixture_path,
                binding_path,
                out_root,
                RUN_ID,
                junit,
                argv=["hil-runner.py", "run"],
                status=0,
            )
            self.assertEqual(result[0], "failed")
            self.assertIn("boot", result[1])

    def test_source_timeout_calls_stop_and_idle(self):
        # Source wire never reaches terminal after scored_complete: the
        # run fails on the stall and the bounded stop/idle cleanup still
        # runs.  A clock that is always past the deadline makes the stall
        # fail immediately instead of burning the real 90 s scored
        # timeout; queued records are still consumed.
        with tempfile.TemporaryDirectory() as td:
            out_root = os.path.join(td, "out")
            os.makedirs(out_root)
            cfg = os.path.join(td, "cfg")
            os.makedirs(cfg)
            fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
            repo_fake = os.path.join(td, "repo")
            os.makedirs(repo_fake)
            hil_fakes.make_images(repo_fake)
            junit = os.path.join(out_root, "%s.junit.xml" % RUN_ID)
            run_cmd = hil_fakes.ScriptedRunner()
            run_cmd.script_exit(["lsof", "--", "/dev/ttyACM1"], 1)
            run_cmd.script(
                ["fw-flash-hil-source"],
                hil_fakes.FakeProc(stdout="ok\n"),
            )
            run_cmd.script(["fw-flash-54l15"], hil_fakes.FakeProc(stdout="ok\n"))
            receiver_wire = _receiver_passing_wire(RUN_ID)
            t = hil_fakes.SourceTranscript(RUN_ID)
            t.hello(bond_count=0)
            t.idle()
            t.unpair()
            t.hello(bond_count=0)
            t.configure()
            t.start_then_stall()
            source_wire = _source_wire_with_deferred_cleanup(t)
            deps = make_runner_deps(
                receiver_wire,
                source_wire,
                run_cmd=run_cmd,
                clock=lambda: time.monotonic() + 1000.0,
                repo_root=repo_fake,
            )
            engine = Runner(deps)
            result = engine.run(
                fixture_path,
                binding_path,
                out_root,
                RUN_ID,
                junit,
                argv=["hil-runner.py", "run"],
                status=0,
            )
            self.assertEqual(result[0], "failed")
            self.assertIn("run row", result[1])
            commands = [
                w.decode("utf-8").split('"command":"')[1].split('"')[0]
                for w in source_wire.writes
            ]
            self.assertIn("stop", commands)
            self.assertIn("idle", commands)
            run_dir = os.path.join(out_root, RUN_ID)
            self.assertTrue(os.path.isfile(os.path.join(run_dir, "junit.xml")))
            self.assertEqual(os.listdir(os.path.join(out_root, ".locks")), [])

    def test_configure_failure_runs_prestart_idle_cleanup(self):
        # Configure can mutate source state before START registers its normal
        # stop/terminal cleanup. Runner must still return source to idle.
        with tempfile.TemporaryDirectory() as td:
            out_root = os.path.join(td, "out")
            os.makedirs(out_root)
            cfg = os.path.join(td, "cfg")
            os.makedirs(cfg)
            fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
            repo_fake = os.path.join(td, "repo")
            os.makedirs(repo_fake)
            hil_fakes.make_images(repo_fake)
            junit = os.path.join(out_root, "%s.junit.xml" % RUN_ID)
            run_cmd = hil_fakes.ScriptedRunner()
            run_cmd.script_exit(["lsof", "--", "/dev/ttyACM1"], 1)
            run_cmd.script(["fw-flash-hil-source"], hil_fakes.FakeProc(stdout="ok\n"))
            run_cmd.script(["fw-flash-54l15"], hil_fakes.FakeProc(stdout="ok\n"))
            receiver_wire = _receiver_passing_wire(RUN_ID)
            source_t = hil_fakes.SourceTranscript(RUN_ID)
            source_t.hello(bond_count=0)
            source_t.idle()
            source_t.unpair()
            source_t.hello(bond_count=0)
            configure_id = source_t._expect(
                "configure",
                peer_address="DB:A6:0C:05:A2:AA",
                peer_address_type="random",
                mode="mono",
                profile="48_4_1",
                scored_sdu_count=120,
                signal_seed=1218649181,
                reconnect_policy="none",
            )
            source_t._status(
                configure_id,
                hil_fakes.source_status_data("configure", ok=False, error="failed"),
            )
            source_t.idle()
            src_chunks, src_writes = source_t.build()
            source_wire = hil_fakes.Wire(
                "source", chunks=src_chunks, assert_writes=src_writes
            )
            deps = make_runner_deps(
                receiver_wire,
                source_wire,
                run_cmd=run_cmd,
                repo_root=repo_fake,
            )
            outcome, boundary, cleanup = Runner(deps).run(
                fixture_path,
                binding_path,
                out_root,
                RUN_ID,
                junit,
                argv=["hil-runner.py", "run"],
                status=0,
            )
            self.assertEqual(outcome, "failed")
            self.assertEqual(boundary, "run row")
            self.assertEqual(cleanup, [])
            commands = [
                write.decode("utf-8").split('"command":"')[1].split('"')[0]
                for write in source_wire.writes
            ]
            self.assertEqual(commands[-2:], ["configure", "idle"])

    def test_cancel_mid_run_cancelled_with_evidence(self):
        # Cancel fires during the receiver tail hook (after scored_complete);
        # the outcome is cancelled, evidence is retained, and the bounded
        # stop/idle cleanup still runs.
        with tempfile.TemporaryDirectory() as td:
            out_root = os.path.join(td, "out")
            os.makedirs(out_root)
            cfg = os.path.join(td, "cfg")
            os.makedirs(cfg)
            fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
            repo_fake = os.path.join(td, "repo")
            os.makedirs(repo_fake)
            hil_fakes.make_images(repo_fake)
            junit = os.path.join(out_root, "%s.junit.xml" % RUN_ID)
            run_cmd = hil_fakes.ScriptedRunner()
            run_cmd.script_exit(["lsof", "--", "/dev/ttyACM1"], 1)
            run_cmd.script(
                ["fw-flash-hil-source"],
                hil_fakes.FakeProc(stdout="ok\n"),
            )
            run_cmd.script(["fw-flash-54l15"], hil_fakes.FakeProc(stdout="ok\n"))
            receiver_wire = _receiver_passing_wire(RUN_ID)
            t = hil_fakes.SourceTranscript(RUN_ID)
            t.hello(bond_count=0)
            t.idle()
            t.unpair()
            t.hello(bond_count=0)
            t.configure()
            t.start_then_stall()
            source_wire = _source_wire_with_deferred_cleanup(t)

            # Cancellation becomes observable only once the source reached
            # scored_complete and runner began receiver-tail ISO collection.
            # Earlier runner boundaries now also poll cancellation.
            def cancel_during_tail():
                return any(w == b"bt iso quality\r" for w in receiver_wire.writes)

            deps = make_runner_deps(
                receiver_wire,
                source_wire,
                run_cmd=run_cmd,
                cancel=cancel_during_tail,
                clock=lambda: time.monotonic() + 1000.0,
                repo_root=repo_fake,
            )
            engine = Runner(deps)
            result = engine.run(
                fixture_path,
                binding_path,
                out_root,
                RUN_ID,
                junit,
                argv=["hil-runner.py", "run"],
                status=0,
            )
            self.assertEqual(result[0], "cancelled")
            run_dir = os.path.join(out_root, RUN_ID)
            with open(os.path.join(run_dir, "junit.xml")) as fh:
                self.assertIn("<skipped", fh.read())
            with open(os.path.join(run_dir, "MANIFEST.md")) as fh:
                self.assertIn("outcome: cancelled", fh.read())
            commands = [
                w.decode("utf-8").split('"command":"')[1].split('"')[0]
                for w in source_wire.writes
            ]
            self.assertIn("stop", commands)
            self.assertIn("idle", commands)
            self.assertEqual(os.listdir(os.path.join(out_root, ".locks")), [])

    def test_cleanup_failure_changes_passing_row_to_failed(self):
        with tempfile.TemporaryDirectory() as td:
            out_root = os.path.join(td, "out")
            os.makedirs(out_root)
            cfg = os.path.join(td, "cfg")
            os.makedirs(cfg)
            fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
            repo_fake = os.path.join(td, "repo")
            os.makedirs(repo_fake)
            hil_fakes.make_images(repo_fake)
            junit = os.path.join(out_root, "%s.junit.xml" % RUN_ID)
            run_cmd = hil_fakes.ScriptedRunner()
            run_cmd.script_exit(["lsof", "--", "/dev/ttyACM1"], 1)
            run_cmd.script(
                ["fw-flash-hil-source"],
                hil_fakes.FakeProc(stdout="ok\n"),
            )
            run_cmd.script(["fw-flash-54l15"], hil_fakes.FakeProc(stdout="ok\n"))
            receiver_wire = _receiver_passing_wire(RUN_ID)
            source_t = hil_fakes.build_passing_source_wire(RUN_ID)
            src_chunks, src_writes = source_t.build()
            source_wire = hil_fakes.Wire(
                "source", chunks=src_chunks, assert_writes=src_writes
            )
            deps = make_runner_deps(
                receiver_wire,
                source_wire,
                run_cmd=run_cmd,
                repo_root=repo_fake,
            )

            # Force the source console close cleanup to fail after the row
            # otherwise passed: cleanup failure must flip the verdict.
            deps.serial_factory_orig = deps.serial_factory

            def failing_factory(role, path, baud, evidence_path, dtr, rts):
                console = deps.serial_factory_orig(
                    role, path, baud, evidence_path, dtr, rts
                )
                if role == "source":
                    real_close = console.close

                    def boom_close():
                        real_close()
                        raise SerialConsoleError("injected close failure")

                    console.close = boom_close
                return console

            deps.serial_factory = failing_factory
            engine = Runner(deps)
            result = engine.run(
                fixture_path,
                binding_path,
                out_root,
                RUN_ID,
                junit,
                argv=["hil-runner.py", "run"],
                status=0,
            )
            outcome, boundary, cleanup = result
            self.assertEqual(outcome, "failed", "cleanup failure forces failed")
            self.assertEqual([n for n, _ in cleanup], ["close source console"])
            run_dir = os.path.join(out_root, RUN_ID)
            with open(os.path.join(run_dir, "junit.xml")) as fh:
                junit_text = fh.read()
            self.assertIn("<failure", junit_text)
            self.assertIn("close source console", junit_text)
            with open(os.path.join(run_dir, "result.json")) as fh:
                result_json = json.load(fh)
            self.assertEqual(result_json["outcome"], "failed")
            self.assertTrue(result_json["cleanup_failures"])

    def test_receiver_tail_failure_drains_queued_pass_terminal_before_cleanup(self):
        row = rows.RH3_HEALTHY_ROWS[1]
        with tempfile.TemporaryDirectory() as td:
            receiver_wire = hil_fakes.Wire(
                "receiver",
                chunks=(
                    hil_fakes.receiver_boot_chunks(list(runner.RECEIVER_BOOT_MARKERS))
                    + [
                        hil_fakes.receiver_transcript(
                            "bt identity",
                            ["Identity: DB:A6:0C:05:A2:AA (random)"],
                        ),
                        hil_fakes.receiver_transcript(
                            "bt unpair",
                            [
                                "Pairing reset complete: bonds cleared; "
                                "BONDING advertising active."
                            ],
                        ),
                        hil_fakes.receiver_transcript("bt bonds", ["Bond count: 0"]),
                        hil_fakes.receiver_transcript(
                            "bt identity",
                            ["Identity: DB:A6:0C:05:A2:AA (random)"],
                        ),
                        hil_fakes.flpr_offload_transcript(),
                        hil_fakes.receiver_transcript(
                            "bt iso quality", ["ISO link quality unavailable: -128"]
                        ),
                        hil_fakes.audio_status_transcript(),
                        hil_fakes.audio_perf_transcript(),
                        hil_fakes.flpr_status_transcript(),
                    ]
                ),
                assert_writes=[
                    "bt identity\r",
                    "bt unpair\r",
                    "bt bonds\r",
                    "bt identity\r",
                    "flpr offload\r",
                    "bt iso quality\r",
                    "audio status\r",
                    "audio perf\r",
                    "flpr status\r",
                ],
            )

            source_t = hil_fakes.SourceTranscript(RUN_ID)
            source_t.hello(bond_count=0)
            source_t.idle()
            source_t.unpair()
            source_t.hello(bond_count=0)
            source_t.configure(row)
            source_t.start(
                scored=row.scored_sdu_count,
                row=row,
                omit_final_status=True,
            )
            idle_id = source_t._expect("idle")
            source_chunks, source_writes = source_t.build()
            idle_response = hil_fakes.hil_line(
                hil_fakes.source_record(
                    "status",
                    idle_id,
                    RUN_ID,
                    999,
                    0,
                    hil_fakes.source_status_data("idle"),
                )
            )
            source_wire = hil_fakes.Wire(
                "source",
                chunks=source_chunks,
                assert_writes=source_writes,
                write_responses=[(write, []) for write in source_writes[:-1]]
                + [(source_writes[-1], [idle_response])],
            )

            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )

            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "receiver tail")
            self.assertEqual(result[2], [])
            source_commands = [
                write.decode("utf-8").split('"command":"')[1].split('"')[0]
                for write in source_wire.writes
            ]
            self.assertNotIn("stop", source_commands)
            start_index = source_commands.index("start")
            post_start_commands = source_commands[start_index + 1 :]
            self.assertEqual(post_start_commands[-1:], ["idle"])
            self.assertEqual(post_start_commands.count("idle"), 1)
            with open(
                os.path.join(out_root, run_id, "source-records.jsonl"), encoding="utf-8"
            ) as fh:
                source_records = [
                    json.loads(line)["line"] for line in fh if line.strip()
                ]
            terminal_records = [
                source_client.protocol.parse_hil1_line(line)
                for line in source_records
                if line.startswith("HIL1 ")
            ]
            self.assertTrue(
                any(
                    record.kind == "terminal" and record.data.get("verdict") == "pass"
                    for record in terminal_records
                )
            )

    def test_receiver_tail_failure_queued_parse_error_fails_cleanup(self):
        row = rows.RH3_HEALTHY_ROWS[1]
        with tempfile.TemporaryDirectory() as td:
            receiver_wire = _receiver_passing_wire(
                RUN_ID,
                summary_lines=[
                    hil_fakes.receiver_stream_summary_line(slot=0),
                    hil_fakes.receiver_stream_summary_line(slot=1),
                ],
            )
            receiver_wire.chunks[6] = hil_fakes.receiver_transcript(
                "bt iso quality", ["ISO link quality unavailable: -128"]
            )

            source_t = hil_fakes.SourceTranscript(RUN_ID)
            source_t.hello(bond_count=0)
            source_t.idle()
            source_t.unpair()
            source_t.hello(bond_count=0)
            source_t.configure(row)
            source_t.start(
                scored=row.scored_sdu_count,
                row=row,
                omit_final_status=True,
            )
            source_chunks, source_writes = source_t.build()
            source_chunks.append(
                hil_fakes.hil_line(
                    hil_fakes.source_record(
                        "status",
                        "parse-error",
                        "unbound",
                        999,
                        0,
                        {
                            "command": "status",
                            "ok": False,
                            "error": "parse_error",
                            "parse_result": "syntax",
                        },
                    )
                )
            )
            source_wire = hil_fakes.Wire(
                "source", chunks=source_chunks, assert_writes=source_writes
            )

            result, out_root, run_id, _junit, _f, _b, _engine = _run_harness(
                td,
                receiver_wire=receiver_wire,
                source_wire=source_wire,
                row=row,
            )

            self.assertEqual(result[0], "failed")
            self.assertEqual(result[1], "receiver tail")
            self.assertEqual(len(result[2]), 1)
            self.assertEqual(result[2][0][0], "source stop/idle")
            self.assertIn("source diagnostic HIL1 record", result[2][0][1])
            source_commands = [
                write.decode("utf-8").split('"command":"')[1].split('"')[0]
                for write in source_wire.writes
            ]
            start_index = source_commands.index("start")
            self.assertEqual(source_commands[start_index + 1 :], ["status"])
            with open(
                os.path.join(out_root, run_id, "source-records.jsonl"), encoding="utf-8"
            ) as fh:
                source_records = [
                    json.loads(line)["line"] for line in fh if line.strip()
                ]
            diagnostic_records = [
                source_client.protocol.parse_hil1_line(line)
                for line in source_records
                if line.startswith("HIL1 ") and '"command_id":"parse-error"' in line
            ]
            self.assertEqual(len(diagnostic_records), 1)
            self.assertEqual(diagnostic_records[0].run_id, "unbound")
            self.assertEqual(
                diagnostic_records[0].data,
                {
                    "command": "status",
                    "ok": False,
                    "error": "parse_error",
                    "parse_result": "syntax",
                },
            )

    def test_receiver_post_stop_invalid_fails_row(self):
        with tempfile.TemporaryDirectory() as td:
            out_root = os.path.join(td, "out")
            os.makedirs(out_root)
            cfg = os.path.join(td, "cfg")
            os.makedirs(cfg)
            fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
            repo_fake = os.path.join(td, "repo")
            os.makedirs(repo_fake)
            hil_fakes.make_images(repo_fake)
            junit = os.path.join(out_root, "%s.junit.xml" % RUN_ID)
            run_cmd = hil_fakes.ScriptedRunner()
            run_cmd.script_exit(["lsof", "--", "/dev/ttyACM1"], 1)
            run_cmd.script(
                ["fw-flash-hil-source"],
                hil_fakes.FakeProc(stdout="ok\n"),
            )
            run_cmd.script(["fw-flash-54l15"], hil_fakes.FakeProc(stdout="ok\n"))
            # Receiver wire with a nonzero decode-error counter.
            wire = hil_fakes.Wire(
                "receiver",
                chunks=(
                    hil_fakes.receiver_boot_chunks(list(runner.RECEIVER_BOOT_MARKERS))
                    + [
                        hil_fakes.receiver_transcript(
                            "bt identity",
                            ["Identity: DB:A6:0C:05:A2:AA (random)"],
                        ),
                        hil_fakes.receiver_transcript(
                            "bt unpair",
                            [
                                "Pairing reset complete: bonds cleared; "
                                "BONDING advertising active."
                            ],
                        ),
                        hil_fakes.receiver_transcript("bt bonds", ["Bond count: 0"]),
                        hil_fakes.receiver_transcript(
                            "bt identity",
                            ["Identity: DB:A6:0C:05:A2:AA (random)"],
                        ),
                        hil_fakes.flpr_offload_transcript(),
                        hil_fakes.iso_link_quality_transcript(),
                        hil_fakes.receiver_stream_summary_line().encode("utf-8"),
                        hil_fakes.audio_status_transcript(decode=7),
                        hil_fakes.audio_perf_transcript(),
                        hil_fakes.flpr_offload_transcript(
                            state="STOPPED", submit=151, success=151, epoch=0, gen=2
                        ),
                        hil_fakes.flpr_status_transcript(),
                    ]
                ),
            )
            source_t = hil_fakes.build_passing_source_wire(RUN_ID)
            src_chunks, _src_writes = source_t.build()
            # The receiver post-stop failure interrupts the run, so the cleanup
            # issues stop/idle instead of the passing-row completion; disable
            # write-order assertions here.
            source_wire = hil_fakes.Wire("source", chunks=src_chunks, assert_writes=[])
            deps = make_runner_deps(
                wire,
                source_wire,
                run_cmd=run_cmd,
                repo_root=repo_fake,
            )
            engine = Runner(deps)
            result = engine.run(
                fixture_path,
                binding_path,
                out_root,
                RUN_ID,
                junit,
                argv=["hil-runner.py", "run"],
                status=0,
            )
            self.assertEqual(result[0], "failed")
            self.assertIn("receiver post-stop", result[1])
            self.assertTrue(
                os.path.isfile(
                    os.path.join(out_root, RUN_ID, "receiver-post-stop-status.txt")
                )
            )
            with open(
                os.path.join(out_root, RUN_ID, "result.json"), encoding="utf-8"
            ) as fh:
                result_json = json.load(fh)
            self.assertEqual(result_json["first_failed_boundary"], "receiver post-stop")
            self.assertIn("invalid receiver status", result_json["failure_detail"])
            with open(
                os.path.join(out_root, RUN_ID, "junit.xml"), encoding="utf-8"
            ) as fh:
                junit_text = fh.read()
            self.assertIn("failure detail: invalid receiver status", junit_text)


def _map_status(outcome):
    """Public CLI status mapping for one runner outcome."""
    if outcome == "passed":
        return 0
    if outcome == "cancelled":
        return 130
    return 1


class _Args:
    fixture = "f"
    binding = "b"
    output_root = "o"
    run_id = "r"
    junit = "j"


# ── CLI guards ─────────────────────────────────────────────────────


class TestCliRunGuard(unittest.TestCase):
    def test_run_requires_all_args_and_rejects_skip_flags(self):
        parser = cli.build_parser()
        # Missing required arguments are CLI usage errors (HilCliError,
        # formatted as hil-runner: error: by main()).
        with self.assertRaises(cli.HilCliError):
            parser.parse_args(["run", "--fixture", "x"])
        # Unknown skip/ignore/no-flash/erase/recovery flags are rejected
        # at parse time; the production run path has no such escape hatch.
        for flag in (
            "--skip-verify",
            "--ignore-warning",
            "--no-flash",
            "--erase",
            "--recover",
        ):
            with self.assertRaises(cli.HilCliError):
                parser.parse_args(
                    [
                        "run",
                        "--fixture",
                        "f",
                        "--binding",
                        "b",
                        "--output-root",
                        "o",
                        "--run-id",
                        "r",
                        "--junit",
                        "j",
                        flag,
                    ]
                )

    def test_hci_trace_flag_is_run_only_and_forwarded_as_true(self):
        parser = cli.build_parser()
        direct = parser.parse_args(
            [
                "run",
                "--fixture",
                "f",
                "--binding",
                "b",
                "--output-root",
                "o",
                "--run-id",
                "r",
                "--junit",
                "j",
                "--hci-remove-iso-path-trace",
            ]
        )
        self.assertTrue(direct.hci_remove_iso_path_trace)
        for command, extra in (
            ("run-rh3-matrix", []),
            (
                "run-rh4-matrix",
                ["--receiver-artifact", "r.zip", "--source-artifact", "s.zip"],
            ),
            ("run-ma1-matrix", ["--qualification", "q.json"]),
            ("run-sa1-matrix", ["--qualification", "q.json"]),
        ):
            with self.subTest(command=command):
                with self.assertRaises(cli.HilCliError):
                    parser.parse_args(
                        [
                            command,
                            "--fixture",
                            "f",
                            "--binding",
                            "b",
                            "--output-root",
                            "o",
                            "--run-id",
                            "r",
                            "--junit",
                            "j",
                            *extra,
                            "--hci-remove-iso-path-trace",
                        ]
                    )

        from unittest import mock

        calls = []

        class FakeEngine:
            def run(self, *args, **kwargs):
                calls.append((args, kwargs))
                return ("failed", "boundary", [])

        class TraceArgs(_Args):
            hci_remove_iso_path_trace = True

        with mock.patch.object(cli.runner, "RunnerDeps", return_value=object()):
            with mock.patch.object(cli.runner, "Runner", return_value=FakeEngine()):
                with mock.patch.object(cli.signal, "signal"):
                    self.assertEqual(cli.cmd_run(TraceArgs()), 1)
        self.assertTrue(calls[0][1]["hci_remove_iso_path_trace"])

    def test_sdc_trace_flag_is_run_only_forwarded_and_mutually_exclusive(self):
        parser = cli.build_parser()
        direct = parser.parse_args(
            [
                "run",
                "--fixture",
                "f",
                "--binding",
                "b",
                "--output-root",
                "o",
                "--run-id",
                "r",
                "--junit",
                "j",
                "--sdc-hci-remove-iso-path-trace",
            ]
        )
        self.assertTrue(direct.sdc_hci_remove_iso_path_trace)
        with self.assertRaises(cli.HilCliError):
            parser.parse_args(
                [
                    "run",
                    "--fixture",
                    "f",
                    "--binding",
                    "b",
                    "--output-root",
                    "o",
                    "--run-id",
                    "r",
                    "--junit",
                    "j",
                    "--hci-remove-iso-path-trace",
                    "--sdc-hci-remove-iso-path-trace",
                ]
            )
        for command, extra in (
            ("run-rh3-matrix", []),
            (
                "run-rh4-matrix",
                ["--receiver-artifact", "r.zip", "--source-artifact", "s.zip"],
            ),
            ("run-ma1-matrix", ["--qualification", "q.json"]),
            ("run-sa1-matrix", ["--qualification", "q.json"]),
        ):
            with self.subTest(command=command):
                with self.assertRaises(cli.HilCliError):
                    parser.parse_args(
                        [
                            command,
                            "--fixture",
                            "f",
                            "--binding",
                            "b",
                            "--output-root",
                            "o",
                            "--run-id",
                            "r",
                            "--junit",
                            "j",
                            *extra,
                            "--sdc-hci-remove-iso-path-trace",
                        ]
                    )

        from unittest import mock

        calls = []

        class FakeEngine:
            def run(self, *args, **kwargs):
                calls.append((args, kwargs))
                return ("failed", "boundary", [])

        class TraceArgs(_Args):
            sdc_hci_remove_iso_path_trace = True

        with mock.patch.object(cli.runner, "RunnerDeps", return_value=object()):
            with mock.patch.object(cli.runner, "Runner", return_value=FakeEngine()):
                with mock.patch.object(cli.signal, "signal"):
                    self.assertEqual(cli.cmd_run(TraceArgs()), 1)
        self.assertTrue(calls[0][1]["sdc_hci_remove_iso_path_trace"])
        self.assertFalse(calls[0][1]["hci_remove_iso_path_trace"])

    def test_cmd_run_status_mapping(self):
        # cmd_run maps the engine outcome to the process status (public
        # CLI boundary): 0 passed, 1 failed, 130 cancelled.
        from unittest import mock

        from hil import cli as cli_mod

        for outcome, expected_status in (
            ("passed", 0),
            ("failed", 1),
            ("cancelled", 130),
        ):
            calls = []

            class FakeEngine:
                def run(self, *args, **kwargs):
                    calls.append((args, kwargs))
                    return (outcome, "boundary-x", [("cleanup", "err")])

            with mock.patch.object(cli_mod.runner, "RunnerDeps", return_value=object()):
                with mock.patch.object(
                    cli_mod.runner, "Runner", return_value=FakeEngine()
                ):
                    with mock.patch.object(cli_mod.signal, "signal"):
                        status = cli_mod.cmd_run(_Args())
            self.assertEqual(status, expected_status, outcome)
            self.assertTrue(calls, "engine.run must be invoked once")
            args, kwargs = calls[0]
            self.assertEqual(args[0], "f")
            self.assertEqual(args[1], "b")
            self.assertEqual(args[2], "o")
            self.assertEqual(args[3], "r")
            self.assertEqual(args[4], "j")
            self.assertIn("argv", kwargs)

    def test_validate_and_prepare_outputs_unchanged(self):
        from hil import cli as cli_mod

        with tempfile.TemporaryDirectory() as td:
            cfg = os.path.join(td, "cfg")
            os.makedirs(cfg)
            fixture_path, binding_path = hil_fakes.write_fixture_binding(cfg)
            rc, out, err = _run_cli(
                ["validate", "--fixture", fixture_path, "--binding", binding_path]
            )
            self.assertEqual(rc, 0)
            self.assertEqual(err, "")
            self.assertEqual(
                json.loads(out),
                {"fixture_id": FIXTURE_ID, "capture_capability": "none"},
            )
            rc, out, err = _run_cli(
                [
                    "prepare",
                    "--fixture",
                    fixture_path,
                    "--binding",
                    binding_path,
                    "--output-root",
                    td,
                    "--run-id",
                    "rh0-check",
                ]
            )
            self.assertEqual(rc, 0)
            self.assertEqual(err, "")
            self.assertEqual(json.loads(out)["outcome"], "prepared")


def _run_cli(argv):
    import contextlib
    import io

    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = cli.main(argv)
    return rc, out.getvalue(), err.getvalue()


if __name__ == "__main__":
    unittest.main(verbosity=2)
