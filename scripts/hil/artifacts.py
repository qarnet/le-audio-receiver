"""Strict immutable HIL firmware artifact resolver and staging.

No archive is extracted with ``ZipFile.extract*``. Every accepted member is
read, hashed, and explicitly written into a private staging root outside the
repository. Returned ``ArtifactSet`` paths are immutable data only after all
archive contracts have passed.
"""

import hashlib
import io
import json
import os
import shutil
import stat
import tempfile
import zipfile
from dataclasses import dataclass, field
from types import MappingProxyType
import re

import hil.lifecycle as lifecycle

MAX_ARCHIVE_BYTES = 16 * 1024 * 1024
MAX_MEMBER_BYTES = 8 * 1024 * 1024
MAX_TOTAL_MEMBER_BYTES = 20 * 1024 * 1024
FIXED_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
EXPECTED_MODE = 0o100644
EXPECTED_COMPRESSION = zipfile.ZIP_DEFLATED

RECEIVER_MEMBERS = (
    "FLASHING.md",
    "cpuapp.hex",
    "flpr.hex",
    "release-manifest.json",
    "SHA256SUMS",
)
SOURCE_MEMBERS = (
    "cpunet.hex",
    "cpuapp.hex",
    "source-manifest.json",
    "SHA256SUMS",
)

_ARTIFACT_SET_TOKEN = object()
_VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")


class ArtifactError(ValueError):
    """Artifact input or integrity boundary failure."""


@dataclass(frozen=True)
class ArtifactImage:
    role: str
    filename: str
    flash_order: int
    sha256: str
    size: int
    path: str


@dataclass(frozen=True)
class ArtifactIdentity:
    kind: str
    archive_path: str
    outer_sha256: str
    size: int
    manifest: MappingProxyType
    member_sha256: MappingProxyType


@dataclass(frozen=True)
class ArtifactSet:
    """Validated exact receiver/source archives and private staged images."""

    receiver: ArtifactIdentity
    source: ArtifactIdentity
    receiver_images: tuple
    source_images: tuple
    staging_root: str
    _token: object = field(repr=False, compare=False, default=None)

    def evidence(self):
        return {
            "receiver": _identity_evidence(self.receiver, self.receiver_images),
            "source": _identity_evidence(self.source, self.source_images),
        }


def _identity_evidence(identity, images):
    return {
        "kind": identity.kind,
        "archive_filename": os.path.basename(identity.archive_path),
        "archive_sha256": identity.outer_sha256,
        "archive_size": identity.size,
        "manifest": dict(identity.manifest),
        "member_sha256": dict(identity.member_sha256),
        "images": [
            {
                "role": image.role,
                "filename": image.filename,
                "flash_order": image.flash_order,
                "sha256": image.sha256,
                "size": image.size,
            }
            for image in images
        ],
    }


def _repo_root():
    return os.path.realpath(lifecycle.default_repo_root())


def _is_inside(path, root):
    return path == root or path.startswith(root + os.sep)


def _validate_archive_path(path, *, kind=None):
    if not isinstance(path, str) or not path or not os.path.isabs(path):
        raise ArtifactError("artifact path must be absolute")
    try:
        st = os.lstat(path)
    except OSError as exc:
        raise ArtifactError("artifact cannot be read: %s" % exc) from None
    if stat.S_ISLNK(st.st_mode):
        raise ArtifactError("artifact must not be a symlink: %s" % path)
    if not stat.S_ISREG(st.st_mode):
        raise ArtifactError("artifact is not a regular file: %s" % path)
    if st.st_size <= 0:
        raise ArtifactError("artifact is empty: %s" % path)
    if st.st_size > MAX_ARCHIVE_BYTES:
        raise ArtifactError("artifact exceeds size limit: %s" % path)
    canonical = os.path.realpath(path)
    if canonical != path:
        raise ArtifactError("artifact path must be canonical: %s" % path)
    if _is_inside(canonical, _repo_root()):
        raise ArtifactError("artifact must be outside repository: %s" % path)
    name = os.path.basename(canonical)
    if not name.endswith(".zip"):
        raise ArtifactError("artifact path must end in .zip: %s" % path)
    if kind == "receiver":
        match = re.fullmatch(
            r"le-audio-receiver-v((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
            r")-nrf54l15-xiao-factory\.zip",
            name,
        )
        if match is None:
            raise ArtifactError("receiver artifact filename mismatch: %s" % name)
    return canonical, st


def _read_file_no_follow_limited(path, label, maximum):
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(path, flags)
    except OSError as exc:
        raise ArtifactError("%s cannot be opened: %s" % (label, exc)) from None
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_size <= 0:
            raise ArtifactError("%s changed while opening" % label)
        if st.st_size > maximum:
            raise ArtifactError("%s exceeds size limit" % label)
        data = bytearray()
        while True:
            chunk = os.read(fd, 65536)
            if not chunk:
                break
            data.extend(chunk)
            if len(data) > maximum:
                raise ArtifactError("%s exceeds size limit" % label)
        if len(data) != st.st_size:
            raise ArtifactError("%s changed while reading" % label)
        return bytes(data), st
    finally:
        os.close(fd)


def _validate_member_name(name):
    if not isinstance(name, str) or not name or name in (".", ".."):
        raise ArtifactError("invalid archive member name")
    if name.startswith(("/", "\\")) or "/" in name or "\\" in name:
        raise ArtifactError("archive member path is not a basename: %r" % name)
    if ".." in name:
        raise ArtifactError("archive member traversal name: %r" % name)


def _validate_zip_infos(infos, expected):
    if len(infos) != len(expected):
        raise ArtifactError("archive member count mismatch")
    names = []
    total = 0
    for info in infos:
        _validate_member_name(info.filename)
        names.append(info.filename)
        if info.is_dir() or info.filename.endswith("/"):
            raise ArtifactError("archive contains directory member: %s" % info.filename)
        if info.flag_bits & 0x1:
            raise ArtifactError("archive contains encrypted member: %s" % info.filename)
        if info.flag_bits != 0:
            raise ArtifactError(
                "archive member flags are unsupported: %s" % info.filename
            )
        if info.compress_type != EXPECTED_COMPRESSION:
            raise ArtifactError(
                "archive has unsupported compression: %s" % info.filename
            )
        if info.date_time != FIXED_TIMESTAMP:
            raise ArtifactError("archive member timestamp mismatch: %s" % info.filename)
        if (
            info.create_system != 3
            or (info.external_attr >> 16) != EXPECTED_MODE
            or (info.external_attr & 0xFFFF) != 0
        ):
            raise ArtifactError("archive member mode mismatch: %s" % info.filename)
        if info.extra or info.comment:
            raise ArtifactError(
                "archive member metadata must be empty: %s" % info.filename
            )
        if info.file_size <= 0 or info.file_size > MAX_MEMBER_BYTES:
            raise ArtifactError("archive member size invalid: %s" % info.filename)
        if info.compress_size <= 0 or info.compress_size > MAX_MEMBER_BYTES:
            raise ArtifactError(
                "archive member compressed size invalid: %s" % info.filename
            )
        if info.header_offset < 0:
            raise ArtifactError("archive member offset invalid: %s" % info.filename)
        total += info.file_size
        if total > MAX_TOTAL_MEMBER_BYTES:
            raise ArtifactError("archive uncompressed data exceeds size limit")
    if len(set(names)) != len(names):
        raise ArtifactError("archive contains duplicate member names")
    if tuple(names) != tuple(expected):
        raise ArtifactError("archive member set/order mismatch")


def _load_json(raw, label):
    try:
        text = raw.decode("utf-8")
        value = json.loads(text)
    except (UnicodeDecodeError, ValueError) as exc:
        raise ArtifactError("%s is not valid UTF-8 JSON: %s" % (label, exc)) from None
    if not isinstance(value, dict):
        raise ArtifactError("%s must be a JSON object" % label)
    canonical = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")
    if raw != canonical:
        raise ArtifactError("%s is not canonical JSON" % label)
    return value


def _validate_hex_bytes(raw, label):
    if any(byte >= 0x80 for byte in raw):
        raise ArtifactError("%s is not ASCII text" % label)
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError:
        raise ArtifactError("%s is not ASCII text" % label) from None
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines = lines[:-1]
    if not lines:
        raise ArtifactError("%s contains no records" % label)
    eof_count = 0
    for line in lines:
        if line.endswith("\r"):
            line = line[:-1]
        if not line.strip():
            raise ArtifactError("%s contains a blank record" % label)
        if not line.startswith(":"):
            raise ArtifactError("%s record does not start with ':'" % label)
        body = line[1:]
        if len(body) % 2 or not re.fullmatch(r"[0-9A-Fa-f]*", body):
            raise ArtifactError("%s record is not valid hexadecimal" % label)
        record = bytes.fromhex(body)
        if len(record) < 5:
            raise ArtifactError("%s record is too short" % label)
        declared = record[0]
        address = (record[1] << 8) | record[2]
        record_type = record[3]
        if len(record[4:-1]) != declared:
            raise ArtifactError("%s byte-count mismatch" % label)
        if sum(record) & 0xFF:
            raise ArtifactError("%s record checksum does not sum to zero" % label)
        if record_type == 1:
            if declared != 0 or address != 0:
                raise ArtifactError("%s malformed EOF record" % label)
            eof_count += 1
        elif eof_count:
            raise ArtifactError("%s record appears after EOF" % label)
    if eof_count != 1:
        raise ArtifactError("%s must contain exactly one EOF record" % label)


def _parse_sums(raw, expected_names, member_data):
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError:
        raise ArtifactError("SHA256SUMS is not ASCII") from None
    expected_lines = []
    for name in sorted(expected_names):
        expected_lines.append(
            "%s  %s" % (hashlib.sha256(member_data[name]).hexdigest(), name)
        )
    expected = ("\n".join(expected_lines) + "\n").encode("ascii")
    if raw != expected:
        raise ArtifactError("SHA256SUMS contract mismatch")
    return MappingProxyType(
        {name: hashlib.sha256(member_data[name]).hexdigest() for name in expected_names}
    )


def _validate_image_manifest(manifest, expected_images, *, kind):
    if kind == "receiver":
        expected_root = {
            "git_commit",
            "images",
            "ncs_version",
            "project",
            "schema_version",
            "target",
            "version",
        }
        if set(manifest) != expected_root:
            raise ArtifactError("receiver manifest key set mismatch")
        if (
            type(manifest.get("schema_version")) is not int
            or manifest.get("schema_version") != 1
            or manifest.get("project") != "le-audio-receiver"
        ):
            raise ArtifactError("receiver manifest schema/project mismatch")
        if not isinstance(manifest.get("version"), str) or not _VERSION_RE.fullmatch(
            manifest["version"]
        ):
            raise ArtifactError("receiver manifest version invalid")
        if not _is_commit(manifest.get("git_commit")) or not _is_ncs(
            manifest.get("ncs_version")
        ):
            raise ArtifactError("receiver manifest provenance invalid")
        target = manifest.get("target")
        if target != {"board": "nrf54l15dk/nrf54l15/cpuapp", "id": "nrf54l15-xiao"}:
            raise ArtifactError("receiver manifest target mismatch")
    else:
        expected_root = {
            "board",
            "firmware_id",
            "git_commit",
            "images",
            "ncs_version",
            "schema_version",
        }
        if set(manifest) != expected_root:
            raise ArtifactError("source manifest key set mismatch")
        if (
            type(manifest.get("schema_version")) is not int
            or manifest.get("schema_version") != 1
        ):
            raise ArtifactError("source manifest schema mismatch")
        if manifest.get("board") != "nrf5340dk/nrf5340/cpuapp":
            raise ArtifactError("source manifest board mismatch")
        if manifest.get("firmware_id") != "le-audio-hil-source-rh1":
            raise ArtifactError("source manifest firmware id mismatch")
        if not _is_commit(manifest.get("git_commit")) or not _is_ncs(
            manifest.get("ncs_version")
        ):
            raise ArtifactError("source manifest provenance invalid")

    images = manifest.get("images")
    if not isinstance(images, list) or len(images) != len(expected_images):
        raise ArtifactError("artifact manifest images invalid")
    receiver_build_paths = {
        "cpuapp.hex": "nrf54l15/le-audio-receiver/zephyr/zephyr.hex",
        "flpr.hex": "nrf54l15/flpr/zephyr/zephyr.hex",
    }
    for record, expected in zip(images, expected_images):
        if not isinstance(record, dict):
            raise ArtifactError("artifact manifest image is not an object")
        allowed = {"filename", "flash_order", "role", "sha256", "size"}
        if kind == "receiver":
            allowed.add("original_build_path")
        if set(record) != allowed:
            raise ArtifactError("artifact manifest image key set mismatch")
        if (
            not isinstance(record["filename"], str)
            or type(record["flash_order"]) is not int
            or not isinstance(record["role"], str)
            or not isinstance(record["sha256"], str)
            or type(record["size"]) is not int
        ):
            raise ArtifactError("artifact manifest image field type mismatch")
        for key in ("filename", "flash_order", "role", "sha256", "size"):
            if record.get(key) != expected[key]:
                raise ArtifactError(
                    "artifact manifest image mismatch: %s" % expected["filename"]
                )
        if kind == "receiver":
            build_path = record.get("original_build_path")
            if build_path != receiver_build_paths[expected["filename"]]:
                raise ArtifactError("receiver manifest original build path invalid")


def _is_commit(value):
    return (
        isinstance(value, str)
        and len(value) == 40
        and all(c in "0123456789abcdef" for c in value)
    )


def _is_ncs(value):
    if not isinstance(value, str) or not value.startswith("v"):
        return False
    parts = value[1:].split(".")
    return len(parts) == 3 and all(
        part.isdigit() and str(int(part)) == part for part in parts
    )


def _validate_note(data):
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        raise ArtifactError("FLASHING.md is not valid UTF-8") from None


def _resolve_one(path, *, kind):
    expected_members = RECEIVER_MEMBERS if kind == "receiver" else SOURCE_MEMBERS
    archive_path, pre_stat = _validate_archive_path(path, kind=kind)
    raw, opened_stat = _read_file_no_follow_limited(
        archive_path, "%s artifact" % kind, MAX_ARCHIVE_BYTES
    )
    if opened_stat.st_dev != pre_stat.st_dev or opened_stat.st_ino != pre_stat.st_ino:
        raise ArtifactError("%s artifact changed before read" % kind)
    if len(raw) != pre_stat.st_size:
        raise ArtifactError("%s artifact changed while reading" % kind)
    outer = hashlib.sha256(raw).hexdigest()
    try:
        with zipfile.ZipFile(io.BytesIO(raw), "r") as zf:
            if zf.comment:
                raise ArtifactError("archive comment is not allowed")
            infos = zf.infolist()
            _validate_zip_infos(infos, expected_members)
            if zf.testzip() is not None:
                raise ArtifactError("archive member CRC/read failure")
            data = {}
            for info in infos:
                try:
                    payload = zf.read(info)
                except (OSError, RuntimeError, zipfile.BadZipFile) as exc:
                    raise ArtifactError(
                        "archive member CRC/read failure: %s" % exc
                    ) from None
                if len(payload) != info.file_size:
                    raise ArtifactError(
                        "archive member size changed: %s" % info.filename
                    )
                data[info.filename] = payload
    except zipfile.BadZipFile as exc:
        raise ArtifactError("invalid ZIP artifact: %s" % exc) from None

    image_names = (
        ("cpuapp.hex", "flpr.hex")
        if kind == "receiver"
        else ("cpunet.hex", "cpuapp.hex")
    )
    manifest_name = (
        "release-manifest.json" if kind == "receiver" else "source-manifest.json"
    )
    if kind == "receiver":
        _validate_note(data["FLASHING.md"])
    manifest = _load_json(data[manifest_name], manifest_name)
    expected_images = []
    roles = (
        (("cpuapp", 0), ("flpr", 1))
        if kind == "receiver"
        else (("cpunet", 0), ("cpuapp", 1))
    )
    for filename, (role, flash_order) in zip(image_names, roles):
        payload = data[filename]
        _validate_hex_bytes(payload, "%s %s image" % (kind, role))
        expected_images.append(
            {
                "filename": filename,
                "flash_order": flash_order,
                "role": role,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload),
            }
        )
    _validate_image_manifest(manifest, expected_images, kind=kind)
    if kind == "receiver":
        filename_match = re.fullmatch(
            r"le-audio-receiver-v(.+)-nrf54l15-xiao-factory\.zip",
            os.path.basename(archive_path),
        )
        if filename_match is None:
            raise ArtifactError("receiver artifact filename mismatch")
        filename_version = filename_match.group(1)
        if manifest["version"] != filename_version:
            raise ArtifactError("receiver artifact filename/version mismatch")
    sums_names = tuple(name for name in expected_members if name != "SHA256SUMS")
    member_sha256 = _parse_sums(data["SHA256SUMS"], sums_names, data)
    return (
        ArtifactIdentity(
            kind=kind,
            archive_path=archive_path,
            outer_sha256=outer,
            size=len(raw),
            manifest=MappingProxyType(manifest),
            member_sha256=member_sha256,
        ),
        data,
        tuple(expected_images),
    )


def read_verified_archive(identity):
    """Return original archive bytes after no-follow revalidation.

    Matrix evidence copies must come from the same immutable archive identity
    that supplied staged images.  Reopen without following links, re-hash, and
    re-parse the complete archive immediately before returning bytes.
    """
    if not isinstance(identity, ArtifactIdentity) or identity.kind not in (
        "receiver",
        "source",
    ):
        raise ArtifactError("artifact identity shape mismatch")
    path, pre_stat = _validate_archive_path(identity.archive_path, kind=identity.kind)
    raw, opened_stat = _read_file_no_follow_limited(
        path, "%s artifact" % identity.kind, MAX_ARCHIVE_BYTES
    )
    if (
        opened_stat.st_dev != pre_stat.st_dev
        or opened_stat.st_ino != pre_stat.st_ino
        or len(raw) != pre_stat.st_size
    ):
        raise ArtifactError("%s artifact changed before evidence copy" % identity.kind)
    if (
        hashlib.sha256(raw).hexdigest() != identity.outer_sha256
        or len(raw) != identity.size
    ):
        raise ArtifactError("%s artifact changed after validation" % identity.kind)
    validated, _data, _images = _resolve_one(path, kind=identity.kind)
    if (
        validated.outer_sha256 != identity.outer_sha256
        or validated.size != identity.size
        or dict(validated.manifest) != dict(identity.manifest)
        or dict(validated.member_sha256) != dict(identity.member_sha256)
    ):
        raise ArtifactError(
            "%s artifact contract changed after validation" % identity.kind
        )
    return raw


def _write_staged(path, data):
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    fd = os.open(path, flags, 0o600)
    try:
        view = memoryview(data)
        while view:
            written = os.write(fd, view)
            view = view[written:]
        os.fsync(fd)
    finally:
        os.close(fd)
    st = os.lstat(path)
    if (
        stat.S_ISLNK(st.st_mode)
        or not stat.S_ISREG(st.st_mode)
        or st.st_size != len(data)
    ):
        raise ArtifactError("staged image validation failed: %s" % path)
    try:
        os.chmod(path, 0o400)
    except OSError as exc:
        raise ArtifactError("cannot protect staged image: %s" % exc) from None


def _validate_staging_parent(staging_parent):
    if staging_parent is None:
        staging_parent = tempfile.gettempdir()
    if not isinstance(staging_parent, str) or not os.path.isabs(staging_parent):
        raise ArtifactError("staging parent must be an absolute path")
    if not os.path.isdir(staging_parent) or os.path.islink(staging_parent):
        raise ArtifactError("staging parent must be existing real directory")
    canonical = os.path.realpath(staging_parent)
    if canonical != staging_parent:
        raise ArtifactError("staging parent must be canonical")
    if _is_inside(canonical, _repo_root()):
        raise ArtifactError("staging parent must be outside repository")
    return canonical


def resolve_artifacts(receiver_artifact, source_artifact, *, staging_parent=None):
    """Validate two exact archives then stage their image bytes outside repo.

    No staging directory exists until both archives fully validate.
    """
    receiver, receiver_data, receiver_images = _resolve_one(
        receiver_artifact, kind="receiver"
    )
    source, source_data, source_images = _resolve_one(source_artifact, kind="source")
    parent = _validate_staging_parent(staging_parent)
    staging = tempfile.mkdtemp(prefix="hil-artifacts-", dir=parent)
    os.chmod(staging, 0o700)
    try:
        staged_receiver = []
        for image in receiver_images:
            path = os.path.join(staging, "receiver-" + image["filename"])
            _write_staged(path, receiver_data[image["filename"]])
            staged_receiver.append(ArtifactImage(path=path, **image))
        staged_source = []
        for image in source_images:
            path = os.path.join(staging, "source-" + image["filename"])
            _write_staged(path, source_data[image["filename"]])
            staged_source.append(ArtifactImage(path=path, **image))
        return ArtifactSet(
            receiver=receiver,
            source=source,
            receiver_images=tuple(staged_receiver),
            source_images=tuple(staged_source),
            staging_root=staging,
            _token=_ARTIFACT_SET_TOKEN,
        )
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def cleanup_artifacts(artifacts):
    """Remove private staged images only. Original archives are untouched."""
    if (
        not isinstance(artifacts, ArtifactSet)
        or artifacts._token is not _ARTIFACT_SET_TOKEN
    ):
        raise TypeError("artifacts must be an ArtifactSet")
    root = artifacts.staging_root
    if not os.path.isabs(root) or _is_inside(os.path.realpath(root), _repo_root()):
        raise ArtifactError("invalid artifact staging root")
    parent = os.path.dirname(root)
    if os.path.basename(root).startswith("hil-artifacts-") and os.path.isdir(parent):
        shutil.rmtree(root)
    elif os.path.lexists(root):
        raise ArtifactError("invalid artifact staging root")


def revalidate_artifact_set(artifacts):
    """Re-hash original ZIPs and staged images immediately before flashing."""
    if (
        not isinstance(artifacts, ArtifactSet)
        or artifacts._token is not _ARTIFACT_SET_TOKEN
    ):
        raise ArtifactError("artifacts must be an ArtifactSet")
    _validate_artifact_set_shape(artifacts)
    for identity in (artifacts.receiver, artifacts.source):
        path, _st = _validate_archive_path(identity.archive_path, kind=identity.kind)
        raw, _opened = _read_file_no_follow_limited(
            path, "%s artifact" % identity.kind, MAX_ARCHIVE_BYTES
        )
        if (
            hashlib.sha256(raw).hexdigest() != identity.outer_sha256
            or len(raw) != identity.size
        ):
            raise ArtifactError("%s artifact changed after validation" % identity.kind)
        validated, _data, _images = _resolve_one(path, kind=identity.kind)
        if (
            validated.outer_sha256 != identity.outer_sha256
            or dict(validated.manifest) != dict(identity.manifest)
            or dict(validated.member_sha256) != dict(identity.member_sha256)
        ):
            raise ArtifactError(
                "%s artifact contract changed after validation" % identity.kind
            )
    for image in artifacts.receiver_images + artifacts.source_images:
        if not os.path.isabs(image.path) or _is_inside(
            os.path.realpath(image.path), _repo_root()
        ):
            raise ArtifactError("staged image path is unsafe")
        try:
            st = os.lstat(image.path)
        except OSError as exc:
            raise ArtifactError("staged image missing: %s" % exc) from None
        if (
            stat.S_ISLNK(st.st_mode)
            or not stat.S_ISREG(st.st_mode)
            or st.st_size != image.size
            or stat.S_IMODE(st.st_mode) != 0o400
        ):
            raise ArtifactError(
                "staged image changed after validation: %s" % image.filename
            )
        data, _opened = _read_file_no_follow_limited(
            image.path, "staged image", MAX_MEMBER_BYTES
        )
        if hashlib.sha256(data).hexdigest() != image.sha256:
            raise ArtifactError("staged image hash drift: %s" % image.filename)


def _validate_artifact_set_shape(artifacts):
    expected = (
        (
            artifacts.receiver,
            artifacts.receiver_images,
            "receiver",
            (("cpuapp", "cpuapp.hex", 0), ("flpr", "flpr.hex", 1)),
        ),
        (
            artifacts.source,
            artifacts.source_images,
            "source",
            (("cpunet", "cpunet.hex", 0), ("cpuapp", "cpuapp.hex", 1)),
        ),
    )
    if not os.path.isabs(artifacts.staging_root) or _is_inside(
        os.path.realpath(artifacts.staging_root), _repo_root()
    ):
        raise ArtifactError("artifact staging root must be outside repository")
    for identity, images, kind, expected_images in expected:
        if not isinstance(identity, ArtifactIdentity) or identity.kind != kind:
            raise ArtifactError("artifact identity kind mismatch")
        if (
            not isinstance(identity.archive_path, str)
            or not isinstance(identity.outer_sha256, str)
            or len(identity.outer_sha256) != 64
            or not isinstance(identity.size, int)
            or identity.size <= 0
            or not isinstance(identity.manifest, MappingProxyType)
            or not isinstance(identity.member_sha256, MappingProxyType)
        ):
            raise ArtifactError("artifact identity shape mismatch")
        if not _is_commit(identity.manifest.get("git_commit")) or not _is_ncs(
            identity.manifest.get("ncs_version")
        ):
            raise ArtifactError("artifact identity provenance mismatch")
        if kind == "receiver":
            if not isinstance(
                identity.manifest.get("version"), str
            ) or not _VERSION_RE.fullmatch(identity.manifest["version"]):
                raise ArtifactError("receiver artifact identity version missing")
        elif identity.manifest.get("firmware_id") != "le-audio-hil-source-rh1":
            raise ArtifactError("source artifact identity mismatch")
        if not isinstance(images, tuple) or len(images) != len(expected_images):
            raise ArtifactError("artifact image tuple mismatch")
        for image, expected_image in zip(images, expected_images):
            role, filename, order = expected_image
            if (
                not isinstance(image, ArtifactImage)
                or image.role != role
                or image.filename != filename
                or image.flash_order != order
                or not isinstance(image.sha256, str)
                or len(image.sha256) != 64
                or not isinstance(image.size, int)
                or image.size <= 0
            ):
                raise ArtifactError("artifact image role/order mismatch")


def validate_artifact_set(artifacts):
    """Validate opaque artifact-set shape without reading mutable inputs."""
    if (
        not isinstance(artifacts, ArtifactSet)
        or artifacts._token is not _ARTIFACT_SET_TOKEN
    ):
        raise ArtifactError("artifacts must be an ArtifactSet")
    _validate_artifact_set_shape(artifacts)


def image_by_role(images, role):
    matches = [image for image in images if image.role == role]
    if len(matches) != 1:
        raise ArtifactError("artifact image role mismatch: %s" % role)
    return matches[0]
