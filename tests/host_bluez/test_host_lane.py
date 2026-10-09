"""Real isolated BlueZ guest CLI lane; never part of automatic unit discovery."""

import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

import pytest
from scripts.bluez_host_results import complete_marker_lines, validate_guest


REPO = Path(__file__).resolve().parents[2]
RUNNER = REPO / "scripts/bluez_host_guest.py"
STIMULUS = REPO / "tests/fixtures/lc3/bsim_48k_10ms_120b_l.lc3"
PHASES = ("fresh1", "retained", "fresh2")
SUFFIXES = ("normal", "sigint", "sigterm", "timeout", "repeat")


def live_group(pgid):
    for entry in Path("/proc").iterdir():
        if not entry.name.isdecimal():
            continue
        try:
            fields = (entry / "stat").read_text().rsplit(")", 1)[1].split()
            if int(fields[2]) == pgid and fields[0] not in ("Z", "X"):
                return True
        except (OSError, ValueError, IndexError):
            continue
    return False


def runner_qemu_pids(runner_pid):
    pids = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdecimal():
            continue
        try:
            fields = (entry / "stat").read_text().rsplit(")", 1)[1].split()
            cmd = (entry / "cmdline").read_bytes().replace(b"\0", b" ")
            if int(fields[1]) == runner_pid and b"qemu-system-x86_64" in cmd:
                pids.append((int(entry.name), cmd))
        except (OSError, ValueError, IndexError):
            continue
    return pids


def finish_runner(proc, output, known_qemu=None, captured=None):
    """Close only owned runner; verify identified QEMU group after its handler."""
    observed = {pid for pid, _ in runner_qemu_pids(proc.pid)}
    if known_qemu is not None:
        observed.add(known_qemu)
    errors = []
    stdout, stderr = captured if captured is not None else ("", "")
    try:
        if proc.poll() is None:
            proc.terminate()
        if captured is None:
            try:
                stdout, stderr = proc.communicate(timeout=15)
            except subprocess.TimeoutExpired:
                # Runner handler had its full grace; kill runner PID only, never guest/QEMU.
                proc.kill()
                stdout, stderr = proc.communicate(timeout=5)
                errors.append("Owned host runner did not stop after SIGTERM grace")
    except Exception as exc:
        errors.append(f"Host runner cleanup failed: {exc}")
    if output.is_dir():
        try:
            (output / "cli.stdout").write_text(stdout)
            (output / "cli.stderr").write_text(stderr)
        except OSError as exc:
            errors.append(f"CLI output retention failed: {exc}")
        record_path = output / "run-record.json"
        if record_path.is_file():
            try:
                pid = json.loads(record_path.read_text())["process"]["pid"]
                if pid is not None:
                    observed.add(pid)
            except (OSError, ValueError, KeyError, TypeError) as exc:
                errors.append(f"Run record ownership unreadable: {exc}")
    for pid in observed:
        if live_group(pid):
            errors.append(f"Live owned QEMU group remains: {pid}")
    if errors:
        active = sys.exc_info()[1]
        if active is not None:
            active.add_note("; ".join(errors))
        else:
            raise AssertionError("; ".join(errors))
    return stdout, stderr


def argv(output, timeout, scenario="normal"):
    return [
        sys.executable,
        str(RUNNER),
        "run",
        "--prepared",
        os.environ["PB053_PREPARED_ROOT"],
        "--output",
        str(output),
        "--timeout",
        str(timeout),
        "--manifest-sha256",
        os.environ["PB053_MANIFEST_SHA256"],
        "--scenario",
        scenario,
    ]


def load_record(output):
    record = json.loads((output / "run-record.json").read_text())
    serial = (output / "serial.log").read_bytes()
    process = record["process"]
    assert process["bytes_logged"] == len(serial)
    assert process["log_sha256"] == hashlib.sha256(serial).hexdigest()
    assert process["cleanup_errors"] == []
    assert process["log_limit_exceeded"] is False
    assert not live_group(process["pid"])
    return record


@pytest.fixture(scope="session")
def roots():
    base = Path(os.environ["PB053_SUITE_ROOT"])
    assert base.is_dir() and base.parent.is_dir()
    prepared = Path(os.environ["PB053_PREPARED_ROOT"]).resolve()
    outputs = {suffix: base.with_name(base.name + "-" + suffix) for suffix in SUFFIXES}
    for path in outputs.values():
        assert not path.exists() and not path.is_symlink(), (
            f"Preserved output exists: {path}"
        )
        assert (
            path.parent == base.parent
            and path != prepared
            and prepared not in path.parents
        )
    return outputs


def normal_run(output):
    proc = subprocess.Popen(
        argv(output, 240), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    captured = None
    try:
        captured = proc.communicate(timeout=260)
    finally:
        stdout, stderr = finish_runner(proc, output, captured=captured)
    assert proc.returncode == 0, (stdout, stderr)
    assert json.loads(stdout)["ok"] is True
    record = load_record(output)
    assert record["ok"] is True and record["scenario"] == "normal"
    assert record["manifest_sha256"] == os.environ["PB053_MANIFEST_SHA256"]
    assert record["process"]["ok"] is True and record["process"]["returncode"] == 0
    assert record["guest_result"]["ok"] is True
    assert record["guest_result"]["run_id"] == record["run_id"]
    validate_guest(
        record["guest_result"],
        STIMULUS.read_bytes(),
        record["run_id"],
        record["scenario"],
    )
    return record


@pytest.fixture(scope="session")
def normal(roots):
    return normal_run(roots["normal"])


def stage(record, name):
    matches = [s for s in record["guest_result"]["stages"] if s["stage"] == name]
    assert len(matches) == 1, (name, matches)
    return matches[0]


@pytest.mark.parametrize("phase", PHASES)
def test_public_phase(normal, phase):
    result = stage(normal, "public_" + phase)
    assert result["operation"] == "success" and result["child_exit_code"] == 0
    public = result["result"]
    assert public["ok"] is True and public["error"] is None
    assert public["cleanup_errors"] == []
    assert public["state"] == ("retained" if phase == "retained" else "fresh")
    assert set(public["cases"]) == {
        "state_initialization",
        "endpoint_registration",
        "discovery",
        "pairing",
        "endpoint_configuration",
        "iso_delivery",
    }
    assert all(value is True for value in public["cases"].values())


def test_daemon_restart_state_and_disappearance(normal):
    stages = normal["guest_result"]["stages"]
    names = [
        s["stage"]
        for s in stages
        if s["stage"]
        in (
            "public_fresh1",
            "daemon_stopped_fresh1",
            "state_preserved",
            "public_retained",
            "daemon_stopped_retained",
            "state_reset",
            "public_fresh2",
        )
    ]
    assert names == [
        "public_fresh1",
        "daemon_stopped_fresh1",
        "state_preserved",
        "public_retained",
        "daemon_stopped_retained",
        "state_reset",
        "public_fresh2",
    ]
    starts = {s["process"]: s["pid"] for s in stages if s["stage"] == "started"}
    pids = [starts["bluez-" + phase] for phase in PHASES]
    assert len(set(pids)) == 3
    for phase in ("fresh1", "retained"):
        stopped = stage(normal, "daemon_stopped_" + phase)
        assert stopped["pid"] == starts["bluez-" + phase]
        assert stopped["returncode"] == 0 and stopped["name_has_owner"] is False
    preserved = stage(normal, "state_preserved")["hashes"]
    reset = stage(normal, "state_reset")
    assert preserved and reset["original_hashes"] == preserved
    assert reset["backup"] == "/var/lib/pb053-retained-bluetooth"
    assert reset["hashes"]


def test_delivery_content_accounting(normal):
    data = STIMULUS.read_bytes()
    assert len(data) == 15360
    assert (
        hashlib.sha256(data).hexdigest()
        == "c16222f9d0e107488a1aec502d1bbb5a4c6e3944ce28b7f886c55415f51130be"
    )
    for phase in PHASES:
        events = stage(normal, "public_" + phase)["result"]["events"]
        frames = [event for event in events if "frame" in event]
        assert len(frames) == 16
        for i, frame in enumerate(frames):
            expected = data[i * 120 : (i + 1) * 120]
            assert frame["frame"] == i
            assert frame["sent"] == frame["received_len"] == len(expected) == 120
            assert frame["received_hex"] == expected.hex()
            assert frame["sha256"] == hashlib.sha256(expected).hexdigest()
            assert frame["flags"] == 0
    capture = stage(normal, "traffic_capture")
    assert capture["opcode_counts"]["18"] >= 48
    assert capture["opcode_counts"]["19"] >= 48
    assert capture["reported_drops"] in (None, 0)


def owned_qemu_pid(runner_pid, nonce):
    matches = runner_qemu_pids(runner_pid)
    assert len(matches) == 1
    assert ("pb053_run=" + nonce + " ").encode() in matches[0][1]
    return matches[0][0]


def hold_run(output, signum=None):
    proc = subprocess.Popen(
        argv(output, 60 if signum else 30, "hold"),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    qemu_pid = None
    nonce = None
    ready = None
    captured = None
    try:
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            assert proc.poll() is None, "Host CLI exited before hold readiness"
            serial = output / "serial.log"
            if serial.exists():
                content = serial.read_bytes()
                assert len(content) <= 33554432
                lines = complete_marker_lines(content, b"PB053_HOLD_READY ")
                assert len(lines) <= 1
                if lines:
                    ready = json.loads(lines[0].split(b" ", 1)[1])
                    nonce = ready["run_id"]
                    assert len(nonce) == 32 and ready["boot_id"]
                    assert len(set(ready["actors"].values())) == 4
                    qemu_pid = owned_qemu_pid(proc.pid, nonce)
                    break
            time.sleep(0.05)
        assert qemu_pid is not None, "No nonce-matched live hold readiness within 25s"
        assert ready is not None
        if signum:
            os.kill(proc.pid, signum)
        captured = proc.communicate(timeout=45 if signum is None else 15)
        stdout, stderr = captured
        assert proc.returncode != 0, (stdout, stderr)
        record = load_record(output)
        owner = record["process"]
        assert owner["pid"] == qemu_pid and record["run_id"] == nonce
        assert record["ok"] is False and owner["ok"] is False
        assert record["guest_result"] is None
        assert record["hold_ready"]["run_id"] == nonce
        assert record["hold_ready"]["boot_id"] == ready["boot_id"]
        assert owner["cancelled_signal"] == signum
        assert owner["timed_out"] is (signum is None)
        assert owner["error"] is None and not owner["descendant_cleanup_required"]
        return record
    finally:
        finish_runner(proc, output, qemu_pid, captured)


@pytest.mark.parametrize(
    "signum", [signal.SIGINT, signal.SIGTERM], ids=["SIGINT", "SIGTERM"]
)
def test_signal_cancellation(roots, signum):
    hold_run(roots["sigint" if signum == signal.SIGINT else "sigterm"], signum)


def test_timeout_owned_vm(roots):
    hold_run(roots["timeout"])


def test_fresh_guest_repetition(normal, roots):
    repeated = normal_run(roots["repeat"])
    for phase in PHASES:
        test_public_phase(repeated, phase)
    test_daemon_restart_state_and_disappearance(repeated)
    test_delivery_content_accounting(repeated)
    assert repeated["run_id"] != normal["run_id"]
    assert repeated["guest_result"]["boot_id"] != normal["guest_result"]["boot_id"]
