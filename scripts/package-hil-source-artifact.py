#!/usr/bin/env python3
"""Deterministic HIL source firmware artifact packager (stdlib only).

Packages validated nRF5340 HIL source build outputs into one immutable ZIP.
The public contract is intentionally independent from FR1 so accepted FR1
artifact bytes remain unchanged.
"""

import argparse
import hashlib
import json
import os
import re
import stat
import sys
import tempfile
import zipfile

ERROR_PREFIX = "package-hil-source-artifact: error: "
STAGING_PREFIX = ".hil-source-artifact-"
SCHEMA_VERSION = 1
FIRMWARE_ID = "le-audio-hil-source-rh1"
BOARD = "nrf5340dk/nrf5340/cpuapp"

_VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
_GIT_COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
_HEX_RE = re.compile(r"[0-9A-Fa-f]*")

IMAGES = (
    {
        "role": "cpunet",
        "path": "hci_ipc/zephyr/zephyr.hex",
        "filename": "cpunet.hex",
        "flash_order": 0,
    },
    {
        "role": "cpuapp",
        "path": "app/zephyr/zephyr.hex",
        "filename": "cpuapp.hex",
        "flash_order": 1,
    },
)


class PackagerError(Exception):
    """Caller error reported as one stable diagnostic."""


def _validate_git_commit(value):
    if not _GIT_COMMIT_RE.fullmatch(value):
        raise PackagerError(
            "invalid git commit %r (expected exactly 40 lowercase hexadecimal "
            "characters)" % value
        )
    return value


def _validate_ncs_version(value):
    if (
        not isinstance(value, str)
        or not value.startswith("v")
        or not _VERSION_RE.fullmatch(value[1:])
    ):
        raise PackagerError(
            "invalid NCS version %r (expected canonical vMAJOR.MINOR.PATCH, "
            "each part numeric without leading zeros)" % value
        )
    return value


def _validate_regular_file(path, label):
    try:
        st = os.lstat(path)
    except OSError as exc:
        raise PackagerError("%s cannot be read: %s (%s)" % (label, path, exc)) from None
    if stat.S_ISLNK(st.st_mode):
        raise PackagerError("%s is a symlink: %s" % (label, path))
    if not stat.S_ISREG(st.st_mode):
        raise PackagerError("%s is not a regular file: %s" % (label, path))
    if st.st_size == 0:
        raise PackagerError("%s is empty: %s" % (label, path))
    return st


def _read_regular_file(path, label):
    initial = _validate_regular_file(path, label)
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(path, flags)
    except OSError as exc:
        raise PackagerError(
            "%s cannot be opened: %s (%s)" % (label, path, exc)
        ) from None
    try:
        st = os.fstat(fd)
        if (
            not stat.S_ISREG(st.st_mode)
            or st.st_size == 0
            or st.st_dev != initial.st_dev
            or st.st_ino != initial.st_ino
        ):
            raise PackagerError("%s changed while opening: %s" % (label, path))
        chunks = []
        while True:
            chunk = os.read(fd, 65536)
            if not chunk:
                break
            chunks.append(chunk)
        data = b"".join(chunks)
        if len(data) != st.st_size:
            raise PackagerError("%s changed while reading: %s" % (label, path))
        return data
    finally:
        os.close(fd)


def _validate_hex_bytes(raw, label, path):
    """Accept same Intel HEX contract as FR1 without importing its CLI module."""
    if any(byte >= 0x80 for byte in raw):
        raise PackagerError("%s is not ASCII text: %s" % (label, path))
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError:
        raise PackagerError("%s is not ASCII text: %s" % (label, path)) from None
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines = lines[:-1]
    if not lines:
        raise PackagerError("%s contains no records: %s" % (label, path))
    eof_count = 0
    for line in lines:
        if line.endswith("\r"):
            line = line[:-1]
        if not line.strip():
            raise PackagerError("%s contains a blank record" % label)
        if not line.startswith(":"):
            raise PackagerError("%s record does not start with ':': %s" % (label, path))
        body = line[1:]
        if len(body) % 2:
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
        if len(record[4:-1]) != declared:
            raise PackagerError(
                "%s byte-count mismatch: declared %d, actual %d"
                % (label, declared, len(record[4:-1]))
            )
        if sum(record) & 0xFF:
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


def _prepare_images(build_root):
    prepared = []
    for image in IMAGES:
        path = os.path.join(build_root, image["path"])
        label = "%s image" % image["role"]
        data = _read_regular_file(path, label)
        _validate_hex_bytes(data, label, path)
        prepared.append(
            {
                **image,
                "data": data,
                "sha256": hashlib.sha256(data).hexdigest(),
                "size": len(data),
            }
        )
    return prepared


def _validate_build_root(path):
    if not isinstance(path, str) or not path:
        raise PackagerError("build root must be a nonempty path")
    absolute = os.path.abspath(path)
    try:
        st = os.lstat(absolute)
    except OSError as exc:
        raise PackagerError(
            "build root cannot be read: %s (%s)" % (absolute, exc)
        ) from None
    if stat.S_ISLNK(st.st_mode) or not stat.S_ISDIR(st.st_mode):
        raise PackagerError("build root must be a non-symlink directory: %s" % absolute)
    return absolute


def _manifest(images, commit, ncs_version):
    return {
        "board": BOARD,
        "firmware_id": FIRMWARE_ID,
        "git_commit": commit,
        "images": [
            {
                "filename": image["filename"],
                "flash_order": image["flash_order"],
                "role": image["role"],
                "sha256": image["sha256"],
                "size": image["size"],
            }
            for image in images
        ],
        "ncs_version": ncs_version,
        "schema_version": SCHEMA_VERSION,
    }


def _zip_info(name):
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def _sums_text(items):
    return (
        "\n".join("%s  %s" % (digest, name) for name, digest in sorted(items)) + "\n"
    ).encode("ascii")


def _write_zip(path, members):
    with zipfile.ZipFile(
        path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as zf:
        for name, data in members:
            zf.writestr(_zip_info(name), data)


def _validate_output(output):
    if not os.path.isabs(output):
        raise PackagerError("output path must be absolute: %s" % output)
    parent = os.path.dirname(output)
    if not os.path.isdir(parent):
        raise PackagerError("output parent does not exist: %s" % parent)
    if os.path.lexists(output):
        raise PackagerError("output path already exists: %s" % output)
    if os.path.islink(parent):
        raise PackagerError("output parent is a symlink: %s" % parent)
    canonical_parent = os.path.realpath(parent)
    if os.path.realpath(output) != os.path.join(
        canonical_parent, os.path.basename(output)
    ):
        raise PackagerError("output path escapes through a symlink: %s" % output)
    return canonical_parent, os.path.basename(output)


def _run(args):
    commit = _validate_git_commit(args.git_commit)
    ncs_version = _validate_ncs_version(args.ncs_version)
    build_root = _validate_build_root(args.build_root)
    output_parent, output_name = _validate_output(args.output)
    output = os.path.join(output_parent, output_name)
    images = _prepare_images(build_root)

    manifest_bytes = (
        json.dumps(_manifest(images, commit, ncs_version), indent=2, sort_keys=True)
        + "\n"
    ).encode("utf-8")
    members = [(image["filename"], image["data"]) for image in images]
    members.append(("source-manifest.json", manifest_bytes))
    sums = _sums_text(
        [(image["filename"], image["sha256"]) for image in images]
        + [("source-manifest.json", hashlib.sha256(manifest_bytes).hexdigest())]
    )
    members.append(("SHA256SUMS", sums))

    temporary = None
    try:
        fd, temporary = tempfile.mkstemp(
            prefix=STAGING_PREFIX, suffix=".zip", dir=output_parent
        )
        os.close(fd)
        _write_zip(temporary, members)
        try:
            # link() is same-directory and fails if another writer creates the
            # final path after validation. os.replace() would overwrite it.
            os.link(temporary, output)
        except FileExistsError:
            raise PackagerError("output path already exists: %s" % output) from None
        os.unlink(temporary)
        temporary = None
    except OSError as exc:
        raise PackagerError("cannot write output: %s" % exc) from None
    finally:
        if temporary is not None:
            try:
                os.unlink(temporary)
            except OSError:
                pass
    print("package-hil-source-artifact: wrote %s" % output_name)


class _ArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        self.exit(2, "%s%s\n" % (ERROR_PREFIX, message))


def _build_parser():
    parser = _ArgumentParser(prog="package-hil-source-artifact", add_help=True)
    parser.add_argument("--git-commit", required=True)
    parser.add_argument("--ncs-version", required=True)
    parser.add_argument("--build-root", required=True)
    parser.add_argument("--output", required=True)
    return parser


def main(argv=None):
    args = _build_parser().parse_args(argv)
    try:
        _run(args)
    except (PackagerError, OSError, zipfile.BadZipFile) as exc:
        print("%s%s" % (ERROR_PREFIX, exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
