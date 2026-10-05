"""Real Linux reparenting and cancellation regression; no simulated process boundary."""

import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import time
import unittest


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from bluez_host_descendants import DescendantScope, _flag


def nested():
    grandchild = subprocess.Popen(
        [
            sys.executable,
            "-c",
            "import signal,time; signal.signal(signal.SIGTERM, signal.SIG_IGN); print('ready', flush=True); time.sleep(30)",
        ],
        start_new_session=True,
        stdout=subprocess.PIPE,
        text=True,
    )
    assert grandchild.stdout.readline().strip() == "ready"
    print(grandchild.pid, flush=True)
    grandchild.stdout.close()
    if "hold" in sys.argv:
        time.sleep(30)


def harness(mode):
    scope = DescendantScope()
    try:
        with scope:
            if mode == "empty":
                subprocess.run([sys.executable, "-c", "pass"], check=True)
            else:
                middle = subprocess.Popen(
                    [sys.executable, __file__, "nested"]
                    + (["hold"] if mode == "cancel-parent" else []),
                    stdout=subprocess.PIPE,
                    text=True,
                )
                print("CHILD " + middle.stdout.readline().strip(), flush=True)
                if mode != "cancel-parent":
                    middle.wait(timeout=3)
                middle.stdout.close()
                if mode in ("cancel", "cancel-parent"):
                    while True:
                        time.sleep(0.05)
    except BaseException as exc:
        print("FAIL " + type(exc).__name__, flush=True)
    print("RECORD " + json.dumps(scope.record), flush=True)
    print("FLAG " + str(_flag()), flush=True)
    return 0 if scope.record["ok"] else 1


class DescendantTests(unittest.TestCase):
    def run_harness(self, mode, cancel=False):
        prior = _flag()
        sibling = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(30)"]
        )
        proc = subprocess.Popen(
            [sys.executable, __file__, "harness", mode],
            stdout=subprocess.PIPE,
            text=True,
        )
        try:
            child = None
            if mode != "empty":
                ready, _, _ = select.select([proc.stdout], [], [], 5)
                self.assertTrue(ready)
                child = int(proc.stdout.readline().split()[1])
            if cancel:
                os.kill(proc.pid, signal.SIGTERM)
                os.kill(proc.pid, signal.SIGINT)
            output, _ = proc.communicate(timeout=12)
            self.assertEqual(proc.returncode, 0 if mode == "empty" else 1, output)
            record = json.loads(
                next(
                    line[7:]
                    for line in output.splitlines()
                    if line.startswith("RECORD ")
                )
            )
            self.assertEqual(record["restored_flag"], prior)
            self.assertEqual(record["errors"], [])
            self.assertIsNone(sibling.poll())
            if child is not None:
                self.assertIn(child, [item["pid"] for item in record["adopted"]])
                self.assertIn(
                    signal.SIGKILL,
                    [item["signal"] for item in record["actions"] if "signal" in item],
                )
                self.assertFalse(Path(f"/proc/{child}").exists())
            return record
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.wait(timeout=3)
            sibling.terminate()
            sibling.wait(timeout=3)

    def test_normal_child_no_descendants(self):
        self.assertTrue(self.run_harness("empty")["ok"])

    def test_normal_detached_leak_fails_after_cleanup(self):
        record = self.run_harness("leak")
        self.assertTrue(record["unexpected_live_descendants"])
        self.assertFalse(record["ok"])

    def test_cancel_detached_child_and_preserve_failure(self):
        record = self.run_harness("cancel", cancel=True)
        self.assertIn(record["cancelled_signal"], (signal.SIGINT, signal.SIGTERM))
        self.assertFalse(record["ok"])

    def test_cancel_live_intermediary_adopts_grandchild(self):
        record = self.run_harness("cancel-parent", cancel=True)
        self.assertGreaterEqual(len(record["adopted"]), 2)
        self.assertFalse(record["ok"])


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "nested":
        nested()
    elif len(sys.argv) > 2 and sys.argv[1] == "harness":
        sys.exit(harness(sys.argv[2]))
    else:
        unittest.main()
