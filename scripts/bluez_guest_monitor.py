#!/usr/bin/env python3
"""Bounded raw guest-only Linux Bluetooth monitor; never interpret payloads."""

import ctypes
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import sys
import time


CAP = 4 * 1024 * 1024
RESERVE = 4096
READY = "PB053_MONITOR_READY "
RESULT = "PB053_MONITOR_RESULT "
HEADER = struct.Struct("<HHH")  # hci_mon.h: packed opcode,index,length
STOP = None


# include/net/bluetooth/hci_sock.h: sockaddr_hci has three native unsigned shorts.
class SockaddrHci(ctypes.Structure):
    _fields_ = [
        ("hci_family", ctypes.c_ushort),
        ("hci_dev", ctypes.c_ushort),
        ("hci_channel", ctypes.c_ushort),
    ]


def requested_stop(signum, frame):
    global STOP
    STOP = signum


def encode(prefix, value):
    return (prefix + json.dumps(value, separators=(",", ":")) + "\n").encode()


def collect(sock, output, stop_predicate, cap=CAP):
    """Read datagrams until signal-number stop; output accepts UTF-8 text."""
    if type(cap) is not int or cap <= RESERVE:
        raise ValueError("Monitor cap must be integer greater than terminal reserve")
    sha = hashlib.sha256()
    count = total = emitted = 0
    drops = None
    error = None
    stopped = None

    def emit(row, reserved=0):
        nonlocal emitted
        data = row.encode()
        if emitted + len(data) + reserved > cap:
            raise ValueError("Monitor output cap exceeded")
        output.write(row)
        output.flush()
        emitted += len(data)

    try:
        emit(encode(READY, {"schema_version": 1}).decode(), RESERVE)
        while True:
            stopped = stop_predicate()
            if stopped is not None:
                if type(stopped) is not int or stopped not in (2, 15):
                    raise ValueError("Invalid requested stop signal")
                break
            if not select.select([sock], [], [], 0.1)[0]:
                continue
            wall_ns = time.time_ns()
            monotonic_ns = time.monotonic_ns()
            data, control, flags, _ = sock.recvmsg(65541, 256)
            if not data:
                raise ValueError("Monitor EOF")
            if flags & (socket.MSG_TRUNC | socket.MSG_CTRUNC):
                raise ValueError("Monitor datagram/control truncated")
            if len(data) < HEADER.size:
                raise ValueError("Monitor short header")
            opcode, index, length = HEADER.unpack_from(data)
            if length != len(data) - HEADER.size:
                raise ValueError("Monitor payload length mismatch")
            kernel_ns = None
            for level, kind, value in control:
                if level != socket.SOL_SOCKET:
                    continue
                if kind == 35:  # Linux x86_64 SO_TIMESTAMPNS, native timespec
                    if len(value) != 16 or kernel_ns is not None:
                        raise ValueError("Invalid timestamp ancillary")
                    sec, nano = struct.unpack("=qq", value)
                    if sec < 0 or not 0 <= nano < 1000000000:
                        raise ValueError("Invalid kernel timestamp")
                    kernel_ns = sec * 1000000000 + nano
                elif kind == 40:  # Linux SO_RXQ_OVFL, uint32 cumulative drops
                    if len(value) != 4:
                        raise ValueError("Invalid drop ancillary")
                    observed = struct.unpack("=I", value)[0]
                    if drops is not None and observed < drops:
                        raise ValueError("Drop counter regressed")
                    drops = observed
            if drops:
                raise ValueError("Monitor receive queue reported drops")
            row = {
                "packet": count,
                "wall_ns": wall_ns,
                "monotonic_ns": monotonic_ns,
                "kernel_timestamp_ns": kernel_ns,
                "opcode": opcode,
                "index": index,
                "raw_hex": data.hex(),
                "flags": flags,
                "reported_drops": drops,
            }
            emit(json.dumps(row, separators=(",", ":")) + "\n", RESERVE)
            sha.update(struct.pack("<I", len(data)))
            sha.update(data)
            count += 1
            total += len(data)
    except (OSError, ValueError) as exc:
        error = str(exc)[:512]
    result = {
        "schema_version": 1,
        "ok": error is None and stopped in (2, 15),
        "packets": count,
        "bytes": total,
        "sha256": sha.hexdigest(),
        "error": error,
        "stop_signal": stopped,
        "reported_drops": drops,
    }
    emit(encode(RESULT, result).decode())
    return result


def main():
    runtime = (
        json.loads(Path("/opt/pb053/runtime.json").read_text())
        if "pb053_guest=1" in Path("/proc/cmdline").read_text().split()
        else {}
    )
    if (
        "pb053_guest=1" not in Path("/proc/cmdline").read_text().split()
        or not isinstance(runtime, dict)
        or type(runtime.get("pb053_guest")) is not int
        or runtime["pb053_guest"] != 1
    ):
        raise RuntimeError("Monitor requires pb053_guest=1")
    signal.signal(signal.SIGTERM, requested_stop)
    signal.signal(signal.SIGINT, requested_stop)
    # include/net/bluetooth/hci_sock.h: HCI_DEV_NONE=65535, MONITOR=2.
    with socket.socket(
        socket.AF_BLUETOOTH, socket.SOCK_RAW, socket.BTPROTO_HCI
    ) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2 * 1024 * 1024)
        sock.setsockopt(socket.SOL_SOCKET, 35, 1)
        sock.setsockopt(socket.SOL_SOCKET, 40, 1)
        libc = ctypes.CDLL(None, use_errno=True)
        libc.bind.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
        libc.bind.restype = ctypes.c_int
        # Python's HCI tuple binder supports device format only; pass full
        # six-byte Linux sockaddr_hci to kernel without opening host HCI.
        address = SockaddrHci(socket.AF_BLUETOOTH, 65535, 2)
        if (
            libc.bind(sock.fileno(), ctypes.byref(address), ctypes.sizeof(address))
            == -1
        ):
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error))
        return 0 if collect(sock, sys.stdout, lambda: STOP)["ok"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"PB053 monitor refusal: {exc}", file=sys.stderr)
        sys.exit(1)
