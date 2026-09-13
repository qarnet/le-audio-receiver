#!/usr/bin/env python3
"""Deterministic draft-release preparation and validation CLI (stdlib only).

Validates a downloaded FR1 factory firmware artifact set (the nRF54L15
target ZIP plus a top-level SHA256SUMS; the nRF5340 receiver target is
eliminated from the release line, 2026-09-03 decision) and writes
deterministic draft-release metadata, exactly per
``docs/development/firmware-release-plan.md`` FR3.

Public CLI (all arguments required):

    python3 scripts/prepare-draft-release.py \
      --tag v0.1.0 \
      --version 0.1.0 \
      --git-commit 0123456789abcdef0123456789abcdef01234567 \
      --ncs-version v3.3.0 \
      --repository qarnet/le-audio-receiver \
      --workflow "Firmware build" \
      --workflow-ref qarnet/le-audio-receiver/.github/workflows/firmware-build.yml@refs/heads/main \
      --run-id 123456 \
      --run-attempt 1 \
      --artifact-dir dist \
      --output-dir release-metadata

Guarantees:

- every string input is checked for control characters/newlines;
- tag is canonical vMAJOR.MINOR.PATCH and its suffix equals --version;
- git commit is exactly 40 lowercase hexadecimal characters;
- NCS version, repository, workflow name, and workflow ref are the exact
  FR3 values for this release track;
- run ID and attempt are canonical positive decimal integers;
- the artifact directory must be a real directory (not a symlink) holding
  exactly the two expected regular non-symlink files and nothing else;
- the top-level checksum file is ASCII/UTF-8 GNU two-space format, sorted,
  exactly one line for the nRF54L15 ZIP, no duplicates, and the hash matches
  the file;
- the ZIP passes integrity checks, has no duplicate/directory/
  path-traversal members, and exactly the FR1 member order;
- the ZIP's ``FLASHING.md`` is strict UTF-8;
- each internal SHA256SUMS is exact/sorted and hashes every member except
  itself;
- the release-manifest.json matches the exact FR1 schema and reports the
  supplied version/commit/NCS plus the expected target and image records,
  and every image size/hash matches the packaged bytes;
- no host paths, timestamps, runner paths, usernames, tokens, or mutable
  tag-only dependencies enter the metadata;
- the output directory must be absent; all inputs and artifacts are
  validated before any output creation;
- metadata is written into a private ``.draft-release-*`` staging sibling
  and atomically renamed into place only when complete; on any handled
  failure the final output and the staging sibling both remain absent;
- success prints exactly two stable, deterministic lines;
- caller errors print one ``prepare-draft-release: error: ...`` line to
  stderr and return nonzero, without a traceback.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import sys
import tempfile
import zipfile
import zlib

PROJECT = "le-audio-receiver"
SCHEMA_VERSION = 1
ERROR_PREFIX = "prepare-draft-release: error: "
STAGING_PREFIX = ".draft-release-"

# Immutable FR3 pins for this release track.
NCS_VERSION = "v3.3.0"
REPOSITORY = "qarnet/le-audio-receiver"
WORKFLOW_NAME = "Firmware build"
TOOLCHAIN_IMAGE = (
    "ghcr.io/nrfconnect/sdk-nrf-toolchain@sha256:"
    "f24d8932ff081ebcd8da9c248f4449bdabe461c0620a7a4ac9e95eb577ba2276"
)
TOOLCHAIN_SDK_NRF_COMMIT = "ba167d9f3db4abbdc9b67887ca3ea66c64f2d956"

WORKFLOW_REF_TEMPLATE = "%s/.github/workflows/firmware-build.yml@refs/heads/main"

# Canonical MAJOR.MINOR.PATCH: each part numeric, no leading zeros except
# the literal zero.
_VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
_GIT_COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_POSITIVE_INT_RE = re.compile(r"^[1-9][0-9]*$")

# Expected FR1 member order for the active nRF54L15 target ZIP.
MEMBERS_54L15 = (
    "FLASHING.md",
    "cpuapp.hex",
    "flpr.hex",
    "release-manifest.json",
    "SHA256SUMS",
)

# Expected image records for the active nRF54L15 target ZIP: (filename, role,
# original path, flash order), sorted by flash order.
IMAGES_54L15 = (
    ("cpuapp.hex", "cpuapp", "nrf54l15/le-audio-receiver/zephyr/zephyr.hex", 0),
    ("flpr.hex", "flpr", "nrf54l15/flpr/zephyr/zephyr.hex", 1),
)

MANIFEST_KEYS = frozenset(
    {
        "git_commit",
        "images",
        "ncs_version",
        "project",
        "schema_version",
        "target",
        "version",
    }
)
IMAGE_KEYS = frozenset(
    {"filename", "flash_order", "original_build_path", "role", "sha256", "size"}
)

_SUMS_LINE_RE = re.compile(r"^[0-9a-f]{64}  ([^ ]+)$")


class ReleaseError(Exception):
    """A caller error that must be reported as a clean stderr diagnostic."""


def _validate_string_input(value, label):
    """Reject control characters and newlines in string inputs."""
    if any(ord(ch) < 0x20 or ord(ch) == 0x7F for ch in value):
        raise ReleaseError(
            "%s contains control characters or newlines: %r" % (label, value)
        )
    return value


def _validate_version(value):
    if not _VERSION_RE.match(value):
        raise ReleaseError(
            "invalid project version %r (expected canonical MAJOR.MINOR.PATCH, "
            "each part numeric without leading zeros)" % value
        )
    return value


def _validate_git_commit(value):
    if not _GIT_COMMIT_RE.match(value):
        raise ReleaseError(
            "invalid git commit %r (expected exactly 40 lowercase hexadecimal "
            "characters)" % value
        )
    return value


def _validate_positive_int(value, label):
    if not _POSITIVE_INT_RE.match(value):
        raise ReleaseError(
            "%s %r (expected a canonical positive decimal integer, no leading "
            "zeros)" % (label, value)
        )
    return value


def _expected_zip_name(target_id, version):
    return "le-audio-receiver-v%s-%s-factory.zip" % (version, target_id)


def _read_text_file(path, label):
    """Read one file as strict UTF-8 text; rejects symlinks and non-regular
    files.  Also rejects any byte that is not ASCII text, so checksum files
    are kept to the exact GNU ASCII contract."""
    if os.path.islink(path):
        raise ReleaseError("%s is a symlink: %s" % (label, path))
    try:
        st = os.stat(path)
    except OSError as exc:
        raise ReleaseError("%s cannot be read: %s (%s)" % (label, path, exc))
    if not stat.S_ISREG(st.st_mode):
        raise ReleaseError("%s is not a regular file: %s" % (label, path))
    try:
        with open(path, "rb") as fh:
            raw = fh.read()
    except OSError as exc:
        raise ReleaseError("%s cannot be read: %s (%s)" % (label, path, exc))
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ReleaseError("%s is not valid UTF-8: %s (%s)" % (label, path, exc))


def _parse_sums(text, label):
    """Parse GNU two-space SHA256SUMS text into a name -> digest mapping.

    Enforces exact format, sorted lines, no duplicates, and rejects
    absolute, traversal, or empty names.
    """
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines = lines[:-1]
    parsed = {}
    for line in lines:
        match = _SUMS_LINE_RE.match(line)
        if not match:
            raise ReleaseError(
                "%s malformed checksum line %r (expected '<64 lowercase hex>  <name>')"
                % (label, line)
            )
        name = match.group(1)
        if name.startswith("/") or ".." in name or name == "":
            raise ReleaseError(
                "%s checksum name is not a safe basename: %r" % (label, name)
            )
        digest = line[:64]
        if name in parsed:
            raise ReleaseError("%s duplicate checksum line for %r" % (label, name))
        parsed[name] = digest
    if sorted(parsed) != list(parsed):
        raise ReleaseError("%s checksum lines are not sorted" % label)
    return parsed


def _validate_artifact_dir(artifact_dir, version):
    """Require a real directory holding exactly the two expected regular
    non-symlink files and nothing else."""
    if os.path.islink(artifact_dir):
        raise ReleaseError("artifact directory is a symlink: %s" % artifact_dir)
    try:
        st = os.stat(artifact_dir)
    except OSError as exc:
        raise ReleaseError(
            "artifact directory cannot be read: %s (%s)" % (artifact_dir, exc)
        )
    if not stat.S_ISDIR(st.st_mode):
        raise ReleaseError("artifact directory is not a directory: %s" % artifact_dir)

    expected = {
        "SHA256SUMS",
        _expected_zip_name("nrf54l15-xiao", version),
    }
    try:
        entries = sorted(os.listdir(artifact_dir))
    except OSError as exc:
        raise ReleaseError(
            "artifact directory cannot be listed: %s (%s)" % (artifact_dir, exc)
        )
    if set(entries) != expected:
        raise ReleaseError(
            "artifact directory must contain exactly %s (found %s)"
            % (sorted(expected), entries)
        )
    for name in entries:
        path = os.path.join(artifact_dir, name)
        if os.path.islink(path):
            raise ReleaseError("artifact entry is a symlink: %s" % path)
        try:
            entry_st = os.stat(path)
        except OSError as exc:
            raise ReleaseError("artifact entry cannot be read: %s (%s)" % (path, exc))
        if not stat.S_ISREG(entry_st.st_mode):
            raise ReleaseError("artifact entry is not a regular file: %s" % path)


def _validate_top_checksum(artifact_dir, version):
    """Validate the top-level SHA256SUMS: exact format, sorted, exactly one
    nRF54L15 ZIP line, no duplicates, and the hash matches the file."""
    path = os.path.join(artifact_dir, "SHA256SUMS")
    text = _read_text_file(path, "top-level SHA256SUMS")
    parsed = _parse_sums(text, "top-level SHA256SUMS")
    expected_names = [
        _expected_zip_name("nrf54l15-xiao", version),
    ]
    if sorted(parsed) != sorted(expected_names):
        raise ReleaseError(
            "top-level SHA256SUMS must hash exactly %s (found %s)"
            % (expected_names, sorted(parsed))
        )
    for name, digest in sorted(parsed.items()):
        full = os.path.join(artifact_dir, name)
        with open(full, "rb") as fh:
            actual = hashlib.sha256(fh.read()).hexdigest()
        if actual != digest:
            raise ReleaseError("top-level SHA256SUMS digest mismatch for %s" % name)


def _validate_member_names(members, label):
    """Reject duplicate, directory, absolute, and path-traversal member
    names; return the name -> data mapping."""
    seen = set()
    mapping = {}
    for info in members:
        name = info.filename
        if info.is_dir() or name.endswith("/"):
            raise ReleaseError("%s contains a directory member: %r" % (label, name))
        if name.startswith("/") or ".." in name or name == "":
            raise ReleaseError(
                "%s contains an absolute or traversal member name: %r" % (label, name)
            )
        if name in seen:
            raise ReleaseError("%s contains a duplicate member: %r" % (label, name))
        seen.add(name)
    return mapping


def _validate_internal_sums(mapping, members, label):
    """Internal SHA256SUMS must be exact/sorted and hash every member except
    itself."""
    sums = mapping.get("SHA256SUMS")
    if sums is None:
        raise ReleaseError("%s is missing its internal SHA256SUMS member" % label)
    try:
        text = sums.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ReleaseError("%s internal SHA256SUMS is not valid UTF-8" % label)
    parsed = _parse_sums(text, "%s internal SHA256SUMS" % label)
    expected = {
        info.filename: info for info in members if info.filename != "SHA256SUMS"
    }
    if sorted(parsed) != sorted(expected):
        raise ReleaseError(
            "%s internal SHA256SUMS must hash every member except itself "
            "(found %s, expected %s)" % (label, sorted(parsed), sorted(expected))
        )
    for name, digest in parsed.items():
        info = expected[name]
        data = mapping[name]
        if len(data) != info.file_size:
            raise ReleaseError("%s member size mismatch: %r" % (label, name))
        if hashlib.sha256(data).hexdigest() != digest:
            raise ReleaseError(
                "%s internal SHA256SUMS digest mismatch for %r" % (label, name)
            )


def _validate_manifest(
    mapping,
    label,
    version,
    commit,
    ncs,
    target_id,
    board,
    expected_members,
    expected_images,
):
    """Validate the release-manifest.json member against the exact FR1
    schema and the supplied metadata; every image size/hash must match the
    packaged bytes."""
    raw = mapping.get("release-manifest.json")
    if raw is None:
        raise ReleaseError("%s is missing release-manifest.json" % label)
    try:
        manifest = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as exc:
        raise ReleaseError(
            "%s release-manifest.json is malformed JSON: %s" % (label, exc)
        )
    if not isinstance(manifest, dict):
        raise ReleaseError("%s release-manifest.json is not an object" % label)
    if set(manifest) != MANIFEST_KEYS:
        raise ReleaseError(
            "%s release-manifest.json has unexpected or missing top-level keys "
            "(found %s, expected %s)" % (label, sorted(manifest), sorted(MANIFEST_KEYS))
        )
    if manifest["schema_version"] != SCHEMA_VERSION:
        raise ReleaseError(
            "%s release-manifest.json schema_version is %r (expected %d)"
            % (label, manifest["schema_version"], SCHEMA_VERSION)
        )
    if manifest["project"] != PROJECT:
        raise ReleaseError(
            "%s release-manifest.json project is %r (expected %r)"
            % (label, manifest["project"], PROJECT)
        )
    if manifest["version"] != version:
        raise ReleaseError(
            "%s release-manifest.json version is %r (expected %r)"
            % (label, manifest["version"], version)
        )
    if manifest["git_commit"] != commit:
        raise ReleaseError(
            "%s release-manifest.json git_commit is %r (expected %r)"
            % (label, manifest["git_commit"], commit)
        )
    if manifest["ncs_version"] != ncs:
        raise ReleaseError(
            "%s release-manifest.json ncs_version is %r (expected %r)"
            % (label, manifest["ncs_version"], ncs)
        )
    target = manifest["target"]
    if not isinstance(target, dict) or set(target) != {"board", "id"}:
        raise ReleaseError("%s release-manifest.json target is malformed" % label)
    if target["id"] != target_id or target["board"] != board:
        raise ReleaseError(
            "%s release-manifest.json target is %r (expected id=%r board=%r)"
            % (label, target, target_id, board)
        )

    images = manifest["images"]
    if not isinstance(images, list) or len(images) != len(expected_images):
        raise ReleaseError(
            "%s release-manifest.json images must list exactly %d records"
            % (label, len(expected_images))
        )
    actual_tuples = []
    for record in images:
        if not isinstance(record, dict) or set(record) != IMAGE_KEYS:
            raise ReleaseError(
                "%s release-manifest.json image record malformed" % label
            )
        filename = record["filename"]
        flash_order = record["flash_order"]
        if not isinstance(flash_order, int) or isinstance(flash_order, bool):
            raise ReleaseError(
                "%s release-manifest.json flash_order is not an integer: %r"
                % (label, flash_order)
            )
        if not isinstance(record["size"], int) or isinstance(record["size"], bool):
            raise ReleaseError(
                "%s release-manifest.json size is not an integer: %r"
                % (label, record["size"])
            )
        if not _SHA256_RE.match(record["sha256"]):
            raise ReleaseError(
                "%s release-manifest.json sha256 is not canonical: %r"
                % (label, record["sha256"])
            )
        actual_tuples.append(
            (
                filename,
                record["role"],
                record["original_build_path"],
                flash_order,
            )
        )
    if sorted(actual_tuples, key=lambda t: t[3]) != list(expected_images):
        raise ReleaseError(
            "%s release-manifest.json image records do not match the expected "
            "filename/role/path/order tuples" % label
        )
    for record in images:
        filename = record["filename"]
        if filename not in mapping or filename == "SHA256SUMS":
            raise ReleaseError(
                "%s release-manifest.json references missing member %r"
                % (label, filename)
            )
        data = mapping[filename]
        if len(data) != record["size"]:
            raise ReleaseError(
                "%s release-manifest.json size mismatch for %r (declared %d, "
                "actual %d)" % (label, filename, record["size"], len(data))
            )
        if hashlib.sha256(data).hexdigest() != record["sha256"]:
            raise ReleaseError(
                "%s release-manifest.json digest mismatch for %r" % (label, filename)
            )


def _validate_zip(
    path, version, commit, ncs, target_id, board, expected_members, expected_images
):
    """Open one target ZIP and run every integrity, member, checksum, and
    manifest check.  Returns (name -> data) mapping for hashing."""
    try:
        zf = zipfile.ZipFile(path, "r")
    except zipfile.BadZipFile as exc:
        raise ReleaseError("%s is not a valid ZIP: %s" % (path, exc))
    with zf:
        try:
            bad = zf.testzip()
        except (zipfile.BadZipFile, zlib.error, EOFError, OSError) as exc:
            raise ReleaseError("%s ZIP integrity check failed: %s" % (path, exc))
        if bad is not None:
            raise ReleaseError(
                "%s ZIP integrity check failed on member %r" % (path, bad)
            )
        members = zf.infolist()
        _validate_member_names(members, path)
        names = [info.filename for info in members]
        if names != list(expected_members):
            raise ReleaseError(
                "%s member order/names are not the FR1 contract (found %s, "
                "expected %s)" % (path, names, list(expected_members))
            )
        mapping = {}
        for info in members:
            try:
                mapping[info.filename] = zf.read(info.filename)
            except (zipfile.BadZipFile, zlib.error, EOFError, OSError) as exc:
                raise ReleaseError(
                    "%s member %r cannot be read: %s" % (path, info.filename, exc)
                )
    flashing = mapping.get("FLASHING.md")
    if flashing is None:
        raise ReleaseError("%s is missing FLASHING.md" % path)
    try:
        flashing.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ReleaseError("%s FLASHING.md is not valid UTF-8: %s" % (path, exc))
    _validate_internal_sums(mapping, members, path)
    _validate_manifest(
        mapping,
        path,
        version,
        commit,
        ncs,
        target_id,
        board,
        expected_members,
        expected_images,
    )
    return mapping


def _release_notes(version, commit, ncs, repository, run_id, zip_names):
    """Deterministic draft release notes text (no em dashes, no timestamps,
    no host paths)."""
    lines = []
    lines.append("# LE Audio Receiver v%s" % version)
    lines.append("")
    lines.append(
        "This is a draft factory-flash candidate. It has not been published "
        "and has not passed hardware acceptance."
    )
    lines.append("")
    lines.append("Source commit: %s" % commit)
    lines.append("NCS version: %s" % ncs)
    lines.append(
        "Workflow run: https://github.com/%s/actions/runs/%s" % (repository, run_id)
    )
    lines.append("")
    lines.append("Artifacts in this draft:")
    for name in zip_names:
        lines.append("- %s" % name)
    lines.append("- SHA256SUMS")
    lines.append("")
    lines.append(
        "The nRF54L15 target ZIP is a release tuple: its companion images must be "
        "flashed together from the same version and must never be mixed "
        "across versions."
    )
    lines.append("")
    lines.append(
        "Normal flashing preserves settings and bonds; a clean erase is a "
        "separate, explicitly documented procedure."
    )
    lines.append("")
    lines.append(
        "Do not publish this draft until FR4 exact-asset hardware acceptance passes."
    )
    lines.append("")
    lines.append("MCUboot/DFU is not included in this release.")
    lines.append("")
    return "\n".join(lines)


def _run(args):
    tag = _validate_string_input(args.tag, "tag")
    version = _validate_string_input(args.version, "version")
    commit = _validate_string_input(args.git_commit, "git commit")
    ncs = _validate_string_input(args.ncs_version, "NCS version")
    repository = _validate_string_input(args.repository, "repository")
    workflow = _validate_string_input(args.workflow, "workflow")
    workflow_ref = _validate_string_input(args.workflow_ref, "workflow ref")
    run_id = _validate_string_input(args.run_id, "run ID")
    run_attempt = _validate_string_input(args.run_attempt, "run attempt")

    _validate_version(version)
    if not tag.startswith("v") or not _VERSION_RE.match(tag[1:]):
        raise ReleaseError(
            "invalid tag %r (expected canonical vMAJOR.MINOR.PATCH)" % tag
        )
    if tag[1:] != version:
        raise ReleaseError("tag %r suffix does not equal version %r" % (tag, version))
    _validate_git_commit(commit)
    if ncs != NCS_VERSION:
        raise ReleaseError(
            "NCS version %r is not %r for this release track" % (ncs, NCS_VERSION)
        )
    if repository != REPOSITORY:
        raise ReleaseError(
            "repository %r is not %r for this release track" % (repository, REPOSITORY)
        )
    if workflow != WORKFLOW_NAME:
        raise ReleaseError(
            "workflow %r is not %r for this release track" % (workflow, WORKFLOW_NAME)
        )
    expected_ref = WORKFLOW_REF_TEMPLATE % repository
    if workflow_ref != expected_ref:
        raise ReleaseError(
            "workflow ref %r does not equal %r" % (workflow_ref, expected_ref)
        )
    _validate_positive_int(
        run_id, "run ID must be a canonical positive decimal integer: "
    )
    _validate_positive_int(
        run_attempt, "run attempt must be a canonical positive decimal integer: "
    )

    # 0. Validate the path arguments as string inputs before any filesystem
    #    action, so control characters/newlines cannot reach the filesystem.
    artifact_dir = _validate_string_input(args.artifact_dir, "artifact directory")
    output_dir_arg = _validate_string_input(args.output_dir, "output directory")

    # 1. Validate the artifact directory and every artifact before any
    #    output creation.
    _validate_artifact_dir(artifact_dir, version)
    _validate_top_checksum(artifact_dir, version)

    zip_names = (_expected_zip_name("nrf54l15-xiao", version),)
    mappings = {
        zip_names[0]: _validate_zip(
            os.path.join(artifact_dir, zip_names[0]),
            version,
            commit,
            ncs,
            "nrf54l15-xiao",
            "nrf54l15dk/nrf54l15/cpuapp",
            MEMBERS_54L15,
            IMAGES_54L15,
        ),
    }

    # 2. Require the output directory absent.
    output_dir = os.path.abspath(output_dir_arg)
    if os.path.lexists(output_dir):
        raise ReleaseError("output directory already exists: %s" % output_dir)

    # 3. The output parent may be created after validation.
    parent = os.path.dirname(output_dir)
    os.makedirs(parent, exist_ok=True)

    # 4. Build the metadata set in one private staging sibling, then
    #    atomically rename it into place.  On failure, clean only staging
    #    and leave the final output directory absent.
    staging = tempfile.mkdtemp(prefix=STAGING_PREFIX, dir=parent)
    try:
        provenance = _provenance(
            args.artifact_dir,
            version,
            commit,
            ncs,
            repository,
            workflow,
            workflow_ref,
            int(run_id),
            int(run_attempt),
            tag,
            zip_names,
            mappings,
        )
        with open(
            os.path.join(staging, "release-provenance.json"),
            "w",
            encoding="utf-8",
            newline="\n",
        ) as fh:
            fh.write(json.dumps(provenance, indent=2, sort_keys=True) + "\n")
        notes = _release_notes(version, commit, ncs, repository, int(run_id), zip_names)
        with open(
            os.path.join(staging, "release-notes.md"),
            "w",
            encoding="utf-8",
            newline="\n",
        ) as fh:
            fh.write(notes + "\n")
        os.rename(staging, output_dir)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise

    print("prepare-draft-release: wrote release-provenance.json")
    print("prepare-draft-release: wrote release-notes.md")


def _provenance(
    artifact_dir,
    version,
    commit,
    ncs,
    repository,
    workflow,
    workflow_ref,
    run_id,
    run_attempt,
    tag,
    zip_names,
    mappings,
):
    """Exact FR3 provenance object: artifacts sorted by filename, no
    timestamps, runner paths, usernames, tokens, or host paths."""
    artifacts = []
    sums_path = os.path.join(artifact_dir, "SHA256SUMS")
    with open(sums_path, "rb") as fh:
        sums_data = fh.read()
    artifacts.append(
        {
            "filename": "SHA256SUMS",
            "sha256": hashlib.sha256(sums_data).hexdigest(),
            "size": len(sums_data),
        }
    )
    for name in zip_names:
        path = os.path.join(artifact_dir, name)
        with open(path, "rb") as fh:
            data = fh.read()
        artifacts.append(
            {
                "filename": name,
                "sha256": hashlib.sha256(data).hexdigest(),
                "size": len(data),
            }
        )
    artifacts.sort(key=lambda rec: rec["filename"])
    return {
        "artifacts": artifacts,
        "build": {
            "git_commit": commit,
            "ncs_version": ncs,
            "project": PROJECT,
            "version": version,
        },
        "ci": {
            "repository": repository,
            "run_attempt": run_attempt,
            "run_id": run_id,
            "workflow": workflow,
            "workflow_ref": workflow_ref,
        },
        "release": {"draft": True, "tag": tag},
        "schema_version": SCHEMA_VERSION,
        "toolchain": {
            "container_image": TOOLCHAIN_IMAGE,
            "sdk_nrf_commit": TOOLCHAIN_SDK_NRF_COMMIT,
        },
    }


class _ArgumentParser(argparse.ArgumentParser):
    """Argparse variant that emits exactly one clean stderr diagnostic
    without a usage block or traceback."""

    def error(self, message):
        self.exit(2, "%s%s\n" % (ERROR_PREFIX, message))


def _build_parser():
    parser = _ArgumentParser(prog="prepare-draft-release", add_help=True)
    parser.add_argument("--tag", required=True, help="vMAJOR.MINOR.PATCH tag")
    parser.add_argument(
        "--version", required=True, help="project version MAJOR.MINOR.PATCH"
    )
    parser.add_argument("--git-commit", required=True, help="40 lowercase hex commit")
    parser.add_argument("--ncs-version", required=True, help="exact v3.3.0")
    parser.add_argument("--repository", required=True, help="exact owner/repository")
    parser.add_argument("--workflow", required=True, help="exact workflow name")
    parser.add_argument("--workflow-ref", required=True, help="exact workflow ref")
    parser.add_argument("--run-id", required=True, help="canonical positive decimal")
    parser.add_argument(
        "--run-attempt", required=True, help="canonical positive decimal"
    )
    parser.add_argument(
        "--artifact-dir", required=True, help="downloaded artifact directory"
    )
    parser.add_argument(
        "--output-dir", required=True, help="final metadata output directory"
    )
    return parser


def main(argv=None):
    parser = _build_parser()
    args = parser.parse_args(argv)
    try:
        _run(args)
    except (ReleaseError, OSError, zipfile.BadZipFile) as exc:
        print("%s%s" % (ERROR_PREFIX, exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
