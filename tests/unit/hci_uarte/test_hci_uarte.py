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
from patch_ncs_uarte import (  # noqa: E402
    AUDITED_SHA256,
    BOUNDARY_CHANGES,
    COHERENCY_CHANGES,
    NEW_BODY,
    OLD_BODY,
    transform,
)


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
        self.assertEqual(
            hashlib.sha256(generated).hexdigest(),
            "c16932ae1fba3047f70b5a9785ef0eb735b3a5b376b9fc60f9a349893f98bf2d",
        )
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
                *(
                    bad
                    for _, old, _ in BOUNDARY_CHANGES
                    for bad in (original.replace(old, b"", 1), original + old)
                ),
                *(
                    bad
                    for _, old, new in COHERENCY_CHANGES
                    for bad in (
                        original.replace(old, b"", 1)
                        if old in original
                        else original + new,
                        original + old,
                    )
                ),
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
        restored = generated
        for label, old, new in reversed(COHERENCY_CHANGES):
            self.assertEqual(restored.count(new), 1, label)
            restored = restored.replace(new, old, 1)
        self.assertEqual(
            hashlib.sha256(restored).hexdigest(),
            "d264f54f8f3b0053fb2ee6fe84e1572fe7d20062aaffdf2487cdc12a7cf12beb",
        )
        for label, old, new in reversed(BOUNDARY_CHANGES):
            self.assertEqual(restored.count(new), 1, label)
            restored = restored.replace(new, old, 1)
        self.assertEqual(
            restored, original.replace(old_function.encode(), new_function.encode(), 1)
        )
        self.assertEqual(
            restored.replace(new_function.encode(), old_function.encode(), 1), original
        )
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
                # Host uses 64-bit pointers, target DMA register uses low 32 bits.
                # Equivalent uintptr_t narrowing only in extracted host functions.
                (directory / "functions.inc").write_text(
                    "\n".join(
                        function(code, name).replace(
                            "(uint32_t)cbwt_data->curr_bounce_buf",
                            "(uint32_t)(uintptr_t)cbwt_data->curr_bounce_buf",
                        )
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
                    [str(executable)], capture_output=True, text=True, timeout=5
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

    def test_deferred_boundary_uart_output_red_then_green(self):
        original = SDK.read_bytes()
        source = original.decode()
        prepare = function(source, "prepare_bounce_buf").encode()
        self.assertEqual(prepare.count(OLD_BODY), 1)
        aa = original.replace(prepare, prepare.replace(OLD_BODY, NEW_BODY, 1), 1)
        self.assertEqual(
            hashlib.sha256(aa).hexdigest(),
            "0a7c0e0d7feb41860f7cda67b206c5406f68404a9b82c5603d3be6ef09c6147b",
        )
        v1 = aa
        for label, old, new in BOUNDARY_CHANGES:
            self.assertEqual(v1.count(old), 1, label)
            v1 = v1.replace(old, new, 1)
        self.assertEqual(
            hashlib.sha256(v1).hexdigest(),
            "d264f54f8f3b0053fb2ee6fe84e1572fe7d20062aaffdf2487cdc12a7cf12beb",
        )
        v2 = transform(original)
        names = (
            "rx_buf_req",
            "notify_rx_rdy",
            "sample_bounce_position",
            "anomaly_byte_handle",
            "settle_bounce_anomaly",
            "fill_usr_buf",
            "prepare_bounce_buf",
            "resolve_bounce_boundary",
            "update_usr_buf",
            "bounce_buf_swap",
            "get_swap_len",
            "cbwt_rx_enable",
        )
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            for runtime in (False, True):
                for label, driver in (("aa", aa), ("v1", v1), ("v2", v2)):
                    with self.subTest(runtime_configure=runtime, source=label):
                        snippets = [
                            function(driver.decode(), name)
                            for name in names
                            if (
                                name
                                not in (
                                    "sample_bounce_position",
                                    "settle_bounce_anomaly",
                                )
                                or label == "v2"
                            )
                            and (name != "resolve_bounce_boundary" or label != "aa")
                        ]
                        # Host-only low-32-bit DMA pointer adaptation, identical
                        # for every original/generated extracted function.
                        snippets = [
                            snippet.replace(
                                "(uint32_t)cbwt_data->curr_bounce_buf",
                                "(uint32_t)(uintptr_t)cbwt_data->curr_bounce_buf",
                            )
                            for snippet in snippets
                        ]
                        (directory / "functions.inc").write_text("\n".join(snippets))
                        executable = directory / (
                            label + ("-runtime" if runtime else "-fixed")
                        )
                        compile_result = subprocess.run(
                            [
                                os.environ.get("CC", "cc"),
                                "-std=c11",
                                "-Wall",
                                "-Wextra",
                                "-Werror",
                                *(
                                    ["-DCONFIG_UART_USE_RUNTIME_CONFIGURE=1"]
                                    if runtime
                                    else []
                                ),
                                *(["-DREPAIR=1"] if label != "aa" else []),
                                *(["-DV2=1"] if label == "v2" else []),
                                "-I",
                                str(directory),
                                str(Path(__file__).with_name("boundary_replay.c")),
                                "-o",
                                str(executable),
                            ],
                            capture_output=True,
                            text=True,
                            timeout=30,
                        )
                        self.assertEqual(
                            compile_result.returncode, 0, compile_result.stderr
                        )
                        self.assertNotIn("warning:", compile_result.stderr.lower())
                        result = subprocess.run(
                            [str(executable)], capture_output=True, text=True, timeout=5
                        )
                        print(
                            label,
                            "runtime" if runtime else "fixed",
                            result.stdout.strip(),
                        )
                        self.assertEqual(
                            result.returncode,
                            0 if label == "v2" else 1,
                            result.stdout + result.stderr,
                        )
                        self.assertIn(
                            {
                                "aa": "263 mismatches",
                                "v1": "3 mismatches",
                                "v2": "0 mismatches",
                            }[label],
                            result.stdout,
                        )
                        if label == "aa":
                            self.assertIn(
                                "54118 mismatch offset=7 got=aa expected=de",
                                result.stderr,
                            )
                            self.assertIn(
                                "54119-real91 mismatch offset=5 got=aa expected=91",
                                result.stderr,
                            )
                            self.assertIn(
                                "complete-129-byte-H4-ISO mismatch", result.stderr
                            )
                        elif label == "v1":
                            self.assertIn(
                                "captured-38740-first-cut mismatch offset=4 got=aa expected=d5",
                                result.stderr,
                            )
                            self.assertIn(
                                "late-anomaly-78 mismatch offset=7 got=aa expected=78",
                                result.stderr,
                            )
                        else:
                            self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
