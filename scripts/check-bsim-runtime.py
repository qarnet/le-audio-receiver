#!/usr/bin/env python3
"""Prove default PHY models load before launching simulator peers."""

import argparse
import os
import pty
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import time
import uuid


def check(root, timeout, cancelled=lambda: False, peers=()):
    root = root.resolve()
    phy = root / "bin/bs_2G4_phy_v1"
    if not phy.is_file() or not os.access(phy, os.X_OK):
        raise RuntimeError("PHY executable missing: %s" % phy)
    for name in (
        "lib_2G4Channel_NtNcable.so",
        "lib_2G4Modem_Magic.so",
        "libCryptov1.so",
    ):
        plugin = root / "lib" / name
        if not plugin.is_file() or not plugin.stat().st_size:
            raise RuntimeError(
                "required simulator runtime library missing or empty: %s" % plugin
            )
    probe = root / "bin/bs_crypto_probe"

    def elf_abi(path):
        with path.open("rb") as stream:
            header = stream.read(20)
        if len(header) < 20 or header[:4] != b"\x7fELF":
            raise RuntimeError("not an ELF runtime artifact: %s" % path)
        return header[4:6], header[18:20]

    crypto_abi = elf_abi(root / "lib/libCryptov1.so")
    if elf_abi(probe) != crypto_abi:
        raise RuntimeError("crypto probe/library ELF ABI mismatch")
    for peer in peers:
        if elf_abi(peer) != crypto_abi:
            raise RuntimeError("encrypted peer/library ELF ABI mismatch: %s" % peer)
    crypto = subprocess.Popen(
        [str(probe)],
        cwd=root / "bin",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    try:
        deadline = time.monotonic() + min(timeout, 10)
        while crypto.poll() is None:
            if cancelled():
                raise RuntimeError("runtime preflight cancelled")
            if time.monotonic() >= deadline:
                raise RuntimeError("crypto probe did not complete before deadline")
            time.sleep(0.01)
        output, _ = crypto.communicate()
        if crypto.returncode != 0:
            raise RuntimeError(
                "encrypted-peer runtime failed: " + output.decode(errors="replace")
            )
    finally:
        if crypto.poll() is None:
            crypto.terminate()
            try:
                crypto.wait(timeout=2)
            except subprocess.TimeoutExpired:
                crypto.kill()
        if crypto.stdout is not None and not crypto.stdout.closed:
            crypto.communicate()
        else:
            crypto.wait()
    if cancelled():
        raise RuntimeError("runtime preflight cancelled")
    # A terminal gives C stdout line buffering without LD_PRELOAD ABI assumptions.
    master, slave = pty.openpty()
    try:
        process = subprocess.Popen(
            [
                str(phy),
                "-v=9",
                "-s=runtime_check_" + uuid.uuid4().hex,
                "-D=2",
                "-sim_length=1e3",
                "-nodump",
            ],
            cwd=root / "bin",
            stdout=slave,
            stderr=slave,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
    except BaseException:
        os.close(master)
        raise
    finally:
        os.close(slave)
    output = bytearray()
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(master, selectors.EVENT_READ)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if cancelled():
                    raise RuntimeError("PHY preflight cancelled")
                for key, _events in selector.select(
                    min(0.1, max(0, deadline - time.monotonic()))
                ):
                    block = os.read(key.fd, 4096)
                    if not block:
                        raise RuntimeError(
                            "PHY exited before model readiness (exit %s): %s"
                            % (process.poll(), output.decode(errors="replace"))
                        )
                    output.extend(block)
                    if len(output) > 65536:
                        raise RuntimeError("PHY preflight output exceeded limit")
                    if b"ERROR:" in output or b"WARNING:" in output:
                        raise RuntimeError(
                            "PHY model initialization failed: "
                            + output.decode(errors="replace")
                        )
                    # Installed PHY prints this only after RTLD_NOW/dlsym and
                    # model initialization, immediately before peer connection.
                    if b"main: Connecting..." in output:
                        if cancelled():
                            raise RuntimeError("PHY preflight cancelled")
                        return
            raise RuntimeError(
                "PHY did not reach model readiness before deadline: "
                + output.decode(errors="replace")
            )
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
        process.wait()
        os.close(master)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=10)
    parser.add_argument("--peer", type=Path, action="append", default=[])
    args = parser.parse_args()
    if not 0 < args.timeout <= 60:
        parser.error("--timeout must be > 0 and <= 60 seconds")
    # Record cancellation rather than raising asynchronously during Popen:
    # ownership must be established before any signal can trigger cleanup.
    pending = False

    def cancel(signum, _frame):
        nonlocal pending
        pending = True

    previous = {
        sig: signal.signal(sig, cancel) for sig in (signal.SIGTERM, signal.SIGINT)
    }
    try:
        check(args.root, args.timeout, cancelled=lambda: bool(pending), peers=args.peer)
    except (OSError, RuntimeError) as exc:
        print("BabbleSim runtime preflight FAILED: %s" % exc, file=sys.stderr)
        return 1
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)
    print("BabbleSim runtime ready: default NtNcable/Magic models initialized")
    return 0


if __name__ == "__main__":
    sys.exit(main())
