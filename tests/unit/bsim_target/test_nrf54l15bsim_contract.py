#!/usr/bin/env python3
"""Public build configuration contracts for canonical nRF54L15BSim Stage 1."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
BOARD = "nrf54l15bsim/nrf54l15/cpuapp"
BOARD_TS = "nrf54l15bsim_nrf54l15_cpuapp"


def read_repo_file(relative_path):
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


def config_assignments(relative_path):
    return {
        line
        for line in read_repo_file(relative_path).splitlines()
        if line.startswith("CONFIG_")
    }


class TestNrf54L15BsimContract(unittest.TestCase):
    def test_default_board_is_nrf54l15bsim_cpuapp(self):
        env = read_repo_file("scripts/bsim-env.sh")
        self.assertIn('BOARD="${BOARD:-%s}"' % BOARD, env)

    def test_stage1_uses_sw_split_and_role_fragments(self):
        runner = read_repo_file("scripts/bsim-stage1-run.sh")
        receiver_fragment = "${REPO_ROOT}/tests/bsim/overlay-bt_ll_sw_split.conf"
        client_fragment = "${REPO_ROOT}/tests/bsim/client/overlay-bt_ll_sw_split.conf"

        self.assertEqual(runner.count('snippet="bt-ll-sw-split"'), 2)
        self.assertIn('conf_overlay="%s"' % receiver_fragment, runner)
        self.assertIn('conf_overlay="%s"' % client_fragment, runner)
        self.assertLess(runner.index(receiver_fragment), runner.index(client_fragment))

        official_smoke = read_repo_file("scripts/bsim-official-smoke.sh")
        self.assertIn('snippet="bt-ll-sw-split"', official_smoke)
        self.assertIn(
            'conf_overlay="${ZEPHYR_BASE}/tests/bsim/bluetooth/audio/'
            'overlay-bt_ll_sw_split.conf"',
            official_smoke,
        )

    def test_role_controller_fragments_pin_required_capacity(self):
        receiver = config_assignments("tests/bsim/overlay-bt_ll_sw_split.conf")
        client = config_assignments("tests/bsim/client/overlay-bt_ll_sw_split.conf")

        self.assertEqual(
            receiver,
            {
                "CONFIG_BT_LL_SW_SPLIT=y",
                "CONFIG_BT_CTLR_ASSERT_HANDLER=y",
                "CONFIG_BT_CTLR_DATA_LENGTH_MAX=251",
                "CONFIG_BT_CTLR_ADV_DATA_LEN_MAX=191",
                "CONFIG_BT_CTLR_PERIPHERAL_ISO=y",
                "CONFIG_BT_CTLR_CONN_ISO_GROUPS=1",
                "CONFIG_BT_CTLR_CONN_ISO_STREAMS=2",
                "CONFIG_BT_CTLR_CONN_ISO_STREAMS_PER_GROUP=2",
                "CONFIG_BT_CTLR_CONN_ISO_PDU_LEN_MAX=251",
                "CONFIG_BT_CTLR_ISO_TX_BUFFERS=3",
                "CONFIG_BT_CTLR_ISO_TX_BUFFER_SIZE=255",
                "CONFIG_BT_CTLR_ISO_TX_SDU_LEN_MAX=255",
                "CONFIG_BT_CTLR_ISO_RX_BUFFERS=4",
                "CONFIG_BT_CTLR_ISOAL_SOURCES=1",
                "CONFIG_BT_CTLR_ISOAL_SINKS=2",
                "CONFIG_ASSERT=y",
            },
        )
        self.assertEqual(
            client,
            {
                "CONFIG_BT_LL_SW_SPLIT=y",
                "CONFIG_BT_CTLR_ASSERT_HANDLER=y",
                "CONFIG_BT_CTLR_DATA_LENGTH_MAX=251",
                "CONFIG_BT_CTLR_SCAN_DATA_LEN_MAX=191",
                "CONFIG_BT_CTLR_CENTRAL_ISO=y",
                "CONFIG_BT_CTLR_CONN_ISO_GROUPS=1",
                "CONFIG_BT_CTLR_CONN_ISO_STREAMS=2",
                "CONFIG_BT_CTLR_CONN_ISO_STREAMS_PER_GROUP=2",
                "CONFIG_BT_CTLR_CONN_ISO_PDU_LEN_MAX=251",
                "CONFIG_BT_CTLR_CONN_ISO_RELIABILITY_POLICY=y",
                "CONFIG_BT_CTLR_ISO_TX_BUFFERS=6",
                "CONFIG_BT_CTLR_ISO_TX_BUFFER_SIZE=255",
                "CONFIG_BT_CTLR_ISO_TX_SDU_LEN_MAX=255",
                "CONFIG_BT_CTLR_ISO_RX_BUFFERS=1",
                "CONFIG_BT_CTLR_ISOAL_SOURCES=2",
                "CONFIG_BT_CTLR_ISOAL_SINKS=1",
                "CONFIG_ASSERT=y",
            },
        )
        self.assertIn(
            "CONFIG_BT_ISO_TX_BUF_COUNT=6",
            config_assignments("tests/bsim/client/prj.conf"),
        )

    def test_sysbuild_has_only_cpuapp_image(self):
        for relative_path in (
            "tests/bsim/Kconfig.sysbuild",
            "tests/bsim/client/Kconfig.sysbuild",
        ):
            content = read_repo_file(relative_path)
            self.assertIn('source "share/sysbuild/Kconfig"', content)
            self.assertIn("NRF54L15BSIM_NRF54L15_CPUAPP", content)
            for forbidden in ("NET_CORE_BOARD", "NET_CORE_IMAGE_HCI_IPC", "nrf5340"):
                self.assertNotIn(forbidden, content)

        for relative_path in (
            "tests/bsim/sysbuild.cmake",
            "tests/bsim/client/sysbuild.cmake",
        ):
            content = read_repo_file(relative_path)
            self.assertIn(
                "native_simulator_set_final_executable(${DEFAULT_IMAGE})", content
            )
            self.assertIn(
                "native_simulator_set_primary_mcu_index(${DEFAULT_IMAGE})", content
            )
            for forbidden in (
                "ExternalZephyrProject_Add",
                "hci_ipc",
                "NET_APP",
                "native_simulator_set_child_images",
                "nrf5340",
            ):
                self.assertNotIn(forbidden, content)

    def test_metadata_script_and_lsp_use_nrf54l15bsim(self):
        testcase = read_repo_file("tests/bsim/testcase.yaml")
        self.assertEqual(testcase.count(BOARD), 2)

        standalone = read_repo_file("tests/bsim/test_scripts/le_audio_receiver.sh")
        self.assertIn('BOARD_TS="%s"' % BOARD_TS, standalone)

        lsp = read_repo_file("scripts/gen-lsp-links.sh")
        self.assertIn(
            "bs_%s_le_audio_receiver_bsim_prj_conf/bsim/compile_commands.json"
            % BOARD_TS,
            lsp,
        )
        self.assertIn(
            "bs_%s_bsim_client_bsim_prj_conf/client/compile_commands.json" % BOARD_TS,
            lsp,
        )

    def test_active_bsim_contracts_have_no_nrf5340_dependency(self):
        active_paths = (
            "scripts/bsim-env.sh",
            "scripts/bsim-stage1-run.sh",
            "scripts/bsim-official-smoke.sh",
            "scripts/gen-lsp-links.sh",
            "tests/bsim/Kconfig.sysbuild",
            "tests/bsim/sysbuild.cmake",
            "tests/bsim/prj.conf",
            "tests/bsim/CMakeLists.txt",
            "tests/bsim/overlay-bt_ll_sw_split.conf",
            "tests/bsim/testcase.yaml",
            "tests/bsim/test_scripts/le_audio_receiver.sh",
            "tests/bsim/client/Kconfig.sysbuild",
            "tests/bsim/client/sysbuild.cmake",
            "tests/bsim/client/prj.conf",
            "tests/bsim/client/CMakeLists.txt",
            "tests/bsim/client/overlay-bt_ll_sw_split.conf",
        )
        for relative_path in active_paths:
            with self.subTest(path=relative_path):
                content = read_repo_file(relative_path)
                self.assertNotIn("nrf5340bsim", content)
                self.assertNotIn("nrf5340_cpunet", content)

    def test_build_diagnostics_keep_warnings_as_errors(self):
        for relative_path in (
            "tests/bsim/CMakeLists.txt",
            "tests/bsim/client/CMakeLists.txt",
        ):
            content = read_repo_file(relative_path)
            self.assertLess(
                content.index("set(CMAKE_EXPORT_COMPILE_COMMANDS ON)"),
                content.index("find_package(Zephyr REQUIRED"),
            )
            self.assertNotIn("-Wno-cpp", content)

        for relative_path in (
            "scripts/bsim-stage1-run.sh",
            "scripts/bsim-official-smoke.sh",
        ):
            content = read_repo_file(relative_path)
            self.assertIn('NIX_HARDENING_ENABLE="${NIX_HARDENING_ENABLE:-}"', content)
            self.assertIn("fortify|fortify3", content)
            self.assertIn(
                "export cmake_args='-DCONFIG_COVERAGE=y "
                "-DCONFIG_COMPILER_WARNINGS_AS_ERRORS=y'",
                content,
            )
            self.assertIn("export cmake_extra_args='-DCONFIG_ASSERT=y'", content)

        official_smoke = read_repo_file("scripts/bsim-official-smoke.sh")
        self.assertNotIn("CONFIG_COMPILER_WARNINGS_AS_ERRORS=n", official_smoke)


if __name__ == "__main__":
    unittest.main()
