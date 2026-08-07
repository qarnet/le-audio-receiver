#!/usr/bin/env python3
"""Unit tests for the hci_raw_connect.py confirmed-connect retry helper
and the bap_central.py ready-line gate (T8 tooling fix).

Proves, on stdlib python3 with NO live HCI:
  - raw HCI packet framing validation (parse_hci_packet);
  - HCI event parsing: command status/complete, LE connection complete
    (legacy 0x01 and enhanced 0x0A), disconnect complete;
  - the LE Extended Create Connection parameter builder matches the
    known-good wire layout byte-for-byte;
  - the ConnectSession state machine: exact-peer success, unrelated-peer
    ignore, command failure, completion failure, per-attempt
    timeout -> cancel -> retry -> success, cancel-ack timeout, and
    post-success link-down;
  - bap_central_security.wait_for_helper_ready gating on the machine-readable
    ready line (real pipes, no threads).
"""

import os
import struct
import sys
import time
import unittest

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "scripts"
    ),
)

from hci_raw_connect import (  # noqa: E402
    ConnectSession,
    FATAL_CMD_STATUS_CODES,
    HciPacketError,
    LE_SUBEVT_CONN_COMPLETE,
    LE_SUBEVT_ENH_CONN_COMPLETE,
    OP_LE_CREATE_CONN_CANCEL,
    OP_LE_EXT_CREATE_CONN,
    READY_PREFIX,
    build_ext_create_conn,
    build_hci_filter,
    cmd,
    force_cancel,
    format_peer,
    parse_event,
    parse_hci_packet,
    parse_peer,
)

from bap_central_security import wait_for_helper_ready  # noqa: E402

PEER = parse_peer("DB:A6:0C:05:A2:AA")  # little-endian wire bytes
PEER_TYPE = 0x01


def evt_body(code, payload):
    """Assemble an event body (code + plen + payload) as the socket hands it."""
    return bytes([code, len(payload)]) + payload


def cmd_status_evt(opcode, status):
    return evt_body(0x0F, bytes([status, 0x01]) + struct.pack("<H", opcode))


def le_conn_evt(subevent, status, handle, peer=PEER, peer_type=PEER_TYPE):
    if subevent == LE_SUBEVT_CONN_COMPLETE:
        payload = (
            bytes([subevent, status])
            + struct.pack("<H", handle)
            + bytes([0x00, peer_type])
            + peer
            + struct.pack("<HHHHB", 0x18, 0x00, 0x64, 0x00, 0x00)
        )
    else:
        payload = (
            bytes([subevent, status])
            + struct.pack("<H", handle)
            + bytes([0x00, peer_type])
            + peer
            + bytes(12)  # local_rpa + peer_rpa
            + struct.pack("<HHHHB", 0x18, 0x00, 0x64, 0x00, 0x00)
        )
    return evt_body(0x3E, payload)


class TestParseHciPacket(unittest.TestCase):
    def test_valid_event_roundtrip(self):
        buf = bytes([0x04, 0x0F, 0x04, 0x00, 0x01]) + struct.pack("<H", 0x2043)
        ptype, body = parse_hci_packet(buf)
        self.assertEqual(ptype, 0x04)
        self.assertEqual(body[0], 0x0F)
        self.assertEqual(body[1], 0x04)

    def test_valid_command_roundtrip(self):
        c = bytes([0x01]) + cmd(OP_LE_EXT_CREATE_CONN, b"\x00\x01")
        ptype, body = parse_hci_packet(c)
        self.assertEqual(ptype, 0x01)
        self.assertEqual(struct.unpack_from("<H", body, 0)[0], OP_LE_EXT_CREATE_CONN)

    def test_valid_acl_roundtrip(self):
        buf = bytes([0x02, 0x00, 0x00, 0x02, 0x00, 0xAA, 0xBB])
        ptype, body = parse_hci_packet(buf)
        self.assertEqual(ptype, 0x02)
        self.assertEqual(len(body), 6)

    def test_unknown_packet_type_raises(self):
        with self.assertRaises(HciPacketError):
            parse_hci_packet(bytes([0x63, 0x00, 0x00]))

    def test_truncated_header_raises(self):
        with self.assertRaises(HciPacketError):
            parse_hci_packet(bytes([0x04, 0x0F]))

    def test_event_length_mismatch_raises(self):
        # declares plen=5 but only 2 payload bytes follow
        with self.assertRaises(HciPacketError):
            parse_hci_packet(bytes([0x04, 0x0F, 0x05, 0x00, 0x00]))

    def test_command_length_mismatch_raises(self):
        # declares plen=3 but only 1 param byte follows
        buf = bytes([0x01]) + struct.pack("<HB", OP_LE_EXT_CREATE_CONN, 3) + b"\x00"
        with self.assertRaises(HciPacketError):
            parse_hci_packet(buf)


class TestParseEvent(unittest.TestCase):
    def test_cmd_status_ok(self):
        evt = parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0))
        self.assertEqual(evt["evt"], "cmd_status")
        self.assertEqual(evt["opcode"], OP_LE_EXT_CREATE_CONN)
        self.assertEqual(evt["status"], 0)

    def test_cmd_status_error(self):
        evt = parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0x0C))
        self.assertEqual(evt["status"], 0x0C)

    def test_cmd_complete(self):
        body = evt_body(
            0x0E, bytes([0x01]) + struct.pack("<H", OP_LE_EXT_CREATE_CONN) + b"\x00"
        )
        evt = parse_event(body)
        self.assertEqual(evt["evt"], "cmd_complete")
        self.assertEqual(evt["opcode"], OP_LE_EXT_CREATE_CONN)

    def test_legacy_le_conn_complete(self):
        evt = parse_event(le_conn_evt(LE_SUBEVT_CONN_COMPLETE, 0, 0x0042))
        self.assertEqual(evt["evt"], "le_conn_complete")
        self.assertEqual(evt["status"], 0)
        self.assertEqual(evt["handle"], 0x0042)
        self.assertEqual(evt["peer_addr"], PEER)
        self.assertEqual(evt["peer_addr_type"], PEER_TYPE)
        self.assertEqual(evt["interval"], 0x18)

    def test_enhanced_le_conn_complete(self):
        evt = parse_event(le_conn_evt(LE_SUBEVT_ENH_CONN_COMPLETE, 0, 0x0043))
        self.assertEqual(evt["evt"], "le_conn_complete")
        self.assertEqual(evt["subevent"], LE_SUBEVT_ENH_CONN_COMPLETE)
        self.assertEqual(evt["handle"], 0x0043)
        self.assertEqual(evt["peer_addr"], PEER)
        self.assertEqual(evt["interval"], 0x18)

    def test_disconn_complete(self):
        body = evt_body(0x05, bytes([0x00]) + struct.pack("<H", 0x0042) + bytes([0x13]))
        evt = parse_event(body)
        self.assertEqual(evt["evt"], "disconn_complete")
        self.assertEqual(evt["handle"], 0x0042)
        self.assertEqual(evt["reason"], 0x13)

    def test_unknown_event(self):
        evt = parse_event(evt_body(0x33, b"\x00"))
        self.assertEqual(evt["evt"], "unknown")
        self.assertEqual(evt["code"], 0x33)

    def test_truncated_cmd_status_raises(self):
        with self.assertRaises(ValueError):
            parse_event(evt_body(0x0F, b"\x00\x00"))

    def test_truncated_le_meta_raises(self):
        with self.assertRaises(ValueError):
            parse_event(evt_body(0x3E, b"\x0a\x00\x00"))

    def test_length_mismatch_raises(self):
        with self.assertRaises(ValueError):
            parse_event(bytes([0x0F, 0x08, 0x00, 0x01]) + struct.pack("<H", 0x2043))


class TestBuilders(unittest.TestCase):
    def test_build_ext_create_conn_regression(self):
        expected = (
            bytes([0x00, 0x00, 0x01])
            + PEER
            + bytes([0x01])
            + struct.pack("<HHHHHHHH", 0x60, 0x60, 0x18, 0x28, 0, 0x64, 0, 0)
        )
        self.assertEqual(build_ext_create_conn(0x00, PEER, PEER_TYPE), expected)

    def test_build_hci_filter(self):
        f = build_hci_filter()
        self.assertEqual(len(f), 16)
        self.assertEqual(struct.unpack_from("<I", f, 0)[0], 1 << 4)
        self.assertEqual(struct.unpack_from("<I", f, 4)[0], 0xFFFFFFFF)
        self.assertEqual(struct.unpack_from("<I", f, 8)[0], 0xFFFFFFFF)
        self.assertEqual(struct.unpack_from("<H", f, 12)[0], 0)

    def test_parse_peer_valid(self):
        self.assertEqual(
            parse_peer("DB:A6:0C:05:A2:AA"), bytes([0xAA, 0xA2, 0x05, 0x0C, 0xA6, 0xDB])
        )

    def test_parse_peer_malformed(self):
        for bad in (
            "DB:A6:0C:05:A2",
            "DB:A6:0C:05:A2:ZZ",
            "DB:A6:0C:05:A2:AA:01",
            "xyz",
        ):
            with self.assertRaises(ValueError):
                parse_peer(bad)

    def test_format_peer_roundtrip(self):
        self.assertEqual(format_peer(PEER), "DB:A6:0C:05:A2:AA")


class TestConnectSession(unittest.TestCase):
    def test_exact_peer_success_enhanced(self):
        s = ConnectSession(PEER, PEER_TYPE)
        s.begin_attempt()
        s.handle_event(parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0)))
        actions = s.handle_event(
            parse_event(le_conn_evt(LE_SUBEVT_ENH_CONN_COMPLETE, 0, 0x0042))
        )
        self.assertEqual(actions, [("success", 0x0042)])
        self.assertEqual(s.result, ("success", 0x0042))

    def test_exact_peer_success_legacy(self):
        s = ConnectSession(PEER, PEER_TYPE)
        s.begin_attempt()
        s.handle_event(parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0)))
        actions = s.handle_event(
            parse_event(le_conn_evt(LE_SUBEVT_CONN_COMPLETE, 0, 0x0042))
        )
        self.assertEqual(actions, [("success", 0x0042)])

    def test_unrelated_peer_ignored_then_success(self):
        other = parse_peer("11:22:33:44:55:66")
        s = ConnectSession(PEER, PEER_TYPE)
        s.begin_attempt()
        s.handle_event(parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0)))
        actions = s.handle_event(
            parse_event(le_conn_evt(LE_SUBEVT_ENH_CONN_COMPLETE, 0, 0x0044, peer=other))
        )
        self.assertEqual(actions, [])
        self.assertIsNone(s.result)
        actions = s.handle_event(
            parse_event(le_conn_evt(LE_SUBEVT_ENH_CONN_COMPLETE, 0, 0x0045))
        )
        self.assertEqual(actions, [("success", 0x0045)])

    def test_command_failure(self):
        s = ConnectSession(PEER, PEER_TYPE)
        s.begin_attempt()
        actions = s.handle_event(
            parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0x0C))
        )
        self.assertEqual(actions, [("failed_attempt", 0x0C)])
        self.assertEqual(s.state, "idle")
        self.assertEqual(s.last_status, 0x0C)

    def test_completion_failure(self):
        s = ConnectSession(PEER, PEER_TYPE)
        s.begin_attempt()
        s.handle_event(parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0)))
        actions = s.handle_event(
            parse_event(le_conn_evt(LE_SUBEVT_ENH_CONN_COMPLETE, 0x0D, 0x0042))
        )
        self.assertEqual(actions, [("failed_attempt", 0x0D)])
        self.assertEqual(s.state, "idle")

    def test_timeout_cancel_retry_success(self):
        s = ConnectSession(PEER, PEER_TYPE, attempt_timeout_s=10.0)
        s.begin_attempt()
        t0 = 1000.0
        actions = s.handle_timeout(t0 + 10.0)
        self.assertEqual(actions, [("send_cmd", OP_LE_CREATE_CONN_CANCEL)])
        self.assertEqual(s.state, "cancel_sent")
        actions = s.handle_event(
            parse_event(cmd_status_evt(OP_LE_CREATE_CONN_CANCEL, 0))
        )
        self.assertEqual(actions, [("cancelled",)])
        self.assertEqual(s.state, "idle")
        s.begin_attempt()
        s.handle_event(parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0)))
        actions = s.handle_event(
            parse_event(le_conn_evt(LE_SUBEVT_ENH_CONN_COMPLETE, 0, 0x0042))
        )
        self.assertEqual(actions, [("success", 0x0042)])

    def test_cancel_ack_timeout(self):
        s = ConnectSession(PEER, PEER_TYPE, attempt_timeout_s=10.0)
        s.begin_attempt()
        t0 = 1000.0
        s.handle_timeout(t0 + 10.0)  # -> cancel_sent
        actions = s.handle_timeout(t0 + 12.1)
        self.assertEqual(actions, [("cancel_timeout",)])
        self.assertEqual(s.state, "idle")

    def test_link_down_after_success(self):
        s = ConnectSession(PEER, PEER_TYPE)
        s.begin_attempt()
        s.handle_event(parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0)))
        s.handle_event(parse_event(le_conn_evt(LE_SUBEVT_ENH_CONN_COMPLETE, 0, 0x0042)))
        body = evt_body(0x05, bytes([0x00]) + struct.pack("<H", 0x0042) + bytes([0x13]))
        actions = s.handle_event(parse_event(body))
        self.assertEqual(actions, [("link_down", 0x0042, 0x13)])


class TestFatalCommandStatus(unittest.TestCase):
    """Command-status errors that can never be fixed by retry stop the
    session (fatal_status); transient errors stay retryable."""

    def test_fatal_status_ends_session(self):
        s = ConnectSession(PEER, PEER_TYPE)
        s.begin_attempt()
        actions = s.handle_event(
            parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0x01))
        )
        self.assertEqual(actions, [("fatal_status", 0x01)])
        self.assertTrue(s.is_done(), "fatal status must stop retries")
        self.assertEqual(s.result, ("fatal_status", 0x01))
        self.assertEqual(s.state, "idle")
        self.assertEqual(s.last_status, 0x01)

    def test_every_fatal_code_ends_session(self):
        for code in sorted(FATAL_CMD_STATUS_CODES):
            s = ConnectSession(PEER, PEER_TYPE)
            s.begin_attempt()
            actions = s.handle_event(
                parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, code))
            )
            self.assertEqual(actions, [("fatal_status", code)], hex(code))
            self.assertTrue(s.is_done(), hex(code))

    def test_retryable_statuses_keep_session_alive(self):
        for code in (0x07, 0x0B, 0x0C, 0x0D, 0x0F, 0x10):
            s = ConnectSession(PEER, PEER_TYPE)
            s.begin_attempt()
            actions = s.handle_event(
                parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, code))
            )
            self.assertEqual(actions, [("failed_attempt", code)], hex(code))
            self.assertFalse(s.is_done(), hex(code))
            self.assertEqual(s.state, "idle")

    def test_retry_after_transient_status_succeeds(self):
        s = ConnectSession(PEER, PEER_TYPE)
        s.begin_attempt()
        s.handle_event(parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0x0C)))
        s.begin_attempt()
        s.handle_event(parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0)))
        actions = s.handle_event(
            parse_event(le_conn_evt(LE_SUBEVT_ENH_CONN_COMPLETE, 0, 0x0042))
        )
        self.assertEqual(actions, [("success", 0x0042)])


class TestForceCancel(unittest.TestCase):
    """Global-deadline cancel is independent of the per-attempt timeout:
    any in-flight attempt is cancelled before the socket closes."""

    def test_force_cancel_in_progress_attempt(self):
        s = ConnectSession(PEER, PEER_TYPE, attempt_timeout_s=10.0)
        s.begin_attempt()
        s.handle_event(parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0)))
        # Global deadline at +2s — far below the 10 s per-attempt timeout.
        actions = force_cancel(s, 1002.0)
        self.assertEqual(actions, [("send_cmd", OP_LE_CREATE_CONN_CANCEL)])
        self.assertEqual(s.state, "cancel_sent")
        # Cancel ack completes the cancellation; session may retry.
        actions = s.handle_event(
            parse_event(cmd_status_evt(OP_LE_CREATE_CONN_CANCEL, 0))
        )
        self.assertEqual(actions, [("cancelled",)])
        self.assertEqual(s.state, "idle")
        self.assertFalse(s.is_done())

    def test_force_cancel_pending_status(self):
        s = ConnectSession(PEER, PEER_TYPE)
        s.begin_attempt()  # pending_status (no cmd_status yet)
        actions = force_cancel(s, 5.0)
        self.assertEqual(actions, [("send_cmd", OP_LE_CREATE_CONN_CANCEL)])
        self.assertEqual(s.state, "cancel_sent")

    def test_force_cancel_idle_noop(self):
        s = ConnectSession(PEER, PEER_TYPE)
        self.assertEqual(force_cancel(s, 5.0), [])
        self.assertEqual(s.state, "idle")

    def test_force_cancel_connected_noop(self):
        s = ConnectSession(PEER, PEER_TYPE)
        s.begin_attempt()
        s.handle_event(parse_event(cmd_status_evt(OP_LE_EXT_CREATE_CONN, 0)))
        s.handle_event(parse_event(le_conn_evt(LE_SUBEVT_ENH_CONN_COMPLETE, 0, 0x0042)))
        self.assertEqual(force_cancel(s, 5.0), [])
        self.assertEqual(s.state, "connected")


class TestWaitForHelperReady(unittest.TestCase):
    def _call(self, payload, alive, deadline_s=1.0):
        r, w = os.pipe()
        try:
            os.write(w, payload)
            out = os.fdopen(r, "rb")
            try:
                return wait_for_helper_ready(out, alive, time.monotonic() + deadline_s)
            finally:
                out.close()
        finally:
            os.close(w)

    def test_ready_line_detected(self):
        payload = (
            b"noise line\n"
            + b"more noise\n"
            + READY_PREFIX
            + b" peer=x handle=0x0001\n"
        )
        ok, detail, lines = self._call(payload, lambda: True)
        self.assertTrue(ok)
        self.assertTrue(detail.startswith(READY_PREFIX))
        self.assertEqual(len(lines), 3)

    def test_helper_early_exit(self):
        ok, detail, lines = self._call(b"", lambda: False)
        self.assertFalse(ok)
        self.assertEqual(detail, "helper exited before ready")

    def test_deadline_expiry(self):
        ok, detail, lines = self._call(b"", lambda: True, deadline_s=0.1)
        self.assertFalse(ok)
        self.assertEqual(detail, "helper ready-line timeout")

    def test_unrelated_lines_then_ready(self):
        payload = b"line1\nline2\n" + READY_PREFIX + b"\n"
        ok, detail, lines = self._call(payload, lambda: True)
        self.assertTrue(ok)
        self.assertEqual(lines[0], b"line1")
        self.assertEqual(lines[1], b"line2")


if __name__ == "__main__":
    unittest.main()
