#!/usr/bin/env python3
"""Tests for scripts/check-build-contract.py.

Builds minimal temporary sysbuild fixtures (resolved .config + zephyr.dts
for the nRF54L15 app and FLPR images) and exercises parser/check functions
directly plus CLI exit codes.
"""

import importlib.util
import os
import shutil
import sys
import tempfile
import unittest

# The checker filename is hyphenated (scripts/check-build-contract.py),
# so it cannot be imported by module name; load it explicitly.
_CHECKER = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..",
    "..",
    "..",
    "scripts",
    "check-build-contract.py",
)
_SPEC = importlib.util.spec_from_file_location("check_build_contract", _CHECKER)
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("cannot load check-build-contract.py")
cbc = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(cbc)


# ── fixture content ─────────────────────────────────────────────────

APP54_CONFIG = """\
CONFIG_SOC_NRF54L15_CPUAPP=y
CONFIG_BT_LL_SOFTDEVICE=y
CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR=y
# CONFIG_AUDIO_RESAMPLER_IDENTITY is not set
CONFIG_AUDIO_CLOCK_ACTUATOR_NONE=y
# CONFIG_AUDIO_CLOCK_ACTUATOR_APLL is not set
CONFIG_AUDIO_OFFLOAD_ASRC=y
CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ=47619
CONFIG_LIBLC3=y
CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT=2
CONFIG_BT_BUF_ACL_TX_COUNT=3
CONFIG_BT_ISO_TX_BUF_COUNT=1
CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=1
CONFIG_BT_ISO_RX_BUF_COUNT=3
CONFIG_BT_FILTER_ACCEPT_LIST=y
CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y
CONFIG_USER_PAIRING_CONTROL=y
CONFIG_USER_PAIRING_INPUT=y
CONFIG_INPUT=y
CONFIG_USER_PAIRING_DEBOUNCE_MS=30
CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS=15000
CONFIG_USER_PAIRING_WORKQ_STACK_SIZE=1536
CONFIG_HEAP_MEM_POOL_SIZE=0
"""

FLPR_CONFIG = """\
CONFIG_FLASH_BASE_ADDRESS=0x165000
CONFIG_FLASH_SIZE=96
CONFIG_USE_DT_CODE_PARTITION=y
CONFIG_FLASH_USES_MAPPED_PARTITION=y
CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS=y
"""

DOMAINS_54L15 = """\
default: le-audio-receiver
build_dir: %ROOT%
domains:
  - name: le-audio-receiver
    build_dir: %ROOT%/le-audio-receiver
  - name: flpr
    build_dir: %ROOT%/flpr
flash_order:
  - le-audio-receiver
  - flpr
"""

# Resolved-DTS-style fixture: comments, ; terminators, labels, phandles.
APP54_DTS = """\
/dts-v1/;

/ {
	reserved-memory {
		#address-cells = < 0x1 >;
		#size-cells = < 0x1 >;
		ranges;

		sram_rx: memory@20028000 {
			reg = < 0x20028000 0x2000 >;
		};

		sram_tx: memory@2002A000 {
			reg = < 0x2002a000 0x2000 >;
		};

		pcm_ring: memory@2002C000 {
			reg = < 0x2002c000 0x4000 >;
		};

		cpuflpr_code_partition: image@165000 {
			reg = < 0x165000 0x18000 >;
		};
	};

	chosen {
		zephyr,bt-hci = &bt_hci_sdc;
	};

	cpus {
		#address-cells = < 0x1 >;

		cpuapp: cpu: cpu@0 {
			compatible = "arm,cortex-m33f";
			reg = < 0x0 >;
		};
	};

	clocks {
		lfxo: lfxo {
			compatible = "nordic,nrf54l-lfxo";
			load-capacitors = "internal";
			load-capacitance-femtofarad = < 0x3e80 >;
			status = "okay";
		};

		hfxo: hfxo {
			compatible = "nordic,nrf54l-hfxo";
			load-capacitors = "internal";
			load-capacitance-femtofarad = < 16000 >;
			status = "okay";
		};
	};

	soc {
		cpuapp_sram: memory@20000000 {
			compatible = "mmio-sram";
			reg = < 0x20000000 0x28000 >;
		};

		gpio0: gpio@50000000 {
			compatible = "nordic,nrf-gpio";
		};

		gpio1: gpio@50000800 {
			compatible = "nordic,nrf-gpio";
		};

		spi00: spi@4a000 {
			compatible = "nordic,nrf-spim";
			status = "disabled";

			mx25r64: mx25r6435f@0 {
				compatible = "jedec,spi-nor";
				status = "disabled";
			};
		};

		timer20: timer@ca000 {
			compatible = "nordic,nrf-timer";
			status = "reserved";
		};

		pdm20: pdm@d0000 {
			compatible = "nordic,nrf-pdm";
			status = "disabled";
		};

		i2s20: i2s@dd000 {
			compatible = "nordic,nrf-i2s";
			status = "okay";
			clock-source = "PCLK32M";
			pinctrl-0 = < &i2s20_default >;
		};

		bt_hci_sdc: bt_hci_sdc {
			compatible = "nordic,bt-hci-sdc";
			status = "okay";
		};

		cpuflpr_sram_code_data: memory@20030000 {
			compatible = "mmio-sram";
			reg = < 0x20030000 0x10000 >;
		};
	};

	rfsw_ctl: rfsw-ctl {
		compatible = "regulator-fixed";
		enable-gpios = < &gpio2 0x5 0x1 >;
		regulator-boot-on;
	};

	rfsw_pwr: rfsw-pwr {
		compatible = "regulator-fixed";
		enable-gpios = < &gpio2 0x3 0x0 >;
		regulator-boot-on;
	};

	leds {
		compatible = "gpio-leds";

		led0: led_0 {
			gpios = < &gpio2 0x0 0x1 >;
		};
	};

	buttons {
		compatible = "gpio-keys";
		debounce-interval-ms = < 0x1e >;

		button0: button_0 {
			gpios = < &gpio0 0x0 0x11 >;
			zephyr,code = < 0xb >;
		};

		button1: button_1 {
			gpios = < &gpio1 0x9 0x11 >;
			zephyr,code = < 0x2 >;
			status = "disabled";
		};

		button2: button_2 {
			gpios = < &gpio1 0x8 0x11 >;
			zephyr,code = < 0x3 >;
			status = "disabled";
		};

		button3: button_3 {
			gpios = < &gpio0 0x4 0x11 >;
			zephyr,code = < 0x4 >;
			status = "disabled";
		};
	};

	aliases {
		user-button = &button0;
		user-led = &led0;
	};

	gpio2: gpio@50001000 {
		compatible = "nordic,nrf-gpio";
	};

	pin-controller {
		i2s20_default: i2s20_default {
			group1 {
				psels = < 0xd000024 >,
				        < 0xf000025 >,
				        < 0x12000026 >,
				        < 0x13000027 >;
			};
		};
	};
};
"""

FLPR_DTS = """\
/dts-v1/;

/ {
	reserved-memory {
		#address-cells = < 0x1 >;
		#size-cells = < 0x1 >;
		ranges;

		sram_tx: memory@20028000 {
			reg = < 0x20028000 0x2000 >;
		};

		sram_rx: memory@2002A000 {
			reg = < 0x2002a000 0x2000 >;
		};

		pcm_ring: memory@2002C000 {
			reg = < 0x2002c000 0x4000 >;
		};
	};

	chosen {
		zephyr,sram = &cpuflpr_sram;
		zephyr,code-partition = &cpuflpr_code_partition;
	};

	cpuflpr_sram: memory@20030000 {
		compatible = "mmio-sram";
		status = "okay";
		reg = < 0x20030000 0x10000 >;
	};

	soc {
		rram-controller@5004b000 {
			rram@165000 {
				partitions {
					compatible = "fixed-partitions";
					cpuflpr_code_partition: partition@0 {
						label = "image-0";
						reg = < 0x0 0x18000 >;
					};
				};
			};
		};
	};
};
"""

BT_BAP_SOURCE = """\
static const struct bt_audio_codec_cap lc3_codec_cap = BT_AUDIO_CODEC_CAP_LC3(
	BT_AUDIO_CODEC_CAP_FREQ_48KHZ, ...);
	if (freq_hz != 48000) {
"""


def write(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(content)


class Fixture:
    """Minimal nRF54L15 sysbuild fixture in a temp directory."""

    def __init__(self):
        self.root = tempfile.mkdtemp(prefix="build_contract_test_")
        self.nrf54l15 = os.path.join(self.root, "nrf54l15")
        self.bt_bap = os.path.join(self.root, "bt_bap.c")
        self.write_valid()

    def write_valid(self):
        write(
            os.path.join(self.nrf54l15, "le-audio-receiver", "zephyr", ".config"),
            APP54_CONFIG,
        )
        write(
            os.path.join(self.nrf54l15, "le-audio-receiver", "zephyr", "zephyr.dts"),
            APP54_DTS,
        )
        write(os.path.join(self.nrf54l15, "flpr", "zephyr", ".config"), FLPR_CONFIG)
        write(os.path.join(self.nrf54l15, "flpr", "zephyr", "zephyr.dts"), FLPR_DTS)
        write(
            os.path.join(self.nrf54l15, "domains.yaml"),
            DOMAINS_54L15.replace("%ROOT%", self.nrf54l15),
        )
        write(self.bt_bap, BT_BAP_SOURCE)

    def config(self, target, image):
        return os.path.join(self.nrf54l15, image, "zephyr", ".config")

    def dts(self, target, image):
        return os.path.join(self.nrf54l15, image, "zephyr", "zephyr.dts")

    def destroy(self):
        shutil.rmtree(self.root, ignore_errors=True)


# ── tests ───────────────────────────────────────────────────────────


class TestParseConfig(unittest.TestCase):
    def test_set_unset_values(self):
        cfg = cbc.parse_config(
            "CONFIG_A=y\n# CONFIG_B is not set\nCONFIG_C=n\nCONFIG_D=0x1f\n"
            'CONFIG_E="str"\nCONFIG_F=-3\nCONFIG_G=42\n'
        )
        self.assertEqual(cfg["CONFIG_A"], "y")
        self.assertEqual(cfg["CONFIG_B"], "unset")
        self.assertEqual(cfg["CONFIG_C"], "n")
        self.assertEqual(cfg["CONFIG_D"], 0x1F)
        self.assertEqual(cfg["CONFIG_E"], "str")
        self.assertEqual(cfg["CONFIG_F"], -3)
        self.assertEqual(cfg["CONFIG_G"], 42)
        # explicit unset is distinct from absent and from =n
        self.assertIn("CONFIG_B", cfg)
        self.assertNotIn("CONFIG_MISSING", cfg)
        self.assertTrue(cbc.config_enabled(cfg, "CONFIG_A"))
        self.assertFalse(cbc.config_enabled(cfg, "CONFIG_C"))
        self.assertTrue(cbc.config_not_enabled(cfg, "CONFIG_B"))
        self.assertTrue(cbc.config_not_enabled(cfg, "CONFIG_C"))

    def test_duplicate_key_hard_error(self):
        with self.assertRaises(cbc.ConfigError):
            cbc.parse_config("CONFIG_A=y\nCONFIG_A=n\n")

    def test_malformed_line_hard_error(self):
        with self.assertRaises(cbc.ConfigError):
            cbc.parse_config("CONFIG_A=maybe\n")
        with self.assertRaises(cbc.ConfigError):
            cbc.parse_config("CONFIG_A\n")


class TestParseDts(unittest.TestCase):
    def test_labels_chosen_pins(self):
        nodes, labels = cbc.parse_dts(APP54_DTS)
        self.assertIn("i2s20", labels)
        self.assertEqual(labels["i2s20"].status(), "okay")
        self.assertEqual(cbc.decode_psel(0xD000024), (13, 1, 4))
        chosen = cbc.find_chosen(nodes)
        self.assertIsNotNone(chosen)
        hci = cbc.chosen_ref(labels, chosen, "zephyr,bt-hci")
        self.assertIs(hci, labels["bt_hci_sdc"])
        flpr_nodes, flpr_labels = cbc.parse_dts(FLPR_DTS)
        self.assertIs(
            cbc.chosen_ref(flpr_labels, cbc.find_chosen(flpr_nodes), "zephyr,sram"),
            flpr_labels["cpuflpr_sram"],
        )

    def test_multi_label_node(self):
        nodes, labels = cbc.parse_dts(APP54_DTS)
        self.assertIn("cpuapp", labels)
        self.assertIn("cpu", labels)
        self.assertIs(labels["cpuapp"], labels["cpu"])

    def test_comments_cannot_satisfy(self):
        text = '/* status = "okay"; */\n/ { foo { }; };'
        nodes, labels = cbc.parse_dts(text)
        self.assertEqual(labels, {})

    def test_partition_ancestor_unit_addr(self):
        nodes, labels = cbc.parse_dts(FLPR_DTS)
        part = labels["cpuflpr_code_partition"]
        anc = part.parent
        while anc is not None and anc.unit_addr is None:
            anc = anc.parent
        if anc is None:
            self.fail("expected partition ancestor with a unit address")
        self.assertEqual(anc.unit_addr, "@165000")


class TestValidFixture(unittest.TestCase):
    def test_full_valid_fixture_passes(self):
        fx = Fixture()
        try:
            parsed = cbc.resolve_inputs(fx.nrf54l15, fx.bt_bap)
            result = cbc.run_all(parsed)
            self.assertEqual(
                result.failures(), [], "failures: %s" % cbc.format_result(result)
            )
        finally:
            fx.destroy()

    def test_cli_success(self):
        fx = Fixture()
        try:
            rc = cbc.main(
                [
                    "--nrf54l15",
                    fx.nrf54l15,
                    "--bt-bap-source",
                    fx.bt_bap,
                ]
            )
            self.assertEqual(rc, 0)
        finally:
            fx.destroy()

    def test_legacy_cli_rejected(self):
        fx = Fixture()
        try:
            with self.assertRaises(SystemExit) as exit_code:
                cbc.main(["--nrf54l15", fx.nrf54l15, "--nrf5340", fx.root])
            self.assertEqual(exit_code.exception.code, 2)
        finally:
            fx.destroy()

    def test_alternate_default_domain_name(self):
        # Sysbuild names the app image directory after the application
        # source directory basename (e.g. the checkout directory), so the
        # checker must resolve the default image from domains.yaml rather
        # than assume a fixed name.
        fx = Fixture()
        try:
            os.rename(
                os.path.join(fx.nrf54l15, "le-audio-receiver"),
                os.path.join(fx.nrf54l15, "custom-checkout"),
            )
            write(
                os.path.join(fx.nrf54l15, "domains.yaml"),
                DOMAINS_54L15.replace("%ROOT%", fx.nrf54l15).replace(
                    "le-audio-receiver", "custom-checkout"
                ),
            )
            parsed = cbc.resolve_inputs(fx.nrf54l15, fx.bt_bap)
            result = cbc.run_all(parsed)
            self.assertEqual(
                result.failures(), [], "failures: %s" % cbc.format_result(result)
            )
        finally:
            fx.destroy()


class TestHardInputErrors(unittest.TestCase):
    def test_missing_domains_yaml(self):
        fx = Fixture()
        try:
            os.remove(os.path.join(fx.nrf54l15, "domains.yaml"))
            rc = cbc.main(
                [
                    "--nrf54l15",
                    fx.nrf54l15,
                    "--bt-bap-source",
                    fx.bt_bap,
                ]
            )
            self.assertEqual(rc, 2)
        finally:
            fx.destroy()

    def test_missing_image(self):
        fx = Fixture()
        try:
            os.remove(fx.config("54l15", "flpr"))
            rc = cbc.main(
                [
                    "--nrf54l15",
                    fx.nrf54l15,
                    "--bt-bap-source",
                    fx.bt_bap,
                ]
            )
            self.assertEqual(rc, 2)
        finally:
            fx.destroy()

    def test_missing_build_root(self):
        fx = Fixture()
        try:
            rc = cbc.main(
                [
                    "--nrf54l15",
                    os.path.join(fx.root, "nope"),
                    "--bt-bap-source",
                    fx.bt_bap,
                ]
            )
            self.assertEqual(rc, 2)
        finally:
            fx.destroy()

    def test_duplicate_config_key_hard_error(self):
        fx = Fixture()
        try:
            with open(fx.config("54l15", "le-audio-receiver"), "a") as fh:
                fh.write("CONFIG_LIBLC3=y\n")
            rc = cbc.main(
                [
                    "--nrf54l15",
                    fx.nrf54l15,
                    "--bt-bap-source",
                    fx.bt_bap,
                ]
            )
            self.assertEqual(rc, 2)
        finally:
            fx.destroy()

    def test_malformed_config_hard_error(self):
        fx = Fixture()
        try:
            write(
                fx.config("54l15", "flpr"),
                FLPR_CONFIG.replace("CONFIG_FLASH_SIZE=96", "CONFIG_FLASH_SIZE=big"),
            )
            rc = cbc.main(
                [
                    "--nrf54l15",
                    fx.nrf54l15,
                    "--bt-bap-source",
                    fx.bt_bap,
                ]
            )
            self.assertEqual(rc, 2)
        finally:
            fx.destroy()


class TestAssertionFailures(unittest.TestCase):
    def _rc_and_fails(self, mutate):
        fx = Fixture()
        try:
            mutate(fx)
            parsed = cbc.resolve_inputs(fx.nrf54l15, fx.bt_bap)
            result = cbc.run_all(parsed)
            return cbc.main(
                [
                    "--nrf54l15",
                    fx.nrf54l15,
                    "--bt-bap-source",
                    fx.bt_bap,
                ]
            ), [e[1] for e in result.failures()]
        finally:
            fx.destroy()

    def test_explicit_unset_vs_set_symbol(self):
        rc, fails = self._rc_and_fails(
            lambda fx: write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace(
                    "CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR=y",
                    "# CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR is not set",
                ),
            )
        )
        self.assertEqual(rc, 1)
        self.assertIn("54l15-001", fails)

    def test_comments_cannot_satisfy_dts(self):
        def mutate(fx):
            # Remove the real I2S node; only a comment mentions it.
            dts = APP54_DTS
            dts = dts.replace(
                "\t\ti2s20: i2s@dd000 {\n"
                '\t\t\tcompatible = "nordic,nrf-i2s";\n'
                '\t\t\tstatus = "okay";\n'
                '\t\t\tclock-source = "PCLK32M";\n'
                "\t\t\tpinctrl-0 = < &i2s20_default >;\n\t\t};",
                '/* i2s20: i2s@dd000 { status = "okay"; }; */',
            )
            write(fx.dts("54l15", "le-audio-receiver"), dts)

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-014", fails)

    def test_wrong_node_status(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    'compatible = "nordic,bt-hci-sdc";\n\t\t\tstatus = "okay";',
                    'compatible = "nordic,bt-hci-sdc";\n\t\t\tstatus = "disabled";',
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-054", fails)

    def test_wrong_compatible(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace("nordic,bt-hci-sdc", "zephyr,bt-hci-ll-sw-split"),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-055", fails)

    def test_wrong_chosen(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace("&bt_hci_sdc", "&bt_hci_missing"),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-053", fails)

    def test_wrong_encoded_pin(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace("0xf000025", "0xf000026"),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-016", fails)

    def test_host_controller_count_mismatch(self):
        def mutate(fx):
            write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace(
                    "CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=1",
                    "CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=2",
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-013", fails)

    def test_wrong_soc_rejected(self):
        rc, fails = self._rc_and_fails(
            lambda fx: write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace(
                    "CONFIG_SOC_NRF54L15_CPUAPP=y", "CONFIG_SOC_NRF5340_CPUAPP=y"
                ),
            )
        )
        self.assertEqual(rc, 1)
        self.assertIn("54l15-051", fails)

    def test_wrong_controller_selection_rejected(self):
        rc, fails = self._rc_and_fails(
            lambda fx: write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace(
                    "CONFIG_BT_LL_SOFTDEVICE=y", "CONFIG_BT_LL_SW_SPLIT=y"
                ),
            )
        )
        self.assertEqual(rc, 1)
        self.assertIn("54l15-052", fails)

    def test_wrong_rf_polarity(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace("< &gpio2 0x5 0x1 >", "< &gpio2 0x5 0x0 >"),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-021", fails)

    def test_wrong_capacitance(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    "load-capacitance-femtofarad = < 16000 >",
                    "load-capacitance-femtofarad = < 15000 >",
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-024", fails)

    def test_overlapping_memory_interval(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace("< 0x20028000 0x2000 >", "< 0x20027000 0x2000 >"),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-025", fails)

    def test_missing_memory_interval(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    "\t\tpcm_ring: memory@2002C000 {\n"
                    "\t\t\treg = < 0x2002c000 0x4000 >;\n"
                    "\t\t};\n",
                    "",
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-025", fails)

    def test_shared_memory_unit_address_mismatch(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    "sram_rx: memory@20028000", "sram_rx: memory@20029000"
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-025", fails)

    def test_nested_reserved_memory_fails_shared_and_partition_contracts(self):
        def mutate(fx):
            dts = APP54_DTS
            start = dts.index("\treserved-memory {\n")
            end = dts.index("\n\t};\n\n\tchosen", start) + len("\n\t};")
            block = dts[start:end]
            dts = dts[:start] + dts[end:]
            nested = "\n".join("\t" + line for line in block.splitlines())
            dts = dts.replace("\n\tsoc {\n", "\n\tsoc {\n" + nested + "\n", 1)
            write(fx.dts("54l15", "le-audio-receiver"), dts)

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-025", fails)
        self.assertIn("54l15-026", fails)

    def test_flpr_memory_unit_address_mismatch(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "flpr"),
                FLPR_DTS.replace(
                    "cpuflpr_sram: memory@20030000",
                    "cpuflpr_sram: memory@2002F000",
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-027", fails)

    def test_deterministic_multi_error_report(self):
        def mutate(fx):
            write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace(
                    "CONFIG_BT_ISO_RX_BUF_COUNT=3", "CONFIG_BT_ISO_RX_BUF_COUNT=2"
                ),
            )
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    'clock-source = "PCLK32M"', 'clock-source = "PCLK32M_HFXO"'
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-012", fails)
        self.assertIn("54l15-015", fails)

    def test_source_contract_failure(self):
        def mutate(fx):
            write(fx.bt_bap, "static int x; /* no capability markers */")

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("SRC-001", fails)
        self.assertIn("SRC-002", fails)

    def test_flpr_mapped_partition_contract(self):
        for original, replacement in (
            ("CONFIG_FLASH_SIZE=96\n", ""),
            ("CONFIG_FLASH_SIZE=96", "CONFIG_FLASH_SIZE=95"),
            ("CONFIG_USE_DT_CODE_PARTITION=y\n", ""),
            ("CONFIG_USE_DT_CODE_PARTITION=y", "CONFIG_USE_DT_CODE_PARTITION=n"),
            ("CONFIG_FLASH_USES_MAPPED_PARTITION=y\n", ""),
            (
                "CONFIG_FLASH_USES_MAPPED_PARTITION=y",
                "CONFIG_FLASH_USES_MAPPED_PARTITION=n",
            ),
        ):
            with self.subTest(replacement=replacement, original=original):

                def mutate(fx):
                    write(
                        fx.config("54l15", "flpr"),
                        FLPR_CONFIG.replace(original, replacement),
                    )

                rc, fails = self._rc_and_fails(mutate)
                self.assertEqual(rc, 1)
                self.assertIn("54l15-033", fails)

    def test_flpr_wrong_partition_size(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "flpr"),
                FLPR_DTS.replace("reg = < 0x0 0x18000 >", "reg = < 0x0 0x17000 >"),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-030", fails)

    def test_flpr_acceptance_off_parity(self):
        def mutate(fx):
            write(
                fx.config("54l15", "flpr"),
                FLPR_CONFIG.replace(
                    "CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS=y",
                    "# CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS is not set",
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-036", fails)

    def test_cpuapp_acceptance_off_parity(self):
        def mutate(fx):
            write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace(
                    "CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y",
                    "# CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS is not set",
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-035", fails)


class TestPairingControlAssertions(unittest.TestCase):
    """P6 resolved-artifact assertions: aliases, GPIO flags, deleted nodes,
    disabled inherited buttons, feature checks (wrong resolved values
    must fail, not copied constants)."""

    def _rc_and_fails(self, mutate):
        fx = Fixture()
        try:
            mutate(fx)
            parsed = cbc.resolve_inputs(fx.nrf54l15, fx.bt_bap)
            result = cbc.run_all(parsed)
            return cbc.main(
                [
                    "--nrf54l15",
                    fx.nrf54l15,
                    "--bt-bap-source",
                    fx.bt_bap,
                ]
            ), [e[1] for e in result.failures()]
        finally:
            fx.destroy()

    def test_54l15_control_off_fails(self):
        rc, fails = self._rc_and_fails(
            lambda fx: write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace(
                    "CONFIG_USER_PAIRING_CONTROL=y",
                    "# CONFIG_USER_PAIRING_CONTROL is not set",
                ),
            )
        )
        self.assertEqual(rc, 1)
        self.assertIn("54l15-037", fails)

    def test_54l15_input_dependency_off_fails(self):
        # USER_PAIRING_INPUT without its mandatory CONFIG_INPUT=y must be
        # caught by the contract (Kconfig silently downgrades INPUT=n to
        # 'n', so a config without the dependency would not compile the
        # adapter).
        rc, fails = self._rc_and_fails(
            lambda fx: write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace("CONFIG_INPUT=y", "# CONFIG_INPUT is not set"),
            )
        )
        self.assertEqual(rc, 1)
        self.assertIn("54l15-050", fails)

    def test_54l15_wrong_workq_stack(self):
        # The hardware-validated 1536-byte pairing work-queue stack must not
        # silently regress to the pre-fix 1024-byte size whose hardware
        # high-water measured 1012/1024 (98%, 12 B unused).
        rc, fails = self._rc_and_fails(
            lambda fx: write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace(
                    "CONFIG_USER_PAIRING_WORKQ_STACK_SIZE=1536",
                    "CONFIG_USER_PAIRING_WORKQ_STACK_SIZE=1024",
                ),
            )
        )
        self.assertEqual(rc, 1)
        self.assertIn("54l15-041", fails)

    def test_54l15_heap_not_zero(self):
        rc, fails = self._rc_and_fails(
            lambda fx: write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace(
                    "CONFIG_HEAP_MEM_POOL_SIZE=0", "CONFIG_HEAP_MEM_POOL_SIZE=4096"
                ),
            )
        )
        self.assertEqual(rc, 1)
        self.assertIn("54l15-042", fails)

    def test_wrong_user_button_alias(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace("user-button = &button0;", "user-button = &button1;"),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-043", fails)

    def test_wrong_debounce(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    "debounce-interval-ms = < 0x1e >;",
                    "debounce-interval-ms = < 0x14 >;",
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-044", fails)

    def test_wrong_button_pin(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    "gpios = < &gpio0 0x0 0x11 >;", "gpios = < &gpio0 0x1 0x11 >;"
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-045", fails)

    def test_wrong_button_polarity(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    "gpios = < &gpio0 0x0 0x11 >;", "gpios = < &gpio0 0x0 0x1 >;"
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-045", fails)

    def test_wrong_button_code(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace("zephyr,code = < 0xb >;", "zephyr,code = < 0x1e >;"),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-045", fails)

    def test_button_re_enabled(self):
        # Re-enable button1 specifically (its zephyr,code 0x2 is unique),
        # leaving the other disabled nodes untouched.
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    'zephyr,code = < 0x2 >;\n\t\t\tstatus = "disabled";',
                    "zephyr,code = < 0x2 >;",
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-046", fails)

    def test_wrong_user_led_alias(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace("user-led = &led0;", "user-led = &button0;"),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-047", fails)

    def test_wrong_led_pin(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    "gpios = < &gpio2 0x0 0x1 >;", "gpios = < &gpio2 0x9 0x1 >;"
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-048", fails)

    def test_wrong_led_polarity(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    "gpios = < &gpio2 0x0 0x1 >;", "gpios = < &gpio2 0x0 0x0 >;"
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-048", fails)

    def test_led1_reintroduced_fails(self):
        def mutate(fx):
            write(
                fx.dts("54l15", "le-audio-receiver"),
                APP54_DTS.replace(
                    '\tleds {\n\t\tcompatible = "gpio-leds";\n\n\t\tled0: led_0 {',
                    '\tleds {\n\t\tcompatible = "gpio-leds";\n\n'
                    "\t\tled1: led_1 {\n\t\t\tgpios = < &gpio1 0xa 0x1 >;\n\t\t};\n\n"
                    "\t\tled0: led_0 {",
                ),
            )

        rc, fails = self._rc_and_fails(mutate)
        self.assertEqual(rc, 1)
        self.assertIn("54l15-049", fails)

    def test_wrong_input_debounce_kconfig(self):
        rc, fails = self._rc_and_fails(
            lambda fx: write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace(
                    "CONFIG_USER_PAIRING_DEBOUNCE_MS=30",
                    "CONFIG_USER_PAIRING_DEBOUNCE_MS=50",
                ),
            )
        )
        self.assertEqual(rc, 1)
        self.assertIn("54l15-039", fails)

    def test_wrong_shell_timeout(self):
        rc, fails = self._rc_and_fails(
            lambda fx: write(
                fx.config("54l15", "le-audio-receiver"),
                APP54_CONFIG.replace(
                    "CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS=15000",
                    "CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS=1000",
                ),
            )
        )
        self.assertEqual(rc, 1)
        self.assertIn("54l15-040", fails)


if __name__ == "__main__":
    unittest.main()
