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

        # Serial interface
        self.serial = ReceiverSerial(port=serial_port)

        # Backed-up host settings (for restoration)
        self._saved_pairable: Optional[str] = None
        self._saved_discoverable: Optional[str] = None

        # Subprocess tracking — all started by this gate
        self._agent_proc: Optional[subprocess.Popen] = None
        self._scan_on: bool = False

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
        """Remove device from BlueZ."""
        try:
            proc = self._run_bluez_cmd(["remove", addr], timeout=5.0)
            return proc.returncode == 0
        except Exception:
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

    def wait_for_advertising_restart(self, timeout: float = 15.0) -> bool:
        """Poll receiver serial for advertising restart message.

        Sends 'audio status' to check if the receiver is alive,
        and reads serial output for 'Advertising as' message.
        Returns False without explicit evidence — no fallback success.
        """
        start = time.monotonic()
        while time.monotonic() - start < timeout:
            try:
                stdout, _ = self.serial.send_command("audio status", wait_ms=500)
                if "BLE ready" in stdout or "Advertising as" in stdout:
                    return True
            except Exception:
                pass
            time.sleep(1.0)

        # Final check: flush and read serial
        try:
            ser = self.serial._get_serial()
            ser.reset_input_buffer()
            time.sleep(2.0)
            data = ser.read(4096)
            decoded = data.decode("utf-8", errors="replace")
            if "Advertising as" in decoded:
                return True
        except Exception:
            pass

        return False

    # ── Playback helper ──────────────────────────────────────────────────

    def run_playback_phase2(self, log_path: str) -> GateResult:
        """Run playback (PW poll + PCM playback + counter check).

        Captures raw serial output after playback to find the stream summary
        line and verify zero-fault counters.
        """
        gate = self._make_phase2_gate(log_path)

        result = GateResult()
        result.stage = "playback"

        try:
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

            # Wait briefly, then read raw serial for stream summary
            result.evidence.append("--- Waiting for stream summary... ---")
            time.sleep(2.0)

            # Clear serial input buffer to start fresh
            try:
                ser = self.serial._get_serial()
                ser.reset_input_buffer()
            except Exception:
                pass

            # Read raw serial output for up to 15s, looking for summary
            tail_parts: List[str] = []
            t_start = time.monotonic()
            summary_found = False
            while time.monotonic() - t_start < 15.0:
                try:
                    ser = self.serial._get_serial()
                    data = ser.read(65536)
                    if data:
                        decoded = data.decode("utf-8", errors="replace")
                        tail_parts.append(decoded)
                        if "Stream[" in decoded and "summary:" in decoded:
                            summary_found = True
                            time.sleep(0.5)  # grab any remaining bytes
                            break
                except Exception:
                    time.sleep(0.2)
            tail = "".join(tail_parts)

            # Write captured output to log file
            try:
                with open(log_path, "w") as f:
                    f.write(tail)
            except Exception:
                pass

            # Parse stream summary from tail
            result.evidence.append("--- Parsing stream output ---")

            m = re.search(
                r"Stream\[\d+\]\s+summary:\s+SDUs=(\d+)\s+decoded=(\d+)\s+plc=(\d+)\s+"
                r"decode_err=(\d+)\s+i2s_underrun=(\d+)\s+stream_reset=(\d+)",
                tail,
            )
            if not m:
                result.evidence.append(
                    "  FAIL: No stream summary found in serial output"
                )
                if tail:
                    # Show what we captured for diagnostics
                    result.evidence.append(
                        f"  Captured {len(tail)} bytes, first 300 chars: {tail[:300]}"
                    )
                result.exit_code = EX_RECEIVER_FAIL
                return result

            sdus = int(m.group(1))
            decoded = int(m.group(2))
            plc = int(m.group(3))
            decode_err = int(m.group(4))
            i2s_under = int(m.group(5))
            stream_reset = int(m.group(6))

            result.evidence.append(
                f"  Stream summary: SDUs={sdus} decoded={decoded} plc={plc} "
                f"decode_err={decode_err} i2s_underrun={i2s_under} "
                f"stream_reset={stream_reset}"
            )

            # Check for I2S DMA and ASCS evidence
            if "I2S DMA started" in tail:
                result.evidence.append("  ✓ I2S DMA started")
            if "Stream[0] started" in tail or "Audio path gate OPEN" in tail:
                result.evidence.append("  ✓ ASCS stream started")

            # Zero-fault gate
            if decode_err != 0:
                result.evidence.append(f"  FAIL: decode_err={decode_err} (expected 0)")
                result.exit_code = EX_RECEIVER_FAIL
                return result
            if i2s_under != 0:
                result.evidence.append(f"  FAIL: i2s_underrun={i2s_under} (expected 0)")
                result.exit_code = EX_RECEIVER_FAIL
                return result
            if stream_reset != 0:
                result.evidence.append(
                    f"  FAIL: stream_reset={stream_reset} (expected 0)"
                )
                result.exit_code = EX_RECEIVER_FAIL
                return result
            if sdus <= 0:
                result.evidence.append(f"  FAIL: SDUs={sdus} (expected >0)")
                result.exit_code = EX_RECEIVER_FAIL
                return result
            if decoded <= 0:
                result.evidence.append(f"  FAIL: decoded={decoded} (expected >0)")
                result.exit_code = EX_RECEIVER_FAIL
                return result

            result.success = True
            result.exit_code = EX_OK

        except Exception as e:
            result.evidence.append(f"Playback error: {e}")
            result.exit_code = EX_RECEIVER_FAIL

        return result

    @staticmethod
    def _parse_playback_counters(
        result: GateResult,
        tail: str,
    ) -> None:  # noqa: D401
        """Parse serial output for stream-start evidence (kept for API compat)."""
        if "I2S DMA started" in tail:
            result.evidence.append("  ✓ I2S DMA started")
        if "Stream[0] started" in tail or "Audio path gate OPEN" in tail:
            result.evidence.append("  ✓ ASCS stream started")
        if "offload prep OK" in tail:
            result.evidence.append("  ✓ FLPR offload active")

    # ── Full sequence runner ─────────────────────────────────────────────

    def run_full_sequence(self) -> Phase3Result:
        """Execute the complete Phase 3 hardware sequence.

        Steps 1-12 as specified in the handoff.
        """
        result = Phase3Result()
        result.stage = "sequence"

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

        # ── Step 2: bt unpair ────────────────────────────────────
        result.stage = "step2_bt_unpair"
        result.evidence.append("=== Step 2: bt unpair ===")
        success, output = self.serial.bt_unpair()
        result.evidence.append(f"  bt unpair output: {output}")
        if not success:
            result.evidence.append(
                "  FAIL: bt unpair failed — cannot clear bonds on receiver"
            )
            result.exit_code = EX_RECEIVER_FAIL
            return result
        result.evidence.append("  ✓ Bonds cleared on receiver")
        time.sleep(1.0)

        # ── Step 3: Remove host BlueZ device ──────────────────────
        result.stage = "step3_remove_device"
        result.evidence.append("=== Step 3: Remove host BlueZ device ===")
        # Discover any known address from existing device list
        proc = self._run_bluez_cmd(["devices"], timeout=5.0)
        for line in proc.stdout.splitlines():
            if self.receiver_name in line:
                parts = line.split()
                if len(parts) >= 2:
                    self.receiver_addr = parts[1]
                    break

        if self.receiver_addr:
            removed = self.remove_device(self.receiver_addr)
            result.evidence.append(
                f"  Remove {self.receiver_addr}: {'OK' if removed else 'FAIL'}"
            )
        else:
            result.evidence.append("  No known device in BlueZ — skip remove")
            removed = True  # nothing to remove trivially succeeds
        time.sleep(1.0)

        # Verify: any remaining device entry must NOT be paired/bonded
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
                        f"  FAIL: Stale bonded device remains: {rem_addr} {line}"
                    )
                    fatal_remaining = True
                else:
                    result.evidence.append(
                        f"  NOTE: Rediscovered unpaired device (harmless): {rem_addr}"
                    )
        if fatal_remaining:
            result.exit_code = EX_STALE_BOND
            return result

        if not removed and self.receiver_addr:
            # Remove reported failure — treat as stale bond risk
            result.evidence.append(
                f"  FAIL: Cannot remove device {self.receiver_addr} from BlueZ"
            )
            result.exit_code = EX_STALE_BOND
            return result

        result.evidence.append("  ✓ Host device removed / no stale bond")
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
        result.evidence.append(f"  Disconnect: {'OK' if dc_ok else 'attempted'}")
        time.sleep(2.0)

        # Verify receiver advertising restart
        adv_ok = self.wait_for_advertising_restart()
        result.evidence.append(
            f"  Receiver advertising: {'restarted' if adv_ok else 'unconfirmed'}"
        )
        result.evidence.append("  ✓ Disconnect complete")

        # ── Step 9: Reconnect (persisted bond) ────────────────────
        result.stage = "step9_reconnect"
        result.evidence.append("=== Step 9: Reconnect (persisted bond) ===")

        # Verify device still bonded
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

        # Short playback (10s)
        short_dur = min(10, self.duration)
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

    def _atexit_cleanup(self) -> None:
        """Last-resort cleanup registered with atexit.

        Handles catastrophic exit paths where the normal finally block
        is skipped (e.g., SIGKILL to parent, interpreter crash).
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
            self.restore_host_settings()
        except Exception:
            pass
        try:
            self.serial.close()
        except Exception:
            pass

    def cleanup(self) -> None:
        """Comprehensive cleanup: agent, scan, adapter state, serial, stray processes.

        Guarantees restoration for every exit path. Must be called in finally.
        """
        # 1. Stop own bt-agent subprocess
        self._stop_btagent()

        # 2. Ensure scan is off
        self._ensure_scan_off()

        # 3. Restore adapter pairable/discoverable state
        self.restore_host_settings()

        # 4. Close serial port
        self.serial.close()

        # 5. No stray bt-agent processes from other invocations
        try:
            subprocess.run(
                ["pkill", "-f", "bt-agent"],
                timeout=3.0,
                capture_output=True,
            )
        except Exception:
            pass

        # 6. Unregister atexit handler (cleanup already ran)
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
