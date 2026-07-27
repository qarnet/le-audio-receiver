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

Address type selection:
  --addr-type public  (default) — uses controller's public BD_ADDR.
     No LE Set Random Address needed. Use when the controller has a
     compile-time public address (bt_ctlr_set_public_addr).

  --addr-type random  — sets a random address via HCI LE Set Random
     Address before connect. Use for controllers with all-zero FICR
     DEVICEADDR (e.g. nRF5340 SW Split without identity fix).

Usage (as root):
    python3 scripts/hci_raw_connect.py <peer-addr> [hold_seconds] [--addr-type public|random] [--own-addr ADDR]

Example:
    sudo python3 scripts/hci_raw_connect.py DB:A6:0C:05:A2:AA 120
    sudo python3 scripts/hci_raw_connect.py DB:A6:0C:05:A2:AA 120 --addr-type random --own-addr C0:AA:BB:CC:DD:EE
"""

import argparse
import socket
import struct
import sys
import time

HCI_COMMAND_PKT = 0x01

# Default static random address (only used when --addr-type random).
# Locally-administered, MSBs=0b11.
DEFAULT_OWN_ADDR = "C0:AA:BB:CC:DD:EE"

# own_address_type values for HCI LE Extended Create Connection.
OWN_ADDR_PUBLIC = 0x00
OWN_ADDR_RANDOM = 0x01


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
        "--addr-type",
        choices=("public", "random"),
        default="public",
        help="Own address type for the connection (default: public). "
        "public = use controller BD_ADDR, no LE Set Random Address. "
        "random = set own random address via HCI 0x2005 before connect.",
    )
    parser.add_argument(
        "--own-addr",
        default=DEFAULT_OWN_ADDR,
        help=f"Own random address (used only with --addr-type random; default: {DEFAULT_OWN_ADDR})",
    )
    args = parser.parse_args()

    peer_str = args.peer
    hold = args.hold
    addr_type = args.addr_type
    own_str = args.own_addr

    peer = bytes.fromhex(peer_str.replace(":", ""))[::-1]  # LE-first on the wire

    s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_RAW, socket.BTPROTO_HCI)
    s.bind((0,))

    own_addr_type = OWN_ADDR_PUBLIC
    if addr_type == "random":
        own_addr_type = OWN_ADDR_RANDOM
        own = bytes.fromhex(own_str.replace(":", ""))  # LE-first on the wire
        s.send(cmd(0x2005, own))
        time.sleep(0.1)
        print(f"[hci_raw_connect] own random address set to {own_str}", flush=True)
    else:
        print("[hci_raw_connect] using controller public BD_ADDR", flush=True)

    # Stop any scan, disable address resolution, direct connect (1M).
    s.send(cmd(0x2042, bytes([0, 0, 0, 0, 0, 0])))
    time.sleep(0.2)
    s.send(cmd(0x202D, bytes([0x00])))
    time.sleep(0.2)
    body = bytes([0x00, own_addr_type, 0x01]) + peer + bytes([0x01])
    body += struct.pack("<HHHHHHHH", 0x60, 0x60, 0x18, 0x28, 0, 0x64, 0, 0)
    s.send(cmd(0x2043, body))
    print(
        f"[hci_raw_connect] connecting to {peer_str}, holding {hold:.0f}s", flush=True
    )

    time.sleep(hold)


if __name__ == "__main__":
    main()
