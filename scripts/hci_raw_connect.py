#!/usr/bin/env python3
"""Raw-HCI direct LE connect helper.

The kernel's LE auto-connect path (accept-list filtered background scan)
does not work with some hci_usb controllers (no advertising reports are
delivered when the accept-list filter is active), so BlueZ Pair/Connect
hangs forever.

This helper issues a direct LE Extended Create Connection to the peer and
then holds the raw HCI socket open — the kernel reaps connections created
through a raw socket as soon as it closes, so the socket must stay open for
the lifetime of the ACL link.

On controllers with all-zero FICR DEVICEADDR (e.g. nRF5340 SW Split) the
own random address must be set via HCI LE Set Random Address before the
connect. This script sets a stable locally-administered static random
address (MSBs 11xxxxxx for static random) via --own-addr or a default.

Usage (as root):
    python3 scripts/hci_raw_connect.py <peer-addr> [hold_seconds] [--own-addr ADDR]

Example:
    sudo python3 scripts/hci_raw_connect.py DB:A6:0C:05:A2:AA 120
    sudo python3 scripts/hci_raw_connect.py DB:A6:0C:05:A2:AA 120 --own-addr C0:AA:BB:CC:DD:EE
"""

import argparse
import socket
import struct
import sys
import time

HCI_COMMAND_PKT = 0x01

# Default static random address: locally administered, MSBs=0b11
DEFAULT_OWN_ADDR = "C0:AA:BB:CC:DD:EE"


def cmd(opcode, params=b""):
    return struct.pack("<BHB", HCI_COMMAND_PKT, opcode, len(params)) + params


def main():
    parser = argparse.ArgumentParser(description="Raw-HCI direct LE connect helper")
    parser.add_argument("peer", help="Peer BLE address (xx:xx:xx:xx:xx:xx)")
    parser.add_argument(
        "hold",
        nargs="?",
        type=float,
        default=120.0,
        help="Hold raw HCI socket open for N seconds (default 120)",
    )
    parser.add_argument(
        "--own-addr",
        default=DEFAULT_OWN_ADDR,
        help=f"Own static random address (default: {DEFAULT_OWN_ADDR})",
    )
    args = parser.parse_args()

    peer_str = args.peer
    hold = args.hold
    own_str = args.own_addr

    peer = bytes.fromhex(peer_str.replace(":", ""))[::-1]  # LE-first on the wire
    own = bytes.fromhex(own_str.replace(":", ""))  # LE-first on the wire

    s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_RAW, socket.BTPROTO_HCI)
    s.bind((0,))

    # Set own random address (required when controller BD_ADDR is all-zero).
    s.send(cmd(0x2005, own))
    time.sleep(0.1)
    print(f"[hci_raw_connect] own random address set to {own_str}", flush=True)

    # Stop any scan, disable address resolution, direct connect (1M).
    s.send(cmd(0x2042, bytes([0, 0, 0, 0, 0, 0])))
    time.sleep(0.2)
    s.send(cmd(0x202D, bytes([0x00])))
    time.sleep(0.2)
    body = bytes([0x00, 0x01, 0x01]) + peer + bytes([0x01])
    body += struct.pack("<HHHHHHHH", 0x60, 0x60, 0x18, 0x28, 0, 0x64, 0, 0)
    s.send(cmd(0x2043, body))
    print(
        f"[hci_raw_connect] connecting to {peer_str}, holding {hold:.0f}s", flush=True
    )

    time.sleep(hold)


if __name__ == "__main__":
    main()
