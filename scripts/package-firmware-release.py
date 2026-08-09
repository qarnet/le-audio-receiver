#!/usr/bin/env python3
"""Deterministic factory-firmware release packager (stdlib only).

Packages the existing production build outputs for both receiver targets
into one release set of two deterministic ZIPs plus a top-level
SHA256SUMS, exactly per ``docs/development/firmware-release-plan.md`` FR1.

Public CLI (all five arguments required):

    python3 scripts/package-firmware-release.py \
      --version 0.1.0 \
      --git-commit 0123456789abcdef0123456789abcdef01234567 \
      --ncs-version v3.3.0 \
      --build-root build \
      --output-dir dist

Guarantees:

- project version is canonical MAJOR.MINOR.PATCH (numeric parts, no
  leading zeros except a literal ``0``);
- Git commit is exactly 40 lowercase hexadecimal characters;
- NCS version is canonical vMAJOR.MINOR.PATCH;
- build root and output parent are resolved, but no absolute host paths
  ever enter archives or manifests;
- an existing output directory is rejected;
- every input for both targets is validated before any output creation;
- all four image inputs are regular, non-symlink, nonempty Intel HEX
  files that pass full record validation (byte count, checksum, exactly
  one final EOF record);
- both flashing notes are regular, non-symlink, nonempty UTF-8 files;
- the release set is built in a private ``.firmware-release-*`` staging
  sibling and atomically renamed into place only when complete;
- ZIP bytes are deterministic for identical inputs and metadata;
- success prints stable, concise, output-relative lines;
- caller errors print one ``package-firmware-release: error: ...`` line
  to stderr and return nonzero, without a traceback.
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

PROJECT = "le-audio-receiver"
SCHEMA_VERSION = 1
ERROR_PREFIX = "package-firmware-release: error: "
STAGING_PREFIX = ".firmware-release-"

# Canonical MAJOR.MINOR.PATCH: each part numeric, no leading zeros except
# the literal zero.
_VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
_GIT_COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
_HEX_RE = re.compile(r"[0-9A-Fa-f]*")

# Canonical input mapping (paths relative to --build-root), per the FR1
# contract.  Flash order 0 = first.  One invocation always packages both
# receiver targets as one release set.
TARGETS = (
    {
        "id": "nrf5340-e83",
        "board": "ebyte_e83_nrf5340/nrf5340/cpuapp",
        "note": "release/flashing/nrf5340-e83.md",
        "images": (
            {
                "role": "cpuapp",
                "path": "nrf5340/merged.hex",
                "filename": "merged.hex",
                "flash_order": 0,
            },
            {
                "role": "cpunet",
                "path": "nrf5340/merged_CPUNET.hex",
                "filename": "merged_CPUNET.hex",
                "flash_order": 1,
            },
        ),
    },
    {
        "id": "nrf54l15-xiao",
        "board": "nrf54l15dk/nrf54l15/cpuapp",
        "note": "release/flashing/nrf54l15-xiao.md",
        "images": (
            {
                "role": "cpuapp",
                "path": "nrf54l15/le-audio-receiver/zephyr/zephyr.hex",
                "filename": "cpuapp.hex",
                "flash_order": 0,
            },
            {
                "role": "flpr",
                "path": "nrf54l15/flpr/zephyr/zephyr.hex",
                "filename": "flpr.hex",
                "flash_order": 1,
            },
        ),
    },
)


class PackagerError(Exception):
    """A caller error that must be reported as a clean stderr diagnostic."""


def _repo_root():
    """Repository root derived from this script's fixed location."""
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _validate_version(value):
    if not _VERSION_RE.match(value):
        raise PackagerError(
            "invalid project version %r (expected canonical MAJOR.MINOR.PATCH, "
            "each part numeric without leading zeros)" % value
        )
    return value


def _validate_git_commit(value):
    if not _GIT_COMMIT_RE.match(value):
        raise PackagerError(
            "invalid git commit %r (expected exactly 40 lowercase hexadecimal "
            "characters)" % value
        )
    return value


def _validate_ncs_version(value):
    if not value.startswith("v") or not _VERSION_RE.match(value[1:]):
        raise PackagerError(
            "invalid NCS version %r (expected canonical vMAJOR.MINOR.PATCH, "
            "each part numeric without leading zeros)" % value
        )
    return value


def _validate_regular_file(path, label):
    """Require a regular, non-symlink, nonempty file."""
    if os.path.islink(path):
        raise PackagerError("%s is a symlink: %s" % (label, path))
    try:
        st = os.stat(path)
    except OSError as exc:
        raise PackagerError("%s cannot be read: %s (%s)" % (label, path, exc))
    if not stat.S_ISREG(st.st_mode):
        raise PackagerError("%s is not a regular file: %s" % (label, path))
    if st.st_size == 0:
        raise PackagerError("%s is empty: %s" % (label, path))


def _validate_hex_bytes(raw, label, path):
    """Validate Intel HEX content: ASCII text, structural records, exactly
    one final EOF record."""
    if any(b >= 0x80 for b in raw):
        raise PackagerError("%s is not ASCII text: %s" % (label, path))
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as exc:
        raise PackagerError("%s is not ASCII text: %s" % (label, path))
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines = lines[:-1]  # tolerate a single trailing newline
    if not lines:
        raise PackagerError("%s contains no records: %s" % (label, path))
    eof_count = 0
    for line in lines:
        if line.endswith("\r"):
            line = line[:-1]  # tolerate CRLF line endings
        if not line.strip():
            raise PackagerError("%s contains a blank record" % label)
        if not line.startswith(":"):
            raise PackagerError("%s record does not start with ':': %s" % (label, path))
        body = line[1:]
        if len(body) % 2 != 0:
            raise PackagerError(
                "%s record has an odd number of hexadecimal characters" % label
            )
        if not _HEX_RE.fullmatch(body):
            raise PackagerError("%s record contains non-hexadecimal characters" % label)
        record = bytes.fromhex(body)
        if len(record) < 5:
            raise PackagerError("%s record is too short" % label)
        declared = record[0]
        address = (record[1] << 8) | record[2]
        rec_type = record[3]
        data = record[4:-1]
        if len(data) != declared:
            raise PackagerError(
                "%s byte-count mismatch: declared %d, actual %d"
                % (label, declared, len(data))
            )
        if sum(record) & 0xFF != 0:
            raise PackagerError("%s record checksum does not sum to zero" % label)
        if rec_type == 1:
            if declared != 0 or address != 0:
                raise PackagerError("%s malformed EOF record" % label)
            eof_count += 1
        elif eof_count:
            raise PackagerError("%s record appears after EOF" % label)
    if eof_count != 1:
        raise PackagerError(
            "%s must contain exactly one EOF record (found %d)" % (label, eof_count)
        )


def _read_hex_file(path, label):
    """Validate and read one image input as raw bytes."""
    _validate_regular_file(path, label)
    with open(path, "rb") as fh:
        raw = fh.read()
    _validate_hex_bytes(raw, label, path)
    return raw


def _read_note_file(path, label):
    """Validate and read one flashing note as raw bytes."""
    _validate_regular_file(path, label)
    with open(path, "rb") as fh:
        raw = fh.read()
    try:
        raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise PackagerError("%s is not valid UTF-8: %s" % (label, path))
    return raw


def _prepare_target(target, build_root, repo_root):
    """Validation phase: read and validate every input for one target.
    Returns prepared records; nothing is written."""
    note_path = os.path.join(repo_root, target["note"])
    note_data = _read_note_file(note_path, "%s flashing note" % target["id"])
    images = []
    for img in target["images"]:
        src = os.path.join(build_root, img["path"])
        label = "%s %s image" % (target["id"], img["role"])
        data = _read_hex_file(src, label)
        images.append(
            {
                "role": img["role"],
                "path": img["path"],
                "filename": img["filename"],
                "flash_order": img["flash_order"],
                "data": data,
                "sha256": hashlib.sha256(data).hexdigest(),
                "size": len(data),
            }
        )
    images.sort(key=lambda rec: rec["flash_order"])
    return {"note_data": note_data, "images": images}


def _manifest(target, prepared, version, commit, ncs):
    """Exact FR1 manifest object (schema version 1)."""
    return {
        "git_commit": commit,
        "images": [
            {
                "filename": rec["filename"],
                "flash_order": rec["flash_order"],
                "original_build_path": rec["path"],
                "role": rec["role"],
                "sha256": rec["sha256"],
                "size": rec["size"],
            }
            for rec in prepared["images"]
        ],
        "ncs_version": ncs,
        "project": PROJECT,
        "schema_version": SCHEMA_VERSION,
        "target": {"board": target["board"], "id": target["id"]},
        "version": version,
    }


def _zip_info(name):
    """Fixed deterministic member metadata: 1980-01-01, mode 0644, Unix
    creator, no extra fields or comments."""
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def _write_zip(path, members):
    """Write a deterministic ZIP.  Members are an ordered list of
    ``(name, bytes)``; every member uses the same compression."""
    with zipfile.ZipFile(
        path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as zf:
        for name, data in members:
            zf.writestr(_zip_info(name), data)


def _sums_text(items):
    """GNU-style sorted SHA256SUMS text: '<64 lowercase hex>  <name>'."""
    lines = ["%s  %s" % (digest, name) for name, digest in sorted(items)]
    return ("\n".join(lines) + "\n").encode("ascii")


def _zip_name(target, version):
    return "le-audio-receiver-v%s-%s-factory.zip" % (version, target["id"])


def _build_target_zip(target, prepared, version, commit, ncs, staging):
    """Write one target ZIP into staging; returns (path, name, sha256)."""
    images = prepared["images"]
    manifest_bytes = (
        json.dumps(
            _manifest(target, prepared, version, commit, ncs), indent=2, sort_keys=True
        )
        + "\n"
    ).encode("utf-8")

    members = [("FLASHING.md", prepared["note_data"])]
    members += [(rec["filename"], rec["data"]) for rec in images]
    members += [("release-manifest.json", manifest_bytes)]

    sums_items = [("FLASHING.md", hashlib.sha256(prepared["note_data"]).hexdigest())]
    sums_items += [(rec["filename"], rec["sha256"]) for rec in images]
    sums_items += [
        ("release-manifest.json", hashlib.sha256(manifest_bytes).hexdigest())
    ]
    members += [("SHA256SUMS", _sums_text(sums_items))]

    name = _zip_name(target, version)
    path = os.path.join(staging, name)
    _write_zip(path, members)
    with open(path, "rb") as fh:
        digest = hashlib.sha256(fh.read()).hexdigest()
    return path, name, digest


def _run(args):
    version = _validate_version(args.version)
    commit = _validate_git_commit(args.git_commit)
    ncs = _validate_ncs_version(args.ncs_version)
    build_root = os.path.abspath(args.build_root)
    output_dir = os.path.abspath(args.output_dir)
    repo_root = _repo_root()

    # 1. Validate metadata and every input for both targets before any
    #    output-directory creation.
    prepared = [_prepare_target(target, build_root, repo_root) for target in TARGETS]

    # 2. Require the output directory absent.
    if os.path.lexists(output_dir):
        raise PackagerError("output directory already exists: %s" % output_dir)

    # 3. The output parent may be created after validation.
    parent = os.path.dirname(output_dir)
    os.makedirs(parent, exist_ok=True)

    # 4. Build the whole release set in one private staging sibling, then
    #    atomically rename it into place.  On failure, clean only staging
    #    and leave the final output directory absent.
    staging = tempfile.mkdtemp(prefix=STAGING_PREFIX, dir=parent)
    try:
        zip_results = [
            _build_target_zip(target, prep, version, commit, ncs, staging)
            for target, prep in zip(TARGETS, prepared)
        ]
        top_items = sorted((name, digest) for _path, name, digest in zip_results)
        with open(os.path.join(staging, "SHA256SUMS"), "wb") as fh:
            fh.write(_sums_text(top_items))
        os.rename(staging, output_dir)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise

    for _path, name, _digest in zip_results:
        print("%s: wrote %s" % ("package-firmware-release", name))
    print("package-firmware-release: wrote SHA256SUMS")


class _ArgumentParser(argparse.ArgumentParser):
    """Argparse variant that emits exactly one clean stderr diagnostic
    without a usage block or traceback."""

    def error(self, message):
        self.exit(2, "%s%s\n" % (ERROR_PREFIX, message))


def _build_parser():
    parser = _ArgumentParser(prog="package-firmware-release", add_help=True)
    parser.add_argument(
        "--version", required=True, help="project version MAJOR.MINOR.PATCH"
    )
    parser.add_argument("--git-commit", required=True, help="40 lowercase hex commit")
    parser.add_argument("--ncs-version", required=True, help="vMAJOR.MINOR.PATCH")
    parser.add_argument("--build-root", required=True, help="build output directory")
    parser.add_argument("--output-dir", required=True, help="final output directory")
    return parser


def main(argv=None):
    parser = _build_parser()
    args = parser.parse_args(argv)
    try:
        _run(args)
    except (PackagerError, OSError) as exc:
        print("%s%s" % (ERROR_PREFIX, exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
