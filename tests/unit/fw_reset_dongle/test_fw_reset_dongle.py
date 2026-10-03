#!/usr/bin/env python3
"""Public source-only reset boundary tests."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "fw_flash_dongle"))
import test_fw_flash_dongle as flash_tests  # noqa: E402


class Reset(unittest.TestCase):
    setUp = flash_tests.HardwareFixture.setUp
    discovery = flash_tests.HardwareFixture.discovery
    command = flash_tests.HardwareFixture.command
    run_action = flash_tests.HardwareFixture.run_action
    result = flash_tests.HardwareFixture.result
    assert_no_target = flash_tests.HardwareFixture.assert_no_target

    def test_reset_source_only(self):
        self.run_action("reset")
        result = self.result()
        self.assertEqual(result["status"], "completed")
        argv, env = self.calls[-1]
        self.assertIn("adapter serial SRC456", argv)
        self.assertEqual(argv[-4:], ["-c", "reset run", "-c", "shutdown"])
        self.assertFalse(any("nrf54l-load" in arg for arg in argv))
        self.assertEqual(env["OPENOCD_INTERFACE"], "cmsis-dap")

    def test_no_inputs_and_unavailable_device(self):
        import subprocess

        proc = subprocess.run(
            [sys.executable, str(flash_tests.REPO / "scripts/hci_dongle.py"), "reset"],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(proc.returncode, 0)
        self.assert_no_target()
        self.props.pop("ttyACM1")
        with self.assertRaises(Exception):
            self.run_action("reset")
        self.assertIn("source-udev", self.result()["raw_identity"])
        self.assert_no_target()

    def test_reset_diagnostic_and_exit_fail(self):
        self.openocd_text = "Warn : something wrong\n"
        with self.assertRaises(Exception):
            self.run_action("reset")
        self.assertEqual(self.result()["status"], "error")
        self.assertEqual(self.result()["commands"][-1]["returncode"], 0)

    def test_held_source_tty(self):
        self.lsof_rc = 0
        with self.assertRaises(Exception):
            self.run_action("reset")
        self.assert_no_target()


if __name__ == "__main__":
    unittest.main(verbosity=2)
