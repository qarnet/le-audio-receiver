"""Logical fixture and physical binding schemas for the system HIL runner.

Host-only schema layer (RH0).  Parsing is strict and fail-closed: non-UTF-8,
invalid JSON, non-object roots, unknown keys, missing keys, wrong scalar
types, duplicate semantic role names, empty strings, and booleans where
integers are expected are all rejected.  A required field is never silently
filled.
"""

import hashlib
import json
import os
import re
import stat
from dataclasses import dataclass
from enum import Enum
from types import MappingProxyType
from typing import Optional

SCHEMA_VERSION = 1

FIXTURE_ROOT_KEYS = frozenset(
    {"schema_version", "fixture_id", "capture_capability", "roles"}
)
ZEPHYR_ROLE_KEYS = frozenset({"kind", "board", "images"})
CAPTURE_ROLE_KEYS = frozenset({"kind", "channels"})
ZEPHYR_KIND = "zephyr_dut"
CAPTURE_KIND = "alsa_capture"

BINDING_ROOT_KEYS = frozenset({"schema_version", "fixture_id", "roles"})
BINDING_ROLE_KEYS = frozenset({"probe", "serial"})
CAPTURE_BINDING_ROLE_KEYS = frozenset(
    {
        "backend",
        "device",
        "channels",
        "sample_rate",
        "sample_format",
        "udev",
        "mixer",
        "fixture_metadata",
    }
)
PROBE_KEYS = frozenset({"backend", "family", "udev"})
SERIAL_KEYS = frozenset({"baud", "dtr", "rts", "udev"})
CAPTURE_MIXER_KEYS = frozenset(
    {"control", "volume", "capture_switch", "agc_control", "agc"}
)
CAPTURE_METADATA_KEYS = frozenset(
    {
        "fixture_id",
        "capture_hardware",
        "dac",
        "cable_network_schematic_id",
        "measured_attenuation",
        "measured_channel_mismatch",
        "electrical_review_id",
        "electrical_review_date",
        "usb_path",
        "kernel_version",
        "alsa_version",
        "notes",
    }
)
CAPTURE_BACKEND = "alsa"
CAPTURE_SAMPLE_RATE = 48000
CAPTURE_SAMPLE_FORMAT = "S16_LE"
CAPTURE_DEVICE_RE = re.compile(r"^hw:[A-Za-z0-9_.-]+,[0-9]+$")
#: Physical probe backend per role.  The receiver uses nrf-probes
#: (CMSIS-DAP); the source nRF5340DK uses its onboard Segger J-Link, which
#: nrf-probes never enumerates.  Corrected in RH2 from the RH0 assumption
#: that both were nrf-probes.
PROBE_BACKENDS = {"receiver": "nrf-probes", "source": "jlink"}
PROBE_FAMILIES = {"receiver": "nrf54l", "source": "nrf53"}
UDEV_REQUIRED = frozenset({"ID_VENDOR_ID", "ID_MODEL_ID"})
UDEV_OR = ("ID_SERIAL_SHORT", "ID_PATH")
UDEV_FORBIDDEN = frozenset({"DEVNAME"})


class HilSchemaError(ValueError):
    """Raised for any schema violation in a fixture or binding document."""


class _DuplicateKeyError(ValueError):
    """Internal: duplicate JSON object key (carries the offending key)."""

    def __init__(self, key):
        super().__init__(key)
        self.key = key


class CaptureCapability(str, Enum):
    """Capture capability enum with exactly the three allowed values."""

    NONE = "none"
    MONO = "mono"
    STEREO = "stereo"


@dataclass(frozen=True)
class LogicalRole:
    """One logical fixture role (receiver, source, or capture)."""

    kind: str
    board: Optional[str]  # None for capture roles
    images: tuple  # empty for capture roles
    channels: Optional[int]  # None for zephyr roles


@dataclass(frozen=True)
class LogicalFixture:
    """Checked-in logical fixture description."""

    schema_version: int
    fixture_id: str
    capture_capability: CaptureCapability
    roles: MappingProxyType  # immutable view: role name -> LogicalRole


@dataclass(frozen=True)
class UdevIdentity:
    """Stable udev identity map for one serial endpoint."""

    values: MappingProxyType  # str -> str

    def get(self, key, default=None):
        return self.values.get(key, default)

    def __iter__(self):
        return iter(self.values)

    def __contains__(self, key):
        return key in self.values


@dataclass(frozen=True)
class ProbeBinding:
    """Probe resolution contract (never a static serial mapping).

    ``udev`` is None when the probe needs no USB identity filtering
    (receiver CMSIS-DAP is resolved through nrf-probes alone); a jlink
    source probe always carries one so the onboard J-Link can be located
    and its current serial read from the matching USB device.
    """

    backend: str
    family: str
    udev: Optional[UdevIdentity]  # None when the probe has no udev map


@dataclass(frozen=True)
class SerialBinding:
    """Serial endpoint identity and pre-open modem-line state.

    ``dtr`` and ``rts`` are physical-fixture properties, not transient host
    defaults. The console applies both before opening the tty and never
    changes either line after open.
    """

    baud: int
    dtr: bool
    rts: bool
    udev: UdevIdentity


@dataclass(frozen=True)
class PhysicalRoleBinding:
    """Physical binding for one zephyr role."""

    probe: ProbeBinding
    serial: SerialBinding


@dataclass(frozen=True)
class CaptureMixerBinding:
    """Frozen ALSA mixer state for one capture fixture."""

    control: str
    volume: str
    capture_switch: str
    agc_control: str
    agc: str


@dataclass(frozen=True)
class CaptureFixtureMetadata:
    """Validated external electrical-fixture metadata and exact hash."""

    path: str
    sha256: str
    values: MappingProxyType


@dataclass(frozen=True)
class CaptureBinding:
    """Strict direct-ALSA capture binding for mono or stereo evidence."""

    backend: str
    device: str
    channels: int
    sample_rate: int
    sample_format: str
    udev: UdevIdentity
    mixer: CaptureMixerBinding
    fixture_metadata: CaptureFixtureMetadata


@dataclass(frozen=True)
class PhysicalBinding:
    """Local, gitignored physical binding for one logical fixture."""

    schema_version: int
    fixture_id: str
    roles: MappingProxyType  # role name -> PhysicalRoleBinding/CaptureBinding


def _read_text(path):
    with open(path, "rb") as fh:
        raw = fh.read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise HilSchemaError("%s is not valid UTF-8" % path) from None


def _sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _require_external_regular_file(path, label):
    """Return canonical ``path`` only for a regular non-symlink repo-external file."""
    if not isinstance(path, str) or not path or not os.path.isabs(path):
        raise HilSchemaError("%s must be an absolute path" % label)
    if os.path.islink(path):
        raise HilSchemaError("%s must not be a symlink: %s" % (label, path))
    canonical = os.path.realpath(path)
    if canonical != os.path.normpath(path):
        raise HilSchemaError("%s must be canonical: %s" % (label, path))
    repo_root = os.path.realpath(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    )
    if canonical == repo_root or canonical.startswith(repo_root + os.sep):
        raise HilSchemaError("%s must be outside repository: %s" % (label, path))
    try:
        mode = os.stat(canonical).st_mode
    except OSError as exc:
        raise HilSchemaError("cannot inspect %s %s: %s" % (label, path, exc)) from None
    if not stat.S_ISREG(mode):
        raise HilSchemaError("%s must be a regular file: %s" % (label, path))
    return canonical


def _reject_duplicate_keys(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise _DuplicateKeyError(key)
        out[key] = value
    return out


def _parse_object(text, path):
    try:
        obj = json.loads(text, object_pairs_hook=_reject_duplicate_keys)
    except _DuplicateKeyError as exc:
        raise HilSchemaError("duplicate key %r in %s" % (exc.key, path)) from None
    except json.JSONDecodeError as exc:
        raise HilSchemaError("invalid JSON in %s: %s" % (path, exc)) from None
    if not isinstance(obj, dict):
        raise HilSchemaError("%s must contain a JSON object" % path)
    return obj


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def _reject_unknown(obj, allowed, path):
    for key in obj:
        if key not in allowed:
            raise HilSchemaError("unknown key %r in %s" % (key, path))


def _require_member(obj, key, path):
    if key not in obj:
        raise HilSchemaError("missing key %r in %s" % (key, path))
    return obj[key]


def _require_nonempty_str(obj, key, path):
    value = _require_member(obj, key, path)
    if not isinstance(value, str) or not value:
        raise HilSchemaError("%s must be a nonempty string in %s" % (key, path))
    return value


def _parse_role(name, spec, capability, path):
    if not isinstance(spec, dict):
        raise HilSchemaError("role %r must be an object in %s" % (name, path))
    kind = spec.get("kind")
    if kind == ZEPHYR_KIND:
        if name == "capture":
            raise HilSchemaError(
                "capture role must use kind %r in %s" % (CAPTURE_KIND, path)
            )
        _reject_unknown(spec, ZEPHYR_ROLE_KEYS, path)
        board = _require_nonempty_str(spec, "board", path)
        images = spec.get("images")
        if not isinstance(images, list) or not images:
            raise HilSchemaError(
                "role %r images must be a nonempty list in %s" % (name, path)
            )
        seen = set()
        for image in images:
            if not isinstance(image, str) or not image:
                raise HilSchemaError(
                    "role %r image entries must be nonempty strings in %s"
                    % (name, path)
                )
            if image in seen:
                raise HilSchemaError(
                    "role %r images must be unique in %s" % (name, path)
                )
            seen.add(image)
        return LogicalRole(kind=kind, board=board, images=tuple(images), channels=None)
    if kind == CAPTURE_KIND:
        if name != "capture":
            raise HilSchemaError(
                "role %r must use kind %r in %s" % (name, ZEPHYR_KIND, path)
            )
        _reject_unknown(spec, CAPTURE_ROLE_KEYS, path)
        channels = spec.get("channels")
        if not isinstance(channels, int) or isinstance(channels, bool):
            raise HilSchemaError("capture channels must be an integer in %s" % path)
        if channels != 1 and channels != 2:
            raise HilSchemaError("capture channels must be 1 or 2 in %s" % path)
        if capability is CaptureCapability.MONO and channels != 1:
            raise HilSchemaError(
                "capture channels must be 1 for mono capability in %s" % path
            )
        if capability is CaptureCapability.STEREO and channels != 2:
            raise HilSchemaError(
                "capture channels must be 2 for stereo capability in %s" % path
            )
        return LogicalRole(kind=kind, board=None, images=(), channels=channels)
    raise HilSchemaError(
        "role %r kind must be %r or %r in %s" % (name, ZEPHYR_KIND, CAPTURE_KIND, path)
    )


def load_logical_fixture(path):
    """Load and strictly validate a logical fixture document."""
    obj = _parse_object(_read_text(path), path)
    _reject_unknown(obj, FIXTURE_ROOT_KEYS, path)
    schema_version = obj.get("schema_version")
    if not _is_int(schema_version) or schema_version != SCHEMA_VERSION:
        raise HilSchemaError("schema_version must be %d in %s" % (SCHEMA_VERSION, path))
    fixture_id = _require_nonempty_str(obj, "fixture_id", path)
    capability_raw = _require_member(obj, "capture_capability", path)
    if not isinstance(capability_raw, str) or capability_raw not in (
        "none",
        "mono",
        "stereo",
    ):
        raise HilSchemaError(
            "capture_capability must be none, mono, or stereo in %s" % path
        )
    capability = CaptureCapability(capability_raw)
    roles = _require_member(obj, "roles", path)
    if not isinstance(roles, dict) or not roles:
        raise HilSchemaError("roles must be a nonempty object in %s" % path)
    role_names = set(roles)
    if "receiver" not in role_names or "source" not in role_names:
        raise HilSchemaError("roles must contain receiver and source in %s" % path)
    has_capture = "capture" in role_names
    if capability is CaptureCapability.NONE and has_capture:
        raise HilSchemaError(
            "capture role is forbidden when capture_capability is none in %s" % path
        )
    if capability is not CaptureCapability.NONE and not has_capture:
        raise HilSchemaError(
            "capture role is required when capture_capability is %s in %s"
            % (capability.value, path)
        )
    extra_roles = role_names - {"receiver", "source", "capture"}
    if extra_roles:
        raise HilSchemaError("extra role %r in %s" % (sorted(extra_roles)[0], path))
    parsed = {}
    for name in roles:
        parsed[name] = _parse_role(name, roles[name], capability, path)
    return LogicalFixture(
        schema_version=SCHEMA_VERSION,
        fixture_id=fixture_id,
        capture_capability=capability,
        roles=MappingProxyType(parsed),
    )


def _parse_udev_map(udev, path, reject_tty_paths):
    """Parse and validate one strict udev identity map (probe or serial).

    Shared key/value rules: only ``ID_*`` keys, nonempty string values,
    no ``DEVNAME``.  ``reject_tty_paths`` additionally rejects values that
    start with ``/dev/tty`` (serial endpoints only; a probe USB device
    never carries a tty path, so the check is irrelevant there).
    """
    parsed_udev = {}
    for key, value in udev.items():
        if key in UDEV_FORBIDDEN:
            raise HilSchemaError("udev key %r forbidden in %s" % (key, path))
        if not key.startswith("ID_"):
            raise HilSchemaError("unknown udev key %r in %s" % (key, path))
        if not isinstance(value, str) or not value:
            raise HilSchemaError(
                "udev value for %r must be a nonempty string in %s" % (key, path)
            )
        if value.startswith("/dev/snd/"):
            raise HilSchemaError(
                "static /dev/snd path not allowed as udev identity in %s" % path
            )
        if reject_tty_paths and value.startswith("/dev/tty"):
            raise HilSchemaError(
                "volatile tty path not allowed as udev identity in %s" % path
            )
        parsed_udev[key] = value
    for required in UDEV_REQUIRED:
        if required not in parsed_udev:
            raise HilSchemaError("udev missing %r in %s" % (required, path))
    if not any(key in parsed_udev for key in UDEV_OR):
        raise HilSchemaError(
            "udev must contain ID_SERIAL_SHORT or ID_PATH in %s" % path
        )
    return UdevIdentity(MappingProxyType(parsed_udev))


def _parse_fixture_metadata(path, fixture_id):
    canonical = _require_external_regular_file(path, "fixture_metadata")
    obj = _parse_object(_read_text(canonical), canonical)
    _reject_unknown(obj, CAPTURE_METADATA_KEYS, canonical)
    if _require_nonempty_str(obj, "fixture_id", canonical) != fixture_id:
        raise HilSchemaError("fixture metadata fixture_id mismatch in %s" % canonical)
    for key in (
        "capture_hardware",
        "dac",
        "cable_network_schematic_id",
        "measured_attenuation",
        "measured_channel_mismatch",
        "electrical_review_id",
        "electrical_review_date",
        "usb_path",
        "kernel_version",
        "alsa_version",
        "notes",
    ):
        _require_nonempty_str(obj, key, canonical)
    return CaptureFixtureMetadata(
        path=canonical,
        sha256=_sha256_file(canonical),
        values=MappingProxyType(dict(obj)),
    )


def verify_capture_fixture_metadata(capture_binding):
    """Re-read external metadata so a post-parse fixture edit cannot hide."""
    if not isinstance(capture_binding, CaptureBinding):
        raise HilSchemaError("validated capture binding is required")
    metadata = _parse_fixture_metadata(
        capture_binding.fixture_metadata.path,
        capture_binding.fixture_metadata.values["fixture_id"],
    )
    if metadata.sha256 != capture_binding.fixture_metadata.sha256:
        raise HilSchemaError(
            "capture fixture metadata changed after binding validation"
        )
    return metadata


def _parse_capture_binding_role(spec, path, fixture_id, capability):
    if not isinstance(spec, dict):
        raise HilSchemaError("capture binding must be an object in %s" % path)
    _reject_unknown(spec, CAPTURE_BINDING_ROLE_KEYS, path)
    backend = _require_nonempty_str(spec, "backend", path)
    if backend != CAPTURE_BACKEND:
        raise HilSchemaError(
            "capture backend must be %r in %s" % (CAPTURE_BACKEND, path)
        )
    device = _require_nonempty_str(spec, "device", path)
    if not CAPTURE_DEVICE_RE.fullmatch(device):
        raise HilSchemaError(
            "capture device must be exact direct hw:CARD,DEV in %s" % path
        )
    channels = _require_member(spec, "channels", path)
    expected_channels = 1 if capability is CaptureCapability.MONO else 2
    if not _is_int(channels) or channels != expected_channels:
        raise HilSchemaError(
            "capture channels must be %d for %s capability in %s"
            % (expected_channels, capability.value, path)
        )
    sample_rate = _require_member(spec, "sample_rate", path)
    if not _is_int(sample_rate) or sample_rate != CAPTURE_SAMPLE_RATE:
        raise HilSchemaError(
            "capture sample_rate must be %d in %s" % (CAPTURE_SAMPLE_RATE, path)
        )
    sample_format = _require_nonempty_str(spec, "sample_format", path)
    if sample_format != CAPTURE_SAMPLE_FORMAT:
        raise HilSchemaError(
            "capture sample_format must be %r in %s" % (CAPTURE_SAMPLE_FORMAT, path)
        )
    udev = _require_member(spec, "udev", path)
    if not isinstance(udev, dict) or not udev:
        raise HilSchemaError("capture udev must be a nonempty object in %s" % path)
    capture_udev = _parse_udev_map(udev, path, reject_tty_paths=False)
    mixer = _require_member(spec, "mixer", path)
    if not isinstance(mixer, dict):
        raise HilSchemaError("capture mixer must be an object in %s" % path)
    _reject_unknown(mixer, CAPTURE_MIXER_KEYS, path)
    values = {}
    for key in ("control", "volume", "capture_switch", "agc_control", "agc"):
        values[key] = _require_nonempty_str(mixer, key, path)
    if values["capture_switch"] != "on":
        raise HilSchemaError("capture mixer capture_switch must be 'on' in %s" % path)
    if values["agc"] != "off":
        raise HilSchemaError("capture mixer agc must be 'off' in %s" % path)
    metadata_path = _require_nonempty_str(spec, "fixture_metadata", path)
    return CaptureBinding(
        backend=backend,
        device=device,
        channels=channels,
        sample_rate=sample_rate,
        sample_format=sample_format,
        udev=capture_udev,
        mixer=CaptureMixerBinding(**values),
        fixture_metadata=_parse_fixture_metadata(metadata_path, fixture_id),
    )


def _parse_binding_role(name, spec, path):
    if not isinstance(spec, dict):
        raise HilSchemaError("binding role %r must be an object in %s" % (name, path))
    _reject_unknown(spec, BINDING_ROLE_KEYS, path)
    probe = spec.get("probe")
    serial = spec.get("serial")
    if not isinstance(probe, dict) or not isinstance(serial, dict):
        raise HilSchemaError(
            "binding role %r needs probe and serial objects in %s" % (name, path)
        )
    if "serial" in probe:
        raise HilSchemaError("probe serial field is not allowed in %s" % path)
    _reject_unknown(probe, PROBE_KEYS, path)
    backend = probe.get("backend")
    expected_backend = PROBE_BACKENDS.get(name)
    if (
        expected_backend is None
        or not isinstance(backend, str)
        or backend != expected_backend
    ):
        raise HilSchemaError("probe backend mismatch for role %r in %s" % (name, path))
    family = probe.get("family")
    expected_family = PROBE_FAMILIES.get(name)
    if (
        expected_family is None
        or not isinstance(family, str)
        or family != expected_family
    ):
        raise HilSchemaError("probe family mismatch for role %r in %s" % (name, path))
    probe_udev = None
    if "udev" in probe:
        raw_udev = probe.get("udev")
        if not isinstance(raw_udev, dict) or not raw_udev:
            raise HilSchemaError("probe udev must be a nonempty object in %s" % path)
        probe_udev = _parse_udev_map(raw_udev, path, reject_tty_paths=False)
    elif expected_backend == "jlink":
        # A jlink source probe is located by its exact USB identity map;
        # without it the onboard J-Link cannot be resolved safely.
        raise HilSchemaError("probe udev is required for source in %s" % path)
    _reject_unknown(serial, SERIAL_KEYS, path)
    baud = serial.get("baud")
    if not isinstance(baud, int) or isinstance(baud, bool) or baud <= 0:
        raise HilSchemaError("serial baud must be a positive integer in %s" % path)
    dtr = serial.get("dtr")
    if not isinstance(dtr, bool):
        raise HilSchemaError("serial dtr must be a boolean in %s" % path)
    rts = serial.get("rts")
    if not isinstance(rts, bool):
        raise HilSchemaError("serial rts must be a boolean in %s" % path)
    udev = serial.get("udev")
    if not isinstance(udev, dict):
        raise HilSchemaError("serial udev must be an object in %s" % path)
    serial_udev = UdevIdentity(MappingProxyType({}))
    if udev:
        serial_udev = _parse_udev_map(udev, path, reject_tty_paths=True)
    return PhysicalRoleBinding(
        probe=ProbeBinding(backend=backend, family=family, udev=probe_udev),
        serial=SerialBinding(baud=baud, dtr=dtr, rts=rts, udev=serial_udev),
    )


def load_physical_binding(path, logical_fixture):
    """Load and cross-validate a physical binding against a logical fixture."""
    obj = _parse_object(_read_text(path), path)
    _reject_unknown(obj, BINDING_ROOT_KEYS, path)
    schema_version = obj.get("schema_version")
    if not _is_int(schema_version) or schema_version != SCHEMA_VERSION:
        raise HilSchemaError("schema_version must be %d in %s" % (SCHEMA_VERSION, path))
    fixture_id = obj.get("fixture_id")
    if not isinstance(fixture_id, str) or not fixture_id:
        raise HilSchemaError(
            "binding fixture_id must be a nonempty string in %s" % path
        )
    if fixture_id != logical_fixture.fixture_id:
        raise HilSchemaError(
            "binding fixture_id %r does not match logical fixture_id %r"
            % (fixture_id, logical_fixture.fixture_id)
        )
    roles = obj.get("roles")
    if not isinstance(roles, dict) or not roles:
        raise HilSchemaError("binding roles must be a nonempty object in %s" % path)
    if set(roles) != set(logical_fixture.roles):
        raise HilSchemaError("binding role set mismatch in %s" % path)
    parsed = {}
    for name in sorted(roles):
        if name == "capture":
            parsed[name] = _parse_capture_binding_role(
                roles[name], path, fixture_id, logical_fixture.capture_capability
            )
        else:
            parsed[name] = _parse_binding_role(name, roles[name], path)
    return PhysicalBinding(
        schema_version=SCHEMA_VERSION,
        fixture_id=fixture_id,
        roles=MappingProxyType(parsed),
    )
