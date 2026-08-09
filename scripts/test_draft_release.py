#!/usr/bin/env python3
"""Public-boundary tests for prepare-draft-release.py (stdlib unittest).

Launches the real CLI as a subprocess against valid FR1 artifact sets
built by the real FR1 packager in independent temporary fixtures, then
asserts CLI output, the resulting filesystem, JSON, ZIP, checksum, and
process contracts.  No private helper from either CLI is imported; every
assertion observes a public boundary.  No production build directory is
used.
"""

import hashlib
import json
import os
import resource
import subprocess
import sys
import tempfile
import unittest
import zipfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREPARE_SCRIPT = os.path.join(REPO_ROOT, "scripts", "prepare-draft-release.py")
PACKAGER_SCRIPT = os.path.join(REPO_ROOT, "scripts", "package-firmware-release.py")

VERSION = "0.1.0"
TAG = "v0.1.0"
COMMIT = "0123456789abcdef0123456789abcdef01234567"
NCS = "v3.3.0"
REPOSITORY = "qarnet/le-audio-receiver"
WORKFLOW = "Firmware build"
WORKFLOW_REF = REPOSITORY + "/.github/workflows/firmware-build.yml@refs/tags/" + TAG
RUN_ID = "123456"
RUN_ATTEMPT = "1"

ZIP5340 = "le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip"
ZIP54L15 = "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
TOP_SUMS = "SHA256SUMS"
OUTPUT_DIR = "release-metadata"

SUCCESS_STDOUT = (
    "prepare-draft-release: wrote release-provenance.json\n"
    "prepare-draft-release: wrote release-notes.md\n"
)
ERROR_PREFIX = "prepare-draft-release: error: "
STAGING_PREFIX = ".draft-release-"

TOOLCHAIN_IMAGE = (
    "ghcr.io/nrfconnect/sdk-nrf-toolchain@sha256:"
    "f24d8932ff081ebcd8da9c248f4449bdabe461c0620a7a4ac9e95eb577ba2276"
)
TOOLCHAIN_COMMIT = "ba167d9f3db4abbdc9b67887ca3ea66c64f2d956"


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
    """Four valid, mutually distinct image inputs for both targets."""
    return {
        "nrf5340/merged.hex": make_hex(
            [
                hex_record(0, 0, b"\xaa\xbb"),
                hex_record(0, 0x100, b"\x01\x02\x03\x04"),
                eof_record(),
            ]
        ),
        "nrf5340/merged_CPUNET.hex": make_hex(
            [hex_record(0, 0, b"\xcc"), eof_record()]
        ),
        "nrf54l15/le-audio-receiver/zephyr/zephyr.hex": make_hex(
            [hex_record(0, 0, b"\x10\x20\x30"), eof_record()]
        ),
        "nrf54l15/flpr/zephyr/zephyr.hex": make_hex(
            [
                hex_record(4, 0, b"\x01\x02"),
                hex_record(0, 0, b"\xde\xad\xbe\xef"),
                eof_record(),
            ]
        ),
    }


def build_dist(tmp, images=None):
    """Build a valid FR1 artifact set under tmp by invoking the real FR1
    packager as a subprocess against a temporary build root.  The real
    flashing notes are resolved by the packager from the repository."""
    build_root = os.path.join(tmp, "build")
    for rel, data in (images if images is not None else default_images()).items():
        path = os.path.join(build_root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as fh:
            fh.write(data)
    dist = os.path.join(tmp, "dist")
    res = subprocess.run(
        [
            sys.executable,
            PACKAGER_SCRIPT,
            "--version",
            VERSION,
            "--git-commit",
            COMMIT,
            "--ncs-version",
            NCS,
            "--build-root",
            build_root,
            "--output-dir",
            dist,
        ],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
    )
    if res.returncode != 0:
        raise AssertionError("fixture packager failed: %s" % res.stderr)
    return dist


def run_prepare(
    tmp,
    dist,
    output_dir=None,
    tag=TAG,
    version=VERSION,
    commit=COMMIT,
    ncs=NCS,
    repository=REPOSITORY,
    workflow=WORKFLOW,
    workflow_ref=WORKFLOW_REF,
    run_id=RUN_ID,
    run_attempt=RUN_ATTEMPT,
    preexec_fn=None,
):
    if output_dir is None:
        output_dir = os.path.join(tmp, OUTPUT_DIR)
    cmd = [
        sys.executable,
        PREPARE_SCRIPT,
        "--tag",
        tag,
        "--version",
        version,
        "--git-commit",
        commit,
        "--ncs-version",
        ncs,
        "--repository",
        repository,
        "--workflow",
        workflow,
        "--workflow-ref",
        workflow_ref,
        "--run-id",
        run_id,
        "--run-attempt",
        run_attempt,
        "--artifact-dir",
        dist,
        "--output-dir",
        output_dir,
    ]
    return subprocess.run(
        cmd, capture_output=True, text=True, cwd=REPO_ROOT, preexec_fn=preexec_fn
    )


def zip_members(path):
    with zipfile.ZipFile(path, "r") as zf:
        return [(info.filename, zf.read(info.filename)) for info in zf.infolist()]


def rewrite_zip(path, members):
    """Rebuild a ZIP with the given ordered (name, data) member list,
    preserving deterministic member metadata."""
    with zipfile.ZipFile(
        path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as zf:
        for name, data in members:
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            zf.writestr(info, data)


def sums_text(items):
    lines = ["%s  %s" % (digest, name) for name, digest in sorted(items)]
    return ("\n".join(lines) + "\n").encode("ascii")


def replace_member(zip_path, name, data):
    """Replace one member's bytes in place, preserving member order."""
    members = []
    with zipfile.ZipFile(zip_path, "r") as zf:
        for info in zf.infolist():
            payload = zf.read(info.filename)
            if info.filename == name:
                payload = data
            members.append((info.filename, payload))
    rewrite_zip(zip_path, members)


def rehash_zip(zip_path):
    """Recompute the internal SHA256SUMS member from the current member
    bytes (member order preserved, SHA256SUMS stays last)."""
    members = zip_members(zip_path)
    items = [
        (name, hashlib.sha256(data).hexdigest())
        for name, data in members
        if name != "SHA256SUMS"
    ]
    members = [(name, data) for name, data in members if name != "SHA256SUMS"]
    members.append(("SHA256SUMS", sums_text(items)))
    rewrite_zip(zip_path, members)


def rehash_top_sums(dist):
    """Recompute the top-level SHA256SUMS from the current dist file
    bytes (ZIPs only, never itself), so zip-level checks are reached
    after a mutation."""
    items = []
    for name in sorted(os.listdir(dist)):
        if name == TOP_SUMS:
            continue
        path = os.path.join(dist, name)
        if os.path.islink(path) or not os.path.isfile(path):
            continue
        with open(path, "rb") as fh:
            items.append((name, hashlib.sha256(fh.read()).hexdigest()))
    with open(os.path.join(dist, TOP_SUMS), "w", encoding="ascii") as fh:
        fh.write(sums_text(items).decode("ascii"))


def get_manifest(zip_path):
    with zipfile.ZipFile(zip_path, "r") as zf:
        return json.loads(zf.read("release-manifest.json").decode("utf-8"))


def put_manifest(zip_path, manifest):
    raw = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
    replace_member(zip_path, "release-manifest.json", raw)


def corrupt_zip_bytes(zip_path):
    with open(zip_path, "rb") as fh:
        data = bytearray(fh.read())
    mid = len(data) // 2
    data[mid] ^= 0xFF
    with open(zip_path, "wb") as fh:
        fh.write(bytes(data))


def expected_provenance(dist):
    entries = {}
    for name in sorted(os.listdir(dist)):
        path = os.path.join(dist, name)
        with open(path, "rb") as fh:
            data = fh.read()
        entries[name] = data
    artifacts = [
        {
            "filename": name,
            "sha256": hashlib.sha256(entries[name]).hexdigest(),
            "size": len(entries[name]),
        }
        for name in sorted(entries)
    ]
    return {
        "artifacts": artifacts,
        "build": {
            "git_commit": COMMIT,
            "ncs_version": NCS,
            "project": "le-audio-receiver",
            "version": VERSION,
        },
        "ci": {
            "repository": REPOSITORY,
            "run_attempt": 1,
            "run_id": 123456,
            "workflow": WORKFLOW,
            "workflow_ref": WORKFLOW_REF,
        },
        "release": {"draft": True, "tag": TAG},
        "schema_version": 1,
        "toolchain": {
            "container_image": TOOLCHAIN_IMAGE,
            "sdk_nrf_commit": TOOLCHAIN_COMMIT,
        },
    }


class TestHappyPath(unittest.TestCase):
    """Tests 1-2: successful preparation produces the exact metadata set."""

    def test_happy_path_exact_metadata_stdout_and_notes(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            output = os.path.join(tmp, OUTPUT_DIR)
            res = run_prepare(tmp, dist, output)
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertEqual(res.stderr, "")
            self.assertEqual(res.stdout, SUCCESS_STDOUT)
            self.assertEqual(
                sorted(os.listdir(output)),
                ["release-notes.md", "release-provenance.json"],
            )
            for line in res.stdout.splitlines():
                self.assertNotIn("/", line, "stdout must name files only")
                self.assertNotIn("..", line)
                self.assertNotIn(tmp, line)

            with open(
                os.path.join(output, "release-provenance.json"), "r", encoding="utf-8"
            ) as fh:
                raw = fh.read()
            expected = expected_provenance(dist)
            self.assertEqual(json.loads(raw), expected)
            canonical = json.dumps(expected, indent=2, sort_keys=True) + "\n"
            self.assertEqual(raw, canonical)
            self.assertEqual(
                list(json.loads(raw).keys()), sorted(json.loads(raw).keys())
            )
            names = [a["filename"] for a in json.loads(raw)["artifacts"]]
            self.assertEqual(
                names,
                sorted(names),
                "artifacts must be sorted by filename",
            )
            self.assertEqual(names, ["SHA256SUMS", ZIP5340, ZIP54L15])

            with open(
                os.path.join(output, "release-notes.md"), "r", encoding="utf-8"
            ) as fh:
                notes = fh.read()
            self.assertIn("# LE Audio Receiver v0.1.0", notes)
            self.assertIn("draft factory-flash candidate", notes)
            self.assertIn(COMMIT, notes)
            self.assertIn("v3.3.0", notes)
            self.assertIn(
                "https://github.com/qarnet/le-audio-receiver/actions/runs/123456",
                notes,
            )
            self.assertIn(ZIP5340, notes)
            self.assertIn(ZIP54L15, notes)
            self.assertIn("SHA256SUMS", notes)
            self.assertIn("companion", notes)
            self.assertIn("preserves settings and bonds", notes)
            self.assertIn("clean erase", notes)
            self.assertIn(
                "Do not publish this draft until FR4 exact-asset hardware "
                "acceptance passes.",
                notes,
            )
            self.assertIn("MCUboot/DFU is not included", notes)
            self.assertNotIn("\u2014", notes, "no em dash in release notes")

    def test_repeated_identical_input_byte_identical_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            out_a = os.path.join(tmp, "meta_a")
            out_b = os.path.join(tmp, "meta_b")
            res_a = run_prepare(tmp, dist, out_a)
            res_b = run_prepare(tmp, dist, out_b)
            self.assertEqual(res_a.returncode, 0, res_a.stderr)
            self.assertEqual(res_b.returncode, 0, res_b.stderr)
            self.assertEqual(res_a.stdout, res_b.stdout)
            for name in ("release-provenance.json", "release-notes.md"):
                with open(os.path.join(out_a, name), "rb") as fh:
                    a = fh.read()
                with open(os.path.join(out_b, name), "rb") as fh:
                    b = fh.read()
                self.assertEqual(a, b, "metadata differs for %s" % name)


class TestInvalidInputs(unittest.TestCase):
    """Test 3: invalid or mismatched inputs fail before output creation."""

    def test_invalid_and_mismatched_inputs_fail_before_output(self):
        cases = [
            ("tag_plain", dict(tag="0.1.0")),
            ("tag_short", dict(tag="v1.0")),
            ("tag_leading_zero", dict(tag="v01.0.0")),
            ("tag_extra_part", dict(tag="v0.1.0.0")),
            ("tag_suffix_mismatch", dict(tag="v0.2.0")),
            ("version_short", dict(version="1.0")),
            ("version_leading_zero", dict(version="01.0.0")),
            ("version_extra_part", dict(version="0.1.0.0")),
            (
                "commit_uppercase",
                dict(commit="ABCDEF0123456789abcdef0123456789abcdef01234567"),
            ),
            ("commit_short", dict(commit="0123456789abcdef0123456789abcdef0123456")),
            ("ncs_wrong_minor", dict(ncs="v3.3.1")),
            ("ncs_no_v", dict(ncs="3.3.0")),
            ("repository_other", dict(repository="someone/else")),
            ("workflow_other", dict(workflow="Other workflow")),
            (
                "workflow_ref_wrong_tag",
                dict(
                    workflow_ref=REPOSITORY
                    + "/.github/workflows/firmware-build.yml@refs/tags/v9.9.9"
                ),
            ),
            (
                "workflow_ref_wrong_repo",
                dict(
                    workflow_ref="other/repo/.github/workflows/firmware-build.yml@refs/tags/v0.1.0"
                ),
            ),
            (
                "workflow_ref_wrong_path",
                dict(
                    workflow_ref=REPOSITORY
                    + "/.github/workflows/other.yml@refs/tags/v0.1.0"
                ),
            ),
            ("run_id_zero", dict(run_id="0")),
            ("run_id_leading_zero", dict(run_id="0123")),
            ("run_id_negative", dict(run_id="-1")),
            ("run_id_non_numeric", dict(run_id="abc")),
            ("run_attempt_zero", dict(run_attempt="0")),
            ("run_attempt_leading_zero", dict(run_attempt="01")),
            ("tag_control_char", dict(tag="v0.1.0\n")),
            ("workflow_control_char", dict(workflow="Firmware\nbuild")),
            (
                "repository_control_char",
                dict(repository="qarnet/le-audio-receiver\t"),
            ),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            output = os.path.join(tmp, OUTPUT_DIR)
            for name, kwargs in cases:
                with self.subTest(variant=name):
                    res = run_prepare(tmp, dist, output, **kwargs)
                    self.assertNotEqual(res.returncode, 0, "must fail: %s" % name)
                    self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
                    self.assertNotIn("Traceback", res.stderr)
                    self.assertEqual(res.stdout, "")
                    self.assertFalse(
                        os.path.exists(output), "output must stay absent: %s" % name
                    )


class TestArtifactDirectory(unittest.TestCase):
    """Test 4: missing/extra/directory/symlink/nonregular artifact entries
    fail."""

    def test_missing_entry_fails(self):
        for name in (TOP_SUMS, ZIP5340, ZIP54L15):
            with self.subTest(missing=name):
                with tempfile.TemporaryDirectory() as tmp:
                    dist = build_dist(tmp)
                    os.unlink(os.path.join(dist, name))
                    res = run_prepare(tmp, dist)
                    self.assertNotEqual(res.returncode, 0, "must fail: %s" % name)
                    self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
                    self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_extra_entry_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            with open(os.path.join(dist, "extra.txt"), "w") as fh:
                fh.write("extra")
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_directory_entry_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            os.mkdir(os.path.join(dist, "subdir"))
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_symlink_entry_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            target = os.path.join(dist, ZIP5340)
            link = os.path.join(dist, ZIP54L15)
            os.unlink(link)
            os.symlink(target, link)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("symlink", res.stderr.lower())
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_artifact_dir_is_file_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            os.unlink(os.path.join(dist, ZIP54L15))
            path = os.path.join(dist, ZIP54L15)
            with open(path, "w") as fh:
                fh.write("not a directory")
            res = run_prepare(tmp, dist, output_dir=os.path.join(tmp, "meta_file"))
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)


class TestTopChecksum(unittest.TestCase):
    """Test 5: malformed/unsorted/duplicate/wrong-name/wrong-digest top
    checksum files fail."""

    def _write_top_sums(self, dist, text):
        with open(os.path.join(dist, TOP_SUMS), "w", encoding="ascii") as fh:
            fh.write(text)

    def _top_sums_text(self, dist):
        with open(os.path.join(dist, TOP_SUMS), "r", encoding="ascii") as fh:
            return fh.read()

    def test_malformed_line_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            self._write_top_sums(dist, "not a checksum line\n")
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_short_digest_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            self._write_top_sums(dist, "abcd  %s\n" % ZIP5340)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)

    def test_unsorted_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            lines = self._top_sums_text(dist).split("\n")[:-1]
            self.assertEqual(len(lines), 2)
            self._write_top_sums(dist, lines[1] + "\n" + lines[0] + "\n")
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("sorted", res.stderr.lower())
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_duplicate_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            lines = self._top_sums_text(dist).split("\n")[:-1]
            self._write_top_sums(dist, lines[0] + "\n" + lines[0] + "\n")
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("duplicate", res.stderr.lower())
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_wrong_name_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            lines = self._top_sums_text(dist).split("\n")[:-1]
            self._write_top_sums(dist, lines[0] + "\nother.zip  deadbeef\n")
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_traversal_name_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            lines = self._top_sums_text(dist).split("\n")[:-1]
            self._write_top_sums(dist, lines[0] + "\n../escape.zip  deadbeef\n")
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_wrong_digest_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            text = self._top_sums_text(dist)
            flipped = ("0" if text[0] != "0" else "1") + text[1:]
            self._write_top_sums(dist, flipped)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("mismatch", res.stderr.lower())
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))


class TestZipIntegrity(unittest.TestCase):
    """Test 6: corrupt ZIP, duplicate/extra/traversal/wrong-order members
    fail."""

    def test_corrupt_zip_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            corrupt_zip_bytes(os.path.join(dist, ZIP5340))
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_duplicate_member_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            members = zip_members(os.path.join(dist, ZIP5340))
            self.assertEqual(members[0][0], "FLASHING.md")
            dup = members + [members[0]]
            rewrite_zip(os.path.join(dist, ZIP5340), dup)
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("duplicate", res.stderr.lower())
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_extra_member_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            members = zip_members(os.path.join(dist, ZIP54L15))
            members.append(("extra.txt", b"extra"))
            rewrite_zip(os.path.join(dist, ZIP54L15), members)
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_traversal_member_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            members = zip_members(os.path.join(dist, ZIP5340))
            members.append(("../escape.txt", b"escape"))
            rewrite_zip(os.path.join(dist, ZIP5340), members)
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_absolute_member_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            members = zip_members(os.path.join(dist, ZIP5340))
            members.append(("/abs.txt", b"abs"))
            rewrite_zip(os.path.join(dist, ZIP5340), members)
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_directory_member_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            members = zip_members(os.path.join(dist, ZIP5340))
            members.append(("dir/", b""))
            rewrite_zip(os.path.join(dist, ZIP5340), members)
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("directory", res.stderr.lower())
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_wrong_member_order_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            members = zip_members(os.path.join(dist, ZIP5340))
            # Move FLASHING.md to the end; the FR1 order requires it first.
            reordered = members[1:] + [members[0]]
            rewrite_zip(os.path.join(dist, ZIP5340), reordered)
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("order", res.stderr.lower())
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))


class TestInternalSumsAndManifest(unittest.TestCase):
    """Test 7: internal checksum and manifest schema/metadata/image
    mutations fail."""

    def test_internal_sums_missing_member_line_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            path = os.path.join(dist, ZIP5340)
            members = zip_members(path)
            sums = members[-1][1].decode("ascii")
            lines = sums.split("\n")[:-1]
            self.assertGreater(len(lines), 2)
            replace_member(
                path, "SHA256SUMS", ("\n".join(lines[1:]) + "\n").encode("ascii")
            )
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("SHA256SUMS", res.stderr)
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_internal_sums_wrong_digest_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            path = os.path.join(dist, ZIP5340)
            members = zip_members(path)
            sums = members[-1][1].decode("ascii")
            lines = sums.split("\n")[:-1]
            digest, name = lines[0].split("  ", 1)
            bad = ("0" if digest[0] != "0" else "1") + digest[1:]
            lines[0] = "%s  %s" % (bad, name)
            replace_member(
                path, "SHA256SUMS", ("\n".join(lines) + "\n").encode("ascii")
            )
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("mismatch", res.stderr.lower())
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_internal_sums_unsorted_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            path = os.path.join(dist, ZIP5340)
            members = zip_members(path)
            sums = members[-1][1].decode("ascii")
            lines = sums.split("\n")[:-1]
            replace_member(
                path,
                "SHA256SUMS",
                (lines[-1] + "\n" + "\n".join(lines[:-1]) + "\n").encode("ascii"),
            )
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("sorted", res.stderr.lower())
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_internal_sums_hashes_itself_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            path = os.path.join(dist, ZIP5340)
            members = zip_members(path)
            sums = members[-1][1].decode("ascii")
            lines = sums.split("\n")[:-1]
            lines.append("%s  SHA256SUMS" % ("0" * 64))
            replace_member(
                path,
                "SHA256SUMS",
                (lines[-1] + "\n" + "\n".join(lines[:-1]) + "\n").encode("ascii"),
            )
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("SHA256SUMS", res.stderr)
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_manifest_malformed_json_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            replace_member(
                os.path.join(dist, ZIP5340), "release-manifest.json", b"{not json"
            )
            rehash_zip(os.path.join(dist, ZIP5340))
            rehash_top_sums(dist)
            res = run_prepare(tmp, dist)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("malformed", res.stderr.lower())
            self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def _mutated_manifest_case(self, tmp, mutate, check):
        dist = build_dist(tmp)
        path = os.path.join(dist, ZIP5340)
        manifest = get_manifest(path)
        mutate(manifest)
        put_manifest(path, manifest)
        rehash_zip(path)
        rehash_top_sums(dist)
        res = run_prepare(tmp, dist)
        self.assertNotEqual(res.returncode, 0, "must fail: %s" % check)
        self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
        self.assertFalse(os.path.exists(os.path.join(tmp, OUTPUT_DIR)))

    def test_manifest_extra_key_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m.update({"extra": 1}),
                "extra key",
            )

    def test_manifest_missing_key_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m.pop("ncs_version"),
                "missing key",
            )

    def test_manifest_wrong_project_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m.update({"project": "other"}),
                "wrong project",
            )

    def test_manifest_wrong_version_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m.update({"version": "9.9.9"}),
                "wrong version",
            )

    def test_manifest_wrong_commit_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m.update({"git_commit": "f" * 40}),
                "wrong commit",
            )

    def test_manifest_wrong_ncs_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m.update({"ncs_version": "v9.9.9"}),
                "wrong ncs",
            )

    def test_manifest_wrong_target_id_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m["target"].update({"id": "other"}),
                "wrong target id",
            )

    def test_manifest_wrong_board_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m["target"].update({"board": "other/board"}),
                "wrong board",
            )

    def test_manifest_wrong_image_role_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m["images"][0].update({"role": "bogus"}),
                "wrong role",
            )

    def test_manifest_wrong_image_filename_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m["images"][0].update({"filename": "other.hex"}),
                "wrong filename",
            )

    def test_manifest_wrong_image_path_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m["images"][0].update({"original_build_path": "bogus/"}),
                "wrong path",
            )

    def test_manifest_wrong_image_order_fails(self):
        with tempfile.TemporaryDirectory() as tmp:

            def mutate(m):
                m["images"][0]["flash_order"], m["images"][1]["flash_order"] = (
                    m["images"][1]["flash_order"],
                    m["images"][0]["flash_order"],
                )

            self._mutated_manifest_case(tmp, mutate, "wrong order")

    def test_manifest_wrong_image_size_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m["images"][0].update({"size": 1}),
                "wrong size",
            )

    def test_manifest_wrong_image_sha256_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._mutated_manifest_case(
                tmp,
                lambda m: m["images"][0].update({"sha256": "f" * 64}),
                "wrong sha256",
            )

    def test_manifest_extra_image_fails(self):
        with tempfile.TemporaryDirectory() as tmp:

            def mutate(m):
                extra = dict(m["images"][1])
                extra.update({"filename": "extra.hex"})
                m["images"].append(extra)

            self._mutated_manifest_case(tmp, mutate, "extra image")


class TestOutputContract(unittest.TestCase):
    """Test 8: existing output sentinel unchanged; handled output I/O
    failure is clean and leaves no staging sibling."""

    def test_existing_output_dir_rejected_sentinel_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            output = os.path.join(tmp, OUTPUT_DIR)
            os.makedirs(output)
            sentinel = os.path.join(output, "sentinel.txt")
            with open(sentinel, "w") as fh:
                fh.write("keep-me")
            res = run_prepare(tmp, dist, output)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn(ERROR_PREFIX, res.stderr)
            with open(sentinel) as fh:
                self.assertEqual(fh.read(), "keep-me")

    @unittest.skipUnless(
        sys.platform.startswith("linux"), "Linux RLIMIT_FSIZE regression"
    )
    def test_output_io_failure_clean_no_staging_sibling(self):
        def limit_file_size_100():
            resource.setrlimit(resource.RLIMIT_FSIZE, (100, 100))

        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            output = os.path.join(tmp, OUTPUT_DIR)
            res = run_prepare(tmp, dist, output, preexec_fn=limit_file_size_100)
            self.assertNotEqual(res.returncode, 0, res.stderr)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertNotIn("Traceback", res.stderr)
            self.assertFalse(os.path.exists(output), "output must stay absent")
            leftovers = [
                name for name in os.listdir(tmp) if name.startswith(STAGING_PREFIX)
            ]
            self.assertEqual(leftovers, [])


class TestProcessContract(unittest.TestCase):
    """Test 9: stable stderr diagnostics and host-path-free success output."""

    def test_error_paths_stable_prefix_no_traceback(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            output = os.path.join(tmp, OUTPUT_DIR)
            # Missing required arguments.
            res = subprocess.run(
                [sys.executable, PREPARE_SCRIPT, "--tag", TAG],
                capture_output=True,
                text=True,
                cwd=REPO_ROOT,
            )
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertNotIn("Traceback", res.stderr)
            # Invalid version.
            res = run_prepare(tmp, dist, output, version="not-a-version")
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertNotIn("Traceback", res.stderr)
            # Missing artifact.
            os.unlink(os.path.join(dist, ZIP54L15))
            res = run_prepare(tmp, dist, output)
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertNotIn("Traceback", res.stderr)
            # Corrupt artifact.
            corrupt_zip_bytes(os.path.join(dist, ZIP5340))
            res = run_prepare(tmp, dist, output)
            self.assertNotEqual(res.returncode, 0)
            self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
            self.assertNotIn("Traceback", res.stderr)
            self.assertFalse(os.path.exists(output))

    def test_success_stdout_contains_no_host_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = build_dist(tmp)
            output = os.path.join(tmp, OUTPUT_DIR)
            res = run_prepare(tmp, dist, output)
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertEqual(res.stdout, SUCCESS_STDOUT)
            self.assertEqual(res.stderr, "")
            for line in res.stdout.splitlines():
                self.assertNotIn("/", line)
                self.assertNotIn("..", line)
                self.assertNotIn(tmp, line)


if __name__ == "__main__":
    unittest.main()
