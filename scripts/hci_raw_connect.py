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

Usage (as root):
    python3 scripts/hci_raw_connect.py <peer-addr> [hold_seconds]

Example:
    sudo python3 scripts/hci_raw_connect.py DB:A6:0C:05:A2:AA 120
"""

import socket
import struct
import sys
import time

HCI_COMMAND_PKT = 0x01


def cmd(opcode, params=b""):
    return struct.pack("<BHB", HCI_COMMAND_PKT, opcode, len(params)) + params


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    peer_str = sys.argv[1]
    hold = float(sys.argv[2]) if len(sys.argv) > 2 else 120.0

    peer = bytes.fromhex(peer_str.replace(":", ""))[::-1]  # LE-first on the wire

    s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_RAW, socket.BTPROTO_HCI)
    s.bind((0,))

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
