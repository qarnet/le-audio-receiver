"""Ordinary process and real-file controls; no SDK/BSim execution claim."""

import copy
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from ascs_bsim_run import (  # noqa: E402
    ANCHOR,
    Cancel,
    ROLES,
    _regular_snapshot,
    freeze_sources,
    identity,
    inspect_warnings,
    make_jobs,
    owned_command,
    publish_cohort,
    publish_suite,
    public_main,
    read_job,
    seal_family,
    sdk_root,
    source_paths,
    verify_image,
    verify_cohort_result,
    worker_cohort,
)
from ascs_results import BUILD_PROFILE  # noqa: E402
from bluez_host_descendants import DescendantScope, _children  # noqa: E402

sys.path.insert(0, str(ROOT / "tests/unit/ascs_results"))
from test_ascs_results import ControlTrace  # noqa: E402
from ascs_results import load_inventory  # noqa: E402
from bsim_link_env import resolve_library_dirs  # noqa: E402


class RunnerTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="pb051-runner-test-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)

    def commands(self, programs):
        return {role: [sys.executable, "-c", programs[role]] for role in ROLES}

    def exercise(self, programs, *, deadline=None, diagnostic=None):
        with Cancel() as cancel:
            return worker_cohort(
                self.commands(programs),
                self.base,
                30,
                cancel,
                diagnostic=diagnostic,
                _test_deadline=deadline,
            )

    def test_three_real_concurrent_children(self):
        programs = {
            role: "import time; time.sleep(.3); print('child complete')"
            for role in ROLES
        }
        started = time.monotonic()
        detail = {}
        self.exercise(programs, diagnostic=detail)
        self.assertLess(time.monotonic() - started, 1.3)
        self.assertIsNone(detail["first_cause"])
        self.assertFalse(detail["timed_out"])
        self.assertEqual(set(detail["workers"]), set(ROLES))
        for role in ROLES:
            self.assertIn(
                b"child complete", (self.base / f"{role}-worker.log").read_bytes()
            )
            self.assertGreater(detail["workers"][role]["pid"], 0)
            self.assertGreater(detail["workers"][role]["start_ticks"], 0)
            self.assertEqual(detail["workers"][role]["observed_exit"], 0)
            self.assertEqual(detail["workers"][role]["cleanup_faults"], [])
            self.assertEqual(
                detail["workers"][role]["log_bytes"],
                (self.base / f"{role}-worker.log").stat().st_size,
            )

    def test_first_failure_terminates_remaining_children(self):
        programs = {
            "receiver": "import sys, time; time.sleep(.1); sys.exit(7)",
            "client": "import time; time.sleep(20)",
            "phy": "import time; time.sleep(20)",
        }
        started = time.monotonic()
        detail = {}
        with self.assertRaisesRegex(ValueError, "worker failed"):
            self.exercise(programs, diagnostic=detail)
        self.assertLess(time.monotonic() - started, 8)
        self.assertEqual(detail["first_cause"], "worker_failure")
        self.assertFalse(detail["timed_out"])
        self.assertEqual(detail["workers"]["receiver"]["observed_exit"], 7)

    def test_worker_failure_not_reclassified_as_late_cleanup_deadline(self):
        programs = {
            "receiver": "import sys,time;time.sleep(.1);sys.exit(4)",
            "client": (
                "import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);"
                "time.sleep(20)"
            ),
            "phy": "import time;time.sleep(20)",
        }
        detail = {}
        with self.assertRaisesRegex(ValueError, "worker failed"):
            self.exercise(programs, deadline=0.3, diagnostic=detail)
        self.assertEqual(detail["first_cause"], "worker_failure")
        self.assertFalse(detail["timed_out"])
        self.assertGreater(detail["ended_at"] - detail["started_at"], 0.3)

    def test_timeout_output_quota_and_spawn_failure(self):
        slow = {role: "import time; time.sleep(20)" for role in ROLES}
        detail = {}
        with self.assertRaisesRegex(ValueError, "deadline"):
            self.exercise(slow, deadline=0.3, diagnostic=detail)
        self.assertEqual(detail["first_cause"], "deadline")
        self.assertTrue(detail["timed_out"])
        self.assertGreater(detail["ended_at"] - detail["started_at"], 0.3)
        with tempfile.TemporaryDirectory(prefix="pb051-runner-quota-") as other:
            quota = {}
            with Cancel() as cancel, self.assertRaisesRegex(ValueError, "quota"):
                worker_cohort(
                    self.commands(
                        {
                            "receiver": "print('x' * 70000)",
                            "client": "import time; time.sleep(20)",
                            "phy": "import time; time.sleep(20)",
                        }
                    ),
                    other,
                    30,
                    cancel,
                    diagnostic=quota,
                )
            self.assertEqual(quota["first_cause"], "output_quota")
            self.assertFalse(quota["timed_out"])
            self.assertTrue(quota["workers"]["receiver"]["log_limit_exceeded"])
        with tempfile.TemporaryDirectory(prefix="pb051-runner-spawn-") as other:
            commands = self.commands(
                {role: "import time; time.sleep(20)" for role in ROLES}
            )
            commands["client"] = ["/nonexistent-pb051-worker"]
            spawn = {}
            with Cancel() as cancel, self.assertRaises(FileNotFoundError):
                worker_cohort(commands, other, 30, cancel, diagnostic=spawn)
            self.assertEqual(spawn["first_cause"], "spawn_or_identity")
            self.assertFalse(spawn["timed_out"])
            self.assertGreater(spawn["workers"]["receiver"]["pid"], 0)
            self.assertGreater(spawn["workers"]["receiver"]["start_ticks"], 0)
            self.assertIsNotNone(spawn["workers"]["receiver"]["observed_exit"])

    def test_repeated_real_signals_latch_and_clean(self):
        sender = subprocess.Popen(
            [
                sys.executable,
                "-c",
                "import os,signal,time; time.sleep(.2); os.kill(os.getppid(),signal.SIGINT); "
                "time.sleep(.1); os.kill(os.getppid(),signal.SIGTERM)",
            ],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            with Cancel() as cancel:
                detail = {}
                with self.assertRaisesRegex(ValueError, "cancelled"):
                    worker_cohort(
                        self.commands(
                            {role: "import time; time.sleep(20)" for role in ROLES}
                        ),
                        self.base,
                        30,
                        cancel,
                        diagnostic=detail,
                    )
                sender.wait(timeout=3)
                self.assertEqual(cancel.signal, signal.SIGINT)
                self.assertEqual(detail["first_cause"], "cancelled")
                self.assertEqual(detail["cancelled_signal"], signal.SIGINT)
                self.assertFalse(detail["timed_out"])
        finally:
            if sender.poll() is None:
                sender.kill()
            sender.wait(timeout=3)

    def test_owned_command_propagates_child_signal_and_retains_record(self):
        sender = subprocess.Popen(
            [
                sys.executable,
                "-c",
                "import os,signal,time;time.sleep(.2);os.kill(os.getppid(),signal.SIGINT);"
                "time.sleep(.1);os.kill(os.getppid(),signal.SIGTERM)",
            ],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            with Cancel() as cancel:
                with self.assertRaisesRegex(ValueError, "cancelled"):
                    owned_command(
                        [sys.executable, "-c", "import time;time.sleep(20)"],
                        self.base,
                        "held",
                        30,
                        65536,
                        cancel=cancel,
                    )
                sender.wait(timeout=3)
                self.assertEqual(cancel.signal, signal.SIGINT)
                record = json.loads((self.base / "held.json").read_text())
                self.assertEqual(record["cancelled_signal"], signal.SIGINT)
                self.assertFalse(record["ok"])
                with self.assertRaises(ValueError):
                    owned_command(
                        [sys.executable, "-c", "pass"],
                        self.base,
                        "not-launched",
                        30,
                        65536,
                        cancel=cancel,
                    )
                self.assertFalse((self.base / "not-launched.log").exists())
        finally:
            if sender.poll() is None:
                sender.kill()
            sender.wait(timeout=3)

    def test_root_scope_seal_catches_signal_outside_child_call(self):
        scope = DescendantScope()
        with Cancel() as cancel:
            with scope:
                cancel.reinstall()
            os.kill(os.getpid(), signal.SIGTERM)
            outcome = {
                "accepted": True,
                "errors": [],
                "cleanup_errors": [],
                "cancelled_signal": None,
            }
            publish_suite(self.base, outcome, scope, cancel)
            sealed = json.loads((self.base / "suite-record.json").read_text())
            self.assertFalse(sealed["accepted"])
            self.assertEqual(sealed["cancelled_signal"], signal.SIGTERM)
            self.assertTrue(sealed["scope"]["ok"])
            os.kill(os.getpid(), signal.SIGINT)
            self.assertEqual(cancel.signal, signal.SIGTERM)

    def test_cohort_late_publication_signal_fails_terminal_record(self):
        class SignalDuringEncoding(dict):
            fired = False

            def items(self):
                if not self.fired:
                    self.fired = True
                    os.kill(os.getpid(), signal.SIGTERM)
                return super().items()

        scope = DescendantScope()
        with Cancel() as cancel:
            with scope:
                cancel.reinstall()
            outcome = SignalDuringEncoding(
                schema_version=1,
                accepted=True,
                family="control_frame_validation",
                run_id="ab" * 16,
                images={},
                error=None,
                errors=[],
                cancelled_signal=None,
                timed_out=False,
                worker={"first_cause": None},
                scope=scope.record,
                claim="owned-cohort-only; protocol checker separately required",
            )
            publish_cohort(self.base, outcome, cancel)
            self.assertTrue(outcome.fired)
            sealed = json.loads((self.base / "cohort-result.json").read_text())
            self.assertFalse(sealed["accepted"])
            self.assertEqual(sealed["cancelled_signal"], signal.SIGTERM)
            self.assertEqual(sealed["worker"]["first_cause"], "cancelled")
            self.assertIn("cancelled by signal 15", sealed["error"])
            self.assertTrue(sealed["scope"]["ok"])

    def test_scope_contains_detached_grandchild_from_failed_worker(self):
        # Real subprocess, detached grandchild, and production DescendantScope.
        spawned = (
            "import subprocess,sys,time; "
            "subprocess.Popen([sys.executable,'-c','import time;time.sleep(20)'],"
            "start_new_session=True,stdin=subprocess.DEVNULL,"
            "stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);time.sleep(20)"
        )
        commands = self.commands(
            {
                "receiver": spawned,
                "client": "import sys,time; time.sleep(.3);sys.exit(3)",
                "phy": spawned,
            }
        )
        scope = DescendantScope()
        with self.assertRaisesRegex(ValueError, "Descendant scope failed"):
            with scope:
                with Cancel() as cancel:
                    with self.assertRaisesRegex(ValueError, "worker failed"):
                        worker_cohort(commands, self.base, 30, cancel)
        self.assertFalse(scope.record["ok"])
        self.assertTrue(scope.record["unexpected_live_descendants"])
        self.assertEqual(scope.record["errors"], [])
        self.assertEqual(scope.record["saved_flag"], scope.record["restored_flag"])
        self.assertEqual(_children(scope.record["owner_pid"]), {})
        self.assertTrue(scope.record["actions"])
        self.assertTrue(any("signal" in action for action in scope.record["actions"]))

    def test_exited_worker_inherited_pipe_has_bounded_failure(self):
        hold = (
            "import subprocess,sys,time;"
            "subprocess.Popen([sys.executable,'-c',"
            "'import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(20)'],"
            "start_new_session=True,stdin=subprocess.DEVNULL,"
            "stdout=sys.stdout,stderr=sys.stderr);time.sleep(.2)"
        )
        programs = {
            "receiver": hold,
            "client": "import time;time.sleep(.4)",
            "phy": "import time;time.sleep(.4)",
        }
        scope = DescendantScope()
        started = time.monotonic()
        detail = {}
        with self.assertRaisesRegex(ValueError, "Descendant scope failed"):
            with scope:
                unrelated = subprocess.Popen(
                    [sys.executable, "-c", "import time;time.sleep(20)"],
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                try:
                    with Cancel() as cancel:
                        with self.assertRaisesRegex(
                            ValueError, "inherited pipe remained"
                        ):
                            worker_cohort(
                                self.commands(programs),
                                self.base,
                                30,
                                cancel,
                                diagnostic=detail,
                            )
                    self.assertIsNone(
                        unrelated.poll(), "unrelated sibling was signalled"
                    )
                finally:
                    if unrelated.poll() is None:
                        unrelated.terminate()
                    unrelated.wait(timeout=3)
        self.assertLess(time.monotonic() - started, 9)
        self.assertEqual(detail["first_cause"], "final_cleanup")
        self.assertFalse(detail["timed_out"])
        self.assertEqual(detail["workers"]["receiver"]["observed_exit"], 0)
        self.assertTrue(scope.record["unexpected_live_descendants"])
        self.assertEqual(scope.record["errors"], [])
        self.assertTrue(
            any(
                action.get("signal") == signal.SIGKILL
                for action in scope.record["actions"]
            )
        )

    def test_job_schema_paths_digest_and_image_drift(self):
        # Tiny ELF-shaped fixture is metadata-only; never executed as firmware.
        images_dir = self.base / "images"
        images_dir.mkdir()
        area = self.base / "families"
        area.mkdir()
        area = area / "control_frame_validation"
        bin_dir = self.base / "bsim" / "bin"
        bin_dir.mkdir(parents=True)
        old = os.environ.get("BSIM_OUT_PATH")
        os.environ["BSIM_OUT_PATH"] = str(bin_dir.parent)
        self.addCleanup(
            lambda: (
                os.environ.pop("BSIM_OUT_PATH", None)
                if old is None
                else os.environ.__setitem__("BSIM_OUT_PATH", old)
            )
        )
        images = {}
        for role in ROLES:
            raw = bytearray(64 if role == "phy" else 52)
            raw[:6] = b"\x7fELF\x02\x01" if role == "phy" else b"\x7fELF\x01\x01"
            raw[18:20] = b"\x3e\x00" if role == "phy" else b"\x03\x00"
            path = images_dir / f"{role}.elf"
            path.write_bytes(raw)
            images[role] = identity(raw, path)
            verify_image(path, images[role], role)
        location, digest = make_jobs(
            area, "control_frame_validation", "ab" * 16, images, bin_dir, 30
        )
        job = read_job(location, digest, "cohort")
        self.assertEqual(job["kind"], "cohort")
        with self.assertRaises((OSError, ValueError)):
            seal_family(job, {"ok": True})  # worker records must exist
        self.assertFalse((area / "family-execution-record.json").exists())
        with self.assertRaises(ValueError):
            read_job(location, "0" * 64, "cohort")
        valid_job = json.loads(location.read_text())
        altered = copy.deepcopy(valid_job)
        altered["timeout"] = 0
        location.write_text(json.dumps(altered))
        from hashlib import sha256

        with self.assertRaises(ValueError):
            read_job(location, sha256(location.read_bytes()).hexdigest(), "cohort")
        for key, value in (
            ("schema_version", True),
            ("root", False),
            ("timeout", True),
            ("cwd", 7),
            ("images", None),
        ):
            variant = copy.deepcopy(valid_job)
            variant[key] = value
            location.write_text(json.dumps(variant))
            with self.subTest(field=key), self.assertRaises((ValueError, TypeError)):
                read_job(location, sha256(location.read_bytes()).hexdigest(), "cohort")
        images["phy"]["sha256"] = "1" * 64
        with self.assertRaises(ValueError):
            verify_image(images_dir / "phy.elf", images["phy"], "phy")

    def test_real_actor_workers_seal_synthetic_trace_only(self):
        # Host-compiled stand-ins execute exact role argv under real run_owned,
        # never simulator firmware. Encoded traffic is independently authored.
        source = self.base / "synthetic_actor.c"
        source.write_text(r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
int main(int argc, char **argv) {
    const char *role = "phy";
    const char *dir = getenv("PB051_SYNTHETIC_ROOT");
    char path[1024];
    char buffer[4096];
    size_t n;
    struct timespec pause = {0, 400000000};
    if (!dir) return 3;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d=0")) role = "receiver";
        if (!strcmp(argv[i], "-d=1")) role = "client";
    }
    if (snprintf(path, sizeof(path), "%s/%s.trace", dir, role) >= (int)sizeof(path)) return 4;
    FILE *in = fopen(path, "rb");
    if (!in) return 5;
    nanosleep(&pause, NULL);
    const char *fail_role = getenv("PB051_SYNTHETIC_FAIL_ROLE");
    if (fail_role && !strcmp(fail_role, role)) { fclose(in); return 7; }
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, n, stdout) != n) return 6;
    }
    if (ferror(in)) return 7;
    fclose(in);
    return 0;
}
""")
        binary = self.base / "synthetic_actor32"
        libc32, gcc32 = resolve_library_dirs("gcc")
        env = os.environ.copy()
        env["NIX_LDFLAGS"] = f"-L{libc32} -L{gcc32} " + env.get("NIX_LDFLAGS", "")
        compiled = subprocess.run(
            ["gcc", "-m32", "-Wall", "-Werror", str(source), "-o", str(binary)],
            capture_output=True,
            check=False,
            env=env,
        )
        self.assertEqual(compiled.returncode, 0, compiled.stderr.decode())
        self.assertFalse(compiled.stderr, compiled.stderr.decode())
        native = self.base / "synthetic_actor64"
        native_env = os.environ.copy()
        native_env.pop("NIX_LDFLAGS", None)
        native_build = subprocess.run(
            ["gcc", "-m64", "-Wall", "-Werror", str(source), "-o", str(native)],
            capture_output=True,
            check=False,
            env=native_env,
        )
        self.assertEqual(native_build.returncode, 0, native_build.stderr.decode())
        self.assertFalse(native_build.stderr, native_build.stderr.decode())
        images_dir = self.base / "images"
        images_dir.mkdir()
        images = {}
        for role in ROLES:
            image = images_dir / f"{role}.elf"
            image.write_bytes((native if role == "phy" else binary).read_bytes())
            image.chmod(0o555)
            images[role] = identity(image.read_bytes(), image)
            verify_image(image, images[role], role)
        client, receiver = ControlTrace(
            load_inventory((ROOT / "tests/ascs_bsim/cases.json").read_bytes(), ANCHOR)
        ).build()
        (self.base / "client.trace").write_bytes(client)
        (self.base / "receiver.trace").write_bytes(receiver)
        (self.base / "phy.trace").write_bytes(b"")
        bsim = self.base / "bsim"
        (bsim / "bin").mkdir(parents=True)
        family_root = self.base / "families"
        family_root.mkdir()
        family_root = family_root / "control_frame_validation"
        location, job_hash = make_jobs(
            family_root, "control_frame_validation", "ab" * 16, images, bsim / "bin", 30
        )
        previous_bsim = os.environ.get("BSIM_OUT_PATH")
        previous_fixture = os.environ.get("PB051_SYNTHETIC_ROOT")
        os.environ["BSIM_OUT_PATH"] = str(bsim)
        os.environ["PB051_SYNTHETIC_ROOT"] = str(self.base)
        try:
            from ascs_bsim_run import cohort

            self.assertEqual(cohort(location, job_hash), 0)
            accepted_cohort = json.loads(
                (family_root / "cohort-result.json").read_text()
            )
            self.assertTrue(accepted_cohort["accepted"])
            self.assertIsNone(accepted_cohort["worker"]["first_cause"])
            self.assertTrue(accepted_cohort["scope"]["ok"])
            self.assertEqual(
                accepted_cohort["scope"]["saved_flag"],
                accepted_cohort["scope"]["restored_flag"],
            )
            self.assertFalse(accepted_cohort["scope"]["unexpected_live_descendants"])
            self.assertEqual(
                accepted_cohort["claim"],
                "owned-cohort-only; protocol checker separately required",
            )
            self.assertTrue(
                verify_cohort_result(
                    family_root, "control_frame_validation", "ab" * 16, images
                )["accepted"]
            )
            failed_root = self.base / "families/metadata_length_validation"
            failed_job, failed_sha = make_jobs(
                failed_root,
                "metadata_length_validation",
                "cd" * 16,
                images,
                bsim / "bin",
                30,
            )
            os.environ["PB051_SYNTHETIC_FAIL_ROLE"] = "client"
            try:
                with self.assertRaisesRegex(ValueError, "worker failed"):
                    cohort(failed_job, failed_sha)
            finally:
                os.environ.pop("PB051_SYNTHETIC_FAIL_ROLE", None)
            rejected = json.loads((failed_root / "cohort-result.json").read_text())
            self.assertFalse(rejected["accepted"])
            self.assertEqual(rejected["worker"]["first_cause"], "worker_failure")
            self.assertFalse(rejected["timed_out"])
            self.assertIsNone(rejected["cancelled_signal"])
            self.assertTrue(rejected["scope"]["ok"])
            self.assertEqual(
                rejected["scope"]["saved_flag"], rejected["scope"]["restored_flag"]
            )
            self.assertFalse(rejected["scope"]["unexpected_live_descendants"])
            self.assertEqual(
                rejected["worker"]["workers"]["client"]["observed_exit"], 1
            )
            self.assertFalse((failed_root / "family-execution-record.json").exists())
            with self.assertRaises(ValueError):
                verify_cohort_result(
                    failed_root, "metadata_length_validation", "cd" * 16, images
                )
        finally:
            for name, previous in (
                ("BSIM_OUT_PATH", previous_bsim),
                ("PB051_SYNTHETIC_ROOT", previous_fixture),
            ):
                if previous is None:
                    os.environ.pop(name, None)
                else:
                    os.environ[name] = previous
        sealed = json.loads((family_root / "family-execution-record.json").read_text())
        self.assertEqual(set(sealed["participants"]), set(ROLES))
        self.assertTrue(sealed["scope"]["ok"])

    def test_public_preflight_no_output_on_bad_inputs(self):
        root = self.base / "new-output"
        old_case = os.environ.get("ASCS_CASE")
        os.environ["ASCS_CASE"] = ""
        try:
            with self.assertRaises(SystemExit):
                public_main(
                    ["--output", str(root), "--expected-inventory-sha256", ANCHOR]
                )
            self.assertFalse(root.exists())
        finally:
            if old_case is None:
                os.environ.pop("ASCS_CASE", None)
            else:
                os.environ["ASCS_CASE"] = old_case
        for args in (["--expected-inventory-sha256", "0" * 64],):
            with self.assertRaises(SystemExit):
                public_main(["--output", str(root), *args])
            self.assertFalse(root.exists())
        old = os.environ.pop("ZEPHYR_BASE", None)
        try:
            with self.assertRaises(ValueError):
                public_main(
                    ["--output", str(root), "--expected-inventory-sha256", ANCHOR]
                )
            self.assertFalse(root.exists())
        finally:
            if old is not None:
                os.environ["ZEPHYR_BASE"] = old
        fake_sdk = self.base / "v3.4.1"
        (fake_sdk / "zephyr").mkdir(parents=True)
        (fake_sdk / "nrf").mkdir()
        os.environ["ZEPHYR_BASE"] = str(fake_sdk / "zephyr")
        try:
            self.assertEqual(sdk_root(), fake_sdk)
            root.mkdir()
            with self.assertRaises(ValueError):
                public_main(
                    ["--output", str(root), "--expected-inventory-sha256", ANCHOR]
                )
            alias = self.base / "alias"
            alias.symlink_to(root, target_is_directory=True)
            with self.assertRaises(ValueError):
                public_main(
                    ["--output", str(alias), "--expected-inventory-sha256", ANCHOR]
                )
            os.environ["ZEPHYR_BASE"] = str(self.base / "v3.3.0/zephyr")
            new = self.base / "fresh-output"
            with self.assertRaises(ValueError):
                public_main(
                    ["--output", str(new), "--expected-inventory-sha256", ANCHOR]
                )
            self.assertFalse(new.exists())
        finally:
            if old is None:
                os.environ.pop("ZEPHYR_BASE", None)
            else:
                os.environ["ZEPHYR_BASE"] = old
        self.assertFalse((root / "suite-record.json").exists())

    def test_runtime_identity_drift_detected_at_boundaries(self):
        # verify_runtime_identity rehashes the recorded runtime libraries at
        # readiness/each cohort boundary/final; ordinary drift must fail
        # against the exact recorded identity without any binary patching.
        from ascs_bsim_run import verify_runtime_identity

        bsim = self.base / "bsim"
        (bsim / "lib").mkdir(parents=True)
        result = {"runtime": {}}
        libs = {}
        for index, name in enumerate(
            (
                "lib_2G4Channel_NtNcable.so",
                "lib_2G4Modem_Magic.so",
                "libCryptov1.so",
            )
        ):
            path = bsim / "lib" / name
            path.write_bytes(bytes((index + 1,)) * 64)
            raw = path.read_bytes()
            result["runtime"][name] = identity(raw, path.resolve())
            libs[name] = path
        self.assertTrue(verify_runtime_identity(result, bsim, "ready"))
        # Ordinary replacement between boundaries: content rewrite.
        libs["lib_2G4Modem_Magic.so"].write_bytes(b"drifted" * 9)
        with self.assertRaisesRegex(ValueError, "drifted: lib_2G4Modem_Magic"):
            verify_runtime_identity(result, bsim, "post-ready")
        # Between-cohort boundary with same-size but different content.
        libs["lib_2G4Modem_Magic.so"].write_bytes(b"drifter" * 9)
        with self.assertRaisesRegex(ValueError, "post-ready"):
            verify_runtime_identity(result, bsim, "post-ready")
        # Restore; resize drift on another library fails the final boundary.
        libs["lib_2G4Modem_Magic.so"].write_bytes(bytes((2,)) * 64)
        self.assertTrue(verify_runtime_identity(result, bsim, "cohort:meta:pre"))
        libs["lib_2G4Channel_NtNcable.so"].write_bytes(bytes((1,)) * 63)
        with self.assertRaisesRegex(ValueError, "cohort:control:final"):
            verify_runtime_identity(result, bsim, "cohort:control:final")
        # Missing recorded entry entirely: population check rejects first.
        result["runtime"].pop("lib_2G4Channel_NtNcable.so")
        with self.assertRaisesRegex(
            ValueError, "final: runtime identity population missing or extra"
        ):
            verify_runtime_identity(result, bsim, "final")
        result["runtime"]["lib_2G4Channel_NtNcable.so"] = libs[
            "lib_2G4Channel_NtNcable.so"
        ].read_bytes() and identity(
            libs["lib_2G4Channel_NtNcable.so"].read_bytes(),
            bsim / "lib" / "lib_2G4Channel_NtNcable.so",
        )
        # Path move to another root fails strict path comparison.
        moved = self.base / "bsim2"
        (moved / "lib").mkdir(parents=True)
        for name in (
            "lib_2G4Channel_NtNcable.so",
            "lib_2G4Modem_Magic.so",
            "libCryptov1.so",
        ):
            shutil.copyfile(bsim / "lib" / name, moved / "lib" / name)
        with self.assertRaisesRegex(ValueError, "drifted: lib_2G4Channel_NtNcable.so"):
            verify_runtime_identity(result, moved, "other-root")

    def test_phy_argv_carries_nodump_and_old_rejected(self):
        # Exact fixed PHY argv must include -nodump; the job/actor contract is
        # derived from the same helper, so a stub job lacks it only by direct
        # construction and must never pass the argv equality check.
        images_dir = self.base / "images"
        images_dir.mkdir()
        area = self.base / "families" / "control_frame_validation"
        area.parent.mkdir(parents=True)
        bin_dir = self.base / "bsim" / "bin"
        bin_dir.mkdir(parents=True)
        old = os.environ.get("BSIM_OUT_PATH")
        os.environ["BSIM_OUT_PATH"] = str(bin_dir.parent)
        self.addCleanup(
            lambda: (
                os.environ.pop("BSIM_OUT_PATH", None)
                if old is None
                else os.environ.__setitem__("BSIM_OUT_PATH", old)
            )
        )
        images = {}
        for role in ROLES:
            raw = bytearray(64 if role == "phy" else 52)
            raw[:6] = b"\x7fELF\x02\x01" if role == "phy" else b"\x7fELF\x01\x01"
            raw[18:20] = b"\x3e\x00" if role == "phy" else b"\x03\x00"
            path = images_dir / f"{role}.elf"
            path.write_bytes(raw)
            images[role] = identity(raw, path)
        location, digest = make_jobs(
            area, "control_frame_validation", "ab" * 16, images, bin_dir, 30
        )
        job = json.loads(location.read_text())
        phy_job = job["jobs"]["phy"]
        phy_bare = json.loads((area / "phy-job.json").read_text())
        self.assertIn("-nodump", phy_bare["argv"], "PHY job argv must carry -nodump")
        stub = dict(phy_bare, argv=[a for a in phy_bare["argv"] if a != "-nodump"])
        written = area / "phy-job-old.json"
        written.write_text(json.dumps(stub))
        from hashlib import sha256

        with self.assertRaisesRegex(ValueError, "argv"):
            read_job(written, sha256(written.read_bytes()).hexdigest(), "actor")

    def test_public_preflight_rejects_wrong_components_root(self):
        # Public preflight must fail before any output root creation when
        # BSIM_COMPONENTS_PATH differs from the pinned installed components.
        fake_sdk = self.base / "v3.4.1"
        (fake_sdk / "zephyr/cmake/modules").mkdir(parents=True)
        (fake_sdk / "nrf").mkdir()
        (fake_sdk / "tools/bsim/bin").mkdir(parents=True)
        (fake_sdk / "tools/bsim/components").mkdir()
        installed = fake_sdk / "tools/bsim/components"
        wrong = self.base / "babblesim_alt"
        wrong.mkdir()
        old_bsim = os.environ.get("BSIM_OUT_PATH")
        old_comp = os.environ.get("BSIM_COMPONENTS_PATH")
        old_base = os.environ.get("ZEPHYR_BASE")
        os.environ["ZEPHYR_BASE"] = str(fake_sdk / "zephyr")
        os.environ["BSIM_OUT_PATH"] = str(fake_sdk / "tools/bsim")
        root = self.base / "never-created"
        try:
            # Wrong absolute components root is rejected before mkdir.
            os.environ["BSIM_COMPONENTS_PATH"] = str(wrong)
            with self.assertRaisesRegex(ValueError, "BSIM_COMPONENTS_PATH"):
                public_main(
                    ["--output", str(root), "--expected-inventory-sha256", ANCHOR]
                )
            self.assertFalse(root.exists())
            # Relative or unset components path also fails the pin.
            os.environ.pop("BSIM_COMPONENTS_PATH", None)
            with self.assertRaisesRegex(ValueError, "BSIM_COMPONENTS_PATH"):
                public_main(
                    ["--output", str(root), "--expected-inventory-sha256", ANCHOR]
                )
            self.assertFalse(root.exists())
            # Valid direct components path passes the pin and proceeds to the
            # deeper preflight/executable stages (root then created, suite
            # still not accepted by the end); the failure no longer mentions
            # the components pin.
            os.environ["BSIM_COMPONENTS_PATH"] = str(installed)
            public_main(["--output", str(root), "--expected-inventory-sha256", ANCHOR])
            record = json.loads((root / "suite-record.json").read_text())
            self.assertFalse(record["accepted"])
            self.assertFalse(
                any("BSIM_COMPONENTS_PATH" in error for error in record["errors"]),
                "correct components root must pass this pin",
            )
            self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        finally:
            for name, previous in (
                ("BSIM_OUT_PATH", old_bsim),
                ("BSIM_COMPONENTS_PATH", old_comp),
                ("ZEPHYR_BASE", old_base),
            ):
                if previous is None:
                    os.environ.pop(name, None)
                else:
                    os.environ[name] = previous

    def test_component_headers_snapped_and_drift_detected(self):
        # The 15 consumed component headers plus FindBabbleSim.cmake enter the
        # frozen source population; real-file content drift must be detected.
        fake_sdk = self.base / "v3.4.1"
        (fake_sdk / "zephyr/cmake/modules").mkdir(parents=True)
        (fake_sdk / "nrf").mkdir(parents=True)
        (fake_sdk / "zephyr/subsys/bluetooth/audio").mkdir(parents=True)
        for name in ("ascs_internal.h", "audio_internal.h"):
            (fake_sdk / "zephyr/subsys/bluetooth/audio" / name).write_text("x")
        (fake_sdk / "nrf/cmake").mkdir(parents=True)
        (fake_sdk / "nrf/cmake/device_support.cmake").write_text("#\n")
        components = fake_sdk / "tools/bsim/components"
        (components / "libUtilv1/src").mkdir(parents=True)
        (components / "libPhyComv1/src").mkdir(parents=True)
        (components / "libUtilv1/src/bs_types.h").write_text("/* header */\n")
        (components / "libPhyComv1/src/bs_pc_base.h").write_text("/* pc */\n")
        (components / "libUtilv1/src/bs_cmd_line.32.d").write_text("junk\n")
        paths = source_paths(fake_sdk)
        self.assertIn(
            components / "libUtilv1/src/bs_types.h",
            paths,
            "expected consumed component header missing from snapshot set",
        )
        self.assertIn(components / "libPhyComv1/src/bs_pc_base.h", paths)
        self.assertIn(fake_sdk / "zephyr/cmake/modules/FindBabbleSim.cmake", paths)
        # Only .h files are consumed; build artifacts like .32.d excluded.
        self.assertNotIn(components / "libUtilv1/src/bs_cmd_line.32.d", paths)
        # Fixture headers all snapped; drift is caught by freeze comparison.
        snapshots = self.base / "snapshots"
        snapshots.mkdir()
        tracked = [p for p in paths if "components" in p.parts]
        self.assertEqual(len(tracked), 2, "fixture must keep header population bounded")
        before = freeze_sources(tracked, snapshots)
        self.assertEqual(len(before), 2)
        (components / "libUtilv1/src/bs_types.h").write_text("/* altered */\n")
        # Copy comparison trips first on the second call (fresh copies compare
        # against the pre-existing snapshot file), which still proves drift.
        with self.assertRaises(ValueError):
            freeze_sources(tracked, snapshots, before)
        (components / "libUtilv1/src/bs_types.h").write_text("/* header */\n")
        self.assertEqual(freeze_sources(tracked, snapshots, before), before)

    def test_execute_real_lifecycle_runtime_boundaries(self):
        """Drive the real execute() lifecycle against an isolated fake SDK.

        Only external SDK/build/tool/checker boundaries are replaced with
        ordinary command fixtures; verify_runtime_identity itself is never
        mocked. Actual library files are read and mutated; the actual execute
        finally runs. Ready-stage and cohort-stage hooks mutate real
        libCryptov1 bytes through the real read path.
        """
        import unittest.mock as mock

        import ascs_bsim_run as runner

        fake_sdk = self.base / "v3.4.1"
        (fake_sdk / "zephyr/cmake/modules").mkdir(parents=True)
        (fake_sdk / "nrf").mkdir()
        (fake_sdk / "zephyr/subsys/bluetooth/audio").mkdir(parents=True)
        for name in ("ascs_internal.h", "audio_internal.h"):
            (fake_sdk / "zephyr/subsys/bluetooth/audio" / name).write_text("h\n")
        (fake_sdk / "nrf/cmake").mkdir(parents=True)
        (fake_sdk / "nrf/cmake/device_support.cmake").write_text("#\n")
        for rel in (
            "share/sysbuild/CMakeLists.txt",
            "share/sysbuild/Kconfig",
            "boards/native/native_sim/board.cmake",
            "boards/native/native_sim/Kconfig.native_sim",
        ):
            path = fake_sdk / "zephyr" / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("#\n")
        for rel in (
            "subsys/bluetooth/audio/audio.c",
            "subsys/bluetooth/audio/ascs.c",
            "subsys/bluetooth/audio/bap_stream.c",
            "subsys/bluetooth/audio/bap_unicast_client.c",
            "subsys/bluetooth/audio/codec.c",
            "subsys/bluetooth/host/gatt.c",
            "include/zephyr/bluetooth/audio/audio.h",
            "include/zephyr/bluetooth/audio/bap.h",
            "include/zephyr/bluetooth/audio/pacs.h",
            "include/zephyr/bluetooth/gatt.h",
            "include/zephyr/bluetooth/iso.h",
            "include/zephyr/bluetooth/conn.h",
        ):
            path = fake_sdk / "zephyr" / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("#\n")
        (fake_sdk / "zephyr/cmake/modules/FindBabbleSim.cmake").write_text("#\n")
        # Minimal tracked-clean fake git repos are produced by faking the
        # external git invocations, so no real git init is needed.

        bsim = self.base / "bsim"
        bsim.mkdir()
        (bsim / "bin").mkdir()
        (bsim / "lib").mkdir()
        names = (
            "lib_2G4Channel_NtNcable.so",
            "lib_2G4Modem_Magic.so",
            "libCryptov1.so",
        )
        for index, name in enumerate(names):
            (bsim / "lib" / name).write_bytes(bytes((index + 1,)) * 32)

        # ELF-shaped peer images satisfy copy_image/ABI checks.
        images_dir = self.base / "images-fixture"
        images_dir.mkdir()
        peer = images_dir / "peer.elf"
        peer_bytes = bytearray(52)
        peer_bytes[:6] = b"\x7fELF\x01\x01"
        peer_bytes[18:20] = b"\x03\x00"
        peer.write_bytes(bytes(peer_bytes))
        phy = images_dir / "phy.elf"
        phy_bytes = bytearray(64)
        phy_bytes[:6] = b"\x7fELF\x02\x01"
        phy_bytes[18:20] = b"\x3e\x00"
        phy.write_bytes(bytes(phy_bytes))
        (bsim / "bin/bs_2G4_phy_v1").write_bytes(bytes(phy_bytes))
        (bsim / "bin/bs_crypto_probe").write_bytes(bytes(phy_bytes))

        class Hooks:
            phase = None

        def fake_owned_command(
            argv, root, name, timeout, cap, *, cwd=None, cancel=None
        ):
            if cancel is not None:
                cancel.check()
            log = Path(root) / f"{name}.log"
            if name == "cmake":
                # Nothing must read the log: emit exact approved warnings.
                role = "receiver" if "receiver" in str(Path(root).name) else "client"
                approved = BUILD_PROFILE[f"{role}_experimental"]
                log.write_text(
                    "\n".join("warning: " + t for t in approved)
                    + f"\nCMake Warning at {fake_sdk}/nrf/cmake/device_support.cmake:34 (message):\n"
                    "  SoC native is not supported by this release.\n"
                )
                # Bounded build artifacts the real runner reads afterwards;
                # owned_command receives root=build/<role> and the real
                # runner reads <root>/<role>/CMakeFiles/... plus
                # <root>/zephyr/zephyr.exe, so emit that exact tree.
                role_dir = Path(root) / role
                role_dir.mkdir(parents=True, exist_ok=True)
                configure = role_dir / "CMakeFiles/CMakeConfigureLog.yaml"
                configure.parent.mkdir(parents=True, exist_ok=True)
                configure.write_text(
                    'buildResult:\n  variable: "C_COMPILER_CHECK"\n  exitCode: 0\n'
                )
                (role_dir / "zephyr/.config").parent.mkdir(parents=True, exist_ok=True)
                config = (
                    "CONFIG_BT_LL_SW_SPLIT=y\n"
                    "CONFIG_COVERAGE=y\n"
                    "CONFIG_ASSERT=y\n"
                    "CONFIG_COMPILER_WARNINGS_AS_ERRORS=y\n"
                    + (
                        "CONFIG_BT_CTLR_PERIPHERAL_ISO=y\n"
                        if role == "receiver"
                        else "CONFIG_BT_CTLR_CENTRAL_ISO=y\n"
                    )
                )
                (role_dir / "zephyr/.config").write_text(config)
                exe = Path(root) / "zephyr/zephyr.exe"
                exe.parent.mkdir(parents=True, exist_ok=True)
                raw = bytearray(52)
                raw[:6] = b"\x7fELF\x01\x01"
                raw[18:20] = b"\x03\x00"
                exe.write_bytes(bytes(raw))
            elif name == "ninja":
                log.write_text("[100/413] Building C object error.c.obj\n")
            elif name == "runtime-ready":
                log.write_text("runtime ready\n")
                if Hooks.phase == "ready":
                    victim = bsim / "lib" / "libCryptov1.so"
                    victim.write_bytes(b"injected" * 4)
                    raise ValueError("authored ready-stage original failure")
                if Hooks.phase == "vanish":
                    (bsim / "lib" / "libCryptov1.so").unlink()
                    raise ValueError("authored vanish-stage original failure")
            elif name == "cohort":
                log.write_text("cohort ok\n")
                if Hooks.phase == "cohort":
                    victim = bsim / "lib" / "libCryptov1.so"
                    victim.write_bytes(b"tampered" * 4)
            else:
                log.write_text(f"{name} ok\n")
            record = {
                "schema_version": 1,
                "argv": list(argv),
                "pid": 4242,
                "start_time": 1.0,
                "end_time": 2.0,
                "returncode": 0,
                "ok": True,
                "timed_out": False,
                "cancelled_signal": None,
                "log_limit_exceeded": False,
                "bytes_logged": log.stat().st_size,
                "log_sha256": hashlib.sha256(log.read_bytes()).hexdigest(),
                "cleanup_errors": [],
                "error": None,
                "descendant_cleanup_required": False,
            }
            import json

            json_text = json.dumps(record)
            (Path(root) / f"{name}.json").write_text(json_text)
            if cancel is not None:
                cancel.latch(record["cancelled_signal"])
                cancel.check()
            return record

        def fake_sdk_identity(root, sdk, cancel=None, suffix=""):
            return {
                "zephyr": "33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6",
                "nrf": "b20f8619ba9a5530f8c34b0a130d829947cfe55d",
            }

        def fake_which(tool):
            return str(self.base / f"tool-{tool}")

        for tool in ("cmake", "ninja", "gcc"):
            (self.base / f"tool-{tool}").write_bytes(b"tool\x00el\x00data" * 10)

        def fake_image_abi(raw, role):
            return None

        # freeze_sources must see the real file population: fake SDK paths
        # exist above; check population is bounded and files present.
        old_env = dict(
            BSIM_OUT_PATH=os.environ.get("BSIM_OUT_PATH"),
            ZEPHYR_BASE=os.environ.get("ZEPHYR_BASE"),
        )
        os.environ["BSIM_OUT_PATH"] = str(bsim)
        os.environ["ZEPHYR_BASE"] = str(fake_sdk / "zephyr")

        def fresh_root(name):
            root = self.base / name
            root.mkdir()
            (root / "families").mkdir(mode=0o700)
            return root

        failures = {}

        def run_execute(phase):
            Hooks.phase = phase
            root = fresh_root(f"run-root-{phase}")
            outcome = None
            try:
                with (
                    mock.patch.object(runner, "owned_command", fake_owned_command),
                    mock.patch.object(runner, "sdk_identity", fake_sdk_identity),
                    mock.patch.object(runner.shutil, "which", fake_which),
                ):
                    outcome = runner.execute(root, 30, runner.Cancel(), fake_sdk)
            finally:
                Hooks.phase = None
            if outcome is None:
                self.fail(f"{phase}: execute raised without outcome")
            return outcome

        baseline = run_execute("plain")
        # First family proceeds through the whole cohort+checker chain but has
        # no real encoded traffic, so acceptance stops at the verdict without
        # inventing success; the actual boundary stages all pass.
        self.assertFalse(baseline["accepted"])
        self.assertTrue(baseline["runtime"]), "runtime identities captured"
        self.assertFalse(
            any(
                "runtime" in error and "drift" in error for error in baseline["errors"]
            ),
            baseline["errors"],
        )
        self.assertFalse(
            any(
                error.startswith("post-run runtime integrity")
                for error in baseline["errors"]
            ),
            baseline["errors"],
        )
        failures["baseline"] = list(baseline["errors"])
        # Restore pristine libraries between phases: real boundary must be the
        # only difference, not residual mutation.
        if (bsim / "lib" / "libCryptov1.so").read_bytes() != bytes((3,)) * 32:
            (bsim / "lib" / "libCryptov1.so").write_bytes(bytes((3,)) * 32)

        # Ready-stage hook authoring: the exact original failure and distinct
        # post-run entry both land in errors, in capture order.
        ready_outcome = run_execute("ready")
        self.assertFalse(ready_outcome["accepted"])
        self.assertTrue(
            any("/test_execute_runtime" in error for error in ready_outcome["errors"])
            or any(
                "authored ready-stage original failure" in error
                for error in ready_outcome["errors"]
            ),
            ready_outcome["errors"],
        )
        self.assertTrue(
            any(
                entry.startswith("post-run runtime integrity: ValueError:")
                and "runtime-ready" not in entry.split(": ", 1)[-1][:0]
                and "drifted: libCryptov1" in entry
                for entry in ready_outcome["errors"]
            ),
            ready_outcome["errors"],
        )
        self.assertLess(
            ready_outcome["errors"].index(
                next(
                    entry
                    for entry in ready_outcome["errors"]
                    if "authored ready-stage original failure" in entry
                )
            ),
            ready_outcome["errors"].index(
                next(
                    entry
                    for entry in ready_outcome["errors"]
                    if entry.startswith("post-run runtime integrity")
                )
            ),
            "original failure must remain before post-run runtime entry",
        )
        failures["ready"] = list(ready_outcome["errors"])
        (bsim / "lib" / "libCryptov1.so").write_bytes(bytes((3,)) * 32)
        # Cohort-stage hook: mutation occurs inside the cohort command; the
        # post-cohort recheck must fail before any protocol verdict for that
        # family, so no fake family verdict gets appended and no fake checker
        # acceptance exists.
        cohort_outcome = run_execute("cohort")
        self.assertFalse(cohort_outcome["accepted"])
        error_blob = "\n".join(cohort_outcome["errors"])
        self.assertIn("cohort:control_frame_validation:post", error_blob)
        self.assertIn("drifted: libCryptov1", error_blob)
        # Distinct post-run entry appended after the cohort post failure.
        self.assertTrue(
            any(
                entry.startswith("post-run runtime integrity: ValueError:")
                for entry in cohort_outcome["errors"]
            ),
            cohort_outcome["errors"],
        )
        # Post-cohort drift precedes any checker verdict for that family.
        families_recorded = [f["name"] for f in cohort_outcome["families"]]
        self.assertNotIn("control_frame_validation", families_recorded)
        self.assertNotIn("metadata_length_validation", families_recorded)
        failures["cohort"] = list(cohort_outcome["errors"])
        (bsim / "lib" / "libCryptov1.so").write_bytes(bytes((3,)) * 32)

        # Vanished runtime library after capture: finally must not silently
        # skip integrity. The bounded read fails and a distinct post-run
        # runtime error is reported (root existence is never a waiver).
        vanish_outcome = run_execute("vanish")
        self.assertFalse(vanish_outcome["accepted"])
        self.assertTrue(
            any(
                entry.startswith("post-run runtime integrity: ")
                and (
                    "drifted: libCryptov1" in entry
                    or "No such file" in entry
                    or "unsafe or oversized snapshot" in entry
                )
                for entry in vanish_outcome["errors"]
            ),
            vanish_outcome["errors"],
        )
        self.assertTrue(
            any(
                "authored vanish-stage original failure" in entry
                for entry in vanish_outcome["errors"]
            ),
            vanish_outcome["errors"],
        )
        failures["vanish"] = list(vanish_outcome["errors"])
        (bsim / "lib" / "libCryptov1.so").write_bytes(bytes((3,)) * 32)

    def test_source_mutation_detected(self):
        path = self.base / "source.c"
        path.write_bytes(b"original")
        snapshots = self.base / "source-snapshots"
        snapshots.mkdir()
        before = freeze_sources([path], snapshots)
        path.write_bytes(b"modified")
        with self.assertRaises(ValueError):
            freeze_sources([path], snapshots, before)
        path.write_bytes(b"original")
        copy_path = Path(before[str(path)]["copy"]["path"])
        copy_path.write_bytes(b"tampered")
        with self.assertRaises(ValueError):
            freeze_sources([path], snapshots, before)
        path.write_bytes(b"x" * (2 * 1024 * 1024 + 1))
        with self.assertRaises(ValueError):
            freeze_sources([path], snapshots, before)
        with self.assertRaises(ValueError):
            freeze_sources([path] * 513, snapshots)
        paths = source_paths(self.base / "v3.4.1")
        self.assertIn(ROOT / "tests/bsim/Kconfig", paths)
        self.assertIn(
            self.base / "v3.4.1/zephyr/subsys/bluetooth/audio/ascs_internal.h", paths
        )

    def test_build_warning_whitelist_exact_source_and_count(self):
        sdk = self.base / "v3.4.1"
        raw = (
            "\n".join(
                "warning: " + text for text in BUILD_PROFILE["receiver_experimental"]
            )
            + f"\nCMake Warning at {sdk}/nrf/cmake/device_support.cmake:34 (message):\n"
            "  SoC native is not supported by this release.\n"
        ).encode()
        progress = b"[100/413] Building C object mbedx509.dir/error.c.obj\n"
        clean_yaml = b'buildResult:\n  variable: "C_COMPILER_TEST"\n  exitCode: 0\n'
        inspect_warnings(raw, progress, "receiver", sdk, clean_yaml)
        for cmake, ninja in (
            (raw + b"warning: unrelated\n", b""),
            (raw.replace(b"device_support.cmake:34", b"device_support.cmake:35"), b""),
            (raw, b"skipping incompatible libfoo.so\n"),
            (raw, progress + b"gcc: error: real failure after error.c.obj\n"),
            (raw, b"ninja: build stopped: subcommand failed.\n"),
            (raw + b"warning: Experimental symbol BT_LL_SW_SPLIT is enabled.\n", b""),
        ):
            with self.assertRaises(ValueError):
                inspect_warnings(cmake, ninja, "receiver", sdk, clean_yaml)
        actual_probe = (
            'buildResult:\n  variable: "C_COMPILER_SUPPORTS_WFORMAT_SIGNEDNESS"\n'
            "  stdout: |\n"
            + "".join(
                "    /nix/bin/ld.bfd: skipping incompatible libgcc_s.so.1\n"
                for _ in range(8)
            )
            + "  exitCode: 0\n"
        ).encode()
        with self.assertRaisesRegex(
            ValueError, "probe=C_COMPILER_SUPPORTS_WFORMAT_SIGNEDNESS exit=0"
        ):
            inspect_warnings(raw, progress, "receiver", sdk, actual_probe)
        with self.assertRaisesRegex(ValueError, "fatal error: missing.h"):
            inspect_warnings(
                raw,
                progress,
                "receiver",
                sdk,
                b'buildResult:\n  variable: "HAVE_HEADER"\n'
                b"  stdout: |\n    gcc: fatal error: missing.h\n  exitCode: 1\n",
            )

    def test_native_build_checker_cli_requires_all_retained_raw_files(self):
        sdk = self.base / "v3.4.1"
        (sdk / "zephyr").mkdir(parents=True)
        (sdk / "nrf").mkdir()
        checker = ROOT / "scripts/check-native-bsim-build.py"
        for label, role, mutate in (
            ("receiver-valid", "receiver", None),
            ("client-valid", "client", None),
            ("receiver-compiler-error", "receiver", "compiler"),
            ("client-arch-warning", "client", "incompatible"),
            ("receiver-missing-yaml", "receiver", "missing"),
            ("client-config-drift", "client", "config"),
        ):
            with self.subTest(label=label):
                output = self.base / label
                area = output / "build" / role
                area.mkdir(parents=True)
                cmake = (
                    "\n".join(
                        "warning: " + text
                        for text in BUILD_PROFILE[f"{role}_experimental"]
                    )
                    + f"\nCMake Warning at {sdk}/nrf/cmake/device_support.cmake:34 (message):\n"
                    "  SoC native is not supported by this release.\n"
                )
                ninja = "[100/413] Building C object mbedx509.dir/error.c.obj\n"
                config = (
                    "\n".join(
                        (
                            "CONFIG_BT_LL_SW_SPLIT=y",
                            "CONFIG_COVERAGE=y",
                            "CONFIG_ASSERT=y",
                            "CONFIG_COMPILER_WARNINGS_AS_ERRORS=y",
                            "CONFIG_BT_CTLR_PERIPHERAL_ISO=y"
                            if role == "receiver"
                            else "CONFIG_BT_CTLR_CENTRAL_ISO=y",
                        )
                    )
                    + "\n"
                )
                yaml = 'buildResult:\n  variable: "C_COMPILER_CHECK"\n  exitCode: 0\n'
                if mutate == "compiler":
                    ninja += "gcc: error: wrong configuration after error.c.obj\n"
                if mutate == "incompatible":
                    yaml += "ld.bfd: skipping incompatible libgcc_s.so.1\n"
                if mutate == "config":
                    config = config.replace("CONFIG_BT_LL_SW_SPLIT=y\n", "")
                for name, data in (
                    ("cmake.out", cmake),
                    ("ninja.out", ninja),
                    ("resolved.config", config),
                    ("cmake-configure.yaml", yaml),
                ):
                    if mutate == "missing" and name == "cmake-configure.yaml":
                        continue
                    (area / name).write_text(data)
                env = os.environ.copy()
                env["ZEPHYR_BASE"] = str(sdk / "zephyr")
                proc = subprocess.run(
                    [
                        sys.executable,
                        str(checker),
                        "--log-root",
                        str(output),
                        "--role",
                        role,
                    ],
                    capture_output=True,
                    text=True,
                    check=False,
                    env=env,
                    timeout=30,
                )
                verdict = json.loads((area / "build-warning-verdict.json").read_text())
                if mutate is None:
                    self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
                    self.assertTrue(verdict["accepted"])
                    self.assertEqual(
                        set(verdict["files"]),
                        {
                            "cmake.out",
                            "ninja.out",
                            "resolved.config",
                            "cmake-configure.yaml",
                        },
                    )
                    for file_name, entry in verdict["files"].items():
                        raw = (area / file_name).read_bytes()
                        self.assertEqual(entry["bytes"], len(raw))
                        self.assertEqual(
                            entry["sha256"], hashlib.sha256(raw).hexdigest()
                        )
                else:
                    self.assertEqual(proc.returncode, 1, proc.stdout + proc.stderr)
                    self.assertFalse(verdict["accepted"])
                    self.assertTrue(verdict["errors"])


if __name__ == "__main__":
    unittest.main()


class HostCmakeProbeEntry(unittest.TestCase):
    """Actual host-CMake capability harness for the new -Wl,--entry=main
    probe fix.  Uses the installed cmake/ninja (missing tool is a hard
    failure, not a skip); never runs the produced probe binaries."""

    TOOLCHAIN_BIN = Path(
        "/home/thomas-workstation/ncs/toolchains/8285d8ad56/usr/local/bin"
    )

    def _tool(self, name):
        found = shutil.which(name)
        if found is None:
            candidate = self.TOOLCHAIN_BIN / name
            if candidate.is_file() and os.access(candidate, os.X_OK):
                return str(candidate)
            self.fail(f"required tool {name!r} not installed")
        return found

    def _env(self, compiler):
        libc32, gcc32 = resolve_library_dirs(compiler)
        env = os.environ.copy()
        env["NIX_LDFLAGS"] = f"-L{libc32} -L{gcc32} " + env.get("NIX_LDFLAGS", "")
        return env

    def _write_probe(self, root, *, with_entry):
        root.mkdir(parents=True, exist_ok=True)
        source = root / "probe.c"
        source.write_text(
            "int main(void) { return 0; }\n"
            if with_entry
            # Baseline probe also needs main; the difference
            # is only CMAKE_REQUIRED_LINK_OPTIONS.
            else "int main(void) { return 0; }\n"
        )
        project = root / "CMakeLists.txt"
        if with_entry:
            project.write_text(
                "cmake_minimum_required(VERSION 3.20.0)\n"
                "project(probe C)\n"
                "include(CheckCCompilerFlag)\n"
                'set(CMAKE_REQUIRED_FLAGS "-fuse-ld=bfd")\n'
                'set(CMAKE_REQUIRED_LINK_OPTIONS "-nostdlib")\n'
                'list(APPEND CMAKE_REQUIRED_LINK_OPTIONS "-Wl,--entry=main")\n'
                'check_c_compiler_flag("" HAS_NOSTDLIB)\n'
                'message(NOTICE "RESULT_HAS_NOSTDLIB=${HAS_NOSTDLIB}")\n'
            )
        else:
            project.write_text(
                "cmake_minimum_required(VERSION 3.20.0)\n"
                "project(probe C)\n"
                "include(CheckCCompilerFlag)\n"
                'set(CMAKE_REQUIRED_FLAGS "-fuse-ld=bfd")\n'
                'set(CMAKE_REQUIRED_LINK_OPTIONS "-nostdlib")\n'
                'check_c_compiler_flag("" HAS_NOSTDLIB)\n'
                'message(NOTICE "RESULT_HAS_NOSTDLIB=${HAS_NOSTDLIB}")\n'
            )
        return source

    def _configure(self, root, env, cache):
        configure = subprocess.run(
            [
                self._tool("cmake"),
                "-G",
                "Ninja",
                f"-DCMAKE_C_FLAGS=-m32",
                f"-DCMAKE_MAKE_PROGRAM:FILEPATH={self._tool('ninja')}",
                "-S",
                str(root),
                "-B",
                str(cache),
                "-DCMAKE_C_COMPILER=gcc",
            ],
            capture_output=True,
            text=True,
            env=env,
            cwd=str(root),
            timeout=120,
        )
        return configure

    def test_probe_warning_removed_by_entry_option(self):
        base = self.base if hasattr(self, "base") else None
        with tempfile.TemporaryDirectory(prefix="pb051-probe-") as tempdir:
            root = Path(tempdir)
            env = self._env("gcc")
            baseline_dir = root / "baseline"
            fixed_dir = root / "fixed"
            self._write_probe(root, with_entry=True)
            # Baseline: run a second project dir with the no-entry variant.
            baseline_root = root / "baseline-project"
            baseline_dir.parent.mkdir(exist_ok=True)
            self._write_probe(baseline_root, with_entry=False)
            baseline = self._configure(baseline_root, env, baseline_dir)
            self.assertEqual(
                0,
                baseline.returncode,
                baseline.stdout + baseline.stderr,
            )
            baseline_yaml = list(
                baseline_dir.rglob("CMakeFiles/CMakeConfigureLog.yaml")
            )
            self.assertTrue(baseline_yaml, baseline_dir)
            raw = baseline_yaml[0].read_bytes()
            # Known intentional negative fixture: baseline probe prints the
            # exact _start warning with exit 0.
            self.assertIn(b"cannot find entry symbol _start", raw)
            self.assertIn(b"exitCode: 0", raw)
            # Fixed probe: HAS_NOSTDLIB must be TRUE with no warning.
            fixed = self._configure(root, env, fixed_dir)
            self.assertEqual(0, fixed.returncode, fixed.stdout + fixed.stderr)
            fixed_yaml = list(fixed_dir.rglob("CMakeFiles/CMakeConfigureLog.yaml"))
            self.assertTrue(fixed_yaml)
            # Cached HAS_NOSTDLIB truth lives in CMakeCache.txt; NOTICE
            # output can be stripped by CMake's internal checks.
            cache = (fixed_dir / "CMakeCache.txt").read_text()
            self.assertIn("HAS_NOSTDLIB:INTERNAL=1", cache)
            fixed_log = fixed_yaml[0].read_bytes()
            self.assertNotIn(b"cannot find entry symbol", fixed_log)
            # Only probe link carries the entry option: the configure
            # scratch try_compile line includes -Wl,--entry=main.
            self.assertIn(b"-Wl,--entry=main", fixed_log)

    def test_inspect_warnings_still_rejects_start_warning(self):
        # Independent checker unchanged: a synthetic yaml with the _start
        # warning must keep being rejected.
        from ascs_bsim_run import inspect_warnings

        sdk = (
            self.base
            if hasattr(self, "base")
            else Path(tempfile.mkdtemp(prefix="pb051-inspect-"))
        )
        raw = (
            "\n".join(
                "warning: " + text for text in BUILD_PROFILE["receiver_experimental"]
            )
            + f"\nCMake Warning at {sdk}/nrf/cmake/device_support.cmake:34 (message):\n"
            "  SoC native is not supported by this release.\n"
        )
        yaml_with_warning = (
            'buildResult:\n  variable: "check_C__fuse_ld_bfd__nostdlib"\n'
            "  stdout: |\n"
            "    ld.bfd: warning: cannot find entry symbol _start; defaulting to 00001000\n"
            "  exitCode: 0\n"
        ).encode()
        with self.assertRaisesRegex(ValueError, "probe=check_C__fuse_ld_bfd__nostdlib"):
            inspect_warnings(raw.encode(), b"", "receiver", sdk, yaml_with_warning)
