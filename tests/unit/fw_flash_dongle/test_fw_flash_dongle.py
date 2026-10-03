#!/usr/bin/env python3
"""Source-only dongle CLI tests with real session validation and synthetic sysfs."""

import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

REPO = pathlib.Path(__file__).resolve().parents[3]
sys.path[:0] = [str(REPO / "scripts"), str(REPO / "tests" / "hil")]
import hci_dongle  # noqa: E402
import hil_fakes  # noqa: E402
from hil import session, lifecycle  # noqa: E402


class HardwareFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.sysfs = hil_fakes.build_fake_sysfs(str(self.root))
        fixture = json.loads((REPO / "tests/hil/fixture-xiao-source.json").read_text())
        binding = json.loads(
            (REPO / "tests/hil/fixture-xiao-source.local.example.json").read_text()
        )
        self.fixture = self.root / "fixture.json"
        self.binding = self.root / "binding.json"
        self.fixture.write_text(json.dumps(fixture))
        self.binding.write_text(json.dumps(binding))
        self.out = self.root / "out"
        self.out.mkdir()
        self.sessions = self.root / "sessions"
        self.sessions.mkdir()
        self.sdk = self.root / "sdk"
        cfg = self.sdk / hci_dongle.BOARD_CFG
        cfg.parent.mkdir(parents=True)
        cfg.write_text("# fake config\n")
        self.repo = self.root / "repo"
        image_dir = self.repo / "build/dongle/zephyr"
        image_dir.mkdir(parents=True)
        (image_dir / ".config").write_text(
            "CONFIG_SOC_NRF54L15_CPUAPP=y\nCONFIG_BT_HCI_RAW=y\n"
        )
        self.image = image_dir / "zephyr.hex"
        self.image.write_text(":0400000001020304F2\n:00000001FF\n")
        self.calls = []
        self.probe_rows = hil_fakes.default_probe_table(
            rows=[
                (s, "DAPLink", "nRF54L15", "0x6ba02477", "0x00054b15", "BAAA", "")
                for s in ("RECV123", "SRC456")
            ]
        )
        # Both probe fingerprints use same valid variant as table.
        self.props = {}
        for role, tty, usb, serial in (
            ("receiver", "ttyACM0", "1-2", "RECV123"),
            ("source", "ttyACM1", "1-3", "SRC456"),
        ):
            self.props[tty] = {
                "ID_BUS": "usb",
                "ID_VENDOR_ID": "2886",
                "ID_MODEL_ID": "0066",
                "ID_SERIAL_SHORT": serial,
                "ID_USB_INTERFACE_NUM": "02",
                "ID_USB_DRIVER": "cdc_acm",
                "ID_PATH": "pci-%s" % usb,
                "DEVPATH": hil_fakes.tty_devpath(usb, tty),
            }
        self.manifest = session.create_session(
            str(self.fixture),
            str(self.binding),
            "initial",
            "RECV123",
            "SRC456",
            run_cmd=self.discovery,
            session_root=str(self.sessions),
            sysfs_root=self.sysfs,
        )
        self.args = argparse.Namespace(
            session_manifest=self.manifest.path,
            fixture=str(self.fixture),
            binding=str(self.binding),
            output_root=str(self.out),
            run_id="run1",
            image=None,
            sha256=None,
        )
        self.env = patch.dict(
            os.environ,
            {"ZEPHYR_BASE": str(self.sdk), "FW_DONGLE_JLINK_SERIAL": "FOREIGN"},
        )
        self.env.start()
        self.addCleanup(self.env.stop)
        self.lsof_rc = 1
        self.openocd_rc = 0
        self.openocd_text = "Info : verified\n"
        self.timeout = False

    def discovery(self, argv, timeout):
        if argv[:2] == ["nix-nrf", "probes"]:
            return subprocess.CompletedProcess(argv, 0, self.probe_rows, "")
        if argv[0] == "openocd":
            return subprocess.CompletedProcess(
                argv,
                0,
                hil_fakes.cmsis_dap_fingerprint_output(variant="0x42414141"),
                "",
            )
        if argv[0] == "udevadm":
            props = self.props.get(pathlib.Path(argv[-1]).name, {})
            return subprocess.CompletedProcess(
                argv, 0, "".join("%s=%s\n" % pair for pair in props.items()), ""
            )
        raise AssertionError(argv)

    def command(self, argv, *, capture_output, text, timeout, env=None):
        self.calls.append((argv, env))
        if argv[0] in ("nix-nrf", "udevadm") or (
            argv[0] == "openocd" and "fwc_scan" in argv
        ):
            return self.discovery(argv, timeout)
        if argv[0] == "sudo":
            return subprocess.CompletedProcess(
                argv, self.lsof_rc, "held" if self.lsof_rc == 0 else "", ""
            )
        if self.timeout:
            raise subprocess.TimeoutExpired(argv, timeout, output=b"partial")
        return subprocess.CompletedProcess(argv, self.openocd_rc, self.openocd_text, "")

    def run_action(self, action="flash"):
        with patch.object(hci_dongle.subprocess, "run", side_effect=self.command):
            return hci_dongle.execute(
                action, self.args, sysfs_root=self.sysfs, repo_root=str(self.repo)
            )

    def result(self):
        return json.loads((self.out / self.args.run_id / "result.json").read_text())

    def assert_no_target(self):
        self.assertFalse(
            any(c[0][0] == "openocd" and "fwc_scan" not in c[0] for c in self.calls)
        )

    def test_flash_source_only_snapshot(self):
        self.run_action()
        result = self.result()
        self.assertEqual(result["status"], "completed")
        self.assertEqual(result["source_identity"]["probe_serial"], "SRC456")
        self.assertEqual(
            result["image"]["sha256"],
            hashlib.sha256(self.image.read_bytes()).hexdigest(),
        )
        self.assertEqual(result["image"]["segments"], [[0, 4]])
        flash, env = self.calls[-1]
        self.assertEqual(env["OPENOCD_INTERFACE"], "cmsis-dap")
        self.assertIn("adapter serial SRC456", flash)
        self.assertNotIn("adapter serial RECV123", flash)
        self.assertIn("nrf54l-load {%s}" % result["image"]["snapshot"], flash)
        self.assertIn("verify_image {%s}" % result["image"]["snapshot"], flash)
        self.assertEqual(
            (self.out / "run1/image.hex").read_bytes(), self.image.read_bytes()
        )
        self.assertFalse(
            (
                self.out
                / ".locks"
                / hashlib.sha256(b"local-xiao-nrf54l15-pair").hexdigest()
            ).exists()
        )

    def test_missing_unsafe_and_mismatched_image(self):
        for i in range(3):
            if i == 0:
                self.image.unlink()
            elif i == 1:
                self.image.write_text(":0200000400FFFB\n:01000000AA55\n:00000001FF\n")
            else:
                self.image.write_text(":0400000001020304F2\n:00000001FF\n")
                self.args.sha256 = "0" * 64
            self.args.run_id = "bad%d" % i
            with self.assertRaises(Exception):
                self.run_action()
            self.assertEqual(self.result()["status"], "error")
            self.assert_no_target()

    def test_mutated_manifest_and_lock_conflict(self):
        with lifecycle.CleanupStack() as cleanup:
            lifecycle.FixtureLock.acquire(
                str(self.out), self.manifest.fixture_id, cleanup
            )
            with self.assertRaises(lifecycle.FixtureBusy):
                self.run_action()
        os.chmod(self.manifest.path, 0o600)
        with open(self.manifest.path, "a") as fh:
            fh.write("\n")
        with self.assertRaises(session.HilSessionError):
            self.run_action()
        self.assert_no_target()

    def test_wrong_identity_and_ambiguous_tty(self):
        self.probe_rows = self.probe_rows.replace("nRF54L15", "nRF5340")
        with self.assertRaises(Exception):
            self.run_action()
        self.assertIn("nrf-probes", self.result()["raw_identity"])
        self.assert_no_target()
        self.probe_rows = self.probe_rows.replace("nRF5340", "nRF54L15")
        self.args.run_id = "ambiguous"
        pathlib.Path(self.sysfs, "class/tty/ttyACM9").mkdir()
        self.props["ttyACM9"] = dict(self.props["ttyACM1"])
        with self.assertRaises(Exception):
            self.run_action()
        self.assert_no_target()

    def test_held_tty_lsof_error_and_failed_flash(self):
        for i, rc in enumerate((0, 2)):
            self.lsof_rc = rc
            self.args.run_id = "held%d" % i
            with self.assertRaises(hci_dongle.DongleError):
                self.run_action()
            self.assert_no_target()
            self.assertEqual(self.result()["status"], "error")
        self.lsof_rc = 1
        self.args.run_id = "flashfail"
        self.openocd_rc = 1
        with self.assertRaises(hci_dongle.DongleError):
            self.run_action()
        self.assertEqual(self.result()["status"], "error")
        self.assertIn("recovery_error", self.result())

    def test_custom_image_needs_hash_and_timeout(self):
        self.args.image = str(self.image)
        with self.assertRaises(hci_dongle.DongleError):
            self.run_action()
        self.assert_no_target()
        self.args.sha256 = hashlib.sha256(self.image.read_bytes()).hexdigest()
        self.timeout = True
        with self.assertRaises(hci_dongle.DongleError):
            self.run_action()
        self.assertEqual(self.result()["status"], "error")
        self.assertTrue(
            any(c.get("error") == "timeout" for c in self.result()["commands"])
        )

    def test_wrapper_requires_dev_shell(self):
        env = dict(os.environ)
        env.pop("ZEPHYR_BASE", None)
        proc = subprocess.run(
            [str(REPO / "scripts/bin/fw-flash-dongle")],
            env=env,
            capture_output=True,
            text=True,
            timeout=15,
        )
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("firmware tool error", proc.stderr)
        self.assert_no_target()

    def test_cancelled_flash_revalidates_and_resets(self):
        def interrupt_flash(argv, *, capture_output, text, timeout, env=None):
            if argv[0] == "openocd" and any("nrf54l-load" in part for part in argv):
                self.calls.append((argv, env))
                raise hci_dongle.Cancelled("controlled interruption")
            return self.command(
                argv, capture_output=capture_output, text=text, timeout=timeout, env=env
            )

        with patch.object(hci_dongle.subprocess, "run", side_effect=interrupt_flash):
            with self.assertRaises(hci_dongle.Cancelled):
                hci_dongle.execute(
                    "flash", self.args, sysfs_root=self.sysfs, repo_root=str(self.repo)
                )
        result = self.result()
        self.assertEqual(result["status"], "cancelled")
        self.assertEqual(result["error"], "controlled interruption")
        self.assertNotIn("recovery_error", result)
        self.assertEqual(
            sum(c["argv"][:2] == ["nix-nrf", "probes"] for c in result["commands"]), 2
        )
        self.assertEqual(
            result["commands"][-1]["argv"][-4:], ["-c", "reset run", "-c", "shutdown"]
        )
        self.assertEqual(result["commands"][-1]["returncode"], 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
