#!/usr/bin/env python3
"""Strict compiler for the hash-pinned bundled BabbleSim OpenSSL build."""

import hashlib
from pathlib import Path
import subprocess
import sys

# strncpy copies at most inl bytes into malloc(inl+1), then buf[inl]='\0'.
# GCC14's truncation diagnostic does not describe a missing terminator here.
PINNED = {
    "crypto/bio/bss_log.c": (
        "db979649ebd92c04c4ec44836eee8320b8fef71ddd9fc118bd5a5e204deae4fe",
        "-Wno-stringop-truncation",
    ),
    # no-err removes reporting which consumes the macro-populated ASN.1 context.
    "crypto/asn1/x_pkey.c": (
        "f86e9dbce522471c42009c1536ecc37843c85c3e814cae252290440302b35681",
        "-Wno-unused-but-set-variable",
    ),
    # Legacy generic CMAC block-size analysis includes bl=0. The simulated
    # crypto interface uses AES ECB/CCM, not CMAC; this is not CMAC validation.
    "crypto/cmac/cmac.c": (
        "ea439db212fc198ad1355425d9b09570294ae318bbc5a0207ee213188c6bb215",
        "-Wno-stringop-overflow",
    ),
}


def main():
    if len(sys.argv) < 3:
        print(
            "OpenSSL compiler: expected COMPILER SOURCE_ROOT ARGS...", file=sys.stderr
        )
        return 2
    compiler, root, *args = sys.argv[1:]
    root = Path(root).resolve()
    flags = []
    if "-c" in args:
        sources = [
            arg for arg in args if arg.endswith(".c") and not arg.startswith("-")
        ]
        if len(sources) > 1:
            print("OpenSSL compiler: ambiguous C source invocation", file=sys.stderr)
            return 2
        for name in sources:
            path = Path(name).resolve()
            try:
                relative = path.relative_to(root).as_posix()
            except ValueError:
                continue
            if relative in PINNED:
                expected, flag = PINNED[relative]
                if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                    print(
                        "OpenSSL compiler: re-audit required: " + relative,
                        file=sys.stderr,
                    )
                    return 1
                flags.append(flag)
    return subprocess.run([compiler, *args, "-Werror", *flags]).returncode


if __name__ == "__main__":
    sys.exit(main())
