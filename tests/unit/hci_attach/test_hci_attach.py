#!/usr/bin/env python3
"""H4 PTY and real process-group attach lifecycle, with real session validation."""

import importlib
import json
import os
import pathlib
import pty
import signal
import subprocess
import sys
import threading
import time
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / "scripts"), str(ROOT / "tests/unit/fw_flash_dongle")]
import hci_dongle  # noqa: E402

flash = importlib.import_module("test_fw_flash_dongle")


class Uart:
    def __init__(self, slave, **kwargs):
        import serial

        self.inner = serial.Serial(**kwargs)
        self.slave = slave

    def __setattr__(self, key, value):
        if key in ("inner", "slave"):
            object.__setattr__(self, key, value)
        elif key == "port":
            self.inner.port = self.slave
        else:
            setattr(self.inner, key, value)

    def __getattr__(self, key):
        return getattr(self.inner, key)


class Attach(unittest.TestCase):
    setUp = flash.HardwareFixture.setUp
    discovery = flash.HardwareFixture.discovery

    def test_cli_requires_child_and_valid_timeout(self):
        common = [
            sys.executable,
            str(ROOT / "scripts/hci_dongle.py"),
            "attach",
            "--session-manifest",
            "/tmp/missing",
            "--fixture",
            "/tmp/missing",
            "--binding",
            "/tmp/missing",
            "--output-root",
            "/tmp/opencode",
            "--run-id",
            "dry",
        ]
        no_child = subprocess.run(common, capture_output=True, text=True)
        self.assertNotEqual(no_child.returncode, 0)
        self.assertIn("attach requires -- COMMAND", no_child.stderr)
        parsed = subprocess.run(
            common + ["--", "python3", "-c", "pass"], capture_output=True, text=True
        )
        self.assertNotEqual(parsed.returncode, 0)
        self.assertIn("session manifest basename", parsed.stderr)
        self.assertNotIn("arguments are required", parsed.stderr)
        self.args.timeout = float("nan")
        self.args.command = ["true"]
        with self.assertRaisesRegex(hci_dongle.DongleError, "finite"):
            hci_dongle.execute("attach", self.args)

    def prepare(self, mode="good"):
        self.mode = mode
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        path = os.ttyname(slave)
        self.args.command = [
            sys.executable,
            "-c",
            "import os; print(os.environ['HCI_ADAPTER'])",
            "@HCI@",
        ]
        self.args.timeout = 2
        self.fakebin = self.root / "fakebin"
        self.fakebin.mkdir()
        bt = self.fakebin / "btattach"
        bt.write_text(
            "#!/usr/bin/env python3\n"
            "import os,signal,time,pathlib\n"
            "node=pathlib.Path(os.environ['FAKE_SYSFS'])/'class/bluetooth/hci7'\n"
            "if not os.environ.get('FAKE_NO_ADAPTER'): node.mkdir()\n"
            "extra=pathlib.Path(os.environ['FAKE_SYSFS'])/'class/bluetooth/hci8'\n"
            "if os.environ.get('FAKE_AMBIGUOUS'): extra.mkdir()\n"
            "def stop(sig,frame):\n node.rmdir() if node.exists() else None; extra.rmdir() if extra.exists() else None; raise SystemExit(0)\n"
            "signal.signal(signal.SIGTERM,stop)\n"
            "while True: time.sleep(.1)\n"
        )
        bt.chmod(0o755)
        sudo = self.fakebin / "sudo"
        sudo.write_text(
            "#!/usr/bin/env python3\n"
            "import os,sys,signal\n"
            "a=sys.argv[2:]\n"
            "if a[0]=='kill':\n"
            " if os.environ.get('FAKE_REFUSE_KILL'): sys.exit(1)\n"
            " s={'-TERM':signal.SIGTERM,'-KILL':signal.SIGKILL,'-0':0}[a[1]]\n"
            " try: os.killpg(-int(a[-1]),s)\n"
            " except ProcessLookupError: sys.exit(1)\n"
            " sys.exit(0)\n"
            "os.execvp(a[0],a)\n"
        )
        sudo.chmod(0o755)
        mgmt = self.fakebin / "btmgmt"
        mgmt.write_text(
            "#!/usr/bin/env python3\n"
            "import sys,os,pathlib\n"
            "a=sys.argv[1:]; state=pathlib.Path(os.environ['FAKE_STATE'])\n"
            "if a[1]!='hci7': sys.exit(10)\n"
            "if os.environ.get('FAKE_BAD_INFO') and a[2]=='info': sys.exit(9)\n"
            "if a[2]=='info':\n"
            " print('addr C0:AA:BB:CC:DD:EE')\n"
            " print('supported settings: powered le secure-conn cis-central')\n"
            " print('current settings: powered le secure-conn cis-central' if state.exists() else 'current settings: le secure-conn cis-central')\n"
            "elif a[2]=='power':\n"
            " if a[3]=='on': state.touch()\n"
            " else: state.unlink(missing_ok=True)\n"
        )
        mgmt.chmod(0o755)
        self.addCleanup(self.stop_emulator)
        self.stop = threading.Event()

        def emulate():
            while not self.stop.is_set():
                try:
                    command = os.read(master, 4)
                    if not command:
                        continue
                    while len(command) < 4:
                        command += os.read(master, 4 - len(command))
                    if self.mode == "truncated":
                        response = bytes.fromhex("040e")
                    elif self.mode == "wrong":
                        response = bytes.fromhex("040e0401030c01")
                    elif command == bytes.fromhex("01030c00"):
                        response = bytes.fromhex("040e0401030c00")
                    elif self.mode == "wrongaddr":
                        response = bytes.fromhex("040e0a01091000eeddccbbaa00")
                    elif self.mode == "extra":
                        response = bytes.fromhex("040e0a01091000eeddccbbaac0ff")
                    else:
                        response = bytes.fromhex("040e0a01091000eeddccbbaac0")
                    os.write(master, response[:2])
                    time.sleep(0.01)
                    os.write(master, response[2:]) if len(response) > 2 else None
                except OSError:
                    return

        self.thread = threading.Thread(target=emulate, daemon=True)
        self.thread.start()
        self.addCleanup(self.thread.join, 1)
        self.slave = path
        self.old_env = patch.dict(
            os.environ,
            {
                "PATH": str(self.fakebin) + os.pathsep + os.environ["PATH"],
                "FAKE_SYSFS": self.sysfs,
                "FAKE_STATE": str(self.root / "power"),
            },
        )
        self.old_env.start()
        self.addCleanup(self.old_env.stop)
        bluetooth = pathlib.Path(self.sysfs, "class/bluetooth")
        bluetooth.mkdir()
        (bluetooth / "hci2").mkdir()
        self.real_run = subprocess.run

    def stop_emulator(self):
        self.stop.set()

    def command(self, argv, *, capture_output, text, timeout, env=None):
        if getattr(self, "repeat_signal_during_cleanup", False) and argv[:4] == [
            "sudo",
            "-n",
            "kill",
            "-TERM",
        ]:
            os.kill(os.getpid(), signal.SIGTERM)
        if argv[0] in ("nix-nrf", "udevadm") or (
            argv[0] == "openocd" and "fwc_scan" in argv
        ):
            return self.discovery(argv, timeout)
        if argv[:3] == ["sudo", "-n", "lsof"]:
            return subprocess.CompletedProcess(argv, self.lsof_rc, "", "")
        return self.real_run(
            argv, capture_output=capture_output, text=text, timeout=timeout, env=env
        )

    def run_attach(self):
        with patch.object(hci_dongle.subprocess, "run", side_effect=self.command):
            return hci_dongle.execute(
                "attach",
                self.args,
                sysfs_root=self.sysfs,
                serial_factory=lambda **kw: Uart(self.slave, **kw),
                settle=0,
                repo_root=str(self.repo),
            )

    def result(self):
        return json.loads((self.out / self.args.run_id / "result.json").read_text())

    def test_success_and_owned_cleanup(self):
        self.prepare()
        self.run_attach()
        result = self.result()
        self.assertEqual(result["status"], "completed")
        self.assertEqual(result["adapters_before"], ["hci2"])
        self.assertEqual(result["owned_adapter"], "hci7")
        self.assertEqual(result["hci_preflight"][1]["rx"], "040e0a01091000eeddccbbaac0")
        self.assertIn("hci7", (self.out / "run1/child.stdout").read_text())
        self.assertFalse(pathlib.Path(self.sysfs, "class/bluetooth/hci7").exists())
        self.assertFalse(any("hci2" in str(c["argv"]) for c in result["commands"]))

    def test_corrupt_and_truncated_uart(self):
        self.prepare("wrong")
        for i, mode in enumerate(("wrong", "truncated", "wrongaddr", "extra")):
            self.mode = mode
            self.args.run_id = "bad%d" % i
            with self.assertRaises(hci_dongle.DongleError):
                self.run_attach()
            self.assertEqual(self.result()["status"], "error")
            self.assertFalse(
                any("btattach" in str(c["argv"]) for c in self.result()["commands"])
            )

    def test_stale_tty_and_child_exit(self):
        self.prepare()
        self.lsof_rc = 0
        with self.assertRaises(hci_dongle.DongleError):
            self.run_attach()
        self.assertNotIn("hci_preflight", self.result())
        self.args.run_id = "childfail"
        self.lsof_rc = 1
        self.args.command = [sys.executable, "-c", "import sys; sys.exit(4)"]
        with self.assertRaises(hci_dongle.DongleError):
            self.run_attach()
        self.assertEqual(self.result()["status"], "error")
        self.assertFalse(pathlib.Path(self.sysfs, "class/bluetooth/hci7").exists())

    def test_timeout_kills_child_group(self):
        self.prepare()
        self.args.timeout = 0.2
        self.args.command = [
            sys.executable,
            "-c",
            "import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(30)",
        ]
        with self.assertRaises(hci_dongle.ChildTimeout):
            self.run_attach()
        self.assertEqual(self.result()["status"], "timeout")
        self.assertFalse(pathlib.Path(self.sysfs, "class/bluetooth/hci7").exists())

    def test_ambiguous_adapters_and_startup_failure(self):
        self.prepare()
        with patch.dict(os.environ, {"FAKE_AMBIGUOUS": "1"}):
            with self.assertRaises(hci_dongle.DongleError):
                self.run_attach()
        self.assertEqual(self.result()["status"], "error")
        self.assertFalse(
            any("child.stdout" in str(c) for c in self.result()["commands"])
        )
        self.args.run_id = "badinfo"
        with patch.dict(os.environ, {"FAKE_BAD_INFO": "1"}):
            with self.assertRaises(hci_dongle.DongleError):
                self.run_attach()
        self.assertEqual(self.result()["status"], "error")
        self.assertFalse(pathlib.Path(self.sysfs, "class/bluetooth/hci7").exists())

    def test_startup_timeout_no_child(self):
        self.prepare()
        with patch.dict(os.environ, {"FAKE_NO_ADAPTER": "1"}):
            with self.assertRaisesRegex(hci_dongle.ChildTimeout, "not found"):
                self.run_attach()
        self.assertEqual(self.result()["status"], "timeout")
        self.assertFalse(
            any("child.stdout" in str(c) for c in self.result()["commands"])
        )
        self.assertTrue(pathlib.Path(self.sysfs, "class/bluetooth/hci2").exists())

    def test_sigterm_cancels_and_reaps(self):
        self.prepare()
        self.repeat_signal_during_cleanup = True
        self.args.command = [sys.executable, "-c", "import time; time.sleep(30)"]
        timer = threading.Timer(0.6, lambda: os.kill(os.getpid(), signal.SIGTERM))
        timer.start()
        try:
            with self.assertRaises(hci_dongle.Cancelled):
                self.run_attach()
        finally:
            timer.cancel()
            timer.join()
        self.assertEqual(self.result()["status"], "cancelled")
        self.assertFalse(pathlib.Path(self.sysfs, "class/bluetooth/hci7").exists())

    def test_sudo_cleanup_refusal_is_error_not_absence(self):
        self.prepare()
        owned = []
        real_popen = subprocess.Popen

        def capture(argv, *args, **kwargs):
            proc = real_popen(argv, *args, **kwargs)
            if "btattach" in argv:
                owned.append(proc)
            return proc

        try:
            with patch.object(hci_dongle.subprocess, "Popen", side_effect=capture):
                with patch.dict(os.environ, {"FAKE_REFUSE_KILL": "1"}):
                    with self.assertRaisesRegex(
                        hci_dongle.DongleError, "process group"
                    ):
                        self.run_attach()
            result = self.result()
            self.assertEqual(result["status"], "error")
            self.assertIn("btattach cleanup", str(result["cleanup_errors"]))
            self.assertIn("still alive", str(result["cleanup_errors"]))
            self.assertNotEqual(result["status"], "completed")
        finally:
            # Test owns fake process; never leave it behind after intentional refusal.
            for proc in owned:
                try:
                    os.killpg(proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                proc.wait(timeout=2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
