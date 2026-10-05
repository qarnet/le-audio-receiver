"""Public PB-053 guest preparation and result-boundary tests."""

import importlib.util
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests/unit/bluez_host_results"))
from test_bluez_host_results import (
    STIMULUS,
    authored_capture,
    synthetic_guest,
    synthetic_public,
    RUN_ID,
)
from bluez_host_results import decode_marker, validate_public
from bluez_host_process import _group_live

HOST = ROOT / "scripts/bluez_host_guest.py"
GUEST = ROOT / "scripts/bluez_guest_init.py"
PUBLIC = ROOT / "scripts/bluez_guest_public.py"
MONITOR = ROOT / "scripts/bluez_guest_monitor.py"
ACQUIRE = ROOT / "scripts/bluez_guest_acquire.py"
LIMITS = ROOT / "scripts/bluez_guest_limits.py"
PROCESS = ROOT / "scripts/bluez_host_process.py"
spec = importlib.util.spec_from_file_location("bluez_host_guest", HOST)
guest_host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest_host)
spec = importlib.util.spec_from_file_location("bluez_guest_init", GUEST)
guest_init = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest_init)
spec = importlib.util.spec_from_file_location("bluez_guest_public", PUBLIC)
guest_public = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest_public)


class GuestBoundaryTests(unittest.TestCase):
    def test_preparation_entry_and_stage_metadata_budgets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "empty").write_bytes(b"")
            (root / "folder").mkdir()
            (root / "folder/link").symlink_to("../empty")
            records, used = guest_host.entries(root)
            self.assertEqual(used, 0)
            self.assertEqual(set(records), {"empty", "folder", "folder/link"})
            with self.assertRaisesRegex(ValueError, "entry quota"):
                guest_host.entries(root, guest_host.EntryBudget(max_entries=2))
            with self.assertRaisesRegex(ValueError, "metadata quota"):
                guest_host.entries(root, guest_host.EntryBudget(max_field=4))
            with self.assertRaisesRegex(ValueError, "metadata quota"):
                guest_host.entries(
                    root,
                    guest_host.EntryBudget(max_metadata=len(str(root).encode()) + 5),
                )
            first = root / "first"
            second = root / "second"
            first.mkdir()
            second.mkdir()
            budget = guest_host.EntryBudget(max_roots=1)
            guest_host.entries(first, budget)
            with self.assertRaisesRegex(ValueError, "root quota"):
                guest_host.entries(second, budget)
            (first / "a").touch()
            (second / "b").touch()
            shared = guest_host.EntryBudget(max_entries=1)
            guest_host.entries(first, shared)
            with self.assertRaisesRegex(ValueError, "entry quota"):
                guest_host.entries(second, shared)
            stage = root / "stage"
            stage.mkdir()
            (stage / "dev").mkdir()
            (stage / "dev/console").touch()
            (stage / "entry").touch()
            self.assertEqual(guest_host.stage_paths(stage), ["dev", "entry"])
            with self.assertRaisesRegex(ValueError, "entry quota"):
                guest_host.stage_paths(stage, guest_host.EntryBudget(max_entries=2))
            (stage / "link").symlink_to("x" * 20)
            with self.assertRaisesRegex(ValueError, "metadata quota"):
                guest_host.stage_paths(stage, guest_host.EntryBudget(max_field=10))

    def test_streamed_preparation_outputs_exact_cap_and_overflow(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = {"files": {"empty": {}, "name": "value"}}
            encoded = (json.dumps(record, indent=2) + "\n").encode()
            guest_host.write_manifest(record, root / "manifest", len(encoded))
            self.assertEqual((root / "manifest").read_bytes(), encoded)
            with self.assertRaisesRegex(ValueError, "output quota"):
                guest_host.write_manifest(
                    record, root / "manifest-short", len(encoded) - 1
                )
            paths = ["one", "two"]
            raw_paths = b"one\0two\0"
            guest_host.write_path_list(paths, root / "paths", len(raw_paths))
            self.assertEqual((root / "paths").read_bytes(), raw_paths)
            with self.assertRaisesRegex(ValueError, "output quota"):
                guest_host.write_path_list(
                    paths, root / "paths-short", len(raw_paths) - 1
                )
            cpio = root / "cpio"
            cpio.write_bytes(b"authored cpio data")
            guest_host.compress_cpio(cpio, root / "gzip", input_cap=18, output_cap=1024)
            compressed = (root / "gzip").read_bytes()
            import gzip

            self.assertEqual(
                gzip.decompress(compressed),
                guest_host.console_header() + cpio.read_bytes(),
            )
            (root / "exact").mkdir()
            (root / "short").mkdir()
            guest_host.compress_cpio(
                cpio, root / "exact/gzip", input_cap=18, output_cap=len(compressed)
            )
            with self.assertRaisesRegex(ValueError, "output quota"):
                guest_host.compress_cpio(
                    cpio,
                    root / "short/gzip",
                    input_cap=18,
                    output_cap=len(compressed) - 1,
                )
            with self.assertRaisesRegex(ValueError, "Uncompressed cpio quota"):
                guest_host.compress_cpio(
                    cpio, root / "input-short", input_cap=17, output_cap=1024
                )

    def test_bounded_preparation_command_outputs_and_owned_cancellation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run = guest_host.run_bounded_command

            def child(out, err):
                return [
                    sys.executable,
                    "-c",
                    f"import os; os.write(1,{out!r}); os.write(2,{err!r})",
                ]

            exact = run(child(b"abc", b""), root, "exact", 3, 1, 5)
            self.assertEqual(exact.read_bytes(), b"abc")
            self.assertEqual((root / "exact.stderr").read_bytes(), b"")
            with self.assertRaisesRegex(ValueError, "output quota"):
                run(child(b"abcd", b""), root, "stdout-over", 3, 1, 5)
            with self.assertRaisesRegex(ValueError, "output quota"):
                run(child(b"", b"ab"), root, "stderr-over", 3, 1, 5)
            with self.assertRaisesRegex(ValueError, "emitted stderr"):
                run(child(b"", b"a"), root, "stderr", 3, 1, 5)
            program = (
                "import os, subprocess, sys, time; "
                "subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)']); "
                "print(os.getpgrp(), flush=True); time.sleep(30)"
            )
            with self.assertRaisesRegex(ValueError, "Command timed out"):
                run(
                    [sys.executable, "-u", "-c", program],
                    root,
                    "timeout",
                    64,
                    64,
                    0.5,
                )
            self.assertFalse(
                _group_live(int((root / "timeout.stdout").read_text().strip()))
            )
            timer = threading.Timer(0.2, os.kill, args=(os.getpid(), signal.SIGTERM))
            timer.start()
            try:
                with self.assertRaisesRegex(ValueError, "Run cancelled by signal"):
                    run(
                        [sys.executable, "-u", "-c", program], root, "cancel", 64, 64, 5
                    )
            finally:
                timer.join(timeout=5)
            pgid = int((root / "cancel.stdout").read_text().strip())
            self.assertFalse(_group_live(pgid))

    def test_source_snapshots_bind_frozen_bytes_and_reject_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "prepared"
            (output / "opt/pb053").mkdir(parents=True)
            emulator_bytes = b"authored source snapshot fixture, not an emulator\n"
            emulator_sha = hashlib.sha256(emulator_bytes).hexdigest()
            sources = {
                key: root / ("emulator.bin" if key == "emulator" else key + ".py")
                for key in (*guest_host.SOURCE_DESTINATIONS, "host")
            }
            for key, source in sources.items():
                source.write_bytes(
                    emulator_bytes
                    if key == "emulator"
                    else ("reviewed " + key).encode()
                )
            with self.assertRaisesRegex(ValueError, "emulator pin mismatch"):
                guest_host.freeze_sources(sources)
            with self.assertRaisesRegex(ValueError, "emulator pin mismatch"):
                guest_host.freeze_sources(sources, expected_emulator_sha="0" * 64)
            for invalid in (emulator_sha.upper(), emulator_sha[:-1], 42):
                with (
                    self.subTest(expected=invalid),
                    self.assertRaisesRegex(ValueError, "lowercase64-hex"),
                ):
                    guest_host.freeze_sources(sources, expected_emulator_sha=invalid)
            frozen = guest_host.freeze_sources(
                sources, expected_emulator_sha=emulator_sha
            )
            self.assertEqual(len(frozen), 9)
            copied = guest_host.stage_sources(sources, frozen, output, output)
            guest_host.verify_sources(sources, frozen, copied)
            self.assertEqual(copied["host"], output / "host-source.py")
            self.assertEqual(stat.S_IMODE(copied["emulator"].stat().st_mode), 0o755)
            self.assertEqual(copied["emulator"].read_bytes(), emulator_bytes)
            self.assertEqual(guest_host.digest(copied["guest"]), frozen["guest"])

            sources["guest"].write_bytes(b"changed after staging")
            with self.assertRaisesRegex(ValueError, "Source changed.*guest"):
                guest_host.verify_sources(sources, frozen, copied)
            self.assertEqual(guest_host.digest(copied["guest"]), frozen["guest"])
            sources["guest"].write_bytes(b"reviewed guest")
            copied["guest"].chmod(0o644)
            copied["guest"].write_bytes(b"changed staged copy")
            with self.assertRaisesRegex(ValueError, "Staged source changed.*guest"):
                guest_host.verify_sources(sources, frozen, copied)

    def test_guest_run_tokens_strict(self):
        self.assertEqual(
            guest_init.parse_run_tokens(
                f"console=ttyS0 pb053_run={RUN_ID} pb053_scenario=normal"
            ),
            (RUN_ID, "normal"),
        )
        self.assertEqual(
            guest_init.parse_run_tokens(f"pb053_run={RUN_ID} pb053_scenario=hold"),
            (RUN_ID, "hold"),
        )
        for cmdline in (
            "pb053_scenario=hold",
            f"pb053_run={RUN_ID}",
            f"pb053_run={RUN_ID} pb053_run={RUN_ID} pb053_scenario=normal",
            f"pb053_run={RUN_ID} pb053_scenario=hold pb053_scenario=normal",
            "pb053_run=INVALID pb053_scenario=hold",
            f"pb053_run={RUN_ID} pb053_scenario=custom",
        ):
            with self.subTest(cmdline=cmdline), self.assertRaises(RuntimeError):
                guest_init.parse_run_tokens(cmdline)

    def serial_fixture(self, record=None):
        """Authored component-only report/capture pair; not VM evidence."""
        return (
            "PB053_GUEST_RESULT "
            + json.dumps(record or synthetic_guest())
            + "\n"
            + json.dumps({"process_log": "monitor", "content": authored_capture()})
            + "\n"
        )

    def test_guest_stopped_event_is_appended_with_pid(self):
        stages = []
        guest_init.record_stopped(
            stages, "monitor", SimpleNamespace(pid=37, returncode=0)
        )
        self.assertEqual(
            stages,
            [{"stage": "stopped", "process": "monitor", "pid": 37, "returncode": 0}],
        )

    def test_owned_actor_requires_live_child_until_term_and_reaps(self):
        for code in (0, 7):
            with self.subTest(prior_exit=code):
                with subprocess.Popen(
                    [sys.executable, "-c", f"raise SystemExit({code})"],
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                ) as proc:
                    self.assertEqual(proc.wait(timeout=5), code)
                    with self.assertRaisesRegex(
                        RuntimeError, f"emulator exited before owned stop: {code}"
                    ):
                        guest_init.stop_owned_actor("emulator", proc)
                    self.assertEqual(proc.returncode, code)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ready = root / "ready"
            terminated = root / "terminated"
            program = (
                "import pathlib, signal, sys, time; "
                "signal.signal(signal.SIGTERM, "
                "lambda signum, frame: (pathlib.Path(sys.argv[2]).write_text('TERM'), sys.exit(0))); "
                "pathlib.Path(sys.argv[1]).write_text('ready'); "
                "time.sleep(30)"
            )
            with subprocess.Popen(
                [sys.executable, "-c", program, str(ready), str(terminated)],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            ) as proc:
                try:
                    deadline = time.monotonic() + 5
                    while not ready.exists() and time.monotonic() < deadline:
                        self.assertIsNone(proc.poll(), "Actor exited before ready")
                        time.sleep(0.01)
                    self.assertTrue(ready.exists(), "Actor not ready")
                    guest_init.stop_owned_actor("bluez-fresh1", proc)
                    self.assertEqual(proc.returncode, 0)
                    self.assertEqual(terminated.read_text(), "TERM")
                    stages = []
                    guest_init.record_stopped(stages, "bluez-fresh1", proc)
                    self.assertEqual(len(stages), 1)
                    with self.assertRaisesRegex(
                        RuntimeError, "bluez-fresh1 exited before owned stop: 0"
                    ):
                        guest_init.stop_owned_actor("bluez-fresh1", proc)
                    self.assertEqual(terminated.read_text(), "TERM")
                finally:
                    if proc.poll() is None:
                        proc.kill()
                    proc.wait(timeout=5)

        with tempfile.TemporaryDirectory() as directory:
            ready = Path(directory) / "ready"
            program = (
                "import pathlib, signal, sys, time; "
                "signal.signal(signal.SIGTERM, lambda *_: sys.exit(7)); "
                "pathlib.Path(sys.argv[1]).write_text('ready'); time.sleep(30)"
            )
            with subprocess.Popen(
                [sys.executable, "-c", program, str(ready)],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            ) as proc:
                try:
                    deadline = time.monotonic() + 5
                    while not ready.exists() and time.monotonic() < deadline:
                        self.assertIsNone(proc.poll(), "Actor exited before ready")
                        time.sleep(0.01)
                    self.assertTrue(ready.exists(), "Actor not ready")
                    with self.assertRaisesRegex(
                        RuntimeError, "monitor owned stop failed: 7"
                    ):
                        guest_init.stop_owned_actor("monitor", proc)
                    self.assertEqual(proc.returncode, 7)
                finally:
                    if proc.poll() is None:
                        proc.kill()
                    proc.wait(timeout=5)

    def test_cleanup_failure_cannot_be_reported_as_success(self):
        record = synthetic_guest()
        self.assertTrue(
            guest_host.parse_result(self.serial_fixture(record), RUN_ID, "normal")["ok"]
        )
        failed = {
            **record,
            "stages": record["stages"]
            + [
                {
                    "stage": "cleanup_failure",
                    "process": "monitor",
                    "error": "TERM/wait: monitor exited before owned stop: 0",
                }
            ],
        }
        with self.assertRaises(ValueError):
            guest_host.parse_result(self.serial_fixture(failed), RUN_ID, "normal")

    def test_guest_controller_identity_and_info(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("hci2", "hci5"):
                (root / name).mkdir()
            prior = {
                "adapters": ["/org/bluez/hci2", "/org/bluez/hci5"],
                "addresses": ["00:AA:01:00:00:00", "00:AA:02:00:00:00"],
            }
            self.assertEqual(
                guest_init.controller_indices(prior, root),
                [(2, prior["addresses"][0]), (5, prior["addresses"][1])],
            )
            self.assertEqual(
                guest_init.controller_info(
                    "hci2: Primary controller\n\taddr 00:AA:01:00:00:00 version 11\n"
                    "\tcurrent settings: powered le\n",
                    prior["addresses"][0],
                ),
                prior["addresses"][0],
            )
            with self.assertRaises(RuntimeError):
                guest_init.controller_info(
                    "addr 00:AA:01:00:00:00\ncurrent settings: le\n",
                    prior["addresses"][0],
                )
            with self.assertRaises(RuntimeError):
                guest_init.controller_indices(
                    {**prior, "adapters": ["/org/bluez/hci2", "/org/bluez/hci9"]}, root
                )

    def run_cli(self, prepared, output, expected=None):
        return subprocess.run(
            [
                sys.executable,
                str(HOST),
                "run",
                "--prepared",
                str(prepared),
                "--output",
                str(output),
                "--manifest-sha256",
                expected or "0" * 64,
            ],
            capture_output=True,
            text=True,
            timeout=5,
        )

    def authored_manifest(self, prepared):
        manifest = {
            "schema_version": 2,
            "version": guest_host.VERSION,
            "scope": guest_host.SCOPE,
            "cpu_profile": guest_host.CPU_PROFILE,
            "artifacts": {name: "0" * 64 for name in guest_host.ARTIFACT_CAPS},
            "source_hashes": {
                "host": guest_host.digest(HOST),
                "guest": guest_host.digest(GUEST),
                "public": guest_host.digest(PUBLIC),
                "results": guest_host.digest(ROOT / "scripts/bluez_host_results.py"),
                "monitor": guest_host.digest(MONITOR),
                "acquire": guest_host.digest(ACQUIRE),
                "limits": guest_host.digest(LIMITS),
                "process": guest_host.digest(PROCESS),
                "emulator": guest_host.EMULATOR_SHA,
            },
            "stimulus": {
                "source": str(guest_host.STIMULUS.relative_to(ROOT)),
                "guest_path": "/opt/pb053/stimulus.lc3",
                "size": guest_host.STIMULUS_SIZE,
                "sha256": guest_host.STIMULUS_SHA,
                "purpose": guest_host.STIMULUS_PURPOSE,
            },
            "qemu": {"path": str(guest_host.QEMU), "size": 123, "sha256": "0" * 64},
        }

        def write():
            data = json.dumps(manifest).encode()
            (prepared / "manifest.json").write_bytes(data)
            return hashlib.sha256(data).hexdigest()

        return manifest, write

    def test_manifest_shape_and_digest_fail_before_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            prepared = Path(directory) / "prepared"
            prepared.mkdir()
            manifest, write = self.authored_manifest(prepared)
            out = Path(directory) / "out"
            for key, bad in (
                ("schema_version", 1),
                ("version", "wrong"),
                ("scope", "wrong"),
                ("source_hashes", {}),
                ("artifacts", {"kernel": "0" * 64}),
                ("qemu", {"path": "wrong"}),
                ("stimulus", {"size": False}),
            ):
                with self.subTest(key=key):
                    original = manifest[key]
                    manifest[key] = bad
                    self.assertNotEqual(
                        self.run_cli(prepared, out, write()).returncode, 0
                    )
                    self.assertFalse(out.exists())
                    manifest[key] = original
            for changed in (
                {**manifest["artifacts"], "kernel": "wrong"},
                {**manifest["artifacts"], "config.gz": 42},
                {**manifest["artifacts"], "unexpected": "0" * 64},
            ):
                manifest["artifacts"] = changed
                self.assertNotEqual(self.run_cli(prepared, out, write()).returncode, 0)
                self.assertFalse(out.exists())
            manifest["artifacts"] = {
                name: "0" * 64 for name in guest_host.ARTIFACT_CAPS
            }
            manifest["source_hashes"]["host"] = "f" * 64
            self.assertNotEqual(self.run_cli(prepared, out, write()).returncode, 0)
            self.assertFalse(out.exists())
            manifest["source_hashes"]["host"] = guest_host.digest(HOST)
            for key in ("limits", "process"):
                original = manifest["source_hashes"][key]
                manifest["source_hashes"][key] = "f" * 64
                self.assertNotEqual(self.run_cli(prepared, out, write()).returncode, 0)
                self.assertFalse(out.exists())
                manifest["source_hashes"][key] = original
            self.assertNotEqual(self.run_cli(prepared, out, "f" * 64).returncode, 0)
            self.assertFalse(out.exists())
            raw = b'{"schema_version":2,"schema_version":2}'
            (prepared / "manifest.json").write_bytes(raw)
            self.assertNotEqual(
                self.run_cli(prepared, out, hashlib.sha256(raw).hexdigest()).returncode,
                0,
            )
            for raw in (b'{"schema_version":NaN}', b'{"schema_version":Infinity}'):
                (prepared / "manifest.json").write_bytes(raw)
                self.assertNotEqual(
                    self.run_cli(
                        prepared, out, hashlib.sha256(raw).hexdigest()
                    ).returncode,
                    0,
                )

    def test_oversized_and_symlink_manifest_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            prepared = Path(directory) / "prepared"
            prepared.mkdir()
            out = Path(directory) / "out"
            path = prepared / "manifest.json"
            with path.open("wb") as stream:
                stream.truncate(guest_host.MANIFEST_CAP + 1)
            self.assertNotEqual(self.run_cli(prepared, out).returncode, 0)
            path.unlink()
            path.symlink_to(Path(directory) / "source")
            (Path(directory) / "source").write_text("secret")
            self.assertNotEqual(self.run_cli(prepared, out).returncode, 0)
            self.assertFalse(out.exists())

    def test_fifo_and_symlink_artifact_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            prepared = Path(directory) / "prepared"
            prepared.mkdir()
            _, write = self.authored_manifest(prepared)
            expected = write()
            out = Path(directory) / "out"
            os.mkfifo(prepared / "kernel")
            self.assertNotEqual(self.run_cli(prepared, out, expected).returncode, 0)
            (prepared / "kernel").unlink()
            (prepared / "kernel").symlink_to(prepared / "manifest.json")
            self.assertNotEqual(self.run_cli(prepared, out, expected).returncode, 0)
            self.assertFalse(out.exists())

    def test_nested_output_refused_without_touching_sentinel(self):
        with tempfile.TemporaryDirectory() as directory:
            prepared = Path(directory) / "prepared"
            prepared.mkdir()
            sentinel = prepared / "sentinel"
            sentinel.write_text("unchanged")
            output = prepared / "out"
            self.assertNotEqual(self.run_cli(prepared, output).returncode, 0)
            self.assertEqual(sentinel.read_text(), "unchanged")
            self.assertFalse(output.exists())
            build = guest_host.EMULATOR.parent
            with self.assertRaises(ValueError):
                guest_host.exclusive(build / "new", protected=(prepared,))

    def test_snapshot_exact_bytes_and_changed_source_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.write_bytes(b"authored kernel")
            original = source.read_bytes()
            expected = hashlib.sha256(original).hexdigest()
            copied = root / "copy"
            self.assertEqual(
                guest_host.snapshot(source, copied, 1024, expected), expected
            )
            self.assertEqual(copied.read_bytes(), original)
            self.assertEqual(source.read_bytes(), original)
            self.assertFalse(copied.stat().st_mode & 0o222)
            source.write_bytes(b"replacement")
            with self.assertRaises(ValueError):
                guest_host.snapshot(source, root / "other", 1024, expected)

    def test_scoped_run_cancellation_interrupts_snapshot_and_restores_handlers(self):
        handlers = {
            signum: signal.getsignal(signum)
            for signum in (signal.SIGINT, signal.SIGTERM)
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            contents = b"a" * (2 * 1024 * 1024)
            source.write_bytes(contents)
            expected = hashlib.sha256(contents).hexdigest()
            with guest_host.RunCancellation() as cancellation:
                os.kill(os.getpid(), signal.SIGTERM)
                with self.assertRaisesRegex(ValueError, "Run cancelled by signal 15"):
                    guest_host.snapshot(
                        source,
                        root / "pre-copy",
                        len(contents),
                        expected,
                        cancel_check=cancellation.check,
                    )
                self.assertFalse((root / "pre-copy").exists())
                os.kill(os.getpid(), signal.SIGINT)
                self.assertEqual(cancellation.cancelled_signal, signal.SIGTERM)
            with guest_host.RunCancellation() as cancellation:
                calls = 0

                def during_copy():
                    nonlocal calls
                    calls += 1
                    if calls == 4:
                        os.kill(os.getpid(), signal.SIGINT)
                    cancellation.check()

                with self.assertRaisesRegex(ValueError, "Run cancelled by signal 2"):
                    guest_host.snapshot(
                        source,
                        root / "during-copy",
                        len(contents),
                        expected,
                        cancel_check=during_copy,
                    )
                self.assertLess((root / "during-copy").stat().st_size, len(contents))
        for signum, handler in handlers.items():
            self.assertIs(signal.getsignal(signum), handler)

    def test_post_processing_signal_seals_failure_even_during_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "run-record.json"

            class SignallingRecord:
                def __init__(self):
                    self.writes = 0

                def write_text(self, contents):
                    target.write_text(contents)
                    self.writes += 1
                    if self.writes == 1:
                        os.kill(os.getpid(), signal.SIGTERM)
                        os.kill(os.getpid(), signal.SIGINT)

            with guest_host.RunCancellation() as cancellation:
                record = {"ok": True, "error": None, "process": {"timed_out": False}}
                publication = SignallingRecord()
                guest_host.seal_run_record(record, publication, cancellation)
                self.assertEqual(publication.writes, 2)
                saved = json.loads(target.read_text())
                self.assertFalse(saved["ok"])
                self.assertTrue(saved["cancellation"])
                self.assertEqual(saved["cancelled_signal"], signal.SIGTERM)
                self.assertIn("Run cancelled by signal", saved["error"])
                self.assertFalse(saved["process"]["timed_out"])

            with guest_host.RunCancellation() as cancellation:
                record = {"ok": True, "error": "ValueError: existing failure"}
                os.kill(os.getpid(), signal.SIGINT)
                with self.assertRaisesRegex(ValueError, "Run cancelled by signal 2"):
                    cancellation.check()
                guest_host.seal_run_record(record, target, cancellation)
                saved = json.loads(target.read_text())
                self.assertFalse(saved["ok"])
                self.assertEqual(saved["error"], "ValueError: existing failure")
                self.assertEqual(saved["cancelled_signal"], signal.SIGINT)

    def test_run_cancellation_rejects_non_main_thread(self):
        errors = []

        def enter():
            try:
                with guest_host.RunCancellation():
                    pass
            except ValueError as exc:
                errors.append(str(exc))

        thread = threading.Thread(target=enter)
        thread.start()
        thread.join(timeout=5)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, ["RunCancellation must run in main thread"])

    def test_stimulus_snapshot_and_pre_seal_verification(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "stimulus.lc3"
            original = guest_host.STIMULUS.read_bytes()
            source.write_bytes(original)
            self.assertEqual(len(original), guest_host.STIMULUS_SIZE)
            self.assertEqual(
                hashlib.sha256(original).hexdigest(), guest_host.STIMULUS_SHA
            )

            source.write_bytes(bytes([original[0] ^ 1]) + original[1:])
            with self.assertRaisesRegex(ValueError, "Snapshot hash mismatch"):
                guest_host.stage_stimulus(source, root / "mismatch.lc3")

            source.write_bytes(original)
            staged = root / "staged.lc3"
            guest_host.stage_stimulus(source, staged)
            self.assertEqual(staged.read_bytes(), original)
            guest_host.verify_staged_stimulus(staged)
            staged.chmod(0o644)
            staged.write_bytes(bytes([original[0] ^ 1]) + original[1:])
            with self.assertRaisesRegex(ValueError, "Staged LC3 transport stimulus"):
                guest_host.verify_staged_stimulus(staged)

            short = root / "short.lc3"
            guest_host.stage_stimulus(source, short)
            short.chmod(0o644)
            short.write_bytes(original[:-1])
            with self.assertRaisesRegex(ValueError, "Staged LC3 transport stimulus"):
                guest_host.verify_staged_stimulus(short)

    def test_empty_regular_input_rejected_by_bounded_read(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "empty"
            source.write_bytes(b"")
            with self.assertRaises(ValueError):
                guest_host.regular_hash(source, 1024)
            with self.assertRaises(ValueError):
                guest_host.snapshot(
                    source,
                    Path(directory) / "copy",
                    1024,
                    hashlib.sha256(b"").hexdigest(),
                )

    def test_raw_false_result_remains_parseable_without_acceptance(self):
        line = "PB053_GUEST_RESULT " + json.dumps(
            {"ok": False, "failed_stage": "daemon"}
        )
        self.assertEqual(guest_host.parse_guest_marker(line)["failed_stage"], "daemon")
        with self.assertRaises(ValueError):
            guest_host.parse_result(line, RUN_ID, "normal")
        for serial in (line + "\n" + line, "PB053_GUEST_RESULT {broken"):
            with self.assertRaises(ValueError):
                guest_host.parse_guest_marker(serial)

    def test_receive_packet_seqpacket_and_truncation(self):
        sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        with sender, receiver:
            payload = bytes(range(120))
            self.assertEqual(sender.send(payload), 120)
            data, flags = guest_public.receive_packet(receiver.fileno(), 121, 0.5)
            self.assertEqual(data, payload)
            self.assertFalse(flags & socket.MSG_TRUNC)

            self.assertEqual(sender.send(payload + b"extra"), 125)
            data, flags = guest_public.receive_packet(receiver.fileno(), 121, 0.5)
            self.assertEqual(data, (payload + b"extra")[:121])
            self.assertTrue(flags & socket.MSG_TRUNC)

            with self.assertRaises(socket.timeout):
                guest_public.receive_packet(receiver.fileno(), 121, 0.05)

    def test_revocation_requires_hangup_or_eof_without_unread_data(self):
        sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        with sender, receiver:
            self.assertEqual(sender.send(b""), 0)
            with self.assertRaisesRegex(
                RuntimeError, "Empty ISO packet without peer hangup at revocation"
            ):
                guest_public.wait_revoked(receiver.fileno(), 0.5)
            self.assertEqual(sender.send(b"still connected"), len(b"still connected"))
            self.assertEqual(
                guest_public.receive_packet(receiver.fileno(), 121, 0.5)[0],
                b"still connected",
            )

        sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        with sender, receiver:
            sender.close()
            evidence = guest_public.wait_revoked(receiver.fileno(), 0.5)
            self.assertIs(evidence["pollhup"], True)
            self.assertTrue(evidence["poll_flags"] & select.POLLHUP)

        sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        with sender, receiver:
            with self.assertRaises(socket.timeout):
                guest_public.wait_revoked(receiver.fileno(), 0.05)

        sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        with sender, receiver:
            sender.send(b"unread")
            sender.close()
            with self.assertRaisesRegex(RuntimeError, "Unread ISO packet"):
                guest_public.wait_revoked(receiver.fileno(), 0.5)

        sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        with sender, receiver:
            sender.send(b"still live")
            with self.assertRaisesRegex(RuntimeError, "Unread ISO packet"):
                guest_public.wait_revoked(receiver.fileno(), 0.5)

    def test_structured_public_property_snapshots_are_pure(self):
        device = {
            "Address": "00:AA:01:01:00:01",
            "Adapter": "/org/bluez/hci0",
            "Paired": 1,
            "Bonded": 0,
            "Connected": True,
            "ServicesResolved": False,
            "ignored": "diagnostic",
        }
        expected = {
            "Address": device["Address"],
            "Adapter": device["Adapter"],
            "Paired": True,
            "Bonded": False,
            "Connected": True,
            "ServicesResolved": False,
        }
        self.assertEqual(guest_public.device_snapshot(device), expected)
        self.assertEqual(device["Paired"], 1)
        for missing in expected:
            with self.subTest(missing=missing), self.assertRaises(KeyError):
                guest_public.device_snapshot(
                    {key: value for key, value in device.items() if key != missing}
                )
        transport = {
            "Codec": 6,
            "Configuration": bytes.fromhex("02010802020103047800050301000000"),
            "Device": "/org/bluez/hci0/dev_00_AA_01_01_00_01",
            "UUID": "00002BCB-0000-1000-8000-00805F9B34FB",
            "State": "idle",
            "ignored": "diagnostic",
        }
        self.assertEqual(
            guest_public.transport_snapshot(transport),
            {
                "Codec": 6,
                "Configuration": transport["Configuration"].hex(),
                "Device": transport["Device"],
                "UUID": transport["UUID"].lower(),
                "State": "idle",
            },
        )
        for missing in ("Codec", "Configuration", "Device", "UUID", "State"):
            with self.subTest(missing=missing), self.assertRaises(KeyError):
                guest_public.transport_snapshot(
                    {key: value for key, value in transport.items() if key != missing}
                )

    def test_exact_guest_environment_link_boundary(self):
        record = {"link": guest_host.GUEST_ENV_TARGET, "resolved": "/etc/environment"}
        for origin in guest_host.GUEST_ENV_LINKS:
            self.assertTrue(guest_host.guest_environment_link(Path(origin), record))
            self.assertFalse(
                guest_host.guest_environment_link(
                    Path(origin), {**record, "link": "/etc/environment"}
                )
            )
            self.assertFalse(
                guest_host.guest_environment_link(
                    Path(origin), {**record, "resolved": "/run/environment"}
                )
            )
        self.assertFalse(
            guest_host.guest_environment_link(
                Path("/nix/store/unreviewed/lib/environment.d/99-environment.conf"),
                record,
            )
        )

    def test_regular_store_file_catalog_and_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "plain-file"
            root.write_bytes(b"guest closure file")
            records, size = guest_host.entries(root)
            self.assertEqual(
                records,
                {
                    "": {
                        "sha256": guest_host.digest(root),
                        "size": len(b"guest closure file"),
                    }
                },
            )
            self.assertEqual(size, len(b"guest closure file"))

    def test_console_newc_metadata(self):
        header = guest_host.console_header()
        self.assertEqual(header[:6], b"070701")
        fields = [int(header[6 + 8 * n : 14 + 8 * n], 16) for n in range(13)]
        self.assertEqual(
            fields,
            [0x7FFFFFFE, 0o20600, 0, 0, 1, 0, 0, 0, 0, 5, 1, len(b"dev/console\0"), 0],
        )
        self.assertEqual(header[110 : 110 + fields[11]], b"dev/console\0")
        self.assertEqual(len(header) % 4, 0)
        self.assertEqual(
            header[110 + fields[11] :], b"\0" * (len(header) - 110 - fields[11])
        )

    def test_existing_output_preserves_sentinel(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "existing"
            output.mkdir()
            sentinel = output / "sentinel"
            sentinel.write_text("unchanged")
            proc = subprocess.run(
                [sys.executable, str(HOST), "prepare", "--output", str(output)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(proc.returncode, 0)
            self.assertEqual(sentinel.read_text(), "unchanged")

    def test_run_rejects_missing_or_mismatched_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            missing = subprocess.run(
                [
                    sys.executable,
                    str(HOST),
                    "run",
                    "--prepared",
                    str(parent / "missing"),
                    "--output",
                    str(parent / "out"),
                    "--manifest-sha256",
                    "0" * 64,
                ],
                capture_output=True,
            )
            self.assertNotEqual(missing.returncode, 0)
            self.assertFalse((parent / "out").exists())

            prepared = parent / "prepared"
            prepared.mkdir()
            (prepared / "manifest.json").write_text(
                json.dumps(
                    {
                        "version": guest_host.VERSION,
                        "schema_version": 1,
                        "artifacts": {
                            "kernel": "wrong",
                            "config.gz": "wrong",
                            "initramfs.cpio.gz": "wrong",
                        },
                    }
                )
            )
            for name in ("kernel", "config.gz", "initramfs.cpio.gz"):
                (prepared / name).write_bytes(b"broken")
            bad = subprocess.run(
                [
                    sys.executable,
                    str(HOST),
                    "run",
                    "--prepared",
                    str(prepared),
                    "--output",
                    str(parent / "out"),
                    "--manifest-sha256",
                    guest_host.digest(prepared / "manifest.json"),
                ],
                capture_output=True,
            )
            self.assertNotEqual(bad.returncode, 0)
            self.assertFalse((parent / "out").exists())

    def test_scenario_cli_rejects_unknown_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "out"
            proc = subprocess.run(
                [
                    sys.executable,
                    str(HOST),
                    "run",
                    "--prepared",
                    directory,
                    "--output",
                    str(output),
                    "--manifest-sha256",
                    "0" * 64,
                    "--scenario",
                    "custom",
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(proc.returncode, 0)
            self.assertFalse(output.exists())

    def test_invalid_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "out"
            proc = subprocess.run(
                [
                    sys.executable,
                    str(HOST),
                    "run",
                    "--prepared",
                    directory,
                    "--output",
                    str(output),
                    "--timeout",
                    "29",
                    "--manifest-sha256",
                    "0" * 64,
                ],
                capture_output=True,
            )
            self.assertNotEqual(proc.returncode, 0)
            self.assertFalse(output.exists())

    def test_guest_direct_execution_refused(self):
        proc = subprocess.run(
            [sys.executable, str(GUEST)], capture_output=True, text=True, timeout=5
        )
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("Guest requires PID1", proc.stderr)

    def test_monitor_direct_host_execution_refused(self):
        proc = subprocess.run(
            [sys.executable, str(MONITOR)], capture_output=True, text=True, timeout=5
        )
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("Monitor requires pb053_guest=1", proc.stderr)

    def test_public_direct_host_execution_refused(self):
        proc = subprocess.run(
            [sys.executable, str(PUBLIC)], capture_output=True, text=True, timeout=5
        )
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("Public child requires pb053_guest=1", proc.stdout)

    def test_public_case_result_cardinality(self):
        valid = "PB053_PUBLIC_RESULT " + json.dumps(synthetic_public("fresh"))
        self.assertTrue(
            guest_host.parse_result(self.serial_fixture(), RUN_ID, "normal")["ok"]
        )
        self.assertTrue(
            validate_public(synthetic_public("fresh"), "fresh", STIMULUS)["ok"]
        )
        for message in (
            "",
            valid + "\n" + valid,
            "PB053_PUBLIC_RESULT {bad",
            valid.replace('"pairing": true', '"pairing": false'),
            valid.replace('"pairing": true', '"other": true'),
            valid.replace('"pairing": true', ""),
            valid.replace(
                '"endpoint_configuration": true', '"endpoint_configuration": false'
            ),
            valid.replace('"endpoint_configuration": true', '"other": true'),
            valid.replace('"endpoint_configuration": true', ""),
            valid.replace('"iso_delivery": true', '"iso_delivery": false'),
            valid.replace('"iso_delivery": true', '"other": true'),
            valid.replace('"iso_delivery": true', ""),
        ):
            with self.subTest(message=message), self.assertRaises(ValueError):
                validate_public(
                    decode_marker(message, "PB053_PUBLIC_RESULT ", 1024 * 1024),
                    "fresh",
                    STIMULUS,
                )

    def test_serial_result_cardinality_and_shape(self):
        record = synthetic_guest()
        stages = record["stages"]
        valid = self.serial_fixture(record)
        self.assertTrue(
            guest_host.parse_result("boot\n" + valid + "\n", RUN_ID, "normal")["ok"]
        )
        for message in (
            "",
            valid + "\n" + valid,
            "PB053_GUEST_RESULT {bad",
            valid.replace("true", "false"),
            "PB053_GUEST_RESULT "
            + json.dumps({"ok": True, "kernel": guest_host.VERSION, "controllers": 2}),
        ):
            with self.subTest(message=message), self.assertRaises(ValueError):
                guest_host.parse_result(message, RUN_ID, "normal")

        def rejected(changed):
            with self.assertRaises(ValueError):
                guest_host.parse_result(
                    self.serial_fixture({**record, "stages": changed}), RUN_ID, "normal"
                )

        rejected([stage for stage in stages if stage.get("stage") != "public_retained"])
        rejected(stages + [stages[1]])
        rejected(
            [
                {**stage, "result": {**stage["result"], "state": "fresh"}}
                if stage.get("stage") == "public_retained"
                else stage
                for stage in stages
            ]
        )
        rejected(
            [
                {
                    **stage,
                    "result": {
                        **stage["result"],
                        "cases": {**stage["result"]["cases"], "iso_delivery": False},
                    },
                }
                if stage.get("stage") == "public_fresh2"
                else stage
                for stage in stages
            ]
        )
        rejected([stage for stage in stages if stage.get("stage") != "state_preserved"])
        rejected([stage for stage in stages if stage.get("process") != "monitor"])
        rejected([stage for stage in stages if stage.get("phase") != "retained"])

    def test_retained_raw_capture_must_match_guest_stage(self):
        serial = self.serial_fixture()
        self.assertTrue(guest_host.parse_result(serial, RUN_ID, "normal")["ok"])
        for changed in (
            serial.replace(
                json.dumps(authored_capture()),
                json.dumps(
                    authored_capture().replace(
                        '"raw_hex": "020000000000"', '"raw_hex": "030000000000"', 1
                    )
                ),
            ),
            serial.replace(
                '"sha256": "' + synthetic_guest()["stages"][-2]["sha256"] + '"',
                '"sha256": "' + "f" * 64 + '"',
                1,
            ),
            serial
            + json.dumps({"process_log": "monitor", "content": authored_capture()})
            + "\n",
        ):
            with self.subTest(changed=changed[-60:]), self.assertRaises(ValueError):
                guest_host.parse_result(changed, RUN_ID, "normal")


if __name__ == "__main__":
    unittest.main()
