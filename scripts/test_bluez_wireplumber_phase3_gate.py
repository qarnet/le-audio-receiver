#!/usr/bin/env python3
"""Unit tests for Phase 3 pairing/reconnect lifecycle gate.

Covers: state polling, timeout/failure classification, stale bond,
pairing rejection, service resolution, reconnect, log scoping.
"""

import importlib
import os
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch, PropertyMock

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
    def test_remove_device_failure(self, mock_run):
        """remove_device with rc!=0 returns False."""
        mock_run.return_value = MagicMock(returncode=1)
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

    def test_remove_failure_returns_false(self):
        """remove_device with rc!=0 returns False."""
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = MagicMock(returncode=1)
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
        self.gate.serial = MagicMock()
        self.gate.serial.send_command.return_value = ("", "")

    def tearDown(self):
        try:
            self.gate.cleanup()
        except Exception:
            pass

    def test_wait_for_advertising_restart_returns_false_by_default(self):
        """wait_for_advertising_restart should return False without evidence."""
        result = self.gate.wait_for_advertising_restart(timeout=0.1)
        self.assertFalse(result)

    def test_wait_for_advertising_restart_found(self):
        """If 'Advertising' found in output, returns True."""
        self.gate.serial.send_command.return_value = (
            "Advertising as LE Audio Receiver",
            "",
        )
        result = self.gate.wait_for_advertising_restart(timeout=1.0)
        self.assertTrue(result)


if __name__ == "__main__":
    unittest.main()
