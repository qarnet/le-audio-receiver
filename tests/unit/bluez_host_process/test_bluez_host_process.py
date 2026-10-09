"""Real Linux process-boundary checks for PB-053 host ownership."""

import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from bluez_host_process import run_owned  # noqa: E402


def live(pid):
    try:
        stat = Path(f"/proc/{pid}/stat").read_text()
        return stat[stat.rfind(")") + 2] not in ("Z", "X")
    except FileNotFoundError:
        return False


def wait_dead(pid):
    until = time.monotonic() + 3
    while live(pid) and time.monotonic() < until:
        time.sleep(0.02)
    return not live(pid)


class OwnedProcessTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def test_normal_nonzero_and_handlers(self):
        original = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}
        for code in (0, 7):
            with self.subTest(code=code):
                path = self.root / f"out-{code}"
                record = run_owned(
                    [
                        sys.executable,
                        "-c",
                        f"import sys; sys.stdout.buffer.write(b'abc'); sys.exit({code})",
                    ],
                    path,
                    2,
                )
                self.assertEqual(path.read_bytes(), b"abc")
                self.assertEqual(
                    record["log_sha256"], hashlib.sha256(b"abc").hexdigest()
                )
                self.assertEqual(record["bytes_logged"], 3)
                self.assertEqual(record["returncode"], code)
                self.assertEqual(record["ok"], code == 0)
                self.assertEqual(record["cleanup_errors"], [])
                self.assertEqual({s: signal.getsignal(s) for s in original}, original)

    def test_merged_stdout_stderr_order_and_hash(self):
        path = self.root / "merged"
        payload = b"out-1\nerr-2\nout-3\n"
        script = (
            "import os; os.write(1,b'out-1\\n'); "
            "os.write(2,b'err-2\\n'); os.write(1,b'out-3\\n')"
        )
        record = run_owned([sys.executable, "-c", script], path, 2)
        self.assertTrue(record["ok"])
        self.assertEqual(path.read_bytes(), payload)
        self.assertEqual(record["bytes_logged"], len(payload))
        self.assertEqual(record["log_sha256"], hashlib.sha256(payload).hexdigest())

    def test_explicit_child_environment_isolated_from_parent(self):
        sentinel = "PB053_PARENT_ONLY_TEST_SENTINEL"
        selected = "PB053_CHILD_SELECTED_TEST_VALUE"
        previous = os.environ.get(sentinel)
        os.environ[sentinel] = "parent-only-value"
        try:
            before = dict(os.environ)
            path = self.root / "child-environment"
            script = (
                "import json,os; print(json.dumps({"
                f"'selected':os.environ.get({selected!r}),"
                f"'sentinel_present':{sentinel!r} in os.environ"
                "}))"
            )
            record = run_owned(
                [sys.executable, "-c", script],
                path,
                2,
                env={selected: "guest-private-value"},
            )
            self.assertTrue(record["ok"], record)
            self.assertEqual(
                json.loads(path.read_text()),
                {"selected": "guest-private-value", "sentinel_present": False},
            )
            self.assertEqual(dict(os.environ), before)
        finally:
            if previous is None:
                os.environ.pop(sentinel, None)
            else:
                os.environ[sentinel] = previous

    def test_real_child_uses_explicit_cwd_and_env(self):
        directory = self.root / "child-cwd"
        directory.mkdir()
        output = self.root / "cwd-log"
        record = run_owned(
            [
                sys.executable,
                "-c",
                "import os,json; print(json.dumps([os.getcwd(),os.getenv('PB053_TEST')]))",
            ],
            output,
            2,
            env={"PB053_TEST": "selected"},
            cwd=directory,
        )
        self.assertTrue(record["ok"], record)
        self.assertEqual(json.loads(output.read_text()), [str(directory), "selected"])

    def test_timeout_and_detached_descendant(self):
        for exit_leader in (False, True):
            with self.subTest(exit_leader=exit_leader):
                child_pid = self.root / f"pid-{exit_leader}"
                script = (
                    "import subprocess,sys,time; "
                    "p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)']); "
                    f"open({str(child_pid)!r},'w').write(str(p.pid)); "
                    "print('ready',flush=True); "
                    + ("sys.exit(0)" if exit_leader else "time.sleep(30)")
                )
                record = run_owned(
                    [sys.executable, "-c", script],
                    self.root / f"log-{exit_leader}",
                    0.5,
                )
                self.assertTrue(child_pid.exists())
                self.assertTrue(wait_dead(int(child_pid.read_text())))
                self.assertTrue(wait_dead(record["pid"]))
                self.assertFalse(record["ok"])
                if exit_leader:
                    self.assertTrue(record["descendant_cleanup_required"])
                else:
                    self.assertTrue(record["timed_out"])

    def test_cap_and_exact_cap(self):
        for size in (8, 9):
            with self.subTest(size=size):
                path = self.root / f"cap-{size}"
                script = f"import os,time; os.write(1,b'x'*{size}); time.sleep(0.2)"
                record = run_owned(
                    [sys.executable, "-c", script], path, 2, max_log_bytes=8
                )
                self.assertEqual(path.read_bytes(), b"x" * 8)
                self.assertEqual(
                    record["log_sha256"], hashlib.sha256(b"x" * 8).hexdigest()
                )
                self.assertEqual(record["log_limit_exceeded"], size > 8)
                self.assertEqual(record["ok"], size == 8)
                self.assertTrue(wait_dead(record["pid"]))

    def test_sigterm_ignored_escalates_to_sigkill(self):
        path = self.root / "ignore-term"
        marker = self.root / "descendant-ready"
        descendant = (
            "import signal,time,pathlib; "
            "signal.signal(signal.SIGTERM,signal.SIG_IGN); "
            f"pathlib.Path({str(marker)!r}).write_text('ready'); time.sleep(30)"
        )
        script = (
            "import os,signal,subprocess,sys,time,pathlib; "
            "signal.signal(signal.SIGTERM,signal.SIG_IGN); "
            f"p=subprocess.Popen([sys.executable,'-c',{descendant!r}]); "
            f"m=pathlib.Path({str(marker)!r})\n"
            "while not m.exists(): time.sleep(0.01)\n"
            "print(f'child-ready {os.getpid()} {p.pid}',flush=True); time.sleep(30)"
        )
        start = time.monotonic()
        record = run_owned([sys.executable, "-c", script], path, 0.5)
        self.assertLess(time.monotonic() - start, 5)
        self.assertTrue(record["timed_out"])
        self.assertFalse(record["ok"])
        self.assertEqual(record["returncode"], -signal.SIGKILL)
        self.assertEqual(record["cleanup_errors"], [])
        first = path.read_text().splitlines()[0].split()
        self.assertEqual(first[0], "child-ready")
        self.assertTrue(wait_dead(int(first[1])))
        self.assertTrue(wait_dead(int(first[2])))

    def test_repeated_signals_during_cleanup_preserve_first(self):
        log = self.root / "repeat-log"
        report = self.root / "repeat-record"
        term_seen = self.root / "term-seen"
        child = (
            "import os,signal,time; "
            f"signal.signal(signal.SIGTERM,lambda *_: open({str(term_seen)!r},'w').write('seen')); "
            "print(f'child-ready {os.getpid()}',flush=True); time.sleep(30)"
        )
        script = (
            "import json,pathlib,sys; from bluez_host_process import run_owned; "
            f"r=run_owned([sys.executable,'-c',{child!r}],{str(log)!r},10); "
            f"pathlib.Path({str(report)!r}).write_text(json.dumps(r)); sys.exit(1)"
        )
        harness = subprocess.Popen(
            [sys.executable, "-c", script],
            env=dict(os.environ, PYTHONPATH=str(ROOT / "scripts")),
        )
        try:
            until = time.monotonic() + 4
            while time.monotonic() < until:
                if log.exists() and b"child-ready" in log.read_bytes():
                    break
                time.sleep(0.02)
            else:
                self.fail("child readiness missing")
            pid = int(log.read_text().splitlines()[0].split()[1])
            os.kill(harness.pid, signal.SIGTERM)
            until = time.monotonic() + 4
            while time.monotonic() < until:
                if term_seen.exists():
                    break
                time.sleep(0.02)
            else:
                self.fail("SIGTERM cleanup not observed")
            os.kill(harness.pid, signal.SIGINT)
            self.assertEqual(harness.wait(timeout=5), 1)
            record = json.loads(report.read_text())
            self.assertEqual(record["cancelled_signal"], signal.SIGTERM)
            self.assertFalse(record["ok"])
            self.assertTrue(wait_dead(pid))
            self.assertEqual(
                record["log_sha256"], hashlib.sha256(log.read_bytes()).hexdigest()
            )
        finally:
            if harness.poll() is None:
                harness.kill()
                harness.wait()

    def test_refuse_invalid_and_existing_log(self):
        target = self.root / "sentinel"
        target.write_text("unchanged")
        link = self.root / "link"
        link.symlink_to(target)
        for path in (target, link, self.root / "absent" / "log"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                run_owned([sys.executable], path, 1)
        for argv, timeout, cap in (
            ([], 1, 1),
            ([""], 1, 1),
            ([sys.executable], True, 1),
            ([sys.executable], float("inf"), 1),
            ([sys.executable], 1, False),
            ([sys.executable], 1, 0),
        ):
            with (
                self.subTest(argv=argv, timeout=timeout, cap=cap),
                self.assertRaises(ValueError),
            ):
                run_owned(argv, self.root / "new", timeout, cap)
        self.assertEqual(target.read_text(), "unchanged")
        self.assertFalse((self.root / "new").exists())

    def test_spawn_failure_and_non_main_thread(self):
        path = self.root / "spawn-failed"
        original = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}
        record = run_owned([str(self.root / "missing-executable")], path, 1)
        self.assertFalse(record["ok"])
        self.assertIsNone(record["pid"])
        self.assertIsNone(record["returncode"])
        self.assertIn("FileNotFoundError", record["error"])
        self.assertEqual(path.read_bytes(), b"")
        self.assertEqual(record["log_sha256"], hashlib.sha256(b"").hexdigest())
        self.assertEqual({s: signal.getsignal(s) for s in original}, original)

        errors = []

        def worker():
            try:
                run_owned([sys.executable], self.root / "thread-log", 1)
            except ValueError as exc:
                errors.append(str(exc))

        thread = threading.Thread(target=worker)
        thread.start()
        thread.join(timeout=2)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, ["run_owned must run in main thread"])
        self.assertFalse((self.root / "thread-log").exists())

    def test_cancellation_from_external_harness(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signum=signum):
                report = self.root / f"record-{signum}"
                log = self.root / f"signal-log-{signum}"
                script = (
                    "import json,pathlib,sys; from bluez_host_process import run_owned; "
                    "r=run_owned([sys.executable,'-c',"
                    + repr(
                        "import os,sys,time; print(os.getpid(),flush=True); "
                        "print('child-ready',flush=True); time.sleep(30)"
                    )
                    + f"],{str(log)!r},10); pathlib.Path({str(report)!r}).write_text(json.dumps(r)); sys.exit(1)"
                )
                env = dict(os.environ, PYTHONPATH=str(ROOT / "scripts"))
                harness = subprocess.Popen([sys.executable, "-c", script], env=env)
                try:
                    until = time.monotonic() + 4
                    while time.monotonic() < until:
                        if log.exists() and b"child-ready" in log.read_bytes():
                            break
                        time.sleep(0.02)
                    else:
                        self.fail("child readiness missing")
                    pid = int(log.read_text().splitlines()[0])
                    os.kill(harness.pid, signum)
                    self.assertEqual(harness.wait(timeout=5), 1)
                    record = json.loads(report.read_text())
                    self.assertEqual(record["cancelled_signal"], signum)
                    self.assertFalse(record["ok"])
                    self.assertTrue(wait_dead(pid))
                    self.assertEqual(
                        record["log_sha256"],
                        hashlib.sha256(log.read_bytes()).hexdigest(),
                    )
                finally:
                    if harness.poll() is None:
                        harness.kill()
                        harness.wait()


if __name__ == "__main__":
    unittest.main()
