#!/usr/bin/env python3
"""Unit tests for fw-build-dongle merge behavior, run through public script
execution with fake west / fake mergehex.py and a temp repo.  No Zephyr
build and no hardware needed.

Run directly:

    python3 tests/unit/fw_flash_dongle/test_fw_build_dongle.py
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
_SCRIPTS_BIN = os.path.join(_REPO_ROOT, "scripts", "bin")

FAKE_WEST = """#!/usr/bin/env bash
printf '%s\\0' "$@" > "${WEST_ARGV_FILE:?}"
d=""
prev=""
for a in "$@"; do
  if [ "$prev" = "-d" ]; then d="$a"; fi
  prev="$a"
done
[ -n "$d" ] || exit 1
case "$*" in
  *nrf5340dk/nrf5340/cpunet*)
    # step 1: repo-owned hci_ipc netcore build (skip when simulating a
    # failed step-1 build so no netcore hex is produced)
    if [ -n "${FAKE_WEST_SKIP_NET_HEX:-}" ]; then exit 0; fi
    mkdir -p "$d/hci_ipc/zephyr"
    printf 'nethex\\n' > "$d/hci_ipc/zephyr/zephyr.hex"
    ;;
  *nrf5340dk/nrf5340/cpuapp*)
    # step 2: sysbuild app core + empty netcore placeholder
    mkdir -p "$d/hci_uart/zephyr" "$d/hci_ipc/zephyr"
    printf 'apphex\\n' > "$d/hci_uart/zephyr/zephyr.hex"
    printf 'placeholder\\n' > "$d/hci_ipc/zephyr/zephyr.hex"
    ;;
esac
exit 0
"""

FAKE_MERGEHEX = """#!/usr/bin/env python3
import os
import sys

args = sys.argv[1:]
out = None
for i, a in enumerate(args):
    if a == "-o":
        out = args[i + 1]
with open(os.environ["FAKE_MERGEHEX_ARGV_FILE"], "w") as fh:
    fh.write("\\0".join(args))

if os.environ.get("FAKE_MERGEHEX_FAIL") == "1":
    print("fake mergehex failure", file=sys.stderr)
    sys.exit(1)
if not out:
    print("missing -o", file=sys.stderr)
    sys.exit(2)
with open(out, "w") as fh:
    fh.write("merged\\n")
sys.exit(0)
"""


def _write(path, body, mode=0o755):
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(body)
    os.chmod(path, mode)
    return path


class BuildDongleHarness:
    def __init__(self, tmpdir):
        self.tmpdir = tmpdir
        self.repo = os.path.join(tmpdir, "repo")
        bin_dir = os.path.join(self.repo, "scripts", "bin")
        os.makedirs(bin_dir)
        shutil.copy(
            os.path.join(_SCRIPTS_BIN, "fw-build-dongle"),
            os.path.join(bin_dir, "fw-build-dongle"),
        )
        shutil.copy(
            os.path.join(_SCRIPTS_BIN, "fw-common.sh"),
            os.path.join(bin_dir, "fw-common.sh"),
        )
        # repo-owned dongle sources the build script validates
        os.makedirs(os.path.join(self.repo, "dongle", "hci_uart"))
        with open(os.path.join(self.repo, "dongle", "hci_uart", "app.conf"), "w") as fh:
            fh.write("# conf\n")
        os.makedirs(os.path.join(self.repo, "dongle", "hci_ipc", "src"))
        with open(
            os.path.join(self.repo, "dongle", "hci_ipc", "src", "main.c"), "w"
        ) as fh:
            fh.write("int main(void) { return 0; }\n")

        # fake Zephyr base: hci_uart sample dir + mergehex tool
        self.zephyr_base = os.path.join(tmpdir, "zephyrbase")
        os.makedirs(os.path.join(self.zephyr_base, "samples", "bluetooth", "hci_uart"))
        os.makedirs(os.path.join(self.zephyr_base, "scripts", "build"))
        self.mergehex_path = _write(
            os.path.join(self.zephyr_base, "scripts", "build", "mergehex.py"),
            FAKE_MERGEHEX,
        )

        self.fakebin = os.path.join(tmpdir, "fakebin")
        os.makedirs(self.fakebin)
        self.west_path = _write(os.path.join(self.fakebin, "west"), FAKE_WEST)

        self.west_argv_file = os.path.join(tmpdir, "west.argv")
        self.mergehex_argv_file = os.path.join(tmpdir, "mergehex.argv")

        self.env = dict(os.environ)
        self.env["PATH"] = self.fakebin + os.pathsep + self.env["PATH"]
        self.env["ZEPHYR_BASE"] = self.zephyr_base
        self.env["WEST_ARGV_FILE"] = self.west_argv_file
        self.env["FAKE_MERGEHEX_ARGV_FILE"] = self.mergehex_argv_file
        self.env.pop("FAKE_MERGEHEX_FAIL", None)

        self.build_dir = os.path.join(self.repo, "build", "dongle")
        self.merged_hex = os.path.join(self.build_dir, "merged.hex")
        self.merged_cpunet = os.path.join(self.build_dir, "merged_CPUNET.hex")
        self.app_hex = os.path.join(self.build_dir, "hci_uart", "zephyr", "zephyr.hex")
        self.sysbuild_net_hex = os.path.join(
            self.build_dir, "hci_ipc", "zephyr", "zephyr.hex"
        )
        self.hci_ipc_build_hex = os.path.join(
            self.repo, "build", "dongle_hci_ipc", "hci_ipc", "zephyr", "zephyr.hex"
        )

    def run(self, fail_merge=False, skip_net_hex=False):
        env = dict(self.env)
        if fail_merge:
            env["FAKE_MERGEHEX_FAIL"] = "1"
        if skip_net_hex:
            env["FAKE_WEST_SKIP_NET_HEX"] = "1"
        return subprocess.run(
            [os.path.join(self.repo, "scripts", "bin", "fw-build-dongle")],
            env=env,
            capture_output=True,
            text=True,
            timeout=30,
        )


def _nul_args(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as fh:
        return [a.decode("utf-8") for a in fh.read().split(b"\0") if a]


class FwBuildDongleMerge(unittest.TestCase):
    def test_successful_merge_produces_expected_artifact(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = BuildDongleHarness(tmp)
            r = h.run()
            self.assertEqual(0, r.returncode, r.stderr)
            self.assertIn("Merged hexes written", r.stdout)
            with open(h.merged_hex, "r") as fh:
                self.assertEqual("merged\n", fh.read())
            with open(h.merged_cpunet, "r") as fh:
                self.assertEqual("nethex\n", fh.read(), "CPUNET hex = real hci_ipc hex")
            args = _nul_args(h.mergehex_argv_file) or []
            self.assertEqual(
                args[0:4], ["-o", h.merged_hex, h.app_hex, h.sysbuild_net_hex]
            )

    def test_merge_failure_returns_nonzero_and_reports_no_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = BuildDongleHarness(tmp)
            r = h.run(fail_merge=True)
            self.assertNotEqual(0, r.returncode)
            self.assertIn("ERROR: mergehex.py failed", r.stderr)
            self.assertNotIn("Merged hexes written", r.stdout)
            self.assertFalse(
                os.path.exists(h.merged_hex),
                "failed merge must not leave a merged.hex behind",
            )
            err_log = os.path.join(h.build_dir, "mergehex.err")
            self.assertTrue(os.path.exists(err_log), "merge stderr evidence kept")

    def test_merge_failure_removes_stale_target(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = BuildDongleHarness(tmp)
            os.makedirs(h.build_dir)
            with open(h.merged_hex, "w") as fh:
                fh.write("stale\n")
            r = h.run(fail_merge=True)
            self.assertNotEqual(0, r.returncode)
            self.assertFalse(
                os.path.exists(h.merged_hex),
                "stale merged.hex must not survive a failed merge",
            )

    def test_successful_merge_replaces_stale_target(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = BuildDongleHarness(tmp)
            os.makedirs(h.build_dir)
            with open(h.merged_hex, "w") as fh:
                fh.write("stale\n")
            r = h.run()
            self.assertEqual(0, r.returncode, r.stderr)
            with open(h.merged_hex, "r") as fh:
                self.assertEqual("merged\n", fh.read())

    def test_missing_hci_ipc_hex_fails_before_merge(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = BuildDongleHarness(tmp)
            # Simulate a failed step-1 build: fake west produces no netcore
            # hex, so the pre-existing failure path must fire and no merge
            # may happen.
            r = h.run(skip_net_hex=True)
            self.assertNotEqual(0, r.returncode)
            self.assertIn("ERROR: hci_ipc hex not found", r.stderr)
            self.assertNotIn("Merged hexes written", r.stdout)
            self.assertFalse(os.path.exists(h.merged_hex))


if __name__ == "__main__":
    unittest.main(verbosity=2)
