#!/usr/bin/env python3
"""PB-053 local-only emulator preparation; no host adapters or downloads.

Builds existing reviewed BlueZ source into an exclusive external directory.
Vendor sources remain read-only; compiler inputs, commands and logs retained.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import threading
import time

PIN = "4dc15be8ee3f7422d447087f1893d215575cb2c8"
SOURCES = [
    "emulator/main.c",
    "emulator/serial.c",
    "emulator/server.c",
    "emulator/vhci.c",
    "emulator/btdev.c",
    "emulator/bthost.c",
    "emulator/smp.c",
    "emulator/phy.c",
    "emulator/le.c",
    "lib/bluetooth/bluetooth.c",
    "lib/bluetooth/hci.c",
    "lib/bluetooth/sdp.c",
    "lib/bluetooth/uuid.c",
    "src/shared/mainloop.c",
    "src/shared/mainloop-notify.c",
    "src/shared/io-mainloop.c",
    "src/shared/timeout-mainloop.c",
    "src/shared/queue.c",
    "src/shared/util.c",
    "src/shared/crypto.c",
    "src/shared/ecc.c",
    "src/shared/hci.c",
    "src/shared/hci-crypto.c",
]


def source_macro_flags(source_bytes):
    if re.search(rb"^\s*#\s*define\s+_GNU_SOURCE\b", source_bytes, re.MULTILINE):
        return []
    return ["-D_GNU_SOURCE"]


class PreparationCancelled(Exception):
    pass


def file_identity(path):
    path = Path(path)
    if not path.is_file() or not os.access(path, os.R_OK):
        raise ValueError(f"Dependency not a readable regular file: {path}")
    contents = path.read_bytes()
    return {"size": len(contents), "sha256": hashlib.sha256(contents).hexdigest()}


def dependency_identities(depfile, directory):
    text = depfile.read_text().replace("\\\n", "")
    if not text.startswith("PB053_DEP:"):
        raise ValueError(f"Invalid dependency target: {depfile}")
    paths = shlex.split(text[len("PB053_DEP:") :])
    if not paths:
        raise ValueError(f"Empty dependency list: {depfile}")
    return {
        str((directory / path).resolve()): file_identity(directory / path)
        for path in paths
    }


def verify_inputs(source, inputs, dependencies):
    for name, digest in inputs.items():
        if file_identity(source / name)["sha256"] != digest:
            raise ValueError(f"Source changed during build: {name}")
    for name, paths in dependencies.items():
        for path, identity in paths.items():
            if file_identity(path) != identity:
                raise ValueError(f"Dependency changed during build: {name}: {path}")


def run_logged(argv, log_path, timeout):
    """Run one owned process group; bound leader and surviving descendants."""
    proc = None
    old_handlers = {}

    def cancel(signum, frame):
        raise PreparationCancelled(f"Interrupted by signal {signum}")

    if threading.current_thread() is threading.main_thread():
        for sig in (signal.SIGINT, signal.SIGTERM):
            old_handlers[sig] = signal.signal(sig, cancel)
    try:
        with Path(log_path).open("xb") as log:
            try:
                proc = subprocess.Popen(
                    argv,
                    start_new_session=True,
                    stdin=subprocess.DEVNULL,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                )
                result = proc.wait(timeout=timeout)
                if result:
                    raise subprocess.CalledProcessError(result, argv)
            finally:
                if proc is not None:
                    group = proc.pid
                    try:
                        os.killpg(group, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        proc.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        pass
                    deadline = time.monotonic() + 3
                    while True:
                        try:
                            os.killpg(group, 0)
                        except ProcessLookupError:
                            break
                        if time.monotonic() >= deadline:
                            try:
                                os.killpg(group, signal.SIGKILL)
                            except ProcessLookupError:
                                pass
                            break
                        time.sleep(0.05)
                    try:
                        proc.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        raise RuntimeError(f"Owned process did not exit: {argv}")
    finally:
        for sig, handler in old_handlers.items():
            signal.signal(sig, handler)


def check_output(source, output):
    raw = Path(output).absolute()
    if raw.is_symlink() or raw.exists():
        raise ValueError("Output must not already exist or be a symlink")
    output = raw.resolve()
    repo = Path(__file__).resolve().parents[1]
    home = Path.home().resolve()
    protected = Path("/tmp/opencode")
    if (
        output == Path("/")
        or output == home
        or home in output.parents
        or output == Path("/nix/store")
        or Path("/nix/store") in output.parents
        or output == repo
        or repo in output.parents
        or output == source
        or source in output.parents
        or (
            protected in output.parents
            and any(
                part.startswith("pb053-")
                for part in output.relative_to(protected).parts[:-1]
            )
        )
    ):
        raise ValueError("Output must be a fresh external directory")
    if not output.parent.is_dir():
        raise ValueError("Output parent must already exist")
    return output


def prepare(source, output, compiler):
    source = Path(source).resolve()
    output = check_output(source, output)
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != PIN:
        raise ValueError("BlueZ source revision differs from reviewed pin")
    if subprocess.check_output(
        ["git", "-C", str(source), "status", "--porcelain"], text=True
    ):
        raise ValueError("Vendor source must be clean")
    inputs = {
        name: hashlib.sha256((source / name).read_bytes()).hexdigest()
        for name in SOURCES
    }
    output.mkdir(exist_ok=False)
    objects = []
    commands = []
    completed = []
    dependencies = {}
    record = {
        "schema_version": 2,
        "source_revision": revision,
        "sources": inputs,
        "commands": commands,
        "completed_commands": completed,
        "dependencies": dependencies,
        "scope": "local build, no host controller execution",
        "outcome": "failed",
    }
    flags = [
        "-std=gnu11",
        "-O2",
        "-Wall",
        "-Werror",
        '-DVERSION="5.87"',
        "-ffunction-sections",
        "-fdata-sections",
        "-I",
        str(source),
        "-I",
        str(source / "lib"),
    ]
    try:
        executable = shutil.which(compiler[0]) if compiler else None
        if executable is None:
            raise ValueError("Compiler executable unavailable")
        executable = Path(executable).resolve()
        record["compiler"] = {
            "executable": str(executable),
            **file_identity(executable),
        }
        version_argv = compiler + ["--version"]
        commands.append(version_argv)
        run_logged(version_argv, output / "compiler-version.log", 10)
        completed.append(version_argv)
        record["compiler"]["version"] = (output / "compiler-version.log").read_text()
        for number, name in enumerate(SOURCES):
            obj = output / f"{number:02d}.o"
            depfile = output / f"{number:02d}.d"
            argv = (
                compiler
                + flags
                + source_macro_flags((source / name).read_bytes())
                + ["-MD", "-MF", str(depfile), "-MT", "PB053_DEP"]
                + ["-c", str(source / name), "-o", str(obj)]
            )
            commands.append(argv)
            run_logged(argv, output / f"{number:02d}.log", 120)
            completed.append(argv)
            dependencies[name] = dependency_identities(depfile, source)
            objects.append(str(obj))
        binary = output / "btvirt"
        argv = compiler + ["-Wl,--gc-sections"] + objects + ["-o", str(binary)]
        commands.append(argv)
        run_logged(argv, output / "link.log", 120)
        completed.append(argv)
        # --version exits before any controller/emulator device is created.
        argv = [str(binary), "--version"]
        commands.append(argv)
        run_logged(argv, output / "version.log", 10)
        completed.append(argv)
        version = (output / "version.log").read_text().strip()
        if version != "5.87":
            raise ValueError("Emulator version mismatch")
        if (
            subprocess.check_output(
                ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
            ).strip()
            != PIN
        ):
            raise ValueError("BlueZ source revision changed during build")
        if subprocess.check_output(
            ["git", "-C", str(source), "status", "--porcelain"], text=True
        ):
            raise ValueError("Vendor source changed during build")
        verify_inputs(source, inputs, dependencies)
        record.update(
            outcome="success",
            binary={"path": str(binary), **file_identity(binary)},
            version=version,
        )
        return binary
    except BaseException as exc:
        record["error"] = f"{type(exc).__name__}: {exc}"
        raise
    finally:
        try:
            with (output / "build-record.json").open("x") as stream:
                json.dump(record, stream, indent=2)
                stream.write("\n")
        except OSError:
            if record["outcome"] == "success":
                raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    binary = prepare(args.source, args.output, shlex.split(args.cc))
    print(
        json.dumps(
            {
                "binary": str(binary),
                "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            }
        )
    )


if __name__ == "__main__":
    main()
