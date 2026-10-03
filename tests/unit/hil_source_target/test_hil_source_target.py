#!/usr/bin/env python3
"""Static source config contracts and executable single-image build CLI checks.

Not radio, physical matrix, or resolved configuration acceptance.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[3]
BIN = REPO / "scripts/bin"
APP = REPO / "hil/source/app"


class TestHilSourceTarget(unittest.TestCase):
    def test_build_alias_and_direct_helper_use_same_single_image_target(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            bin_dir = root / "repo/scripts/bin"
            bin_dir.mkdir(parents=True)
            for name in (
                "fw-common.sh",
                "fw-build-hil-source",
                "fw-build-hil-source-54l15",
            ):
                shutil.copy2(BIN / name, bin_dir / name)
            fakebin = root / "fakebin"
            fakebin.mkdir()
            west = fakebin / "west"
            west.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$FAKE_WEST_ARGS"\n')
            west.chmod(0o755)
            zephyr = root / "zephyr"
            zephyr.mkdir()
            argv_path = root / "west.argv"
            env = dict(os.environ)
            env.update(
                PATH=str(fakebin) + os.pathsep + env.get("PATH", ""),
                ZEPHYR_BASE=str(zephyr),
                FAKE_WEST_ARGS=str(argv_path),
            )
            expected = [
                "build",
                "-b",
                "nrf54l15dk/nrf54l15/cpuapp",
                "--no-sysbuild",
                "--pristine",
                "-d",
                str(root / "repo/build/hil-source-nrf54l15"),
                "hil/source/app",
                "--",
                "-DCONFIG_TEST_VALUE=y",
            ]
            for name in ("fw-build-hil-source", "fw-build-hil-source-54l15"):
                with self.subTest(helper=name):
                    result = subprocess.run(
                        [str(bin_dir / name), "-DCONFIG_TEST_VALUE=y"],
                        env=env,
                        capture_output=True,
                        text=True,
                        timeout=30,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(argv_path.read_text().splitlines(), expected)

    def test_source_cmake_requires_nrf54l15_cpuapp_and_no_netcore(self):
        cmake = (APP / "CMakeLists.txt").read_text()
        self.assertIn("if(NOT CONFIG_SOC_NRF54L15_CPUAPP)", cmake)
        self.assertIn(
            'message(FATAL_ERROR "HIL source requires nRF54L15 CPUAPP")', cmake
        )
        self.assertIn("src/hil_source_controller_time_nrf54.c", cmake)
        self.assertNotIn("hil_source_controller_time_nrf53_app.c", cmake)
        self.assertNotIn("CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP", cmake)
        kconfig = (APP / "Kconfig.sysbuild").read_text()
        self.assertIn('source "share/sysbuild/Kconfig"', kconfig)
        self.assertNotIn("NET_CORE_BOARD", kconfig)
        self.assertNotIn("NET_CORE_IMAGE_HCI_IPC", kconfig)
        sysbuild = (APP / "sysbuild.cmake").read_text()
        self.assertNotIn("ExternalZephyrProject_Add", sysbuild)
        self.assertNotIn("hci_ipc", sysbuild)
        for path in (
            "src/hil_source_controller_time_nrf53_app.c",
            "boards/nrf5340dk_nrf5340_cpuapp.conf",
            "overlay-nrf5340_cpunet_sdc.conf",
            "overlay-nrf5340_cpunet_iso-bt_ll_sw_split.conf",
        ):
            with self.subTest(path=path):
                self.assertFalse((APP / path).exists())


if __name__ == "__main__":
    unittest.main()
