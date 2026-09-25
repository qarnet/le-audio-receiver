#!/usr/bin/env python3
"""Strict compiler for the pinned BabbleSim dependency build only.

Invocation: python3 bsim-component-cc.py REAL_GCC COMPONENT_ROOT GCC_ARGS...
The caller supplies this command as Make's CC, never as a general CC export.
"""

import hashlib
from pathlib import Path
import subprocess
import sys


# NCS v3.4.1 bsim_west sources. These are audited exceptions, not fixes for
# upstream FIFO/proc error handling or invalid BLE channel handling.
EXCEPTIONS = {
    "libUtilv1/src/bs_oswrap.c": (
        "0ff55f3d11d79892a5f8c7425f393fad8a17bd11ef96c05f17e10a844d05f869",
        "-Wno-unused-result",
    ),
    "libPhyComv1/src/bs_pc_base.c": (
        "fa6d7926e16716e86d29f461c02650ff0b99b7d869c14118ad1248539d848b77",
        "-Wno-unused-result",
    ),
    "ext_2G4_libPhyComv1/src/bs_pc_2G4_stateless.c": (
        "63a6f6ee4b462b485a4d4cb9b98f00812b928a06c624cff01565e7fef74e1163",
        "-Wno-unused-result",
    ),
    "ext_2G4_libPhyComv1/src/bs_pc_2G4_stateless_wo_callbacks.c": (
        "b036d89380ec5ed5c1bd50f7ff24b946d23984d1afbae3181c957277f9efd600",
        "-Wno-unused-result",
    ),
    "ext_2G4_libPhyComv1/src/bs_pc_2G4_utils.c": (
        "d75e783a86c96b954ae4f4f964eaadaa80a5b1a26c75992e853a5df9d27693c6",
        "-Wno-maybe-uninitialized",
    ),
}


def main():
    if len(sys.argv) < 4:
        print(
            "bsim-component-cc: expected REAL_GCC COMPONENT_ROOT GCC_ARGS...",
            file=sys.stderr,
        )
        return 2

    compiler, root, *args = sys.argv[1:]
    root = Path(root).resolve()
    exceptions = []
    if "-c" in args:
        # Make passes exactly one C translation unit; reject ambiguous invocations.
        sources = [
            arg for arg in args if arg.endswith(".c") and not arg.startswith("-")
        ]
        if len(sources) != 1:
            print("bsim-component-cc: expected one C source for -c", file=sys.stderr)
            return 2
        source = Path(sources[0]).resolve()
        try:
            relative = source.relative_to(root).as_posix()
        except ValueError:
            relative = None
        if relative in EXCEPTIONS:
            digest, flag = EXCEPTIONS[relative]
            try:
                actual = hashlib.sha256(source.read_bytes()).hexdigest()
            except OSError as exc:
                print(
                    "bsim-component-cc: re-audit required: %s: %s" % (relative, exc),
                    file=sys.stderr,
                )
                return 1
            if actual != digest:
                print(
                    "bsim-component-cc: re-audit required: %s: SHA-256 %s (expected %s)"
                    % (relative, actual, digest),
                    file=sys.stderr,
                )
                return 1
            exceptions.append(flag)

    try:
        return subprocess.run(
            [compiler, *args, "-Werror", *exceptions], check=False
        ).returncode
    except OSError as exc:
        print("bsim-component-cc: compiler failed to start: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
