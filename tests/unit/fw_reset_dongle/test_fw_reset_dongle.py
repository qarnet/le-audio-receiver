#!/usr/bin/env python3
"""Unit tests for fw-reset-dongle J-Link probe selection, run through public
script execution with fake openocd / nrf-probes and a temp repo.  The
receiver CMSIS-DAP selectors (scripts/probe-serial.local, nrf-probes
--find nrf53) must never be consulted; the exact J-Link selector/reset argv
must be used.  No hardware needed.

Run directly:

    python3 tests/unit/fw_flash_dongle/test_fw_reset_dongle.py
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
_SCRIPTS_BIN = os.path.join(_REPO_ROOT, "scripts", "bin")


def _write_fake(path, body):
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(body)
    os.chmod(path, 0o755)
    return path


class ResetDongleHarness:
    def __init__(self, tmpdir):
        self.tmpdir = tmpdir
        self.repo = os.path.join(tmpdir, "repo")
        bin_dir = os.path.join(self.repo, "scripts", "bin")
        os.makedirs(bin_dir)
        shutil.copy(
            os.path.join(_SCRIPTS_BIN, "fw-reset-dongle"),
            os.path.join(bin_dir, "fw-reset-dongle"),
        )
        shutil.copy(
            os.path.join(_SCRIPTS_BIN, "fw-common.sh"),
            os.path.join(bin_dir, "fw-common.sh"),
        )

        self.fakebin = os.path.join(tmpdir, "fakebin")
        os.makedirs(self.fakebin)
        _write_fake(
            os.path.join(self.fakebin, "west"),
            "#!/usr/bin/env bash\nexit 0\n",
        )
        _write_fake(
            os.path.join(self.fakebin, "openocd"),
            "#!/usr/bin/env bash\n"
            'printf \'%s\\0\' "$@" > "${OPENOCD_ARGV_FILE:?}"\n'
            "exit 0\n",
        )
        self.nrf_called = os.path.join(tmpdir, "nrf-probes.called")
        _write_fake(
            os.path.join(self.fakebin, "nrf-probes"),
            "#!/usr/bin/env bash\n"
            'touch "${NRF_PROBES_CALLED_FILE:?}"\n'
            'echo "nrf-probes must not be invoked" >&2\n'
            "exit 99\n",
        )

        self.zephyr_base = os.path.join(tmpdir, "zephyrbase")
        os.makedirs(self.zephyr_base)

        self.argv_file = os.path.join(tmpdir, "openocd.argv")
        self.env = dict(os.environ)
        self.env["PATH"] = self.fakebin + os.pathsep + self.env["PATH"]
        self.env["ZEPHYR_BASE"] = self.zephyr_base
        self.env["OPENOCD_ARGV_FILE"] = self.argv_file
        self.env["NRF_PROBES_CALLED_FILE"] = self.nrf_called
        self.env.pop("FW_DONGLE_JLINK_SERIAL", None)

        self.script = os.path.join(bin_dir, "fw-reset-dongle")

    def with_probe_serial_local(self, serial="RECEIVER-PROBE-42"):
        """Simulate a stale receiver probe-serial.local that the reset
        helper must ignore."""
        with open(os.path.join(self.repo, "scripts", "probe-serial.local"), "w") as fh:
            fh.write(serial + "\n")
        return self

    def run(self, serial=None):
        env = dict(self.env)
        if serial is not None:
            env["FW_DONGLE_JLINK_SERIAL"] = serial
        return subprocess.run(
            [self.script], env=env, capture_output=True, text=True, timeout=30
        )


def _argv(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as fh:
        return [a.decode("utf-8") for a in fh.read().split(b"\0") if a]


class FwResetDongle(unittest.TestCase):
    def test_default_jlink_autodetect_ignores_receiver_selectors(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = ResetDongleHarness(tmp).with_probe_serial_local()
            r = h.run()
            self.assertEqual(0, r.returncode, r.stderr)
            self.assertIn("J-Link auto-detection", r.stdout)
            self.assertFalse(
                os.path.exists(h.nrf_called), "nrf-probes must not be invoked"
            )
            self.assertTrue(os.path.exists(h.argv_file), "openocd never invoked")
            args = _argv(h.argv_file) or []
            self.assertIn("interface/jlink.cfg", args)
            self.assertIn("target/nordic/nrf53.cfg", args)
            self.assertIn("transport select swd", args)
            self.assertIn("adapter speed 2000", args)
            self.assertIn("reset run", args)
            self.assertIn("shutdown", args)
            self.assertNotIn("adapter serial RECEIVER-PROBE-42", args)
            self.assertFalse(
                any(a.startswith("adapter serial") for a in args),
                "no serial may be passed in default auto-detect mode",
            )

    def test_explicit_jlink_serial_override(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = ResetDongleHarness(tmp).with_probe_serial_local()
            r = h.run(serial="ABC-123.45")
            self.assertEqual(0, r.returncode, r.stderr)
            self.assertIn("J-Link override, serial ABC-123.45", r.stdout)
            self.assertFalse(
                os.path.exists(h.nrf_called), "nrf-probes must not be invoked"
            )
            args = _argv(h.argv_file) or []
            serial_cmds = [a for a in args if a.startswith("adapter serial")]
            self.assertEqual(serial_cmds, ["adapter serial ABC-123.45"])
            self.assertLess(
                args.index("interface/jlink.cfg"),
                args.index("adapter serial ABC-123.45"),
            )
            self.assertLess(
                args.index("adapter serial ABC-123.45"),
                args.index("target/nordic/nrf53.cfg"),
            )
            self.assertNotIn("adapter serial RECEIVER-PROBE-42", args)

    def test_invalid_serial_rejected_before_openocd(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = ResetDongleHarness(tmp).with_probe_serial_local()
            r = h.run(serial="bad!serial")
            self.assertNotEqual(0, r.returncode)
            self.assertIn("Invalid FW_DONGLE_JLINK_SERIAL", r.stderr)
            self.assertIn("^[[:alnum:]_.:-]+$", r.stderr)
            self.assertFalse(os.path.exists(h.argv_file), "openocd must not be invoked")
            self.assertFalse(
                os.path.exists(h.nrf_called), "nrf-probes must not be invoked"
            )

    def test_missing_dev_shell_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = ResetDongleHarness(tmp)
            h.env.pop("ZEPHYR_BASE", None)
            r = h.run()
            self.assertNotEqual(0, r.returncode)
            self.assertIn("firmware tool error", r.stderr)
            self.assertFalse(os.path.exists(h.argv_file))
            self.assertFalse(os.path.exists(h.nrf_called))


if __name__ == "__main__":
    unittest.main(verbosity=2)
