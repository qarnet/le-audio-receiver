#!/usr/bin/env python3
"""Put verified ELF32 runtime libraries first for native BabbleSim links."""

import argparse
import os
from pathlib import Path
import shlex
import shutil
import stat
import sys
from tempfile import TemporaryDirectory

from bluez_host_guest import run_bounded_command


QUERY_LIMIT = 64 * 1024


def _safe_path(value):
    if (
        not value
        or not Path(value).is_absolute()
        or any(c.isspace() or ord(c) < 32 or ord(c) == 127 for c in value)
    ):
        raise ValueError("compiler returned an invalid library path")
    return Path(value)


def _regular(path):
    try:
        return stat.S_ISREG(path.resolve(strict=True).stat().st_mode)
    except (OSError, RuntimeError):
        return False


def _elf32(path):
    try:
        resolved = path.resolve(strict=True)
        fd = os.open(resolved, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        try:
            if not stat.S_ISREG(os.fstat(fd).st_mode):
                return False
            header = os.read(fd, 20)
        finally:
            os.close(fd)
    except (OSError, RuntimeError):
        return False
    return (
        len(header) == 20
        and header[:4] == b"\x7fELF"
        and header[4:6] == b"\x01\x01"
        and header[18:20] == b"\x03\x00"
    )


def _query(compiler, name, *, timeout=10):
    try:
        with TemporaryDirectory() as temporary:
            output = run_bounded_command(
                [compiler, "-m32", f"-print-file-name={name}"],
                Path(temporary),
                "query",
                QUERY_LIMIT,
                QUERY_LIMIT,
                timeout,
            )
            with output.open("rb") as stream:
                data = stream.read(QUERY_LIMIT + 1)
        if len(data) > QUERY_LIMIT:
            raise ValueError("compiler output exceeded limit")
        return _safe_path(data.decode("utf-8").removesuffix("\n"))
    except (ValueError, OSError, UnicodeError) as exc:
        raise ValueError(f"compiler query failed for {name}: {exc}") from exc


def resolve_library_dirs(compiler="gcc"):
    """Return (glibc ELF32 directory, libgcc ELF32 directory)."""
    found = shutil.which(compiler)
    if found is None or not _regular(Path(found)) or not os.access(found, os.X_OK):
        raise ValueError(f"compiler is not an executable file: {compiler}")

    libc = _query(found, "libc.so")
    if not _regular(libc):
        raise ValueError(f"libc.so not a regular file: {libc}")
    libc_dir = libc.parent
    for name in ("libc.so.6", "libm.so", "libdl.so", "libpthread.so"):
        if not _elf32(libc_dir / name):
            raise ValueError(f"missing ELF32 i386 {name} in {libc_dir}")

    reported = _query(found, "libgcc_s.so.1")
    candidates = dict.fromkeys(
        (
            reported,
            reported.parent / "32" / reported.name,
            reported.parent.parent / "lib" / reported.name,
            reported.parent.parent / "lib32" / reported.name,
        )
    )
    gcc_lib = next((path for path in candidates if _elf32(path)), None)
    if gcc_lib is None:
        raise ValueError("no ELF32 i386 libgcc_s.so.1 at compiler-reported locations")
    gcc_dir = gcc_lib.parent
    for directory in (libc_dir, gcc_dir):
        resolved = _safe_path(str(directory.resolve(strict=True)))
        if str(resolved) == "/" or str(directory) == "/":
            raise ValueError("library search directory must not be root")
    return str(libc_dir), str(gcc_dir)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="gcc")
    args = parser.parse_args()
    try:
        libc_dir, gcc_dir = resolve_library_dirs(args.compiler)
        flags = f"-L{libc_dir} -L{gcc_dir}"
        old_flags = os.environ.get("NIX_LDFLAGS", "")
        if old_flags:
            flags += " " + old_flags
        print(f"export NIX_LDFLAGS={shlex.quote(flags)}")
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
