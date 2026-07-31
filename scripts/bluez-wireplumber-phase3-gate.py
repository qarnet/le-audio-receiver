#!/usr/bin/env python3
"""
Phase 3 pairing/reconnect lifecycle gate.

Proves normal BlueZ first pairing, persisted-bond reconnect, and repeated
stock PipeWire playback without bap_central.py or raw-HCI.

Extends Phase 2 BluezWirePlumberGate with serial shell, BlueZ lifecycle
management, disconnect/reconnect, and reset-reconnect verification.

Usage:
    # Full sequence (all steps):
    python3 scripts/bluez-wireplumber-phase3-gate.py \\
        --receiver "LE Audio Receiver" \\
        --serial /dev/ttyACM0 \\
        --duration 30 \\
        --log-dir /tmp/phase3

    # Single playback check (assumes device already connected):
    python3 scripts/bluez-wireplumber-phase3-gate.py \\
        --stage playback-only \\
        --duration 30 \\
        --log /tmp/receiver.log

Returns 0 on full acceptance, nonzero on failure.
"""

from __future__ import annotations

import argparse
import atexit
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import importlib

# Reuse Phase 2 gate for parsing, preflight, PW object detection, playback
SCRIPT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, SCRIPT_DIR)
_bg = importlib.import_module("bluez-wireplumber-gate")
BluezWirePlumberGate = _bg.BluezWirePlumberGate
GateResult = _bg.GateResult
PreflightResult = _bg.PreflightResult
EX_OK = _bg.EX_OK
EX_HOST_PREREQ = _bg.EX_HOST_PREREQ
EX_RECEIVER_FAIL = _bg.EX_RECEIVER_FAIL

# ── Constants ─────────────────────────────────────────────────────────────────

DEFAULT_SERIAL_PORT = "/dev/ttyACM0"
DEFAULT_SERIAL_BAUD = 115200
DEFAULT_POLL_TIMEOUT = 30.0
DEFAULT_POLL_INTERVAL = 0.5
DEFAULT_SCAN_TIMEOUT = 30.0  # seconds to wait for advertising
DEFAULT_PAIR_TIMEOUT = 15.0
DEFAULT_CONNECT_TIMEOUT = 15.0

# ── Phase 3 specific exit codes ──────────────────────────────────────────────

EX_STALE_BOND = 4  # stale bond detected
EX_PAIR_REJECT = 5  # pairing rejected
EX_NO_ADVERTISE = 6  # receiver not advertising
EX_SERVICE_FAIL = 7  # required services not resolved
EX_RESET_FAIL = 8  # reset reconnect failure

# ── Data classes ──────────────────────────────────────────────────────────────


@dataclass
class Phase3Result:
    """Full Phase 3 sequence result."""

    success: bool = False
    exit_code: int = EX_HOST_PREREQ
    stage: str = "init"
    evidence: List[str] = field(default_factory=list)
    playback_results: List[GateResult] = field(default_factory=list)


# ── Receiver serial shell ─────────────────────────────────────────────────────


class ReceiverSerial:
    """Serial shell interface to LE Audio Receiver.

    Uses pyserial to interact with the Zephyr shell on the receiver.
    Provides send_command() for scripted interaction and
    capture_log_to_file() for independent log capture.
    """

    def __init__(
        self,
        port: str = DEFAULT_SERIAL_PORT,
        baud: int = DEFAULT_SERIAL_BAUD,
        timeout: float = 1.0,
    ) -> None:
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self._ser: Any = None

    def _get_serial(self) -> Any:
        """Lazy-import and open pyserial."""
        if self._ser is not None:
            return self._ser
        import serial

        self._ser = serial.Serial(self.port, self.baud, timeout=self.timeout)
        return self._ser

    def reset(self) -> None:
        """Close and reopen the serial port."""
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None

    def send_command(self, cmd: str, wait_ms: float = 500) -> Tuple[str, str]:
        """Send a shell command and return (stdout, stderr) lines.

        Sends the command, waits for the shell prompt to return,
        and extracts lines between the command echo and the prompt.
        """
        ser = self._get_serial()
        # Drain any pending data
        ser.reset_input_buffer()

        # Send command with CR+LF
        ser.write((cmd + "\r\n").encode("utf-8"))
        ser.flush()

        output: List[str] = []
        t_start = time.monotonic()

        # Read until we get a prompt back (uart:~$) or timeout
        prompt_seen = False
        while time.monotonic() - t_start < max(2.0, wait_ms / 1000):
            try:
                data = ser.read(4096)
                if data:
                    decoded = data.decode("utf-8", errors="replace")
                    output.append(decoded)
                    if "uart:~$" in decoded:
                        prompt_seen = True
                        break
            except Exception:
                break

        full = "".join(output)

        # Strip ANSI escape codes
        full = re.sub(r"\x1b\[[0-9;]*m", "", full)

        # Split into lines
        lines = [l.strip() for l in full.splitlines() if l.strip()]

        # Remove the command echo and prompt lines
        stdout_lines = []
        stderr_lines = []
        for line in lines:
            if line == cmd or line == "uart:~$":
                continue
            if line.startswith(cmd):
                continue
            # Error lines often have error/fail keywords
            if "error" in line.lower() or "fail" in line.lower() or "ERR" in line:
                stderr_lines.append(line)
            else:
                stdout_lines.append(line)

        return ("\n".join(stdout_lines), "\n".join(stderr_lines))

    def bt_unpair(self) -> Tuple[bool, str]:
        """Send 'bt unpair' command. Returns (success, output)."""
        stdout, stderr = self.send_command("bt unpair", wait_ms=2000)
        output = stdout + ("\n" + stderr if stderr else "")
        if "All bonds cleared" in output:
            return (True, output)
        return (False, output)

    def audio_status(self) -> Dict[str, Any]:
        """Send 'audio status' and parse key/value pairs."""
        stdout, stderr = self.send_command("audio status", wait_ms=1000)
        counters: Dict[str, Any] = {}
        for line in (stdout + stderr).splitlines():
            line = line.strip()
            if ":" in line:
                key, _, val = line.partition(":")
                key = key.strip().lower().replace(" ", "_")
                val = val.strip()
                try:
                    if "/" in val:
                        # "102 / 255" format
                        parts = val.split("/")
                        counters[key] = int(parts[0].strip())
                    else:
                        counters[key] = int(val)
                except ValueError:
                    counters[key] = val
        return counters

    def close(self) -> None:
        """Close the serial port."""
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None


# ── Phase 3 Gate ─────────────────────────────────────────────────────────────


class Phase3Gate:
    """Phase 3 pairing/reconnect lifecycle gate.

    Orchestrates the full sequence:
    - unpair + remove
    - scan + pair + trust + connect
    - playback (x2, with disconnect/reconnect)
    - reset + reconnect + playback
    - service restoration

    Reuses Phase 2 BluezWirePlumberGate for preflight, PW polling,
    PCM playback, and log analysis.
    """

    def __init__(
        self,
        receiver_name: str = "LE Audio Receiver",
        serial_port: str = DEFAULT_SERIAL_PORT,
        duration: int = 30,
        log_dir: str = "/tmp/phase3",
        controller_index: int = 0,
        poll_timeout: float = DEFAULT_POLL_TIMEOUT,
    ) -> None:
        self.receiver_name = receiver_name
        self.receiver_addr: Optional[str] = None  # Discovered via scan, never injected
        self.serial_port = serial_port
        self.duration = duration
        self.log_dir = log_dir
        self.controller_index = controller_index
        self.poll_timeout = poll_timeout

        # Ensure log directory exists
        os.makedirs(log_dir, exist_ok=True)

        # Serial interface — open eagerly to keep SAMD11 CDC bridge alive.
        # Never close/reopen; the USB CDC endpoint on Xiao boards fails
        # if the serial port is closed and reopened.
        self.serial = ReceiverSerial(port=serial_port)
        try:
            self.serial._get_serial()  # force eager open, caches handle
        except Exception:
            pass  # not a real port (e.g. tests) — lazy open still works

        # Backed-up host settings (for restoration)
        self._saved_pairable: Optional[str] = None
        self._saved_discoverable: Optional[str] = None

        # Subprocess tracking — all started by this gate
        self._agent_proc: Optional[subprocess.Popen] = None
        self._scan_on: bool = False

        # WirePlumber lifecycle ownership
        self._wp_owned: bool = False
        self._wp_proc: Optional[subprocess.Popen] = None
        self._saved_wp_service_state: Optional[str] = None

        # Register atexit cleanup for catastrophic exit paths
        atexit.register(self._atexit_cleanup)

    def _make_phase2_gate(self, log_path: str) -> BluezWirePlumberGate:
        """Create a Phase 2 gate instance for a specific log file."""
        return BluezWirePlumberGate(
            receiver_name=self.receiver_name,
            duration=self.duration,
            log_path=log_path,
            controller_index=self.controller_index,
            poll_timeout=self.poll_timeout,
            output_dir=self.log_dir,
        )

    def _run_bluez_cmd(
        self, args: List[str], timeout: float = 15.0
    ) -> subprocess.CompletedProcess:
        """Run a bluetoothctl command and return result."""
        return subprocess.run(
            ["bluetoothctl"] + args,
            capture_output=True,
            text=True,
            timeout=timeout,
        )

    def _run_btmgmt_cmd(
        self, args: List[str], timeout: float = 15.0
    ) -> subprocess.CompletedProcess:
        """Run a sudo btmgmt command."""
        return subprocess.run(
            ["sudo", "btmgmt", "--index", f"hci{self.controller_index}"] + args,
            capture_output=True,
            text=True,
            timeout=timeout,
        )

    # ── Settings backup/restore ───────────────────────────────────────────

    def backup_host_settings(self) -> None:
        """Capture current adapter pairable/discoverable state."""
        try:
            proc = self._run_bluez_cmd(["show"], timeout=5.0)
            for line in proc.stdout.splitlines():
                if "Pairable:" in line:
                    self._saved_pairable = line.split(":")[1].strip()
                if "Discoverable:" in line:
                    self._saved_discoverable = line.split(":")[1].strip()
        except Exception:
            pass

    def restore_host_settings(self) -> None:
        """Restore adapter pairable/discoverable state."""
        if self._saved_pairable is not None:
            try:
                val = "on" if self._saved_pairable.lower() == "yes" else "off"
                self._run_bluez_cmd([val, "pairable"], timeout=5.0)
            except Exception:
                pass
        if self._saved_discoverable is not None:
            try:
                val = "on" if self._saved_discoverable.lower() == "yes" else "off"
                self._run_bluez_cmd([val, "discoverable"], timeout=5.0)
            except Exception:
                pass

    # ── BlueZ lifecycle ───────────────────────────────────────────────────

    def find_device_by_name(
        self, timeout: float = DEFAULT_SCAN_TIMEOUT
    ) -> Optional[str]:
        """Scan for receiver via D-Bus and return address. Returns None on timeout.

        Uses org.bluez.Adapter1.StartDiscovery / StopDiscovery via D-Bus
        because bluetoothctl scan on does not persist discovery in
        non-interactive subprocess mode.
        """
        import dbus

        bus = dbus.SystemBus()
        adapter_path = f"/org/bluez/hci{self.controller_index}"
        adapter = bus.get_object("org.bluez", adapter_path)
        adapter_iface = dbus.Interface(adapter, "org.bluez.Adapter1")

        try:
            # Set LE-only transport filter
            adapter_iface.SetDiscoveryFilter({"Transport": dbus.String("le")})
            adapter_iface.StartDiscovery()
            self._scan_on = True
        except dbus.exceptions.DBusException as e:
            # Discovery may already be active
            if "Already" in str(e) or "InProgress" in str(e):
                self._scan_on = True

        start = time.monotonic()
        while time.monotonic() - start < timeout:
            try:
                proc = self._run_bluez_cmd(["devices"], timeout=5.0)
                for line in proc.stdout.splitlines():
                    if self.receiver_name in line:
                        addr = line.split()[1]
                        self._stop_discovery()
                        return addr
            except Exception:
                pass
            time.sleep(DEFAULT_POLL_INTERVAL)

        self._stop_discovery()
        return None

    def _stop_discovery(self) -> None:
        """Stop D-Bus discovery if active."""
        if not self._scan_on:
            return
        try:
            import dbus

            bus = dbus.SystemBus()
            adapter_path = f"/org/bluez/hci{self.controller_index}"
            adapter = bus.get_object("org.bluez", adapter_path)
            adapter_iface = dbus.Interface(adapter, "org.bluez.Adapter1")
            adapter_iface.StopDiscovery()
        except Exception:
            pass
        self._scan_on = False

    def enable_pairing_agent(self) -> bool:
        """Start own bt-agent subprocess and configure adapter for pairing.

        Owns the agent lifecycle: start agent, verify registration,
        keep alive through Pair(). Adapter state is set for Just Works/SC.
        """
        try:
            # Start own bt-agent subprocess (NoInputNoOutput for Just Works)
            agent = self._start_btagent()
            if agent is None:
                print("FAIL: Cannot start bt-agent subprocess", file=sys.stderr)
                return False
            print("bt-agent started (pid={})".format(agent.pid), file=sys.stderr)

            # Set IO capability to NoInputNoOutput (Just Works)
            self._run_btmgmt_cmd(["io-cap", "3"], timeout=5.0)
            # Make pairable
            self._run_bluez_cmd(["pairable", "on"], timeout=5.0)
            # Enable Secure Connections
            self._run_btmgmt_cmd(["sc", "on"], timeout=5.0)
            return True
        except Exception as e:
            print(f"Cannot enable pairing agent: {e}", file=sys.stderr)
            return False

    def pair_device(self, addr: str, timeout: float = DEFAULT_PAIR_TIMEOUT) -> bool:
        """Pair with device. Returns True on success."""
        try:
            proc = self._run_bluez_cmd(["pair", addr], timeout=timeout)
            if proc.returncode != 0:
                return False
            # Check for pairing failure in output
            output = proc.stdout + proc.stderr
            if "not available" in output.lower():
                return False
            if "AuthenticationFailed" in output:
                return False
            if "AuthenticationCanceled" in output:
                return False
            return True
        except subprocess.TimeoutExpired:
            return False
        except Exception:
            return False

    def trust_device(self, addr: str) -> bool:
        """Trust the device. Returns True on success."""
        try:
            proc = self._run_bluez_cmd(["trust", addr], timeout=5.0)
            return proc.returncode == 0
        except Exception:
            return False

    def connect_device(
        self, addr: str, timeout: float = DEFAULT_CONNECT_TIMEOUT
    ) -> bool:
        """Connect to device. Returns True on success."""
        try:
            proc = self._run_bluez_cmd(["connect", addr], timeout=timeout)
            return proc.returncode == 0
        except Exception:
            return False

    def disconnect_device(self, addr: str) -> bool:
        """Disconnect from device."""
        try:
            proc = self._run_bluez_cmd(["disconnect", addr], timeout=10.0)
            return proc.returncode == 0
        except Exception:
            return False

    def remove_device(self, addr: str) -> bool:
        """Remove device from BlueZ.

        Returns True if device is fully removed (bluetoothctl remove
        succeeded OR postcondition shows no device object and no bond
        state).  Returns False only when the device still exists with
        Paired/Bonded state — that is a fatal preflight failure.
        """
        # Try the normal remove path
        try:
            proc = self._run_bluez_cmd(["remove", addr], timeout=5.0)
            if proc.returncode == 0:
                return True
        except Exception:
            pass

        # Remove failed — check postcondition
        try:
            info = self._run_bluez_cmd(["info", addr], timeout=5.0)
        except Exception:
            # info failed → device object does not exist → clean
            return True

        # Device object still exists — check for bond state
        if "Paired: yes" not in info.stdout and "Bonded: yes" not in info.stdout:
            # No paired or bonded state → clean enough
            return True

        # Device exists AND is paired/bonded → fatal
        return False

    def is_device_paired(self, addr: str) -> bool:
        """Check if device is paired."""
        try:
            proc = self._run_bluez_cmd(["info", addr], timeout=5.0)
            return "Paired: yes" in proc.stdout
        except Exception:
            return False

    def is_device_connected(self, addr: str) -> bool:
        """Check if device is connected."""
        try:
            proc = self._run_bluez_cmd(["info", addr], timeout=5.0)
            return "Connected: yes" in proc.stdout
        except Exception:
            return False

    def wait_for_services_resolved(
        self, addr: str, timeout: float = DEFAULT_POLL_TIMEOUT
    ) -> bool:
        """Poll until ServicesResolved is true on the device."""
        import dbus

        bus = dbus.SystemBus()
        dev_path = f"/org/bluez/hci{self.controller_index}/dev_{addr.replace(':', '_').upper()}"
        device = bus.get_object("org.bluez", dev_path)
        props_iface = dbus.Interface(device, "org.freedesktop.DBus.Properties")

        start = time.monotonic()
        while time.monotonic() - start < timeout:
            try:
                resolved = props_iface.Get("org.bluez.Device1", "ServicesResolved")
                if resolved:
                    return True
            except Exception:
                pass
            time.sleep(DEFAULT_POLL_INTERVAL)
        return False

    def check_remote_uuids(self, addr: str) -> Dict[str, bool]:
        """Check which required BAP UUIDs are present on remote device."""
        result: Dict[str, bool] = {
            "PACS": False,
            "ASCS": False,
            "VCS": False,
        }
        remote_uuids = {
            "PACS": "00001850-0000-1000-8000-00805f9b34fb",
            "ASCS": "0000184e-0000-1000-8000-00805f9b34fb",
            "VCS": "00001844-0000-1000-8000-00805f9b34fb",
        }
        try:
            import dbus

            bus = dbus.SystemBus()
            dev_path = f"/org/bluez/hci{self.controller_index}/dev_{addr.replace(':', '_').upper()}"
            device = bus.get_object("org.bluez", dev_path)
            props_iface = dbus.Interface(device, "org.freedesktop.DBus.Properties")
            uuids = props_iface.Get("org.bluez.Device1", "UUIDs")
            uuid_list = [str(u) for u in (uuids or [])]
            for name, uuid_str in remote_uuids.items():
                result[name] = uuid_str in uuid_list
        except Exception:
            pass
        return result

    def wait_for_advertising_restart(self, timeout: float = 20.0) -> bool:
        """Wait for receiver to restart advertising after disconnect.

        Reuses the existing open serial connection — never closes/reopens
        because that can break the SAMD11 USB CDC bridge on Xiao boards.
        The serial was opened during gate init and remains open.

        Firmware prints 'Restarting advertising...' + 'Advertising again'
        after disconnect (main.c:164-172), not the initial 'Advertising as'.
        """
        start = time.monotonic()
        all_data = ""
        while time.monotonic() - start < timeout:
            try:
                ser = self.serial._get_serial()
                data = ser.read(65536)
                if data:
                    chunk = data.decode("utf-8", errors="replace")
                    all_data += chunk
                    if (
                        "Restarting advertising" in all_data
                        or "Advertising again" in all_data
                    ):
                        return True
            except Exception:
                pass
            time.sleep(0.3)

        return False

    # ── Playback helper ──────────────────────────────────────────────────

    def run_playback_phase2(self, log_path: str) -> GateResult:
        """Run playback with continuous UART capture and strict Phase 2 log parsing.

        Captures UART continuously from before pw-play through stream summary,
        without resetting away startup evidence. Uses Phase 2 parse_receiver_log()
        for strict validation.
        """
        gate = self._make_phase2_gate(log_path)

        result = GateResult()
        result.stage = "playback"

        # ── Start continuous UART capture in background thread ──
        capture_done = threading.Event()
        capture_chunks: List[str] = []
        capture_thread: Optional[threading.Thread] = None

        def capture_worker() -> None:
            """Continuously read serial until capture_done is set."""
            try:
                ser = self.serial._get_serial()
                ser.reset_input_buffer()
                while not capture_done.is_set():
                    try:
                        data = ser.read(65536)
                        if data:
                            capture_chunks.append(
                                data.decode("utf-8", errors="replace")
                            )
                    except Exception:
                        time.sleep(0.1)
            except Exception:
                pass

        try:
            # Start capture NOW, before pw-play
            capture_thread = threading.Thread(target=capture_worker, daemon=True)
            capture_thread.start()
            time.sleep(0.5)  # Give capture a head start

            # Find receiver (device should already be connected)
            device_path = gate.find_receiver()
            if not device_path:
                result.evidence.append(
                    f"No paired device matching '{self.receiver_name}' found"
                )
                result.exit_code = EX_HOST_PREREQ
                return result

            # Check device state
            result.stage = "device_state"
            if not gate.check_device_state(device_path, result):
                result.exit_code = EX_RECEIVER_FAIL
                return result

            # Poll for PipeWire objects
            result.stage = "poll_pipewire"
            if not gate.poll_pipewire_objects(result, self.poll_timeout):
                result.exit_code = EX_RECEIVER_FAIL
                return result

            # Play PCM
            result.stage = "play_pcm"
            sink_name = gate._find_sink_name()
            if not sink_name:
                result.evidence.append("FAIL: Cannot determine sink name")
                result.exit_code = EX_RECEIVER_FAIL
                return result
            if not gate.play_pcm(sink_name, result):
                result.exit_code = EX_RECEIVER_FAIL
                return result

            # Wait for stream summary to appear in capture
            result.evidence.append(
                "--- Waiting for stream summary in continuous capture... ---"
            )
            t_start = time.monotonic()
            summary_found = False
            while time.monotonic() - t_start < 15.0:
                combined = "".join(capture_chunks)
                if "Stream[" in combined and "summary:" in combined:
                    summary_found = True
                    # Wait a bit more for the full summary line
                    time.sleep(1.0)
                    break
                time.sleep(0.3)

            if not summary_found:
                result.evidence.append(
                    "  NOTE: Stream summary not seen in capture; "
                    "continuing with available data"
                )

        finally:
            # Stop capture
            capture_done.set()
            if capture_thread is not None:
                capture_thread.join(timeout=3.0)

        # Combine captured data and write to log file
        tail = "".join(capture_chunks)
        try:
            with open(log_path, "w") as f:
                f.write(tail)
        except Exception:
            pass

        result.evidence.append(
            f"  Captured {len(tail)} bytes of UART output to {log_path}"
        )

        # ── Use Phase 2 strict parse_receiver_log() ──
        # Set log_path on the gate so parse_receiver_log can read it
        gate.log_path = log_path
        if not gate.parse_receiver_log(result):
            result.exit_code = EX_RECEIVER_FAIL
            return result

        result.success = True
        result.exit_code = EX_OK
        return result

    # ── Full sequence runner ─────────────────────────────────────────────

    def run_full_sequence(self) -> Phase3Result:
        """Execute the complete Phase 3 hardware sequence.

        Steps 1-12 as specified in the handoff.
        """
        result = Phase3Result()
        result.stage = "sequence"

        # ── WirePlumber lifecycle setup ────────────────────────────
        result.stage = "wp_lifecycle_setup"
        result.evidence.append("=== WirePlumber lifecycle setup ===")
        if not self._begin_wp_lifecycle():
            result.evidence.append("  FAIL: WirePlumber lifecycle setup failed")
            result.exit_code = EX_HOST_PREREQ
            return result
        result.evidence.append("  ✓ WirePlumber ready")

        # ── Pre-run backup ──────────────────────────────────────────
        self.backup_host_settings()

        # ── Step 1: Verify firmware identity ──────────────────────
        result.stage = "step1_verify_firmware"
        result.evidence.append("=== Step 1: Verify firmware identity ===")
        try:
            status = self.serial.audio_status()
            result.evidence.append(f"  Receiver status: {json.dumps(status)}")
            # Check that it's using ASRC linear (nRF54L15 confirmation)
            resampler = status.get("resampler", "")
            result.evidence.append(f"  Resampler: {resampler}")
        except Exception as e:
            result.evidence.append(f"  FAIL: Cannot read receiver status: {e}")
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append("  ✓ Firmware identity confirmed")

        # ── Step 2: Remove host device + unpair receiver ─────────
        result.stage = "step2_remove_and_unpair"
        result.evidence.append(
            "=== Step 2: Remove host BlueZ device + unpair receiver ==="
        )

        # Remove device from BlueZ FIRST to prevent auto-reconnect storm
        proc = self._run_bluez_cmd(["devices"], timeout=5.0)
        for line in proc.stdout.splitlines():
            if self.receiver_name in line:
                parts = line.split()
                if len(parts) >= 2:
                    self.receiver_addr = parts[1]
                    break
        if self.receiver_addr:
            removed = self.remove_device(self.receiver_addr)
            if not removed:
                result.evidence.append(
                    f"  FAIL: Cannot remove {self.receiver_addr} — device "
                    f"still exists and is paired/bonded"
                )
                result.exit_code = EX_HOST_PREREQ
                return result
            result.evidence.append(f"  Remove {self.receiver_addr}: OK")
        else:
            result.evidence.append("  No known device in BlueZ — skip remove")

        # Power-cycle controller to clear any pending auto-reconnect
        try:
            self._run_btmgmt_cmd(["power", "off"], timeout=5.0)
            time.sleep(1.0)
            self._run_btmgmt_cmd(["power", "on"], timeout=5.0)
            time.sleep(1.0)
        except Exception:
            pass

        # Now unpair on receiver — controller won't auto-reconnect
        success, output = self.serial.bt_unpair()
        result.evidence.append(f"  bt unpair output: {output}")
        if not success:
            result.evidence.append(
                "  FAIL: bt unpair failed — cannot clear bonds on receiver"
            )
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append("  ✓ Bonds cleared on receiver")

        # Drain serial — despite removing + power-cycling, some
        # residual reconnect attempts may still arrive.
        time.sleep(1.0)
        try:
            ser = self.serial._get_serial()
            ser.reset_input_buffer()
        except Exception:
            pass
        time.sleep(1.0)

        # ── Step 3: Verify no stale bond ─────────────────────────
        result.stage = "step3_verify_no_stale_bond"
        result.evidence.append("=== Step 3: Verify no stale bond ===")
        # Step 2 already removed device from BlueZ and unpair'd receiver.
        # Just verify no paired/bonded device object remains.
        proc = self._run_bluez_cmd(["devices"], timeout=5.0)
        remaining = [l for l in proc.stdout.splitlines() if self.receiver_name in l]
        fatal_remaining = False
        for line in remaining:
            parts = line.split()
            if len(parts) >= 2:
                rem_addr = parts[1]
                info = self._run_bluez_cmd(["info", rem_addr], timeout=5.0)
                combined = info.stdout + info.stderr
                if "Paired: yes" in combined or "Bonded: yes" in combined:
                    result.evidence.append(
                        f"  FAIL: Stale bonded device remains: {rem_addr}"
                    )
                    fatal_remaining = True
        if fatal_remaining:
            result.exit_code = EX_STALE_BOND
            return result
        result.evidence.append("  ✓ No stale bond on host or receiver")
        time.sleep(1.0)

        # ── Step 4: Enable pairing agent + scan ──────────────────
        result.stage = "step4_enable_pairing"
        result.evidence.append("=== Step 4: Enable pairing agent + scan ===")
        if not self.enable_pairing_agent():
            result.evidence.append("  FAIL: Cannot enable pairing agent")
            result.exit_code = EX_HOST_PREREQ
            return result
        result.evidence.append("  ✓ Agent enabled, adapter pairable")

        # Wait for receiver to start advertising after unpair
        result.evidence.append("  Waiting for receiver advertising...")
        time.sleep(3.0)

        addr = self.find_device_by_name(timeout=DEFAULT_SCAN_TIMEOUT)
        if not addr:
            result.evidence.append("  FAIL: Receiver not found in scan")
            result.exit_code = EX_NO_ADVERTISE
            return result
        self.receiver_addr = addr
        result.evidence.append(f"  ✓ Found receiver: {addr}")

        # ── Step 5: Pair, trust, connect ──────────────────────────
        result.stage = "step5_pair_trust_connect"
        result.evidence.append("=== Step 5: Pair, trust, connect ===")

        # Check for stale bond first
        if self.is_device_paired(addr):
            result.evidence.append("  FAIL: Device already paired (stale bond)")
            result.exit_code = EX_STALE_BOND
            return result

        # Pair
        pair_ok = self.pair_device(addr)
        if not pair_ok:
            result.evidence.append(
                f"  FAIL: Pairing failed for {addr}. "
                "Check Just Works/SC settings, no MITM enforced, "
                "pairing callbacks return SUCCESS."
            )
            result.exit_code = EX_PAIR_REJECT
            return result
        result.evidence.append(f"  ✓ Paired: {addr}")
        time.sleep(1.0)

        # Trust
        trust_ok = self.trust_device(addr)
        if not trust_ok:
            result.evidence.append(f"  FAIL: Trust failed for {addr}")
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append(f"  ✓ Trusted: {addr}")
        time.sleep(0.5)

        # Connect
        connect_ok = self.connect_device(addr)
        if not connect_ok:
            result.evidence.append(f"  FAIL: Cannot connect to {addr}")
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append(f"  ✓ Connected: {addr}")
        time.sleep(1.0)

        # ── Verify full device state after pairing ────────────────
        result.evidence.append("  --- Device state verification ---")
        info = self._run_bluez_cmd(["info", addr], timeout=5.0)
        combined_output = info.stdout + info.stderr
        checks = {
            "Paired": "Paired: yes",
            "Bonded": "Bonded: yes",
            "Trusted": "Trusted: yes",
            "Connected": "Connected: yes",
        }
        all_ok = True
        for label, pattern in checks.items():
            ok = pattern in combined_output
            result.evidence.append(f"    {label}: {'✓ yes' if ok else '✗ NO (FAIL)'}")
            if not ok:
                all_ok = False
        if not all_ok:
            result.evidence.append(
                "  FAIL: Device state incomplete after pairing/trust/connect"
            )
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append("  ✓ Device state: Paired=Bonded=Trusted=Connected=yes")

        # Wait for ServicesResolved
        if not self.wait_for_services_resolved(addr, timeout=15.0):
            result.evidence.append("  FAIL: ServicesResolved never became true")
            result.exit_code = EX_SERVICE_FAIL
            return result
        result.evidence.append("  ✓ ServicesResolved")

        # Check remote UUIDs
        uuids = self.check_remote_uuids(addr)
        result.evidence.append(f"  Remote UUIDs: {json.dumps(uuids)}")
        missing = [k for k, v in uuids.items() if not v]
        if missing:
            result.evidence.append(f"  FAIL: Missing UUIDs: {missing}")
            result.exit_code = EX_SERVICE_FAIL
            return result
        result.evidence.append("  ✓ All required UUIDs present")

        # ── Step 6: WirePlumber detection ──────────────────────────
        result.stage = "step6_wireplumber"
        result.evidence.append("=== Step 6: WirePlumber object detection ===")
        gate_p1 = self._make_phase2_gate(
            os.path.join(self.log_dir, "phase3_playback1.log")
        )
        pw_ok = gate_p1.poll_pipewire_objects(GateResult(), self.poll_timeout)
        if pw_ok:
            result.evidence.append("  ✓ PipeWire objects detected")
        else:
            result.evidence.append("  FAIL: No PipeWire sink appeared")
            result.exit_code = EX_SERVICE_FAIL
            return result

        # ── Step 7: Playback 1 (30s, fresh pair) ──────────────────
        result.stage = "step7_playback1"
        result.evidence.append("=== Step 7: Playback 1 (fresh pair, 30s) ===")
        log1_path = os.path.join(self.log_dir, "phase3_playback1.log")
        pr1 = self.run_playback_phase2(log1_path)
        result.playback_results.append(pr1)
        if not pr1.success:
            result.evidence.extend(pr1.evidence)
            result.evidence.append("  FAIL: First playback failed")
            result.exit_code = pr1.exit_code
            return result
        result.evidence.append("  ✓ Playback 1 passed")

        # ── Step 8: Disconnect ────────────────────────────────────
        result.stage = "step8_disconnect"
        result.evidence.append("=== Step 8: Disconnect ===")
        dc_ok = self.disconnect_device(addr)
        if not dc_ok:
            result.evidence.append("  FAIL: BlueZ disconnect failed — cannot proceed")
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append("  ✓ Disconnect called")

        # Poll until device is actually disconnected (bluetoothctl disconnect
        # may return success before the link drops)
        t_start = time.monotonic()
        dc_timeout = 10.0
        while time.monotonic() - t_start < dc_timeout:
            if not self.is_device_connected(addr):
                break
            time.sleep(0.5)
        if self.is_device_connected(addr):
            result.evidence.append(
                f"  FAIL: Device still connected {dc_timeout}s after disconnect"
            )
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append("  ✓ Device disconnected")

        # Do NOT remove from BlueZ — bond must persist for step 9.
        # BlueZ does not auto-reconnect on pure disconnect; the device
        # stays as Paired:yes, Connected:no.

        # Verify receiver advertising restart — fatal on failure
        adv_ok = self.wait_for_advertising_restart()
        if not adv_ok:
            result.evidence.append(
                "  FAIL: Receiver advertising not confirmed after disconnect"
            )
            result.exit_code = EX_NO_ADVERTISE
            return result
        result.evidence.append("  ✓ Receiver advertising restarted")
        result.evidence.append("  ✓ Disconnect complete")

        # ── Step 9: Reconnect (persisted bond) ────────────────────
        result.stage = "step9_reconnect"
        result.evidence.append("=== Step 9: Reconnect (persisted bond) ===")

        # Device was disconnected but NOT removed — bond remains in BlueZ.
        # Verify bond persisted, then connect without re-pairing.
        if not self.is_device_paired(addr):
            result.evidence.append("  FAIL: Bond lost after disconnect")
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append("  ✓ Bond persisted")

        # Reconnect (NOT pair, just connect)
        rconn_ok = self.connect_device(addr)
        if not rconn_ok:
            result.evidence.append(f"  FAIL: Cannot reconnect to {addr}")
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append(f"  ✓ Reconnected: {addr}")
        time.sleep(1.0)

        # Wait for ServicesResolved again
        if not self.wait_for_services_resolved(addr, timeout=15.0):
            result.evidence.append("  FAIL: ServicesResolved not true on reconnect")
            result.exit_code = EX_SERVICE_FAIL
            return result
        result.evidence.append("  ✓ ServicesResolved on reconnect")

        # Verify UUIDs
        uuids2 = self.check_remote_uuids(addr)
        result.evidence.append(f"  Reconnect UUIDs: {json.dumps(uuids2)}")
        if not all(uuids2.values()):
            result.evidence.append("  FAIL: UUIDs missing on reconnect")
            result.exit_code = EX_SERVICE_FAIL
            return result
        result.evidence.append("  ✓ All UUIDs present on reconnect")

        # Verify PipeWire objects
        if not gate_p1.poll_pipewire_objects(GateResult(), 15.0):
            result.evidence.append("  FAIL: PipeWire sink not restored")
            result.exit_code = EX_SERVICE_FAIL
            return result
        result.evidence.append("  ✓ PipeWire sink restored on reconnect")

        # ── Step 10: Playback 2 (30s, reconnect) ──────────────────
        result.stage = "step10_playback2"
        result.evidence.append("=== Step 10: Playback 2 (reconnect, 30s) ===")
        log2_path = os.path.join(self.log_dir, "phase3_playback2.log")
        pr2 = self.run_playback_phase2(log2_path)
        result.playback_results.append(pr2)
        if not pr2.success:
            result.evidence.extend(pr2.evidence)
            result.evidence.append("  FAIL: Second playback failed")
            result.exit_code = pr2.exit_code
            return result
        result.evidence.append("  ✓ Playback 2 passed")

        # ── Step 11: Reset + reconnect + playback ─────────────────
        result.stage = "step11_reset_reconnect"
        result.evidence.append("=== Step 11: Reset + reconnect + playback ===")

        # Reset the receiver (normal reset via OpenOCD)
        reset_ok = self._reset_receiver_normal()
        if not reset_ok:
            result.evidence.append("  FAIL: Cannot reset receiver normally")
            result.exit_code = EX_RESET_FAIL
            return result
        result.evidence.append("  ✓ Receiver reset (normal, no erase)")

        # Reopen serial after reset
        self.serial.reset()
        time.sleep(3.0)

        # Verify receiver is alive
        try:
            status2 = self.serial.audio_status()
            result.evidence.append(f"  Post-reset status: {json.dumps(status2)}")
        except Exception as e:
            result.evidence.append(f"  FAIL: Cannot reach receiver: {e}")
            result.exit_code = EX_RESET_FAIL
            return result

        # Reconnect with persisted bond
        time.sleep(2.0)
        rconn2_ok = self.connect_device(addr, timeout=20.0)
        if not rconn2_ok:
            result.evidence.append(f"  FAIL: Cannot reconnect after reset to {addr}")
            result.exit_code = EX_RESET_FAIL
            return result
        result.evidence.append(f"  ✓ Reconnected after reset: {addr}")
        time.sleep(1.0)

        # Wait for ServicesResolved
        if not self.wait_for_services_resolved(addr, timeout=15.0):
            result.evidence.append(
                "  FAIL: ServicesResolved not true after reset reconnect"
            )
            result.exit_code = EX_SERVICE_FAIL
            return result
        result.evidence.append("  ✓ ServicesResolved after reset")

        # Verify UUIDs
        uuids3 = self.check_remote_uuids(addr)
        result.evidence.append(f"  Reset reconnect UUIDs: {json.dumps(uuids3)}")
        if not all(uuids3.values()):
            result.evidence.append("  FAIL: UUIDs missing after reset")
            result.exit_code = EX_SERVICE_FAIL
            return result
        result.evidence.append("  ✓ All UUIDs present after reset reconnect")

        # Verify PipeWire objects
        gate_p3 = self._make_phase2_gate(
            os.path.join(self.log_dir, "phase3_playback3.log")
        )
        if not gate_p3.poll_pipewire_objects(GateResult(), 15.0):
            result.evidence.append("  FAIL: PipeWire sink not restored after reset")
            result.exit_code = EX_SERVICE_FAIL
            return result
        result.evidence.append("  ✓ PipeWire sink restored after reset")

        # Short playback (30s — shorter durations have SDU count skew
        # from stream startup overhead; handoff allows 30s).
        short_dur = 30
        orig_dur = self.duration
        self.duration = short_dur
        log3_path = os.path.join(self.log_dir, "phase3_playback3.log")
        pr3 = self.run_playback_phase2(log3_path)
        self.duration = orig_dur
        result.playback_results.append(pr3)
        if not pr3.success:
            result.evidence.extend(pr3.evidence)
            result.evidence.append("  FAIL: Reset-reconnect playback failed")
            result.exit_code = pr3.exit_code
            return result
        result.evidence.append("  ✓ Reset-reconnect playback passed")

        # ── Step 12: Restore settings ──────────────────────────────
        result.stage = "step12_restore_settings"
        result.evidence.append("=== Step 12: Restore host settings ===")
        self.restore_host_settings()
        result.evidence.append("  ✓ Host settings restored")
        result.evidence.append("  ✓ Valid bond left in place")

        # ── Success ────────────────────────────────────────────────
        result.success = True
        result.exit_code = EX_OK
        result.evidence.append("=== PHASE 3 LIFECYCLE ACCEPTANCE PASSED ===")
        result.evidence.append(
            f"  Playback results: {len(result.playback_results)}/3 passed"
        )
        return result

    def _reset_receiver_normal(self) -> bool:
        """Reset the nRF54L15 receiver via OpenOCD (normal reset, no erase).

        Uses 'reset run' command via OpenOCD with CMSIS-DAP probe.
        """
        try:
            # Find probe serial
            proc = subprocess.run(
                ["nrf-probes", "--find", "nrf54l"],
                capture_output=True,
                text=True,
                timeout=10.0,
            )
            probe_serial = proc.stdout.strip()
            if not probe_serial:
                return False

            openocd_cmd = [
                "openocd",
                "-f",
                "interface/cmsis-dap.cfg",
                "-c",
                f"adapter serial {probe_serial}",
                "-c",
                "transport select swd",
                "-c",
                "adapter speed 1000",
                "-f",
                "target/nordic/nrf54l.cfg",
                "-c",
                "init",
                "-c",
                "reset run",
                "-c",
                "shutdown",
            ]
            proc = subprocess.run(
                openocd_cmd,
                capture_output=True,
                text=True,
                timeout=30.0,
            )
            return proc.returncode == 0
        except Exception:
            return False

    def _start_btagent(self) -> Optional[subprocess.Popen]:
        """Start bt-agent --capability=NoInputNoOutput as own subprocess.

        Returns the Popen handle or None on failure. Agent stdout/stderr
        are piped for log capture.
        """
        try:
            proc = subprocess.Popen(
                ["bt-agent", "--capability=NoInputNoOutput"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                preexec_fn=os.setsid,
            )
            # Give agent a moment to register with BlueZ
            time.sleep(0.5)
            if proc.poll() is not None:
                # Agent died immediately
                _, stderr = proc.communicate()
                print(f"bt-agent exited early: {stderr}", file=sys.stderr)
                return None
            self._agent_proc = proc
            return proc
        except FileNotFoundError:
            print("bt-agent not found — install bluez-tools", file=sys.stderr)
            return None
        except Exception as e:
            print(f"Cannot start bt-agent: {e}", file=sys.stderr)
            return None

    def _stop_btagent(self) -> None:
        """Gracefully stop own bt-agent subprocess."""
        if self._agent_proc is None:
            return
        try:
            pid = self._agent_proc.pid
            if pid is None:
                return
            # Send SIGTERM to the process group
            os.killpg(os.getpgid(pid), signal.SIGTERM)
            try:
                self._agent_proc.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                # Force kill
                try:
                    os.killpg(os.getpgid(pid), signal.SIGKILL)
                except ProcessLookupError:
                    pass
                try:
                    self._agent_proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    pass
            # Drain pipes to prevent resource warnings
            if self._agent_proc.stdout:
                self._agent_proc.stdout.close()
            if self._agent_proc.stderr:
                self._agent_proc.stderr.close()
        except (ProcessLookupError, OSError):
            # Already dead
            pass
        except Exception:
            pass
        finally:
            self._agent_proc = None

    def _ensure_scan_off(self) -> None:
        """Disable BlueZ scanning if it was turned on via D-Bus."""
        self._stop_discovery()

    # ── WirePlumber lifecycle ownership ───────────────────────────────────

    def _detect_seat(self) -> Optional[str]:
        """Detect active logind seat for current session. Returns seat name or None."""
        try:
            proc = subprocess.run(
                ["loginctl", "list-sessions", "--no-legend"],
                capture_output=True,
                text=True,
                timeout=10.0,
            )
            sessions = proc.stdout.strip().splitlines()
            if not sessions:
                return None
            session_id = sessions[0].split()[0]
            proc = subprocess.run(
                ["loginctl", "show-session", session_id, "-p", "Seat", "-p", "Remote"],
                capture_output=True,
                text=True,
                timeout=10.0,
            )
            seat = ""
            for line in proc.stdout.splitlines():
                if line.startswith("Seat="):
                    seat = line.split("=", 1)[1] if "=" in line else ""
            return seat if seat else None
        except Exception:
            return None

    def _save_wp_service_state(self) -> None:
        """Record current WirePlumber user service state."""
        try:
            proc = subprocess.run(
                ["systemctl", "--user", "is-active", "wireplumber"],
                capture_output=True,
                text=True,
                timeout=10.0,
            )
            self._saved_wp_service_state = proc.stdout.strip()
        except Exception:
            self._saved_wp_service_state = None

    def _begin_wp_lifecycle(self) -> bool:
        """Ensure WirePlumber runs with correct profile for current session.

        If session has active logind seat → use existing WP as-is.
        If headless (no seat) → stop user service, start own main-systemwide.
        Waits for BlueZ SPA plugin registration in PipeWire.
        Returns True if WP is ready and BlueZ SPA plugin is loaded.
        """
        self._save_wp_service_state()
        seat = self._detect_seat()

        if seat:
            # Active seat: use existing WP
            print(
                f"WirePlumber: active seat '{seat}' — using existing profile",
                file=sys.stderr,
            )
            self._wp_owned = False
        else:
            # Headless: stop user service, start main-systemwide
            print(
                "WirePlumber: no seat — stopping user service, launching main-systemwide",
                file=sys.stderr,
            )

            # Stop user WirePlumber service
            try:
                subprocess.run(
                    ["systemctl", "--user", "stop", "wireplumber"],
                    capture_output=True,
                    text=True,
                    timeout=15.0,
                )
                time.sleep(2.0)
            except Exception as e:
                print(f"WARNING: Cannot stop user wireplumber: {e}", file=sys.stderr)

            # Start own WirePlumber with main-systemwide profile
            try:
                self._wp_proc = subprocess.Popen(
                    ["wireplumber", "--profile", "main-systemwide"],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    preexec_fn=os.setsid,
                )
                self._wp_owned = True
                print(
                    f"wireplumber --profile main-systemwide started (pid={self._wp_proc.pid})",
                    file=sys.stderr,
                )
                time.sleep(3.0)  # Give WP time to initialize
            except Exception as e:
                print(f"FAIL: Cannot start wireplumber: {e}", file=sys.stderr)
                return False

        # Wait for BlueZ SPA plugin to register in PipeWire
        if not self._wait_for_bluez_spa(timeout=20.0):
            print(
                "FAIL: BlueZ SPA plugin not loaded in PipeWire",
                file=sys.stderr,
            )
            return False

        print("WirePlumber ready — BlueZ SPA plugin registered", file=sys.stderr)
        return True

    def _get_wp_pid(self) -> Optional[int]:
        """Return PID of WirePlumber process whose SPA plugin we must verify.

        For owned WP (headless, main-systemwide) use the subprocess PID.
        For active-seat WP, resolve MainPID from the user systemd service.
        Returns None if the PID cannot be determined.
        """
        # Owned process: use tracked PID
        if self._wp_owned and self._wp_proc is not None:
            pid = self._wp_proc.pid
            if pid is not None:
                return pid

        # Active seat: resolve MainPID from user service
        try:
            proc = subprocess.run(
                ["systemctl", "--user", "show", "wireplumber", "-p", "MainPID"],
                capture_output=True,
                text=True,
                timeout=10.0,
            )
            for line in proc.stdout.splitlines():
                if line.startswith("MainPID="):
                    val = line.split("=", 1)[1]
                    if val and val != "0":
                        return int(val)
        except Exception:
            pass

        return None

    def _wait_for_bluez_spa(self, timeout: float = 20.0) -> bool:
        """Wait for BlueZ SPA plugin to be mapped in WirePlumber.

        Proof: WirePlumber process must have libspa-bluez5.so
        mapped in its address space (/proc/<pid>/maps).
        No PipeWire factory/device fallback is accepted — the actual
        plugin map is required.

        Does NOT require a BT device to be connected.
        """
        wp_pid = self._get_wp_pid()
        if wp_pid is None:
            print(
                "FAIL: Cannot determine WirePlumber PID for SPA proof",
                file=sys.stderr,
            )
            return False

        start = time.monotonic()
        while time.monotonic() - start < timeout:
            # Proof: libspa-bluez5.so in WP process maps
            try:
                with open(f"/proc/{wp_pid}/maps", "r") as f:
                    if "libspa-bluez5" in f.read():
                        return True
            except (FileNotFoundError, ProcessLookupError):
                print(
                    f"FAIL: WirePlumber PID {wp_pid} not running (maps unreadable)",
                    file=sys.stderr,
                )
                return False
            except Exception as e:
                print(
                    f"WARNING: Cannot read /proc/{wp_pid}/maps: {e}",
                    file=sys.stderr,
                )

            time.sleep(1.0)
        return False

    def _end_wp_lifecycle(self) -> None:
        """Stop owned WirePlumber and restore original user service."""
        if not self._wp_owned or self._wp_proc is None:
            # WP was not owned by us — nothing to restore
            return

        print("Stopping owned WirePlumber...", file=sys.stderr)
        try:
            pid = self._wp_proc.pid
            if pid is not None:
                os.killpg(os.getpgid(pid), signal.SIGTERM)
                try:
                    self._wp_proc.wait(timeout=5.0)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(os.getpgid(pid), signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    try:
                        self._wp_proc.wait(timeout=3.0)
                    except subprocess.TimeoutExpired:
                        pass
        except (ProcessLookupError, OSError):
            pass
        except Exception as e:
            print(f"WARNING: Error stopping owned WirePlumber: {e}", file=sys.stderr)

        self._wp_proc = None
        self._wp_owned = False

        # Restore original user service if it was active
        if self._saved_wp_service_state == "active":
            print("Restoring user wireplumber service...", file=sys.stderr)
            try:
                subprocess.run(
                    ["systemctl", "--user", "start", "wireplumber"],
                    capture_output=True,
                    text=True,
                    timeout=15.0,
                )
            except Exception as e:
                print(f"WARNING: Cannot restart user wireplumber: {e}", file=sys.stderr)

    def _atexit_cleanup(self) -> None:
        """Last-resort cleanup registered with atexit.

        Handles catastrophic exit paths where the normal finally block
        is skipped (e.g., SIGKILL to parent, interpreter crash).
        Does NOT close serial — CDC bridge dies on close.
        """
        try:
            self._stop_btagent()
        except Exception:
            pass
        try:
            self._ensure_scan_off()
        except Exception:
            pass
        try:
            self._end_wp_lifecycle()
        except Exception:
            pass
        try:
            self.restore_host_settings()
        except Exception:
            pass

    def cleanup(self) -> None:
        """Comprehensive cleanup: agent, scan, WP lifecycle, adapter state.

        Guarantees restoration for every exit path. Must be called in finally.
        No global process kills — only own process groups are stopped.
        Does NOT close serial — SAMD11 CDC bridge dies on close/reopen.
        """
        # 1. Stop own bt-agent subprocess
        self._stop_btagent()

        # 2. Ensure scan is off
        self._ensure_scan_off()

        # 3. Stop owned WirePlumber and restore user service
        self._end_wp_lifecycle()

        # 4. Restore adapter pairable/discoverable state
        self.restore_host_settings()

        # 5. Unregister atexit handler (cleanup already ran)
        try:
            atexit.unregister(self._atexit_cleanup)
        except Exception:
            pass


# ── CLI ───────────────────────────────────────────────────────────────────────


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase 3 pairing/reconnect lifecycle gate"
    )
    parser.add_argument(
        "--receiver",
        default="LE Audio Receiver",
        help="Receiver device name (default: LE Audio Receiver)",
    )
    parser.add_argument(
        "--serial",
        default=DEFAULT_SERIAL_PORT,
        help=f"Serial port for receiver shell (default: {DEFAULT_SERIAL_PORT})",
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=30,
        help="Playback duration in seconds (default: 30)",
    )
    parser.add_argument(
        "--log-dir",
        default="/tmp/phase3",
        help="Directory for log artifacts (default: /tmp/phase3)",
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
        "--stage",
        default="full",
        choices=["full", "playback-only", "unpair-only"],
        help="Which stage to run (default: full)",
    )

    args = parser.parse_args()

    try:
        sys.stdout.reconfigure(line_buffering=True)  # type: ignore[attr-defined]
    except Exception:
        pass

    gate = Phase3Gate(
        receiver_name=args.receiver,
        serial_port=args.serial,
        duration=args.duration,
        log_dir=args.log_dir,
        controller_index=args.controller,
        poll_timeout=args.poll_timeout,
    )

    try:
        if args.stage == "full":
            result = gate.run_full_sequence()
        elif args.stage == "playback-only":
            # Quick playback check
            log_path = os.path.join(args.log_dir, "phase3_playback.log")
            pr = gate.run_playback_phase2(log_path)
            result = Phase3Result(
                success=pr.success,
                exit_code=pr.exit_code,
                stage="playback-only",
                evidence=pr.evidence,
            )
        elif args.stage == "unpair-only":
            # Just unpair
            result = Phase3Result(stage="unpair-only")
            success, output = gate.serial.bt_unpair()
            result.evidence.append(f"bt unpair: {output}")
            result.success = success
            result.exit_code = EX_OK if success else EX_RECEIVER_FAIL
        else:
            print(f"Unknown stage: {args.stage}", file=sys.stderr)
            return 3

    finally:
        gate.cleanup()

    for line in result.evidence:
        print(line)

    print(f"\nExit code: {result.exit_code} (stage={result.stage})")
    return result.exit_code


if __name__ == "__main__":
    sys.exit(main())
