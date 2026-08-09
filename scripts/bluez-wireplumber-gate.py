#!/usr/bin/env python3
"""
Stock desktop stream gate — Phase 2.

Proves a BAP Unicast Server (receiver) appears as a normal WirePlumber/PipeWire
playback sink and accepts standard PCM playback, using only stock system BlueZ,
WirePlumber, and PipeWire interfaces.

Never imports or runs bap_central.py, registers a MediaEndpoint, opens ISO
sockets, invokes raw-HCI connect, or writes LC3 SDUs.

Usage:
    python3 scripts/bluez-wireplumber-gate.py \\
        --receiver "LE Audio Receiver" \\
        --duration 30 \\
        --log /tmp/receiver.log

Returns 0 on full acceptance, nonzero on failure.  Classifies host prerequisite
failures (exit code 1) separately from receiver interoperability failures
(exit code 2).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

# ── Exit codes ───────────────────────────────────────────────────────────────

EX_OK = 0
EX_HOST_PREREQ = 1  # missing host prerequisite
EX_RECEIVER_FAIL = 2  # receiver interoperability failure
EX_USAGE = 3  # usage / argument error

# ── Constants ─────────────────────────────────────────────────────────────────

BLUEZ_SERVICE = "org.bluez"
BLUEZ_ROOT = "/org/bluez"
BLUEZ_IFACE_DEVICE = "org.bluez.Device1"
BLUEZ_IFACE_ADAPTER = "org.bluez.Adapter1"
BLUEZ_IFACE_MEDIA = "org.bluez.Media1"
DBUS_PROPS = "org.freedesktop.DBus.Properties"

BAP_UUIDS = {
    "PACS": "00001850-0000-1000-8000-00805f9b34fb",
    "ASCS": "0000184e-0000-1000-8000-00805f9b34fb",
    "VCS": "00001844-0000-1000-8000-00805f9b34fb",
}

# Required remote UUIDs (PACS/ASCS/VCS)
REMOTE_UUIDS = {
    "PACS": "00001850-0000-1000-8000-00805f9b34fb",
    "ASCS": "0000184e-0000-1000-8000-00805f9b34fb",
    "VCS": "00001844-0000-1000-8000-00805f9b34fb",
}

EXPERIMENTAL_ISO_UUID = "6fbaf188-05e0-496a-9885-d6ddfdb4e03e"

DEFAULT_POLL_TIMEOUT = 30.0  # seconds to wait for PipeWire objects
DEFAULT_POLL_INTERVAL = 0.5

# Minimum BlueZ version expected
MIN_BLUEZ_VERSION = (5, 66)

RECEIVER_ADDRESS_RE = re.compile(r"^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$")


# ── Data classes ──────────────────────────────────────────────────────────────


@dataclass
class PreflightResult:
    passed: bool = True
    failures: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)
    details: Dict[str, str] = field(default_factory=dict)


@dataclass
class GateResult:
    success: bool = False
    exit_code: int = EX_HOST_PREREQ
    stage: str = "init"
    evidence: List[str] = field(default_factory=list)
    receiver_counters: Dict[str, Any] = field(default_factory=dict)


# ── Helpers ───────────────────────────────────────────────────────────────────


def _run(args: List[str], timeout: float = 15.0) -> subprocess.CompletedProcess:
    """Run a command and return CompletedProcess; raise on timeout."""
    return subprocess.run(
        args,
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def _run_ok(args: List[str], timeout: float = 15.0) -> bool:
    """Run a command; return True if exit code is 0."""
    try:
        return _run(args, timeout=timeout).returncode == 0
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return False


def _parse_version(version_str: str) -> Tuple[int, ...]:
    """Parse a dotted version string into a tuple of ints.
    Handles strings like "5.86" or "bluetoothctl: 5.86"."""
    # Extract first dotted-version substring
    m = re.search(r"(\d+(?:\.\d+)+)", version_str)
    if m:
        version_str = m.group(1)
    parts = version_str.strip().split(".")
    return tuple(int(p) for p in parts if p.isdigit())


# ── Gate ──────────────────────────────────────────────────────────────────────


class BluezWirePlumberGate:
    """Stock desktop stream gate — Phase 2 acceptance test runner."""

    def __init__(
        self,
        receiver_name: str,
        duration: int,
        log_path: str,
        controller_index: int = 0,
        poll_timeout: float = DEFAULT_POLL_TIMEOUT,
        output_dir: str = "/tmp",
        receiver_address: Optional[str] = None,
    ) -> None:
        self.receiver_name = receiver_name
        self.duration = duration
        self.log_path = log_path
        self.controller_index = controller_index
        self.poll_timeout = poll_timeout
        self.output_dir = output_dir
        if receiver_address is not None and not RECEIVER_ADDRESS_RE.match(
            receiver_address
        ):
            raise ValueError(
                "receiver_address must match XX:XX:XX:XX:XX:XX, got %r"
                % receiver_address
            )
        self.receiver_address = (
            receiver_address.upper() if receiver_address is not None else None
        )
        # PipeWire-visible identity of the receiver (underscore form, e.g.
        # DB_A6_0C_05_A2_AA), derived from --receiver-address or from the
        # BlueZ device path once the receiver is found.  Every PipeWire
        # device/sink must be associated with this address.
        self._receiver_addr: Optional[str] = (
            self._addr_underscore(self.receiver_address)
            if self.receiver_address is not None
            else None
        )

        self._dbus_available: bool = False

    @staticmethod
    def _addr_underscore(addr: str) -> str:
        """Normalize a BT address to the PipeWire underscore form."""
        return addr.replace(":", "_").upper()

    @staticmethod
    def _object_bt_address(obj: Any) -> Optional[str]:
        """Extract the BT address of a pw-dump BlueZ object.

        SPA bluez5 objects carry ``bluez5.address`` (colon form) and/or a
        ``bluez_card.``/``bluez_output.``/``bluez_input.``/``bluez_midi.``
        prefixed name.  Returns the underscore form (``DB_A6_0C_05_A2_AA``)
        or None when the object is not bound to a device.
        """
        props = obj.get("info", {}).get("props", {})
        addr = props.get("bluez5.address")
        if addr:
            return BluezWirePlumberGate._addr_underscore(str(addr))
        name = props.get("node.name") or props.get("device.name") or ""
        for prefix in (
            "bluez_output.",
            "bluez_input.",
            "bluez_card.",
            "bluez_midi.",
        ):
            if name.startswith(prefix):
                return name[len(prefix) :].split(".")[0].upper()
        return None

    # ── Preflight ─────────────────────────────────────────────────────────

    def preflight(self) -> PreflightResult:
        """Run all host prerequisite checks.  Return PreflightResult."""
        result = PreflightResult()

        self._check_bluez(result)
        self._check_controller(result)
        self._check_experimental_iso(result)
        self._check_local_uuids(result)
        self._check_wireplumber_pipewire(result)
        self._check_session_seat(result)
        self._check_wp_profile(result)

        # Check BlueZ SPA monitor: if not active, determine whether this is
        # a host prerequisite failure (no seat + wrong profile) or just
        # a diagnostic warning.
        spa_active = self._check_bluez_spa_monitor(result)
        if not spa_active:
            seat = result.details.get("session_seat", "")
            profile = result.details.get("wp_profile", "unknown")
            if not seat and profile != "main-systemwide":
                result.failures.append(
                    "BlueZ SPA monitor not loaded. "
                    "Session has no logind seat and WirePlumber profile is "
                    "'{}', not 'main-systemwide'. "
                    "Remedy: either run under a local active desktop session, "
                    "or start WirePlumber with --profile main-systemwide.".format(
                        profile
                    )
                )
            elif seat:
                result.failures.append(
                    "BlueZ SPA monitor not loaded despite active seat. "
                    "Check WirePlumber/BlueZ SPA plugin installation."
                )
            else:
                result.warnings.append(
                    "BlueZ SPA monitor not loaded. "
                    "Profile is main-systemwide — monitor should load when "
                    "a LE Audio device connects."
                )

        if result.failures:
            result.passed = False
        return result

    def _check_bluez(self, result: PreflightResult) -> None:
        """Check BlueZ version and running service."""
        # Version
        try:
            proc = _run(["bluetoothctl", "--version"])
            ver = proc.stdout.strip()
            result.details["bluez_version"] = ver
            # Parse version from "bluetoothctl: 5.86"
            m = re.search(r"(\d+\.\d+)", ver)
            if m:
                v_tuple = _parse_version(m.group(1))
                if v_tuple < MIN_BLUEZ_VERSION:
                    msg = (
                        f"BlueZ version {m.group(1)} < "
                        f"{'.'.join(str(x) for x in MIN_BLUEZ_VERSION)}"
                    )
                    result.failures.append(msg)
            else:
                result.warnings.append(f"Cannot parse BlueZ version from: {ver}")
        except (subprocess.TimeoutExpired, FileNotFoundError) as e:
            result.failures.append(f"bluetoothctl not available: {e}")

        # Service running
        try:
            proc = _run(["systemctl", "--user", "is-active", "bluetooth"])
            result.details["bluetoothd_active"] = proc.stdout.strip()
        except Exception:
            # bluetoothd may be system service
            try:
                proc = _run(["systemctl", "is-active", "bluetooth"])
                result.details["bluetoothd_active"] = proc.stdout.strip()
            except Exception:
                result.failures.append("bluetoothd service status unknown")

        # Try dbus
        try:
            import dbus  # noqa: F811

            bus = dbus.SystemBus()
            obj = bus.get_object(BLUEZ_SERVICE, BLUEZ_ROOT)
            self._dbus_available = True
            result.details["dbus_bluez"] = "reachable"
        except Exception as e:
            result.failures.append(f"BlueZ D-Bus unreachable: {e}")

    def _check_controller(self, result: PreflightResult) -> None:
        """Check powered LE controller with cis-central and secure-conn."""
        try:
            proc = _run(
                ["sudo", "btmgmt", "--index", f"hci{self.controller_index}", "info"]
            )
            output = proc.stdout
            result.details["btmgmt_info"] = output

            if "current settings" not in output.lower():
                result.failures.append("btmgmt info: no 'current settings' line")
                return

            settings = output.lower()
            for required in ["powered", "le", "cis-central", "secure-conn"]:
                if required not in settings:
                    result.failures.append(f"Controller missing setting: {required}")

            # Check for "current settings" line specifically
            for line in output.splitlines():
                if "current settings" in line.lower():
                    result.details["current_settings"] = line.strip()
        except Exception as e:
            result.failures.append(f"Cannot check controller settings: {e}")

    def _check_experimental_iso(self, result: PreflightResult) -> None:
        """Check BlueZ experimental ISO UUID is enabled."""
        if not self._dbus_available:
            result.failures.append("Cannot check experimental ISO (no D-Bus)")
            return
        try:
            import dbus

            bus = dbus.SystemBus()
            adapter_path = f"/org/bluez/hci{self.controller_index}"
            adapter = bus.get_object(BLUEZ_SERVICE, adapter_path)
            props_iface = dbus.Interface(adapter, DBUS_PROPS)
            features = props_iface.Get(BLUEZ_IFACE_ADAPTER, "ExperimentalFeatures")
            features_list = list(features) if features else []
            result.details["experimental_features"] = str(features_list)

            if EXPERIMENTAL_ISO_UUID not in features_list:
                result.failures.append(
                    f"Experimental ISO UUID ({EXPERIMENTAL_ISO_UUID}) "
                    "not in adapter ExperimentalFeatures"
                )
        except Exception as e:
            result.failures.append(f"Experimental ISO check failed: {e}")

    def _check_local_uuids(self, result: PreflightResult) -> None:
        """Check local controller exposes PACS/ASCS UUIDs as evidence BAP
        roles are active."""
        if not self._dbus_available:
            result.failures.append("Cannot check local UUIDs (no D-Bus)")
            return
        try:
            import dbus

            bus = dbus.SystemBus()
            adapter_path = f"/org/bluez/hci{self.controller_index}"
            adapter = bus.get_object(BLUEZ_SERVICE, adapter_path)
            props_iface = dbus.Interface(adapter, DBUS_PROPS)
            uuids = props_iface.Get(BLUEZ_IFACE_ADAPTER, "UUIDs")
            uuid_list = [str(u) for u in (uuids or [])]
            result.details["local_uuids"] = str(uuid_list)

            for role, uuid in BAP_UUIDS.items():
                if uuid not in uuid_list:
                    result.failures.append(
                        f"Local controller missing {role} UUID ({uuid})"
                    )
        except Exception as e:
            result.failures.append(f"Local UUID check failed: {e}")

    def _check_wireplumber_pipewire(self, result: PreflightResult) -> None:
        """Check running WirePlumber / PipeWire and active user session.
        WirePlumber may be running as a systemd service OR manually;
        detect actual PipeWire connectivity and SPA presence instead of
        treating service status alone as sufficient."""
        # PipeWire must be active
        try:
            proc = _run(["systemctl", "--user", "is-active", "pipewire"])
            status = proc.stdout.strip()
            result.details["pipewire_active"] = status
            if status != "active":
                result.failures.append(f"pipewire not active: {status}")
        except Exception as e:
            result.failures.append(f"Cannot check pipewire: {e}")

        # WirePlumber: record service status but do NOT fail on inactive —
        # manual or systemwide sessions start wp without systemd.
        try:
            proc = _run(["systemctl", "--user", "is-active", "wireplumber"])
            result.details["wireplumber_active"] = proc.stdout.strip()
        except Exception:
            result.details["wireplumber_active"] = "unknown"

        # Check pw-dump works
        try:
            proc = _run(["pw-dump"], timeout=10.0)
            if proc.returncode == 0:
                result.details["pw_dump_ok"] = "true"
            else:
                result.failures.append("pw-dump returned nonzero")
        except Exception as e:
            result.failures.append(f"pw-dump failed: {e}")

        # Check wpctl works
        try:
            proc = _run(["wpctl", "status"], timeout=10.0)
            if proc.returncode == 0:
                result.details["wpctl_ok"] = "true"
            else:
                result.warnings.append("wpctl status returned nonzero")
        except Exception as e:
            result.warnings.append(f"wpctl failed: {e}")

    def _check_session_seat(self, result: PreflightResult) -> None:
        """Detect active logind seat for this session."""
        try:
            proc = _run(["loginctl"])
            result.details["loginctl_available"] = "true"
        except Exception:
            result.details["loginctl_available"] = "false"
            result.warnings.append("loginctl not available; cannot detect seat")
            return

        try:
            # Get current session
            proc = _run(
                ["loginctl", "list-sessions", "--no-legend"],
                timeout=10.0,
            )
            sessions = proc.stdout.strip().splitlines()
            if not sessions:
                result.details["session_seat"] = ""
                result.details["session_remote"] = "unknown"
                result.warnings.append("No logind sessions found")
                return

            # Use first session
            session_id = sessions[0].split()[0]
            proc = _run(
                ["loginctl", "show-session", session_id, "-p", "Seat", "-p", "Remote"],
                timeout=10.0,
            )
            for line in proc.stdout.splitlines():
                if line.startswith("Seat="):
                    seat_val = line.split("=", 1)[1] if "=" in line else ""
                    result.details["session_seat"] = seat_val
                if line.startswith("Remote="):
                    remote_val = line.split("=", 1)[1] if "=" in line else ""
                    result.details["session_remote"] = remote_val

            seat = result.details.get("session_seat", "")
            remote = result.details.get("session_remote", "")
            if not seat:
                result.details["session_has_seat"] = "false"
                result.warnings.append(
                    "Session has no logind seat (Remote={}). "
                    "BlueZ monitor requires active seat or systemwide profile.".format(
                        remote
                    )
                )
            else:
                result.details["session_has_seat"] = "true"
        except Exception as e:
            result.warnings.append(f"Session seat check failed: {e}")

    def _check_bluez_spa_monitor(self, result: PreflightResult) -> bool:
        """Detect whether BlueZ SPA monitor is actually loaded in PipeWire.
        Returns True if monitor is active (bluetooth device nodes present)."""
        monitor_active = False
        try:
            proc = _run(["pw-dump"], timeout=10.0)
            dump = json.loads(proc.stdout)
            bt_device_count = 0
            bt_node_count = 0
            for obj in dump:
                props = obj.get("info", {}).get("props", {})
                da = props.get("device.api", "")
                nn = props.get("node.name", "")
                if "bluez" in da.lower():
                    bt_device_count += 1
                if "bluez" in nn.lower():
                    bt_node_count += 1

            result.details["bluez_spa_devices"] = str(bt_device_count)
            result.details["bluez_spa_nodes"] = str(bt_node_count)

            if bt_device_count > 0 or bt_node_count > 0:
                monitor_active = True
                result.details["bluez_spa_monitor"] = "active"
            else:
                result.details["bluez_spa_monitor"] = "inactive"
        except Exception as e:
            result.details["bluez_spa_monitor"] = f"error: {e}"

        return monitor_active

    def _check_wp_profile(self, result: PreflightResult) -> None:
        """Detect WirePlumber profile (main vs main-systemwide)."""
        try:
            proc = _run(["ps", "aux"], timeout=5.0)
            wp_lines = [
                l for l in proc.stdout.splitlines() if "wireplumber" in l.lower()
            ]
            result.details["wp_process_lines"] = str(len(wp_lines))
            for line in wp_lines:
                if "--profile" in line:
                    result.details["wp_profile_argv"] = line.strip()[:200]
                    if "main-systemwide" in line:
                        result.details["wp_profile"] = "main-systemwide"
                    elif "main" in line:
                        result.details["wp_profile"] = "main"
                    else:
                        result.details["wp_profile"] = "custom"
                    return
            # No --profile flag → default (main)
            result.details["wp_profile"] = (
                "main" if "wireplumber" in " ".join(wp_lines) else "unknown"
            )
        except Exception as e:
            result.details["wp_profile"] = f"error: {e}"

    # ── BlueZ device checks ──────────────────────────────────────────────

    def find_receiver(self) -> Optional[str]:
        """Find the receiver device path in BlueZ.  Return D-Bus object path
        or None.

        Identity is exact, not substring-based: with ``receiver_address``
        configured only that address matches; otherwise the device name must
        equal the configured receiver name (case-insensitive).  A different
        device that merely shares a name fragment never qualifies.
        """
        try:
            proc = _run(["bluetoothctl", "devices"])
            wanted = self.receiver_address or None
            for line in proc.stdout.splitlines():
                toks = line.split()
                if len(toks) < 3 or toks[0] != "Device":
                    continue
                addr = toks[1].upper()
                name = " ".join(toks[2:]).strip()
                if wanted is not None:
                    if addr != wanted:
                        continue
                elif name.lower() != self.receiver_name.lower():
                    continue
                return f"/org/bluez/hci{self.controller_index}/dev_{addr.replace(':', '_')}"
        except Exception:
            pass
        return None

    def check_device_state(self, device_path: str, result: GateResult) -> bool:
        """Check device connected/bonded/trusted/ServicesResolved and has
        PACS/ASCS/VCS UUIDs.  Append evidence to result.  Return True if all
        required conditions met."""
        try:
            import dbus

            bus = dbus.SystemBus()
            device = bus.get_object(BLUEZ_SERVICE, device_path)
            props_iface = dbus.Interface(device, DBUS_PROPS)

            checks = {
                "Connected": "b",
                "Paired": "b",
                "Bonded": "b",
                "Trusted": "b",
                "ServicesResolved": "b",
            }

            all_ok = True
            for prop, _type in checks.items():
                try:
                    val = props_iface.Get(BLUEZ_IFACE_DEVICE, prop)
                    result.evidence.append(f"  {prop}: {val}")
                    if not val:
                        all_ok = False
                        result.evidence.append(
                            f"  FAIL: {prop} is {val}, expected True"
                        )
                except Exception as e:
                    all_ok = False
                    result.evidence.append(f"  FAIL: Cannot read {prop}: {e}")

            # Identity consistency: the D-Bus object's Address must match the
            # address the path was built from, so a name/address-matched
            # device cannot be swapped for an unrelated BlueZ object.
            path_addr = (
                device_path.rsplit("/", 1)[-1].removeprefix("dev_").replace("_", ":")
            )
            try:
                dev_addr = str(props_iface.Get(BLUEZ_IFACE_DEVICE, "Address")).upper()
                result.evidence.append(f"  Address: {dev_addr}")
                if dev_addr != path_addr.upper():
                    all_ok = False
                    result.evidence.append(
                        f"  FAIL: Device Address {dev_addr} does not match "
                        f"object path address {path_addr}"
                    )
            except Exception as e:
                all_ok = False
                result.evidence.append(f"  FAIL: Cannot read Address: {e}")

            # Check UUIDs
            try:
                uuids = props_iface.Get(BLUEZ_IFACE_DEVICE, "UUIDs")
                uuid_list = [str(u) for u in (uuids or [])]
                result.evidence.append(f"  UUIDs: {uuid_list}")
                for name, uuid_str in REMOTE_UUIDS.items():
                    if uuid_str in uuid_list:
                        result.evidence.append(f"  ✓ {name} UUID present")
                    else:
                        result.evidence.append(f"  ✗ {name} UUID missing")
                        all_ok = False
            except Exception as e:
                all_ok = False
                result.evidence.append(f"  FAIL: Cannot read UUIDs: {e}")

            return all_ok

        except ImportError:
            result.evidence.append("  FAIL: dbus-python not available")
            return False
        except Exception as e:
            result.evidence.append(f"  FAIL: Device state check error: {e}")
            return False

    # ── PipeWire / WirePlumber polling ───────────────────────────────────

    def poll_pipewire_objects(self, result: GateResult, timeout: float) -> bool:
        """Poll pw-dump / wpctl until the receiver's bluetooth device, BAP
        profile, and audio playback sink appear.  Report exact objects on
        timeout.  Return True if all three objects found.

        Every object must be associated with the receiver's BT address and
        the sink must be an actual Audio/Sink playback node: an unrelated
        BlueZ device (mouse, keyboard, MIDI) never satisfies readiness.
        """
        start = time.monotonic()
        result.evidence.append(f"Polling PipeWire objects (timeout={timeout}s)...")

        receiver_addr = self._receiver_addr
        found_device = False
        found_profile = False
        found_sink = False
        last_dump: Dict[str, Any] = {}
        last_wpctl: str = ""

        while time.monotonic() - start < timeout:
            # Poll pw-dump
            dump = self._get_pw_dump()
            if dump:
                last_dump = dump

                # Search for the receiver's bluetooth device (address-bound).
                devices = self._find_bt_devices(dump, receiver_addr)
                if devices:
                    found_device = True
                    result.evidence.append(f"  Found BT devices: {json.dumps(devices)}")

                # Search for the receiver's Audio/Sink playback node — never
                # MIDI/source/unrelated nodes.
                sinks = self._find_bt_sink_nodes(dump, receiver_addr)
                if sinks:
                    found_sink = True
                    result.evidence.append(f"  Found BT sinks: {json.dumps(sinks)}")

            # Poll wpctl: the receiver's node (address form) must appear so
            # the audio profile is actually exposed, not merely any BlueZ
            # object.
            try:
                wproc = _run(["wpctl", "status"], timeout=10.0)
                last_wpctl = wproc.stdout
                if receiver_addr:
                    if receiver_addr in last_wpctl:
                        found_profile = True
                elif (
                    "LE Audio Receiver" in last_wpctl
                    or self.receiver_name in last_wpctl
                ):
                    found_profile = True
            except Exception:
                pass

            if found_device and found_sink and found_profile:
                break

            time.sleep(DEFAULT_POLL_INTERVAL)

        if not found_device or not found_sink or not found_profile:
            result.evidence.append("--- PipeWire object search timed out ---")
            result.evidence.append(
                f"found_device={found_device} found_sink={found_sink} "
                f"found_profile={found_profile} (receiver_addr={receiver_addr})"
            )
            result.evidence.append(f"Last wpctl status:\n{last_wpctl[:2000]}")
            result.evidence.append(
                f"Last pw-dump summary: "
                f"{json.dumps(self._summarize_pw_dump(last_dump))}"
            )
            return False

        return True

    @staticmethod
    def _get_pw_dump() -> Optional[Any]:
        """Run pw-dump and return parsed JSON (list of objects)."""
        try:
            proc = _run(["pw-dump"], timeout=10.0)
            return json.loads(proc.stdout)
        except (json.JSONDecodeError, subprocess.TimeoutExpired, FileNotFoundError):
            return None

    @staticmethod
    def _find_bt_devices(
        dump: Any,
        receiver_addr: Optional[str] = None,
    ) -> List[Dict[str, str]]:
        """Search pw-dump for BlueZ device objects.

        With ``receiver_addr`` (underscore form) set, only the object bound
        to that address is returned — an unrelated BlueZ device never
        matches the receiver.
        """
        results = []
        for obj in dump:
            info = obj.get("info", {})
            props = info.get("props", {})
            device_api = props.get("device.api", "")
            if "bluez" not in device_api.lower():
                continue
            if receiver_addr is not None:
                if BluezWirePlumberGate._object_bt_address(obj) != receiver_addr:
                    continue
            results.append(
                {
                    "id": obj.get("id"),
                    "name": props.get("device.name", ""),
                    "description": props.get("device.description", ""),
                    "api": device_api,
                    "media_class": props.get("media.class", ""),
                }
            )
        return results

    @staticmethod
    def _find_bt_sink_nodes(
        dump: Any,
        receiver_addr: Optional[str] = None,
    ) -> List[Dict[str, str]]:
        """Search pw-dump for the receiver's actual audio playback nodes.

        Only ``Audio/Sink`` media-class BlueZ nodes count; MIDI/source/
        unrelated nodes never do.  With ``receiver_addr`` set, only nodes
        bound to that address are returned.
        """
        results = []
        for obj in dump:
            info = obj.get("info", {})
            props = info.get("props", {})
            node_name = props.get("node.name", "")
            if "bluez" not in node_name.lower():
                continue
            media_class = props.get("media.class", "")
            if "Audio/Sink" not in media_class:
                continue
            if receiver_addr is not None:
                if BluezWirePlumberGate._object_bt_address(obj) != receiver_addr:
                    continue
            results.append(
                {
                    "id": obj.get("id"),
                    "name": node_name,
                    "media_class": media_class,
                }
            )
        return results

    @staticmethod
    def _find_bt_nodes(
        dump: Any,
    ) -> List[Dict[str, str]]:
        """Search pw-dump for bluetooth audio nodes."""
        results = []
        for obj in dump:
            info = obj.get("info", {})
            props = info.get("props", {})
            node_name = props.get("node.name", "")
            if "bluez" in node_name.lower():
                results.append(
                    {
                        "id": obj.get("id"),
                        "name": node_name,
                        "media_class": props.get("media.class", ""),
                    }
                )
        return results

    @staticmethod
    def _summarize_pw_dump(dump: Any) -> Dict[str, Any]:
        """Return a brief summary of pw-dump relevant objects."""
        node_names = []
        device_apis = []
        for obj in dump:
            props = obj.get("info", {}).get("props", {})
            nn = props.get("node.name")
            if nn:
                node_names.append(nn)
            da = props.get("device.api")
            if da:
                device_apis.append(da)
        return {
            "total_objects": len(dump),
            "node_names": node_names,
            "device_apis": device_apis,
        }

    # ── PCM playback ─────────────────────────────────────────────────────

    def play_pcm(self, sink_name: str, result: GateResult) -> bool:
        """Generate temporary 48 kHz stereo PCM/WAV and play to sink."""
        result.evidence.append(f"Generating PCM for sink: {sink_name}")
        try:
            wav_path = os.path.join(self.output_dir, "gate_test.wav")

            # Generate 48 kHz stereo 16-bit sine via sox
            wav_ok = False
            # Try sox first
            if shutil.which("sox"):
                try:
                    _run(
                        [
                            "sox",
                            "-n",
                            "-r",
                            "48000",
                            "-c",
                            "2",
                            "-b",
                            "16",
                            wav_path,
                            "synth",
                            str(self.duration),
                            "sine",
                            "440",
                            "gain",
                            "-12",
                        ],
                        timeout=30.0,
                    )
                    wav_ok = True
                except Exception as e:
                    result.evidence.append(f"  sox failed: {e}")

            # Try ffmpeg
            if not wav_ok and shutil.which("ffmpeg"):
                try:
                    _run(
                        [
                            "ffmpeg",
                            "-y",
                            "-f",
                            "lavfi",
                            "-i",
                            f"sine=frequency=440:duration={self.duration}",
                            "-ar",
                            "48000",
                            "-ac",
                            "2",
                            "-sample_fmt",
                            "s16",
                            wav_path,
                        ],
                        timeout=30.0,
                    )
                    wav_ok = True
                except Exception as e:
                    result.evidence.append(f"  ffmpeg failed: {e}")

            if not wav_ok:
                # Generate raw PCM with Python fallback
                result.evidence.append("  Using Python PCM fallback")
                self._generate_pcm_fallback(wav_path)

            result.evidence.append(f"  WAV created: {wav_path}")

            # Play with pw-play
            result.evidence.append(f"  Playing via pw-play to sink={sink_name}...")
            proc = _run(
                [
                    "pw-play",
                    "--target",
                    sink_name,
                    wav_path,
                ],
                timeout=float(self.duration) + 15.0,
            )
            if proc.returncode == 0:
                result.evidence.append("  pw-play succeeded")
                return True
            else:
                result.evidence.append(
                    f"  pw-play failed (rc={proc.returncode}): {proc.stderr[:500]}"
                )
                return False

        except subprocess.TimeoutExpired:
            result.evidence.append("  pw-play timed out")
            return False
        except Exception as e:
            result.evidence.append(f"  PCM playback error: {e}")
            return False

    def _generate_pcm_fallback(self, wav_path: str) -> None:
        """Generate a WAV file with a 440 Hz sine using pure Python."""
        import struct
        import math

        sr = 48000
        channels = 2
        n_samples = sr * self.duration
        with open(wav_path, "wb") as f:
            # WAV header
            data_size = n_samples * channels * 2
            f.write(b"RIFF")
            f.write(struct.pack("<I", 36 + data_size))
            f.write(b"WAVE")
            f.write(b"fmt ")
            f.write(struct.pack("<I", 16))  # chunk size
            f.write(struct.pack("<H", 1))  # PCM
            f.write(struct.pack("<H", channels))
            f.write(struct.pack("<I", sr))
            f.write(struct.pack("<I", sr * channels * 2))  # byte rate
            f.write(struct.pack("<H", channels * 2))  # block align
            f.write(struct.pack("<H", 16))  # bits per sample
            f.write(b"data")
            f.write(struct.pack("<I", data_size))
            # Generate sine
            for i in range(n_samples):
                val = int(16000 * math.sin(2 * math.pi * 440 * i / sr))
                val = max(-32768, min(32767, val))
                f.write(struct.pack("<hh", val, val))

    # ── Receiver log parsing ─────────────────────────────────────────────

    def parse_receiver_log(self, result: GateResult) -> bool:
        """Parse captured receiver log for ASCS streaming evidence and
        integrity faults.  Return True if green."""
        result.evidence.append("--- Parsing receiver log ---")

        if not os.path.exists(self.log_path):
            result.evidence.append(f"  Log file not found: {self.log_path}")
            return False

        try:
            with open(self.log_path, "rb") as f:
                raw = f.read()
        except Exception as e:
            result.evidence.append(f"  Cannot read log: {e}")
            return False

        # Try to decode as UTF-8, replace errors
        text = raw.decode("utf-8", errors="replace")
        lines = text.splitlines()

        ascs_config = False
        ascs_start = False
        nonzero_frames = False
        decoder_init = False
        i2s_start = False
        frames_per_sec = 0.0
        expected_fps = 0.0  # derived from negotiated frame duration
        malformed_count = 0
        decode_faults = 0
        i2s_faults = 0
        offload_faults = 0
        decoder_not_ready = False
        frame_dur_not_set = False
        freq_not_set = False
        valid_sdus = 0  # from stream summary
        decoded_frames = 0  # from stream summary
        summary_seen = False

        for line in lines:
            # --- ASCS / BAP codec configuration (any prefix) ---
            # Match patterns from LE Audio Receiver firmware (src/bt_bap.c):
            #   "bt_bap: ASE Config"  / "ASE[0] configured"  / "ASE codec_cfg"
            #   "bt_bap: codec_cfg"  / "Frequency:" / "Octets per frame"
            # Also match older "ASCS config" / "ASCS configure"
            if re.search(
                r"(ASCS|bt_bap|ASE).*(config|configured|codec_cfg)",
                line,
                re.IGNORECASE,
            ) or re.search(
                r"(Frequency|Octets per frame|Frames per SDU|chan alloc)",
                line,
                re.IGNORECASE,
            ):
                ascs_config = True
                result.evidence.append(f"  ASCS config: {line[:200]}")

            # --- ASCS / BAP stream start ---
            #   "bt_bap: Enable: stream" / "Stream[0] started"
            #   "bt_bap: Audio path gate OPEN"
            #   "audio_offload: offload stream start" / "offload prep OK"
            #   Also "ASCS start" / "ASCS enable" / "ASCS streaming"
            if re.search(
                r"(bt_bap|ASCS|audio_offload).*"
                r"(Enable:|started|gate OPEN|offload stream start|offload prep OK|"
                r"streaming|enable)",
                line,
                re.IGNORECASE,
            ):
                ascs_start = True
                result.evidence.append(f"  ASCS start: {line[:200]}")
            if "I2S DMA started" in line:
                i2s_start = True
                result.evidence.append(f"  I2S DMA: {line[:200]}")

            # Derive expected frame rate from negotiated frame duration
            # Patterns: "Frame Duration: 7500 us" or "LC3 decoder[0]: 48000 Hz 7500 us"
            m = re.search(r"(?:Frame Duration|decoder).*?(\d+)\s*us", line)
            if m and expected_fps == 0.0:
                frame_us = int(m.group(1))
                if frame_us > 0:
                    expected_fps = 1e6 / frame_us
                    result.evidence.append(
                        f"  Frame duration: {frame_us} us → expected {expected_fps:.1f} fps"
                    )

            # Parse stream summary for all fields including faults.
            # Pattern: "Stream[0] summary: SDUs=123 decoded=456 plc=7 decode_err=0 i2s_underrun=0 stream_reset=0"
            # The trailing empty_sdu=<N> field is optional so historical
            # logs and existing fixtures (older firmware without the
            # field) still parse with the same ordered prefix.
            m = re.search(
                r"Stream\[\d+\]\s+summary:\s+SDUs=(\d+)\s+decoded=(\d+)\s+plc=(\d+)\s+"
                r"decode_err=(\d+)\s+i2s_underrun=(\d+)\s+stream_reset=(\d+)"
                r"(?:\s+empty_sdu=(\d+))?",
                line,
            )
            if m:
                sdu_val = int(m.group(1))
                dec_val = int(m.group(2))
                plc_val = int(m.group(3))
                dec_err_val = int(m.group(4))
                i2s_under_val = int(m.group(5))
                sreset_val = int(m.group(6))
                empty_val = int(m.group(7)) if m.group(7) is not None else None
                valid_sdus = max(valid_sdus, sdu_val)
                decoded_frames = max(decoded_frames, dec_val)
                summary_seen = True
                result.receiver_counters["sdu_summary"] = sdu_val
                result.receiver_counters["decoded_summary"] = dec_val
                result.receiver_counters["plc_summary"] = plc_val
                result.receiver_counters["decode_err_summary"] = dec_err_val
                result.receiver_counters["i2s_underrun_summary"] = i2s_under_val
                result.receiver_counters["stream_reset_summary"] = sreset_val
                if empty_val is not None:
                    result.receiver_counters["empty_sdu_summary"] = empty_val
                evidence_tail = (
                    f" empty_sdu={empty_val}" if empty_val is not None else ""
                )
                result.evidence.append(
                    f"  Stream summary: SDUs={sdu_val} decoded={dec_val} plc={plc_val} "
                    f"decode_err={dec_err_val} i2s_underrun={i2s_under_val} "
                    f"stream_reset={sreset_val}{evidence_tail}"
                )

            # Fatal firmware-side error patterns
            if re.search(r"LC3 decoder not ready", line):
                decoder_not_ready = True
                result.evidence.append(f"  DECODER NOT READY: {line[:200]}")
            if re.search(r"LC3 decoder\[\d+\]:", line):
                decoder_init = True
                result.evidence.append(f"  Decoder init: {line[:200]}")
            if re.search(r"freq not set", line):
                freq_not_set = True
                result.evidence.append(f"  FREQ NOT SET: {line[:200]}")
            if re.search(r"frame dur not set", line):
                frame_dur_not_set = True
                result.evidence.append(f"  FRAME DUR NOT SET: {line[:200]}")

            # Look for frame counters (fps or decoded frames)
            # Skip summary lines — parsed separately above.
            if "summary:" not in line:
                m = re.search(r"(\d+)\s*fps", line, re.IGNORECASE)
                if m and not nonzero_frames:
                    val = int(m.group(1))
                    if val > 0:
                        nonzero_frames = True
                        frames_per_sec = float(val)
                        result.evidence.append(f"  FPS: {line[:200]}")

                # Look for decoded count
                m = re.search(r"decoded[=:\s]*(\d+)", line, re.IGNORECASE)
                if m:
                    val = int(m.group(1))
                    if val > 0:
                        nonzero_frames = True
                        result.evidence.append(f"  Decoded count: {line[:200]}")

            # Fault patterns (skip stream summary lines)
            if "summary:" not in line:
                if re.search(r"malformed", line, re.IGNORECASE):
                    malformed_count += 1
                    result.evidence.append(f"  MALFORMED: {line[:200]}")
                if re.search(r"decode.*(fault|fail|error)", line, re.IGNORECASE):
                    decode_faults += 1
                    result.evidence.append(f"  DECODE FAULT: {line[:200]}")
                if (
                    re.search(
                        r"i2s.*(fault|underrun|error|corrupt)", line, re.IGNORECASE
                    )
                    and "dma corruption" not in line.lower()
                ):
                    # Skip the AGENTS.md gotcha about double-write
                    i2s_faults += 1
                    result.evidence.append(f"  I2S FAULT: {line[:200]}")
                if re.search(r"offload.*(fault|fail|error|stall)", line, re.IGNORECASE):
                    offload_faults += 1
                    result.evidence.append(f"  OFFLOAD FAULT: {line[:200]}")

        result.evidence.append("")
        result.evidence.append(f"  ASCS configured: {ascs_config}")
        result.evidence.append(f"  ASCS streaming: {ascs_start}")
        result.evidence.append(
            f"  Valid SDUs (summary): {valid_sdus}  Decoded frames (summary): {decoded_frames}"
        )
        result.evidence.append(f"  Summary log seen: {summary_seen}")
        result.evidence.append(f"  Nonzero frames: {nonzero_frames}")
        result.evidence.append(f"  I2S DMA start: {i2s_start}")
        result.evidence.append(
            f"  Expected fps: {expected_fps:.1f} ({'derived' if expected_fps > 0 else 'unknown'})"
        )
        result.evidence.append(f"  Frames/sec: {frames_per_sec:.1f}")
        result.evidence.append(f"  Malformed frames: {malformed_count}")
        result.evidence.append(f"  Decode faults: {decode_faults}")
        result.evidence.append(f"  I2S faults: {i2s_faults}")
        result.evidence.append(f"  Offload faults: {offload_faults}")
        result.evidence.append(
            f"  Firmware errors: freq_not_set={freq_not_set} frame_dur_not_set={frame_dur_not_set} decoder_not_ready={decoder_not_ready}"
        )

        # Count lines for basic sanity
        result.evidence.append(f"  Total log lines: {len(lines)}")

        # Determine acceptance — strict evidence gate
        if not ascs_config and not ascs_start:
            result.evidence.append(
                "  FAIL: No ASCS configuration or streaming detected"
            )
            return False
        if not ascs_config:
            result.evidence.append(
                "  NOTE: No ASCS config in log — stream reused cached configuration."
            )
        if not ascs_start:
            result.evidence.append("  FAIL: No ASCS streaming detected")
            return False
        if freq_not_set:
            result.evidence.append(
                "  FAIL: Frequency not set — codec configuration rejected"
            )
            return False
        if frame_dur_not_set:
            result.evidence.append(
                "  FAIL: Frame Duration LTV not set — codec configuration rejected. "
                "Negotiated codec must include the Frame Duration codec configuration field."
            )
            return False
        if decoder_not_ready:
            result.evidence.append(
                "  FAIL: LC3 decoder not ready — codec configuration incomplete"
            )
            return False

        # Require explicit nonzero SDU and decoded counts from stream summary.
        # Decoder init + ASCS start is necessary evidence but never substitutes
        # for actual frame counts.
        if summary_seen:
            if valid_sdus <= 0:
                result.evidence.append(
                    f"  FAIL: Zero valid SDUs reported (summary SDUs={valid_sdus}). "
                    "Expected nonzero SDU count."
                )
                return False
            if decoded_frames <= 0:
                result.evidence.append(
                    f"  FAIL: Zero decoded frames reported (summary decoded={decoded_frames}). "
                    "Expected nonzero decoded frame count."
                )
                return False
            nonzero_frames = True  # Explicit count satisfies the gate
        elif not nonzero_frames:
            result.evidence.append(
                "  FAIL: No explicit SDU/decoded count (stream summary missing) AND "
                "no fps/decoded counter found in log. "
                "Expected stream summary with nonzero SDUs and decoded frames."
            )
            return False
        if not i2s_start:
            result.evidence.append("  FAIL: I2S DMA not started — no audio output path")
            return False
        if malformed_count > 0:
            result.evidence.append(f"  FAIL: {malformed_count} malformed frames")
            return False
        if decode_faults > 0:
            result.evidence.append(f"  FAIL: {decode_faults} decode faults")
            return False
        if i2s_faults > 0:
            result.evidence.append(f"  FAIL: {i2s_faults} I2S faults")
            return False
        if offload_faults > 0:
            result.evidence.append(f"  FAIL: {offload_faults} offload faults")
            return False

        # ── Summary fault-field checks (Phase 2 strict correction) ──
        if summary_seen:
            dec_err = result.receiver_counters.get("decode_err_summary", 0)
            i2s_und = result.receiver_counters.get("i2s_underrun_summary", 0)
            sreset = result.receiver_counters.get("stream_reset_summary", 0)

            if dec_err != 0:
                result.evidence.append(
                    f"  FAIL: Summary decode_err={dec_err} (must be zero). "
                    "Decode errors in stream summary are hard faults."
                )
                return False
            if i2s_und != 0:
                result.evidence.append(
                    f"  FAIL: Summary i2s_underrun={i2s_und} (must be zero). "
                    "I2S underruns in stream summary are hard faults."
                )
                return False
            if sreset != 0:
                result.evidence.append(
                    f"  FAIL: Summary stream_reset={sreset} (must be zero). "
                    "Stream resets in stream summary are hard faults."
                )
                return False

        # ── Duration-consistent SDU count (Phase 2 strict correction) ──
        if summary_seen and expected_fps > 0 and valid_sdus > 0:
            expected_sdus = self.duration * expected_fps
            # Allow ±15% tolerance for stream startup/teardown variance
            lower = expected_sdus * 0.85
            upper = expected_sdus * 1.15
            if valid_sdus < lower:
                result.evidence.append(
                    f"  FAIL: SDU count {valid_sdus} too low for {self.duration}s "
                    f"at {expected_fps:.1f} fps (expected ~{expected_sdus:.0f}, "
                    f"lower bound {lower:.0f}). "
                    "Playback did not produce duration-consistent audio — "
                    "transport may have dropped early or log is stale."
                )
                return False
            if valid_sdus > upper:
                result.evidence.append(
                    f"  FAIL: SDU count {valid_sdus} exceeds expected "
                    f"{expected_sdus:.0f} (+15%). "
                    "SDU counter may be stale from a prior run."
                )
                return False
            result.evidence.append(
                f"  SDU consistency: {valid_sdus} SDUs in {self.duration}s "
                f"at {expected_fps:.1f} fps (expected ~{expected_sdus:.0f}, "
                f"tolerance ±15%)"
            )

        # Validate frame rate against negotiated duration
        if frames_per_sec > 0 and expected_fps > 0:
            if abs(frames_per_sec - expected_fps) > (expected_fps * 0.2):
                result.evidence.append(
                    f"  FAIL: FPS {frames_per_sec:.1f} deviates from "
                    f"expected {expected_fps:.1f} (±20%)"
                )
                return False

        result.evidence.append("  ✓ Receiver log clean")
        return True

    # ── Full gate run ────────────────────────────────────────────────────

    def run(self) -> GateResult:
        """Execute the full Phase 2 gate.  Return GateResult."""
        result = GateResult()
        result.stage = "preflight"

        # 1. Preflight
        pf = self.preflight()
        result.evidence.append("=== Preflight ===")
        result.evidence.extend(pf.failures)
        result.evidence.extend(pf.warnings)
        for k, v in pf.details.items():
            result.evidence.append(f"  {k}: {v}")

        if not pf.passed:
            result.exit_code = EX_HOST_PREREQ
            result.evidence.append("FAIL: Host prerequisites not met")
            return result

        result.evidence.append("✓ All preflights passed")

        # 2. Find receiver
        result.stage = "find_receiver"
        result.evidence.append("=== Find Receiver ===")
        device_path = self.find_receiver()
        if not device_path:
            result.evidence.append(
                f"No paired device matching '{self.receiver_name}' found"
            )
            result.evidence.append("Try: bluetoothctl devices")
            result.exit_code = EX_HOST_PREREQ
            return result
        result.evidence.append(f"Device path: {device_path}")
        if self._receiver_addr is None:
            # Derive the PipeWire address identity from the BlueZ object path
            # (…/dev_DB_A6_0C_05_A2_AA) so every downstream check can bind
            # PipeWire objects to the receiver.
            self._receiver_addr = device_path.rsplit("/", 1)[-1].removeprefix("dev_")
        result.evidence.append(f"Receiver address: {self._receiver_addr}")

        # 3. Check device state
        result.stage = "device_state"
        result.evidence.append("=== Device State ===")
        if not self.check_device_state(device_path, result):
            result.evidence.append("FAIL: Device state requirements not met")
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append("✓ Device state OK")

        # 4. Poll for PipeWire objects
        result.stage = "poll_pipewire"
        result.evidence.append("=== PipeWire Object Poll ===")
        pw_ok = self.poll_pipewire_objects(result, self.poll_timeout)

        if not pw_ok:
            result.evidence.append(
                "FAIL: No PipeWire bluetooth sink appeared within timeout. "
                "Check SPA monitor status (bluez_spa_monitor in preflight) "
                "and verify the LE Audio device is connected with all "
                "required UUIDs (PACS/ASCS/VCS)."
            )
            result.exit_code = EX_RECEIVER_FAIL
            return result

        # 6. Play PCM
        result.stage = "playback"
        result.evidence.append("=== PCM Playback ===")
        sink_name = self._find_sink_name()
        if not sink_name:
            result.evidence.append("FAIL: Cannot determine sink name")
            result.exit_code = EX_RECEIVER_FAIL
            return result
        if not self.play_pcm(sink_name, result):
            result.evidence.append("FAIL: PCM playback failed")
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append("✓ PCM playback OK")

        # 7. Parse receiver log
        result.stage = "receiver_log"
        result.evidence.append("=== Receiver Log Analysis ===")
        if not self.parse_receiver_log(result):
            result.evidence.append("FAIL: Receiver log has faults")
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append("✓ Receiver log clean")

        result.success = True
        result.exit_code = EX_OK
        result.evidence.append("=== PHASE 2 STREAM ACCEPTANCE PASSED ===")
        profile = result.evidence[0:0]  # no-op; profile recorded during preflight
        return result

    def _find_sink_name(self) -> Optional[str]:
        """Find the receiver's audio playback sink name from pw-dump.

        Only an Audio/Sink node bound to the receiver's address qualifies.
        MIDI or other unrelated BlueZ nodes are never returned.
        """
        dump = self._get_pw_dump()
        if not dump:
            return None

        sinks = self._find_bt_sink_nodes(dump, self._receiver_addr)
        if sinks:
            return sinks[0]["name"]
        return None


# ── CLI ───────────────────────────────────────────────────────────────────────


def main() -> int:
    parser = argparse.ArgumentParser(description="Phase 2 stock desktop stream gate")
    parser.add_argument(
        "--receiver",
        default="LE Audio Receiver",
        help="Receiver device name (default: LE Audio Receiver)",
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=30,
        help="Playback duration in seconds (default: 30)",
    )
    parser.add_argument(
        "--log",
        default="/tmp/receiver.log",
        help="Path to captured receiver serial log",
    )
    parser.add_argument(
        "--controller",
        type=int,
        default=0,
        help="BlueZ controller index (default: 0)",
    )
    parser.add_argument(
        "--poll-timeout",
        type=float,
        default=DEFAULT_POLL_TIMEOUT,
        help=f"Seconds to wait for PipeWire objects (default: {DEFAULT_POLL_TIMEOUT})",
    )
    parser.add_argument(
        "--output-dir",
        default="/tmp",
        help="Directory for generated files (default: /tmp)",
    )
    parser.add_argument(
        "--receiver-address",
        default=None,
        help=(
            "Exact receiver BT address (XX:XX:XX:XX:XX:XX). When set, only "
            "that device qualifies as the receiver and every PipeWire "
            "device/sink must be bound to it."
        ),
    )

    args = parser.parse_args()

    try:
        sys.stdout.reconfigure(line_buffering=True)  # type: ignore[attr-defined]
    except Exception:
        pass

    if args.receiver_address is not None and not RECEIVER_ADDRESS_RE.match(
        args.receiver_address
    ):
        parser.error(
            "--receiver-address must match XX:XX:XX:XX:XX:XX, got %r"
            % args.receiver_address
        )

    gate = BluezWirePlumberGate(
        receiver_name=args.receiver,
        duration=args.duration,
        log_path=args.log,
        controller_index=args.controller,
        poll_timeout=args.poll_timeout,
        output_dir=args.output_dir,
        receiver_address=args.receiver_address,
    )

    gresult = gate.run()

    for line in gresult.evidence:
        print(line)

    print(f"\nExit code: {gresult.exit_code} (stage={gresult.stage})")
    return gresult.exit_code


if __name__ == "__main__":
    sys.exit(main())
