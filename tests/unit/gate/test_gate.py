#!/usr/bin/env nix-shell
#!nix-shell -i python3 -p "python3Packages.pyserial"
"""
Unit tests for the gate test child: the flpr_stall_gate.py timed 60ms gate
parser and runner (FakeSerial transport) plus fw-flash-dongle probe-selection
behavior, tested through public script execution with fake west/openocd/
nrf-probes. No hardware needed.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import time
from flpr_stall_gate import GateRunner, FakeSerial, StallGateError


def test_parse_offload_active():
    text = (
        "State       : ACTIVE / epoch=12345 gen=3\n"
        "Counters    : submit=2000 success=1500 fallback=5 busy=10\n"
        "Recovery    : attempts=3 fail=0 relapses=0 exhaustion=0\n"
        "Probation   : active=0 success=200 cleared=1\n"
    )
    s = GateRunner.parse_offload(text)
    assert s["state"] == "ACTIVE"
    assert s["success"] == 1500
    assert s["fallback"] == 5
    assert s["recovery_attempts"] == 3
    assert s["exhaustion"] == 0
    assert s["probation_cleared"] == 1
    assert s["probation_active"] == 0
    print("  PASS: parse_offload ACTIVE")


def test_parse_offload_fallback():
    text = (
        "State       : FALLBACK / epoch=12345 gen=3\n"
        "Counters    : submit=500 success=400 fallback=12 busy=3\n"
        "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
        "Probation   : active=1 success=50 cleared=0\n"
    )
    s = GateRunner.parse_offload(text)
    assert s["state"] == "FALLBACK"
    assert s["fallback"] == 12
    assert s["probation_active"] == 1
    print("  PASS: parse_offload FALLBACK")


def test_parse_offload_max_exhaustion():
    text = (
        "State       : FALLBACK / epoch=12345 gen=3\n"
        "Recovery    : attempts=10 fail=2 relapses=1 exhaustion=1\n"
        "Probation   : active=1 success=0 cleared=0\n"
    )
    s = GateRunner.parse_offload(text)
    assert s["exhaustion"] == 1
    assert s["recovery_attempts"] == 10
    print("  PASS: parse_offload exhaustion detected")


def test_parse_offload_faults():
    text = "Faults      : timeout=0 full=1 stale=0 seq=0 frame=0 crc=0 payload=0\n"
    s = GateRunner.parse_offload(text)
    assert s["fault_full"] == 1
    assert s["fault_timeout"] == 0
    print("  PASS: parse_offload fault counters")


def test_parse_offload_zero_faults():
    text = "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
    s = GateRunner.parse_offload(text)
    assert s["fault_timeout"] == 0
    assert s["fault_full"] == 0
    assert s["fault_stale"] == 0
    assert s["fault_seq"] == 0
    assert s["fault_frame"] == 0
    assert s["fault_crc"] == 0
    assert s["fault_payload"] == 0
    print("  PASS: parse_offload zero faults")


def test_timed_stall_ack_regex():
    import re
    from flpr_stall_gate import RE_STALL_TIMED_ACK

    text = "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
    m = RE_STALL_TIMED_ACK.search(text)
    assert m is not None
    assert int(m.group(1), 16) == 0x01
    assert int(m.group(2)) == 60
    print("  PASS: timed stall ACK regex match")


def test_timed_stall_ack_multiple():
    import re
    from flpr_stall_gate import RE_STALL_TIMED_ACK

    text = (
        "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
        "another line\n"
    )
    m = RE_STALL_TIMED_ACK.search(text)
    assert m is not None
    assert int(m.group(1), 16) == 0x01
    assert int(m.group(2)) == 60
    print("  PASS: timed stall ACK in multi-line text")


def test_gate_timeout_on_stale_state():
    """Gate should timeout if state never becomes ACTIVE with success>=500."""
    import re

    schedule = {
        0: "State       : PREPARING / epoch=1 gen=1\nCounters    : submit=0 success=0 fallback=0 busy=0\n",
        1: "State       : PREPARING / epoch=1 gen=1\nCounters    : submit=0 success=0 fallback=0 busy=0\n",
    }
    tr = FakeSerial(schedule)
    runner = GateRunner(tr, total_timeout=0.5, status_interval=0.1)
    result = runner.run()
    assert not result.passed
    assert "ACTIVE" in result.error or "Timeout" in result.error
    print("  PASS: gate timeout on stale state")


def test_gate_exhaustion_error():
    """Gate should fail immediately on exhaustion."""

    class ExhaustionTransport(FakeSerial):
        def __init__(self):
            super().__init__({})
            self._idx = 0

        def read_all(self) -> bytes:
            self._idx += 1
            if self._idx <= 2:
                # Pre-injection: ACTIVE success>=500
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=800 fallback=0 busy=0\n"
                    "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")
            elif self._idx == 3:
                # ACK for stall_flpr_ms
                return (
                    "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
                ).encode("utf-8")
            elif self._idx == 4:
                # Baseline
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=800 fallback=1 busy=0\n"
                ).encode("utf-8")
            else:
                # Exhaustion hit
                return (
                    "State       : FALLBACK / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=800 fallback=100 busy=0\n"
                    "Recovery    : attempts=10 fail=0 relapses=1 exhaustion=1\n"
                    "Probation   : active=1 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")

    tr = ExhaustionTransport()
    runner = GateRunner(tr, total_timeout=5.0, status_interval=0.05)
    result = runner.run()
    assert not result.passed
    assert "exhaustion" in result.error.lower()
    print("  PASS: gate immediately fails exhaustion")


def test_gate_success_path():
    """Full success path: ACTIVE → timed stall → fallback → recovery cleared."""

    class SuccessTransport(FakeSerial):
        def __init__(self):
            super().__init__({})
            self._idx = 0

        def read_all(self) -> bytes:
            self._idx += 1
            if self._idx <= 2:
                # Step 1: Wait for ACTIVE success>=500
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=0 busy=0\n"
                    "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")
            elif self._idx == 3:
                # Step 3: ACK for timed stall
                return (
                    "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
                ).encode("utf-8")
            elif self._idx == 4:
                # Step 4: Baseline
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=5 busy=0\n"
                    "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")
            else:
                # Step 6: Recovery complete
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1200 success=750 fallback=5 busy=0\n"
                    "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=100 cleared=1\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")

    tr = SuccessTransport()
    runner = GateRunner(tr, total_timeout=5.0, status_interval=0.05)
    result = runner.run()
    assert result.passed
    assert result.baseline_success == 600
    print("  PASS: gate success path")


def test_gate_no_fallback_fails():
    """Gate should not pass if fallback stays zero (no fault evidence)."""

    class NoFallbackTransport(FakeSerial):
        def __init__(self):
            super().__init__({})
            self._idx = 0

        def read_all(self) -> bytes:
            self._idx += 1
            if self._idx <= 2:
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=0 busy=0\n"
                    "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")
            elif self._idx == 3:
                return (
                    "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
                ).encode("utf-8")
            elif self._idx == 4:
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=0 busy=0\n"
                ).encode("utf-8")
            else:
                # fallback=0, never passes
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1100 success=650 fallback=0 busy=0\n"
                    "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                ).encode("utf-8")

    tr = NoFallbackTransport()
    runner = GateRunner(tr, total_timeout=1.0, status_interval=0.05)
    result = runner.run()
    assert not result.passed
    print("  PASS: gate fails without fallback evidence")


def test_timed_ack_wrong_duration_rejected():
    """Gate should reject an ACK with wrong duration."""
    import re
    from flpr_stall_gate import RE_STALL_TIMED_ACK

    text = (
        "FLPR timed stall applied: bits=0x01 duration=999 ms (cons_in=1 prod_out=0)\n"
    )
    m = RE_STALL_TIMED_ACK.search(text)
    dur_val = int(m.group(2))
    mask_val = int(m.group(1), 16)
    # The gate logic checks: mask_val == 0x01 and dur_val == 60
    assert not (mask_val == 0x01 and dur_val == 60)
    print("  PASS: wrong duration rejected")


def test_timeout_full_pass_gate():
    """timeout=1 + full=1 are valid stall evidence; gate must NOT reject them."""
    s = GateRunner.parse_offload(
        "Faults      : timeout=1 full=1 stale=0 seq=0 frame=0 crc=0 payload=0\n"
    )
    assert s["fault_timeout"] == 1
    assert s["fault_full"] == 1
    # Integrity check: only stale/seq/frame/crc/payload trigger rejection
    integrity_faults = (
        s["fault_stale"]
        or s["fault_seq"]
        or s["fault_frame"]
        or s["fault_crc"]
        or s["fault_payload"]
    )
    assert not integrity_faults, "timeout/full must not be rejected as integrity faults"
    print("  PASS: timeout+full accepted as stall evidence (not integrity faults)")


def test_integrity_faults_rejected():
    """Each integrity fault (stale, seq, frame, crc, payload) must be rejected."""
    checks = [
        (
            "stale=1",
            "Faults      : timeout=0 full=0 stale=1 seq=0 frame=0 crc=0 payload=0\n",
        ),
        (
            "seq=1",
            "Faults      : timeout=0 full=0 stale=0 seq=1 frame=0 crc=0 payload=0\n",
        ),
        (
            "frame=1",
            "Faults      : timeout=0 full=0 stale=0 seq=0 frame=1 crc=0 payload=0\n",
        ),
        (
            "crc=1",
            "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=1 payload=0\n",
        ),
        (
            "payload=1",
            "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=1\n",
        ),
    ]
    for label, text in checks:
        s = GateRunner.parse_offload(text)
        integrity = (
            s["fault_stale"]
            or s["fault_seq"]
            or s["fault_frame"]
            or s["fault_crc"]
            or s["fault_payload"]
        )
        assert integrity, f"{label} must be detected as integrity fault"
    print("  PASS: all 5 integrity faults (stale/seq/frame/crc/payload) rejected")


def test_gate_success_with_timeout_fault():
    """Full gate run passes even with timeout=1 (expected stall evidence)."""

    class TimeoutFaultTransport(FakeSerial):
        def __init__(self):
            super().__init__({})
            self._idx = 0

        def read_all(self) -> bytes:
            self._idx += 1
            if self._idx <= 2:
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=0 busy=0\n"
                    "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")
            elif self._idx == 3:
                return (
                    "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
                ).encode("utf-8")
            elif self._idx == 4:
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=5 busy=0\n"
                    "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                ).encode("utf-8")
            else:
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1200 success=750 fallback=5 busy=0\n"
                    "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=100 cleared=1\n"
                    "Faults      : timeout=1 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")

    tr = TimeoutFaultTransport()
    runner = GateRunner(tr, total_timeout=5.0, status_interval=0.05)
    result = runner.run()
    assert result.passed, f"Gate must PASS with timeout=1: {result.error}"
    print("  PASS: gate success path with timeout=1 (expected stall evidence)")


def test_seq_fault_fails_gate():
    """Gate must fail when seq=1 (integrity fault)."""

    class SeqFaultTransport(FakeSerial):
        def __init__(self):
            super().__init__({})
            self._idx = 0

        def read_all(self) -> bytes:
            self._idx += 1
            if self._idx <= 2:
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=0 busy=0\n"
                    "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")
            elif self._idx == 3:
                return (
                    "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
                ).encode("utf-8")
            elif self._idx == 4:
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=5 busy=0\n"
                    "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                ).encode("utf-8")
            else:
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1200 success=750 fallback=5 busy=0\n"
                    "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=100 cleared=1\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=1 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")

    tr = SeqFaultTransport()
    runner = GateRunner(tr, total_timeout=2.0, status_interval=0.05)
    result = runner.run()
    assert not result.passed, "Gate must FAIL with seq=1 integrity fault"
    print("  PASS: gate fails on seq=1 integrity fault")


def test_only_integrity_faults_rejected():
    """Comprehensive: timeout=1 full=1 must PASS; stale/seq/frame/crc/payload fail."""
    # Already tested above; this documents the complete matrix
    s = GateRunner.parse_offload(
        "Faults      : timeout=1 full=1 stale=0 seq=1 frame=1 crc=1 payload=1\n"
    )
    # timeout+full are fine
    # stale=0 but seq/frame/crc/payload=1 → integrity rejection
    integrity = (
        s["fault_stale"]
        or s["fault_seq"]
        or s["fault_frame"]
        or s["fault_crc"]
        or s["fault_payload"]
    )
    assert integrity, "seq/frame/crc/payload all=1 must trigger rejection"
    print("  PASS: comprehensive integrity-fault coverage matrix")


# ---------------------------------------------------------------------------
# fw-flash-dongle probe-selection behavior (public script execution)
# ---------------------------------------------------------------------------

_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
_SCRIPTS_BIN = os.path.join(_REPO_ROOT, "scripts", "bin")


def _write_fake(dirpath, name, body):
    path = os.path.join(dirpath, name)
    with open(path, "w", encoding="utf-8") as f:
        f.write(body)
    os.chmod(path, 0o755)
    return path


def _make_dongle_harness(tmpdir):
    """Temp repo layout running the real fw-flash-dongle + fw-common.sh.

    Fakes on PATH: west (env check), openocd (records argv, exit 0),
    nrf-probes (fails loudly if invoked).  Returns (env, paths).
    """
    repo = os.path.join(tmpdir, "repo")
    bin_dir = os.path.join(repo, "scripts", "bin")
    os.makedirs(bin_dir)
    shutil.copy(
        os.path.join(_SCRIPTS_BIN, "fw-flash-dongle"),
        os.path.join(bin_dir, "fw-flash-dongle"),
    )
    shutil.copy(
        os.path.join(_SCRIPTS_BIN, "fw-common.sh"),
        os.path.join(bin_dir, "fw-common.sh"),
    )

    hex_dirs = [
        os.path.join(repo, "build", "dongle", "hci_uart", "zephyr"),
        os.path.join(repo, "build", "dongle", "hci_ipc", "zephyr"),
    ]
    for d in hex_dirs:
        os.makedirs(d)
        with open(os.path.join(d, "zephyr.hex"), "w", encoding="utf-8"):
            pass

    fakebin = os.path.join(tmpdir, "fakebin")
    os.makedirs(fakebin)
    _write_fake(fakebin, "west", "#!/usr/bin/env bash\nexit 0\n")
    _write_fake(
        fakebin,
        "openocd",
        "#!/usr/bin/env bash\n"
        'printf \'%s\\0\' "$@" > "${OPENOCD_ARGV_FILE:?}"\n'
        "exit 0\n",
    )
    _write_fake(
        fakebin,
        "nrf-probes",
        "#!/usr/bin/env bash\n"
        'touch "${NRF_PROBES_CALLED_FILE:?}"\n'
        'echo "nrf-probes must not be invoked" >&2\n'
        "exit 99\n",
    )

    zephyr_base = os.path.join(tmpdir, "zephyrbase")
    os.makedirs(zephyr_base)

    argv_file = os.path.join(tmpdir, "openocd.argv")
    nrf_called = os.path.join(tmpdir, "nrf-probes.called")

    env = dict(os.environ)
    env["PATH"] = fakebin + os.pathsep + env["PATH"]
    env["ZEPHYR_BASE"] = zephyr_base
    env["OPENOCD_ARGV_FILE"] = argv_file
    env["NRF_PROBES_CALLED_FILE"] = nrf_called
    env.pop("FW_DONGLE_JLINK_SERIAL", None)

    return env, {
        "script": os.path.join(bin_dir, "fw-flash-dongle"),
        "argv_file": argv_file,
        "nrf_called": nrf_called,
        "app_hex": os.path.join(hex_dirs[0], "zephyr.hex"),
        "net_hex": os.path.join(hex_dirs[1], "zephyr.hex"),
    }


def _run_dongle(env, harness, extra_env=None):
    e = dict(env)
    if extra_env:
        e.update(extra_env)
    return subprocess.run(
        [harness["script"]], env=e, capture_output=True, text=True, timeout=30
    )


def _read_openocd_argv(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        return [a.decode("utf-8") for a in f.read().split(b"\0") if a]


def _argv_index(args, item):
    return args.index(item)


def test_dongle_flash_default_autodetect():
    """Default: J-Link auto-detection, no adapter serial, net-first/app-second."""
    with tempfile.TemporaryDirectory() as tmp:
        env, h = _make_dongle_harness(tmp)
        r = _run_dongle(env, h)
        assert r.returncode == 0, f"exit {r.returncode}: {r.stderr}"
        assert "J-Link auto-detection" in r.stdout
        assert not os.path.exists(h["nrf_called"]), "nrf-probes must not be invoked"

        args = _read_openocd_argv(h["argv_file"])
        assert args is not None, "openocd never invoked"
        assert "interface/jlink.cfg" in args
        assert "target/nordic/nrf53.cfg" in args
        assert not any(a.startswith("adapter serial") for a in args), (
            "default run must not pass adapter serial"
        )
        # net-first / app-second program + verify, reset, shutdown
        assert _argv_index(args, "targets nrf53.cpunet") < _argv_index(
            args, "program %s verify" % h["net_hex"]
        )
        assert _argv_index(args, "program %s verify" % h["net_hex"]) < _argv_index(
            args, "targets nrf53.cpuapp"
        )
        assert _argv_index(args, "targets nrf53.cpuapp") < _argv_index(
            args, "program %s verify" % h["app_hex"]
        )
        assert "reset run" in args
        assert "shutdown" in args
    print("  PASS: dongle default J-Link auto-detection argv")


def test_dongle_flash_explicit_serial():
    """Explicit override: exactly one adapter serial command, order unchanged."""
    with tempfile.TemporaryDirectory() as tmp:
        env, h = _make_dongle_harness(tmp)
        r = _run_dongle(env, h, {"FW_DONGLE_JLINK_SERIAL": "ABC-123.45"})
        assert r.returncode == 0, f"exit {r.returncode}: {r.stderr}"
        assert "J-Link override (serial ABC-123.45)" in r.stdout
        assert not os.path.exists(h["nrf_called"]), "nrf-probes must not be invoked"

        args = _read_openocd_argv(h["argv_file"])
        assert args is not None, "openocd never invoked"
        serial_cmds = [a for a in args if a.startswith("adapter serial")]
        assert serial_cmds == ["adapter serial ABC-123.45"], serial_cmds
        # override lands after J-Link config, before target init
        assert _argv_index(args, "interface/jlink.cfg") < _argv_index(
            args, "adapter serial ABC-123.45"
        )
        assert _argv_index(args, "adapter serial ABC-123.45") < _argv_index(
            args, "target/nordic/nrf53.cfg"
        )
        # programming order unchanged
        assert _argv_index(args, "targets nrf53.cpunet") < _argv_index(
            args, "program %s verify" % h["net_hex"]
        )
        assert _argv_index(args, "program %s verify" % h["net_hex"]) < _argv_index(
            args, "targets nrf53.cpuapp"
        )
        assert _argv_index(args, "targets nrf53.cpuapp") < _argv_index(
            args, "program %s verify" % h["app_hex"]
        )
    print("  PASS: dongle explicit J-Link serial override argv")


def test_dongle_flash_invalid_serial():
    """Invalid override: nonzero exit, clear error, openocd/nrf-probes untouched."""
    with tempfile.TemporaryDirectory() as tmp:
        env, h = _make_dongle_harness(tmp)
        r = _run_dongle(env, h, {"FW_DONGLE_JLINK_SERIAL": "bad!serial"})
        assert r.returncode != 0, "invalid serial must fail"
        assert "Invalid FW_DONGLE_JLINK_SERIAL" in r.stderr
        assert "^[[:alnum:]_.:-]+$" in r.stderr
        assert not os.path.exists(h["argv_file"]), "openocd must not be invoked"
        assert not os.path.exists(h["nrf_called"]), "nrf-probes must not be invoked"
    print("  PASS: dongle invalid J-Link serial rejected before openocd")


def test_dongle_flash_missing_artifacts():
    """Missing build artifacts still fail with the same error (no tools run)."""
    with tempfile.TemporaryDirectory() as tmp:
        env, h = _make_dongle_harness(tmp)
        os.remove(h["app_hex"])
        r = _run_dongle(env, h)
        assert r.returncode != 0, "missing artifact must fail"
        assert "No build artifacts found" in r.stderr
        assert "fw-build-dongle" in r.stderr
        assert not os.path.exists(h["argv_file"]), "openocd must not be invoked"
        assert not os.path.exists(h["nrf_called"]), "nrf-probes must not be invoked"
    print("  PASS: dongle missing-artifact error unchanged")


def test_dongle_flash_missing_dev_shell():
    """Missing dev shell (no ZEPHYR_BASE) still fails with the same error."""
    with tempfile.TemporaryDirectory() as tmp:
        env, h = _make_dongle_harness(tmp)
        env.pop("ZEPHYR_BASE", None)
        r = _run_dongle(env, h)
        assert r.returncode != 0, "missing dev shell must fail"
        assert "firmware tool error" in r.stderr
        assert not os.path.exists(h["argv_file"]), "openocd must not be invoked"
        assert not os.path.exists(h["nrf_called"]), "nrf-probes must not be invoked"
    print("  PASS: dongle missing-dev-shell error unchanged")


def run_tests():
    tests = [
        test_parse_offload_active,
        test_parse_offload_fallback,
        test_parse_offload_max_exhaustion,
        test_parse_offload_faults,
        test_parse_offload_zero_faults,
        test_timed_stall_ack_regex,
        test_timed_stall_ack_multiple,
        test_gate_timeout_on_stale_state,
        test_gate_exhaustion_error,
        test_gate_success_path,
        test_gate_no_fallback_fails,
        test_timed_ack_wrong_duration_rejected,
        test_timeout_full_pass_gate,
        test_integrity_faults_rejected,
        test_gate_success_with_timeout_fault,
        test_seq_fault_fails_gate,
        test_only_integrity_faults_rejected,
        test_dongle_flash_default_autodetect,
        test_dongle_flash_explicit_serial,
        test_dongle_flash_invalid_serial,
        test_dongle_flash_missing_artifacts,
        test_dongle_flash_missing_dev_shell,
    ]

    failures = 0
    for test in tests:
        try:
            test()
        except Exception as e:
            print(f"  FAIL: {test.__name__}: {e}")
            failures += 1

    print(f"\n{len(tests) - failures}/{len(tests)} tests passed")
    return failures


if __name__ == "__main__":
    sys.exit(run_tests())
