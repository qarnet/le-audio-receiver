"""Probe, serial, and capture identity discovery for system HIL runner.

Resolves each physical role from stable identity, never from volatile
``/dev/tty*`` numbers: the receiver through ``nrf-probes`` (CMSIS-DAP
target fingerprint) plus its matching CDC tty, the source through its
onboard Segger J-Link located by the exact probe udev map, a read-only
J-Link target fingerprint, and the J-Link VCOM tty that shares the same
``ID_SERIAL_SHORT``.

All command execution goes through one injectable runner so the fake test
suites script every boundary; the sysfs root is injectable for the fake
tty/USB trees.  ``resolve_fixture`` never mutates the target: only
read-only ``nrf-probes``, ``udevadm info``, and OpenOCD J-Link
fingerprinting are issued. Capture identity additionally resolves one ALSA
sound-card sysfs node from its stable udev properties. Capture startup later
correlates that card index with ``arecord --list-devices`` before opening the
direct ``hw:`` endpoint.
"""

import os
import re
from dataclasses import dataclass
from types import MappingProxyType
from typing import Optional

from hil import model

#: nrf-probes plain table header, exact.
NRF_PROBES_HEADER_RE = re.compile(
    r"^\s*(SERIAL)\s{2,}(PROBE)\s{2,}(TARGET)\s{2,}(DPIDR)\s{2,}(PART)\s{2,}"
    r"(VARIANT)\s{2,}(NOTE)\s*$"
)
NRF_PROBES_HEADER = ("SERIAL", "PROBE", "TARGET", "DPIDR", "PART", "VARIANT", "NOTE")

#: Receiver target identity, exact (nrf-probes formats PART as 0x%08x).
RECEIVER_TARGET = "nRF54L15"
RECEIVER_PART = "0x00054b15"

#: nRF53 CTRL-AP IDR (AP2/AP3) and FICR INFO PART/VARIANT addresses.
NRF53_CTRL_AP_IDR = "0x12880000"
NRF53_PART_ADDR = "0x00FF020C"
NRF53_VARIANT_ADDR = "0x00FF0210"
SOURCE_PART = "0x00005340"

#: OpenOCD warning/error/recovery/verify-failure signature for boundary
#: scanning (fingerprint and flash output).  Informational OpenOCD output
#: is not firmware log input, but any line with one of these signatures
#: fails the boundary.
OPENOCD_FAILURE_RE = re.compile(
    r"(?i)\b(warning|error)\b|recovery|verify\s+(fail(?:ure)?|ed)"
)

J_LINK_FINGERPRINT_TCL = r"""
proc fwj_scan {} {
    set dpidr ""
    catch {set dpidr [format 0x%08x [nrf53.dap dpreg 0]]}
    puts "FWJ|dpidr|$dpidr"
    for {set i 0} {$i < 4} {incr i} {
        set idr ""
        catch {set idr [format 0x%08x [nrf53.dap apreg $i 0xfc]]}
        puts "FWJ|ap$i|$idr"
    }
    set part ""
    catch {set part [format 0x%08x [nrf53.cpuapp read_memory 0x00FF020C 32 1]]}
    set variant ""
    catch {set variant [format 0x%08x [nrf53.cpuapp read_memory 0x00FF0210 32 1]]}
    puts "FWJ|part|$part"
    puts "FWJ|variant|$variant"
}
"""


class HilDiscoveryError(Exception):
    """Raised for any unresolved, ambiguous, or drifted identity.

    ``raw`` is attached by ``resolve_fixture`` before propagation. It retains
    every identity command/result observed before the failed boundary so the
    runner can finalize useful evidence even when no full resolution exists.
    """

    def __init__(self, message):
        super().__init__(message)
        self.raw = MappingProxyType({})


@dataclass(frozen=True)
class ProbeIdentity:
    """One resolved probe: backend, family, current serial, and the
    read-only target fingerprint fields available for its family."""

    role: str
    backend: str
    family: str
    serial: str
    target: str  # human target name (nRF54L15 / nRF5340)
    dpidr: str
    part: str
    variant: str


@dataclass(frozen=True)
class SerialIdentity:
    """One resolved serial endpoint: canonical /dev/tty path, baud,
    pre-open modem-line states, matched udev properties, and USB parent."""

    role: str
    path: str
    baud: int
    properties: MappingProxyType  # str -> str
    usb_parent: Optional[str]
    dtr: bool = False
    rts: bool = False


@dataclass(frozen=True)
class RoleIdentity:
    """Resolved probe plus serial for one role."""

    role: str
    probe: ProbeIdentity
    serial: SerialIdentity


@dataclass(frozen=True)
class FixtureResolution:
    """Complete resolution for one fixture plus the raw identity evidence
    retained in the run directory."""

    roles: MappingProxyType  # role name -> RoleIdentity
    raw: MappingProxyType  # raw evidence records


@dataclass(frozen=True)
class CaptureResolution:
    """Resolved ALSA capture identity, separate from probe/serial roles."""

    device: str
    card_index: int
    card_id: str
    sound_path: str
    properties: MappingProxyType


def resolve_capture_binding(capture_binding, *, run_cmd, sysfs_root="/sys"):
    """Resolve one ALSA sound card from stable udev identity.

    The configured endpoint remains an exact direct ``hw:CARD,DEV`` value.
    This read-only step binds its card to one current ``/sys/class/sound/cardN``
    node. ``CaptureSession`` then requires that same numeric card and device in
    ``arecord --list-devices`` before it launches arecord.
    """
    if not isinstance(capture_binding, model.CaptureBinding):
        raise HilDiscoveryError("capture binding must be a validated CaptureBinding")
    configured = dict(capture_binding.udev.values)
    sound_dir = os.path.join(sysfs_root, "class", "sound")
    if not os.path.isdir(sound_dir):
        raise HilDiscoveryError("sound class directory not found: %s" % sound_dir)
    matches = []
    for name in sorted(os.listdir(sound_dir)):
        match = re.fullmatch(r"card([0-9]+)", name)
        if match is None:
            continue
        node = os.path.join(sound_dir, name)
        if not os.path.isdir(node):
            continue
        props = _udev_properties(run_cmd, node)
        if _match_udev(props, configured):
            matches.append((int(match.group(1)), node, props))
    if len(matches) != 1:
        raise HilDiscoveryError(
            "capture sound-card ambiguity: exactly one card must match capture udev map, found %d"
            % len(matches)
        )
    card_index, sound_path, properties = matches[0]
    configured_card = capture_binding.device[3:].split(",", 1)[0]
    card_id_path = os.path.join(sound_path, "id")
    try:
        with open(card_id_path, "r", encoding="utf-8") as fh:
            card_id = fh.read().strip()
    except OSError as exc:
        raise HilDiscoveryError(
            "cannot read resolved capture card ID %s: %s" % (card_id_path, exc)
        ) from None
    if not card_id:
        raise HilDiscoveryError("resolved capture card ID is empty: %s" % card_id_path)
    if configured_card.isdigit():
        if int(configured_card) != card_index:
            raise HilDiscoveryError(
                "capture direct endpoint card %s does not match resolved card%d"
                % (configured_card, card_index)
            )
    elif configured_card != card_id:
        raise HilDiscoveryError(
            "capture direct endpoint card %s does not match resolved card ID %s"
            % (configured_card, card_id)
        )
    return CaptureResolution(
        device=capture_binding.device,
        card_index=card_index,
        card_id=card_id,
        sound_path=sound_path,
        properties=MappingProxyType(dict(properties)),
    )


# ── nrf-probes table parsing ───────────────────────────────────────


def parse_nrf_probes_table(stdout):
    """Parse the plain ``nrf-probes`` table into a list of row dicts.

    The header must match the exact known columns; a row is rejected as
    truncated when it cannot reach the VARIANT column, and any header
    field outside the known set rejects the whole table.
    """
    lines = [ln for ln in stdout.splitlines() if ln.strip()]
    if not lines:
        raise HilDiscoveryError("nrf-probes produced no output")
    header_match = NRF_PROBES_HEADER_RE.match(lines[0])
    if not header_match:
        raise HilDiscoveryError("malformed nrf-probes header")
    header_words = header_match.groups()
    if tuple(header_words) != NRF_PROBES_HEADER:
        raise HilDiscoveryError("unknown nrf-probes table columns")
    # Column start positions from the header (rows are padded to width).
    starts = []
    pos = 0
    for word in header_words:
        idx = lines[0].index(word, pos)
        starts.append(idx)
        pos = idx + len(word)
    variant_start = starts[NRF_PROBES_HEADER.index("VARIANT")]
    rows = []
    for line in lines[1:]:
        if len(line) < variant_start:
            raise HilDiscoveryError("truncated nrf-probes row")
        cells = []
        for i, name in enumerate(header_words):
            start = starts[i]
            if i + 1 < len(header_words):
                cells.append(line[start : starts[i + 1]].strip())
            else:
                cells.append(line[start:].strip())
        rows.append(dict(zip(header_words, cells)))
    return rows


# ── udev property queries ──────────────────────────────────────────


def _udev_properties(run_cmd, node):
    """Query udev properties for one sysfs node; None when udevadm fails.

    Discovery enumerates sysfs, so use udevadm's ``--path`` form. ``--name``
    accepts a /dev node, not an absolute /sys path, and would make valid
    candidates fail to resolve on a real host.
    """
    proc = run_cmd(["udevadm", "info", "--query=property", "--path", node], 10)
    if proc.returncode != 0:
        return None
    props = {}
    for line in (proc.stdout or "").splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            if key:
                props[key] = value
    return props


def _match_udev(props, configured):
    """Exact equality for every configured udev key."""
    if props is None:
        return False
    for key, value in configured.items():
        if props.get(key) != value:
            return False
    return True


def _usb_parent(sysfs_root, props):
    """USB device sysfs path (deepest ancestor holding idVendor) of a
    node, or None when the node has no USB device ancestor."""
    devpath = props.get("DEVPATH")
    if not devpath:
        return None
    candidate = os.path.normpath(os.path.join(sysfs_root, devpath.lstrip(os.sep)))
    while True:
        if os.path.isfile(os.path.join(candidate, "idVendor")):
            return candidate
        parent = os.path.dirname(candidate)
        if parent == candidate:
            return None
        candidate = parent


# ── J-Link fingerprint ─────────────────────────────────────────────


def fingerprint_jlink(run_cmd, serial, timeout=60):
    """Run one read-only OpenOCD J-Link fingerprint with the explicit
    serial and return the parsed marker map plus raw output.

    Marker lines ``FWJ|key|value`` are retained for DPIDR, AP0..AP3 IDRs,
    FICR INFO.PART at 0x00FF020C, and VARIANT at 0x00FF0210.  Any OpenOCD
    warning/error/recovery/verify-failure line fails the fingerprint.
    """
    argv = [
        "openocd",
        "-f",
        "interface/jlink.cfg",
        "-c",
        "adapter serial %s" % serial,
        "-c",
        "transport select swd",
        "-c",
        "adapter speed 2000",
        "-c",
        "gdb port disabled",
        "-c",
        "tcl port disabled",
        "-c",
        "telnet port disabled",
        "-f",
        "target/nordic/nrf53.cfg",
        "-c",
        J_LINK_FINGERPRINT_TCL,
        "-c",
        "init",
        "-c",
        "fwj_scan",
        "-c",
        "shutdown",
    ]
    proc = run_cmd(argv, timeout)
    raw_out = (proc.stdout or "") + "\n" + (proc.stderr or "")
    markers = {}
    for line in (proc.stdout or "").splitlines():
        if line.startswith("FWJ|"):
            _, key, value = line.split("|", 2)
            markers[key] = value
    failures = [ln for ln in raw_out.splitlines() if OPENOCD_FAILURE_RE.search(ln)]
    if proc.returncode != 0:
        failures.append("OpenOCD exited with status %d" % proc.returncode)
    return argv, raw_out, markers, failures, proc.returncode


# ── resolution ─────────────────────────────────────────────────────


def _resolve_receiver(run_cmd, raw):
    """Receiver: nrf-probes --find nrf54l plus the matching plain-table
    row, requiring the exact nRF54L15 target identity."""
    find_proc = run_cmd(["nrf-probes", "--find", "nrf54l"], 60)
    raw["nrf-probes-find"] = {
        "argv": ["nrf-probes", "--find", "nrf54l"],
        "stdout": find_proc.stdout or "",
        "stderr": find_proc.stderr or "",
        "status": find_proc.returncode,
    }
    tokens = (find_proc.stdout or "").strip().split()
    if find_proc.returncode != 0 or len(tokens) != 1:
        raise HilDiscoveryError(
            "receiver probe unresolved: nrf-probes --find nrf54l must return "
            "exactly one serial"
        )
    serial = tokens[0]
    rows = parse_nrf_probes_table(raw["nrf-probes"]["stdout"] or "")
    matches = [row for row in rows if row.get("SERIAL") == serial]
    if len(matches) != 1:
        raise HilDiscoveryError("receiver probe serial missing from nrf-probes table")
    row = matches[0]
    target = row.get("TARGET", "")
    if target != RECEIVER_TARGET:
        raise HilDiscoveryError(
            "receiver target drift: expected %r, got %r" % (RECEIVER_TARGET, target)
        )
    if row.get("PART", "") != RECEIVER_PART:
        raise HilDiscoveryError(
            "receiver PART drift: expected %s, got %r"
            % (RECEIVER_PART, row.get("PART", ""))
        )
    for field, label in (("DPIDR", "DPIDR"), ("VARIANT", "VARIANT")):
        value = row.get(field, "")
        if not value or value == "-":
            raise HilDiscoveryError("receiver %s missing" % label)
    return (
        ProbeIdentity(
            role="receiver",
            backend="nrf-probes",
            family="nrf54l",
            serial=serial,
            target=target,
            dpidr=row["DPIDR"],
            part=row["PART"],
            variant=row["VARIANT"],
        ),
        row,
    )


def _resolve_source_probe(run_cmd, sysfs_root, binding, raw):
    """Source probe: exact probe udev map against current USB devices,
    then a read-only J-Link fingerprint with the current USB serial."""
    probe_binding = binding.roles["source"].probe
    probe_udev = probe_binding.udev
    if probe_udev is None:
        raise HilDiscoveryError("source probe udev map is missing")
    configured = dict(probe_udev.values)
    matches = []
    usb_dir = os.path.join(sysfs_root, "bus", "usb", "devices")
    if not os.path.isdir(usb_dir):
        raise HilDiscoveryError("usb device tree not found: %s" % usb_dir)
    for dev in sorted(os.listdir(usb_dir)):
        node = os.path.join(usb_dir, dev)
        if not os.path.isdir(node):
            continue
        props = _udev_properties(run_cmd, node)
        if props is None:
            raw.setdefault("source-usb-udev", {})[dev] = None
            continue
        raw.setdefault("source-usb-udev", {})[dev] = dict(props)
        if _match_udev(props, configured):
            matches.append((dev, node, props))
    if len(matches) != 1:
        raise HilDiscoveryError(
            "source J-Link ambiguity: exactly one USB device must match the "
            "probe udev map, found %d" % len(matches)
        )
    _dev, node, props = matches[0]
    serial = props.get("ID_SERIAL_SHORT", "")
    if not serial:
        raise HilDiscoveryError("source J-Link USB device has no ID_SERIAL_SHORT")
    argv, raw_out, markers, failures, status = fingerprint_jlink(run_cmd, serial)
    raw["source-jlink-fingerprint"] = {
        "argv": argv,
        "output": raw_out,
        "status": status,
        "markers": markers,
        "failure_lines": failures,
    }
    if failures:
        raise HilDiscoveryError("source J-Link fingerprint OpenOCD failure")
    ap_idrs = [markers.get("ap%d" % i, "") for i in range(4)]
    if NRF53_CTRL_AP_IDR not in ap_idrs:
        raise HilDiscoveryError("source target is not an nRF53 (CTRL-AP missing)")
    if markers.get("part", "") != SOURCE_PART:
        raise HilDiscoveryError(
            "source PART drift: expected %s, got %r"
            % (SOURCE_PART, markers.get("part", ""))
        )
    for field, label in (("dpidr", "DPIDR"), ("variant", "VARIANT")):
        value = markers.get(field, "")
        if not value or value == "0x00000000":
            raise HilDiscoveryError("source %s missing" % label)
    return (
        ProbeIdentity(
            role="source",
            backend="jlink",
            family="nrf53",
            serial=serial,
            target="nRF5340",
            dpidr=markers["dpidr"],
            part=markers["part"],
            variant=markers["variant"],
        ),
        node,
    )


def _resolve_serial(run_cmd, sysfs_root, role_name, binding, probe, raw):
    """One role's tty: candidates under /sys/class/tty matched by every
    configured serial udev key plus exact probe-serial correlation."""
    role = binding.roles[role_name]
    configured = dict(role.serial.udev.values)
    tty_dir = os.path.join(sysfs_root, "class", "tty")
    if not os.path.isdir(tty_dir):
        raise HilDiscoveryError("tty class directory not found: %s" % tty_dir)
    matches = []
    for name in sorted(os.listdir(tty_dir)):
        node = os.path.join(tty_dir, name)
        if not os.path.isdir(node):
            continue
        props = _udev_properties(run_cmd, node)
        if props is None:
            continue
        if not _match_udev(props, configured):
            continue
        serial = props.get("ID_SERIAL_SHORT", "")
        if not serial or serial != probe.serial:
            continue
        matches.append((name, props))
    if len(matches) != 1:
        raise HilDiscoveryError(
            "%s serial ambiguity: exactly one tty must match udev keys and "
            "probe serial %s, found %d" % (role_name, probe.serial, len(matches))
        )
    name, props = matches[0]
    usb_parent = _usb_parent(sysfs_root, props)
    raw.setdefault("%s-udev" % role_name, {})[name] = dict(props)
    return SerialIdentity(
        role=role_name,
        path=os.path.join("/dev", name),
        baud=role.serial.baud,
        properties=MappingProxyType(dict(props)),
        usb_parent=usb_parent,
        dtr=role.serial.dtr,
        rts=role.serial.rts,
    )


def _resolve_fixture_impl(binding, *, run_cmd, sysfs_root, raw):
    """Resolve every role in one physical binding.

    ``run_cmd(argv, timeout)`` returns a process-like object with
    ``stdout``, ``stderr``, and ``returncode`` (production: subprocess;
    fakes: scripted).  Returns a ``FixtureResolution`` with raw identity
    evidence retained for the run directory.
    """
    if not isinstance(binding, model.PhysicalBinding):
        raise HilDiscoveryError("binding must be a validated PhysicalBinding")
    plain_proc = run_cmd(["nrf-probes"], 90)
    raw["nrf-probes"] = {
        "argv": ["nrf-probes"],
        "stdout": plain_proc.stdout or "",
        "stderr": plain_proc.stderr or "",
        "status": plain_proc.returncode,
    }
    if plain_proc.returncode != 0:
        raise HilDiscoveryError(
            "nrf-probes failed with status %d" % plain_proc.returncode
        )

    receiver_probe, receiver_row = _resolve_receiver(run_cmd, raw)
    raw["receiver-probe"] = dict(receiver_row)
    source_probe, source_node = _resolve_source_probe(run_cmd, sysfs_root, binding, raw)
    raw["source-probe-node"] = source_node

    # Source probe and source serial must share the exact nonempty
    # ID_SERIAL_SHORT: both interfaces belong to one J-Link composite
    # device.  The serial resolution enforces this correlation directly
    # (the tty must carry the probe serial).
    receiver_serial = _resolve_serial(
        run_cmd, sysfs_root, "receiver", binding, receiver_probe, raw
    )
    source_serial = _resolve_serial(
        run_cmd, sysfs_root, "source", binding, source_probe, raw
    )

    # Cross-wiring rejection: distinct ttys from distinct USB parents.
    if receiver_serial.path == source_serial.path:
        raise HilDiscoveryError("receiver and source share the same tty")
    if (
        receiver_serial.usb_parent is not None
        and receiver_serial.usb_parent == source_serial.usb_parent
    ):
        raise HilDiscoveryError("receiver and source share the same USB parent")

    roles = {
        "receiver": RoleIdentity("receiver", receiver_probe, receiver_serial),
        "source": RoleIdentity("source", source_probe, source_serial),
    }
    return FixtureResolution(roles=MappingProxyType(roles), raw=MappingProxyType(raw))


def resolve_fixture(binding, *, run_cmd, sysfs_root="/sys"):
    """Resolve every role and retain partial raw evidence on failure.

    Discovery is intentionally fail-closed, but a failure after the runner
    created its evidence directory must not discard the probe/udev/OpenOCD
    observations that explain it. Attach an immutable snapshot to the raised
    ``HilDiscoveryError`` for that runner-side retention path.
    """
    raw = {}
    try:
        return _resolve_fixture_impl(
            binding, run_cmd=run_cmd, sysfs_root=sysfs_root, raw=raw
        )
    except HilDiscoveryError as exc:
        exc.raw = MappingProxyType(dict(raw))
        raise
    except Exception as exc:  # noqa: BLE001 - external discovery boundary
        wrapped = HilDiscoveryError("identity discovery failed: %s" % exc)
        wrapped.raw = MappingProxyType(dict(raw))
        raise wrapped from exc
