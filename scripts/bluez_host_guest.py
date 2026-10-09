#!/usr/bin/env python3
"""Prepare and run PB-053 isolated, local-artifact BlueZ public checkpoint."""

import argparse
import gzip
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import stat
import subprocess
import sys
import threading
import time

from bluez_host_process import _group_live, run_owned
from bluez_host_results import (
    decode_marker,
    validate_capture,
    validate_guest,
    validate_hold_ready,
)


REPO = Path(__file__).resolve().parents[1]
STORE = Path("/nix/store")
VERSION = "7.1.5"
KERNEL = Path("/nix/store/z94chi3wa8zcz0l170bfz6cq578cfw0s-linux-7.1.5/bzImage")
MODULES = Path("/nix/store/g0wfy5q8m3f7daz4vrbadvgrna0zh1v0-linux-7.1.5-modules")
SHELL = Path("/nix/store/zrynrzpsy2993w555ns9a734lbzfff2b-busybox-1.37.0/bin/ash")
PYTHON = Path(
    "/nix/store/sdfysgb89zdysrknjavcr0crs4qxpk8r-python3-3.13.12/bin/python3.13"
)
MOUNT = Path(
    "/nix/store/vpv0bx37wxwmkhfxqs28il1l24qldj6b-util-linux-2.42.3-mount/bin/mount"
)
MODPROBE = Path("/nix/store/bc9a5ng1vn0v74kidqzkbx4y06yxqd4i-kmod-31/bin/modprobe")
BLUEZ = Path(
    "/nix/store/8l7syi03wm19x41yvarswrqzz7jq404r-bluez-5.87/libexec/bluetooth/bluetoothd"
)
DBUS = Path("/nix/store/w9gn9sy71j4v3jia681vvx5j4d7f5ly7-dbus-1.16.2/bin/dbus-daemon")
DBUS_SEND = DBUS.parent / "dbus-send"
BTMGMT = BLUEZ.parents[2] / "bin/btmgmt"
LIBC = Path("/nix/store/yhawd8dka2563b5mg3vjm5h14sw5lv95-glibc-multi-2.40-224")
EMULATOR = Path("/tmp/opencode/pb053-emulator-build-r2/btvirt")
EMULATOR_SHA = "06611569862825010327200c354377428e996dade36d96c1a4872eb20ed0083c"
STIMULUS = REPO / "tests/fixtures/lc3/bsim_48k_10ms_120b_l.lc3"
STIMULUS_SHA = "c16222f9d0e107488a1aec502d1bbb5a4c6e3944ce28b7f886c55415f51130be"
STIMULUS_SIZE = 15360
QEMU = Path(
    "/nix/store/brhybv2j85y5f384c1nqrq1qbdgynrha-qemu-for-vm-tests-11.0.2/bin/qemu-system-x86_64"
)
DBUS_PY = Path(
    "/nix/store/mvvgdnjagabhvzqqwrv22dhm6b0gymkf-python3.13-dbus-python-1.4.0"
)
GI_PY = Path("/nix/store/r1ii8vh2n6r4kkz9piy394p2r405820q-python3.13-pygobject-3.54.5")
GLIB = Path("/nix/store/bz34hjmhv4fvqxk6wkyxj1hc8mpwgy3w-glib-2.86.3")
ROOTS = (
    SHELL,
    PYTHON,
    MOUNT,
    MODPROBE,
    BLUEZ,
    DBUS,
    MODULES,
    LIBC,
    DBUS_PY,
    GI_PY,
    GLIB,
)
LIMIT = 4 * 1024**3
ENTRY_CAP = 100000
ROOT_CAP = 4096
FIELD_CAP = 4096
METADATA_CAP = 32 * 1024**2
PATH_LIST_CAP = 32 * 1024**2
CPIO_CAP = 5 * 1024**3
CPIO_STDERR_CAP = 64 * 1024
QUERY_STDOUT_CAP = 16 * 1024**2
QUERY_STDERR_CAP = 64 * 1024
MARKER = "PB053_GUEST_RESULT "
SCOPE = "isolated Linux/BlueZ host regression; no physical/codec acceptance"
CPU_PROFILE = "host,ssbd=off; one guest vCPU; no SMP claim"
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
ARTIFACT_CAPS = {
    "kernel": 64 * 1024**2,
    "config.gz": 2 * 1024**2,
    "initramfs.cpio.gz": 2 * 1024**3,
}
GZIP_CAP = ARTIFACT_CAPS["initramfs.cpio.gz"]
MANIFEST_CAP = 64 * 1024**2
STIMULUS_PURPOSE = (
    "valid LC3 transport stimulus only, not independent decode acceptance"
)
GUEST_ENV_LINKS = frozenset(
    {
        "/nix/store/qg23qszvwv9wg125x96k1vlgby75bn2s-systemd-minimal-261.3/lib/environment.d/99-environment.conf",
        "/nix/store/xf9pxr5axxrpi808f4sh7c733gm2phj4-systemd-minimal-260.4/lib/environment.d/99-environment.conf",
        "/nix/store/64qjwn4wvfnlcm5ja238m6i3mrk2q076-systemd-minimal-258.7/lib/environment.d/99-environment.conf",
    }
)
GUEST_ENV_TARGET = "../../../../../etc/environment"
SOURCE_CAP = 2 * 1024 * 1024
SOURCE_DESTINATIONS = {
    "guest": "guest.py",
    "public": "public.py",
    "results": "bluez_host_results.py",
    "monitor": "monitor.py",
    "acquire": "bluez_guest_acquire.py",
    "limits": "bluez_guest_limits.py",
    "process": "bluez_host_process.py",
    "emulator": "btvirt",
}


def source_paths():
    scripts = REPO / "scripts"
    return {
        "host": Path(__file__).resolve(),
        "guest": scripts / "bluez_guest_init.py",
        "public": scripts / "bluez_guest_public.py",
        "results": scripts / "bluez_host_results.py",
        "monitor": scripts / "bluez_guest_monitor.py",
        "acquire": scripts / "bluez_guest_acquire.py",
        "limits": scripts / "bluez_guest_limits.py",
        "process": scripts / "bluez_host_process.py",
        "emulator": EMULATOR,
    }


def source_cap(key):
    return 64 * 1024 * 1024 if key == "emulator" else SOURCE_CAP


def freeze_sources(paths, *, expected_emulator_sha=EMULATOR_SHA):
    if not isinstance(expected_emulator_sha, str) or not HEX64.fullmatch(
        expected_emulator_sha
    ):
        raise ValueError("Expected emulator SHA256 must be lowercase64-hex")
    frozen = {
        key: regular_hash(path, source_cap(key))[1] for key, path in paths.items()
    }
    if (
        set(frozen) != {*SOURCE_DESTINATIONS, "host"}
        or frozen["emulator"] != expected_emulator_sha
    ):
        raise ValueError("Source map or emulator pin mismatch")
    return frozen


def stage_sources(paths, frozen, stage, output):
    copied = {}
    for key, path in paths.items():
        target = (
            output / "host-source.py"
            if key == "host"
            else stage / "opt/pb053" / SOURCE_DESTINATIONS[key]
        )
        copied[key] = target
        snapshot(path, target, source_cap(key), frozen[key])
        if key == "emulator":
            target.chmod(0o755)
    return copied


def verify_sources(paths, frozen, copied):
    for key in frozen:
        if regular_hash(paths[key], source_cap(key))[1] != frozen[key]:
            raise ValueError(f"Source changed during guest preparation: {key}")
        if regular_hash(copied[key], source_cap(key))[1] != frozen[key]:
            raise ValueError(f"Staged source changed during guest preparation: {key}")


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def open_regular(path, cap):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size > cap:
            raise ValueError(f"Invalid regular input: {path}")
        return os.fdopen(fd, "rb")
    except BaseException:
        os.close(fd)
        raise


def regular_hash(path, cap):
    h = hashlib.sha256()
    count = 0
    with open_regular(path, cap) as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            count += len(chunk)
            if count > cap:
                raise ValueError(f"Input exceeds cap: {path}")
            h.update(chunk)
    if count == 0:
        raise ValueError(f"Empty input: {path}")
    return count, h.hexdigest()


def snapshot(source, destination, cap, expected, *, cancel_check=None):
    """Copy bytes from a safe regular input and hash copied bytes, no source mutation."""
    h = hashlib.sha256()
    count = 0
    if cancel_check is not None:
        cancel_check()
    with open_regular(source, cap) as original, destination.open("xb") as target:
        while True:
            if cancel_check is not None:
                cancel_check()
            chunk = original.read(1024 * 1024)
            if cancel_check is not None:
                cancel_check()
            if not chunk:
                break
            count += len(chunk)
            if count > cap:
                raise ValueError(f"Snapshot exceeds cap: {source}")
            target.write(chunk)
            h.update(chunk)
            if cancel_check is not None:
                cancel_check()
    if cancel_check is not None:
        cancel_check()
    if count == 0 or h.hexdigest() != expected:
        raise ValueError(f"Snapshot hash mismatch: {source}")
    destination.chmod(0o444)
    return h.hexdigest()


def verify_staged_stimulus(path):
    size, checksum = regular_hash(path, STIMULUS_SIZE)
    if size != STIMULUS_SIZE or checksum != STIMULUS_SHA:
        raise ValueError("Staged LC3 transport stimulus size/hash mismatch")


def stage_stimulus(source, destination):
    snapshot(source, destination, STIMULUS_SIZE, STIMULUS_SHA)
    verify_staged_stimulus(destination)


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f"Invalid JSON constant: {value}")


def read_manifest(path, expected):
    if not isinstance(expected, str) or not HEX64.fullmatch(expected):
        raise ValueError("Manifest SHA256 must be lowercase64-hex")
    with open_regular(path, MANIFEST_CAP) as stream:
        data = stream.read(MANIFEST_CAP + 1)
    if len(data) > MANIFEST_CAP or hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("Manifest digest mismatch or oversized")
    try:
        value = json.loads(
            data, object_pairs_hook=unique_pairs, parse_constant=reject_constant
        )
    except (UnicodeError, ValueError, RecursionError, TypeError) as exc:
        raise ValueError(f"Invalid manifest JSON: {exc}") from exc
    return value


def validate_manifest(manifest):
    if (
        not isinstance(manifest, dict)
        or type(manifest.get("schema_version")) is not int
        or manifest["schema_version"] != 2
    ):
        raise ValueError("Prepared schema mismatch")
    if (
        manifest.get("version") != VERSION
        or manifest.get("scope") != SCOPE
        or manifest.get("cpu_profile") != CPU_PROFILE
    ):
        raise ValueError("Prepared profile mismatch")
    hashes = manifest.get("artifacts")
    if (
        not isinstance(hashes, dict)
        or set(hashes) != set(ARTIFACT_CAPS)
        or any(
            not isinstance(value, str) or not HEX64.fullmatch(value)
            for value in hashes.values()
        )
    ):
        raise ValueError("Prepared artifact hashes invalid")
    sources = manifest.get("source_hashes")
    expected_sources = {
        "host": digest(Path(__file__)),
        "guest": digest(REPO / "scripts/bluez_guest_init.py"),
        "public": digest(REPO / "scripts/bluez_guest_public.py"),
        "results": digest(REPO / "scripts/bluez_host_results.py"),
        "monitor": digest(REPO / "scripts/bluez_guest_monitor.py"),
        "acquire": digest(REPO / "scripts/bluez_guest_acquire.py"),
        "limits": digest(REPO / "scripts/bluez_guest_limits.py"),
        "process": digest(REPO / "scripts/bluez_host_process.py"),
        "emulator": EMULATOR_SHA,
    }
    if (
        not isinstance(sources, dict)
        or set(sources) != set(expected_sources)
        or sources != expected_sources
    ):
        raise ValueError("Prepared source pins invalid")
    stimulus = manifest.get("stimulus")
    if (
        not isinstance(stimulus, dict)
        or stimulus
        != {
            "source": str(STIMULUS.relative_to(REPO)),
            "guest_path": "/opt/pb053/stimulus.lc3",
            "size": STIMULUS_SIZE,
            "sha256": STIMULUS_SHA,
            "purpose": STIMULUS_PURPOSE,
        }
        or type(stimulus["size"]) is not int
    ):
        raise ValueError("Prepared stimulus identity invalid")
    qemu = manifest.get("qemu")
    if (
        not isinstance(qemu, dict)
        or set(qemu) != {"path", "size", "sha256"}
        or qemu.get("path") != str(QEMU)
        or type(qemu.get("size")) is not int
        or qemu["size"] <= 0
        or not isinstance(qemu.get("sha256"), str)
        or not HEX64.fullmatch(qemu["sha256"])
    ):
        raise ValueError("Prepared QEMU identity invalid")
    return hashes, qemu


def exclusive(path, protected=()):
    path = Path(path).absolute()
    if path.exists() or path.is_symlink() or not path.parent.is_dir():
        raise ValueError("Output must be new with existing parent")
    forbidden = (
        REPO,
        Path.home(),
        Path("/nix/store"),
        Path("/tmp/opencode/bluetooth-test-resources-20261003"),
        EMULATOR.parent,
        *protected,
    )
    resolved = path.resolve()
    if resolved == Path("/") or any(
        resolved == root or root in resolved.parents for root in forbidden
    ):
        raise ValueError("Output overlaps repository, home, store or vendor source")
    if any(
        parent.parent == Path("/tmp/opencode") and parent.name.startswith("pb053-")
        for parent in resolved.parents
    ):
        raise ValueError("Output nested under preserved PB-053 evidence")
    return path


def store_root(path):
    path = Path(path)
    if (
        path.parent != STORE
        or path.suffix == ".drv"
        or not (path.is_dir() or path.is_file())
        or path.is_symlink()
    ):
        raise ValueError(f"Invalid store root: {path}")
    return path


class EntryBudget:
    def __init__(
        self,
        max_entries=ENTRY_CAP,
        max_roots=ROOT_CAP,
        max_field=FIELD_CAP,
        max_metadata=METADATA_CAP,
    ):
        self.max_entries = max_entries
        self.max_roots = max_roots
        self.max_field = max_field
        self.max_metadata = max_metadata
        self.entries = 0
        self.metadata = 0
        self.roots = set()

    def _charge(self, value):
        size = len(os.fsencode(value))
        if size > self.max_field or self.metadata + size > self.max_metadata:
            raise ValueError("Preparation path/link metadata quota exceeded")
        self.metadata += size

    def root(self, path):
        path = str(path)
        if path not in self.roots:
            if len(self.roots) >= self.max_roots:
                raise ValueError("Preparation root quota exceeded")
            self._charge(path)
            self.roots.add(path)

    def entry(self, path, link=None):
        if self.entries >= self.max_entries:
            raise ValueError("Preparation entry quota exceeded")
        self._charge(path)
        if link is not None:
            self._charge(link)
        self.entries += 1


def entries(root, budget=None):
    """Record content and validate symlink resolution before copying."""
    budget = budget if budget is not None else EntryBudget()
    budget.root(root)
    if root.is_file():
        budget.entry("")
        size = root.stat().st_size
        if size > LIMIT:
            raise ValueError("Closure exceeds 4 GiB")
        return {"": {"sha256": digest(root), "size": size}}, size
    result = {}
    total = 0
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in itertools.chain(dirs, files):
            item = Path(directory) / name
            rel = str(item.relative_to(root))
            mode = item.lstat().st_mode
            if stat.S_ISLNK(mode):
                link = os.readlink(item)
                budget.entry(rel, link)
                resolved = str(item.resolve())
                budget._charge(resolved)
                result[rel] = {
                    "link": link,
                    "resolved": resolved,
                }
            elif stat.S_ISREG(mode):
                budget.entry(rel)
                size = item.stat().st_size
                total += size
                if total > LIMIT:
                    raise ValueError("Closure exceeds 4 GiB")
                result[rel] = {"sha256": digest(item), "size": size}
            elif stat.S_ISDIR(mode):
                budget.entry(rel)
                result[rel] = {"directory": True}
            else:
                raise ValueError(f"Special file in closure: {item}")
    return result, total


def linked_store_root(target):
    """Resolve a link only to an existing, local Nix store artifact."""
    target = Path(target)
    if STORE not in target.parents or not target.exists():
        raise ValueError(f"Link escapes local store or is missing: {target}")
    root = next(
        parent for parent in (target, *target.parents) if parent.parent == STORE
    )
    return store_root(root)


def guest_environment_link(origin, record):
    """Allow only reviewed links to guest-authored empty environment."""
    return (
        str(origin) in GUEST_ENV_LINKS
        and record["link"] == GUEST_ENV_TARGET
        and record["resolved"] == "/etc/environment"
    )


def closure_catalog(closure, commands, output, budget):
    """Complete locally available store links without accepting host-root links."""
    catalog = {}
    size = 0
    for root in closure:
        budget.root(root)
    while True:
        pending = sorted(closure - {Path(path) for path in catalog})
        if not pending:
            break
        additions = set()
        for root in pending:
            records, used = entries(root, budget)
            catalog[str(root)] = records
            size += used
            if size > LIMIT:
                raise ValueError("Closure exceeds 4 GiB")
            for rel, record in records.items():
                if "link" not in record:
                    continue
                if guest_environment_link(Path(root) / rel, record):
                    continue
                target = Path(record["resolved"])
                linked = linked_store_root(target)
                if linked in closure or linked in additions:
                    continue
                argv = ["nix-store", "--query", "--requisites", str(linked)]
                commands.append(argv)
                for line in query_roots(argv, output, len(commands), budget):
                    additions.add(line)
                budget.root(linked)
                additions.add(linked)
        closure.update(additions)
    for root, records in catalog.items():
        for rel, record in records.items():
            if "link" in record:
                if guest_environment_link(Path(root) / rel, record):
                    continue
                target = Path(record["resolved"])
                if not any(
                    target == selected
                    or selected.is_dir()
                    and selected in target.parents
                    for selected in closure
                ):
                    raise ValueError(
                        f"Link escapes selected closure: {root}/{rel} -> {target}"
                    )
    return catalog


def console_header():
    name = b"dev/console\0"
    values = (0x7FFFFFFE, 0o20600, 0, 0, 1, 0, 0, 0, 0, 5, 1, len(name), 0)
    header = b"070701" + b"".join(f"{value:08x}".encode() for value in values) + name
    return header + b"\0" * (-len(header) % 4)


def write_capped(stream, data, count, cap):
    if count + len(data) > cap:
        raise ValueError("Preparation output quota exceeded")
    stream.write(data)
    return count + len(data)


class CountingWriter:
    def __init__(self, stream, cap):
        self.stream = stream
        self.name = stream.name
        self.cap = cap
        self.count = 0

    def write(self, data):
        self.count = write_capped(self.stream, data, self.count, self.cap)
        return len(data)

    def flush(self):
        self.stream.flush()

    def tell(self):
        return self.count


def write_manifest(record, path, cap=MANIFEST_CAP):
    count = 0
    with path.open("xb") as stream:
        for chunk in json.JSONEncoder(indent=2).iterencode(record):
            count = write_capped(stream, chunk.encode("utf-8"), count, cap)
        write_capped(stream, b"\n", count, cap)


def stage_paths(stage, budget=None):
    budget = budget if budget is not None else EntryBudget()
    paths = []
    for directory, dirs, files in os.walk(stage, followlinks=False):
        for name in itertools.chain(dirs, files):
            item = Path(directory) / name
            rel = str(item.relative_to(stage))
            mode = item.lstat().st_mode
            if stat.S_ISLNK(mode):
                budget.entry(rel, os.readlink(item))
            elif stat.S_ISDIR(mode) or stat.S_ISREG(mode):
                budget.entry(rel)
            else:
                raise ValueError(f"Special file in stage: {item}")
            if rel != "dev/console":
                paths.append(rel)
    return sorted(paths)


def write_path_list(paths, path, cap=PATH_LIST_CAP):
    count = 0
    with path.open("xb") as stream:
        for name in paths:
            count = write_capped(stream, os.fsencode(name) + b"\0", count, cap)


def compress_cpio(source, destination, input_cap=CPIO_CAP, output_cap=GZIP_CAP):
    count = 0
    with source.open("rb") as cpio, destination.open("xb") as raw:
        with gzip.GzipFile(
            fileobj=CountingWriter(raw, output_cap), mode="wb", mtime=0
        ) as zipped:
            zipped.write(console_header())
            while chunk := cpio.read(65536):
                count += len(chunk)
                if count > input_cap:
                    raise ValueError("Uncompressed cpio quota exceeded")
                zipped.write(chunk)


def run_bounded_command(
    argv, output, tag, stdout_cap, stderr_cap, timeout, stdin=None, cwd=None
):
    """Own one fixed command group and retain separate capped output files."""
    stdout_path = output / (tag + ".stdout")
    stderr_path = output / (tag + ".stderr")
    with (
        stdout_path.open("xb") as stdout,
        stderr_path.open("xb") as stderr,
        (
            Path(stdin).open("rb") if stdin is not None else open(os.devnull, "rb")
        ) as input_stream,
        RunCancellation() as cancellation,
    ):
        selector = selectors.DefaultSelector()
        proc = None
        counts = {"stdout": 0, "stderr": 0}
        streams = {"stdout": stdout, "stderr": stderr}
        caps = {"stdout": stdout_cap, "stderr": stderr_cap}
        deadline = time.monotonic() + timeout
        try:
            cancellation.check()
            proc = subprocess.Popen(
                argv,
                stdin=input_stream,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=cwd,
                start_new_session=True,
            )
            assert proc.stdout is not None and proc.stderr is not None
            for name, pipe in (("stdout", proc.stdout), ("stderr", proc.stderr)):
                os.set_blocking(pipe.fileno(), False)
                selector.register(pipe.fileno(), selectors.EVENT_READ, name)
            while selector.get_map():
                cancellation.check()
                if time.monotonic() >= deadline:
                    raise ValueError(f"Command timed out: {argv}")
                for key, _ in selector.select(
                    min(0.05, max(0, deadline - time.monotonic()))
                ):
                    chunk = os.read(key.fd, 65536)
                    if not chunk:
                        selector.unregister(key.fd)
                    else:
                        name = key.data
                        counts[name] = write_capped(
                            streams[name], chunk, counts[name], caps[name]
                        )
            while proc.poll() is None:
                cancellation.check()
                if time.monotonic() >= deadline:
                    raise ValueError(f"Command timed out: {argv}")
                time.sleep(0.05)
            cancellation.check()
            if proc.returncode != 0 or counts["stderr"]:
                raise ValueError(
                    f"Command failed or emitted stderr: {argv}: {proc.returncode}"
                )
            if _group_live(proc.pid):
                raise ValueError(f"Command left live descendants: {argv}")
        finally:
            try:
                if proc is not None:
                    try:
                        os.killpg(proc.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    grace = time.monotonic() + 2
                    while _group_live(proc.pid) and time.monotonic() < grace:
                        time.sleep(0.05)
                    if _group_live(proc.pid):
                        try:
                            os.killpg(proc.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    try:
                        proc.wait(timeout=2)
                    finally:
                        if proc.stdout is not None:
                            proc.stdout.close()
                        if proc.stderr is not None:
                            proc.stderr.close()
            finally:
                selector.close()
        cancellation.check()
        if proc is not None and _group_live(proc.pid):
            raise ValueError(f"Command group remained live: {argv}")
    return stdout_path


def query_roots(argv, output, ordinal, budget):
    path = run_bounded_command(
        argv, output, f"query-{ordinal:04d}", QUERY_STDOUT_CAP, QUERY_STDERR_CAP, 120
    )
    with path.open("rb") as stream:
        data = stream.read(QUERY_STDOUT_CAP + 1)
    if len(data) > QUERY_STDOUT_CAP:
        raise ValueError("Nix query output quota exceeded")
    roots = []
    for line in data.decode("utf-8").splitlines():
        budget.root(line)
        root = store_root(line)
        roots.append(root)
    return roots


def prepare(output):
    output = exclusive(output)
    if (
        os.uname().release != VERSION
        or Path("/run/booted-system/kernel").resolve() != KERNEL
    ):
        raise ValueError("Running kernel does not match booted 7.1.5 kernel")
    required = (
        KERNEL,
        MODULES / "lib/modules" / VERSION,
        SHELL,
        PYTHON,
        MOUNT,
        MODPROBE,
        BLUEZ,
        DBUS,
        DBUS_SEND,
        BTMGMT,
        EMULATOR,
        QEMU,
        REPO / "scripts/bluez_guest_init.py",
        REPO / "scripts/bluez_guest_public.py",
        REPO / "scripts/bluez_host_results.py",
        REPO / "scripts/bluez_guest_monitor.py",
        REPO / "scripts/bluez_guest_acquire.py",
        REPO / "scripts/bluez_guest_limits.py",
        REPO / "scripts/bluez_host_process.py",
        STIMULUS,
        DBUS_PY / "lib/python3.13/site-packages/dbus/__init__.py",
        GI_PY / "lib/python3.13/site-packages/gi/__init__.py",
        GLIB / "lib/girepository-1.0/GLib-2.0.typelib",
        Path("/proc/config.gz"),
    )
    if any(not path.exists() for path in required):
        raise ValueError(
            f"Missing required artifact: {[str(p) for p in required if not p.exists()]}"
        )
    sources = source_paths()
    frozen = freeze_sources(sources)
    if regular_hash(STIMULUS, STIMULUS_SIZE) != (STIMULUS_SIZE, STIMULUS_SHA):
        raise ValueError("LC3 transport stimulus size/hash mismatch")
    output.mkdir()
    commands = []
    closure = set()
    budget = EntryBudget()
    for executable in ROOTS:
        root = store_root(
            next(
                parent
                for parent in (executable, *executable.parents)
                if parent.parent == STORE
            )
        )
        argv = ["nix-store", "--query", "--requisites", str(root)]
        commands.append(argv)
        closure.update(query_roots(argv, output, len(commands), budget))
    kernel_root = store_root(KERNEL.parent)
    budget.root(kernel_root)
    closure.add(kernel_root)
    catalog = closure_catalog(closure, commands, output, budget)
    stage = output / "stage"
    stage.mkdir()
    for root in sorted(closure):
        destination = stage / str(root).lstrip("/")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if root.is_dir():
            shutil.copytree(root, destination, symlinks=True)
        else:
            shutil.copy2(root, destination)
    for dirname in (
        "proc",
        "sys",
        "dev",
        "run",
        "tmp",
        "etc",
        "var/lib/bluetooth",
        "lib",
        "bin",
        "opt/pb053",
    ):
        (stage / dirname).mkdir(parents=True, exist_ok=True)
    (stage / "lib/modules").symlink_to(MODULES / "lib/modules")
    copied = stage_sources(sources, frozen, stage, output)
    staged_stimulus = stage / "opt/pb053/stimulus.lc3"
    stage_stimulus(STIMULUS, staged_stimulus)
    runtime = {
        "kernel": VERSION,
        "modprobe": str(MODPROBE),
        "dbus": str(DBUS),
        "dbus_send": str(DBUS_SEND),
        "btmgmt": str(BTMGMT),
        "pb053_guest": 1,
        "bluez": str(BLUEZ),
        "emulator": "/opt/pb053/btvirt",
        "python": str(PYTHON),
        "dbus_python": str(DBUS_PY / "lib/python3.13/site-packages"),
        "gi_python": str(GI_PY / "lib/python3.13/site-packages"),
        "gi_typelib": str(GLIB / "lib/girepository-1.0"),
    }
    (stage / "opt/pb053/runtime.json").write_text(json.dumps(runtime, indent=2) + "\n")
    (stage / "etc/passwd").write_text("root:x:0:0:root:/root:/bin/sh\n")
    (stage / "etc/group").write_text("root:x:0:\n")
    (stage / "etc/environment").write_bytes(b"")
    (stage / "etc/machine-id").write_text(os.urandom(16).hex() + "\n")
    (stage / "etc/hostname").write_text("pb053-guest\n")
    init = (
        f'#!{SHELL}\n[ "$$" = 1 ] || {{ echo "PB053 init requires PID1"; exit 1; }}\n'
        f"{MOUNT} -t proc proc /proc || exit 1\n"
        f"{MOUNT} -t sysfs sysfs /sys || exit 1\n"
        f"{MOUNT} -t devtmpfs devtmpfs /dev || exit 1\n"
        f"exec {PYTHON} -u /opt/pb053/guest.py\n"
    )
    (stage / "init").write_text(init)
    (stage / "init").chmod(0o755)
    (stage / "bin/sh").symlink_to(SHELL)
    for source, name in ((KERNEL, "kernel"), (Path("/proc/config.gz"), "config.gz")):
        cap = ARTIFACT_CAPS[name]
        _, expected = regular_hash(source, cap)
        snapshot(source, output / name, cap, expected)
    verify_sources(sources, frozen, copied)
    verify_staged_stimulus(staged_stimulus)
    paths = stage_paths(stage)
    archive = output / "initramfs.cpio.gz"
    path_list = output / "paths.list"
    write_path_list(paths, path_list)
    argv = ["cpio", "--null", "-o", "--format=newc", "--quiet", "--reproducible"]
    commands.append(argv)
    cpio_stdout = run_bounded_command(
        argv,
        output,
        "cpio",
        CPIO_CAP,
        CPIO_STDERR_CAP,
        300,
        stdin=path_list,
        cwd=stage,
    )
    cpio_stdout.rename(output / "initramfs.cpio")
    compress_cpio(output / "initramfs.cpio", archive)
    for name, cap in ARTIFACT_CAPS.items():
        regular_hash(output / name, cap)
    artifacts = {
        name: digest(output / name)
        for name in ("kernel", "config.gz", "initramfs.cpio.gz")
    }
    verify_sources(sources, frozen, copied)
    verify_staged_stimulus(staged_stimulus)
    record = {
        "schema_version": 2,
        "scope": SCOPE,
        "cpu_profile": CPU_PROFILE,
        "version": VERSION,
        "roots": sorted(map(str, closure)),
        "commands": commands,
        "files": catalog,
        "guest_authored_environment": {
            "path": "/etc/environment",
            "sha256": digest(stage / "etc/environment"),
            "allowed_links": sorted(GUEST_ENV_LINKS),
        },
        "source_hashes": frozen,
        "stimulus": {
            "source": str(STIMULUS.relative_to(REPO)),
            "guest_path": "/opt/pb053/stimulus.lc3",
            "size": STIMULUS_SIZE,
            "sha256": STIMULUS_SHA,
            "purpose": STIMULUS_PURPOSE,
        },
        "qemu": {
            "path": str(QEMU),
            "size": QEMU.stat().st_size,
            "sha256": digest(QEMU),
        },
        "artifacts": artifacts,
    }
    write_manifest(record, output / "manifest.json")
    regular_hash(output / "manifest.json", MANIFEST_CAP)
    return record


def parse_result(serial, expected_run_id, expected_scenario):
    guest = validate_guest(
        parse_guest_marker(serial),
        STIMULUS.read_bytes(),
        expected_run_id,
        expected_scenario,
    )
    logs = []
    for line in serial.splitlines():
        if not line.startswith('{"process_log": "monitor",'):
            continue
        row = json.loads(
            line, object_pairs_hook=unique_pairs, parse_constant=reject_constant
        )
        if set(row) != {"process_log", "content"} or not isinstance(
            row["content"], str
        ):
            raise ValueError("Invalid retained monitor log envelope")
        logs.append(row["content"])
    if len(logs) != 1:
        raise ValueError("Expected one retained monitor log")
    capture = validate_capture(logs[0])
    stage = next(item for item in guest["stages"] if item["stage"] == "traffic_capture")
    capture["opcode_counts"] = {
        str(key): value for key, value in capture["opcode_counts"].items()
    }
    if any(
        stage[key] != capture[key]
        for key in ("packets", "bytes", "sha256", "reported_drops", "opcode_counts")
    ):
        raise ValueError("Guest capture stage/log mismatch")
    return guest


def parse_guest_marker(serial):
    return decode_marker(serial, MARKER, 8 * 1024 * 1024)


def check_prepared(prepared, manifest_sha256):
    prepared = Path(prepared).resolve()
    manifest = read_manifest(prepared / "manifest.json", manifest_sha256)
    hashes, qemu = validate_manifest(manifest)
    # Input-shape rejection precedes environment checks, for portable CLI tests.
    for name, cap in ARTIFACT_CAPS.items():
        regular_hash(prepared / name, cap)
    if (
        os.uname().release != VERSION
        or Path("/run/booted-system/kernel").resolve() != KERNEL
    ):
        raise ValueError("Running kernel does not match booted 7.1.5 kernel")
    if (
        not QEMU.is_file()
        or not EMULATOR.is_file()
        or not KERNEL.is_file()
        or not Path("/proc/config.gz").is_file()
    ):
        raise ValueError("Required local runtime missing")
    qemu_size, qemu_hash = regular_hash(QEMU, 2 * 1024**3)
    if qemu_size != qemu["size"] or qemu_hash != qemu["sha256"]:
        raise ValueError("QEMU identity mismatch")
    if regular_hash(EMULATOR, 64 * 1024**2)[1] != EMULATOR_SHA:
        raise ValueError("Emulator identity mismatch")
    if regular_hash(KERNEL, ARTIFACT_CAPS["kernel"])[1] != hashes["kernel"]:
        raise ValueError("Kernel pinned source mismatch")
    if (
        regular_hash("/proc/config.gz", ARTIFACT_CAPS["config.gz"])[1]
        != hashes["config.gz"]
    ):
        raise ValueError("Current kernel config mismatch")
    for name, cap in ARTIFACT_CAPS.items():
        if regular_hash(prepared / name, cap)[1] != hashes[name]:
            raise ValueError(f"Prepared artifact hash mismatch: {name}")
    return {
        "scope": SCOPE,
        "cpu_profile": CPU_PROFILE,
        "artifacts": hashes,
        "qemu": qemu,
        "manifest_sha256": manifest_sha256,
    }


class RunCancellation:
    def __init__(self):
        self.cancelled_signal = None
        self._handlers = {}

    def latch(self, signum):
        if signum is not None and self.cancelled_signal is None:
            self.cancelled_signal = signum

    def _handle(self, signum, _frame):
        self.latch(signum)

    def __enter__(self):
        if threading.current_thread() is not threading.main_thread():
            raise ValueError("RunCancellation must run in main thread")
        for signum in (signal.SIGINT, signal.SIGTERM):
            self._handlers[signum] = signal.getsignal(signum)
            signal.signal(signum, self._handle)
        return self

    def __exit__(self, _type, _value, _traceback):
        for signum, handler in self._handlers.items():
            signal.signal(signum, handler)

    def check(self):
        if self.cancelled_signal is not None:
            raise ValueError(f"Run cancelled by signal {self.cancelled_signal}")


def seal_run_record(record, path, cancellation):
    def update():
        record["cancelled_signal"] = cancellation.cancelled_signal
        record["cancellation"] = cancellation.cancelled_signal is not None
        if record["cancellation"]:
            record["ok"] = False
            if record.get("error") is None:
                record["error"] = (
                    f"ValueError: Run cancelled by signal {cancellation.cancelled_signal}"
                )

    update()
    path.write_text(json.dumps(record, indent=2) + "\n")
    if cancellation.cancelled_signal != record["cancelled_signal"]:
        update()
        path.write_text(json.dumps(record, indent=2) + "\n")


def run(prepared, output, timeout, manifest_sha256, scenario="normal"):
    prepared = Path(prepared).resolve()
    output = exclusive(output, protected=(prepared,))
    if scenario not in ("normal", "hold") or not isinstance(scenario, str):
        raise ValueError("Scenario must be normal or hold")
    if type(timeout) is not int or timeout < 30 or timeout > 600:
        raise ValueError("Timeout must be 30..600 seconds")
    checked = check_prepared(prepared, manifest_sha256)
    hashes, qemu = checked["artifacts"], checked["qemu"]
    output.mkdir()
    run_id = os.urandom(16).hex()
    record = {
        "scope": SCOPE,
        "cpu_profile": CPU_PROFILE,
        "manifest_sha256": manifest_sha256,
        "prepared_hashes": hashes,
        "qemu": qemu,
        "deadline_seconds": timeout,
        "start": time.time(),
        "run_id": run_id,
        "scenario": scenario,
        "ok": False,
        "process": None,
        "snapshot_hashes": {},
        "guest_result": None,
        "validation_error": None,
        "hold_ready": None,
        "cancelled_signal": None,
        "cancellation": False,
    }
    with RunCancellation() as cancellation:
        try:
            inputs = output / "inputs"
            inputs.mkdir()
            for name, cap in ARTIFACT_CAPS.items():
                cancellation.check()
                record["snapshot_hashes"][name] = snapshot(
                    prepared / name,
                    inputs / name,
                    cap,
                    hashes[name],
                    cancel_check=cancellation.check,
                )
                cancellation.check()
            argv = [
                str(QEMU),
                "-nodefaults",
                "-no-user-config",
                "-display",
                "none",
                "-monitor",
                "none",
                "-serial",
                "stdio",
                "-nic",
                "none",
                "-machine",
                "q35,accel=kvm",
                "-cpu",
                "host,ssbd=off",
                "-m",
                "4096",
                "-smp",
                "1",
                "-no-reboot",
                "-kernel",
                str(inputs / "kernel"),
                "-initrd",
                str(inputs / "initramfs.cpio.gz"),
                "-append",
                f"console=ttyS0 rdinit=/init panic=-1 pb053_guest=1 pb053_run={run_id} pb053_scenario={scenario}",
            ]
            record["argv"] = argv
            cancellation.check()
            record["process"] = run_owned(
                argv, output / "serial.log", timeout, max_log_bytes=33554432
            )
            cancellation.latch(record["process"]["cancelled_signal"])
            # Retain available hold and guest diagnostics before refusing cancellation.
            try:
                serial = (output / "serial.log").read_text(errors="replace")
                if scenario == "hold" or "PB053_HOLD_READY " in serial:
                    record["hold_ready"] = validate_hold_ready(
                        decode_marker(serial, "PB053_HOLD_READY ", 4096), run_id
                    )
                    if scenario != "hold":
                        raise ValueError("Unexpected hold readiness in normal run")
                record["guest_result"] = parse_guest_marker(serial)
                parse_result(serial, run_id, scenario)
            except (ValueError, RecursionError, TypeError) as exc:
                record["validation_error"] = str(exc)
            cancellation.check()
            if not record["process"]["ok"]:
                raise ValueError("Owned VM process did not complete successfully")
            if record["validation_error"] is not None:
                raise ValueError(record["validation_error"])
            cancellation.check()
            record["ok"] = True
        except BaseException as exc:
            record["error"] = f"{type(exc).__name__}: {exc}"
            if not isinstance(exc, Exception):
                raise
        finally:
            record["end"] = time.time()
            seal_run_record(record, output / "run-record.json", cancellation)
    if not record["ok"]:
        raise ValueError(record["error"])
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("--output", required=True)
    vm = sub.add_parser("run")
    vm.add_argument("--prepared", required=True)
    vm.add_argument("--output", required=True)
    vm.add_argument("--timeout", type=int, default=240)
    vm.add_argument("--manifest-sha256", required=True)
    vm.add_argument("--scenario", choices=("normal", "hold"), default="normal")
    check = sub.add_parser("check")
    check.add_argument("--prepared", required=True)
    check.add_argument("--manifest-sha256", required=True)
    args = parser.parse_args()
    try:
        result = (
            prepare(args.output)
            if args.action == "prepare"
            else check_prepared(args.prepared, args.manifest_sha256)
            if args.action == "check"
            else run(
                args.prepared,
                args.output,
                args.timeout,
                args.manifest_sha256,
                args.scenario,
            )
        )
        response = {"ok": True, "scope": result.get("scope")}
        if args.action == "check":
            response.update(result)
        if args.action == "prepare":
            prepared = Path(args.output).absolute()
            response.update(
                {
                    "prepared": str(prepared),
                    "manifest_sha256": digest(prepared / "manifest.json"),
                }
            )
        print(json.dumps(response))
    except (ValueError, OSError, subprocess.SubprocessError, KeyError) as exc:
        print(f"PB053: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
