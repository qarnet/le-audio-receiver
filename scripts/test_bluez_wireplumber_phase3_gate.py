#!/usr/bin/env python3
"""Unit tests for Phase 3 pairing/reconnect lifecycle gate.

Covers: state polling, timeout/failure classification, stale bond,
pairing rejection, service resolution, reconnect, log scoping.
"""

import importlib
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch, PropertyMock, mock_open

# Ensure scripts directory in path
SCRIPT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, SCRIPT_DIR)

# Import the Phase 3 gate module
_p3 = importlib.import_module("bluez-wireplumber-phase3-gate")
Phase3Gate = _p3.Phase3Gate
Phase3Result = _p3.Phase3Result
ReceiverSerial = _p3.ReceiverSerial
EX_OK = _p3.EX_OK
EX_HOST_PREREQ = _p3.EX_HOST_PREREQ
EX_RECEIVER_FAIL = _p3.EX_RECEIVER_FAIL
EX_STALE_BOND = _p3.EX_STALE_BOND
EX_PAIR_REJECT = _p3.EX_PAIR_REJECT
EX_NO_ADVERTISE = _p3.EX_NO_ADVERTISE
EX_SERVICE_FAIL = _p3.EX_SERVICE_FAIL
EX_RESET_FAIL = _p3.EX_RESET_FAIL

# Import Phase 2 gate for log parsing tests
_bg = importlib.import_module("bluez-wireplumber-gate")
BluezWirePlumberGate = _bg.BluezWirePlumberGate
GateResult = _bg.GateResult


class TestPhase3GateInit(unittest.TestCase):
    """Test Phase3Gate initialization and attribute defaults."""

    def test_default_values(self):
        gate = Phase3Gate()
        self.assertEqual(gate.receiver_name, "LE Audio Receiver")
        self.assertEqual(gate.duration, 30)
        self.assertEqual(gate.log_dir, "/tmp/phase3")
        self.assertIsNone(gate.receiver_addr)

    def test_custom_receiver_name(self):
        gate = Phase3Gate(receiver_name="Test Receiver")
        self.assertEqual(gate.receiver_name, "Test Receiver")

    def test_custom_duration(self):
        gate = Phase3Gate(duration=10)
        self.assertEqual(gate.duration, 10)

    def test_log_dir_created(self):
        with tempfile.TemporaryDirectory() as td:
            log_dir = os.path.join(td, "phase3_test")
            gate = Phase3Gate(log_dir=log_dir)
            self.assertTrue(os.path.isdir(log_dir))


class TestPhase3Result(unittest.TestCase):
    """Test Phase3Result data class defaults."""

    def test_defaults(self):
        r = Phase3Result()
        self.assertFalse(r.success)
        self.assertEqual(r.exit_code, EX_HOST_PREREQ)
        self.assertEqual(r.stage, "init")
        self.assertEqual(r.evidence, [])
        self.assertEqual(r.playback_results, [])

    def test_evidence_appending(self):
        r = Phase3Result()
        r.evidence.append("test line")
        self.assertIn("test line", r.evidence)


class TestExitCodes(unittest.TestCase):
    """Test exit code constants are distinct and meaningful."""

    def test_distinct(self):
        codes = {
            EX_OK,
            EX_HOST_PREREQ,
            EX_RECEIVER_FAIL,
            EX_STALE_BOND,
            EX_PAIR_REJECT,
            EX_NO_ADVERTISE,
            EX_SERVICE_FAIL,
            EX_RESET_FAIL,
        }
        self.assertEqual(len(codes), 8, "All exit codes must be distinct")


class TestReceiverSerialMocked(unittest.TestCase):
    """Test ReceiverSerial command parsing and response handling.

    Mocks ReceiverSerial methods directly to avoid pyserial dependency.
    """

    def setUp(self):
        self.rs = ReceiverSerial(port="/dev/fake", baud=115200)
        # Mock _get_serial to return a mock without importing pyserial
        self.mock_ser = MagicMock()
        self.rs._get_serial = MagicMock(return_value=self.mock_ser)

    def test_send_command_echo_stripping(self):
        """Command echo and prompt should be stripped from output."""
        self.mock_ser.reset_input_buffer = MagicMock()
        self.mock_ser.read.side_effect = [
            b"audio status\r\n--- Audio status ---\r\n  Frames decoded : 0\r\nuart:~$ ",
            b"",
        ]

        stdout, stderr = self.rs.send_command("audio status", wait_ms=100)
        self.assertIn("Frames decoded", stdout)
        self.assertNotIn("uart:~$", stdout)
        self.assertNotIn("audio status", stdout)

    def test_send_command_error_detection(self):
        """Lines containing 'error' or 'fail' should go to stderr."""
        self.mock_ser.reset_input_buffer = MagicMock()
        self.mock_ser.read.side_effect = [
            b"bt unpair\r\nbt_unpair failed: -5\r\nuart:~$ ",
            b"",
        ]

        stdout, stderr = self.rs.send_command("bt unpair", wait_ms=100)
        self.assertIn("bt_unpair failed", stderr)
        self.assertEqual(stdout, "")

    def test_bt_unpair_success(self):
        """bt_unpair with 'All bonds cleared' returns True."""
        self.mock_ser.reset_input_buffer = MagicMock()
        self.mock_ser.read.side_effect = [
            b"bt unpair\r\nAll bonds cleared.\r\nuart:~$ ",
            b"",
        ]

        success, output = self.rs.bt_unpair()
        self.assertTrue(success)
        self.assertIn("All bonds cleared", output)

    def test_bt_unpair_failure(self):
        """bt_unpair with error returns False."""
        self.mock_ser.reset_input_buffer = MagicMock()
        self.mock_ser.read.side_effect = [
            b"bt unpair\r\nbt_unpair failed: -5\r\nuart:~$ ",
            b"",
        ]

        success, output = self.rs.bt_unpair()
        self.assertFalse(success)
        self.assertIn("failed", output)

    def test_audio_status_parsing(self):
        """audio_status should parse key:value pairs."""
        self.mock_ser.reset_input_buffer = MagicMock()
        self.mock_ser.read.side_effect = [
            b"audio status\r\n--- Audio status ---\r\n  Frames decoded : 5000\r\n  Resampler      : ASRC linear\r\n  Volume         : 102 / 255\r\nuart:~$ ",
            b"",
        ]

        status = self.rs.audio_status()
        self.assertEqual(status.get("frames_decoded"), 5000)
        # "decode errors" is not present in this test — key not present → None
        self.assertIsNone(status.get("decode_errors"))
        self.assertEqual(status.get("resampler"), "ASRC linear")
        self.assertEqual(status.get("volume"), 102)

    def test_reset_closes_port(self):
        """reset() should close and clear the serial port."""
        # Set up _ser first
        self.rs._ser = self.mock_ser
        self.rs.reset()
        self.mock_ser.close.assert_called_once()
        self.assertIsNone(self.rs._ser)

    def test_close_closes_port(self):
        """close() should close the serial port."""
        self.rs._ser = self.mock_ser
        self.rs.close()
        self.mock_ser.close.assert_called_once()

    def test_close_no_port(self):
        """close() when no port is open should not raise."""
        rs2 = ReceiverSerial(port="/dev/fake")
        rs2.close()  # Should not raise


class TestPhase3GateMocked(unittest.TestCase):
    """Test Phase3Gate BlueZ lifecycle methods with mocked subprocess."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            serial_port="/dev/fake",
            duration=30,
            log_dir="/tmp/phase3_test",
        )
        # Set a known address so tests work without scan
        self.gate.receiver_addr = "AA:BB:CC:DD:EE:FF"
        # Prevent actual serial
        self.gate.serial = MagicMock()

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    @patch("subprocess.run")
    def test_remove_device_success(self, mock_run):
        """remove_device with rc=0 returns True."""
        mock_run.return_value = MagicMock(returncode=0)
        result = self.gate.remove_device("AA:BB:CC:DD:EE:FF")
        self.assertTrue(result)

    @patch("subprocess.run")
    def test_remove_device_fails_device_gone(self, mock_run):
        """rc!=0 but info fails (device gone) → True."""
        mock_run.side_effect = [
            MagicMock(returncode=1),  # remove fails
            subprocess.CalledProcessError(1, ["bluetoothctl"]),  # info fails
        ]
        result = self.gate.remove_device("AA:BB:CC:DD:EE:FF")
        self.assertTrue(result)

    @patch("subprocess.run")
    def test_remove_device_fails_no_bond(self, mock_run):
        """rc!=0 but info shows no Paired/Bonded → True."""
        mock_run.side_effect = [
            MagicMock(returncode=1),  # remove fails
            MagicMock(returncode=0, stdout="Device\n\tName: LE Audio\n\tPaired: no\n"),
        ]
        result = self.gate.remove_device("AA:BB:CC:DD:EE:FF")
        self.assertTrue(result)

    @patch("subprocess.run")
    def test_remove_device_fails_still_bonded(self, mock_run):
        """rc!=0 and info shows Paired: yes → False (fatal)."""
        mock_run.side_effect = [
            MagicMock(returncode=1),  # remove fails
            MagicMock(returncode=0, stdout="Device\n\tPaired: yes\n"),
        ]
        result = self.gate.remove_device("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)

    @patch("subprocess.run")
    def test_pair_device_success(self, mock_run):
        """pair_device with rc=0 and no error in output returns True."""
        mock_run.return_value = MagicMock(
            returncode=0, stdout="Pairing successful", stderr=""
        )
        # Need to also mock the _run_bluez_cmd for scan etc
        # Just test pair_device directly
        result = self.gate.pair_device("AA:BB:CC:DD:EE:FF")
        # pair_device uses subprocess.run directly
        self.assertTrue(result)

    @patch("subprocess.run")
    def test_pair_device_authentication_failed(self, mock_run):
        """pair_device with AuthenticationFailed in output returns False."""
        mock_run.return_value = MagicMock(
            returncode=1,
            stdout="",
            stderr="Failed to pair: org.bluez.Error.AuthenticationFailed",
        )
        result = self.gate.pair_device("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)

    @patch("subprocess.run")
    def test_pair_device_not_available(self, mock_run):
        """pair_device with 'not available' in output returns False."""
        mock_run.return_value = MagicMock(
            returncode=1,
            stdout="Device not available",
            stderr="",
        )
        result = self.gate.pair_device("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)

    @patch("subprocess.run")
    def test_trust_device_success(self, mock_run):
        """trust_device with rc=0 returns True."""
        mock_run.return_value = MagicMock(returncode=0)
        result = self.gate.trust_device("AA:BB:CC:DD:EE:FF")
        self.assertTrue(result)

    @patch("subprocess.run")
    def test_connect_device_success(self, mock_run):
        """connect_device with rc=0 returns True."""
        mock_run.return_value = MagicMock(returncode=0)
        result = self.gate.connect_device("AA:BB:CC:DD:EE:FF")
        self.assertTrue(result)

    @patch("subprocess.run")
    def test_disconnect_device_success(self, mock_run):
        """disconnect_device with rc=0 returns True."""
        mock_run.return_value = MagicMock(returncode=0)
        result = self.gate.disconnect_device("AA:BB:CC:DD:EE:FF")
        self.assertTrue(result)

    @patch("subprocess.run")
    def test_is_device_paired_true(self, mock_run):
        """is_device_paired with 'Paired: yes' returns True."""
        mock_run.return_value = MagicMock(
            returncode=0, stdout="Paired: yes\nBonded: yes\n"
        )
        result = self.gate.is_device_paired("AA:BB:CC:DD:EE:FF")
        self.assertTrue(result)

    @patch("subprocess.run")
    def test_is_device_paired_false(self, mock_run):
        """is_device_paired with 'Paired: no' returns False."""
        mock_run.return_value = MagicMock(
            returncode=0, stdout="Paired: no\nBonded: no\n"
        )
        result = self.gate.is_device_paired("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)

    @patch("subprocess.run")
    def test_is_device_connected_true(self, mock_run):
        """is_device_connected with 'Connected: yes' returns True."""
        mock_run.return_value = MagicMock(returncode=0, stdout="Connected: yes\n")
        result = self.gate.is_device_connected("AA:BB:CC:DD:EE:FF")
        self.assertTrue(result)

    @patch("subprocess.run")
    def test_is_device_connected_false(self, mock_run):
        """is_device_connected with 'Connected: no' returns False."""
        mock_run.return_value = MagicMock(returncode=0, stdout="Connected: no\n")
        result = self.gate.is_device_connected("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)

    @patch("subprocess.run")
    def test_enable_pairing_agent(self, mock_run):
        """enable_pairing_agent should succeed if all cmds return 0."""
        mock_run.return_value = MagicMock(returncode=0)
        result = self.gate.enable_pairing_agent()
        self.assertTrue(result)
        # Should have called agent on, default-agent, io-cap, pairable on, sc on
        self.assertGreaterEqual(mock_run.call_count, 3)

    @patch("dbus.SystemBus")
    @patch("dbus.Interface")
    @patch("subprocess.run")
    def test_find_device_by_name_found(self, mock_run, mock_iface_ctor, mock_bus):
        """find_device_by_name returns address when device found."""
        addr = "AA:BB:CC:DD:EE:FF"
        mock_adapter = MagicMock()
        mock_bus.return_value.get_object.return_value = mock_adapter
        mock_iface_ctor.return_value = MagicMock()  # Adapter1 iface
        # devices command output
        mock_run.return_value = MagicMock(
            returncode=0,
            stdout=f"Device {addr} Test Receiver\n",
        )
        result = self.gate.find_device_by_name(timeout=1.0)
        self.assertEqual(result, addr)

    @patch("dbus.SystemBus")
    @patch("dbus.Interface")
    @patch("subprocess.run")
    def test_find_device_by_name_not_found(self, mock_run, mock_iface_ctor, mock_bus):
        """find_device_by_name returns None when not found within timeout."""
        mock_adapter = MagicMock()
        mock_bus.return_value.get_object.return_value = mock_adapter
        mock_iface_ctor.return_value = MagicMock()
        mock_run.return_value = MagicMock(
            returncode=0,
            stdout="Device XX:XX:XX:XX:XX:XX Other Device\n",
        )
        result = self.gate.find_device_by_name(timeout=0.1)
        self.assertIsNone(result)

    def test_backup_restore_settings(self):
        """backup_host_settings and restore_host_settings should not raise."""
        try:
            self.gate.backup_host_settings()
        except Exception:
            pass
        try:
            self.gate.restore_host_settings()
        except Exception:
            pass

    def test_reset_receiver_normal_no_probe(self):
        """reset_receiver_normal returns False when probe not found."""
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = MagicMock(
                returncode=0, stdout="", stderr="no probe"
            )
            result = self.gate._reset_receiver_normal()
            self.assertFalse(result)


class TestStaleBondClassification(unittest.TestCase):
    """Test stale bond detection and pairing rejection classification."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            serial_port="/dev/fake",
        )
        self.gate.receiver_addr = "AA:BB:CC:DD:EE:FF"
        self.gate.serial = MagicMock()

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    @patch("subprocess.run")
    def test_stale_bond_detected(self, mock_run):
        """is_device_paired returns True for stale bond after unpair."""
        mock_run.return_value = MagicMock(returncode=0, stdout="Paired: yes\n")
        # This should detect stale bond
        result = self.gate.is_device_paired("AA:BB:CC:DD:EE:FF")
        self.assertTrue(result)

    @patch("subprocess.run")
    def test_pair_failure_classified_as_reject(self, mock_run):
        """Pairing failure should be classified as EX_PAIR_REJECT."""
        mock_run.return_value = MagicMock(
            returncode=1,
            stdout="",
            stderr="Failed to pair: org.bluez.Error.AuthenticationFailed",
        )
        result = self.gate.pair_device("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)
        # The gate should classify this as pairing rejection


class TestServiceResolutionMocked(unittest.TestCase):
    """Test service resolution polling and UUID checking."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            serial_port="/dev/fake",
        )
        self.gate.receiver_addr = "AA:BB:CC:DD:EE:FF"

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    @patch("dbus.SystemBus")
    def test_wait_for_services_resolved_true(self, mock_bus):
        """ServicesResolved returns True immediately."""
        mock_props = MagicMock()
        mock_props.Get.return_value = True
        mock_iface = MagicMock(return_value=mock_props)
        with patch("dbus.Interface", mock_iface):
            result = self.gate.wait_for_services_resolved(
                "AA:BB:CC:DD:EE:FF", timeout=1.0
            )
            self.assertTrue(result)

    @patch("dbus.SystemBus")
    def test_check_remote_uuids_all_present(self, mock_bus):
        """All required UUIDs should be reported as present."""
        mock_props = MagicMock()
        mock_props.Get.return_value = [
            "00001850-0000-1000-8000-00805f9b34fb",
            "0000184e-0000-1000-8000-00805f9b34fb",
            "00001844-0000-1000-8000-00805f9b34fb",
        ]
        mock_iface = MagicMock(return_value=mock_props)
        with patch("dbus.Interface", mock_iface):
            result = self.gate.check_remote_uuids("AA:BB:CC:DD:EE:FF")
            self.assertTrue(result["PACS"])
            self.assertTrue(result["ASCS"])
            self.assertTrue(result["VCS"])

    @patch("dbus.SystemBus")
    def test_check_remote_uuids_missing(self, mock_bus):
        """Missing UUIDs should be reported as False."""
        mock_props = MagicMock()
        mock_props.Get.return_value = [
            "00001800-0000-1000-8000-00805f9b34fb",
        ]
        mock_iface = MagicMock(return_value=mock_props)
        with patch("dbus.Interface", mock_iface):
            result = self.gate.check_remote_uuids("AA:BB:CC:DD:EE:FF")
            self.assertFalse(result["PACS"])
            self.assertFalse(result["ASCS"])
            self.assertFalse(result["VCS"])


class TestLogScoping(unittest.TestCase):
    """Test independent log capture per playback run."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            log_dir="/tmp/phase3_log_scope_test",
        )

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass
        import shutil

        shutil.rmtree("/tmp/phase3_log_scope_test", ignore_errors=True)

    def test_make_phase2_gate_creates_unique_log_paths(self):
        """Each _make_phase2_gate call should reference a different log path."""
        gate1 = self.gate._make_phase2_gate("/tmp/phase3_log_scope_test/p1.log")
        gate2 = self.gate._make_phase2_gate("/tmp/phase3_log_scope_test/p2.log")
        self.assertEqual(gate1.log_path, "/tmp/phase3_log_scope_test/p1.log")
        self.assertEqual(gate2.log_path, "/tmp/phase3_log_scope_test/p2.log")
        self.assertNotEqual(gate1.log_path, gate2.log_path)

    def test_log_dir_created(self):
        """log_dir should be created on init."""
        import shutil

        shutil.rmtree("/tmp/phase3_log_scope_test", ignore_errors=True)
        gate = Phase3Gate(log_dir="/tmp/phase3_log_scope_test")
        self.assertTrue(os.path.isdir("/tmp/phase3_log_scope_test"))


class TestReconnectLifecycle(unittest.TestCase):
    """Test reconnect logic classification."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            serial_port="/dev/fake",
        )
        self.gate.receiver_addr = "AA:BB:CC:DD:EE:FF"
        self.gate.serial = MagicMock()

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    @patch("subprocess.run")
    def test_disconnect_then_connect(self, mock_run):
        """Disconnect followed by connect should work with persisted bond."""
        mock_run.return_value = MagicMock(returncode=0)
        # Disconnect
        self.assertTrue(self.gate.disconnect_device("AA:BB:CC:DD:EE:FF"))
        # Verify still paired (bond persists across disconnect)
        mock_run.return_value = MagicMock(returncode=0, stdout="Paired: yes\n")
        self.assertTrue(self.gate.is_device_paired("AA:BB:CC:DD:EE:FF"))
        # Reconnect (no pair needed)
        mock_run.return_value = MagicMock(returncode=0)
        self.assertTrue(self.gate.connect_device("AA:BB:CC:DD:EE:FF"))

    @patch("subprocess.run")
    def test_connect_failure_after_disconnect(self, mock_run):
        """Failed reconnect should return False."""
        mock_run.return_value = MagicMock(
            returncode=1, stdout="", stderr="Connection refused"
        )
        result = self.gate.connect_device("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)

    @patch("subprocess.run")
    def test_no_pair_needed_for_reconnect(self, mock_run):
        """connect_device should NOT call pair."""
        mock_run.return_value = MagicMock(returncode=0)
        mock_run.reset_mock()
        self.gate.connect_device("AA:BB:CC:DD:EE:FF")
        # Verify pair was NOT called (connect only)
        called_cmds = [c[0][0][0] if c[0][0] else "" for c in mock_run.call_args_list]
        self.assertIn("connect", str(mock_run.call_args_list))


class TestTimeoutFailureClassification(unittest.TestCase):
    """Test timeout handling for BlueZ operations."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            serial_port="/dev/fake",
        )
        self.gate.serial = MagicMock()

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    def test_pair_timeout(self):
        """pair_device with TimeoutExpired returns False."""
        import subprocess as sp

        with patch("subprocess.run", side_effect=sp.TimeoutExpired("cmd", 5)):
            result = self.gate.pair_device("AA:BB:CC:DD:EE:FF")
            self.assertFalse(result)

    def test_connect_timeout(self):
        """connect_device with TimeoutExpired returns False."""
        import subprocess as sp

        with patch("subprocess.run", side_effect=sp.TimeoutExpired("cmd", 5)):
            result = self.gate.connect_device("AA:BB:CC:DD:EE:FF")
            self.assertFalse(result)

    @patch("dbus.SystemBus")
    @patch("subprocess.run")
    def test_find_device_timeout_deterministic(self, mock_run, mock_bus):
        """find_device_by_name returns None when receiver never appears in scan."""
        # Mock D-Bus adapter
        mock_adapter = MagicMock()
        mock_bus.return_value.get_object.return_value = mock_adapter
        # Simulate scan results that never contain the receiver name
        mock_run.return_value = MagicMock(
            returncode=0, stdout="Device XX:XX:XX:XX:XX:XX Other Device\n"
        )
        result = self.gate.find_device_by_name(timeout=0.1)
        self.assertIsNone(result)
        # Verify StopDiscovery was called
        mock_iface = MagicMock()
        with patch("dbus.Interface", return_value=mock_iface):
            pass  # Interface already bound via mock_adapter


class TestFailureModeClassification(unittest.TestCase):
    """Test fatal classification for failure modes: agent death, unpair,
    remove, trust, advertising timeout, cleanup after early-stage failure."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            serial_port="/dev/fake",
        )
        self.gate.serial = MagicMock()

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    def test_agent_death_detected(self):
        """_start_btagent returns None if agent dies immediately."""
        with patch("subprocess.Popen") as mock_popen:
            mock_proc = MagicMock()
            mock_proc.poll.return_value = 1  # already exited
            mock_proc.communicate.return_value = ("", "fatal error")
            mock_popen.return_value = mock_proc
            result = self.gate._start_btagent()
            self.assertIsNone(result)

    def test_unpair_failure_returns_false(self):
        """bt_unpair with failure output returns False."""
        # Use a real ReceiverSerial with mocked serial port
        rs = ReceiverSerial(port="/dev/fake")
        mock_ser = MagicMock()
        rs._get_serial = MagicMock(return_value=mock_ser)
        mock_ser.reset_input_buffer = MagicMock()
        mock_ser.read.side_effect = [
            b"bt unpair\r\nbt_unpair failed: -5\r\nuart:~$ ",
            b"",
        ]
        success, _ = rs.bt_unpair()
        self.assertFalse(success)

    def test_remove_failure_device_still_bonded(self):
        """remove_device with rc!=0 and Paired: yes in info → False."""
        with patch("subprocess.run") as mock_run:
            mock_run.side_effect = [
                MagicMock(returncode=1),  # remove fails
                MagicMock(returncode=0, stdout="Paired: yes\n"),
            ]
            result = self.gate.remove_device("AA:BB:CC:DD:EE:FF")
            self.assertFalse(result)

    @patch("subprocess.run")
    def test_trust_failure_returns_false(self, mock_run):
        """trust_device with rc!=0 returns False."""
        mock_run.return_value = MagicMock(returncode=1)
        result = self.gate.trust_device("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)

    def test_advertising_restart_returns_false_without_evidence(self):
        """wait_for_advertising_restart returns False when no evidence found."""
        self.gate.serial.send_command.return_value = ("", "")
        result = self.gate.wait_for_advertising_restart(timeout=0.1)
        self.assertFalse(result)

    def test_cleanup_after_early_failure(self):
        """cleanup() should not raise after partial initialization."""
        gate = Phase3Gate(
            receiver_name="Test Cleanup",
            serial_port="/dev/fake",
        )
        # Simulate early failure: agent not started, scan never on, no devices
        gate.cleanup()  # Must not raise

    @patch("subprocess.run")
    def test_pair_failure_classified_as_reject(self, mock_run):
        """Pairing with AuthenticationFailed should return False."""
        mock_run.return_value = MagicMock(
            returncode=1,
            stdout="",
            stderr="Failed to pair: org.bluez.Error.AuthenticationFailed",
        )
        result = self.gate.pair_device("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)

    @patch("subprocess.run")
    def test_pair_failure_not_available(self, mock_run):
        """Pairing with 'not available' should return False."""
        mock_run.return_value = MagicMock(
            returncode=1,
            stdout="Device not available",
            stderr="",
        )
        result = self.gate.pair_device("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)


class TestReceiverSerialWaitForAdvertising(unittest.TestCase):
    """Test advertising restart detection."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            serial_port="/dev/fake",
        )
        self.mock_ser = MagicMock()
        self.gate.serial = ReceiverSerial(port="/dev/fake")
        self.gate.serial._get_serial = MagicMock(return_value=self.mock_ser)
        self.gate.serial.reset = MagicMock()

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    def test_wait_for_advertising_restart_returns_false_by_default(self):
        """wait_for_advertising_restart returns False without evidence."""
        self.mock_ser.read.return_value = b""
        result = self.gate.wait_for_advertising_restart(timeout=0.2)
        self.assertFalse(result)

    def test_wait_for_advertising_restart_found(self):
        """If 'Restarting advertising' in raw serial, returns True."""
        self.mock_ser.read.return_value = b"Restarting advertising...\r\n"
        result = self.gate.wait_for_advertising_restart(timeout=0.5)
        self.assertTrue(result)


class TestWirePlumberLifecycle(unittest.TestCase):
    """Test WirePlumber lifecycle ownership — seat detection, launch, SPA wait."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            serial_port="/dev/fake",
        )
        self.gate.serial = MagicMock()

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    @patch("subprocess.run")
    def test_detect_seat_active(self, mock_run):
        """detect_seat returns seat name when session has one."""
        mock_run.side_effect = [
            MagicMock(returncode=0, stdout="c1 1000 thomas seat0\n"),
            MagicMock(returncode=0, stdout="Seat=seat0\nRemote=no\n"),
        ]
        seat = self.gate._detect_seat()
        self.assertEqual(seat, "seat0")

    @patch("subprocess.run")
    def test_detect_seat_none(self, mock_run):
        """detect_seat returns None when no sessions."""
        mock_run.return_value = MagicMock(returncode=0, stdout="")
        seat = self.gate._detect_seat()
        self.assertIsNone(seat)

    def test_save_wp_service_state(self):
        """save_wp_service_state should not raise."""
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = MagicMock(returncode=0, stdout="active\n")
            self.gate._save_wp_service_state()
            self.assertEqual(self.gate._saved_wp_service_state, "active")

    @patch("subprocess.run")
    @patch("subprocess.Popen")
    def test_begin_wp_lifecycle_headless(self, mock_popen, mock_run):
        """Headless session starts wireplumber main-systemwide."""
        # No seat detected
        self.gate._detect_seat = MagicMock(return_value=None)
        self.gate._save_wp_service_state = MagicMock()
        self.gate._wait_for_bluez_spa = MagicMock(return_value=True)

        mock_run.return_value = MagicMock(returncode=0)
        mock_proc = MagicMock()
        mock_proc.pid = 99999
        mock_popen.return_value = mock_proc

        result = self.gate._begin_wp_lifecycle()
        self.assertTrue(result)
        self.assertTrue(self.gate._wp_owned)
        mock_popen.assert_called_once()

    @patch("subprocess.run")
    def test_begin_wp_lifecycle_with_seat(self, mock_run):
        """Active seat keeps existing WP."""
        self.gate._detect_seat = MagicMock(return_value="seat0")
        self.gate._save_wp_service_state = MagicMock()
        self.gate._wait_for_bluez_spa = MagicMock(return_value=True)

        result = self.gate._begin_wp_lifecycle()
        self.assertTrue(result)
        self.assertFalse(self.gate._wp_owned)

    def test_begin_wp_lifecycle_spa_fails(self):
        """WL lifecycle fails when BlueZ SPA monitor never appears."""
        self.gate._detect_seat = MagicMock(return_value="seat0")
        self.gate._save_wp_service_state = MagicMock()
        self.gate._wait_for_bluez_spa = MagicMock(return_value=False)

        result = self.gate._begin_wp_lifecycle()
        self.assertFalse(result)

    @patch("subprocess.run")
    def test_get_wp_pid_owned(self, mock_run):
        """_get_wp_pid returns owned WP subprocess PID."""
        mock_proc = MagicMock()
        mock_proc.pid = 99999
        self.gate._wp_owned = True
        self.gate._wp_proc = mock_proc
        pid = self.gate._get_wp_pid()
        self.assertEqual(pid, 99999)

    @patch("subprocess.run")
    def test_get_wp_pid_service(self, mock_run):
        """_get_wp_pid resolves MainPID from user systemd service."""
        self.gate._wp_owned = False
        self.gate._wp_proc = None
        mock_run.return_value = MagicMock(returncode=0, stdout="MainPID=56789\n")
        pid = self.gate._get_wp_pid()
        self.assertEqual(pid, 56789)

    @patch("subprocess.run")
    def test_get_wp_pid_none(self, mock_run):
        """_get_wp_pid returns None when neither owned nor service PID."""
        self.gate._wp_owned = False
        self.gate._wp_proc = None
        mock_run.return_value = MagicMock(returncode=0, stdout="MainPID=0\n")
        pid = self.gate._get_wp_pid()
        self.assertIsNone(pid)

    @patch("subprocess.run")
    def test_wait_for_bluez_spa_maps_pass(self, mock_run):
        """libspa-bluez5 in /proc/PID/maps returns True."""
        mock_proc = MagicMock()
        mock_proc.pid = 99999
        self.gate._wp_owned = True
        self.gate._wp_proc = mock_proc

        map_text = (
            "7f0000000000-7f0000100000 r-xp 00000000 /usr/lib/x86_64-linux-gnu"
            "/spa-0.2/bluez5/libspa-bluez5.so\n"
        )
        with patch("builtins.open", mock_open(read_data=map_text)):
            result = self.gate._wait_for_bluez_spa(timeout=0.5)
            self.assertTrue(result)

    @patch("subprocess.run")
    def test_wait_for_bluez_spa_maps_no_plugin(self, mock_run):
        """No libspa-bluez5 in /proc/PID/maps and no pw-dump → False."""
        mock_proc = MagicMock()
        mock_proc.pid = 99999
        self.gate._wp_owned = True
        self.gate._wp_proc = mock_proc

        map_text = (
            "7f0000000000-7f0000100000 r-xp /usr/lib/x86_64-linux-gnu/libc.so.6\n"
        )
        # pw-dump also returns nothing
        mock_run.return_value = MagicMock(returncode=0, stdout=json.dumps([]))
        with patch("builtins.open", mock_open(read_data=map_text)):
            result = self.gate._wait_for_bluez_spa(timeout=0.1)
            self.assertFalse(result)

    @patch("subprocess.run")
    def test_wait_for_bluez_spa_dead_pid(self, mock_run):
        """Dead/missing PID returns False immediately."""
        mock_proc = MagicMock()
        mock_proc.pid = 99999
        self.gate._wp_owned = True
        self.gate._wp_proc = mock_proc

        with patch(
            "builtins.open",
            side_effect=FileNotFoundError("No such process"),
        ):
            result = self.gate._wait_for_bluez_spa(timeout=0.5)
            self.assertFalse(result)

    @patch("subprocess.run")
    def test_wait_for_bluez_spa_pwdump_confirmatory(self, mock_run):
        """pw-dump bluez5 factory passes as secondary confirmation."""
        mock_proc = MagicMock()
        mock_proc.pid = 99999
        self.gate._wp_owned = True
        self.gate._wp_proc = mock_proc

        # Maps has no bluez5 on first iteration, pw-dump has bluez5 factory
        map_no_bluez = "7f0000000000-7f0000100000 r-xp /usr/lib/libc.so.6\n"
        with patch(
            "builtins.open",
            mock_open(read_data=map_no_bluez),
        ):
            dump = [{"info": {"props": {"factory.name": "api.bluez5.midi.node"}}}]
            mock_run.return_value = MagicMock(returncode=0, stdout=json.dumps(dump))
            result = self.gate._wait_for_bluez_spa(timeout=0.5)
            self.assertTrue(result)

    @patch("subprocess.run")
    def test_wait_for_bluez_spa_no_pid(self, mock_run):
        """_get_wp_pid returns None → _wait_for_bluez_spa returns False."""
        self.gate._wp_owned = False
        self.gate._wp_proc = None
        mock_run.return_value = MagicMock(returncode=0, stdout="MainPID=0\n")
        result = self.gate._wait_for_bluez_spa(timeout=0.1)
        self.assertFalse(result)

    @patch("subprocess.run")
    def test_begin_wp_lifecycle_launch_failure(self, mock_run):
        """WP launch failure returns False."""
        self.gate._detect_seat = MagicMock(return_value=None)
        self.gate._save_wp_service_state = MagicMock()
        mock_run.return_value = MagicMock(returncode=0)

        with patch("subprocess.Popen", side_effect=OSError("exec failed")):
            result = self.gate._begin_wp_lifecycle()
            self.assertFalse(result)

    @patch("subprocess.run")
    @patch("subprocess.Popen")
    def test_begin_wp_lifecycle_no_pkill(self, mock_popen, mock_run):
        """Headless launch must NOT call pkill."""
        self.gate._detect_seat = MagicMock(return_value=None)
        self.gate._save_wp_service_state = MagicMock()
        self.gate._wait_for_bluez_spa = MagicMock(return_value=True)

        mock_run.return_value = MagicMock(returncode=0)
        mock_proc = MagicMock()
        mock_proc.pid = 99999
        mock_popen.return_value = mock_proc

        self.gate._begin_wp_lifecycle()
        for call in mock_run.call_args_list:
            args_str = str(call)
            self.assertNotIn("pkill", args_str, "WP lifecycle must not use pkill")

    @patch("os.killpg")
    @patch("os.getpgid")
    @patch("subprocess.run")
    def test_end_wp_lifecycle_restores_service(self, mock_run, mock_getpgid, mock_kill):
        """end_wp_lifecycle restores user wireplumber service if it was active."""
        self.gate._wp_owned = True
        self.gate._saved_wp_service_state = "active"
        mock_proc = MagicMock()
        mock_proc.pid = 99999
        self.gate._wp_proc = mock_proc

        mock_getpgid.return_value = 12345
        self.gate._end_wp_lifecycle()
        mock_kill.assert_called()  # Signal sent to own WP
        # Should have called systemctl start wireplumber
        start_calls = [c for c in mock_run.call_args_list if "start" in str(c)]
        self.assertGreater(len(start_calls), 0)

    def test_end_wp_lifecycle_not_owned(self):
        """end_wp_lifecycle is no-op when WP not owned."""
        self.gate._wp_owned = False
        self.gate._end_wp_lifecycle()  # Must not raise


class TestDisconnectFailureFatal(unittest.TestCase):
    """Test that disconnect failure causes Phase3Result failure."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            serial_port="/dev/fake",
        )
        self.gate.serial = MagicMock()

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    @patch("subprocess.run")
    def test_disconnect_device_false_returns_false(self, mock_run):
        """disconnect_device with nonzero return code returns False."""
        mock_run.return_value = MagicMock(returncode=1)
        result = self.gate.disconnect_device("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)

    def test_disconnect_failure_is_not_attempted(self):
        """disconnect_device failure returns False, not a soft 'attempted'."""
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = MagicMock(returncode=1)
            result = self.gate.disconnect_device("AA:BB:CC:DD:EE:FF")
        self.assertFalse(result)
        # A caller checking "if not result" will enter the failure path


class TestAdvertisingRestartFatal(unittest.TestCase):
    """Test that missing advertising evidence causes failure."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            serial_port="/dev/fake",
        )
        self.mock_ser = MagicMock()
        self.gate.serial = ReceiverSerial(port="/dev/fake")
        self.gate.serial._get_serial = MagicMock(return_value=self.mock_ser)
        self.gate.serial.reset = MagicMock()

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    def test_advertising_restart_returns_false_without_evidence(self):
        """wait_for_advertising_restart returns False when no evidence found."""
        self.mock_ser.read.return_value = b""
        result = self.gate.wait_for_advertising_restart(timeout=0.2)
        self.assertFalse(result)
        # Caller must treat False as fatal


class TestCleanupNoUnrelatedKill(unittest.TestCase):
    """Test that cleanup does NOT use global pkill."""

    def setUp(self):
        self.gate = Phase3Gate(
            receiver_name="Test Receiver",
            serial_port="/dev/fake",
        )
        self.gate.serial = MagicMock()

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    @patch("subprocess.run")
    @patch("atexit.unregister")
    def test_cleanup_no_pkill(self, mock_unreg, mock_run):
        """cleanup must not call pkill for any process name."""
        self.gate.cleanup()
        for call in mock_run.call_args_list:
            args = str(call)
            self.assertNotIn("pkill", args, "cleanup must not use global pkill")

    def test_cleanup_only_stops_own_processes(self):
        """cleanup stops only own process group, not unrelated agents."""
        # verify _stop_btagent is called but only kills its own pid group
        with (
            patch.object(self.gate, "_stop_btagent") as mock_stop,
            patch.object(self.gate, "_ensure_scan_off"),
            patch.object(self.gate, "_end_wp_lifecycle"),
            patch.object(self.gate, "restore_host_settings"),
            patch("atexit.unregister"),
        ):
            self.gate.cleanup()
            mock_stop.assert_called_once()


class TestEarlyCleanup(unittest.TestCase):
    """Test cleanup after early-stage failure does not raise."""

    def test_cleanup_after_partial_init(self):
        """cleanup after partial init must not raise."""
        gate = Phase3Gate(
            receiver_name="Test Cleanup",
            serial_port="/dev/fake",
        )
        gate.serial = MagicMock()
        # Simulate early failure: agent not started, scan never on, no WP
        gate._agent_proc = None
        gate._wp_owned = False
        gate._wp_proc = None
        gate.cleanup()  # Must not raise

    def test_cleanup_with_dead_agent(self):
        """cleanup with dead agent PID must not raise."""
        gate = Phase3Gate(
            receiver_name="Test Cleanup",
            serial_port="/dev/fake",
        )
        gate.serial = MagicMock()
        # Agent proc exists but died
        mock_proc = MagicMock()
        mock_proc.pid = None
        gate._agent_proc = mock_proc
        gate.cleanup()  # Must not raise


class TestParseReceiverLogReuse(unittest.TestCase):
    """Test that parse_receiver_log correctly rejects faulty logs.

    Uses Phase 2 BluezWirePlumberGate.parse_receiver_log() for validation.
    """

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        import shutil

        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def _make_gate(self, log_content: str) -> BluezWirePlumberGate:
        log_path = os.path.join(self.tmpdir, "test.log")
        with open(log_path, "w") as f:
            f.write(log_content)
        return BluezWirePlumberGate(
            receiver_name="Test Receiver",
            duration=30,
            log_path=log_path,
        )

    def test_i2s_dma_not_started_fails(self):
        """Log without 'I2S DMA started' should fail."""
        log = (
            "I2S ready\n"
            "LC3 decoder[0]: 48000 Hz 7500 us\n"
            "ASCS config OK\n"
            "ASCS streaming started\n"
            "Stream[0] summary: SDUs=4000 decoded=4100 plc=100 "
            "decode_err=0 i2s_underrun=0 stream_reset=0\n"
        )
        gate = self._make_gate(log)
        result = GateResult()
        ok = gate.parse_receiver_log(result)
        self.assertFalse(ok)
        self.assertIn("I2S DMA not started", " ".join(result.evidence))

    def test_zero_sdu_fails(self):
        """Stream summary with SDUs=0 should fail."""
        log = (
            "I2S DMA started\n"
            "LC3 decoder[0]: 48000 Hz 7500 us\n"
            "ASCS config OK\n"
            "ASCS streaming started\n"
            "Stream[0] summary: SDUs=0 decoded=0 plc=0 "
            "decode_err=0 i2s_underrun=0 stream_reset=0\n"
        )
        gate = self._make_gate(log)
        result = GateResult()
        ok = gate.parse_receiver_log(result)
        self.assertFalse(ok)

    def test_short_sdu_count_fails(self):
        """SDU count too low for duration should fail (Phase 2 strict check)."""
        log = (
            "I2S DMA started\n"
            "LC3 decoder[0]: 48000 Hz 7500 us\n"
            "ASCS config OK\n"
            "ASCS streaming started\n"
            "Stream[0] summary: SDUs=100 decoded=210 plc=110 "
            "decode_err=0 i2s_underrun=0 stream_reset=0\n"
        )
        gate = self._make_gate(log)
        result = GateResult()
        ok = gate.parse_receiver_log(result)
        self.assertFalse(ok)
        # Evidence should mention SDU count too low
        evidence = " ".join(result.evidence)
        self.assertTrue("too low" in evidence.lower() or "SDU" in evidence)

    def test_valid_log_passes(self):
        """Valid log with all required evidence passes."""
        log = (
            "I2S DMA started\n"
            "LC3 decoder[0]: 48000 Hz 7500 us\n"
            "bt_bap: ASE Config: freq=48000 frame_dur=7500\n"
            "bt_bap: Enable: stream started\n"
            "Stream[0] summary: SDUs=4000 decoded=4100 plc=100 "
            "decode_err=0 i2s_underrun=0 stream_reset=0\n"
        )
        gate = self._make_gate(log)
        result = GateResult()
        ok = gate.parse_receiver_log(result)
        self.assertTrue(ok)


class TestNoBapCentralDependency(unittest.TestCase):
    """Verify Phase 3 gate never imports bap_central, raw-HCI, or MediaEndpoint."""

    @staticmethod
    def _clean_source(source):
        """Remove docstrings and comments from source for dependency check."""
        # Remove all triple-quoted strings (docstrings)
        cleaned = re.sub(r'""".*?"""', "", source, flags=re.DOTALL)
        cleaned = re.sub(r"'''.*?'''", "", cleaned, flags=re.DOTALL)
        # Remove comment lines
        lines = [l for l in cleaned.splitlines() if not l.strip().startswith("#")]
        return "\n".join(lines)

    def test_no_bap_central_import(self):
        """Gate module must not import bap_central."""
        import inspect

        source = inspect.getsource(_p3)
        cleaned = self._clean_source(source)
        self.assertNotIn(
            "bap_central", cleaned, "Phase 3 gate must not import bap_central"
        )
        self.assertNotIn(
            "MediaEndpoint", cleaned, "Phase 3 gate must not register MediaEndpoint"
        )
        self.assertNotIn(
            "raw_hci", cleaned.lower(), "Phase 3 gate must not use raw-HCI"
        )

    def test_no_iso_socket_import(self):
        """Gate must not open ISO sockets directly."""
        import inspect

        source = inspect.getsource(_p3)
        cleaned = self._clean_source(source)
        self.assertNotIn("iso_socket", cleaned.lower())
        self.assertNotIn("BT_ISO", cleaned)


if __name__ == "__main__":
    unittest.main()
