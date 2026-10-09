#!/usr/bin/env python3
"""Compiler-visible GNU feature flags for authored translation units."""

import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "bluez_host_prepare", ROOT / "scripts/bluez_host_prepare.py"
)
assert SPEC is not None and SPEC.loader is not None
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)


class SourceMacroFlags(unittest.TestCase):
    def test_pipe2_declaration_with_source_and_compiler_definitions(self):
        body = b"""#include <fcntl.h>
#include <unistd.h>
int create_pipe(int fds[2]) { return pipe2(fds, O_CLOEXEC); }
"""
        with tempfile.TemporaryDirectory(prefix="bluez-prepare-") as directory:
            for name, contents in (
                ("self-defined", b"#  define _GNU_SOURCE\n" + body),
                ("compiler-defined", body),
            ):
                with self.subTest(name=name):
                    source = Path(directory) / f"{name}.c"
                    source.write_bytes(contents)
                    subprocess.run(
                        ["cc", "-std=gnu11", "-Wall", "-Werror"]
                        + PREPARE.source_macro_flags(source.read_bytes())
                        + ["-c", str(source), "-o", str(source.with_suffix(".o"))],
                        check=True,
                        capture_output=True,
                        timeout=30,
                    )


class PreparationBoundaries(unittest.TestCase):
    def test_forbidden_existing_and_symlink_output_preserved(self):
        with tempfile.TemporaryDirectory(prefix="bluez-prepare-") as directory:
            root = Path(directory)
            existing = root / "existing"
            existing.mkdir()
            marker = existing / "marker"
            marker.write_text("original")
            link = root / "link"
            link.symlink_to(existing, target_is_directory=True)
            for output in (
                existing,
                link,
                Path.home() / "pb053-test",
                ROOT / "pb053-test",
                Path("/nix/store/pb053-test"),
                Path("/tmp/opencode/pb053-emulator-build-r2/child"),
            ):
                with self.subTest(output=str(output)), self.assertRaises(ValueError):
                    PREPARE.check_output(root, output)
            self.assertEqual(marker.read_text(), "original")
            self.assertTrue(link.is_symlink())
            self.assertFalse((root / "new").exists())
            self.assertEqual(
                PREPARE.check_output(root / "vendor", root / "new"), root / "new"
            )

    def test_real_header_dependency_hash_changes(self):
        with tempfile.TemporaryDirectory(prefix="bluez-prepare-") as directory:
            root = Path(directory)
            header = root / "sample.h"
            source = root / "sample.c"
            source.write_text(
                '#include "sample.h"\nint sample(void) { return NUMBER; }\n'
            )
            header.write_text("#define NUMBER 1\n")
            previous = None
            for number in (1, 2):
                header.write_text(f"#define NUMBER {number}\n")
                depfile = root / f"{number}.d"
                PREPARE.run_logged(
                    [
                        "cc",
                        "-Wall",
                        "-Werror",
                        "-MD",
                        "-MF",
                        str(depfile),
                        "-MT",
                        "PB053_DEP",
                        "-c",
                        str(source),
                        "-o",
                        str(root / f"{number}.o"),
                    ],
                    root / f"{number}.log",
                    30,
                )
                identities = PREPARE.dependency_identities(depfile, root)
                self.assertIn(str(header), identities)
                self.assertIn(str(source), identities)
                if previous is not None:
                    self.assertNotEqual(previous, identities[str(header)]["sha256"])
                previous = identities[str(header)]["sha256"]
                PREPARE.verify_inputs(
                    root,
                    {"sample.c": PREPARE.file_identity(source)["sha256"]},
                    {"sample.c": identities},
                )
            header.write_text("#define NUMBER 3\n")
            with self.assertRaisesRegex(ValueError, "Dependency changed"):
                PREPARE.verify_inputs(
                    root,
                    {"sample.c": PREPARE.file_identity(source)["sha256"]},
                    {"sample.c": identities},
                )
            header.write_text("#define NUMBER 2\n")
            original_source_hash = PREPARE.file_identity(source)["sha256"]
            source.write_text(
                '#include "sample.h"\nint sample(void) { return NUMBER + 1; }\n'
            )
            with self.assertRaisesRegex(ValueError, "Source changed"):
                PREPARE.verify_inputs(
                    root, {"sample.c": original_source_hash}, {"sample.c": identities}
                )

    def test_timeout_reaps_real_owned_descendant(self):
        with tempfile.TemporaryDirectory(prefix="bluez-prepare-") as directory:
            root = Path(directory)
            script = root / "spawn.py"
            script.write_text("""import subprocess, sys, time
child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
print(child.pid, flush=True)
time.sleep(60)
""")
            with self.assertRaises(subprocess.TimeoutExpired):
                PREPARE.run_logged(
                    [sys.executable, str(script)], root / "timeout.log", 0.5
                )
            pid = int((root / "timeout.log").read_text().strip())
            self.assertFalse(process_running(pid))

    def test_external_sigterm_cleans_owned_process(self):
        with tempfile.TemporaryDirectory(prefix="bluez-prepare-") as directory:
            root = Path(directory)
            sleeper = root / "sleeper.py"
            sleeper.write_text(
                "import os, time\nprint(os.getpid(), flush=True)\ntime.sleep(60)\n"
            )
            harness = root / "harness.py"
            harness.write_text("""import importlib.util, pathlib, sys
spec = importlib.util.spec_from_file_location("prepare", sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.run_logged([sys.executable, sys.argv[2]], sys.argv[3], 60)
""")
            log = root / "signal.log"
            proc = subprocess.Popen(
                [
                    sys.executable,
                    str(harness),
                    str(ROOT / "scripts/bluez_host_prepare.py"),
                    str(sleeper),
                    str(log),
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
            )
            try:
                deadline = time.monotonic() + 10
                while not log.exists() or not log.read_text().strip():
                    self.assertIsNone(proc.poll())
                    if time.monotonic() >= deadline:
                        self.fail("Harness did not start child")
                    time.sleep(0.05)
                os.kill(proc.pid, signal.SIGTERM)
                _, stderr = proc.communicate(timeout=12)
                self.assertNotEqual(proc.returncode, 0)
                self.assertIn(b"PreparationCancelled", stderr)
                self.assertFalse(process_running(int(log.read_text().strip())))
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=3)


def process_running(pid):
    try:
        stat = Path(f"/proc/{pid}/stat").read_text()
    except FileNotFoundError:
        return False
    return stat.split(") ", 1)[1][0] != "Z"


if __name__ == "__main__":
    unittest.main()
