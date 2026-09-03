#!/usr/bin/env python3
"""RH4 fake artifact tests. No test opens serial, probe, RF, or Bluetooth."""

import hashlib
import json
import os
import struct
import sys
import tempfile
import unittest
import zipfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "scripts"))
sys.path.insert(0, os.path.dirname(__file__))

import hil_fakes  # noqa: E402
from hil import artifacts, cli, matrix, rows  # noqa: E402

COMMIT = "0123456789abcdef0123456789abcdef01234567"


def hex_record(data):
    record = bytes([len(data), 0, 0, 0]) + data
    return (
        ":"
        + (record + bytes([(-sum(record)) & 0xFF])).hex().upper()
        + "\n:00000001FF\n"
    ).encode("ascii")


def zip_info(name, *, compression=zipfile.ZIP_DEFLATED):
    info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
    info.compress_type = compression
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def sums(members):
    return (
        "\n".join(
            "%s  %s" % (hashlib.sha256(data).hexdigest(), name)
            for name, data in sorted(members.items())
        )
        + "\n"
    ).encode("ascii")


def write_zip(path, members, *, compression=zipfile.ZIP_DEFLATED, comment=b""):
    with zipfile.ZipFile(path, "w", compression=compression, compresslevel=9) as zf:
        zf.comment = comment
        for name, data in members:
            zf.writestr(zip_info(name, compression=compression), data)


def patch_encryption_bit(path, member_name):
    """Test-only raw ZIP patch. Set encrypted flag in both matching local and
    central headers without adding encryption support. Validate all signatures,
    lengths, offsets, and filename bounds before changing exactly two fields."""
    with open(path, "rb") as fh:
        raw = bytearray(fh.read())
    eocd = raw.rfind(b"PK\x05\x06")
    if eocd < 0 or eocd + 22 > len(raw):
        raise AssertionError("missing/truncated ZIP end record")
    comment_len = struct.unpack_from("<H", raw, eocd + 20)[0]
    if eocd + 22 + comment_len != len(raw):
        raise AssertionError("invalid ZIP end record bounds")
    entries = struct.unpack_from("<H", raw, eocd + 10)[0]
    central_size, central_offset = struct.unpack_from("<II", raw, eocd + 12)
    if central_offset + central_size != eocd:
        raise AssertionError("invalid central directory bounds")
    offset = central_offset
    matches = []
    for _index in range(entries):
        if offset + 46 > eocd or raw[offset : offset + 4] != b"PK\x01\x02":
            raise AssertionError("invalid central directory signature")
        flags = struct.unpack_from("<H", raw, offset + 8)[0]
        filename_len, extra_len, comment_len = struct.unpack_from(
            "<HHH", raw, offset + 28
        )
        local_offset = struct.unpack_from("<I", raw, offset + 42)[0]
        end = offset + 46 + filename_len + extra_len + comment_len
        if end > eocd or local_offset + 30 > len(raw):
            raise AssertionError("invalid ZIP member bounds")
        name = bytes(raw[offset + 46 : offset + 46 + filename_len]).decode("ascii")
        if name == member_name:
            if raw[local_offset : local_offset + 4] != b"PK\x03\x04":
                raise AssertionError("invalid local file signature")
            local_name_len, local_extra_len = struct.unpack_from(
                "<HH", raw, local_offset + 26
            )
            local_name_end = local_offset + 30 + local_name_len + local_extra_len
            if local_name_end > len(raw):
                raise AssertionError("invalid local file bounds")
            local_name = bytes(
                raw[local_offset + 30 : local_offset + 30 + local_name_len]
            ).decode("ascii")
            if local_name != member_name:
                raise AssertionError("local/central member name mismatch")
            matches.append((offset + 8, local_offset + 6, flags))
        offset = end
    if offset != eocd or len(matches) != 1:
        raise AssertionError("expected exactly one matching ZIP member")
    central_flags_offset, local_flags_offset, flags = matches[0]
    struct.pack_into("<H", raw, central_flags_offset, flags | 0x1)
    struct.pack_into("<H", raw, local_flags_offset, flags | 0x1)
    with open(path, "wb") as fh:
        fh.write(raw)


def receiver_archive(path):
    cpu = hex_record(b"\xaa")
    flpr = hex_record(b"\xbb")
    manifest = {
        "git_commit": COMMIT,
        "images": [
            {
                "filename": "cpuapp.hex",
                "flash_order": 0,
                "original_build_path": "nrf54l15/le-audio-receiver/zephyr/zephyr.hex",
                "role": "cpuapp",
                "sha256": hashlib.sha256(cpu).hexdigest(),
                "size": len(cpu),
            },
            {
                "filename": "flpr.hex",
                "flash_order": 1,
                "original_build_path": "nrf54l15/flpr/zephyr/zephyr.hex",
                "role": "flpr",
                "sha256": hashlib.sha256(flpr).hexdigest(),
                "size": len(flpr),
            },
        ],
        "ncs_version": "v3.3.0",
        "project": "le-audio-receiver",
        "schema_version": 1,
        "target": {"board": "nrf54l15dk/nrf54l15/cpuapp", "id": "nrf54l15-xiao"},
        "version": "0.1.0",
    }
    manifest_data = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode(
        "utf-8"
    )
    note = b"# Flashing\n"
    contents = {
        "FLASHING.md": note,
        "cpuapp.hex": cpu,
        "flpr.hex": flpr,
        "release-manifest.json": manifest_data,
    }
    write_zip(
        path,
        [(name, contents[name]) for name in artifacts.RECEIVER_MEMBERS[:-1]]
        + [("SHA256SUMS", sums(contents))],
    )


def source_archive(path):
    cpunet = hex_record(b"\xcc")
    cpuapp = hex_record(b"\xdd")
    manifest = {
        "board": "nrf5340dk/nrf5340/cpuapp",
        "firmware_id": "le-audio-hil-source-rh1",
        "git_commit": COMMIT,
        "images": [
            {
                "filename": "cpunet.hex",
                "flash_order": 0,
                "role": "cpunet",
                "sha256": hashlib.sha256(cpunet).hexdigest(),
                "size": len(cpunet),
            },
            {
                "filename": "cpuapp.hex",
                "flash_order": 1,
                "role": "cpuapp",
                "sha256": hashlib.sha256(cpuapp).hexdigest(),
                "size": len(cpuapp),
            },
        ],
        "ncs_version": "v3.3.0",
        "schema_version": 1,
    }
    manifest_data = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode(
        "utf-8"
    )
    contents = {
        "cpunet.hex": cpunet,
        "cpuapp.hex": cpuapp,
        "source-manifest.json": manifest_data,
    }
    write_zip(
        path,
        [(name, contents[name]) for name in artifacts.SOURCE_MEMBERS[:-1]]
        + [("SHA256SUMS", sums(contents))],
    )


class FakeRows:
    def __init__(self):
        self.calls = []

    def factory(self, _cancel):
        return self

    def run(
        self,
        fixture,
        binding,
        output_root,
        run_id,
        junit,
        *,
        argv,
        status,
        row,
        artifacts=None,
    ):
        del fixture, binding, argv, status
        self.calls.append((row, artifacts))
        artifacts_module = __import__(
            "hil.artifacts", fromlist=["revalidate_artifact_set"]
        )
        artifacts_module.revalidate_artifact_set(artifacts)
        run_dir = os.path.join(output_root, run_id)
        os.makedirs(run_dir)
        with open(os.path.join(run_dir, "result.json"), "w", encoding="utf-8") as fh:
            json.dump({"outcome": "passed"}, fh)
        for name in ("junit.xml", "MANIFEST.md", "SHA256SUMS"):
            with open(os.path.join(run_dir, name), "w", encoding="utf-8") as fh:
                fh.write(name + "\n")
        with open(junit, "w", encoding="utf-8") as fh:
            fh.write("<testsuite/>\n")
        return "passed", None, []


class TestResolver(unittest.TestCase):
    def _archives(self, td):
        receiver = os.path.join(td, "receiver.zip")
        source = os.path.join(td, "source.zip")
        receiver_archive(receiver)
        source_archive(source)
        return receiver, source

    def test_resolves_valid_archives_stages_outside_repo_and_detects_drift(self):
        with tempfile.TemporaryDirectory() as td:
            receiver, source = self._archives(td)
            versioned_receiver = os.path.join(
                td, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
            )
            os.rename(receiver, versioned_receiver)
            receiver = versioned_receiver
            staged = artifacts.resolve_artifacts(receiver, source, staging_parent=td)
            try:
                self.assertEqual(
                    [item.role for item in staged.receiver_images], ["cpuapp", "flpr"]
                )
                self.assertEqual(
                    [item.role for item in staged.source_images], ["cpunet", "cpuapp"]
                )
                self.assertNotIn(REPO + os.sep, staged.staging_root + os.sep)
                artifacts.revalidate_artifact_set(staged)
                os.chmod(staged.source_images[0].path, 0o600)
                with open(staged.source_images[0].path, "ab") as fh:
                    fh.write(b"drift")
                with self.assertRaises(artifacts.ArtifactError):
                    artifacts.revalidate_artifact_set(staged)
            finally:
                artifacts.cleanup_artifacts(staged)

    def test_rejects_archive_contract_violations_before_staging(self):
        with tempfile.TemporaryDirectory() as td:
            receiver, source = self._archives(td)
            versioned_receiver = os.path.join(
                td, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
            )
            os.rename(receiver, versioned_receiver)
            receiver = versioned_receiver
            bad = os.path.join(td, "bad.zip")
            write_zip(
                bad,
                [("../cpuapp.hex", b"x"), ("cpuapp.hex", b"x")],
            )
            for candidate in (bad,):
                with self.subTest(candidate=os.path.basename(candidate)):
                    with self.assertRaises(artifacts.ArtifactError):
                        artifacts.resolve_artifacts(
                            candidate, source, staging_parent=td
                        )
            with zipfile.ZipFile(receiver, "r") as zf:
                members = [
                    (info.filename, zf.read(info.filename)) for info in zf.infolist()
                ]
            encrypted = os.path.join(td, "encrypted.zip")
            write_zip(encrypted, members)
            patch_encryption_bit(encrypted, "cpuapp.hex")
            with self.assertRaises(artifacts.ArtifactError):
                artifacts.resolve_artifacts(encrypted, source, staging_parent=td)
            wrong_compression = os.path.join(td, "stored.zip")
            write_zip(wrong_compression, members, compression=zipfile.ZIP_STORED)
            with self.assertRaises(artifacts.ArtifactError):
                artifacts.resolve_artifacts(
                    wrong_compression, source, staging_parent=td
                )
            os.symlink(receiver, os.path.join(td, "link.zip"))
            with self.assertRaises(artifacts.ArtifactError):
                artifacts.resolve_artifacts(
                    os.path.join(td, "link.zip"), source, staging_parent=td
                )

    def test_rejects_member_order_metadata_checksum_and_archive_drift(self):
        with tempfile.TemporaryDirectory() as td:
            receiver, source = self._archives(td)
            versioned_receiver = os.path.join(
                td, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
            )
            os.rename(receiver, versioned_receiver)
            receiver = versioned_receiver
            with zipfile.ZipFile(receiver, "r") as zf:
                members = [
                    (info.filename, zf.read(info.filename)) for info in zf.infolist()
                ]
            reordered = os.path.join(
                td, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip.reordered.zip"
            )
            write_zip(reordered, list(reversed(members)))
            with self.assertRaises(artifacts.ArtifactError):
                artifacts.resolve_artifacts(reordered, source, staging_parent=td)
            bad_sums = os.path.join(
                td, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip.bad-sums.zip"
            )
            modified = [
                (name, b"0" * len(data) if name == "SHA256SUMS" else data)
                for name, data in members
            ]
            write_zip(bad_sums, modified)
            with self.assertRaises(artifacts.ArtifactError):
                artifacts.resolve_artifacts(bad_sums, source, staging_parent=td)
            staged = artifacts.resolve_artifacts(receiver, source, staging_parent=td)
            try:
                with open(receiver, "ab") as fh:
                    fh.write(b"drift")
                with self.assertRaises(artifacts.ArtifactError):
                    artifacts.revalidate_artifact_set(staged)
            finally:
                artifacts.cleanup_artifacts(staged)

    def test_rejects_non_hex_image_before_staging(self):
        with tempfile.TemporaryDirectory() as td:
            receiver, source = self._archives(td)
            versioned_receiver = os.path.join(
                td, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
            )
            os.rename(receiver, versioned_receiver)
            receiver = versioned_receiver
            with zipfile.ZipFile(source, "r") as zf:
                members = [
                    (info.filename, zf.read(info.filename)) for info in zf.infolist()
                ]
            bad_source = os.path.join(td, "bad-source.zip")
            bad_members = [
                (name, b"not an Intel HEX image\n" if name == "cpunet.hex" else data)
                for name, data in members
                if name != "SHA256SUMS"
            ]
            contents = dict(bad_members)
            bad_members.append(("SHA256SUMS", sums(contents)))
            write_zip(bad_source, bad_members)
            with self.assertRaises(artifacts.ArtifactError):
                artifacts.resolve_artifacts(receiver, bad_source, staging_parent=td)

    def test_rejects_archive_inside_repository(self):
        inside_repo = tempfile.mkdtemp(prefix=".rh4-artifact-test-", dir=REPO)
        try:
            receiver, source = self._archives(inside_repo)
            versioned_receiver = os.path.join(
                inside_repo, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
            )
            os.rename(receiver, versioned_receiver)
            with self.assertRaises(artifacts.ArtifactError):
                artifacts.resolve_artifacts(versioned_receiver, source)
        finally:
            for name in os.listdir(inside_repo):
                os.unlink(os.path.join(inside_repo, name))
            os.rmdir(inside_repo)

    def test_rejects_noncanonical_staging_parent(self):
        with tempfile.TemporaryDirectory() as td:
            receiver, source = self._archives(td)
            versioned_receiver = os.path.join(
                td, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
            )
            os.rename(receiver, versioned_receiver)
            real_parent = os.path.join(td, "real")
            os.mkdir(real_parent)
            alias = os.path.join(td, "alias")
            os.symlink(real_parent, alias)
            with self.assertRaises(artifacts.ArtifactError):
                artifacts.resolve_artifacts(
                    versioned_receiver, source, staging_parent=alias
                )

    def test_read_verified_archive_rejects_drift(self):
        with tempfile.TemporaryDirectory() as td:
            receiver, source = self._archives(td)
            versioned_receiver = os.path.join(
                td, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
            )
            os.rename(receiver, versioned_receiver)
            staged = artifacts.resolve_artifacts(
                versioned_receiver, source, staging_parent=td
            )
            try:
                with open(versioned_receiver, "rb") as fh:
                    self.assertEqual(
                        artifacts.read_verified_archive(staged.receiver), fh.read()
                    )
                with open(versioned_receiver, "ab") as fh:
                    fh.write(b"drift")
                with self.assertRaises(artifacts.ArtifactError):
                    artifacts.read_verified_archive(staged.receiver)
            finally:
                artifacts.cleanup_artifacts(staged)


class TestRh4Matrix(unittest.TestCase):
    def test_fixed_matrix_children_share_identity_and_aggregate_copies(self):
        with tempfile.TemporaryDirectory() as td:
            receiver, source = TestResolver()._archives(td)
            versioned_receiver = os.path.join(
                td, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
            )
            os.rename(receiver, versioned_receiver)
            receiver = versioned_receiver
            staged = artifacts.resolve_artifacts(receiver, source, staging_parent=td)
            try:
                cfg = os.path.join(td, "cfg")
                os.makedirs(cfg)
                fixture, binding = hil_fakes.write_fixture_binding(cfg)
                output = os.path.join(td, "output")
                os.makedirs(output)
                fake = FakeRows()
                coordinator = matrix.MatrixCoordinator(
                    matrix.MatrixDeps(
                        row_runner_factory=fake.factory,
                        environment=lambda argv, status: {
                            "argv": argv,
                            "status": status,
                        },
                    )
                )
                junit = os.path.join(output, "rh4.junit.xml")
                result = coordinator.run(
                    fixture,
                    binding,
                    output,
                    "rh4-artifact",
                    junit,
                    artifacts=staged,
                )
                self.assertEqual(result, ("passed", None, []))
                self.assertEqual(len(fake.calls), len(rows.rh3_schedule()))
                self.assertTrue(all(call[1] is staged for call in fake.calls))
                matrix_dir = os.path.join(output, "rh4-artifact")
                for source_path, name, identity in (
                    (receiver, "receiver-artifact.zip", staged.receiver),
                    (source, "source-artifact.zip", staged.source),
                ):
                    copied = os.path.join(matrix_dir, name)
                    with (
                        open(source_path, "rb") as source_file,
                        open(copied, "rb") as copied_file,
                    ):
                        copied_bytes = copied_file.read()
                        self.assertEqual(source_file.read(), copied_bytes)
                    self.assertEqual(
                        hashlib.sha256(copied_bytes).hexdigest(), identity.outer_sha256
                    )
                with open(
                    os.path.join(matrix_dir, "result.json"), encoding="utf-8"
                ) as fh:
                    result_json = json.load(fh)
                self.assertEqual(
                    result_json["artifacts"]["receiver"]["archive_sha256"],
                    staged.receiver.outer_sha256,
                )
                self.assertNotIn("ACCEPTED", json.dumps(result_json))
            finally:
                artifacts.cleanup_artifacts(staged)

    def test_cli_exposes_artifact_inputs_only_on_rh4_command(self):
        parser = cli.build_parser()
        args = parser.parse_args(
            [
                "run-rh4-matrix",
                "--fixture",
                "f",
                "--binding",
                "b",
                "--output-root",
                "/tmp/out",
                "--run-id",
                "rh4",
                "--junit",
                "/tmp/out/rh4.xml",
                "--receiver-artifact",
                "/tmp/receiver.zip",
                "--source-artifact",
                "/tmp/source.zip",
            ]
        )
        self.assertIs(args.func, cli.cmd_run_rh4_matrix)
        with self.assertRaises(cli.HilCliError):
            parser.parse_args(
                [
                    "run-rh3-matrix",
                    "--fixture",
                    "f",
                    "--binding",
                    "b",
                    "--output-root",
                    "/tmp/out",
                    "--run-id",
                    "rh3",
                    "--junit",
                    "/tmp/out/rh3.xml",
                    "--receiver-artifact",
                    "/tmp/x",
                ]
            )

    def test_matrix_revalidates_before_first_child(self):
        with tempfile.TemporaryDirectory() as td:
            receiver, source = TestResolver()._archives(td)
            versioned_receiver = os.path.join(
                td, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
            )
            os.rename(receiver, versioned_receiver)
            staged = artifacts.resolve_artifacts(
                versioned_receiver, source, staging_parent=td
            )
            try:
                with open(versioned_receiver, "ab") as fh:
                    fh.write(b"drift")
                cfg = os.path.join(td, "cfg")
                output = os.path.join(td, "output")
                os.makedirs(cfg)
                os.makedirs(output)
                fixture, binding = hil_fakes.write_fixture_binding(cfg)
                fake = FakeRows()
                coordinator = matrix.MatrixCoordinator(
                    matrix.MatrixDeps(row_runner_factory=fake.factory)
                )
                with self.assertRaises(artifacts.ArtifactError):
                    coordinator.run(
                        fixture,
                        binding,
                        output,
                        "rh4-drift",
                        os.path.join(output, "rh4.xml"),
                        artifacts=staged,
                    )
                self.assertEqual(fake.calls, [])
            finally:
                artifacts.cleanup_artifacts(staged)

    def test_matrix_copy_failure_stops_before_children(self):
        with tempfile.TemporaryDirectory() as td:
            receiver, source = TestResolver()._archives(td)
            versioned_receiver = os.path.join(
                td, "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
            )
            os.rename(receiver, versioned_receiver)
            staged = artifacts.resolve_artifacts(
                versioned_receiver, source, staging_parent=td
            )
            try:
                cfg = os.path.join(td, "cfg")
                output = os.path.join(td, "output")
                os.makedirs(cfg)
                os.makedirs(output)
                fixture, binding = hil_fakes.write_fixture_binding(cfg)
                fake = FakeRows()
                coordinator = matrix.MatrixCoordinator(
                    matrix.MatrixDeps(row_runner_factory=fake.factory)
                )
                original = artifacts.read_verified_archive

                def fail_copy(_identity):
                    raise artifacts.ArtifactError("injected copy drift")

                artifacts.read_verified_archive = fail_copy
                try:
                    outcome, boundary, cleanup = coordinator.run(
                        fixture,
                        binding,
                        output,
                        "rh4-copy-fail",
                        os.path.join(output, "rh4.xml"),
                        artifacts=staged,
                    )
                finally:
                    artifacts.read_verified_archive = original
                self.assertEqual(outcome, "failed")
                self.assertIn("injected copy drift", boundary)
                self.assertEqual(cleanup, [])
                self.assertEqual(fake.calls, [])
            finally:
                artifacts.cleanup_artifacts(staged)


if __name__ == "__main__":
    unittest.main()
