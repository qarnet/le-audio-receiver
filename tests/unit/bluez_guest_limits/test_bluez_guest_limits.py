"""Real file, process and event boundaries for PB-053 guest limits."""

import json
import os
from pathlib import Path
import resource
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from bluez_guest_limits import EventLedger, read_bounded
from bluez_guest_init import state_hashes
from bluez_guest_public import close_owned_lease, record_ledger_failure
from bluez_host_process import run_owned


class GuestLimitTests(unittest.TestCase):
    def test_regular_boundaries_and_special_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "regular"
            for contents in (b"", b"abcd"):
                path.write_bytes(contents)
                self.assertEqual(read_bounded(path, 4), contents)
            path.write_bytes(b"abcde")
            with self.assertRaises(ValueError):
                read_bounded(path, 4)
            (root / "link").symlink_to(path)
            os.mkfifo(root / "pipe")
            for name in ("link", "pipe"):
                start = time.monotonic()
                with self.assertRaises((OSError, ValueError)):
                    read_bounded(root / name, 4)
                self.assertLess(time.monotonic() - start, 1)
            for limit in (0, -1, True, 1.0):
                with self.assertRaises(ValueError):
                    read_bounded(path, limit)
        data = read_bounded("/proc/self/stat", 4096)
        self.assertTrue(data.startswith(str(os.getpid()).encode() + b" "))

    def test_state_hash_boundaries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "empty").write_bytes(b"")
            (root / "full").write_bytes(b"a" * (2 * 1024 * 1024))
            self.assertEqual(len(state_hashes(root)), 2)
            (root / "oversized").write_bytes(b"x" * (2 * 1024 * 1024 + 1))
            with self.assertRaises(RuntimeError):
                state_hashes(root)
            (root / "oversized").unlink()
            os.mkfifo(root / "fifo")
            with self.assertRaises(RuntimeError):
                state_hashes(root)
            (root / "fifo").unlink()
            (root / "link").symlink_to(root / "empty")
            with self.assertRaises(RuntimeError):
                state_hashes(root)
            (root / "link").unlink()
            for index in range(4):
                (root / f"extra{index}").write_bytes(b"b" * (2 * 1024 * 1024))
            with self.assertRaises(RuntimeError):
                state_hashes(root)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for index in range(1025):
                (root / f"state{index:04d}").touch()
            with self.assertRaises(RuntimeError):
                state_hashes(root)

    def test_event_copy_quota_and_json_serialization(self):
        original = {"nested": [1]}
        ledger = EventLedger(max_events=2, max_bytes=50)
        ledger.append(original)
        original["nested"].extend(range(1000))
        self.assertEqual(json.loads(json.dumps(ledger)), [{"nested": [1]}])
        self.assertLessEqual(len(json.dumps(ledger).encode()), 50)
        ledger.append({"next": True})
        with self.assertRaises(ValueError):
            ledger.append({"third": 3})
        self.assertEqual(len(ledger), 2)
        tight = EventLedger(max_bytes=len(json.dumps({"v": 1}).encode()))
        tight.append({"v": 1})
        with self.assertRaises(ValueError):
            EventLedger(max_bytes=1).append({"v": 1})
        for value in (float("nan"), object()):
            with self.assertRaises((ValueError, TypeError)):
                ledger.append(value)
        for limit in (0, True, 1.5):
            with self.assertRaises(ValueError):
                EventLedger(max_events=limit)

    def test_concurrent_append_bounded(self):
        ledger = EventLedger(max_events=20, max_bytes=200)
        errors = []

        def writer():
            for _ in range(30):
                try:
                    ledger.append({"n": 1})
                except ValueError:
                    errors.append(True)

        threads = [threading.Thread(target=writer) for _ in range(4)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertEqual(len(ledger), 20)
        self.assertTrue(errors)
        self.assertLessEqual(
            sum(len(json.dumps(item).encode()) for item in ledger), 200
        )

    def test_cleanup_quota_failure_keeps_event_and_closes_both_peers(self):
        ledger = EventLedger(max_events=1)
        ledger.append({"retained": "unchanged"})
        result = {"ok": True, "events": ledger, "cleanup_errors": []}
        closed = set()
        pairs = [socket.socketpair() for _ in range(2)]
        try:
            for index, (owned, peer) in enumerate(pairs):
                close_owned_lease(
                    {"socket": owned, "fd": None, "path": f"/lease/{index}"},
                    closed,
                    result,
                    ledger,
                )
                self.assertEqual(peer.recv(1), b"")
            record_ledger_failure(result)
            self.assertEqual(closed, {"/lease/0", "/lease/1"})
            self.assertEqual(list(ledger), [{"retained": "unchanged"}])
            self.assertFalse(result["ok"])
            self.assertEqual(
                result["cleanup_errors"],
                ["Event ledger: ValueError: Guest event quota exceeded"],
            )
            with self.assertRaisesRegex(ValueError, "Guest event quota exceeded"):
                ledger.append({"no": "eviction"})
        finally:
            for owned, peer in pairs:
                owned.close()
                peer.close()

    def test_ordinary_append_failure_sticks_through_successful_lease_cleanup(self):
        ledger = EventLedger(max_bytes=120)
        ledger.append({"retained": "before"})
        with self.assertRaisesRegex(ValueError, "Guest event quota exceeded"):
            ledger.append({"rejected": "x" * 120})
        result = {"ok": True, "events": ledger, "cleanup_errors": []}
        closed = set()
        pairs = [socket.socketpair() for _ in range(2)]
        try:
            for index, (owned, peer) in enumerate(pairs):
                close_owned_lease(
                    {"socket": owned, "fd": None, "path": f"/lease/{index}"},
                    closed,
                    result,
                    ledger,
                )
                peer.settimeout(0.5)
                self.assertEqual(peer.recv(1), b"")
            self.assertEqual(closed, {"/lease/0", "/lease/1"})
            self.assertEqual(
                list(ledger),
                [
                    {"retained": "before"},
                    {"closed": "/lease/0", "ok": True},
                    {"closed": "/lease/1", "ok": True},
                ],
            )
            record_ledger_failure(result)
            self.assertFalse(result["ok"])
            self.assertEqual(
                result["cleanup_errors"],
                ["Event ledger: ValueError: Guest event quota exceeded"],
            )
        finally:
            for owned, peer in pairs:
                owned.close()
                peer.close()

    def test_ordinary_json_errors_keep_first_failure(self):
        for invalid, exception in (
            (float("nan"), ValueError),
            (object(), TypeError),
        ):
            with self.subTest(exception=exception):
                ledger = EventLedger()
                with self.assertRaises(exception) as raised:
                    ledger.append(invalid)
                error = f"{exception.__name__}: {raised.exception}"
                self.assertEqual(ledger.error, error)
                ledger.append({"cleanup": True})
                with self.assertRaises(ValueError):
                    ledger.append(float("nan"))
                self.assertEqual(ledger.error, error)
                self.assertEqual(list(ledger), [{"cleanup": True}])

    def test_only_ordinary_child_has_file_limit(self):
        before = resource.getrlimit(resource.RLIMIT_FSIZE)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "child.bin"
            child = subprocess.run(
                [
                    sys.executable,
                    "-c",
                    "import resource,signal,sys; "
                    "signal.signal(signal.SIGXFSZ, signal.SIG_IGN); "
                    "resource.setrlimit(resource.RLIMIT_FSIZE,(1024,1024)); "
                    "fd=__import__('os').open(sys.argv[1],__import__('os').O_WRONLY|__import__('os').O_CREAT); "
                    "sys.exit(0 if __import__('os').write(fd,b'x'*2048)==2048 else 2)",
                    str(target),
                ],
                capture_output=True,
                timeout=5,
            )
            self.assertNotEqual(child.returncode, 0)
            self.assertLessEqual(target.stat().st_size, 1024)
        self.assertEqual(resource.getrlimit(resource.RLIMIT_FSIZE), before)

    def test_owned_command_combined_log_and_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "command.log"
            record = run_owned(
                [
                    sys.executable,
                    "-c",
                    "import sys; print('out'); print('err',file=sys.stderr)",
                ],
                log,
                15,
                max_log_bytes=2 * 1024 * 1024,
            )
            self.assertTrue(record["ok"], record)
            self.assertIn(b"out", read_bounded(log, 2 * 1024 * 1024))
            self.assertIn(b"err", read_bounded(log, 2 * 1024 * 1024))
            overflow = run_owned(
                [sys.executable, "-c", "import os; os.write(1,b'x'*3000)"],
                Path(directory) / "overflow.log",
                15,
                max_log_bytes=1024,
            )
            self.assertFalse(overflow["ok"])
            self.assertTrue(overflow["log_limit_exceeded"])
            self.assertEqual((Path(directory) / "overflow.log").stat().st_size, 1024)


if __name__ == "__main__":
    unittest.main()
