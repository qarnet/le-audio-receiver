#!/usr/bin/env python3
"""Public-boundary tests for package-firmware-release.py (stdlib unittest).

Launches the real CLI as a subprocess against independent temporary
repo/build/output fixtures, so every assertion observes the CLI, the
resulting filesystem, archives, JSON, checksums, and process
exit/output.  No private fields or helper-call assertions.

Each fixture copies the real packager script into a temporary
``repo/scripts/`` so the flashing-note lookup (repository-relative to the
script) is controlled per test.
"""

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

CLI_NAME = "package-firmware-release.py"
CLI_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), CLI_NAME)

VERSION = "0.1.0"
COMMIT = "0123456789abcdef0123456789abcdef01234567"
NCS = "v3.3.0"

ZIP54L15 = "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"

MEMBERS_54L15 = [
    "FLASHING.md",
    "cpuapp.hex",
    "flpr.hex",
    "release-manifest.json",
    "SHA256SUMS",
]

SUCCESS_STDOUT = (
    "package-firmware-release: wrote " + ZIP54L15 + "\n"
    "package-firmware-release: wrote SHA256SUMS\n"
)

ERROR_PREFIX = "package-firmware-release: error: "
STAGING_PREFIX = ".firmware-release-"


def hex_record(rec_type, address, data=b""):
    """One valid Intel HEX record line with a computed checksum."""
    payload = (
        bytes([len(data)])
        + address.to_bytes(2, "big")
        + bytes([rec_type])
        + bytes(data)
    )
    checksum = (-sum(payload)) & 0xFF
    return ":" + (payload + bytes([checksum])).hex().upper()


def eof_record():
    return ":00000001FF"


def make_hex(records=None):
    """Intel HEX file bytes ending in exactly one EOF record."""
    if records is None:
        records = [hex_record(0, 0, b"\x00\x01\x02"), eof_record()]
    return ("\n".join(records) + "\n").encode("ascii")


def default_images():
    """Two valid, mutually distinct nRF54L15 image inputs."""
    return {
        "nrf54l15/le-audio-receiver/zephyr/zephyr.hex": make_hex(
            [hex_record(0, 0, b"\x10\x20\x30"), eof_record()]
        ),
        # Extended linear address record (type 04) must be accepted.
        "nrf54l15/flpr/zephyr/zephyr.hex": make_hex(
            [
                hex_record(4, 0, b"\x01\x02"),
                hex_record(0, 0, b"\xde\xad\xbe\xef"),
                eof_record(),
            ]
        ),
    }


def default_notes():
    return {
        "nrf54l15-xiao.md": b"# nRF54L15 Xiao flashing note\n",
    }


def make_fixture(base, images=None, notes=None):
    """Create an independent repo/build fixture under base.  Returns the
    fixture repo root; the expected output dir is ``base/dist``."""
    repo = os.path.join(base, "repo")
    dirs = [
        os.path.join(repo, "scripts"),
        os.path.join(repo, "release", "flashing"),
        os.path.join(repo, "build", "nrf54l15", "le-audio-receiver", "zephyr"),
        os.path.join(repo, "build", "nrf54l15", "flpr", "zephyr"),
    ]
    for d in dirs:
        os.makedirs(d)
    shutil.copy2(CLI_SRC, os.path.join(repo, "scripts", CLI_NAME))
    for rel, data in (images if images is not None else default_images()).items():
        with open(os.path.join(repo, "build", rel), "wb") as fh:
            fh.write(data)
    for name, data in (notes if notes is not None else default_notes()).items():
        with open(os.path.join(repo, "release", "flashing", name), "wb") as fh:
            fh.write(data)
    return repo


def run_cli(
    repo, output_dir, version=VERSION, commit=COMMIT, ncs=NCS, extra=(), preexec_fn=None
):
    script = os.path.join(repo, "scripts", CLI_NAME)
    cmd = [
        sys.executable,
        script,
        "--version",
        version,
        "--git-commit",
        commit,
        "--ncs-version",
        ncs,
        "--build-root",
        os.path.join(repo, "build"),
        "--output-dir",
        output_dir,
    ] + list(extra)
    return subprocess.run(
        cmd, capture_output=True, text=True, cwd=repo, preexec_fn=preexec_fn
    )


def read_zip(path):
    with zipfile.ZipFile(path, "r") as zf:
        return [(info, zf.read(info.filename)) for info in zf.infolist()]


def limit_file_size_100():
    """preexec_fn for the Linux RLIMIT_FSIZE regression: cap the child's
    largest file at 100 bytes so the first ZIP write fails with
    ``OSError: [Errno 27] File too large`` after staging creation."""
    resource.setrlimit(resource.RLIMIT_FSIZE, (100, 100))


class TestHappyPath(unittest.TestCase):
    """Tests 1-7: successful packaging produces the exact artifact set."""

    def test_happy_path_creates_one_zip_and_top_checksum(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist = os.path.join(tmp, "dist")
            res = run_cli(repo, dist)
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertEqual(sorted(os.listdir(dist)), sorted([ZIP54L15, "SHA256SUMS"]))
            for name in (ZIP54L15,):
                self.assertGreater(os.path.getsize(os.path.join(dist, name)), 0)
            self.assertGreater(os.path.getsize(os.path.join(dist, "SHA256SUMS")), 0)

    def test_zip_member_names_and_order_exact(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist = os.path.join(tmp, "dist")
            res = run_cli(repo, dist)
            self.assertEqual(res.returncode, 0, res.stderr)
            with zipfile.ZipFile(os.path.join(dist, ZIP54L15)) as zf:
                self.assertEqual(zf.namelist(), MEMBERS_54L15)

    def test_manifest_schema_exact(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            images = default_images()
            dist = os.path.join(tmp, "dist")
            res = run_cli(repo, dist)
            self.assertEqual(res.returncode, 0, res.stderr)
            expected_54l15 = self._expected_manifest(
                "nrf54l15-xiao",
                "nrf54l15dk/nrf54l15/cpuapp",
                [
                    (
                        "cpuapp.hex",
                        0,
                        "nrf54l15/le-audio-receiver/zephyr/zephyr.hex",
                        "cpuapp",
                        images["nrf54l15/le-audio-receiver/zephyr/zephyr.hex"],
                    ),
                    (
                        "flpr.hex",
                        1,
                        "nrf54l15/flpr/zephyr/zephyr.hex",
                        "flpr",
                        images["nrf54l15/flpr/zephyr/zephyr.hex"],
                    ),
                ],
            )
            with zipfile.ZipFile(os.path.join(dist, ZIP54L15)) as zf:
                self._assert_manifest(zf.read("release-manifest.json"), expected_54l15)

    def _expected_manifest(self, target_id, board, image_tuples):
        return {
            "git_commit": COMMIT,
            "images": [
                {
                    "filename": name,
                    "flash_order": order,
                    "original_build_path": path,
                    "role": role,
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "size": len(data),
                }
                for name, order, path, role, data in sorted(
                    image_tuples, key=lambda t: t[1]
                )
            ],
            "ncs_version": NCS,
            "project": "le-audio-receiver",
            "schema_version": 1,
            "target": {"board": board, "id": target_id},
            "version": VERSION,
        }

    def _assert_manifest(self, raw, expected):
        manifest = json.loads(raw.decode("utf-8"))
        self.assertEqual(manifest, expected)
        # Exact serialization: sorted keys, two-space indent, trailing newline.
        canonical = json.dumps(expected, indent=2, sort_keys=True) + "\n"
        self.assertEqual(raw.decode("utf-8"), canonical)
        self.assertEqual(list(manifest.keys()), sorted(manifest.keys()))
        self.assertTrue(raw.endswith(b"\n"))

    def test_checksums_match_bytes_and_exact_format(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist = os.path.join(tmp, "dist")
            res = run_cli(repo, dist)
            self.assertEqual(res.returncode, 0, res.stderr)
            with open(os.path.join(dist, "SHA256SUMS"), "rb") as fh:
                top_text = fh.read().decode("ascii")
            self.assertTrue(top_text.endswith("\n"))
            top = {}
            for line in top_text.split("\n")[:-1]:
                self.assertRegex(line, r"^[0-9a-f]{64}  \S+$")
                digest, name = line.split("  ", 1)
                self.assertNotIn(name, top, "duplicate top-level checksum line")
                top[name] = digest
            self.assertEqual(sorted(top), [ZIP54L15])
            for zip_name, members in ((ZIP54L15, MEMBERS_54L15),):
                zip_path = os.path.join(dist, zip_name)
                with open(zip_path, "rb") as fh:
                    top_digest = hashlib.sha256(fh.read()).hexdigest()
                self.assertEqual(top[zip_name], top_digest)
                hashed_members = [m for m in members if m != "SHA256SUMS"]
                with zipfile.ZipFile(zip_path) as zf:
                    sums_text = zf.read("SHA256SUMS").decode("ascii")
                    self.assertTrue(sums_text.endswith("\n"))
                    self.assertEqual(sums_text.count("\n"), len(hashed_members))
                    lines = {}
                    for line in sums_text.split("\n")[:-1]:
                        self.assertRegex(line, r"^[0-9a-f]{64}  \S+$")
                        digest, name = line.split("  ", 1)
                        self.assertNotIn(name, lines, "duplicate checksum line")
                        lines[name] = digest
                    self.assertEqual(sorted(lines), sorted(hashed_members))
                    for name in hashed_members:
                        self.assertEqual(
                            lines[name], hashlib.sha256(zf.read(name)).hexdigest()
                        )
                    # SHA256SUMS must not hash itself; the member exists but
                    # carries no self-referential line.
                    self.assertNotIn("SHA256SUMS", lines)

    def test_repeat_packaging_byte_identical_across_locations(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist_a = os.path.join(tmp, "dist_a")
            dist_b = os.path.join(tmp, "dist_b")
            res_a = run_cli(repo, dist_a)
            res_b = run_cli(repo, dist_b)
            self.assertEqual(res_a.returncode, 0, res_a.stderr)
            self.assertEqual(res_b.returncode, 0, res_b.stderr)
            for name in (ZIP54L15, "SHA256SUMS"):
                with open(os.path.join(dist_a, name), "rb") as fh:
                    a = fh.read()
                with open(os.path.join(dist_b, name), "rb") as fh:
                    b = fh.read()
                self.assertEqual(a, b, "bytes differ for %s" % name)

    def test_archive_members_fixed_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist = os.path.join(tmp, "dist")
            res = run_cli(repo, dist)
            self.assertEqual(res.returncode, 0, res.stderr)
            for zip_name in (ZIP54L15,):
                with zipfile.ZipFile(os.path.join(dist, zip_name)) as zf:
                    infos = zf.infolist()
                    self.assertGreater(len(infos), 0)
                    for info in infos:
                        self.assertEqual(info.date_time, (1980, 1, 1, 0, 0, 0))
                        self.assertEqual(info.create_system, 3, "unix creator metadata")
                        self.assertEqual(
                            info.external_attr >> 16, 0o100644, "mode 0644"
                        )
                        self.assertEqual(info.extra, b"", "no extra fields")
                        self.assertEqual(info.comment, b"", "no comment")
                        self.assertFalse(info.is_dir(), "no directory entries")

    def test_no_absolute_fixture_paths_in_archives_or_manifests(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist = os.path.join(tmp, "dist")
            res = run_cli(repo, dist)
            self.assertEqual(res.returncode, 0, res.stderr)
            base = os.path.abspath(tmp)
            for zip_name in (ZIP54L15,):
                for info, data in read_zip(os.path.join(dist, zip_name)):
                    self.assertNotIn("/", info.filename, "member name not a basename")
                    self.assertNotIn("..", info.filename)
                    self.assertFalse(info.filename.startswith("/"))
                    self.assertNotIn(
                        base.encode("utf-8"),
                        data,
                        "absolute path leak in %s" % info.filename,
                    )
            with open(os.path.join(dist, "SHA256SUMS"), "rb") as fh:
                self.assertNotIn(base.encode("utf-8"), fh.read())


class TestFailureAtomicity(unittest.TestCase):
    """Tests 8-13: every rejected input or pre-existing output fails with
    the final output directory absent or untouched."""

    def test_missing_each_hex_input_fails_output_absent(self):
        for rel in (
            "nrf54l15/le-audio-receiver/zephyr/zephyr.hex",
            "nrf54l15/flpr/zephyr/zephyr.hex",
        ):
            with self.subTest(image=rel):
                with tempfile.TemporaryDirectory() as tmp:
                    repo = make_fixture(tmp)
                    os.unlink(os.path.join(repo, "build", rel))
                    dist = os.path.join(tmp, "dist")
                    res = run_cli(repo, dist)
                    self.assertNotEqual(res.returncode, 0)
                    self.assertFalse(os.path.exists(dist), "output must stay absent")

    def test_invalid_hex_variants_fail_output_absent(self):
        cases = {
            "empty": b"",
            "non_ascii": b":00000001FF\xff\n",
            "malformed_record_no_colon": b"0000000001FF\n",
            "odd_hex_length": b":0000001\n",
            "non_hex_chars": b":00ZZ0001FF\n",
            "record_too_short": b":\n",
            "bad_byte_count": b":03000000AABB99\n",
            "bad_checksum": b":02000000AABB00\n",
            "missing_eof": b":02000000AABB99\n",
            "duplicate_eof": b":02000000AABB99\n:00000001FF\n:00000001FF\n",
            "record_after_eof": b":02000000AABB99\n:00000001FF\n:00000000\n",
            "blank_record": b":02000000AABB99\n\n:00000001FF\n",
        }
        for name, content in cases.items():
            with self.subTest(variant=name):
                with tempfile.TemporaryDirectory() as tmp:
                    repo = make_fixture(tmp)
                    with open(
                        os.path.join(
                            repo,
                            "build",
                            "nrf54l15",
                            "le-audio-receiver",
                            "zephyr",
                            "zephyr.hex",
                        ),
                        "wb",
                    ) as fh:
                        fh.write(content)
                    dist = os.path.join(tmp, "dist")
                    res = run_cli(repo, dist)
                    self.assertNotEqual(res.returncode, 0, "must fail: %s" % name)
                    self.assertFalse(os.path.exists(dist), "output must stay absent")

    def test_symlinked_hex_input_fails_output_absent(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            target = os.path.join(
                repo, "build", "nrf54l15", "flpr", "zephyr", "zephyr.hex"
            )
            link = os.path.join(
                repo,
                "build",
                "nrf54l15",
                "le-audio-receiver",
                "zephyr",
                "zephyr.hex",
            )
            os.unlink(link)
            os.symlink(target, link)
            dist = os.path.join(tmp, "dist")
            res = run_cli(repo, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("symlink", res.stderr.lower())
            self.assertFalse(os.path.exists(dist))

    def test_invalid_flashing_notes_fail_output_absent(self):
        variants = {
            "missing": None,
            "empty": b"",
            "invalid_utf8": b"\xff\xfe\x00 not text\n",
        }
        for note_name in ("nrf54l15-xiao.md",):
            for variant, content in variants.items():
                with self.subTest(note=note_name, variant=variant):
                    with tempfile.TemporaryDirectory() as tmp:
                        repo = make_fixture(tmp)
                        note_path = os.path.join(repo, "release", "flashing", note_name)
                        if content is None:
                            os.unlink(note_path)
                        else:
                            with open(note_path, "wb") as fh:
                                fh.write(content)
                        dist = os.path.join(tmp, "dist")
                        res = run_cli(repo, dist)
                        self.assertNotEqual(res.returncode, 0)
                        self.assertFalse(os.path.exists(dist))

    def test_symlinked_flashing_note_fails_output_absent(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            other = os.path.join(repo, "other-note.md")
            with open(other, "wb") as fh:
                fh.write(b"# Other flashing note\n")
            link = os.path.join(repo, "release", "flashing", "nrf54l15-xiao.md")
            os.unlink(link)
            os.symlink(other, link)
            dist = os.path.join(tmp, "dist")
            res = run_cli(repo, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("symlink", res.stderr.lower())
            self.assertFalse(os.path.exists(dist))

    def test_invalid_metadata_fails_before_output_creation(self):
        bad_versions = ["1.0", "01.0.0", "1.0.0.0", "1.0.0-beta", "v1.0.0", ""]
        bad_commits = [
            "short",
            "ABCDEF0123456789abcdef0123456789abcdef01234567",
            "0123456789abcdef0123456789abcdef0123456",
            "0123456789abcdef0123456789abcdef012345678",
            "",
        ]
        bad_ncs = ["3.3.0", "v01.0.0", "v1.0", "v1.0.0.0", "v1.0.0-beta", ""]
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist = os.path.join(tmp, "dist")
            for bad in bad_versions:
                with self.subTest(field="version", value=bad):
                    res = run_cli(repo, dist, version=bad)
                    self.assertNotEqual(res.returncode, 0)
                    self.assertFalse(os.path.exists(dist))
            for bad in bad_commits:
                with self.subTest(field="commit", value=bad):
                    res = run_cli(repo, dist, commit=bad)
                    self.assertNotEqual(res.returncode, 0)
                    self.assertFalse(os.path.exists(dist))
            for bad in bad_ncs:
                with self.subTest(field="ncs", value=bad):
                    res = run_cli(repo, dist, ncs=bad)
                    self.assertNotEqual(res.returncode, 0)
                    self.assertFalse(os.path.exists(dist))

    def test_existing_output_dir_rejected_sentinel_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist = os.path.join(tmp, "dist")
            os.makedirs(dist)
            sentinel = os.path.join(dist, "sentinel.txt")
            with open(sentinel, "w") as fh:
                fh.write("keep-me")
            res = run_cli(repo, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn(ERROR_PREFIX, res.stderr)
            with open(sentinel) as fh:
                self.assertEqual(fh.read(), "keep-me")

    def test_handled_failure_leaves_no_staging_sibling(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            with open(
                os.path.join(
                    repo,
                    "build",
                    "nrf54l15",
                    "le-audio-receiver",
                    "zephyr",
                    "zephyr.hex",
                ),
                "wb",
            ) as fh:
                fh.write(b"garbage-not-hex")
            dist = os.path.join(tmp, "dist")
            res = run_cli(repo, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertFalse(os.path.exists(dist))
            leftovers = [
                name for name in os.listdir(tmp) if name.startswith(STAGING_PREFIX)
            ]
            self.assertEqual(leftovers, [])


class TestTargetIsolation(unittest.TestCase):
    """Test 14: changing an nRF54L15 image changes its ZIP and checksum."""

    def test_nrf54l15_image_change_affects_its_zip_and_checksum(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist_a = os.path.join(tmp, "dist_a")
            res_a = run_cli(repo, dist_a)
            self.assertEqual(res_a.returncode, 0, res_a.stderr)
            # Change the nRF54L15 cpuapp image only.
            with open(
                os.path.join(
                    repo,
                    "build",
                    "nrf54l15",
                    "le-audio-receiver",
                    "zephyr",
                    "zephyr.hex",
                ),
                "wb",
            ) as fh:
                fh.write(make_hex([hex_record(0, 0, b"\x99\x88\x77"), eof_record()]))
            dist_b = os.path.join(tmp, "dist_b")
            res_b = run_cli(repo, dist_b)
            self.assertEqual(res_b.returncode, 0, res_b.stderr)
            for name in (ZIP54L15, "SHA256SUMS"):
                with open(os.path.join(dist_a, name), "rb") as fh:
                    a = fh.read()
                with open(os.path.join(dist_b, name), "rb") as fh:
                    b = fh.read()
                self.assertNotEqual(a, b, "%s must change" % name)
            with zipfile.ZipFile(os.path.join(dist_a, ZIP54L15)) as zf:
                manifest_a = json.loads(zf.read("release-manifest.json"))
            with zipfile.ZipFile(os.path.join(dist_b, ZIP54L15)) as zf:
                manifest_b = json.loads(zf.read("release-manifest.json"))
            self.assertNotEqual(
                manifest_a["images"][0]["sha256"], manifest_b["images"][0]["sha256"]
            )
            self.assertEqual(
                manifest_a["images"][1]["sha256"], manifest_b["images"][1]["sha256"]
            )


class TestProcessContract(unittest.TestCase):
    """Tests 15-16: stable stderr diagnostics and deterministic stdout."""

    def test_error_paths_stable_prefix_no_traceback(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist = os.path.join(tmp, "dist")
            # Missing required arguments.
            script = os.path.join(repo, "scripts", CLI_NAME)
            res = subprocess.run(
                [sys.executable, script, "--version", VERSION],
                capture_output=True,
                text=True,
                cwd=repo,
            )
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertNotIn("Traceback", res.stderr)
            # Invalid version.
            res = run_cli(repo, dist, version="not-a-version")
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertNotIn("Traceback", res.stderr)
            # Missing image.
            os.unlink(
                os.path.join(
                    repo,
                    "build",
                    "nrf54l15",
                    "le-audio-receiver",
                    "zephyr",
                    "zephyr.hex",
                )
            )
            res = run_cli(repo, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertNotIn("Traceback", res.stderr)
            # Existing output directory.
            os.makedirs(dist)
            res = run_cli(repo, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertNotIn("Traceback", res.stderr)
            # Invalid hex content.
            shutil.rmtree(dist)
            with open(
                os.path.join(
                    repo,
                    "build",
                    "nrf54l15",
                    "le-audio-receiver",
                    "zephyr",
                    "zephyr.hex",
                ),
                "wb",
            ) as fh:
                fh.write(b":02000000AABB00\n")
            res = run_cli(repo, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertNotIn("Traceback", res.stderr)

    def test_success_stdout_deterministic_output_relative(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist_a = os.path.join(tmp, "dist_a")
            dist_b = os.path.join(tmp, "dist_b")
            res_a = run_cli(repo, dist_a)
            res_b = run_cli(repo, dist_b)
            self.assertEqual(res_a.returncode, 0, res_a.stderr)
            self.assertEqual(res_b.returncode, 0, res_b.stderr)
            self.assertEqual(res_a.stdout, SUCCESS_STDOUT)
            self.assertEqual(res_a.stdout, res_b.stdout)
            self.assertEqual(res_a.stderr, "")
            for line in res_a.stdout.splitlines():
                self.assertNotIn(
                    "/", line, "stdout must name output-relative files only"
                )
                self.assertNotIn("..", line)


class TestOutputIoFailure(unittest.TestCase):
    """Linux RLIMIT_FSIZE regression: an output-side OSError during ZIP
    writing must produce the stable stderr diagnostic without a traceback
    and leave no staging sibling or final output directory."""

    @unittest.skipUnless(
        sys.platform.startswith("linux"), "Linux RLIMIT_FSIZE regression"
    )
    def test_zip_write_failure_clean_diagnostic_and_cleanup(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_fixture(tmp)
            dist = os.path.join(tmp, "dist")
            res = run_cli(repo, dist, preexec_fn=limit_file_size_100)
            self.assertNotEqual(res.returncode, 0, res.stderr)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertNotIn("Traceback", res.stderr)
            self.assertIn("File too large", res.stderr)
            self.assertFalse(os.path.exists(dist), "output must stay absent")
            leftovers = [
                name for name in os.listdir(tmp) if name.startswith(STAGING_PREFIX)
            ]
            self.assertEqual(leftovers, [])


if __name__ == "__main__":
    unittest.main()
