#!/usr/bin/env python3
"""Raw-HCI direct LE connect helper with confirmed-connect retry.

The kernel's LE auto-connect path (accept-list filtered background scan)
does not work with some hci_usb controllers (no advertising reports are
delivered when the accept-list filter is active), so BlueZ Pair/Connect
hangs forever.

This helper issues LE Extended Create Connection (0x2043) to the exact
peer, watches HCI events for the connection-complete event (legacy LE
Connection Complete 0x01 or LE Enhanced Connection Complete 0x0A) for
THAT peer, and retries with backoff until the link is confirmed, the
connect deadline expires, or a fatal command error occurs.  On success it
emits a stable machine-readable line on stdout

    HCI_CONNECT_READY peer=<addr> addr_type=<n> handle=0x<handle>

then holds the raw HCI socket open — the kernel reaps connections created
through a raw socket as soon as it closes, so the socket must stay open for
the lifetime of the ACL link.  On failure it emits

    HCI_CONNECT_FAIL reason=<...> attempts=<n> last_status=<...>

and exits nonzero.  During the hold phase a link drop emits

    HCI_LINK_DOWN handle=0x<handle> reason=0x<reason>

Address type selection:
  --addr-type public  (default) — uses controller's public BD_ADDR.
     No LE Set Random Address needed. Use when the controller has a
     compile-time public address (bt_ctlr_set_public_addr).

  --addr-type random  — sets a random address via HCI LE Set Random
     Address before connect. Use for controllers with all-zero FICR
     DEVICEADDR (e.g. nRF5340 SW Split without identity fix).

Usage (as root):
    python3 scripts/hci_raw_connect.py <peer-addr> [hold_seconds] \\
        [--addr-type public|random] [--own-addr ADDR] \\
        [--peer-addr-type public|random] \\
        [--connect-deadline SECONDS] [--backoff SECONDS] \\
        [--attempt-timeout SECONDS]

Example:
    sudo python3 scripts/hci_raw_connect.py DB:A6:0C:05:A2:AA 120
    sudo python3 scripts/hci_raw_connect.py DB:A6:0C:05:A2:AA 120 \\
        --addr-type random --own-addr C0:AA:BB:CC:DD:EE
"""

import argparse
import select
import socket
import struct
import sys
import time

HCI_COMMAND_PKT = 0x01
HCI_ACLDATA_PKT = 0x02
HCI_EVENT_PKT = 0x04

# Event codes (BlueZ hci.h: EVT_CMD_STATUS 0x0f, EVT_CMD_COMPLETE 0x0e,
# EVT_LE_META_EVENT 0x3e, EVT_DISCONN_COMPLETE 0x05).
EVT_CMD_STATUS = 0x0F
EVT_CMD_COMPLETE = 0x0E
EVT_LE_META_EVENT = 0x3E
EVT_DISCONN_COMPLETE = 0x05

# LE meta subevents (Zephyr include/zephyr/bluetooth/hci_types.h:
# BT_HCI_LE_CONN_COMPLETE 0x01, BT_HCI_EVT_LE_ENH_CONN_COMPLETE 0x0a).
LE_SUBEVT_CONN_COMPLETE = 0x01
LE_SUBEVT_ENH_CONN_COMPLETE = 0x0A

# Opcodes (Zephyr hci_types.h).
OP_LE_SET_RANDOM_ADDR = 0x2005
OP_LE_EXT_CREATE_CONN = 0x2043  # BT_HCI_OP_LE_EXT_CREATE_CONN
# LE_Create_Connection_Cancel also cancels an in-progress
# LE_Extended_Create_Connection (BT Core Spec Vol 4 Part E 7.8.25).
OP_LE_CREATE_CONN_CANCEL = 0x200E

# Machine-readable stdout tokens (bap_central.py gates on READY).
READY_PREFIX = b"HCI_CONNECT_READY"
READY_PREFIX_TEXT = "HCI_CONNECT_READY"
FAIL_PREFIX = "HCI_CONNECT_FAIL"
LINK_DOWN_PREFIX = "HCI_LINK_DOWN"

# Default static random address (only used when --addr-type random).
# Locally-administered, MSBs=0b11.
DEFAULT_OWN_ADDR = "C0:AA:BB:CC:DD:EE"

# own_address_type values for HCI LE Extended Create Connection.
OWN_ADDR_PUBLIC = 0x00
OWN_ADDR_RANDOM = 0x01


class HciPacketError(ValueError):
    """Malformed or unrecognized raw HCI packet."""


def parse_hci_packet(buf):
    """Parse one raw HCI packet.

    Returns (packet_type, body).  buf must contain exactly one packet: the
    type byte plus the body.  Event body = [evt_code][plen][payload];
    command body = [opcode LE][plen][params]; ACL body = [handle][len LE][data].
    Raises HciPacketError for unknown types, truncated headers, or payload
    length mismatches.
    """
    if len(buf) < 1:
        raise HciPacketError("empty packet")
    ptype = buf[0]
    body = buf[1:]
    if ptype == HCI_EVENT_PKT:
        if len(body) < 2:
            raise HciPacketError("truncated event header")
        plen = body[1]
        if len(body) != 2 + plen:
            raise HciPacketError(
                "event length mismatch: declared %d, got %d" % (plen, len(body) - 2)
            )
        return (ptype, body)
    if ptype == HCI_COMMAND_PKT:
        if len(body) < 3:
            raise HciPacketError("truncated command header")
        plen = body[2]
        if len(body) != 3 + plen:
            raise HciPacketError(
                "command length mismatch: declared %d, got %d" % (plen, len(body) - 3)
            )
        return (ptype, body)
    if ptype == HCI_ACLDATA_PKT:
        if len(body) < 4:
            raise HciPacketError("truncated ACL header")
        dlen = struct.unpack_from("<H", body, 2)[0]
        if len(body) != 4 + dlen:
            raise HciPacketError(
                "ACL length mismatch: declared %d, got %d" % (dlen, len(body) - 4)
            )
        return (ptype, body)
    raise HciPacketError("unknown packet type 0x%02x" % ptype)


def parse_event(body):
    """Parse an HCI event payload (event body incl. code+plen header) into a dict.

    Known results:
      {'evt': 'cmd_status', 'opcode': int, 'status': int}
      {'evt': 'cmd_complete', 'opcode': int}
      {'evt': 'le_conn_complete', 'subevent': int, 'status': int,
       'handle': int, 'role': int, 'peer_addr_type': int,
       'peer_addr': bytes(6), 'interval': int, 'latency': int,
       'supv_timeout': int, 'clock_accuracy': int}
      {'evt': 'disconn_complete', 'handle': int, 'reason': int}
      {'evt': 'unknown', 'code': int}
      {'evt': 'unknown_le_meta', 'subevent': int}

    Raises ValueError on length mismatches / truncated known-event payloads.
    """
    if len(body) < 2:
        raise ValueError("truncated event body")
    code = body[0]
    plen = body[1]
    if plen != len(body) - 2:
        raise ValueError(
            "event length mismatch: declared %d, got %d" % (plen, len(body) - 2)
        )
    p = body[2:]
    if code == EVT_CMD_STATUS:
        if len(p) < 4:
            raise ValueError("truncated command status")
        return {
            "evt": "cmd_status",
            "status": p[0],
            "opcode": struct.unpack_from("<H", p, 2)[0],
        }
    if code == EVT_CMD_COMPLETE:
        if len(p) < 3:
            raise ValueError("truncated command complete")
        return {"evt": "cmd_complete", "opcode": struct.unpack_from("<H", p, 1)[0]}
    if code == EVT_DISCONN_COMPLETE:
        if len(p) < 4:
            raise ValueError("truncated disconnect complete")
        return {
            "evt": "disconn_complete",
            "handle": struct.unpack_from("<H", p, 1)[0],
            "reason": p[3],
        }
    if code == EVT_LE_META_EVENT:
        if len(p) < 1:
            raise ValueError("truncated LE meta event")
        sub = p[0]
        if sub in (LE_SUBEVT_CONN_COMPLETE, LE_SUBEVT_ENH_CONN_COMPLETE):
            if len(p) < 19:
                raise ValueError("truncated LE connection complete")
            status = p[1]
            handle = struct.unpack_from("<H", p, 2)[0]
            role = p[4]
            peer_type = p[5]
            peer = p[6:12]
            if sub == LE_SUBEVT_ENH_CONN_COMPLETE:
                if len(p) < 31:
                    raise ValueError("truncated LE enhanced connection complete")
                interval = struct.unpack_from("<H", p, 24)[0]
                latency = struct.unpack_from("<H", p, 26)[0]
                supv = struct.unpack_from("<H", p, 28)[0]
                clock = p[30]
            else:
                interval = struct.unpack_from("<H", p, 12)[0]
                latency = struct.unpack_from("<H", p, 14)[0]
                supv = struct.unpack_from("<H", p, 16)[0]
                clock = p[18]
            return {
                "evt": "le_conn_complete",
                "subevent": sub,
                "status": status,
                "handle": handle,
                "role": role,
                "peer_addr_type": peer_type,
                "peer_addr": peer,
                "interval": interval,
                "latency": latency,
                "supv_timeout": supv,
                "clock_accuracy": clock,
            }
        return {"evt": "unknown_le_meta", "subevent": sub}
    return {"evt": "unknown", "code": code}


def parse_peer(addr_str):
    """'XX:XX:XX:XX:XX:XX' -> 6 bytes in little-endian wire order.

    Raises ValueError on malformed input.
    """
    try:
        raw = bytes.fromhex(addr_str.replace(":", ""))
    except ValueError:
        raise ValueError("non-hex address %r" % addr_str)
    if len(raw) != 6:
        raise ValueError("address %r must be 6 octets" % addr_str)
    return raw[::-1]


def format_peer(addr_le):
    """6 little-endian wire bytes -> 'XX:XX:XX:XX:XX:XX'."""
    return ":".join("%02X" % b for b in addr_le[::-1])


def build_ext_create_conn(own_addr_type, peer_addr_le, peer_addr_type):
    """LE Extended Create Connection params for one LE 1M PHY.

    Matches Zephyr struct bt_hci_cp_le_ext_create_conn plus one
    bt_hci_ext_conn_phy block: filter_policy=0, own_addr_type, peer
    (type+6 bytes), phys=0x01, then scan_interval/scan_window/conn
    interval min/max/latency/supervision timeout/min-max CE length.
    """
    return (
        bytes([0x00, own_addr_type, peer_addr_type])
        + peer_addr_le
        + bytes([0x01])
        + struct.pack("<HHHHHHHH", 0x60, 0x60, 0x18, 0x28, 0, 0x64, 0, 0)
    )


def build_hci_filter():
    """struct hci_filter (BlueZ hci.h) bytes for setsockopt(HCI_FILTER).

    type_mask = bit(HCI_EVENT_PKT) = 0x10, event_mask all-ones (accept
    every event), opcode 0.  Packed to the kernel's 16-byte struct hci_filter
    size (type_mask u32, event_mask[2] u32, opcode u16, 2 pad bytes).
    """
    # Linux hci_filter uses 4-byte type mask + two 4-byte event masks + 2-byte opcode.
    return struct.pack("<IIIH", 1 << 4, 0xFFFFFFFF, 0xFFFFFFFF, 0) + b"\x00\x00"


def cmd(opcode, params=b""):
    return struct.pack("<HB", opcode, len(params)) + params


class ConnectSession:
    """Pure HCI connect state machine (no socket I/O).

    States:
      idle            — no attempt in flight
      pending_status  — LE Extended Create Connection sent, awaiting CMD_STATUS
      in_progress     — CMD_STATUS ok, awaiting connection-complete for the peer
      cancel_sent     — LE Create Connection Cancel sent, awaiting its ack
      connected       — confirmed link to the exact peer (post-success)

    handle_event(evt) / handle_timeout(now) return lists of action tuples
    the caller executes:
      ('send_cmd', opcode)            — caller builds/sends the command
      ('failed_attempt', status)      — attempt ended in error; caller may retry
      ('cancelled',)                  — cancel acknowledged; caller may retry
      ('cancel_timeout',)             — cancel not acknowledged in time
      ('success', handle)             — confirmed link
      ('link_down', handle, reason)   — confirmed link dropped (post-success)
    """

    def __init__(self, peer_addr_le, peer_addr_type, attempt_timeout_s=10.0):
        self.peer_addr_le = peer_addr_le
        self.peer_addr_type = peer_addr_type
        self.attempt_timeout_s = attempt_timeout_s
        self.state = "idle"
        self.attempt = 0
        self.last_status = None
        self.result = None
        self._attempt_started = 0.0
        self._cancel_started = 0.0

    def is_done(self):
        return self.result is not None

    def begin_attempt(self):
        self.attempt += 1
        self.state = "pending_status"
        self._attempt_started = time.monotonic()
        return ("send_cmd", OP_LE_EXT_CREATE_CONN)

    def handle_event(self, evt):
        kind = evt.get("evt")
        actions = []
        if self.state == "pending_status":
            if kind == "cmd_status" and evt.get("opcode") == OP_LE_EXT_CREATE_CONN:
                if evt["status"] == 0:
                    self.state = "in_progress"
                else:
                    self.last_status = evt["status"]
                    self.state = "idle"
                    actions.append(("failed_attempt", evt["status"]))
        elif self.state == "in_progress":
            if kind == "le_conn_complete":
                peer_ok = (
                    evt.get("peer_addr") == self.peer_addr_le
                    and evt.get("peer_addr_type") == self.peer_addr_type
                )
                if peer_ok:
                    if evt["status"] == 0:
                        self.state = "connected"
                        self.result = ("success", evt["handle"])
                        actions.append(("success", evt["handle"]))
                    else:
                        self.last_status = evt["status"]
                        self.state = "idle"
                        actions.append(("failed_attempt", evt["status"]))
        elif self.state == "cancel_sent":
            if kind in ("cmd_status", "cmd_complete"):
                if evt.get("opcode") == OP_LE_CREATE_CONN_CANCEL:
                    self.state = "idle"
                    actions.append(("cancelled",))
        elif self.state == "connected":
            if kind == "disconn_complete":
                if self.result is not None and self.result[1] == evt.get("handle"):
                    self.result = ("link_down", evt["handle"], evt["reason"])
                    actions.append(("link_down", evt["handle"], evt["reason"]))
        return actions

    def handle_timeout(self, now):
        if self.state in ("pending_status", "in_progress"):
            elapsed = (
                now - self._attempt_started if now >= self._attempt_started else now
            )
            if elapsed >= self.attempt_timeout_s:
                self.state = "cancel_sent"
                self._cancel_started = now
                return [("send_cmd", OP_LE_CREATE_CONN_CANCEL)]
        elif self.state == "cancel_sent":
            elapsed = now - self._cancel_started if now >= self._cancel_started else now
            if elapsed >= 2.0:
                self.state = "idle"
                return [("cancel_timeout",)]
        return []


def main():
    parser = argparse.ArgumentParser(
        description="Raw-HCI direct LE connect helper (confirmed-connect retry)"
    )
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
        help="Own random address (used only with --addr-type random; "
        "default: %s)" % DEFAULT_OWN_ADDR,
    )
    parser.add_argument(
        "--peer-addr-type",
        choices=("public", "random"),
        default="random",
        help="Peer address type (default: random). "
        "Most LE Audio receivers use a random static address.",
    )
    parser.add_argument(
        "--device",
        type=int,
        default=0,
        help="HCI device index (default: 0)",
    )
    parser.add_argument(
        "--connect-deadline",
        type=float,
        default=30.0,
        help="Total connect deadline in seconds (default 30)",
    )
    parser.add_argument(
        "--backoff",
        type=float,
        default=1.0,
        help="Backoff between failed attempts in seconds (default 1)",
    )
    parser.add_argument(
        "--attempt-timeout",
        type=float,
        default=10.0,
        help="Per-attempt timeout before cancel+retry in seconds (default 10)",
    )
    args = parser.parse_args()

    peer_str = args.peer
    hold = args.hold
    addr_type = args.addr_type
    own_str = args.own_addr
    peer_addr_type = args.peer_addr_type

    try:
        peer = parse_peer(peer_str)
    except ValueError as exc:
        print(
            "[hci_raw_connect] invalid peer address: %s: %s" % (peer_str, exc),
            flush=True,
        )
        return 1
    peer_type = OWN_ADDR_RANDOM if peer_addr_type == "random" else OWN_ADDR_PUBLIC

    s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_RAW, socket.BTPROTO_HCI)
    s.bind((args.device,))

    own_addr_type = OWN_ADDR_PUBLIC
    if addr_type == "random":
        own_addr_type = OWN_ADDR_RANDOM
        try:
            own = parse_peer(own_str)
        except ValueError as exc:
            print(
                "[hci_raw_connect] invalid own random address: %s: %s" % (own_str, exc),
                flush=True,
            )
            return 1
        s.send(bytes([HCI_COMMAND_PKT]) + cmd(OP_LE_SET_RANDOM_ADDR, own))
        time.sleep(0.1)
        print("[hci_raw_connect] own random address set to %s" % own_str, flush=True)
    else:
        print("[hci_raw_connect] using controller public BD_ADDR", flush=True)

    # Best-effort event filter: only HCI event packets, all events.
    try:
        s.setsockopt(socket.SOL_HCI, socket.HCI_FILTER, build_hci_filter())
    except OSError as exc:
        print(
            "[hci_raw_connect] HCI filter not set (%s); accepting all packets" % exc,
            file=sys.stderr,
            flush=True,
        )

    # Stop any scan, disable address resolution, direct connect (1M).
    s.send(bytes([HCI_COMMAND_PKT]) + cmd(0x2042, bytes([0, 0, 0, 0, 0, 0])))
    time.sleep(0.2)
    s.send(bytes([HCI_COMMAND_PKT]) + cmd(0x202D, bytes([0x00])))
    time.sleep(0.2)

    deadline = time.monotonic() + args.connect_deadline
    session = ConnectSession(peer, peer_type, args.attempt_timeout)
    backoff_until = 0.0
    last_fail = None  # (source, status) source in ('cmd_status','conn_complete')

    def _send(opcode):
        if opcode == OP_LE_EXT_CREATE_CONN:
            params = build_ext_create_conn(own_addr_type, peer, peer_type)
        else:
            params = b""
        s.send(bytes([HCI_COMMAND_PKT]) + cmd(opcode, params))

    def _dispatch(actions):
        for action in actions:
            act = action[0]
            if act == "send_cmd":
                _send(action[1])
            elif act == "success":
                print(
                    "%s peer=%s addr_type=%d handle=0x%04x"
                    % (READY_PREFIX_TEXT, format_peer(peer), peer_type, action[1]),
                    flush=True,
                )
            elif act == "link_down":
                print(
                    "%s handle=0x%04x reason=0x%02x"
                    % (LINK_DOWN_PREFIX, action[1], action[2]),
                    flush=True,
                )
            # 'failed_attempt'/'cancelled'/'cancel_timeout' drive the loop.

    print(
        "[hci_raw_connect] connecting to %s, holding %.0fs" % (peer_str, hold),
        flush=True,
    )

    while time.monotonic() < deadline and not session.is_done():
        now = time.monotonic()
        _dispatch(session.handle_timeout(now))
        if session.state == "idle" and not session.is_done() and now >= backoff_until:
            _dispatch([session.begin_attempt()])
        wait_s = min(0.2, max(0.0, deadline - time.monotonic()))
        try:
            r, _, _ = select.select([s], [], [], wait_s)
        except (OSError, ValueError) as exc:
            print(
                "%s reason=socket_error attempts=%d last_status=%s"
                % (
                    FAIL_PREFIX,
                    session.attempt,
                    "none"
                    if session.last_status is None
                    else "0x%02x" % session.last_status,
                ),
                flush=True,
            )
            return 1
        if not r:
            continue
        try:
            data = s.recv(4096)
        except OSError as exc:
            print(
                "%s reason=socket_error attempts=%d last_status=%s"
                % (
                    FAIL_PREFIX,
                    session.attempt,
                    "none"
                    if session.last_status is None
                    else "0x%02x" % session.last_status,
                ),
                flush=True,
            )
            return 1
        if not data:
            continue
        try:
            ptype, body = parse_hci_packet(data)
        except HciPacketError as exc:
            print(
                "[hci_raw_connect] ignoring malformed packet: %s" % exc,
                file=sys.stderr,
                flush=True,
            )
            continue
        if ptype != HCI_EVENT_PKT:
            continue
        try:
            evt = parse_event(body)
        except ValueError as exc:
            print(
                "[hci_raw_connect] ignoring malformed event: %s" % exc,
                file=sys.stderr,
                flush=True,
            )
            continue
        prev_state = session.state
        actions = session.handle_event(evt)
        for action in actions:
            if action[0] == "failed_attempt":
                last_fail = (
                    "cmd_status" if prev_state == "pending_status" else "conn_complete",
                    action[1],
                )
            _dispatch([action])

    if not (session.is_done() and session.result[0] == "success"):
        # Deadline expired without a confirmed link.  Cancel any in-flight
        # attempt (protocol permits cancelling an in-progress extended
        # create connection), then report failure.
        if session.state in ("pending_status", "in_progress"):
            _dispatch(session.handle_timeout(time.monotonic()))
            cancel_wait = time.monotonic() + 2.0
            while time.monotonic() < cancel_wait:
                try:
                    r, _, _ = select.select([s], [], [], 0.2)
                except (OSError, ValueError):
                    break
                if not r:
                    continue
                try:
                    data = s.recv(4096)
                    ptype, body = parse_hci_packet(data)
                    if ptype == HCI_EVENT_PKT:
                        evt = parse_event(body)
                        if (
                            evt.get("evt") in ("cmd_status", "cmd_complete")
                            and evt.get("opcode") == OP_LE_CREATE_CONN_CANCEL
                        ):
                            break
                except (OSError, HciPacketError, ValueError):
                    continue
        if session.state == "cancel_sent":
            reason = "cancel_timeout"
        elif last_fail is not None:
            reason = "%s_0x%02x" % (last_fail[0], last_fail[1])
        else:
            reason = "timeout"
        print(
            "%s reason=%s attempts=%d last_status=%s"
            % (
                FAIL_PREFIX,
                reason,
                session.attempt,
                "none"
                if session.last_status is None
                else "0x%02x" % session.last_status,
            ),
            flush=True,
        )
        return 1

    # Hold phase: keep the raw socket open for the lifetime of the ACL link.
    handle = session.result[1]
    hold_until = time.monotonic() + hold
    while time.monotonic() < hold_until:
        try:
            r, _, _ = select.select([s], [], [], 0.2)
        except (OSError, ValueError):
            return 0
        if not r:
            continue
        try:
            data = s.recv(4096)
        except OSError:
            return 0
        if not data:
            continue
        try:
            ptype, body = parse_hci_packet(data)
        except HciPacketError:
            continue
        if ptype != HCI_EVENT_PKT:
            continue
        try:
            evt = parse_event(body)
        except ValueError:
            continue
        for action in session.handle_event(evt):
            if action[0] == "link_down":
                _dispatch([action])
                return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
