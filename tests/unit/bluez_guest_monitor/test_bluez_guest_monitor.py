"""Component-level AF_UNIX datagram tests; never open host HCI."""

import hashlib
import io
import json
from pathlib import Path
import socket
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from bluez_guest_monitor import collect
from bluez_host_results import validate_capture


def packet(opcode, payload=b"data"):
    return struct.pack("<HHH", opcode, 0, len(payload)) + payload


class RawMonitorTests(unittest.TestCase):
    def capture(self, data, stop=15):
        sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        with sender, receiver:
            for item in data:
                sender.send(item)
            output = io.StringIO()
            result = collect(
                receiver,
                output,
                (lambda: stop) if not data else self.stop_after(len(data)),
            )
            return result, output.getvalue()

    @staticmethod
    def stop_after(count):
        # Packet queue drained before select, no private collector state needed.
        calls = 0

        def check():
            nonlocal calls
            calls += 1
            return 15 if calls > count else None

        return check

    def test_raw_packets_and_recomputed_digest(self):
        data = [packet(2), packet(18, b"\x00\xff")]
        result, text = self.capture(data)
        self.assertTrue(result["ok"])
        checked = validate_capture(text)
        self.assertEqual(checked["opcode_counts"], {2: 1, 18: 1})
        self.assertIn(data[1].hex(), text)
        sha = hashlib.sha256()
        for item in data:
            sha.update(struct.pack("<I", len(item)) + item)
        self.assertEqual(checked["sha256"], sha.hexdigest())
        rows = text.splitlines()
        altered = json.loads(rows[2])
        altered["raw_hex"] = packet(18, b"\x01\xff").hex()
        rows[2] = json.dumps(altered)
        with self.assertRaises(ValueError):
            validate_capture("\n".join(rows) + "\n")
        for candidate in (
            "\n".join(rows[:-1]) + "\n",
            text + rows[-1] + "\n",
            text.replace('"packet":1', '"packet":0'),
        ):
            with self.assertRaises(ValueError):
                validate_capture(candidate)

    def test_invalid_header_and_stop(self):
        for raw in (b"abc", struct.pack("<HHH", 18, 0, 2) + b"a"):
            result, text = self.capture([raw])
            self.assertFalse(result["ok"])
            self.assertEqual(result["packets"], 0)
            with self.assertRaises(ValueError):
                validate_capture(text)
        result, text = self.capture([], stop=2)
        self.assertTrue(result["ok"])
        self.assertEqual(validate_capture(text)["stop_signal"], 2)

    def test_quota_preserves_terminal_and_retained_digest(self):
        sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        with sender, receiver:
            sender.send(packet(18, b"x" * 4000))
            output = io.StringIO()
            result = collect(receiver, output, lambda: None, cap=4400)
        self.assertFalse(result["ok"])
        self.assertEqual(result["packets"], 0)
        self.assertEqual(result["sha256"], hashlib.sha256(b"").hexdigest())
        self.assertIn("PB053_MONITOR_RESULT ", output.getvalue())
        with self.assertRaises(ValueError):
            validate_capture(output.getvalue())

    def test_invalid_cap_refused_before_ready(self):
        sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        with sender, receiver:
            for cap in (True, False, None, 4096, 0, -1, 4096.5, "4194304"):
                with self.subTest(cap=cap):
                    output = io.StringIO()
                    with self.assertRaises(ValueError):
                        collect(receiver, output, lambda: 15, cap=cap)
                    self.assertEqual(output.getvalue(), "")

    def test_capture_scalar_and_observation_controls(self):
        _, text = self.capture([packet(2), packet(3)])
        rows = text.splitlines()
        ready = json.loads(rows[0].removeprefix("PB053_MONITOR_READY "))
        ready["schema_version"] = True
        changed = rows.copy()
        changed[0] = "PB053_MONITOR_READY " + json.dumps(ready)
        with self.assertRaises(ValueError):
            validate_capture("\n".join(changed) + "\n")

        changed = rows.copy()
        first = json.loads(changed[1])
        second = json.loads(changed[2])
        second["monotonic_ns"] = first["monotonic_ns"] - 1
        changed[2] = json.dumps(second)
        with self.assertRaises(ValueError):
            validate_capture("\n".join(changed) + "\n")

        changed = rows.copy()
        terminal = json.loads(changed[-1].removeprefix("PB053_MONITOR_RESULT "))
        terminal["reported_drops"] = 0
        changed[-1] = "PB053_MONITOR_RESULT " + json.dumps(terminal)
        with self.assertRaises(ValueError):
            validate_capture("\n".join(changed) + "\n")

    def test_kernel_timestamp_on_unix_datagram(self):
        sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        with sender, receiver:
            receiver.setsockopt(socket.SOL_SOCKET, 35, 1)
            sender.send(packet(19))
            output = io.StringIO()
            self.assertTrue(collect(receiver, output, self.stop_after(1))["ok"])
            row = json.loads(output.getvalue().splitlines()[1])
            self.assertIsInstance(row["kernel_timestamp_ns"], int)
            self.assertEqual(
                validate_capture(output.getvalue())["opcode_counts"], {19: 1}
            )

    def test_truncation_and_eof_fail_closed(self):
        class InjectedSocket:
            def __init__(self, source, flags=0):
                self.source = source
                self.flags = flags

            def fileno(self):
                return self.source.fileno()

            def recvmsg(self, size, ancillary):
                data, control, flags, address = self.source.recvmsg(size, ancillary)
                return data, control, flags | self.flags, address

        for flags, data in (
            (socket.MSG_TRUNC, packet(18)),
            (socket.MSG_CTRUNC, packet(18)),
            (0, b""),
        ):
            sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
            with sender, receiver:
                sender.send(data)
                output = io.StringIO()
                result = collect(InjectedSocket(receiver, flags), output, lambda: None)
                self.assertFalse(result["ok"])
                self.assertEqual(result["packets"], 0)
                with self.assertRaises(ValueError):
                    validate_capture(output.getvalue())


if __name__ == "__main__":
    unittest.main()
