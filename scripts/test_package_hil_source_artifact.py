#!/usr/bin/env python3
"""Public subprocess tests for deterministic HIL source artifact packaging."""

import hashlib
import json
import os
import resource
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

SCRIPT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "package-hil-source-artifact.py"
)
COMMIT = "0123456789abcdef0123456789abcdef01234567"
NCS = "v3.3.0"
MEMBERS = ["cpunet.hex", "cpuapp.hex", "source-manifest.json", "SHA256SUMS"]


def hex_record(record_type, address=0, data=b""):
    payload = (
        bytes([len(data)]) + address.to_bytes(2, "big") + bytes([record_type]) + data
    )
    return ":" + (payload + bytes([(-sum(payload)) & 0xFF])).hex().upper()


def valid_hex(data=b"\x01\x02"):
    return (hex_record(0, 0, data) + "\n:00000001FF\n").encode("ascii")


def make_build(root):
    paths = {
        "app/zephyr/zephyr.hex": valid_hex(b"\xaa\xbb"),
        "hci_ipc/zephyr/zephyr.hex": valid_hex(b"\xcc\xdd"),
    }
    for relative, data in paths.items():
        path = os.path.join(root, relative)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as fh:
            fh.write(data)
    return paths


def run_cli(build_root, output, *, commit=COMMIT, ncs=NCS, preexec_fn=None):
    return subprocess.run(
        [
            sys.executable,
            SCRIPT,
            "--git-commit",
            commit,
            "--ncs-version",
            ncs,
            "--build-root",
            build_root,
            "--output",
            output,
        ],
        capture_output=True,
        text=True,
        preexec_fn=preexec_fn,
    )


def limit_file_size_100():
    resource.setrlimit(resource.RLIMIT_FSIZE, (100, 100))


class TestHilSourceArtifact(unittest.TestCase):
    def test_deterministic_exact_contract(self):
        with tempfile.TemporaryDirectory() as td:
            build = os.path.join(td, "build")
            inputs = make_build(build)
            one = os.path.join(td, "one.zip")
            two = os.path.join(td, "two.zip")
            first = run_cli(build, one)
            second = run_cli(build, two)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            with open(one, "rb") as first_file, open(two, "rb") as second_file:
                self.assertEqual(first_file.read(), second_file.read())
            self.assertEqual(
                first.stdout, "package-hil-source-artifact: wrote one.zip\n"
            )
            with zipfile.ZipFile(one) as zf:
                self.assertEqual(zf.namelist(), MEMBERS)
                for info in zf.infolist():
                    self.assertEqual(info.date_time, (1980, 1, 1, 0, 0, 0))
                    self.assertEqual(info.create_system, 3)
                    self.assertEqual(info.external_attr >> 16, 0o100644)
                    self.assertEqual(info.extra, b"")
                    self.assertEqual(info.comment, b"")
                manifest_raw = zf.read("source-manifest.json")
                manifest = json.loads(manifest_raw)
                self.assertEqual(
                    manifest,
                    {
                        "board": "nrf5340dk/nrf5340/cpuapp",
                        "firmware_id": "le-audio-hil-source-rh1",
                        "git_commit": COMMIT,
                        "images": [
                            {
                                "filename": "cpunet.hex",
                                "flash_order": 0,
                                "role": "cpunet",
                                "sha256": hashlib.sha256(
                                    inputs["hci_ipc/zephyr/zephyr.hex"]
                                ).hexdigest(),
                                "size": len(inputs["hci_ipc/zephyr/zephyr.hex"]),
                            },
                            {
                                "filename": "cpuapp.hex",
                                "flash_order": 1,
                                "role": "cpuapp",
                                "sha256": hashlib.sha256(
                                    inputs["app/zephyr/zephyr.hex"]
                                ).hexdigest(),
                                "size": len(inputs["app/zephyr/zephyr.hex"]),
                            },
                        ],
                        "ncs_version": NCS,
                        "schema_version": 1,
                    },
                )
                self.assertEqual(
                    manifest_raw,
                    (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode(
                        "utf-8"
                    ),
                )
                sums = zf.read("SHA256SUMS").decode("ascii").splitlines()
                expected = [
                    "%s  %s" % (hashlib.sha256(zf.read(name)).hexdigest(), name)
                    for name in sorted(MEMBERS[:-1])
                ]
                self.assertEqual(sums, expected)

    def test_bad_inputs_output_collision_and_cleanup(self):
        with tempfile.TemporaryDirectory() as td:
            build = os.path.join(td, "build")
            make_build(build)
            output = os.path.join(td, "artifact.zip")
            cases = [
                ("bad commit", {"commit": "BAD"}),
                ("bad ncs", {"ncs": "3.3.0"}),
            ]
            for name, kwargs in cases:
                with self.subTest(name=name):
                    result = run_cli(build, output, **kwargs)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertTrue(
                        result.stderr.startswith("package-hil-source-artifact: error: ")
                    )
                    self.assertFalse(os.path.exists(output))
            os.unlink(os.path.join(build, "app/zephyr/zephyr.hex"))
            result = run_cli(build, output)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(os.path.exists(output))
            make_build(build)
            with open(os.path.join(build, "app/zephyr/zephyr.hex"), "wb") as fh:
                fh.write(b":00000001FF\n:00000001FF\n")
            result = run_cli(build, output)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(os.path.exists(output))
            make_build(build)
            os.unlink(os.path.join(build, "app/zephyr/zephyr.hex"))
            os.symlink(
                os.path.join(build, "hci_ipc/zephyr/zephyr.hex"),
                os.path.join(build, "app/zephyr/zephyr.hex"),
            )
            result = run_cli(build, output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("symlink", result.stderr)
            self.assertFalse(os.path.exists(output))
            os.unlink(os.path.join(build, "app/zephyr/zephyr.hex"))
            make_build(build)
            with open(output, "wb") as fh:
                fh.write(b"keep")
            result = run_cli(build, output)
            self.assertNotEqual(result.returncode, 0)
            with open(output, "rb") as fh:
                self.assertEqual(fh.read(), b"keep")

    def test_rejects_relative_and_symlinked_output_paths(self):
        with tempfile.TemporaryDirectory() as td:
            build = os.path.join(td, "build")
            make_build(build)
            relative = run_cli(build, "artifact.zip")
            self.assertNotEqual(relative.returncode, 0)
            self.assertIn("output path must be absolute", relative.stderr)

            real_parent = os.path.join(td, "real")
            os.mkdir(real_parent)
            alias_parent = os.path.join(td, "alias")
            os.symlink(real_parent, alias_parent)
            output = os.path.join(alias_parent, "artifact.zip")
            result = run_cli(build, output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("output parent is a symlink", result.stderr)
            self.assertFalse(os.path.exists(os.path.join(real_parent, "artifact.zip")))

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux output failure test")
    def test_write_failure_leaves_no_temp_or_final_output(self):
        with tempfile.TemporaryDirectory() as td:
            build = os.path.join(td, "build")
            make_build(build)
            output = os.path.join(td, "artifact.zip")
            result = run_cli(build, output, preexec_fn=limit_file_size_100)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(os.path.exists(output))
            self.assertEqual(
                [
                    name
                    for name in os.listdir(td)
                    if name.startswith(".hil-source-artifact-")
                ],
                [],
            )


if __name__ == "__main__":
    unittest.main()
