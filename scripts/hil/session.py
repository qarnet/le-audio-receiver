"""Immutable, host-only same-family HIL fixture sessions.

Phase 1 creates one operator-selected nRF54L15 receiver/source binding under
an external session root.  A manifest records complete read-only probe and USB
identity evidence.  It is never updated: callers load it, prove its bytes and
metadata remain unchanged, then re-resolve current tty paths before later
target actions.
"""

import hashlib
import json
import os
import re
import stat
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from types import MappingProxyType

from hil import discovery, lifecycle, model

SESSION_SCHEMA_VERSION = 1
DEFAULT_SESSION_ROOT = "/tmp/opencode/hil-sessions"
SESSION_FILENAME = "devices.json"

XIAO_FIXTURE_ID = "local-xiao-nrf54l15-pair"
PROBE_SERIAL_RE = re.compile(r"^[A-Za-z0-9_.:-]+$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
RAW_HEX_RE = re.compile(r"^0x[0-9a-f]{8}$")
RFC3339_UTC_RE = re.compile(
    r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\.[0-9]+)?(?:Z|\+00:00)$"
)
TTY_PATH_RE = re.compile(r"^/dev/ttyACM[0-9]+$")
STABLE_UDEV_KEYS = (
    "ID_BUS",
    "ID_VENDOR_ID",
    "ID_MODEL_ID",
    "ID_SERIAL_SHORT",
    "ID_USB_INTERFACE_NUM",
    "ID_USB_DRIVER",
    "ID_PATH",
)
EXPECTED_XIAO_UDEV = {
    "ID_BUS": "usb",
    "ID_VENDOR_ID": "2886",
    "ID_MODEL_ID": "0066",
    "ID_USB_INTERFACE_NUM": "02",
    "ID_USB_DRIVER": "cdc_acm",
}


class HilSessionError(ValueError):
    """Raised when session creation, loading, or revalidation is unsafe."""


class _DuplicateKeyError(ValueError):
    def __init__(self, key):
        super().__init__(key)
        self.key = key


@dataclass(frozen=True)
class SessionRole:
    """One immutable expected firmware/probe/serial session record."""

    expected_firmware: MappingProxyType
    probe: discovery.ProbeIdentity
    serial: discovery.SerialIdentity


@dataclass(frozen=True)
class SessionManifest:
    """Loaded immutable session manifest and exact input bytes.

    ``assert_unchanged`` reopens the manifest itself.  It catches byte, inode,
    file-type, mode, canonical-path, and disappearance drift before discovery
    may start any external read-only process.
    """

    path: str
    raw_bytes: bytes
    sha256: str
    device: int
    inode: int
    mode: int
    session_id: str
    created_at_utc: str
    fixture_id: str
    fixture_sha256: str
    binding_sha256: str
    fixture_bytes: bytes
    binding_bytes: bytes
    roles: MappingProxyType

    def assert_unchanged(self):
        """Fail before hardware discovery when manifest file drifted."""
        path, raw, state = _read_manifest_file(self.path, self.session_id)
        if path != self.path:
            raise HilSessionError("session manifest path changed")
        if state["device"] != self.device or state["inode"] != self.inode:
            raise HilSessionError("session manifest replacement detected")
        if state["mode"] != self.mode or state["mode"] != 0o400:
            raise HilSessionError("session manifest permission changed")
        if raw != self.raw_bytes or _sha256(raw) != self.sha256:
            raise HilSessionError("session manifest bytes changed")


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _reject_duplicate_keys(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise _DuplicateKeyError(key)
        out[key] = value
    return out


def _reject_json_constant(value):
    raise ValueError("invalid JSON constant %s" % value)


def _parse_json_bytes(raw, label):
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise HilSessionError("%s is not valid UTF-8" % label) from None
    try:
        value = json.loads(
            text,
            object_pairs_hook=_reject_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except _DuplicateKeyError as exc:
        raise HilSessionError("duplicate key %r in %s" % (exc.key, label)) from None
    except (json.JSONDecodeError, ValueError) as exc:
        raise HilSessionError("invalid JSON in %s: %s" % (label, exc)) from None
    if not isinstance(value, dict):
        raise HilSessionError("%s must contain a JSON object" % label)
    return value


def _require_exact_keys(value, keys, label):
    if not isinstance(value, dict):
        raise HilSessionError("%s must be an object" % label)
    expected = set(keys)
    actual = set(value)
    unknown = actual - expected
    missing = expected - actual
    if unknown:
        raise HilSessionError("unknown key %r in %s" % (sorted(unknown)[0], label))
    if missing:
        raise HilSessionError("missing key %r in %s" % (sorted(missing)[0], label))


def _require_string(value, label):
    if not isinstance(value, str) or not value or "\x00" in value:
        raise HilSessionError("%s must be a nonempty string" % label)
    return value


def _require_sha256(value, label):
    value = _require_string(value, label)
    if not SHA256_RE.fullmatch(value):
        raise HilSessionError("%s must be 64 lowercase hexadecimal characters" % label)
    return value


def _require_session_id(value):
    if not isinstance(value, str) or lifecycle.RUN_ID_RE.fullmatch(value) is None:
        raise HilSessionError("session ID must match [A-Za-z0-9][A-Za-z0-9._-]{0,63}")
    return value


def _require_probe_serial(value, label):
    if not isinstance(value, str) or not PROBE_SERIAL_RE.fullmatch(value):
        raise HilSessionError("%s must match [A-Za-z0-9_.:-]+" % label)
    return value


def _require_utc(value):
    value = _require_string(value, "created_at_utc")
    if not RFC3339_UTC_RE.fullmatch(value):
        raise HilSessionError("created_at_utc must be RFC3339 UTC")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        raise HilSessionError("created_at_utc must be RFC3339 UTC") from None
    if parsed.tzinfo is None or parsed.utcoffset() != timedelta(0):
        raise HilSessionError("created_at_utc must be UTC")
    return value


def _variant_display(raw):
    if not RAW_HEX_RE.fullmatch(raw):
        raise HilSessionError("probe variant_raw must be raw hexadecimal")
    try:
        display = int(raw[2:], 16).to_bytes(4, byteorder="big").decode("ascii")
    except UnicodeDecodeError:
        raise HilSessionError("probe variant_raw must decode as ASCII") from None
    if not display or not display.isprintable():
        raise HilSessionError("probe variant_raw must decode as printable ASCII")
    return display


def _expected_firmware(role):
    if role == "receiver":
        return {"role": "receiver", "boot_contract": "le-audio-receiver-v1"}
    return {
        "role": "source",
        "firmware_id": "le-audio-hil-source-rh1",
        "protocol_version": 1,
    }


def _parse_role(role, value):
    _require_exact_keys(
        value, ("expected_firmware", "probe", "serial"), "%s role" % role
    )
    expected_firmware = value["expected_firmware"]
    expected = _expected_firmware(role)
    _require_exact_keys(expected_firmware, expected, "%s expected_firmware" % role)
    if role == "source" and (
        not isinstance(expected_firmware["protocol_version"], int)
        or isinstance(expected_firmware["protocol_version"], bool)
    ):
        raise HilSessionError(
            "source expected_firmware protocol_version must be an integer"
        )
    if expected_firmware != expected:
        raise HilSessionError("%s expected_firmware mismatch" % role)

    probe = value["probe"]
    _require_exact_keys(
        probe,
        (
            "backend",
            "family",
            "serial",
            "product",
            "target",
            "dpidr",
            "ap_idrs",
            "part",
            "variant",
            "variant_raw",
        ),
        "%s probe" % role,
    )
    serial_number = _require_probe_serial(probe["serial"], "%s probe serial" % role)
    product = _require_string(probe["product"], "%s probe product" % role)
    for key, expected_value in (
        ("backend", "nrf-probes"),
        ("family", "nrf54l"),
        ("target", discovery.RECEIVER_TARGET),
        ("dpidr", discovery.RECEIVER_DPIDR),
        ("part", discovery.RECEIVER_PART),
    ):
        if probe[key] != expected_value:
            raise HilSessionError("%s probe %s mismatch" % (role, key))
    ap_idrs = probe["ap_idrs"]
    _require_exact_keys(
        ap_idrs, ("ap0", "ap1", "ap2", "ap3"), "%s probe ap_idrs" % role
    )
    for key in ("ap0", "ap1", "ap2", "ap3"):
        if not isinstance(ap_idrs[key], str) or not RAW_HEX_RE.fullmatch(ap_idrs[key]):
            raise HilSessionError("%s probe %s must be raw hexadecimal" % (role, key))
    variant = _require_string(probe["variant"], "%s probe variant" % role)
    variant_raw = _require_string(probe["variant_raw"], "%s probe variant_raw" % role)
    if _variant_display(variant_raw) != variant:
        raise HilSessionError("%s probe variant disagrees with variant_raw" % role)

    serial = value["serial"]
    _require_exact_keys(
        serial,
        ("path_observed", "usb_parent_observed", "stable_udev", "baud", "dtr", "rts"),
        "%s serial" % role,
    )
    path_observed = _require_string(
        serial["path_observed"], "%s serial path_observed" % role
    )
    if (
        not TTY_PATH_RE.fullmatch(path_observed)
        or os.path.normpath(path_observed) != path_observed
    ):
        raise HilSessionError(
            "%s serial path_observed must be canonical /dev/tty path" % role
        )
    usb_parent = _require_string(
        serial["usb_parent_observed"], "%s serial usb_parent_observed" % role
    )
    if not usb_parent.startswith("/") or os.path.normpath(usb_parent) != usb_parent:
        raise HilSessionError(
            "%s serial usb_parent_observed must be canonical absolute path" % role
        )
    stable_udev = serial["stable_udev"]
    _require_exact_keys(stable_udev, STABLE_UDEV_KEYS, "%s serial stable_udev" % role)
    for key in STABLE_UDEV_KEYS:
        _require_string(stable_udev[key], "%s serial stable_udev.%s" % (role, key))
    for key, expected_value in EXPECTED_XIAO_UDEV.items():
        if stable_udev[key] != expected_value:
            raise HilSessionError("%s serial stable_udev.%s mismatch" % (role, key))
    if stable_udev["ID_SERIAL_SHORT"] != serial_number:
        raise HilSessionError("%s serial probe correlation mismatch" % role)
    if (
        not isinstance(serial["baud"], int)
        or isinstance(serial["baud"], bool)
        or serial["baud"] != 115200
    ):
        raise HilSessionError("%s serial baud must be 115200" % role)
    if serial["dtr"] is not True or serial["rts"] is not False:
        raise HilSessionError("%s serial DTR/RTS contract mismatch" % role)
    return SessionRole(
        expected_firmware=MappingProxyType(dict(expected_firmware)),
        probe=discovery.ProbeIdentity(
            role=role,
            backend=probe["backend"],
            family=probe["family"],
            serial=serial_number,
            product=product,
            target=probe["target"],
            dpidr=probe["dpidr"],
            ap_idrs=MappingProxyType(dict(ap_idrs)),
            part=probe["part"],
            variant=variant,
            variant_raw=variant_raw,
        ),
        serial=discovery.SerialIdentity(
            role=role,
            path=path_observed,
            baud=serial["baud"],
            properties=MappingProxyType(dict(stable_udev)),
            usb_parent=usb_parent,
            dtr=serial["dtr"],
            rts=serial["rts"],
        ),
    )


def _parse_manifest(raw, path):
    obj = _parse_json_bytes(raw, path)
    _require_exact_keys(
        obj,
        (
            "schema_version",
            "session_id",
            "created_at_utc",
            "fixture",
            "binding",
            "roles",
        ),
        path,
    )
    if (
        not isinstance(obj["schema_version"], int)
        or isinstance(obj["schema_version"], bool)
        or obj["schema_version"] != SESSION_SCHEMA_VERSION
    ):
        raise HilSessionError("schema_version must be %d" % SESSION_SCHEMA_VERSION)
    session_id = _require_session_id(obj["session_id"])
    created_at_utc = _require_utc(obj["created_at_utc"])
    fixture = obj["fixture"]
    _require_exact_keys(fixture, ("fixture_id", "sha256"), "fixture")
    fixture_id = _require_string(fixture["fixture_id"], "fixture fixture_id")
    fixture_sha256 = _require_sha256(fixture["sha256"], "fixture sha256")
    binding = obj["binding"]
    _require_exact_keys(binding, ("sha256",), "binding")
    binding_sha256 = _require_sha256(binding["sha256"], "binding sha256")
    roles = obj["roles"]
    _require_exact_keys(roles, ("receiver", "source"), "roles")
    parsed_roles = {
        role: _parse_role(role, roles[role]) for role in ("receiver", "source")
    }
    receiver = parsed_roles["receiver"]
    source = parsed_roles["source"]
    if receiver.probe.serial == source.probe.serial:
        raise HilSessionError("receiver and source probe serials must be distinct")
    if receiver.serial.path == source.serial.path:
        raise HilSessionError("receiver and source observed tty paths must be distinct")
    if receiver.serial.usb_parent == source.serial.usb_parent:
        raise HilSessionError(
            "receiver and source observed USB parents must be distinct"
        )
    return {
        "session_id": session_id,
        "created_at_utc": created_at_utc,
        "fixture_id": fixture_id,
        "fixture_sha256": fixture_sha256,
        "binding_sha256": binding_sha256,
        "roles": MappingProxyType(parsed_roles),
    }


def _read_bytes(path, label):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError as exc:
        raise HilSessionError("cannot read %s %s: %s" % (label, path, exc)) from None


def _load_xiao_pair(fixture_path, binding_path):
    fixture_bytes = _read_bytes(fixture_path, "fixture")
    binding_bytes = _read_bytes(binding_path, "binding")
    try:
        fixture = model.parse_logical_fixture_bytes(fixture_bytes, fixture_path)
        binding = model.parse_physical_binding_bytes(
            binding_bytes, binding_path, fixture
        )
    except model.HilSchemaError as exc:
        raise HilSessionError(str(exc)) from None
    if fixture.fixture_id != XIAO_FIXTURE_ID:
        raise HilSessionError("session fixture must be %s" % XIAO_FIXTURE_ID)
    if fixture.capture_capability is not model.CaptureCapability.NONE:
        raise HilSessionError("session fixture must have no capture role")
    if set(fixture.roles) != {"receiver", "source"}:
        raise HilSessionError("session fixture role set must be receiver/source")
    expected_images = {"receiver": ("cpuapp", "flpr"), "source": ("cpuapp",)}
    for role in ("receiver", "source"):
        logical = fixture.roles[role]
        if (
            logical.board != model.NRF54L15_CPUAPP_BOARD
            or logical.images != expected_images[role]
        ):
            raise HilSessionError(
                "session %s logical board/image contract mismatch" % role
            )
        physical = binding.roles[role]
        if physical.probe.backend != "nrf-probes" or physical.probe.family != "nrf54l":
            raise HilSessionError("session %s probe contract mismatch" % role)
    return (
        fixture,
        binding,
        fixture_bytes,
        binding_bytes,
    )


def _assert_input_bytes_unchanged(
    fixture_path, binding_path, fixture_bytes, binding_bytes
):
    """Reject concurrent input edits before any session-root side effect."""
    current_fixture_bytes = _read_bytes(fixture_path, "fixture")
    current_binding_bytes = _read_bytes(binding_path, "binding")
    if current_fixture_bytes != fixture_bytes:
        raise HilSessionError("fixture bytes changed during session creation")
    if current_binding_bytes != binding_bytes:
        raise HilSessionError("binding bytes changed during session creation")


def _canonical_root(path, default_root):
    if (
        not isinstance(path, str)
        or not path
        or "\x00" in path
        or not os.path.isabs(path)
    ):
        raise HilSessionError("session root must be an absolute path")
    normalized = os.path.normpath(path)
    if normalized != path:
        raise HilSessionError("session root must be canonical: %s" % path)
    if default_root:
        parent = "/tmp/opencode"
        if os.path.islink(parent) or not os.path.isdir(parent):
            raise HilSessionError(
                "default session parent must be existing canonical /tmp/opencode"
            )
        if os.path.realpath(parent) != parent:
            raise HilSessionError(
                "default session parent must be canonical /tmp/opencode"
            )
        if os.path.lexists(path):
            if os.path.islink(path) or not os.path.isdir(path):
                raise HilSessionError("session root must be a non-symlink directory")
        else:
            try:
                os.mkdir(path, 0o700)
                os.chmod(path, 0o700)
            except OSError as exc:
                raise HilSessionError(
                    "cannot create default session root: %s" % exc
                ) from None
        return path
    if os.path.islink(path) or not os.path.isdir(path):
        raise HilSessionError(
            "custom session root must be existing non-symlink directory"
        )
    canonical = os.path.realpath(path)
    if canonical != path:
        raise HilSessionError("custom session root must be canonical: %s" % path)
    if canonical == "/":
        raise HilSessionError("custom session root must not be /")
    repo_root = os.path.realpath(lifecycle.default_repo_root())
    common_root = os.path.commonpath((canonical, repo_root))
    if common_root == canonical or common_root == repo_root:
        raise HilSessionError("custom session root must be external to repository")
    return canonical


def _create_session_dir(root, session_id):
    path = os.path.join(root, session_id)
    if os.path.lexists(path):
        raise HilSessionError("session path already exists: %s" % path)
    if os.path.realpath(path) != path:
        raise HilSessionError("session path must not traverse a symlink: %s" % path)
    try:
        os.mkdir(path, 0o700)
        os.chmod(path, 0o700)
    except OSError as exc:
        raise HilSessionError(
            "cannot create session directory %s: %s" % (path, exc)
        ) from None
    return path


def _fsync_directory(path):
    flags = os.O_RDONLY
    if hasattr(os, "O_DIRECTORY"):
        flags |= os.O_DIRECTORY
    try:
        fd = os.open(path, flags)
    except OSError as exc:
        raise HilSessionError(
            "cannot open session directory for fsync: %s" % exc
        ) from None
    try:
        os.fsync(fd)
    except OSError as exc:
        raise HilSessionError("cannot fsync session directory: %s" % exc) from None
    finally:
        os.close(fd)


def _serialize_manifest_payload(payload):
    return (
        json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
        + b"\n"
    )


def _create_manifest_file(session_dir, data):
    path = os.path.join(session_dir, SESSION_FILENAME)
    flags = os.O_CREAT | os.O_EXCL | os.O_WRONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(path, flags, 0o400)
    except OSError as exc:
        raise HilSessionError(
            "cannot create session manifest %s: %s" % (path, exc)
        ) from None
    try:
        offset = 0
        while offset < len(data):
            offset += os.write(fd, data[offset:])
        os.fsync(fd)
        os.fchmod(fd, 0o400)
        os.fsync(fd)
    except OSError as exc:
        raise HilSessionError(
            "cannot write session manifest %s: %s" % (path, exc)
        ) from None
    finally:
        os.close(fd)
    _fsync_directory(session_dir)
    return path


def _canonical_manifest_path(path, expected_session_id=None):
    if (
        not isinstance(path, str)
        or not path
        or "\x00" in path
        or not os.path.isabs(path)
    ):
        raise HilSessionError("session manifest must be an absolute path")
    normalized = os.path.normpath(path)
    if normalized != path:
        raise HilSessionError("session manifest must be canonical: %s" % path)
    if os.path.basename(path) != SESSION_FILENAME:
        raise HilSessionError("session manifest basename must be %s" % SESSION_FILENAME)
    session_id = os.path.basename(os.path.dirname(path))
    _require_session_id(session_id)
    if expected_session_id is not None and session_id != expected_session_id:
        raise HilSessionError("session manifest parent changed")
    if os.path.islink(path) or os.path.islink(os.path.dirname(path)):
        raise HilSessionError("session manifest must not be a symlink")
    canonical = os.path.realpath(path)
    if canonical != path:
        raise HilSessionError("session manifest must be canonical: %s" % path)
    return canonical, session_id


def _read_manifest_file(path, expected_session_id=None):
    canonical, session_id = _canonical_manifest_path(path, expected_session_id)
    try:
        before = os.lstat(canonical)
    except OSError as exc:
        raise HilSessionError(
            "cannot inspect session manifest %s: %s" % (path, exc)
        ) from None
    if stat.S_ISLNK(before.st_mode) or not stat.S_ISREG(before.st_mode):
        raise HilSessionError("session manifest must be a regular non-symlink file")
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(canonical, flags)
    except OSError as exc:
        raise HilSessionError(
            "cannot open session manifest %s: %s" % (path, exc)
        ) from None
    try:
        opened = os.fstat(fd)
        if not stat.S_ISREG(opened.st_mode):
            raise HilSessionError("session manifest must be a regular file")
        chunks = []
        while True:
            chunk = os.read(fd, 65536)
            if not chunk:
                break
            chunks.append(chunk)
    except OSError as exc:
        raise HilSessionError(
            "cannot read session manifest %s: %s" % (path, exc)
        ) from None
    finally:
        os.close(fd)
    if before.st_dev != opened.st_dev or before.st_ino != opened.st_ino:
        raise HilSessionError("session manifest replacement detected")
    mode = stat.S_IMODE(opened.st_mode)
    if mode != 0o400:
        raise HilSessionError("session manifest must have mode 0400")
    return (
        canonical,
        b"".join(chunks),
        {
            "device": opened.st_dev,
            "inode": opened.st_ino,
            "mode": mode,
            "session_id": session_id,
        },
    )


def _stable_udev(properties, role):
    stable = {}
    for key in STABLE_UDEV_KEYS:
        value = properties.get(key)
        if not isinstance(value, str) or not value:
            raise HilSessionError("%s tty missing stable udev %s" % (role, key))
        stable[key] = value
    return stable


def _role_payload(role, identity):
    probe = identity.probe
    serial = identity.serial
    if (
        probe.backend != "nrf-probes"
        or probe.family != "nrf54l"
        or probe.target != discovery.RECEIVER_TARGET
        or probe.dpidr != discovery.RECEIVER_DPIDR
        or probe.part != discovery.RECEIVER_PART
    ):
        raise HilSessionError("%s resolved probe contract mismatch" % role)
    if set(probe.ap_idrs) != {"ap0", "ap1", "ap2", "ap3"}:
        raise HilSessionError("%s resolved AP IDR map mismatch" % role)
    if not serial.usb_parent:
        raise HilSessionError("%s resolved USB parent missing" % role)
    return {
        "expected_firmware": _expected_firmware(role),
        "probe": {
            "backend": probe.backend,
            "family": probe.family,
            "serial": probe.serial,
            "product": probe.product,
            "target": probe.target,
            "dpidr": probe.dpidr,
            "ap_idrs": dict(probe.ap_idrs),
            "part": probe.part,
            "variant": probe.variant,
            "variant_raw": probe.variant_raw,
        },
        "serial": {
            "path_observed": serial.path,
            "usb_parent_observed": serial.usb_parent,
            "stable_udev": _stable_udev(serial.properties, role),
            "baud": serial.baud,
            "dtr": serial.dtr,
            "rts": serial.rts,
        },
    }


def _utc_string(utc_now):
    if utc_now is None:
        value = datetime.now(timezone.utc)
    elif callable(utc_now):
        value = utc_now()
    else:
        value = utc_now
    if isinstance(value, datetime):
        if value.tzinfo is None or value.utcoffset() != timedelta(0):
            raise HilSessionError("utc_now must be a UTC datetime")
        value = value.isoformat()
    if not isinstance(value, str):
        raise HilSessionError("utc_now must be RFC3339 UTC text or datetime")
    return _require_utc(value)


def create_session(
    fixture_path,
    binding_path,
    session_id,
    receiver_probe,
    source_probe,
    *,
    run_cmd,
    session_root=DEFAULT_SESSION_ROOT,
    sysfs_root="/sys",
    utc_now=None,
):
    """Create one exclusive immutable XIAO-pair session manifest."""
    fixture, binding, fixture_bytes, binding_bytes = _load_xiao_pair(
        fixture_path, binding_path
    )
    session_id = _require_session_id(session_id)
    receiver_probe = _require_probe_serial(receiver_probe, "receiver probe serial")
    source_probe = _require_probe_serial(source_probe, "source probe serial")
    if receiver_probe == source_probe:
        raise HilSessionError("receiver and source probe serials must be distinct")
    fixture_sha256 = _sha256(fixture_bytes)
    binding_sha256 = _sha256(binding_bytes)
    resolution = discovery.resolve_fixture(
        binding,
        run_cmd=run_cmd,
        sysfs_root=sysfs_root,
        explicit_probe_serials={"receiver": receiver_probe, "source": source_probe},
    )
    created_at_utc = _utc_string(utc_now)
    payload = {
        "schema_version": SESSION_SCHEMA_VERSION,
        "session_id": session_id,
        "created_at_utc": created_at_utc,
        "fixture": {"fixture_id": fixture.fixture_id, "sha256": fixture_sha256},
        "binding": {"sha256": binding_sha256},
        "roles": {
            role: _role_payload(role, resolution.roles[role])
            for role in ("receiver", "source")
        },
    }
    manifest_bytes = _serialize_manifest_payload(payload)
    _parse_manifest(manifest_bytes, "generated session manifest")
    _assert_input_bytes_unchanged(
        fixture_path, binding_path, fixture_bytes, binding_bytes
    )
    root = _canonical_root(session_root, session_root == DEFAULT_SESSION_ROOT)
    _assert_input_bytes_unchanged(
        fixture_path, binding_path, fixture_bytes, binding_bytes
    )
    session_dir = _create_session_dir(root, session_id)
    manifest_path = _create_manifest_file(session_dir, manifest_bytes)
    return load_session(manifest_path, fixture_path, binding_path)


def load_session(manifest_path, fixture_path, binding_path):
    """Load a strict immutable manifest and bind it to exact input bytes."""
    path, raw, state = _read_manifest_file(manifest_path)
    parsed = _parse_manifest(raw, path)
    if state["session_id"] != parsed["session_id"]:
        raise HilSessionError("session manifest parent/session ID mismatch")
    fixture, _binding, fixture_bytes, binding_bytes = _load_xiao_pair(
        fixture_path, binding_path
    )
    fixture_sha256 = _sha256(fixture_bytes)
    binding_sha256 = _sha256(binding_bytes)
    if parsed["fixture_id"] != fixture.fixture_id:
        raise HilSessionError("session manifest fixture ID mismatch")
    if parsed["fixture_sha256"] != fixture_sha256:
        raise HilSessionError("session manifest fixture bytes changed")
    if parsed["binding_sha256"] != binding_sha256:
        raise HilSessionError("session manifest binding bytes changed")
    return SessionManifest(
        path=path,
        raw_bytes=raw,
        sha256=_sha256(raw),
        device=state["device"],
        inode=state["inode"],
        mode=state["mode"],
        session_id=parsed["session_id"],
        created_at_utc=parsed["created_at_utc"],
        fixture_id=parsed["fixture_id"],
        fixture_sha256=parsed["fixture_sha256"],
        binding_sha256=parsed["binding_sha256"],
        fixture_bytes=fixture_bytes,
        binding_bytes=binding_bytes,
        roles=parsed["roles"],
    )


def _compare_role(manifest_role, current_role, role):
    expected_probe = manifest_role.probe
    current_probe = current_role.probe
    for field in (
        "backend",
        "family",
        "serial",
        "product",
        "target",
        "dpidr",
        "part",
        "variant",
        "variant_raw",
    ):
        if getattr(current_probe, field) != getattr(expected_probe, field):
            raise HilSessionError("%s probe %s drift" % (role, field))
    if dict(current_probe.ap_idrs) != dict(expected_probe.ap_idrs):
        raise HilSessionError("%s probe AP IDR drift" % role)
    expected_serial = manifest_role.serial
    current_serial = current_role.serial
    if current_serial.usb_parent != expected_serial.usb_parent:
        raise HilSessionError("%s USB parent drift" % role)
    if (
        current_serial.baud != expected_serial.baud
        or current_serial.dtr != expected_serial.dtr
        or current_serial.rts != expected_serial.rts
    ):
        raise HilSessionError("%s serial configuration drift" % role)
    if _stable_udev(current_serial.properties, role) != dict(
        expected_serial.properties
    ):
        raise HilSessionError("%s stable udev drift" % role)


def revalidate_session(
    manifest,
    fixture_path,
    binding_path,
    binding,
    *,
    run_cmd,
    sysfs_root="/sys",
):
    """Re-resolve session hardware and return fresh tty paths on no drift."""
    if not isinstance(manifest, SessionManifest):
        raise HilSessionError("manifest must be a SessionManifest")
    if not isinstance(binding, model.PhysicalBinding):
        raise HilSessionError("binding must be a validated PhysicalBinding")
    manifest.assert_unchanged()
    fixture, parsed_binding, fixture_bytes, binding_bytes = _load_xiao_pair(
        fixture_path, binding_path
    )
    if binding != parsed_binding:
        raise HilSessionError("binding does not match binding path")
    if (
        fixture.fixture_id != manifest.fixture_id
        or fixture_bytes != manifest.fixture_bytes
        or _sha256(fixture_bytes) != manifest.fixture_sha256
    ):
        raise HilSessionError("fixture bytes changed before session revalidation")
    if (
        binding_bytes != manifest.binding_bytes
        or _sha256(binding_bytes) != manifest.binding_sha256
    ):
        raise HilSessionError("binding bytes changed before session revalidation")
    resolution = discovery.resolve_fixture(
        binding,
        run_cmd=run_cmd,
        sysfs_root=sysfs_root,
        explicit_probe_serials={
            "receiver": manifest.roles["receiver"].probe.serial,
            "source": manifest.roles["source"].probe.serial,
        },
    )
    if set(resolution.roles) != {"receiver", "source"}:
        raise HilSessionError("revalidated role set mismatch")
    for role in ("receiver", "source"):
        _compare_role(manifest.roles[role], resolution.roles[role], role)
    return resolution
