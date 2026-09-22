"""Probe, serial, and capture identity discovery for system HIL runner.

Resolves each physical role from stable identity, never from volatile
``/dev/tty*`` numbers: the receiver through ``nix-nrf probes`` (CMSIS-DAP
target fingerprint) plus its matching CDC tty, the source through its
onboard Segger J-Link located by the exact probe udev map, a read-only
J-Link target fingerprint, and the J-Link VCOM tty that shares the same
``ID_SERIAL_SHORT``.

All command execution goes through one injectable runner so the fake test
suites script every boundary; the sysfs root is injectable for the fake
tty/USB trees.  ``resolve_fixture`` never mutates the target: only
read-only ``nix-nrf probes``, ``udevadm info``, and OpenOCD J-Link
fingerprinting are issued. Capture identity additionally resolves one ALSA
sound-card sysfs node from its stable udev properties. Capture startup later
correlates that card index with ``arecord --list-devices`` before opening the
direct ``hw:`` endpoint.
"""

import os
import re
from collections.abc import Mapping
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

#: CMSIS-DAP nRF54L15 target identity, exact (the table formats PART as
#: ``0x%08x``).
RECEIVER_TARGET = "nRF54L15"
RECEIVER_PART = "0x00054b15"
RECEIVER_DPIDR = "0x6ba02477"
NIX_NRF_PROBES = ("nix-nrf", "probes")
PROBE_SERIAL_RE = re.compile(r"^[A-Za-z0-9_.:-]+$")
RAW_HEX_RE = re.compile(r"^0x[0-9a-f]{8}$")

#: nRF53 CTRL-AP IDR (AP2/AP3) and FICR INFO PART/VARIANT addresses.
NRF53_CTRL_AP_IDR = "0x12880000"
NRF53_PART_ADDR = "0x00FF020C"
NRF53_VARIANT_ADDR = "0x00FF0210"
SOURCE_PART = "0x00005340"

#: nRF54L15 FICR INFO fields, read after the explicit AP CSW setup below.
NRF54L15_PART_ADDR = "0x00FFC31C"
NRF54L15_VARIANT_ADDR = "0x00FFC320"

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

CMSIS_DAP_FINGERPRINT_TCL = r"""
proc fwc_scan {} {
    set dpidr ""
    catch {set dpidr [format 0x%08x [chip.dap dpreg 0]]}
    puts "FWC|dpidr|$dpidr"
    for {set i 0} {$i < 4} {incr i} {
        set idr ""
        catch {set idr [format 0x%08x [chip.dap apreg $i 0xfc]]}
        puts "FWC|ap$i|$idr"
    }
    chip.dap apcsw 0x01000000 0x01000000
    set part ""
    catch {set part [format 0x%08x [chip.cpu read_memory __NRF54L15_PART_ADDR__ 32 1]]}
    set variant ""
    catch {set variant [format 0x%08x [chip.cpu read_memory __NRF54L15_VARIANT_ADDR__ 32 1]]}
    puts "FWC|part|$part"
    puts "FWC|variant|$variant"
}
""".replace("__NRF54L15_PART_ADDR__", NRF54L15_PART_ADDR).replace(
    "__NRF54L15_VARIANT_ADDR__", NRF54L15_VARIANT_ADDR
)


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
    product: str
    target: str  # human target name (nRF54L15 / nRF5340)
    dpidr: str
    ap_idrs: MappingProxyType  # exact immutable ap0 through ap3 raw IDRs
    part: str
    variant: str
    variant_raw: str


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


# ── OpenOCD fingerprints ───────────────────────────────────────────


def _openocd_failures(proc):
    raw_out = (proc.stdout or "") + "\n" + (proc.stderr or "")
    failures = [ln for ln in raw_out.splitlines() if OPENOCD_FAILURE_RE.search(ln)]
    if proc.returncode != 0:
        failures.append("OpenOCD exited with status %d" % proc.returncode)
    return raw_out, failures


def _parse_cmsis_dap_markers(stdout):
    """Parse exact FWC markers and reject incomplete or malformed output."""
    expected = ("dpidr", "ap0", "ap1", "ap2", "ap3", "part", "variant")
    markers = {}
    failures = []
    for line in (stdout or "").splitlines():
        if not line.startswith("FWC"):
            continue
        fields = line.split("|")
        if len(fields) != 3 or fields[0] != "FWC" or fields[1] not in expected:
            failures.append("malformed CMSIS-DAP marker: %s" % line)
            continue
        key, value = fields[1:]
        if key in markers or not RAW_HEX_RE.fullmatch(value):
            failures.append("malformed CMSIS-DAP marker: %s" % line)
            continue
        markers[key] = value
    for key in expected:
        if key not in markers:
            failures.append("CMSIS-DAP marker missing: %s" % key)
    return markers, failures


def fingerprint_cmsis_dap(run_cmd, serial, timeout=60):
    """Run one explicit, read-only CMSIS-DAP nRF54L15 fingerprint.

    The scanner creates a generic SWD DAP and Cortex-M target only to read
    DPIDR, AP0 through AP3, and FICR INFO.PART/INFO.VARIANT. It never resets,
    halts, writes memory, recovers, or changes modem control lines.
    """
    argv = [
        "openocd",
        "-f",
        "interface/cmsis-dap.cfg",
        "-c",
        "adapter serial %s" % serial,
        "-c",
        "transport select swd",
        "-c",
        "adapter speed 1000",
        "-c",
        "gdb port disabled",
        "-c",
        "tcl port disabled",
        "-c",
        "telnet port disabled",
        "-c",
        "swd newdap chip cpu",
        "-c",
        "dap create chip.dap -chain-position chip.cpu",
        "-c",
        "target create chip.cpu cortex_m -dap chip.dap",
        "-c",
        CMSIS_DAP_FINGERPRINT_TCL,
        "-c",
        "init",
        "-c",
        "fwc_scan",
        "-c",
        "shutdown",
    ]
    proc = run_cmd(argv, timeout)
    raw_out, failures = _openocd_failures(proc)
    markers, marker_failures = _parse_cmsis_dap_markers(proc.stdout or "")
    failures.extend(marker_failures)
    return argv, raw_out, markers, failures, proc.returncode


def fingerprint_jlink(run_cmd, serial, timeout=60):
    """Run one read-only OpenOCD J-Link fingerprint with explicit serial."""
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
    raw_out, failures = _openocd_failures(proc)
    markers = {}
    for line in (proc.stdout or "").splitlines():
        if line.startswith("FWJ|"):
            fields = line.split("|")
            if len(fields) != 3:
                failures.append("malformed J-Link marker: %s" % line)
                continue
            _, key, value = fields
            if key in markers:
                failures.append("duplicate J-Link marker: %s" % key)
                continue
            markers[key] = value
    return argv, raw_out, markers, failures, proc.returncode


def _ap_idr_map(markers, label):
    values = {}
    for index in range(4):
        key = "ap%d" % index
        value = markers.get(key, "")
        if not RAW_HEX_RE.fullmatch(value):
            raise HilDiscoveryError("%s %s missing or malformed" % (label, key))
        values[key] = value
    return MappingProxyType(values)


def _variant_display(raw_variant):
    if not RAW_HEX_RE.fullmatch(raw_variant):
        raise HilDiscoveryError("CMSIS-DAP VARIANT marker malformed")
    try:
        value = int(raw_variant[2:], 16).to_bytes(4, byteorder="big").decode("ascii")
    except UnicodeDecodeError:
        raise HilDiscoveryError("CMSIS-DAP VARIANT marker is not ASCII") from None
    if not value or not value.isprintable():
        raise HilDiscoveryError("CMSIS-DAP VARIANT marker is not printable")
    return value


# ── resolution ─────────────────────────────────────────────────────


def _record_probe_output(raw, key, argv, proc):
    raw[key] = {
        "argv": list(argv),
        "stdout": proc.stdout or "",
        "stderr": proc.stderr or "",
        "status": proc.returncode,
    }


def _require_cmsis_table_row(row, role, serial):
    if row.get("SERIAL") != serial:
        raise HilDiscoveryError("%s probe serial mismatch in probe table" % role)
    product = row.get("PROBE", "")
    if not product or product == "-":
        raise HilDiscoveryError("%s probe product missing" % role)
    if row.get("TARGET", "") != RECEIVER_TARGET:
        raise HilDiscoveryError(
            "%s target drift: expected %r, got %r"
            % (role, RECEIVER_TARGET, row.get("TARGET", ""))
        )
    if row.get("DPIDR", "") != RECEIVER_DPIDR:
        raise HilDiscoveryError(
            "%s DPIDR drift: expected %s, got %r"
            % (role, RECEIVER_DPIDR, row.get("DPIDR", ""))
        )
    if row.get("PART", "") != RECEIVER_PART:
        raise HilDiscoveryError(
            "%s PART drift: expected %s, got %r"
            % (role, RECEIVER_PART, row.get("PART", ""))
        )
    variant = row.get("VARIANT", "")
    if not variant or variant == "-":
        raise HilDiscoveryError("%s VARIANT missing" % role)
    return product, variant


def _resolve_cmsis_probe(run_cmd, role, serial, row, raw):
    """Cross-check table identity with one explicit read-only fingerprint."""
    product, table_variant = _require_cmsis_table_row(row, role, serial)
    argv, raw_out, markers, failures, status = fingerprint_cmsis_dap(run_cmd, serial)
    raw["%s-cmsis-dap-fingerprint" % role] = {
        "argv": argv,
        "output": raw_out,
        "status": status,
        "markers": markers,
        "failure_lines": failures,
    }
    if failures:
        raise HilDiscoveryError("%s CMSIS-DAP fingerprint OpenOCD failure" % role)
    for key, table_key in (("dpidr", "DPIDR"), ("part", "PART")):
        if markers[key] != row[table_key]:
            raise HilDiscoveryError(
                "%s CMSIS-DAP %s disagrees with probe table" % (role, table_key)
            )
    variant = _variant_display(markers["variant"])
    if variant != table_variant:
        raise HilDiscoveryError(
            "%s CMSIS-DAP VARIANT disagrees with probe table" % role
        )
    return ProbeIdentity(
        role=role,
        backend="nrf-probes",
        family="nrf54l",
        serial=serial,
        product=product,
        target=row["TARGET"],
        dpidr=markers["dpidr"],
        ap_idrs=_ap_idr_map(markers, "%s CMSIS-DAP" % role),
        part=markers["part"],
        variant=variant,
        variant_raw=markers["variant"],
    )


def _resolve_receiver(run_cmd, binding, table_rows, raw):
    """Resolve one CMSIS-DAP receiver with targeted or family discovery."""
    configured_serial = binding.roles["receiver"].serial.udev.get("ID_SERIAL_SHORT")
    if configured_serial is not None:
        if not PROBE_SERIAL_RE.fullmatch(configured_serial):
            raise HilDiscoveryError("receiver configured probe serial is unsafe")
        argv = [*NIX_NRF_PROBES, configured_serial]
        proc = run_cmd(argv, 60)
        _record_probe_output(raw, "nrf-probes-targeted", argv, proc)
        if proc.returncode != 0:
            raise HilDiscoveryError(
                "receiver targeted probe lookup failed with status %d" % proc.returncode
            )
        rows = parse_nrf_probes_table(proc.stdout or "")
        matches = [row for row in rows if row.get("SERIAL") == configured_serial]
        if len(matches) != 1:
            raise HilDiscoveryError(
                "receiver probe serial missing from targeted probe table"
            )
        serial = configured_serial
        row = matches[0]
    else:
        argv = [*NIX_NRF_PROBES, "--find", "nrf54l"]
        proc = run_cmd(argv, 60)
        _record_probe_output(raw, "nrf-probes-find", argv, proc)
        tokens = (proc.stdout or "").strip().split()
        if proc.returncode != 0 or len(tokens) != 1:
            raise HilDiscoveryError(
                "receiver probe unresolved: nix-nrf probes --find nrf54l must return "
                "exactly one serial"
            )
        serial = tokens[0]
        if not PROBE_SERIAL_RE.fullmatch(serial):
            raise HilDiscoveryError("receiver discovered probe serial is unsafe")
        matches = [row for row in table_rows if row.get("SERIAL") == serial]
        if len(matches) != 1:
            raise HilDiscoveryError("receiver probe serial missing from probe table")
        row = matches[0]
    return _resolve_cmsis_probe(run_cmd, "receiver", serial, row, raw), row


def _resolve_source_probe(run_cmd, sysfs_root, binding, raw):
    """Source probe: exact J-Link USB identity plus read-only fingerprint."""
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
        raw.setdefault("source-usb-udev", {})[dev] = (
            None if props is None else dict(props)
        )
        if _match_udev(props, configured):
            matches.append((dev, node, props))
    if len(matches) != 1:
        raise HilDiscoveryError(
            "source J-Link ambiguity: exactly one USB device must match the "
            "probe udev map, found %d" % len(matches)
        )
    _dev, node, props = matches[0]
    serial = props.get("ID_SERIAL_SHORT", "")
    if not PROBE_SERIAL_RE.fullmatch(serial):
        raise HilDiscoveryError("source J-Link USB device has unsafe ID_SERIAL_SHORT")
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
    ap_idrs = _ap_idr_map(markers, "source J-Link")
    if NRF53_CTRL_AP_IDR not in ap_idrs.values():
        raise HilDiscoveryError("source target is not an nRF53 (CTRL-AP missing)")
    if markers.get("part", "") != SOURCE_PART:
        raise HilDiscoveryError(
            "source PART drift: expected %s, got %r"
            % (SOURCE_PART, markers.get("part", ""))
        )
    for field, label in (("dpidr", "DPIDR"), ("variant", "VARIANT")):
        value = markers.get(field, "")
        if not RAW_HEX_RE.fullmatch(value) or value == "0x00000000":
            raise HilDiscoveryError("source %s missing" % label)
    return (
        ProbeIdentity(
            role="source",
            backend="jlink",
            family="nrf53",
            serial=serial,
            product="J-Link",
            target="nRF5340",
            dpidr=markers["dpidr"],
            ap_idrs=ap_idrs,
            part=markers["part"],
            variant=markers["variant"],
            variant_raw=markers["variant"],
        ),
        node,
    )


def _resolve_serial(run_cmd, sysfs_root, role_name, binding, probe, raw):
    """Resolve one tty by configured udev keys and exact probe correlation."""
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
        raw.setdefault("%s-udev" % role_name, {})[name] = (
            None if props is None else dict(props)
        )
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
    return SerialIdentity(
        role=role_name,
        path=os.path.join("/dev", name),
        baud=role.serial.baud,
        properties=MappingProxyType(dict(props)),
        usb_parent=_usb_parent(sysfs_root, props),
        dtr=role.serial.dtr,
        rts=role.serial.rts,
    )


def _resolved_roles(binding, receiver_probe, source_probe, run_cmd, sysfs_root, raw):
    receiver_serial = _resolve_serial(
        run_cmd, sysfs_root, "receiver", binding, receiver_probe, raw
    )
    source_serial = _resolve_serial(
        run_cmd, sysfs_root, "source", binding, source_probe, raw
    )
    if receiver_serial.path == source_serial.path:
        raise HilDiscoveryError("receiver and source share the same tty")
    if (
        receiver_serial.usb_parent is not None
        and receiver_serial.usb_parent == source_serial.usb_parent
    ):
        raise HilDiscoveryError("receiver and source share the same USB parent")
    return {
        "receiver": RoleIdentity("receiver", receiver_probe, receiver_serial),
        "source": RoleIdentity("source", source_probe, source_serial),
    }


def _validate_explicit_probe_serials(binding, explicit_probe_serials):
    if explicit_probe_serials is None:
        if binding.roles["source"].probe.backend == "nrf-probes":
            raise HilDiscoveryError(
                "same-family CMSIS-DAP fixture requires explicit probe serials"
            )
        return None
    if not isinstance(explicit_probe_serials, Mapping) or set(
        explicit_probe_serials
    ) != {
        "receiver",
        "source",
    }:
        raise HilDiscoveryError(
            "explicit probe serials must contain receiver and source"
        )
    serials = {}
    for role in ("receiver", "source"):
        serial = explicit_probe_serials[role]
        if not isinstance(serial, str) or not PROBE_SERIAL_RE.fullmatch(serial):
            raise HilDiscoveryError("explicit %s probe serial is unsafe" % role)
        serials[role] = serial
        probe = binding.roles[role].probe
        if probe.backend != "nrf-probes" or probe.family != "nrf54l":
            raise HilDiscoveryError(
                "explicit probe serials require nRF54L15 CMSIS-DAP roles"
            )
    if serials["receiver"] == serials["source"]:
        raise HilDiscoveryError("receiver and source probe serials must be distinct")
    if set(binding.roles) != {"receiver", "source"}:
        raise HilDiscoveryError(
            "explicit probe serials require receiver/source-only fixture"
        )
    return serials


def _resolve_explicit_pair(binding, serials, run_cmd, sysfs_root, raw):
    argv = [*NIX_NRF_PROBES, serials["receiver"], serials["source"]]
    proc = run_cmd(argv, 90)
    _record_probe_output(raw, "nrf-probes", argv, proc)
    if proc.returncode != 0:
        raise HilDiscoveryError(
            "nix-nrf probes failed with status %d" % proc.returncode
        )
    rows = parse_nrf_probes_table(proc.stdout or "")
    if len(rows) != 2:
        raise HilDiscoveryError("explicit probe table must contain exactly two rows")
    probes = {}
    for role in ("receiver", "source"):
        serial = serials[role]
        matches = [row for row in rows if row.get("SERIAL") == serial]
        if len(matches) != 1:
            raise HilDiscoveryError("explicit %s probe table row mismatch" % role)
        raw["%s-probe" % role] = dict(matches[0])
        probes[role] = _resolve_cmsis_probe(run_cmd, role, serial, matches[0], raw)
    roles = _resolved_roles(
        binding, probes["receiver"], probes["source"], run_cmd, sysfs_root, raw
    )
    receiver_parent = roles["receiver"].serial.usb_parent
    source_parent = roles["source"].serial.usb_parent
    if (
        receiver_parent is None
        or source_parent is None
        or receiver_parent == source_parent
    ):
        raise HilDiscoveryError(
            "explicit receiver and source need distinct USB parents"
        )
    return FixtureResolution(roles=MappingProxyType(roles), raw=MappingProxyType(raw))


def _resolve_fixture_impl(binding, *, run_cmd, sysfs_root, raw, explicit_probe_serials):
    """Resolve every role without mutating target or line-control state."""
    if not isinstance(binding, model.PhysicalBinding):
        raise HilDiscoveryError("binding must be a validated PhysicalBinding")
    serials = _validate_explicit_probe_serials(binding, explicit_probe_serials)
    if serials is not None:
        return _resolve_explicit_pair(binding, serials, run_cmd, sysfs_root, raw)

    argv = list(NIX_NRF_PROBES)
    plain_proc = run_cmd(argv, 90)
    _record_probe_output(raw, "nrf-probes", argv, plain_proc)
    if plain_proc.returncode != 0:
        raise HilDiscoveryError(
            "nix-nrf probes failed with status %d" % plain_proc.returncode
        )
    table_rows = parse_nrf_probes_table(plain_proc.stdout or "")
    receiver_probe, receiver_row = _resolve_receiver(run_cmd, binding, table_rows, raw)
    raw["receiver-probe"] = dict(receiver_row)
    source_probe, source_node = _resolve_source_probe(run_cmd, sysfs_root, binding, raw)
    raw["source-probe-node"] = source_node
    roles = _resolved_roles(
        binding, receiver_probe, source_probe, run_cmd, sysfs_root, raw
    )
    return FixtureResolution(roles=MappingProxyType(roles), raw=MappingProxyType(raw))


def resolve_fixture(
    binding, *, run_cmd, sysfs_root="/sys", explicit_probe_serials=None
):
    """Resolve every role and retain partial raw evidence on failure.

    ``explicit_probe_serials`` is required for a same-family nRF54L15 pair;
    it is otherwise ``None`` for the existing mixed nRF54L15/nRF5340 fixture.
    """
    raw = {}
    try:
        return _resolve_fixture_impl(
            binding,
            run_cmd=run_cmd,
            sysfs_root=sysfs_root,
            raw=raw,
            explicit_probe_serials=explicit_probe_serials,
        )
    except HilDiscoveryError as exc:
        exc.raw = MappingProxyType(dict(raw))
        raise
    except Exception as exc:  # noqa: BLE001 - external discovery boundary
        wrapped = HilDiscoveryError("identity discovery failed: %s" % exc)
        wrapped.raw = MappingProxyType(dict(raw))
        raise wrapped from exc
