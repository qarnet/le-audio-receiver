#!/usr/bin/env python3
"""Compile audited SDK receive functions and replay observable output."""

import difflib
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
if not os.environ.get("ZEPHYR_BASE"):
    raise RuntimeError(
        "ZEPHYR_BASE is required to run UARTE replay in the NCS dev shell"
    )
SDK = Path(os.environ["ZEPHYR_BASE"]) / "drivers/serial/uart_nrfx_uarte.c"
sys.path.insert(0, str(ROOT / "scripts"))
import patch_ncs_uarte  # noqa: E402
from patch_ncs_uarte import AUDITED_SHA256, transform  # noqa: E402


def function(source: str, name: str) -> str:
    matches = list(
        re.finditer(
            r"(?m)^static [^\n]*\b" + re.escape(name) + r"\([^;{]*\)\n\{", source
        )
    )
    if len(matches) != 1:
        raise ValueError(f"expected one definition: {name}")
    start = matches[0].start()
    opening = matches[0].end() - 1
    depth = 0
    for i in range(opening, len(source)):
        depth += (source[i] == "{") - (source[i] == "}")
        if depth == 0:
            return source[start : i + 1] + "\n"
    raise ValueError(f"unclosed function: {name}")


class UarteCompatibilityTests(unittest.TestCase):
    def test_source_guard_and_deterministic_generation(self):
        original = SDK.read_bytes()
        self.assertEqual(hashlib.sha256(original).hexdigest(), AUDITED_SHA256)
        generated = transform(original)
        self.assertEqual(generated, transform(original))
        # Force anchor validation in isolation; normal CLI rejects altered
        # source earlier at the hash guard.
        with patch.object(patch_ncs_uarte.hashlib, "sha256") as digest:
            digest.return_value.hexdigest.return_value = AUDITED_SHA256
            for bad in (
                original.replace(
                    b"static void prepare_bounce_buf",
                    b"static void prepare_missing_buf",
                ),
                original + patch_ncs_uarte.START,
                original.replace(patch_ncs_uarte.OLD_BODY, b""),
            ):
                with self.assertRaises(ValueError):
                    transform(bad)
        with tempfile.TemporaryDirectory() as tmp:
            src, out = Path(tmp) / "source.c", Path(tmp) / "generated.c"
            src.write_bytes(original + b"\n")
            for bad in (
                original + b"\n",
                original.replace(
                    b"static void prepare_bounce_buf",
                    b"static void prepare_missing_buf",
                ),
            ):
                src.write_bytes(bad)
                result = subprocess.run(
                    [
                        sys.executable,
                        str(ROOT / "scripts/patch_ncs_uarte.py"),
                        str(src),
                        str(out),
                    ],
                    capture_output=True,
                    text=True,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(out.exists())
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/patch_ncs_uarte.py"),
                    str(SDK),
                    str(SDK),
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(out.exists())
            src.write_bytes(original)
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/patch_ncs_uarte.py"),
                    str(src),
                    str(out),
                ],
                check=True,
            )
            self.assertEqual(out.read_bytes(), generated)
        before = original.decode()
        after = generated.decode()
        old_function = function(before, "prepare_bounce_buf")
        new_function = function(after, "prepare_bounce_buf")
        self.assertEqual(after, before.replace(old_function, new_function))
        print("audited SDK SHA-256:", AUDITED_SHA256)
        print(
            "generated source diff:\n"
            + "".join(
                difflib.unified_diff(
                    old_function.splitlines(keepends=True),
                    new_function.splitlines(keepends=True),
                )
            )
        )

    def test_real_driver_receive_replay_red_then_green(self):
        original = SDK.read_bytes()
        generated = transform(original)
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            for label, source in (("original", original), ("generated", generated)):
                code = source.decode()
                (directory / "functions.inc").write_text(
                    "\n".join(
                        function(code, name)
                        for name in (
                            "anomaly_byte_handle",
                            "fill_usr_buf",
                            "prepare_bounce_buf",
                        )
                    )
                )
                executable = directory / label
                compile_result = subprocess.run(
                    [
                        os.environ.get("CC", "cc"),
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        # Audited SDK driver casts DMA pointers to its 32-bit register;
                        # host replay truncates pointer to same low 32 bits.
                        "-Wno-error=pointer-to-int-cast",
                        "-I",
                        str(directory),
                        str(Path(__file__).with_name("replay.c")),
                        "-o",
                        str(executable),
                    ],
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
                result = subprocess.run(
                    [str(executable)], capture_output=True, text=True
                )
                print(label + ": " + result.stdout.strip())
                if label == "original":
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn("3/8192 mismatches", result.stdout)
                    self.assertIn(
                        "offset=107 stale=49 incoming=aa old_tail=0 got=49",
                        result.stderr,
                    )
                else:
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
                    self.assertIn("0/8192 mismatches", result.stdout)


if __name__ == "__main__":
    unittest.main()
