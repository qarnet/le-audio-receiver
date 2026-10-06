#!/usr/bin/env python3
"""Own complete PB-051 BSim matrix; never accept a partial family run."""

import argparse
import json
import os
import re
import selectors
import shutil
import signal
import subprocess
import sys
import time
import uuid
from pathlib import Path

from ascs_results import (
    BUILD_PROFILE,
    FAMILIES,
    IMAGE_LIMIT,
    LOG_LIMIT,
    POLICY_LIMIT,
    _regular_snapshot,
    check_execution_record,
    fields,
    image_abi,
    load_inventory,
    sha256,
    strict_json,
)
from bluez_host_descendants import DescendantScope, _stat
from bluez_host_guest import exclusive
from bluez_host_process import run_owned
from native_bsim_probes import inspect_configure_probes

REPO = Path(__file__).resolve().parent.parent
POLICY = REPO / "tests/ascs_bsim/cases.json"
ANCHOR = "addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c"
ROLES = ("receiver", "client", "phy")
COHORT_CAP = 16 * 1024 * 1024
BUILD_CAP = 64 * 1024 * 1024
WORKER_CAP = 64 * 1024
CONFIGURE_LOG_LIMIT = 16 * 1024 * 1024


def insist(value, message):
    if not value:
        raise ValueError(message)


def sdk_root():
    base = os.environ.get("ZEPHYR_BASE")
    insist(
        type(base) is str
        and Path(base).is_absolute()
        and Path(base).name == "zephyr"
        and Path(base).parent.name == "v3.4.1",
        "ZEPHYR_BASE must be absolute NCS v3.4.1/zephyr",
    )
    zephyr = Path(base).resolve(strict=True)
    insist(
        zephyr == Path(base) and (zephyr.parent / "nrf").is_dir(),
        "ZEPHYR_BASE not pinned installed NCS v3.4.1",
    )
    return zephyr.parent


def json_bytes(data, limit=2 * 1024 * 1024):
    raw = (json.dumps(data, sort_keys=True, indent=2, allow_nan=False) + "\n").encode()
    insist(len(raw) <= limit, "record exceeds bounded JSON limit")
    return raw


def write_json(path, data):
    raw = json_bytes(data)
    with Path(path).open("xb") as stream:
        stream.write(raw)


def replace_json(path, data):
    # Only caller's own new record may get one corrective cancellation seal.
    raw = json_bytes(data)
    with Path(path).open("r+b") as stream:
        stream.seek(0)
        stream.write(raw)
        stream.truncate()


def identity(raw, path):
    return {"path": str(path), "bytes": len(raw), "sha256": sha256(raw)}


def verify_image(path, expected, role):
    fields(expected, {"path", "bytes", "sha256"}, "image identity")
    insist(str(path) == expected["path"], "image path differs from job")
    raw = _regular_snapshot(str(path), IMAGE_LIMIT)
    image_abi(raw, role)
    insist(identity(raw, path) == expected, "copied image hash/size mismatch")
    return raw


def read_job(path, digest, kind):
    insist(
        isinstance(path, Path) and path.is_absolute() and kind in ("actor", "cohort"),
        "job path/kind invalid",
    )
    insist(
        type(digest) is str and re.fullmatch(r"[0-9a-f]{64}", digest),
        "missing independently supplied job digest",
    )
    raw = _regular_snapshot(str(path), 256 * 1024)
    insist(sha256(raw) == digest, "job digest mismatch")
    job = strict_json(raw, 256 * 1024)
    common = {
        "schema_version",
        "kind",
        "root",
        "family",
        "run_id",
        "images",
        "cwd",
        "timeout",
    }
    fields(
        job,
        common
        | (
            {"role", "argv", "log", "record"}
            if kind == "actor"
            else {"jobs", "execution_record"}
        ),
        "internal job",
    )
    insist(
        type(job["schema_version"]) is int
        and job["schema_version"] == 1
        and job["kind"] == kind
        and type(job["family"]) is str
        and job["family"] in dict((v[0], 1) for v in FAMILIES)
        and type(job["run_id"]) is str
        and re.fullmatch(r"[0-9a-f]{32}", job["run_id"])
        and type(job["timeout"]) is int
        and 30 <= job["timeout"] <= 600,
        "job family/run/timeout invalid",
    )
    insist(
        type(job["root"]) is str and type(job["cwd"]) is str,
        "job root/cwd paths must be strings",
    )
    root = Path(job["root"])
    insist(
        root.is_absolute()
        and root.is_dir()
        and not root.is_symlink()
        and root == root.resolve(strict=True)
        and root.parent.name == "families"
        and root.name == job["family"]
        and path.parent == root,
        "job not owned by exact family directory",
    )
    insist(
        Path(job["cwd"]).is_absolute()
        and Path(job["cwd"]) == Path(os.environ["BSIM_OUT_PATH"]) / "bin"
        and Path(job["cwd"]).is_dir(),
        "job runtime cwd not installed BSim bin",
    )
    insist(
        type(job["images"]) is dict and set(job["images"]) == set(ROLES),
        "job missing required images",
    )
    for role in ROLES:
        image = root.parent.parent / "images" / f"{role}.elf"
        fields(job["images"][role], {"path", "bytes", "sha256"}, "job image")
        desc = job["images"][role]
        insist(
            type(desc["path"]) is str
            and type(desc["bytes"]) is int
            and type(desc["sha256"]) is str
            and re.fullmatch(r"[0-9a-f]{64}", desc["sha256"]),
            "job image identity types invalid",
        )
        insist(job["images"][role]["path"] == str(image), "image not owned by root")
        verify_image(image, job["images"][role], role)
    if kind == "actor":
        insist(
            type(job["role"]) is str
            and job["role"] in ROLES
            and type(job["argv"]) is list
            and all(type(arg) is str for arg in job["argv"])
            and type(job["log"]) is str
            and type(job["record"]) is str,
            "actor job fields/types invalid",
        )
        insist(
            job["argv"] == expected_argv(job, job["role"])
            and job["log"] == str(root / f"{job['role']}.log")
            and job["record"] == str(root / f"{job['role']}-process.json"),
            "actor job paths/argv not fixed",
        )
    else:
        insist(
            type(job["jobs"]) is dict
            and set(job["jobs"]) == set(ROLES)
            and type(job["execution_record"]) is str
            and job["execution_record"] == str(root / "family-execution-record.json"),
            "cohort job fields/record path invalid",
        )
    return job


def expected_argv(job, role):
    session = f"ascs_{job['run_id']}_{job['family']}"
    args = [job["images"][role]["path"], "-v=2", f"-s={session}"]
    if role == "phy":
        # -nodump disables the installed PHY's default CSV dumps (p2G4_main.c
        # open_dump_files when dont_dump==0); no new SDK artifact needed.
        return args + ["-D=2", "-nodump", "-sim_length=250e6"]
    return args + [
        f"-d={0 if role == 'receiver' else 1}",
        f"-testid={job['family']}",
        "-RealEncryption=1",
        f"-rs={23 if role == 'receiver' else 28}",
    ]


def owned_command(argv, root, name, timeout, cap, *, cwd=None, cancel=None):
    root = Path(root)
    if cancel is not None:
        cancel.check()
    result = run_owned(argv, root / f"{name}.log", timeout, cap, cwd=cwd)
    write_json(root / f"{name}.json", result)
    if cancel is not None:
        cancel.latch(result["cancelled_signal"])
        cancel.check()
    insist(result["ok"], f"owned command failed: {name}: {result}")
    return result


def actor(path, digest):
    job = read_job(path, digest, "actor")
    root = Path(job["root"])
    role = job["role"]
    insist(
        type(role) is str and role in ROLES and path == root / f"{role}-job.json",
        "actor role/path mismatch",
    )
    insist(
        job["argv"] == expected_argv(job, role), "actor argv differs from fixed family"
    )
    insist(
        job["log"] == str(root / f"{role}.log")
        and job["record"] == str(root / f"{role}-process.json"),
        "actor log/record not family-owned",
    )
    with Cancel() as cancel:
        record = run_owned(
            job["argv"], job["log"], job["timeout"], LOG_LIMIT, cwd=job["cwd"]
        )
        cancel.latch(record["cancelled_signal"])
        if cancel.signal is not None:
            record["ok"] = False
            record["cancelled_signal"] = cancel.signal
        write_json(job["record"], record)
        if cancel.signal is not None and record["cancelled_signal"] is None:
            record["ok"] = False
            record["cancelled_signal"] = cancel.signal
            replace_json(job["record"], record)
        return 0 if record["ok"] and cancel.signal is None else 1


class Cancel:
    def __init__(self):
        self.signal = None
        self.saved = {}

    def catch(self, signum, _frame):
        self.latch(signum)

    def latch(self, signum):
        if signum is not None and self.signal is None:
            insist(
                type(signum) is int and signum in (signal.SIGINT, signal.SIGTERM),
                "unexpected cancellation signal",
            )
            self.signal = signum

    def __enter__(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            self.saved[signum] = signal.signal(signum, self.catch)
        return self

    def __exit__(self, *_):
        for signum, old in self.saved.items():
            signal.signal(signum, old)

    def reinstall(self):
        # DescendantScope.__enter__ installs a temporary owner; preserve our
        # original saved handlers, restoring only this body without nesting.
        for signum in self.saved:
            signal.signal(signum, self.catch)

    def check(self):
        insist(self.signal is None, f"operation cancelled by signal {self.signal}")


def signal_worker(proc, birth, pidfd, signum):
    state = _stat(proc.pid)
    if (
        proc.poll() is None
        and state is not None
        and state[0] == os.getpid()
        and (birth is None or state[1] == birth)
    ):
        if pidfd is not None:
            signal.pidfd_send_signal(pidfd, signum)
        else:
            proc.send_signal(signum)


def worker_cohort(
    commands, output, timeout, cancel, *, diagnostic=None, _test_deadline=None
):
    """Own concurrent ordinary worker subprocesses, including failures."""
    insist(len(commands) == 3 and 30 <= timeout <= 600, "invalid cohort")
    output = Path(output)
    processes = {}
    selector = selectors.DefaultSelector()
    error = None
    failure = None
    cleanup_faults = []
    first_cause = None
    started_at = time.time()
    if diagnostic is not None:
        diagnostic.update(
            {
                "started_at": started_at,
                "ended_at": None,
                "first_cause": None,
                "cancelled_signal": None,
                "timed_out": False,
                "error": None,
                "cleanup_faults": [],
                "workers": {},
            }
        )

    def cause(value):
        nonlocal first_cause
        if first_cause is None:
            first_cause = value

    deadline = time.monotonic() + (
        timeout if _test_deadline is None else _test_deadline
    )
    term_at = None
    kill_at = None
    drained_at = None
    starting = True
    try:
        for role in ROLES:
            cancel.check()
            proc = subprocess.Popen(
                commands[role],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                start_new_session=True,
                close_fds=True,
            )
            processes[role] = [proc, None, None, bytearray()]
            if diagnostic is not None:
                diagnostic["workers"][role] = {
                    "pid": proc.pid,
                    "start_ticks": None,
                    "observed_exit": None,
                    "log_bytes": 0,
                    "log_sha256": None,
                    "log_limit_exceeded": False,
                    "cleanup_faults": [],
                }
            state = _stat(proc.pid)
            insist(
                state is not None and state[0] == os.getpid(),
                "worker birth identity unavailable",
            )
            processes[role][1] = state[1]
            if diagnostic is not None:
                diagnostic["workers"][role]["start_ticks"] = state[1]
            pidfd = os.pidfd_open(proc.pid)
            processes[role] = [proc, state[1], pidfd, bytearray()]
            assert proc.stdout is not None
            os.set_blocking(proc.stdout.fileno(), False)
            selector.register(proc.stdout, selectors.EVENT_READ, role)
        starting = False
        while True:
            now = time.monotonic()
            if cancel.signal is not None and error is None:
                error = f"cohort cancelled by signal {cancel.signal}"
                cause("cancelled")
            if now >= deadline and error is None:
                error = "cohort deadline reached"
                cause("deadline")
            for role, (proc, _, _, _) in processes.items():
                if proc.poll() not in (None, 0) and error is None:
                    error = f"{role} worker failed: {proc.returncode}"
                    cause("worker_failure")
            if error is not None and term_at is None:
                term_at = now + 6
                for proc, birth, pidfd, _ in processes.values():
                    signal_worker(proc, birth, pidfd, signal.SIGTERM)
            exited = all(
                proc.poll() is not None for proc, _, _, _ in processes.values()
            )
            if exited and error is None:
                # A worker can exit between the failure poll above and this
                # one; re-evaluate exit codes before the break so the first
                # detected cause is always the real worker failure, never
                # the generic post-break diagnostic.
                for role, (proc, _, _, _) in processes.items():
                    if proc.returncode not in (None, 0) and error is None:
                        error = f"{role} worker failed: {proc.returncode}"
                        cause("worker_failure")
            if exited and not selector.get_map():
                break
            if exited and drained_at is None:
                drained_at = now + 2
            if exited and selector.get_map() and now >= drained_at:
                if error is None:
                    error = "worker inherited pipe remained after leader exit"
                    cause("final_cleanup")
                break
            if term_at is not None and kill_at is None and now >= term_at:
                for proc, birth, pidfd, _ in processes.values():
                    signal_worker(proc, birth, pidfd, signal.SIGKILL)
                kill_at = now + 1
            if kill_at is not None and now >= kill_at:
                if error is None:
                    error = "worker remains after SIGKILL/grace"
                    cause("final_cleanup")
                break
            for key, _ in selector.select(0.05):
                role = key.data
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    selector.unregister(key.fileobj)
                else:
                    buf = processes[role][3]
                    buf.extend(chunk[: max(0, WORKER_CAP + 1 - len(buf))])
                    if len(buf) > WORKER_CAP and error is None:
                        error = f"{role} worker output quota exceeded"
                        cause("output_quota")
        insist(
            not error
            and len(processes) == 3
            and all(p[0].returncode == 0 for p in processes.values()),
            error or "missing/failing worker",
        )
    except BaseException as exc:
        failure = exc
        if first_cause is None:
            cause(
                "cancelled"
                if cancel.signal is not None
                else "spawn_or_identity"
                if starting
                else "final_cleanup"
            )
    finally:
        for role, (proc, birth, pidfd, buf) in processes.items():
            faults = []
            try:
                signal_worker(proc, birth, pidfd, signal.SIGKILL)
            except BaseException as exc:
                faults.append(f"{role} signal: {type(exc).__name__}: {exc}")
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                faults.append(f"unreaped worker {role}")
            except BaseException as exc:
                faults.append(f"{role} wait: {type(exc).__name__}: {exc}")
            try:
                with (output / f"{role}-worker.log").open("xb") as stream:
                    stream.write(buf[:WORKER_CAP])
            except BaseException as exc:
                faults.append(f"{role} log: {type(exc).__name__}: {exc}")
            try:
                if proc.stdout is not None:
                    proc.stdout.close()
                if pidfd is not None:
                    os.close(pidfd)
            except BaseException as exc:
                faults.append(f"{role} descriptors: {type(exc).__name__}: {exc}")
            cleanup_faults.extend(faults)
            if diagnostic is not None:
                worker = diagnostic["workers"][role]
                worker.update(
                    {
                        "observed_exit": proc.returncode,
                        "log_bytes": min(len(buf), WORKER_CAP),
                        "log_sha256": sha256(bytes(buf[:WORKER_CAP])),
                        "log_limit_exceeded": len(buf) > WORKER_CAP,
                        "cleanup_faults": faults,
                    }
                )
        try:
            selector.close()
        except BaseException as exc:
            cleanup_faults.append(f"selector close: {type(exc).__name__}: {exc}")
        if cancel.signal is not None:
            cause("cancelled")
        if cleanup_faults:
            cause("final_cleanup")
        if diagnostic is not None:
            diagnostic.update(
                {
                    "ended_at": time.time(),
                    "first_cause": first_cause,
                    "cancelled_signal": cancel.signal,
                    "timed_out": first_cause == "deadline",
                    "cleanup_faults": cleanup_faults[:],
                    "error": (
                        str(failure)
                        if failure is not None
                        else f"worker cleanup faults: {cleanup_faults}"
                        if cleanup_faults
                        else None
                    ),
                }
            )
    if cleanup_faults:
        raise RuntimeError(
            f"worker failure: {failure}; cleanup faults: {cleanup_faults}"
        ) from failure
    if failure is not None:
        raise failure


def cohort(path, digest):
    job = read_job(path, digest, "cohort")
    root = Path(job["root"])
    insist(
        path == root / "cohort-job.json"
        and job["execution_record"] == str(root / "family-execution-record.json")
        and type(job["jobs"]) is dict
        and set(job["jobs"]) == set(ROLES),
        "invalid cohort job paths",
    )
    scope = DescendantScope()
    workers = {}
    outcome = {
        "schema_version": 1,
        "accepted": False,
        "family": job["family"],
        "run_id": job["run_id"],
        "images": job["images"],
        "error": None,
        "errors": [],
        "cancelled_signal": None,
        "timed_out": False,
        "worker": workers,
        "scope": None,
        "claim": "owned-cohort-only; protocol checker separately required",
    }
    failure = None
    with Cancel() as cancel:
        try:
            commands = {}
            for role in ROLES:
                descriptor = job["jobs"][role]
                fields(descriptor, {"path", "sha256"}, "actor job descriptor")
                location = root / f"{role}-job.json"
                insist(descriptor["path"] == str(location), "actor job outside cohort")
                worker = read_job(location, descriptor["sha256"], "actor")
                insist(
                    worker["role"] == role
                    and all(
                        worker[key] == job[key]
                        for key in ("run_id", "family", "images", "cwd", "timeout")
                    ),
                    "actor and cohort identities differ",
                )
                commands[role] = [
                    sys.executable,
                    str(Path(__file__).resolve()),
                    "--_actor",
                    str(location),
                    "--_job-sha",
                    descriptor["sha256"],
                ]
            cancel.check()
            with scope:
                cancel.reinstall()
                worker_cohort(
                    commands, root, job["timeout"], cancel, diagnostic=workers
                )
                cancel.check()
            cancel.latch(scope.record["cancelled_signal"])
            cancel.check()
            insist(scope.record["ok"], "cohort descendant scope failed")
            seal_family(job, scope.record)
            cancel.check()
            outcome["accepted"] = True
        except BaseException as exc:
            failure = exc
            outcome["error"] = f"{type(exc).__name__}: {exc}"
            outcome["errors"].append(outcome["error"])
        finally:
            cancel.latch(scope.record["cancelled_signal"])
            outcome["scope"] = scope.record
            outcome["cancelled_signal"] = cancel.signal
            outcome["timed_out"] = workers.get("timed_out", False)
            if "first_cause" not in workers:
                workers["first_cause"] = (
                    (
                        "cancelled"
                        if cancel.signal is not None
                        else "spawn_or_identity"
                        if not scope.record["ok"] and scope.record["saved_flag"] is None
                        else "final_cleanup"
                    )
                    if failure is not None
                    else None
                )
            elif failure is not None and workers["first_cause"] is None:
                workers["first_cause"] = (
                    "cancelled" if cancel.signal is not None else "final_cleanup"
                )
            if not scope.record["ok"] or cancel.signal is not None:
                outcome["accepted"] = False
            publish_cohort(root, outcome, cancel)
    if failure is not None:
        raise failure
    return 0 if outcome["accepted"] else 1


def publish_cohort(root, outcome, cancel):
    """Seal actual cohort diagnostic, correcting one in-flight signal."""

    def mark_cancelled():
        if cancel.signal is not None:
            outcome["accepted"] = False
            outcome["cancelled_signal"] = cancel.signal
            if outcome["worker"].get("first_cause") is None:
                outcome["worker"]["first_cause"] = "cancelled"
            if outcome["error"] is None:
                outcome["error"] = f"cohort cancelled by signal {cancel.signal}"
                outcome["errors"].append(outcome["error"])

    mark_cancelled()
    location = root / "cohort-result.json"
    write_json(location, outcome)
    if cancel.signal is not None and outcome["cancelled_signal"] is None:
        mark_cancelled()
        replace_json(location, outcome)


def seal_family(job, scope_record):
    """Seal only complete checked child evidence; no missing-record defaults."""
    root = Path(job["root"])
    insist(scope_record["ok"] is True, "cohort scope not clean")
    participants = {}
    logs = {}
    for role in ROLES:
        worker = read_job(
            root / f"{role}-job.json", job["jobs"][role]["sha256"], "actor"
        )
        participants[role] = strict_json(
            _regular_snapshot(worker["record"], 256 * 1024), 256 * 1024
        )
        logs[role] = identity(
            _regular_snapshot(worker["log"], LOG_LIMIT), worker["log"]
        )
    record = {
        "schema_version": 1,
        "run_id": job["run_id"],
        "family": job["family"],
        "images": job["images"],
        "participants": participants,
        "logs": logs,
        "scope": scope_record,
    }
    check_execution_record(
        json.dumps(record).encode(),
        load_inventory(_regular_snapshot(str(POLICY), POLICY_LIMIT), ANCHOR),
        job["family"],
        job["run_id"],
        logs["client"]["path"],
        logs["receiver"]["path"],
    )
    write_json(job["execution_record"], record)
    return 0


def verify_cohort_result(area, family, run_id, images):
    """Confirm real cohort ownership before invoking protocol checker."""
    result = strict_json(
        _regular_snapshot(str(area / "cohort-result.json"), 2 * 1024 * 1024),
        2 * 1024 * 1024,
    )
    fields(
        result,
        {
            "schema_version",
            "accepted",
            "family",
            "run_id",
            "images",
            "error",
            "errors",
            "cancelled_signal",
            "timed_out",
            "worker",
            "scope",
            "claim",
        },
        "owned cohort result",
    )
    worker = result["worker"]
    fields(
        worker,
        {
            "started_at",
            "ended_at",
            "first_cause",
            "cancelled_signal",
            "timed_out",
            "error",
            "cleanup_faults",
            "workers",
        },
        "worker cohort diagnostic",
    )
    insist(
        type(worker["workers"]) is dict and set(worker["workers"]) == set(ROLES),
        "missing worker diagnostics",
    )
    for role in ROLES:
        entry = worker["workers"][role]
        fields(
            entry,
            {
                "pid",
                "start_ticks",
                "observed_exit",
                "log_bytes",
                "log_sha256",
                "log_limit_exceeded",
                "cleanup_faults",
            },
            f"{role} worker diagnostic",
        )
        insist(
            type(entry["pid"]) is int
            and entry["pid"] > 0
            and type(entry["start_ticks"]) is int
            and entry["start_ticks"] > 0
            and type(entry["observed_exit"]) is int
            and entry["observed_exit"] == 0
            and type(entry["log_bytes"]) is int
            and 0 <= entry["log_bytes"] <= WORKER_CAP
            and type(entry["log_sha256"]) is str
            and re.fullmatch(r"[0-9a-f]{64}", entry["log_sha256"])
            and entry["log_limit_exceeded"] is False
            and entry["cleanup_faults"] == [],
            f"{role} worker owner incomplete",
        )
    insist(
        type(result["schema_version"]) is int
        and result["schema_version"] == 1
        and result["accepted"] is True
        and result["family"] == family
        and result["run_id"] == run_id
        and result["images"] == images
        and result["claim"] == "owned-cohort-only; protocol checker separately required"
        and result["error"] is None
        and result["errors"] == []
        and result["cancelled_signal"] is None
        and result["timed_out"] is False
        and result["scope"]["ok"] is True
        and worker["first_cause"] is None
        and worker["cancelled_signal"] is None
        and worker["timed_out"] is False
        and worker["error"] is None
        and worker["cleanup_faults"] == [],
        f"{family}: owned cohort diagnostic not accepted",
    )
    return result


def source_paths(sdk):
    paths = set(REPO.glob("src/*.[ch]"))
    paths.update((REPO / "tests/bsim/client/src").glob("*.[ch]"))
    paths.update((REPO / "tests/bsim/src").glob("*.[ch]"))
    paths.update(
        p
        for p in (REPO / "tests/ascs_bsim").rglob("*")
        if p.is_file()
        and (
            p.suffix in (".c", ".h", ".conf")
            or p.name in ("CMakeLists.txt", "sysbuild.cmake", "cases.json")
            or p.name.startswith("Kconfig")
        )
    )
    for rel in (
        "tests/bsim/client/src/main.c",
        "tests/bsim/client/src/bsim_tx.c",
        "tests/bsim/client/src/bsim_tx.h",
        "tests/bsim/client/prj.conf",
        "tests/bsim/client/overlay-bt_ll_sw_split.conf",
        "tests/bsim/overlay-bt_ll_sw_split.conf",
        "tests/bsim/Kconfig",
        "scripts/ascs_bsim_run.py",
        "scripts/ascs-bsim-run.sh",
        "scripts/ascs_results.py",
        "scripts/check-ascs-results.py",
        "scripts/bluez_host_process.py",
        "scripts/bluez_host_descendants.py",
        "scripts/bluez_host_guest.py",
        "scripts/bsim-env.sh",
        "scripts/bsim_link_env.py",
        "scripts/check-bsim-runtime.py",
        "scripts/native_bsim_probes.py",
    ):
        paths.add(REPO / rel)
    for rel in (
        "zephyr/cmake/linker/ld/linker_flags.cmake",
        "zephyr/cmake/modules/extensions.cmake",
    ):
        paths.add(sdk / rel)
    for stem in (
        "bsim_48k_10ms_120b_l",
        "bsim_48k_10ms_120b_r",
        "bsim_48k_7p5ms_90b_l",
        "bsim_48k_7p5ms_90b_r",
    ):
        paths.add(REPO / "tests/fixtures/lc3" / (stem + ".lc3"))
    for rel in (
        "subsys/bluetooth/audio/audio.c",
        "subsys/bluetooth/audio/ascs.c",
        "subsys/bluetooth/audio/ascs_internal.h",
        "subsys/bluetooth/audio/audio_internal.h",
        "subsys/bluetooth/audio/bap_stream.c",
        "subsys/bluetooth/audio/bap_unicast_client.c",
        "subsys/bluetooth/audio/codec.c",
        "subsys/bluetooth/host/gatt.c",
        "include/zephyr/bluetooth/audio/audio.h",
        "include/zephyr/bluetooth/audio/bap.h",
        "include/zephyr/bluetooth/audio/pacs.h",
        "include/zephyr/bluetooth/gatt.h",
        "include/zephyr/bluetooth/iso.h",
        "include/zephyr/bluetooth/conn.h",
        "share/sysbuild/CMakeLists.txt",
        "share/sysbuild/Kconfig",
        "boards/native/native_sim/board.cmake",
        "boards/native/native_sim/Kconfig.native_sim",
        "cmake/modules/FindBabbleSim.cmake",
    ):
        paths.add(sdk / "zephyr" / rel)
    paths.add(sdk / "nrf/cmake/device_support.cmake")
    # Consumed BabbleSim component headers compiled into the peers and PHY:
    # the exact libUtilv1/libPhyComv1 src headers resolved against the pinned
    # components root. This bounds the consumed components include surface for
    # this lane's builds; it is not an SDK-wide or whole-components freeze.
    components = sdk / "tools/bsim/components"
    for lib in ("libUtilv1", "libPhyComv1"):
        paths.update((components / lib / "src").glob("*.h"))
    return sorted(paths)


SOURCE_LIMIT = 2 * 1024 * 1024
SOURCE_TOTAL_LIMIT = 32 * 1024 * 1024
SOURCE_ENTRY_LIMIT = 512


def verify_runtime_identity(result, bsim, stage):
    """Recheck live runtime libraries against the initially recorded identity.

    Detects ordinary file-level drift (replacement, rewrite or resize) after
    the initial capture at role-image time. This is not a claim of immunity
    against adversarial same-size same-content races; it proves detectable
    ordinary runtime drift per read.
    """
    insist(type(result.get("runtime")) is dict, "runtime identity record absent")
    expect = {
        "lib_2G4Channel_NtNcable.so",
        "lib_2G4Modem_Magic.so",
        "libCryptov1.so",
    }
    insist(
        set(result["runtime"]) == expect,
        f"{stage}: runtime identity population missing or extra",
    )
    for name in (
        "lib_2G4Channel_NtNcable.so",
        "lib_2G4Modem_Magic.so",
        "libCryptov1.so",
    ):
        insist(name in result["runtime"], f"{stage}: runtime identity {name} missing")
        library = (bsim / "lib" / name).resolve(strict=True)
        current = identity(_regular_snapshot(str(library), IMAGE_LIMIT), library)
        insist(
            current == result["runtime"][name],
            f"{stage}: simulator runtime library drifted: {name}",
        )
    return True


def freeze_sources(paths, snapshots, expected=None):
    insist(
        0 < len(paths) <= SOURCE_ENTRY_LIMIT and len(paths) == len(set(paths)),
        "source population missing, duplicated or oversized",
    )
    result = {}
    size = 0
    for path in paths:
        insist(
            path.is_absolute() and path.is_file() and not path.is_symlink(),
            f"missing/unsafe build source: {path}",
        )
        raw = _regular_snapshot(str(path), SOURCE_LIMIT)
        size += len(raw)
        insist(size <= SOURCE_TOTAL_LIMIT, "source total exceeds 32 MiB")
        dest = snapshots / (sha256(str(path).encode()) + (path.suffix or ".source"))
        if expected is None:
            with dest.open("xb") as stream:
                stream.write(raw)
        copy = _regular_snapshot(str(dest), SOURCE_LIMIT)
        insist(copy == raw, f"copied source drift: {path}")
        result[str(path)] = {
            "original": identity(raw, path),
            "copy": identity(copy, dest),
        }
        if expected is not None:
            insist(
                expected.get(str(path)) == result[str(path)],
                f"frozen source drift: {path}",
            )
    if expected is not None:
        insist(result == expected, "source population changed")
    return result


DIAGNOSTIC = re.compile(
    r"(?i)(?:\b(?:fatal\s+error|warning|error):|"
    r"\bCMake\s+(?:Warning|Error)\b|"
    r"\b[A-Za-z]+Warning:|\bskipping incompatible\b|"
    r"\bninja:\s+build stopped\b|\bFAILED:|\bFATAL(?: ERROR)?:)"
)


def inspect_warnings(cmake_raw, ninja_raw, role, sdk, configure_raw=b""):
    """Do not hide raw lines; permit only exact documented CMake/Kconfig notices."""
    cmake = cmake_raw.decode("utf-8")
    ninja = ninja_raw.decode("utf-8")
    configure = configure_raw.decode("utf-8")
    approved = BUILD_PROFILE[f"{role}_experimental"]
    for text in approved:
        insist(
            cmake.count("warning: " + text) == 1,
            f"{role}: required experimental warning missing/duplicated: {text}",
        )
    notice = BUILD_PROFILE["native_cmake_notice"]
    header = f"CMake Warning at {sdk}/nrf/cmake/device_support.cmake:34 (message):"
    insist(
        cmake.count(header) == cmake.count(notice) == 1,
        f"{role}: unexpected/missing native CMake notice source",
    )
    stripped = cmake.replace(header, "", 1).replace(notice, "", 1)
    for text in approved:
        stripped = stripped.replace("warning: " + text, "", 1)
    insist(
        not DIAGNOSTIC.search(stripped) and not DIAGNOSTIC.search(ninja),
        f"{role}: unlisted CMake/Ninja diagnostic",
    )
    # Configure-diagnostic disposition: recognized capability-probe
    # diagnostics come back as structured records (exact SDK-source-verified
    # shapes); anything unrecognized still raises the same strict error.
    probe_records = inspect_configure_probes(configure, sdk)
    return probe_records


def copy_image(src, dest, role):
    raw = _regular_snapshot(str(src), IMAGE_LIMIT)
    image_abi(raw, role)
    with dest.open("xb") as stream:
        stream.write(raw)
    dest.chmod(0o555)
    verify_image(dest, identity(raw, dest), role)
    return identity(raw, dest)


def sdk_identity(root, sdk, cancel=None, suffix=""):
    pins = {
        "zephyr": "33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6",
        "nrf": "b20f8619ba9a5530f8c34b0a130d829947cfe55d",
    }
    observed = {}
    for project, sha in pins.items():
        record = owned_command(
            ["git", "-C", str(sdk / project), "rev-parse", "HEAD"],
            root,
            project + "-head" + suffix,
            15,
            65536,
            cancel=cancel,
        )
        text = (
            _regular_snapshot(str(root / f"{project}-head{suffix}.log"), 65536)
            .decode()
            .strip()
        )
        insist(text == sha and record["bytes_logged"] > 0, f"{project} SDK HEAD drift")
        owned_command(
            [
                "git",
                "-C",
                str(sdk / project),
                "status",
                "--porcelain",
                "--untracked-files=no",
            ],
            root,
            project + "-status" + suffix,
            15,
            65536,
            cancel=cancel,
        )
        insist(
            _regular_snapshot(str(root / f"{project}-status{suffix}.log"), 65536)
            == b"",
            f"{project} tracked SDK source not clean",
        )
        observed[project] = text
    return observed


def make_jobs(root, family, run_id, images, cwd, timeout):
    root.mkdir(mode=0o700)
    base = {
        "schema_version": 1,
        "root": str(root),
        "family": family,
        "run_id": run_id,
        "images": images,
        "cwd": str(cwd),
        "timeout": timeout,
    }
    descriptors = {}
    for role in ROLES:
        job = dict(
            base,
            kind="actor",
            role=role,
            argv=expected_argv(base, role),
            log=str(root / f"{role}.log"),
            record=str(root / f"{role}-process.json"),
        )
        location = root / f"{role}-job.json"
        write_json(location, job)
        descriptors[role] = {
            "path": str(location),
            "sha256": sha256(_regular_snapshot(str(location), 256 * 1024)),
        }
    job = dict(
        base,
        kind="cohort",
        jobs=descriptors,
        execution_record=str(root / "family-execution-record.json"),
    )
    location = root / "cohort-job.json"
    write_json(location, job)
    return location, sha256(_regular_snapshot(str(location), 256 * 1024))


def execute(root, timeout, cancel, sdk):
    policy = load_inventory(_regular_snapshot(str(POLICY), POLICY_LIMIT), ANCHOR)
    result = {
        "run_id": uuid.uuid4().hex,
        "accepted": False,
        "families": [],
        "policy_sha256": ANCHOR,
        "errors": [],
        "cleanup_errors": [],
    }
    paths = source_paths(sdk)
    sources = None
    snapshots = root / "source-snapshots"
    sdk_verified = False
    bsim = None
    try:
        cancel.check()
        sdk_pins = sdk_identity(root, sdk, cancel)
        sdk_verified = True
        result["sdk"] = sdk_pins
        result["toolchain_bundle"] = "8285d8ad56"
        result["tools"] = {}
        for tool in ("cmake", "ninja", "gcc"):
            found = shutil.which(tool)
            insist(found is not None, f"missing tool: {tool}")
            location = Path(found).resolve(strict=True)
            result["tools"][tool] = identity(
                _regular_snapshot(str(location), IMAGE_LIMIT), location
            )
            owned_command(
                [tool, "--version"], root, f"tool-{tool}", 15, 65536, cancel=cancel
            )
        owned_command(
            ["git", "-C", str(REPO), "rev-parse", "HEAD"],
            root,
            "repo-head",
            15,
            65536,
            cancel=cancel,
        )
        owned_command(
            ["git", "-C", str(REPO), "status", "--porcelain=v1", "-uall"],
            root,
            "repo-dirty",
            15,
            1024 * 1024,
            cancel=cancel,
        )
        result["repo_head"] = (
            _regular_snapshot(str(root / "repo-head.log"), 65536).decode().strip()
        )
        result["dirty_diagnostic_sha256"] = sha256(
            _regular_snapshot(str(root / "repo-dirty.log"), 1024 * 1024)
        )
        cancel.check()
        snapshots.mkdir(mode=0o700)
        sources = freeze_sources(paths, snapshots)
        cancel.check()
        write_json(root / "source-hashes.json", sources)
        build = root / "build"
        images_root = root / "images"
        build.mkdir(mode=0o700)
        images_root.mkdir(mode=0o700)
        images = {}
        for role in ("receiver", "client"):
            cancel.check()
            work = build / role
            work.mkdir(mode=0o700)
            overlay = (
                REPO / "tests/bsim/overlay-bt_ll_sw_split.conf"
                if role == "receiver"
                else REPO / "tests/bsim/client/overlay-bt_ll_sw_split.conf"
            )
            overlay_conf = (
                str(overlay)
                if role == "receiver"
                else f"{overlay};{REPO / 'tests/ascs_bsim/client/overlay-ascs-mtu.conf'}"
            )
            argv = [
                "cmake",
                "-GNinja",
                f"-DBOARD_ROOT={REPO}",
                "-DBOARD=nrf54l15bsim/nrf54l15/cpuapp",
                f"-DAPP_DIR={REPO / 'tests/ascs_bsim' / role}",
                f"-DOVERLAY_CONFIG={overlay_conf}",
                "-DEXTRA_CONF_FILE=",
                "-DSNIPPET=bt-ll-sw-split",
                "-DCONFIG_COMPILER_WARNINGS_AS_ERRORS=y",
                "-DCONFIG_ASSERT=y",
                str(sdk / "zephyr/share/sysbuild"),
            ]
            owned_command(
                argv, work, "cmake", 900, BUILD_CAP, cwd=str(work), cancel=cancel
            )
            cancel.check()
            owned_command(
                ["ninja"], work, "ninja", 900, BUILD_CAP, cwd=str(work), cancel=cancel
            )
            configure_path = work / role / "CMakeFiles/CMakeConfigureLog.yaml"
            configure_raw = _regular_snapshot(str(configure_path), CONFIGURE_LOG_LIMIT)
            configure_copy = work / "cmake-configure.yaml"
            with configure_copy.open("xb") as stream:
                stream.write(configure_raw)
            insist(
                _regular_snapshot(str(configure_copy), CONFIGURE_LOG_LIMIT)
                == configure_raw,
                f"{role} configure YAML snapshot drift",
            )
            write_json(
                work / "cmake-configure-identity.json",
                {
                    "source": identity(configure_raw, configure_path),
                    "copy": identity(configure_raw, configure_copy),
                },
            )
            probe_records = inspect_warnings(
                _regular_snapshot(str(work / "cmake.log"), BUILD_CAP),
                _regular_snapshot(str(work / "ninja.log"), BUILD_CAP),
                role,
                sdk,
                configure_raw,
            )
            inspected = work / f"{role}-capability-probes.json"
            write_json(inspected, probe_records)
            result.setdefault("capability_probe_dispositions", {})[role] = probe_records
            config = work / role / "zephyr/.config"
            insist(config.is_file(), f"{role} resolved config absent")
            shutil.copyfile(config, work / "resolved.config")
            for name in ("zephyr.map",):
                for source in work.glob(f"**/{name}"):
                    dest = work / (
                        str(source.relative_to(work)).replace("/", "-") + ".retained"
                    )
                    if source != dest:
                        shutil.copyfile(source, dest)
            images[role] = copy_image(
                work / "zephyr/zephyr.exe", images_root / f"{role}.elf", role
            )
        bsim = Path(os.environ["BSIM_OUT_PATH"])
        insist(bsim.is_absolute() and bsim.is_dir(), "BSIM_OUT_PATH unavailable")
        # The finally block reuses bsim for the closing integrity recheck;
        # keep the resolved absolute root regardless of later env changes.
        bsim = Path(bsim).resolve(strict=True)
        result["runtime"] = {}
        for name in (
            "lib_2G4Channel_NtNcable.so",
            "lib_2G4Modem_Magic.so",
            "libCryptov1.so",
        ):
            library = (bsim / "lib" / name).resolve(strict=True)
            result["runtime"][name] = identity(
                _regular_snapshot(str(library), IMAGE_LIMIT), library
            )
        verify_runtime_identity(result, bsim, "runtime-ready:pre")
        images["phy"] = copy_image(
            bsim / "bin/bs_2G4_phy_v1", images_root / "phy.elf", "phy"
        )
        write_json(root / "images.json", images)
        freeze_sources(paths, snapshots, sources)
        cancel.check()
        owned_command(
            [
                sys.executable,
                str(REPO / "scripts/check-bsim-runtime.py"),
                "--root",
                str(bsim),
                "--peer",
                images["receiver"]["path"],
                "--peer",
                images["client"]["path"],
            ],
            root,
            "runtime-ready",
            30,
            1024 * 1024,
            cwd=str(bsim / "bin"),
            cancel=cancel,
        )
        verify_runtime_identity(result, bsim, "runtime-ready")
        for family, count, phases, exchanges, records in FAMILIES:
            cancel.check()
            verify_runtime_identity(result, bsim, f"cohort:{family}:pre")
            area = root / "families" / family
            location, job_hash = make_jobs(
                area, family, result["run_id"], images, bsim / "bin", timeout
            )
            owned_command(
                [
                    sys.executable,
                    str(Path(__file__).resolve()),
                    "--_cohort",
                    str(location),
                    "--_job-sha",
                    job_hash,
                ],
                area,
                "cohort",
                timeout + 30,
                COHORT_CAP,
                cancel=cancel,
            )
            cancel.check()
            verify_runtime_identity(result, bsim, f"cohort:{family}:post")
            verify_cohort_result(area, family, result["run_id"], images)
            verdict = owned_command(
                [
                    sys.executable,
                    str(REPO / "scripts/check-ascs-results.py"),
                    "--inventory",
                    str(POLICY),
                    "--expected-inventory-sha256",
                    ANCHOR,
                    "--family",
                    family,
                    "--client-log",
                    str(area / "client.log"),
                    "--receiver-log",
                    str(area / "receiver.log"),
                    "--execution-record",
                    str(area / "family-execution-record.json"),
                    "--expected-run-id",
                    result["run_id"],
                ],
                area,
                "checker",
                30,
                1024 * 1024,
                cancel=cancel,
            )
            checked = strict_json(
                _regular_snapshot(str(area / "checker.log"), 1024 * 1024), 1024 * 1024
            )
            insist(
                checked.get("accepted") is True
                and [
                    checked["trace"][key]
                    for key in (
                        "cases",
                        "render_phases",
                        "raw_exchanges",
                        "response_records",
                    )
                ]
                == [count, phases, exchanges, records],
                "required family verdict mismatch",
            )
            result["families"].append(
                {
                    "name": family,
                    "verdict": checked,
                    "verdict_sha256": verdict["log_sha256"],
                }
            )
        insist(
            len(result["families"]) == 6
            and verify_runtime_identity(result, bsim, "final")
            and freeze_sources(paths, snapshots, sources) == sources,
            "missing family or changed source after execution",
        )
        result["totals"] = {
            "cases": sum(f[1] for f in FAMILIES),
            "render_phases": sum(f[2] for f in FAMILIES),
            "raw_exchanges": sum(f[3] for f in FAMILIES),
            "response_records": sum(f[4] for f in FAMILIES),
        }
        insist(result["totals"] == policy["totals"], "policy total drift")
        return result
    except BaseException as exc:
        result["errors"].append(f"{type(exc).__name__}: {exc}")
        return result
    finally:
        if sdk_verified:
            try:
                sdk_identity(root, sdk, cancel, suffix="-final")
            except BaseException as exc:
                result["errors"].append(
                    f"post-run SDK integrity: {type(exc).__name__}: {exc}"
                )
        if sources is not None:
            try:
                freeze_sources(paths, snapshots, sources)
            except BaseException as exc:
                result["errors"].append(
                    f"post-run source integrity: {type(exc).__name__}: {exc}"
                )
        # Closing runtime integrity on every ending, success or failure: run
        # when the initial identity was populated and the pinned root was
        # resolved, regardless of whether the root still exists (a vanished
        # root is itself drift and must be reported, never silently skipped);
        # only a pre-capture ending skips legitimately. Failures join errors
        # as distinct entries without replacing any recorded in-flight one.
        if result.get("runtime") and bsim is not None:
            try:
                verify_runtime_identity(result, bsim, "post-run")
            except BaseException as exc:
                result["errors"].append(
                    f"post-run runtime integrity: {type(exc).__name__}: {exc}"
                )


def publish_suite(root, outcome, scope, cancel):
    """Bounded terminal record, correcting one in-flight publication signal."""
    cancel.latch(scope.record["cancelled_signal"])
    outcome["scope"] = scope.record
    if not scope.record["ok"]:
        outcome["cleanup_errors"].extend(scope.record["errors"])
        outcome["accepted"] = False
    outcome["cancelled_signal"] = cancel.signal
    if cancel.signal is not None:
        outcome["accepted"] = False
    location = root / "suite-record.json"
    write_json(location, outcome)
    if cancel.signal is not None and outcome["cancelled_signal"] is None:
        outcome["cancelled_signal"] = cancel.signal
        outcome["accepted"] = False
        replace_json(location, outcome)


def public_main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    parser.add_argument("--expected-inventory-sha256", required=True)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args(argv)
    if "ASCS_CASE" in os.environ:
        parser.error(
            "ASCS_CASE unsupported: full 60 mandatory; use historical diagnostics separately"
        )
    if not 30 <= args.timeout <= 600:
        parser.error("timeout per cohort must be 30..600 seconds")
    if args.expected_inventory_sha256 != ANCHOR:
        parser.error("policy digest must match approved independent anchor")
    load_inventory(
        _regular_snapshot(str(POLICY), POLICY_LIMIT), args.expected_inventory_sha256
    )
    if not Path(args.output).is_absolute():
        parser.error("output must be an absolute new external directory")
    sdk = sdk_root()
    protected = [sdk]
    preserved = Path("/tmp/opencode")
    if preserved.is_dir():
        protected.extend(
            p for p in preserved.iterdir() if p.name.startswith("pb051-") and p.is_dir()
        )
    root = exclusive(args.output, protected=protected)
    bsim_path = os.environ.get("BSIM_OUT_PATH")
    insist(
        type(bsim_path) is str
        and Path(bsim_path).is_absolute()
        and Path(bsim_path) == sdk / "tools/bsim"
        and Path(bsim_path).is_dir(),
        "BSIM_OUT_PATH must be pinned NCS v3.4.1/tools/bsim",
    )
    # FindBabbleSim.cmake resolves BSIM_COMPONENTS_PATH separately from
    # BSIM_OUT_PATH, so an env override or alternate components root would
    # silently build peers against different sources than the pinned output.
    components = os.environ.get("BSIM_COMPONENTS_PATH")
    expected_components = sdk / "tools/bsim/components"
    insist(
        type(components) is str
        and Path(components).is_absolute()
        and Path(components).resolve(strict=True)
        == expected_components.resolve(strict=True),
        "BSIM_COMPONENTS_PATH must equal installed NCS v3.4.1 tools/bsim/components",
    )
    root.mkdir(mode=0o700)
    outcome = {
        "accepted": False,
        "families": [],
        "errors": [],
        "cleanup_errors": [],
        "cancelled_signal": None,
    }
    scope = DescendantScope()
    with Cancel() as cancel:
        try:
            (root / "families").mkdir(mode=0o700)
            with scope:
                cancel.reinstall()
                outcome = execute(root, args.timeout, cancel, sdk)
                cancel.check()
            cancel.latch(scope.record["cancelled_signal"])
            cancel.check()
            insist(scope.record["ok"], "whole runner scope not closed")
            insist(not outcome["errors"], "runner failed")
            outcome["accepted"] = True
        except BaseException as exc:
            outcome["errors"].append(f"{type(exc).__name__}: {exc}")
        finally:
            publish_suite(root, outcome, scope, cancel)
    return 0 if outcome["accepted"] else 1


def main():
    if (
        len(sys.argv) == 5
        and sys.argv[1] in ("--_actor", "--_cohort")
        and sys.argv[3] == "--_job-sha"
    ):
        try:
            return (
                actor(Path(sys.argv[2]), sys.argv[4])
                if sys.argv[1] == "--_actor"
                else cohort(Path(sys.argv[2]), sys.argv[4])
            )
        except BaseException as exc:
            print(
                f"PB-051 internal owner failed: {type(exc).__name__}: {exc}",
                file=sys.stderr,
            )
            return 1
    try:
        return public_main(sys.argv[1:])
    except (ValueError, OSError) as exc:
        print(f"PB-051 preflight failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
