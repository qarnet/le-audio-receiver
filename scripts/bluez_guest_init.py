#!/usr/bin/env python3
"""Fixed PID1-only entrypoint for PB-053 private BlueZ guest."""

import ctypes
import errno
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import resource
import signal
import socket
import stat
import subprocess
import sys
import time
from types import SimpleNamespace

from bluez_guest_limits import read_bounded
from bluez_host_process import run_owned

from bluez_host_results import (
    decode_marker,
    validate_public,
    validate_capture,
    final_result,
    valid_boot_id,
    valid_run_id,
)


PUBLIC_MARKER = "PB053_PUBLIC_RESULT "
COMMAND_LOGS = itertools.count(1)
CHILD_LOG_CAP = 8 * 1024 * 1024


def guards():
    if (
        os.getpid() != 1
        or "pb053_guest=1" not in Path("/proc/cmdline").read_text().split()
    ):
        raise RuntimeError("Guest requires PID1 and pb053_guest=1")


def parse_run_tokens(cmdline):
    tokens = cmdline.split()
    run = [
        token.removeprefix("pb053_run=")
        for token in tokens
        if token.startswith("pb053_run=")
    ]
    scenarios = [
        token.removeprefix("pb053_scenario=")
        for token in tokens
        if token.startswith("pb053_scenario=")
    ]
    if (
        len(run) != 1
        or not valid_run_id(run[0])
        or len(scenarios) != 1
        or scenarios[0] not in ("normal", "hold")
    ):
        raise RuntimeError("Missing, repeated or invalid PB053 run/scenario token")
    return run[0], scenarios[0]


def event(stage, **detail):
    value = {"stage": stage, **detail}
    print(json.dumps(value), flush=True)
    return value


def owned_command(argv, env, timeout):
    log = Path(f"/tmp/pb053-command-{next(COMMAND_LOGS):03d}.log")
    record = run_owned(argv, log, timeout, max_log_bytes=2 * 1024 * 1024, env=env)
    output = read_bounded(log, 2 * 1024 * 1024).decode("utf-8", errors="replace")
    print(
        json.dumps(
            {
                "command": argv,
                "process": record,
                "log": str(log),
                "stdout": output,
            }
        ),
        flush=True,
    )
    return record, output


def command(argv, env=None, timeout=15):
    record, output = owned_command(argv, env, timeout)
    if not record["ok"]:
        raise RuntimeError(f"Command failed: {argv}: {record}")
    return output


def public_command(argv, env, timeout=90):
    """Retain child output and exit status even when public result fails."""
    record, output = owned_command(argv, env, timeout)
    return SimpleNamespace(
        returncode=record["returncode"], stdout=output, process=record
    )


def state_hashes(root):
    records = {}
    total = 0
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"Guest state symlink not allowed: {path}")
        mode = path.lstat().st_mode
        if stat.S_ISDIR(mode):
            continue
        if not stat.S_ISREG(mode):
            raise RuntimeError(f"Guest state special file not allowed: {path}")
        if len(records) >= 1024 or path.stat().st_size > 2 * 1024 * 1024:
            raise RuntimeError("Guest state quota exceeded")
        digest = hashlib.sha256()
        count = 0
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, "rb") as stream:
            if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                raise RuntimeError(f"Guest state file changed: {path}")
            while block := stream.read(65536):
                count += len(block)
                total += len(block)
                if count > 2 * 1024 * 1024 or total > 8 * 1024 * 1024:
                    raise RuntimeError("Guest state quota exceeded")
                digest.update(block)
        records[str(path.relative_to(root))] = digest.hexdigest()
    if not records:
        raise RuntimeError("Guest Bluetooth state is empty")
    return records


def name_has_owner(runtime, env):
    reply = command(
        [
            runtime["dbus_send"],
            "--system",
            "--print-reply",
            "--reply-timeout=2000",
            "--dest=org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus.NameHasOwner",
            "string:org.bluez",
        ],
        env,
    )
    matches = re.findall(r"\bboolean (true|false)\b", reply)
    if not reply.startswith("method return") or len(matches) != 1:
        raise RuntimeError("Invalid private bus NameHasOwner reply")
    return matches[0] == "true"


def start_child(label, argv, env, children, stages):
    log = Path("/tmp/pb053-" + label + ".log")
    if log.exists():
        raise RuntimeError(f"Guest log already exists: {log}")
    with log.open("x") as stream:
        proc = subprocess.Popen(
            argv,
            stdin=subprocess.DEVNULL,
            stdout=stream,
            stderr=subprocess.STDOUT,
            env=env,
        )
    children.append((label, proc))
    stages.append(event("started", process=label, pid=proc.pid, argv=argv))
    return proc


def monitor_ready(proc):
    log = Path("/tmp/pb053-monitor.log")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"monitor exited before readiness: {proc.returncode}")
        with log.open("rb") as stream:
            first = stream.readline(4097)
        if first.endswith(b"\n"):
            marker = "PB053_MONITOR_READY "
            if len(first) > 4096 or decode_marker(
                first.decode("utf-8"), marker, 4096
            ) != {"schema_version": 1}:
                raise RuntimeError("Monitor readiness invalid")
            if proc.poll() is not None:
                raise RuntimeError("Monitor exited at readiness")
            return
        time.sleep(0.05)
    raise RuntimeError("Monitor readiness timed out")


def daemon_ready(runtime, env, children, stages, active):
    deadline = time.monotonic() + 30
    count = 0
    while time.monotonic() < deadline:
        for label, proc in children:
            if label.startswith("bluez-") and label != active:
                continue
            if proc.poll() is not None:
                raise RuntimeError(
                    f"{label} exited before readiness: {proc.returncode}"
                )
        try:
            reply = command(
                [
                    runtime["dbus_send"],
                    "--system",
                    "--print-reply",
                    "--reply-timeout=2000",
                    "--dest=org.bluez",
                    "/",
                    "org.freedesktop.DBus.ObjectManager.GetManagedObjects",
                ],
                env,
            )
            paths = set(
                re.findall(
                    r'^\s*object path "(/org/bluez/hci[0-9]+)"', reply, re.MULTILINE
                )
            )
            count = sum(
                bool(
                    re.search(
                        r'object path "'
                        + re.escape(path)
                        + r'"(?:(?!object path).)*string "org.bluez.Adapter1"',
                        reply,
                        re.DOTALL,
                    )
                )
                for path in paths
            )
            if count == 2 and name_has_owner(runtime, env):
                stages.append(event("readiness", controllers=count, reply=reply))
                break
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            event("readiness_retry", error=str(exc))
        time.sleep(0.25)
    else:
        raise RuntimeError(f"Adapter readiness timed out: {count}")
    with socket.socket(socket.AF_BLUETOOTH, socket.SOCK_SEQPACKET, 8):
        pass
    stages.append(event("bluetooth_iso", operation="success"))
    return count


def stop_owned_actor(label, proc):
    prior_exit = proc.poll()
    if prior_exit is not None:
        raise RuntimeError(f"{label} exited before owned stop: {prior_exit}")
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired as exc:
        proc.kill()
        proc.wait(timeout=5)
        raise RuntimeError(f"{label} required forced kill") from exc
    if proc.returncode != 0:
        raise RuntimeError(f"{label} owned stop failed: {proc.returncode}")


def stop_daemon(label, proc, runtime, env, stages):
    stop_owned_actor(label, proc)
    if name_has_owner(runtime, env):
        raise RuntimeError(f"{label} stop/NameHasOwner failed: {proc.returncode}")
    stages.append(
        event(
            "daemon_stopped_" + label.removeprefix("bluez-"),
            pid=proc.pid,
            returncode=proc.returncode,
            name_has_owner=False,
        )
    )


def controller_indices(prior, sys_root=Path("/sys/class/bluetooth")):
    paths = prior.get("adapters")
    addresses = prior.get("addresses")
    if (
        not isinstance(paths, list)
        or len(paths) != 2
        or len(set(paths)) != 2
        or not isinstance(addresses, list)
        or len(addresses) != 2
        or any(
            not isinstance(address, str)
            or not re.fullmatch(r"[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}", address)
            for address in addresses
        )
    ):
        raise RuntimeError("Fresh adapter identities missing")
    indices = []
    for path in paths:
        if not isinstance(path, str) or not re.fullmatch(r"/org/bluez/hci[0-9]+", path):
            raise RuntimeError(f"Unexpected guest adapter path: {path}")
        index = path.removeprefix("/org/bluez/hci")
        if not (sys_root / ("hci" + index)).exists():
            raise RuntimeError(f"Guest HCI device absent: hci{index}")
        indices.append(int(index))
    return list(zip(indices, addresses))


def controller_info(reply, address):
    match = re.search(r"\baddr\s+([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})\b", reply)
    current = re.findall(r"(?im)^\s*current settings:\s*([^\n]*)$", reply)
    if (
        match is None
        or match.group(1).upper() != address.upper()
        or len(current) != 1
        or "powered" not in current[0].lower().split()
    ):
        raise RuntimeError("Guest controller info address/powered mismatch")
    return match.group(1).upper()


def prepower_controllers(phase, prior, runtime, env, stages):
    identities = controller_indices(prior)
    checked = []
    for index, expected_address in identities:
        actual = expected_address
        for operation in ("power", "info"):
            argv = [runtime["btmgmt"], "--timeout", "10", "--index", str(index)]
            argv.extend(("power", "on") if operation == "power" else ("info",))
            started = event(
                "controller_command_start",
                phase=phase,
                index=index,
                operation=operation,
                wall=time.time(),
                monotonic=time.monotonic(),
                argv=argv,
            )
            stages.append(started)
            reply = command(argv, env, timeout=15)
            stages.append(
                event(
                    "controller_command_reply",
                    phase=phase,
                    index=index,
                    operation=operation,
                    wall=time.time(),
                    monotonic=time.monotonic(),
                    reply=reply,
                )
            )
            if operation == "info":
                actual = controller_info(reply, expected_address)
        checked.append({"index": index, "address": actual, "powered": True})
    stages.append(
        event(
            "controller_ready",
            phase=phase,
            controllers=checked,
            indices=[item["index"] for item in checked],
            addresses=[item["address"] for item in checked],
            powered=True,
            wall=time.time(),
            monotonic=time.monotonic(),
        )
    )


def record_stopped(stages, label, proc):
    stages.append(
        event("stopped", process=label, pid=proc.pid, returncode=proc.returncode)
    )


def main():
    guards()
    run_id, scenario = parse_run_tokens(Path("/proc/cmdline").read_text())
    boot_id = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
    if not valid_boot_id(boot_id):
        raise RuntimeError("Invalid guest boot ID")
    stages = []
    children = []
    stopped = set()
    stage = "start"
    count = 0
    ok = False
    failure = None
    try:
        resource.setrlimit(resource.RLIMIT_FSIZE, (CHILD_LOG_CAP, CHILD_LOG_CAP))
        runtime = json.loads(Path("/opt/pb053/runtime.json").read_text())
        if os.uname().release != runtime["kernel"]:
            raise RuntimeError("Guest kernel version mismatch")
        stages.append(event("kernel", version=os.uname().release))
        for path in ("/run/dbus", "/tmp", "/var/lib/bluetooth"):
            Path(path).mkdir(parents=True, exist_ok=True)
        Path("/etc/pb053-bus.conf").write_text(
            "<busconfig><type>system</type><listen>unix:path=/run/dbus/system_bus_socket</listen>"
            '<auth>EXTERNAL</auth><policy user="root"><allow own="*"/><allow send_destination="*"/>'
            '<allow receive_sender="*"/></policy></busconfig>\n'
        )
        Path("/etc/pb053-bluez.conf").write_text(
            "[General]\nName=PB053 guest\nExperimental=true\nControllerMode=le\n"
        )
        env = dict(
            os.environ, DBUS_SYSTEM_BUS_ADDRESS="unix:path=/run/dbus/system_bus_socket"
        )
        stage = "modules"
        for module in (
            "hci_vhci",
            "algif_hash",
            "algif_skcipher",
            "cmac",
            "ecb",
            "aesni_intel",
            "aes",
        ):
            command([runtime["modprobe"], module], env)
        stages.append(event(stage))
        for stage, kind, name in (
            ("af_alg_ecb_aes", "skcipher", "ecb(aes)"),
            ("af_alg_cmac_aes", "hash", "cmac(aes)"),
        ):
            event(stage, operation="start")
            with socket.socket(socket.AF_ALG, socket.SOCK_SEQPACKET, 0) as sock:
                sock.bind((kind, name))
            stages.append(event(stage, operation="success"))
        stage = "children"
        for label, argv in (
            (
                "dbus",
                [
                    runtime["dbus"],
                    "--config-file=/etc/pb053-bus.conf",
                    "--nofork",
                    "--nopidfile",
                    "--nosyslog",
                ],
            ),
            (
                "monitor",
                [runtime["python"], "-u", "/opt/pb053/monitor.py"],
            ),
            ("emulator", [runtime["emulator"], "-d", "-l2"]),
        ):
            proc = start_child(label, argv, env, children, stages)
            if label == "monitor":
                stage = "monitor_readiness"
                monitor_ready(proc)
                stage = "children"
            if label == "dbus":
                stage = "bus_readiness"
                bus_deadline = time.monotonic() + 10
                while time.monotonic() < bus_deadline:
                    if proc.poll() is not None:
                        raise RuntimeError(
                            f"dbus exited before readiness: {proc.returncode}"
                        )
                    try:
                        reply = command(
                            [
                                runtime["dbus_send"],
                                "--system",
                                "--print-reply",
                                "--reply-timeout=2000",
                                "--dest=org.freedesktop.DBus",
                                "/org/freedesktop/DBus",
                                "org.freedesktop.DBus.ListNames",
                            ],
                            env,
                        )
                        if not reply.startswith("method return"):
                            raise RuntimeError("Bus ListNames did not return normally")
                        if proc.poll() is not None:
                            raise RuntimeError(
                                f"dbus exited at readiness: {proc.returncode}"
                            )
                        stages.append(event(stage, reply=reply))
                        break
                    except (RuntimeError, subprocess.TimeoutExpired) as exc:
                        event("bus_readiness_retry", error=str(exc))
                    time.sleep(0.25)
                else:
                    raise RuntimeError("Private bus readiness timed out")
                stage = "children"
        daemon_argv = [
            runtime["bluez"],
            "-d",
            "-n",
            "-E",
            "-K",
            "-p",
            "bap,a2dp",
            "-f",
            "/etc/pb053-bluez.conf",
        ]
        public_env = dict(env)
        public_env["PYTHONPATH"] = os.pathsep.join(
            (runtime["dbus_python"], runtime["gi_python"])
        )
        public_env["GI_TYPELIB_PATH"] = runtime["gi_typelib"]
        pids = set()
        retained_hashes = {}
        prior = None
        for label, mode in (
            ("fresh1", "fresh"),
            ("retained", "retained"),
            ("fresh2", "fresh"),
        ):
            daemon_label = "bluez-" + label
            if label != "fresh1":
                stage = "controller_ready_" + label
                if prior is None:
                    raise RuntimeError("Fresh controller identity missing")
                prepower_controllers(label, prior, runtime, env, stages)
            stage = "start_" + label
            daemon = start_child(daemon_label, daemon_argv, env, children, stages)
            if daemon.pid in pids:
                raise RuntimeError("Daemon PID reused")
            pids.add(daemon.pid)
            stage = "readiness_" + label
            count = daemon_ready(runtime, env, children, stages, daemon_label)
            if scenario == "hold":
                actors = {
                    name: proc.pid
                    for name, proc in children
                    if name in ("dbus", "monitor", "emulator", "bluez-fresh1")
                }
                print(
                    "PB053_HOLD_READY "
                    + json.dumps(
                        {
                            "run_id": run_id,
                            "boot_id": boot_id,
                            "actors": actors,
                        }
                    ),
                    flush=True,
                )
                while True:
                    for name, proc in children:
                        if proc.poll() is not None:
                            raise RuntimeError(
                                f"{name} exited while holding: {proc.returncode}"
                            )
                    time.sleep(1)
            stage = "public_" + label
            child = public_command(
                [runtime["python"], "-u", "/opt/pb053/public.py", "--state", mode],
                public_env,
            )
            result = decode_marker(child.stdout, PUBLIC_MARKER, 1024 * 1024)
            validate_public(
                result,
                mode,
                read_bounded("/opt/pb053/stimulus.lc3", 15360),
                require_success=False,
            )
            child_ok = child.process["ok"] and result["ok"] is True
            stages.append(
                event(
                    stage,
                    operation="success" if child_ok else "failure",
                    result=result,
                    child_exit_code=child.returncode,
                )
            )
            if not child_ok:
                raise RuntimeError(
                    f"Public {label} child failed: exit={child.returncode}, error={result['error']}"
                )
            if label == "fresh1":
                adapter_events = [
                    item for item in result["events"] if "adapters" in item
                ]
                peer_events = [item for item in result["events"] if "paired" in item]
                if len(adapter_events) != 1 or len(peer_events) != 1:
                    raise RuntimeError("Fresh public identity ledger missing")
                prior = {
                    "adapters": adapter_events[0]["adapters"],
                    "addresses": adapter_events[0]["addresses"],
                    "peers": [peer_events[0]["paired"], peer_events[0]["reciprocal"]],
                }
                Path("/opt/pb053/prior.json").write_text(json.dumps(prior) + "\n")
            if label != "fresh2":
                stage = "daemon_stopped_" + label
                stop_daemon(daemon_label, daemon, runtime, env, stages)
                stopped.add(daemon_label)
                if label == "fresh1":
                    stage = "state_preserved"
                    retained_hashes = state_hashes(Path("/var/lib/bluetooth"))
                    stages.append(event(stage, hashes=retained_hashes))
                else:
                    stage = "state_reset"
                    backup = Path("/var/lib/pb053-retained-bluetooth")
                    if backup.exists() or backup.is_symlink():
                        raise RuntimeError("Retained state backup already exists")
                    before_reset = state_hashes(Path("/var/lib/bluetooth"))
                    Path("/var/lib/bluetooth").rename(backup)
                    Path("/var/lib/bluetooth").mkdir()
                    if state_hashes(backup) != before_reset or any(
                        Path("/var/lib/bluetooth").iterdir()
                    ):
                        raise RuntimeError("Guest state reset/preservation failed")
                    stages.append(
                        event(
                            stage,
                            backup=str(backup),
                            hashes=before_reset,
                            original_hashes=retained_hashes,
                        )
                    )
        if len(pids) != 3:
            raise RuntimeError("Expected three distinct daemon PIDs")
        ok = True
    except Exception as exc:
        failure = str(exc)
        if stage in ("af_alg_ecb_aes", "af_alg_cmac_aes", "bluetooth_iso"):
            event(
                "socket_diagnostics",
                failed_stage=stage,
                proc_crypto=Path("/proc/crypto").read_text(errors="replace"),
                proc_modules=Path("/proc/modules").read_text(errors="replace"),
            )
        stages.append(event("failure", failed_stage=stage, error=str(exc)))
    finally:
        cleanup_order = sorted(
            children,
            key=lambda child: (
                0
                if child[0].startswith("bluez-")
                else 1
                if child[0] == "emulator"
                else 2
                if child[0] == "monitor"
                else 3
            ),
        )

        for label, proc in cleanup_order:

            def failed(message):
                nonlocal ok, failure
                ok = False
                if failure is None:
                    failure = f"{label}: {message}"
                stages.append(event("cleanup_failure", process=label, error=message))

            if label not in stopped:
                try:
                    stop_owned_actor(label, proc)
                except Exception as exc:
                    failed(f"TERM/wait: {exc}")
                    try:
                        if proc.poll() is None:
                            proc.kill()
                            proc.wait(timeout=5)
                    except Exception as kill_exc:
                        failed(f"KILL/wait: {kill_exc}")
            try:
                proc.poll()
                if proc.returncode != 0:
                    failed(f"Unexpected exit: {proc.returncode}")
                record_stopped(stages, label, proc)
            except Exception as exc:
                failed(f"Stop record: {exc}")
            log = Path("/tmp/pb053-" + label + ".log")
            try:
                if log.stat().st_size >= CHILD_LOG_CAP:
                    failed("Child log reached file limit")
            except OSError as exc:
                failed(f"Log stat: {exc}")
            if label == "monitor":
                try:
                    capture = validate_capture(
                        read_bounded(log, 4 * 1024 * 1024).decode("utf-8")
                    )
                    stages.append(
                        event(
                            "traffic_capture",
                            **{
                                key: capture[key]
                                for key in (
                                    "packets",
                                    "bytes",
                                    "sha256",
                                    "reported_drops",
                                    "opcode_counts",
                                )
                            },
                        )
                    )
                except (OSError, ValueError, UnicodeError) as exc:
                    failed(f"Monitor capture invalid: {exc}")
            try:
                if log.exists():
                    print(
                        json.dumps(
                            {
                                "process_log": label,
                                "content": read_bounded(log, CHILD_LOG_CAP).decode(
                                    "utf-8", errors="replace"
                                ),
                            }
                        ),
                        flush=True,
                    )
            except (OSError, ValueError) as exc:
                failed(f"Log read: {exc}")
        print(
            "PB053_GUEST_RESULT "
            + json.dumps(
                final_result(
                    ok,
                    os.uname().release,
                    count,
                    stages,
                    failure,
                    run_id,
                    boot_id,
                    scenario,
                )
            ),
            flush=True,
        )
        os.sync()
        ctypes.CDLL(None).reboot(0x4321FEDC)
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"PB053 guest refusal: {exc}", file=sys.stderr)
        sys.exit(1)
