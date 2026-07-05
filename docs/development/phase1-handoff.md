# Phase 1 — Board Foundation — Implementation Handoff

Status: **ready for implementation**
Phase plan source: `docs/design.md` §Phase 1

## Goal

Replace the nRF5340DK overlay-hack workflow with a proper custom board
definition for the Ebyte E83-2G4M03S module, so the app builds cleanly for
both `ebyte_e83_nrf5340/nrf5340/cpuapp` (custom board, dual-core sysbuild) and
`nrf54l15dk/nrf54l15/cpuapp` (stock board + small overlay). Switch
`audio_i2s.c` to `DT_ALIAS(i2s_audio)`. Make sysbuild netcore config
board-conditional so nRF54L15 (single-core) doesn't get a netcore image.

After this phase BOTH targets compile. nRF5340 behavior is unchanged
(still builds, flashes, streams). nRF54L15 audio is not yet expected to work
end-to-end — that's Phase 4.

## Decisions locked

1. **Custom board name**: `ebyte_e83_nrf5340`. Board target
   `ebyte_e83_nrf5340/nrf5340/cpuapp`. Vendor dir `boards/ebyte/e83_nrf5340/`.
2. **nRF54L15**: stays on the stock `nrf54l15dk` board with the existing small
   project-level overlay + conf (already in `boards/`). No custom board for it
   until custom hardware exists.
3. **Sysbuild netcore gating**: use the `Kconfig.sysbuild` pattern from
   `~/ncs/v3.3.0/nrf/applications/nrf5340_audio/Kconfig.sysbuild` — set
   `NRF_DEFAULT_BLUETOOTH` default `y if SOC_NRF5340_CPUAPP`, drop the
   unconditional `SB_CONFIG_NETCORE_HCI_IPC=y` from `sysbuild.conf`. The NCS
   sysbuild Kconfig (`~/ncs/v3.3.0/nrf/sysbuild/Kconfig.netcore`) already gates
   `SUPPORT_NETCORE` on `SOC_NRF5340_CPUAPP` and defaults the `NETCORE` choice to
   `NETCORE_HCI_IPC` when `NRF_DEFAULT_BLUETOOTH=y`. On nRF54L15 the choice
   resolves to `NETCORE_NONE` cleanly, no warning.
4. **Custom board covers BOTH cpuapp and cpunet**: sysbuild derives the netcore
   board target from the app board via `NETCORE_REMOTE_BOARD_TARGET_CPUCLUSTER`
   (= "cpunet" when `SOC_NRF5340_CPUAPP`). So
   `ebyte_e83_nrf5340/nrf5340/cpunet` must also be a buildable board target —
   provide cpunet `.dts` + `_defconfig` + `.yaml` mirroring the nrf5340dk split.
   The cpunet board is what hci_ipc builds against.

## In scope

### A. Custom board definition for Ebyte E83-2G4M03S (nRF5340)

Create `boards/ebyte/e83_nrf5340/` containing the files below. Model the
structure on `~/ncs/v3.3.0/zephyr/boards/nordic/nrf5340dk/` but stripped down to
what this project actually uses (no NS/TF-M variant, no Arduino header, no
QSPI, no LEDs/buttons unless the E83 module exposes them — it doesn't, so omit
them).

Required files:

```
boards/ebyte/e83_nrf5340/
├── board.yml
├── board.cmake
├── pre_dt_board.cmake
├── Kconfig.defconfig
├── Kconfig.ebyte_e83_nrf5340
├── ebyte_e83_nrf5340_nrf5340_cpuapp.dts
├── ebyte_e83_nrf5340_nrf5340_cpuapp_defconfig
├── ebyte_e83_nrf5340_nrf5340_cpuapp.yaml
├── ebyte_e83_nrf5340_nrf5340_cpunet.dts
├── ebyte_e83_nrf5340_nrf5340_cpunet_defconfig
└── ebyte_e83_nrf5340_nrf5340_cpunet.yaml
```

#### board.yml

```yaml
board:
  name: ebyte_e83_nrf5340
  full_name: Ebyte E83-2G4M03S nRF5340
  vendor: ebyte
  socs:
  - name: 'nrf5340'
```

No `variants` — we don't build an NS/TF-M variant.

#### board.cmake

Registers OpenOCD as the flasher with the CMSIS-DAP + nRF53 config. Probe
serial is read from `scripts/probe-serial.local` at CMake configure time (the
mechanism already exists in `CMakeLists.txt` — see section C below for how it
moves here).

```cmake
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BOARD_EBYTE_E83_NRF5340_NRF5340_CPUAPP)
  board_set_flasher(openocd)

  # Probe serial is injected via the _PROBE_SERIAL CMake var set in
  # CMakeLists.txt (read from scripts/probe-serial.local). Empty → auto-detect.
  if(_PROBE_SERIAL)
    board_runner_args(openocd
      "--config=interface/cmsis-dap.cfg"
      "--config=target/nordic/nrf53.cfg"
      "--config=${BOARD_DIR}/../../support/flash_nrf5340.tcl"
      "--cmd-pre-init=cmsis_dap_serial ${_PROBE_SERIAL}"
      "--cmd-pre-init=transport select swd"
      "--cmd-pre-init=adapter speed 100"
      "--cmd-pre-init=set NET_CORE_HEX {${_NET_CORE_HEX}}"
      "--cmd-pre-load=check_approtect"
      "--cmd-load=flash_west"
    )
  else()
    board_runner_args(openocd
      "--config=interface/cmsis-dap.cfg"
      "--config=target/nordic/nrf53.cfg"
      "--config=${BOARD_DIR}/../../support/flash_nrf5340.tcl"
      "--cmd-pre-init=transport select swd"
      "--cmd-pre-init=adapter speed 100"
      "--cmd-pre-init=set NET_CORE_HEX {${_NET_CORE_HEX}}"
      "--cmd-pre-load=check_approtect"
      "--cmd-load=flash_west"
    )
  endif()
endif()

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
```

Notes:
- `board_set_flasher(openocd)` (not `board_set_flasher_ifnset`) — we want
  OpenOCD to be THE flasher, not just the default. This replaces the
  `BOARD_FLASH_RUNNER "openocd" CACHE ... FORCE` hack in `CMakeLists.txt`.
- `_PROBE_SERIAL` and `_NET_CORE_HEX` are CMake vars set by `CMakeLists.txt`
  before `find_package(Zephyr)` — see section C. `BOARD_DIR` points at
  `boards/ebyte/e83_nrf5340/`; the TCL lives at `boards/support/` (moved from
  `scripts/` — see section D).
- The cpunet board target does NOT register a flasher — the app-domain runner
  flashes both cores in one OpenOCD session (the existing `BUILD_ONLY TRUE`
  mechanism in `sysbuild.cmake` stays). So `board.cmake` only needs the
  `if(CONFIG_BOARD_EBYTE_E83_NRF5340_NRF5340_CPUAPP)` block.

#### pre_dt_board.cmake

Suppress the same dtc warning the nrf5340dk does (overlapping unit addresses
for power/clock, flash-controller/kmu):

```cmake
# SPDX-License-Identifier: Apache-2.0
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")
```

#### Kconfig.ebyte_e83_nrf5340

```kconfig
# SPDX-License-Identifier: Apache-2.0

config BOARD_EBYTE_E83_NRF5340
	select SOC_NRF5340_CPUAPP_QKAA if BOARD_EBYTE_E83_NRF5340_NRF5340_CPUAPP
	select SOC_NRF5340_CPUNET_QKAA if BOARD_EBYTE_E83_NRF5340_NRF5340_CPUNET
```

#### Kconfig.defconfig

Minimal — only what the project needs. Model on nrf5340dk's but drop QSPI,
Arduino, and the TRUSTED_EXECUTION_SECURE SRAM sizing (we don't use TF-M).
Keep `BT_HCI_IPC` default for the cpunet board (matches nrf5340dk):

```kconfig
# SPDX-License-Identifier: Apache-2.0

config HW_STACK_PROTECTION
	default ARCH_HAS_STACK_PROTECTION

if BOARD_EBYTE_E83_NRF5340_NRF5340_CPUAPP

config BT_HCI_IPC
	default y if BT_HCI

config HEAP_MEM_POOL_ADD_SIZE_BOARD
	int
	default 4096 if BT_HCI_IPC

endif # BOARD_EBYTE_E83_NRF5340_NRF5340_CPUAPP
```

#### ebyte_e83_nrf5340_nrf5340_cpuapp.dts

This file absorbs the current `boards/nrf5340dk_nrf5340_cpuapp.overlay`
content. Structure (model on `nrf5340dk_nrf5340_cpuapp.dts`):

```dts
/dts-v1/;
#include <nordic/nrf5340_cpuapp_qkaa.dtsi>

/ {
	model = "Ebyte E83-2G4M03S nRF5340 Application";
	compatible = "ebyte,e83-nrf5340-cpuapp";

	chosen {
		zephyr,sram = &sram0_image;
		zephyr,flash = &flash0;
		zephyr,code-partition = &slot0_partition;
		zephyr,console = &uart0;
		zephyr,shell-uart = &uart0;
		zephyr,bt-mon-uart = &uart0;
		zephyr,bt-c2h-uart = &uart0;
		zephyr,bt-hci = &bt_hci_ipc0;
	};

	aliases {
		i2s-audio = &i2s0;
	};
};

&vregmain { regulator-initial-mode = <NRF5X_REG_MODE_DCDC>; };
&vregradio { regulator-initial-mode = <NRF5X_REG_MODE_DCDC>; };
&lfxo {
	load-capacitors = "internal";
	load-capacitance-picofarad = <7>;
};

&gpiote { status = "okay"; };
&gpio0 { status = "okay"; };
&gpio1 { status = "okay"; };

&pinctrl {
	/* UART0 remapped to E83-2G4M03S-TB CH340X wiring:
	 * TX=P0.20, RX=P0.22, CTS=P0.21 (no RTS — CH340X 3-wire).
	 */
	uart0_default: uart0_default {
		group1 {
			psels = <NRF_PSEL(UART_TX, 0, 20)>,
				<NRF_PSEL(UART_CTS, 0, 21)>;
		};
		group2 {
			psels = <NRF_PSEL(UART_RX, 0, 22)>;
			bias-pull-up;
		};
	};
	uart0_sleep: uart0_sleep {
		group1 {
			psels = <NRF_PSEL(UART_TX, 0, 20)>,
				<NRF_PSEL(UART_RX, 0, 22)>,
				<NRF_PSEL(UART_CTS, 0, 21)>;
			low-power-enable;
		};
	};

	/* I2S0 to CJMCU-1334 (UDA1334A): BCK=P1.15, DIN=P1.13, LRCK=P1.12 */
	i2s0_default: i2s0_default {
		group1 {
			psels = <NRF_PSEL(I2S_SCK_M, 1, 15)>,
				<NRF_PSEL(I2S_LRCK_M, 1, 12)>,
				<NRF_PSEL(I2S_SDOUT, 1, 13)>;
		};
	};
	i2s0_sleep: i2s0_sleep {
		group1 {
			psels = <NRF_PSEL(I2S_SCK_M, 1, 15)>,
				<NRF_PSEL(I2S_LRCK_M, 1, 12)>,
				<NRF_PSEL(I2S_SDOUT, 1, 13)>;
			low-power-enable;
		};
	};
};

&clock {
	hfclkaudio-frequency = <12288000>;
};

&uart0 {
	status = "okay";
	current-speed = <115200>;
	pinctrl-0 = <&uart0_default>;
	pinctrl-1 = <&uart0_sleep>;
	pinctrl-names = "default", "sleep";
};

&i2s0 {
	compatible = "nordic,nrf-i2s";
	status = "okay";
	pinctrl-0 = <&i2s0_default>;
	pinctrl-1 = <&i2s0_sleep>;
	pinctrl-names = "default", "sleep";
};

/* E83 module has no external QSPI flash */
&qspi {
	status = "disabled";
};

/* Include default memory partition configuration (mcuboot, slot0/1, storage) */
#include <nordic/nrf5340_cpuapp_partition.dtsi>
```

Note: `&vregh` is not enabled (the nrf5340dk enables it for the QSPI/USB
domain; we don't use those). If the build complains about a missing `&vregh`
label, drop the line entirely — it's optional. Verify the build passes.

#### ebyte_e83_nrf5340_nrf5340_cpuapp_defconfig

```kconfig
# SPDX-License-Identifier: Apache-2.0
CONFIG_ARM_MPU=y
CONFIG_ARM_TRUSTZONE_M=y
CONFIG_GPIO=y
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
```

#### ebyte_e83_nrf5340_nrf5340_cpuapp.yaml

```yaml
identifier: ebyte_e83_nrf5340/nrf5340/cpuapp
name: Ebyte-E83-2G4M03S-nRF5340-Application
type: mcu
arch: arm
toolchain:
  - gnuarmemb
  - zephyr
ram: 448
flash: 1024
supported:
  - gpio
  - i2s
  - watchdog
  - uart
vendor: ebyte
```

#### ebyte_e83_nrf5340_nrf5340_cpunet.dts

The cpunet board for the hci_ipc netcore. Model on
`nrf5340dk_nrf5340_cpunet.dts` but stripped (no Arduino, no ieee802154 unless
needed — hci_ipc with SW Split LL doesn't need 802.154). Keep `bt-hci-ipc`
chosen so the netcore registers the IPC HCI.

```dts
/dts-v1/;
#include <nordic/nrf5340_cpunet_qkaa.dtsi>

/ {
	model = "Ebyte E83-2G4M03S nRF5340 Network";
	compatible = "ebyte,e83-nrf5340-cpunet";

	chosen {
		zephyr,console = &uart0;
		zephyr,shell-uart = &uart0;
		zephyr,bt-mon-uart = &uart0;
		zephyr,bt-c2h-uart = &uart0;
		zephyr,bt-hci-ipc = &ipc0;
		zephyr,sram = &sram1;
		zephyr,flash = &flash1;
		zephyr,code-partition = &slot0_partition;
	};

	aliases {
		watchdog0 = &wdt0;
	};
};

&gpiote { status = "okay"; };
&gpio0 { status = "okay"; };
&gpio1 { status = "okay"; };

/* Net core UART0 shares the same CH340X pins as app core (P0.20 TX, P0.22 RX).
 * The app core owns the UART in normal operation; this pinctrl is here so
 * the net-core build can configure the device if it needs logging.
 */
&pinctrl {
	uart0_default: uart0_default {
		group1 {
			psels = <NRF_PSEL(UART_TX, 0, 20)>;
		};
		group2 {
			psels = <NRF_PSEL(UART_RX, 0, 22)>;
			bias-pull-up;
		};
	};
	uart0_sleep: uart0_sleep {
		group1 {
			psels = <NRF_PSEL(UART_TX, 0, 20)>,
				<NRF_PSEL(UART_RX, 0, 22)>;
			low-power-enable;
		};
	};
};

&uart0 {
	status = "okay";
	current-speed = <115200>;
	pinctrl-0 = <&uart0_default>;
	pinctrl-1 = <&uart0_sleep>;
	pinctrl-names = "default", "sleep";
};

&flash1 {
	partitions {
		compatible = "fixed-partitions";
		#address-cells = <1>;
		#size-cells = <1>;

		boot_partition: partition@0 {
			label = "mcuboot";
			reg = <0x00000000 0xc000>;
		};

		slot0_partition: partition@c000 {
			label = "image-0";
			reg = <0x0000c000 0x17000>;
		};

		slot1_partition: partition@23000 {
			label = "image-1";
			reg = <0x00023000 0x17000>;
		};

		storage_partition: partition@3a000 {
			label = "storage";
			reg = <0x0003a000 0x6000>;
		};
	};
};

/* Shared RAM layout between cpuapp and cpunet */
#include <nordic/nrf5340_shared_sram_partition.dtsi>
```

Note: the cpunet flash partition layout matches the nrf5340dk exactly — hci_ipc
with MCUboot expects this layout. Don't change the addresses.

#### ebyte_e83_nrf5340_nrf5340_cpunet_defconfig

```kconfig
# SPDX-License-Identifier: Apache-2.0
CONFIG_ARM_MPU=y
CONFIG_GPIO=y
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
```

#### ebyte_e83_nrf5340_nrf5340_cpunet.yaml

```yaml
identifier: ebyte_e83_nrf5340/nrf5340/cpunet
name: Ebyte-E83-2G4M03S-nRF5340-Network
type: mcu
arch: arm
toolchain:
  - gnuarmemb
  - zephyr
ram: 64
flash: 256
supported:
  - gpio
  - watchdog
vendor: ebyte
```

### B. Delete the nRF5340 overlay hack + switch audio_i2s.c to DT_ALIAS

Once the custom board exists, the project-level overlay
`boards/nrf5340dk_nrf5340_cpuapp.overlay` is obsolete — its content is now in
the board `.dts`. Delete it.

In `CMakeLists.txt` remove the unconditional `DTC_OVERLAY_FILE` append (lines
5-8 currently). Per-board conf/overlay is now via standard Zephyr
auto-discovery (see section E for the nRF54L15 overlay path).

In `src/audio_i2s.c` change line 24:

```c
#define I2S_NODE          DT_NODELABEL(i2s0)
```
to:
```c
#define I2S_NODE          DT_ALIAS(i2s_audio)
```

Both board DTS files (nRF5340 `.dts` above and the existing
`boards/nrf54l15dk_nrf54l15_cpuapp.overlay`) define the `i2s-audio` alias, so
this resolves correctly on both targets. This fixes design.md F1.2.

### C. CMakeLists.txt refactor

The current `CMakeLists.txt` has the `BOARD_FLASH_RUNNER` CACHE hack, the
`app_set_runner_args()` macro, and the probe-serial `file(READ)` block. With
the custom `board.cmake` doing the runner registration, most of this moves out.
What stays in `CMakeLists.txt`:

1. `BOARD_ROOT` — expose the project's `boards/` dir to Zephyr. Add BEFORE
   `find_package(Zephyr)`:
   ```cmake
   list(APPEND BOARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
   ```
2. Probe-serial read + `_NET_CORE_HEX` derivation — these must run before
   `find_package(Zephyr)` because `board.cmake` (loaded during
   `find_package`) consumes `_PROBE_SERIAL` and `_NET_CORE_HEX`. Keep:
   ```cmake
   # Read probe serial from local (gitignored) file if present.
   set(_PROBE_SERIAL "")
   if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/probe-serial.local")
     file(READ "${CMAKE_CURRENT_SOURCE_DIR}/scripts/probe-serial.local" _probe_serial_raw)
     string(STRIP "${_probe_serial_raw}" _PROBE_SERIAL)
   endif()
   if(_PROBE_SERIAL)
     message(STATUS "Using CMSIS-DAP probe serial: ${_PROBE_SERIAL}")
   else()
     message(STATUS "No probe-serial.local — OpenOCD will auto-detect CMSIS-DAP probe")
   endif()

   # Net core hex path: sysbuild top-level merged_CPUNET.hex.
   # CMAKE_BINARY_DIR at this point is the app domain build dir
   # (build/<target>/le-audio-receiver), so ../merged_CPUNET.hex resolves to
   # build/<target>/merged_CPUNET.hex.
   get_filename_component(_NET_CORE_HEX "${CMAKE_BINARY_DIR}/../merged_CPUNET.hex" ABSOLUTE)
   ```
3. Remove the `BOARD_FLASH_RUNNER` CACHE line and the entire
   `app_set_runner_args()` macro — the `board.cmake` does this now.
4. Remove the `DTC_OVERLAY_FILE` append (lines 5-8).
5. `find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})` and `project(...)` stay.
6. `target_sources(app PRIVATE ...)` stays unchanged.
7. `zephyr_sources_ifdef(CONFIG_SHELL src/audio_shell.c)` stays.
8. `zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)` stays
   (hci_ipc sample header path).

**Probe-serial.local path note**: keep reading from
`scripts/probe-serial.local` (the Phase 0 location). Don't move the file. The
path in `CMakeLists.txt` stays `scripts/probe-serial.local`.

### D. Move flash TCL to `boards/support/`

The `board.cmake` references `${BOARD_DIR}/../../support/flash_nrf5340.tcl`.
`BOARD_DIR` = `boards/ebyte/e83_nrf5340/`, so `../../support/` =
`boards/support/`. Move `scripts/flash_nrf5340.tcl` →
`boards/support/flash_nrf5340.tcl`. This is the natural home for board-support
assets. Keep the `monitor.sh` and `read_acm.py` helpers in `scripts/` — they're
runtime/dev tools, not board support.

Update any references: `docs/flashing.md` mentions `scripts/flash_nrf5340.tcl`
in the OpenOCD command example. Update those references to
`boards/support/flash_nrf5340.tcl`.

### E. nRF54L15 board conf/overlay path — verify auto-discovery

The existing `boards/nrf54l15dk_nrf54l15_cpuapp.conf` and
`boards/nrf54l15dk_nrf54l15_cpuapp.overlay` rely on Zephyr's
`<BOARD>_<board_target>.conf` / `.overlay` auto-discovery from the app's
`boards/` dir. With `BOARD_ROOT` now pointing at the project root, this
auto-discovery should pick them up for `nrf54l15dk/nrf54l15/cpuapp`.

Confirm: Zephyr's `configuration_files.cmake` auto-loads
`boards/<board_target>.conf` and `boards/<board_target>.overlay` from
`APPLICATION_SOURCE_DIR` (the project root). The current file names
(`nrf54l15dk_nrf54l15_cpuapp.{conf,overlay}`) match the board target
`nrf54l15dk/nrf54l15/cpuapp` with `/` → `_`. This should work unchanged.

If auto-discovery doesn't pick them up (it should), the fallback is to pass
them explicitly via `DTC_OVERLAY_FILE` / `EXTRA_KCONFIG_TARGETS` in a
board-conditional CMake block — but try auto-discovery first.

### F. sysbuild.conf + Kconfig.sysbuild — board-conditional netcore

Replace `sysbuild.conf` content:
```
SB_CONFIG_NETCORE_HCI_IPC=y
```
with nothing (delete the line, leave the file empty or delete it entirely —
the nrf5340_audio pattern doesn't have a `sysbuild.conf` at all; the
`Kconfig.sysbuild` default handles it). **Delete `sysbuild.conf`**.

Update `Kconfig.sysbuild` to:
```kconfig
# SPDX-License-Identifier: Apache-2.0

config NRF_DEFAULT_BLUETOOTH
	default y if SOC_NRF5340_CPUAPP

source "share/sysbuild/Kconfig"
```

This makes `NRF_DEFAULT_BLUETOOTH=y` only on nRF5340, which (per
`~/ncs/v3.3.0/nrf/sysbuild/Kconfig.netcore`) defaults the `NETCORE` choice to
`NETCORE_HCI_IPC` on nRF5340 and `NETCORE_NONE` on nRF54L15. No more
`depends on` warning on the single-core target. This fixes design.md F1.3.

`sysbuild.cmake` stays unchanged — its `if(SB_CONFIG_NETCORE_HCI_IPC)` guard
already handles the nRF54L15 case correctly (the symbol is just never set
there).

### G. fw-build-5340 — update board target

In `scripts/bin/fw-build-5340` change:
```
exec west build -b nrf5340dk/nrf5340/cpuapp --sysbuild --pristine -d "$BUILD_DIR" -- "$@"
```
to:
```
exec west build -b ebyte_e83_nrf5340/nrf5340/cpuapp --sysbuild --pristine -d "$BUILD_DIR" -- "$@"
```

Update the comment header line if it mentions the board target. The build
dir stays `build/nrf5340` (rename to `build/e83` is optional and out of scope —
keep `build/nrf5340` to avoid churning the `.clangd` path again).

### H. AGENTS.md + docs/flashing.md updates

Update `AGENTS.md`:
- "Stack" section: `nrf5340dk/nrf5340/cpuapp` → `ebyte_e83_nrf5340/nrf5340/cpuapp`
  where the board target is mentioned.
- "Key Files" table: replace the `boards/nrf5340dk_nrf5340_cpuapp.overlay` row
  with a row for the custom board dir `boards/ebyte/e83_nrf5340/` (purpose:
  "Custom board definition for Ebyte E83-2G4M03S: I2S0 pins, ACLK 12.288 MHz,
  QSPI disabled, i2s-audio alias, OpenOCD flash runner").
- Gotchas referencing `sysbuild.cmake` (SW Split LL section) — unchanged, still
  valid.

Update `docs/flashing.md`:
- "CMake runner registration" section: the `app_set_runner_args()` macro is
  gone; the runner is now registered in `board.cmake`. Rewrite this section to
  describe the new `board.cmake` mechanism. The "What a custom board file would
  absorb" table is now the lived reality — either update it to past tense
  ("absorbed by the custom board") or delete it since it's no longer a
  migration plan. Prefer updating to past tense to keep the historical context.
- "Current overlay" section: this described the old
  `boards/nrf5340dk_nrf5340_cpuapp.overlay` — now deleted. Replace with a
  "Board definition" section describing the new `boards/ebyte/e83_nrf5340/`
  layout. Or just delete the "Current overlay" section since the board
  definition is self-documenting via its files.
- `scripts/flash_nrf5340.tcl` references → `boards/support/flash_nrf5340.tcl`.

### I. Plan-of-record status line

In `AGENTS.md` "Plan of record" section, update:
```
Current status: **no phase started yet** — everything below describes the
pre-Phase-0 state ...
```
to:
```
Current status: **Phases 0–1 complete** — the build workflow (Phase 0) and
the custom board foundation (Phase 1) have landed. nRF54L15 now compiles;
audio bring-up is Phase 4. The gotchas below that describe runtime behavior
(SW Split LL, settings_load, pairing, I2S DMA, etc.) remain valid.
```

Update the "Consequences for work in this repo today" bullets:
- Remove the "nRF54L15 target does not build" bullet (now fixed).
- Remove the "Dead code slated for deletion" bullet (deleted in Phase 0).
- Keep the PACS 48 kHz bug bullet (still Phase 2).
- Keep the `nrf54l15-drift-compensation.md` superseded bullet.
- Keep the `serial-mcp` tooling reference bullet (historical context).

## Out of scope (DO NOT touch)

- PACS 48 kHz restriction — Phase 2.
- `main.c` split / audio-sink interface — Phase 2.
- Drift controller refactor — Phase 3.
- nRF54L15 audio bring-up (GRTC, SAMPLE_ADJUST actuator) — Phase 4.
- `sysbuild.cmake` logic (the `if(SB_CONFIG_NETCORE_HCI_IPC)` block + overlays)
  — stays as-is.
- `prj.conf` — stays as-is.
- `src/audio_i2s.c` logic other than the `DT_ALIAS` one-line change.
- `src/main.c` — no changes.
- `docs/design.md`, `docs/nrf54l15-drift-compensation.md` — no changes.
- The `nRF54L15` overlay/conf files in `boards/` — stay as-is (they're now
  auto-discovered, no content change).

## Test plan / verification

The executor MUST run these and report results:

1. **nRF5340 build** (primary exit criterion):
   ```bash
   fw-build-5340
   ```
   Must complete. Report the final west output lines (FLASH/RAM sizes, hex
   files). This is the same target that streams today — behavior must not
   change.

2. **nRF54L15 build compiles** (Phase 1 exit criterion, design.md):
   ```bash
   fw-build-54l15
   ```
   This was broken before (F1). It MUST now at least reach the link/configure
   stage without devicetree or CMake errors. A link error from missing audio
   driver symbols or I2S not being enabled is acceptable for Phase 1 — the
   goal is "compiles" not "works". Report the actual failure (if any) and where
   it stops. If it fully links, great — report the size.

3. **DT_ALIAS resolves on both targets**:
   ```bash
   # After fw-build-5340:
   grep -n 'i2s-audio\|i2s0\|i2s20' build/nrf5340/le-audio-receiver/zephyr/zephyr.dts
   # After fw-build-54l15 (if it gets far enough to generate zephyr.dts):
   grep -n 'i2s-audio\|i2s20' build/nrf54l15/le-audio-receiver/zephyr/zephyr.dts 2>/dev/null || echo "no zephyr.dts yet"
   ```
   The nRF5340 resolved DT must show `i2s-audio = &i2s0;` and the I2S node
   enabled. The nRF54L15 (if generated) must show `i2s-audio = &i2s20;`.

4. **Sysbuild netcore only on nRF5340**:
   ```bash
   # nRF5340: hci_ipc domain must be present
   ls build/nrf5340/hci_ipc/ && echo "OK: netcore built"
   # nRF54L15: no hci_ipc domain
   ls build/nrf54l15/hci_ipc/ 2>/dev/null && echo "FAIL: netcore built on single-core" || echo "OK: no netcore on nRF54L15"
   ```

5. **No leftover overlay hack**:
   ```bash
   git grep -n 'DTC_OVERLAY_FILE\|nrf5340dk_nrf5340_cpuapp.overlay' -- CMakeLists.txt boards/
   ```
   Should return nothing (the overlay file is deleted, the CMake append is
   removed).

6. **No leftover hardcoded i2s0 noderef**:
   ```bash
   git grep -n 'DT_NODELABEL(i2s0)' -- src/
   ```
   Should return nothing — `audio_i2s.c` now uses `DT_ALIAS(i2s_audio)`.

7. **Custom board discovered via BOARD_ROOT**:
   ```bash
   west boards | grep ebyte_e83
   ```
   Should list `ebyte_e83_nrf5340` targets (cpuapp + cpunet). Run from the dev
   shell (so `west` sees `BOARD_ROOT`).

## Constraints and invariants (from repo docs)

- **nRF5340 must keep building AND flashing AND streaming.** Build is the
  executor's exit criterion; flash/stream is the user's later verification.
- **`sysbuild.cmake` overlays (SW Split LL) must stay intact.** The
  `add_overlay_dts` + `add_overlay_config` for hci_ipc is unchanged.
- **`scripts/flash_nrf5340.tcl` procs are unchanged** — only the file location
  moves (`scripts/` → `boards/support/`).
- **Probe serial stays in `scripts/probe-serial.local`** (gitignored). Don't
  move it.
- **`prj.conf` is unchanged.** Board-specific conf goes in the board
  `_defconfig` files or the existing `boards/nrf54l15dk_nrf54l15_cpuapp.conf`.
- **The `sdk-nrf` west project naming gotcha** is irrelevant (no `west.yml`
  after Phase 0).
- **`docs/design.md` is the plan of record** — do not edit.
- **`docs/nrf54l15-drift-compensation.md` is superseded** — do not edit.

## Commit structure

Make these commits, in order, each logically separated:

1. **custom board**: add `boards/ebyte/e83_nrf5340/` (all board files), move
   `scripts/flash_nrf5340.tcl` → `boards/support/flash_nrf5340.tcl`, add
   `BOARD_ROOT` to `CMakeLists.txt`, refactor `CMakeLists.txt` (remove
   `BOARD_FLASH_RUNNER` hack, `app_set_runner_args` macro, `DTC_OVERLAY_FILE`
   append; keep probe-serial read + `_NET_CORE_HEX`), delete
   `boards/nrf5340dk_nrf5340_cpuapp.overlay`, switch `audio_i2s.c` to
   `DT_ALIAS(i2s_audio)`, update `fw-build-5340` board target.
2. **sysbuild netcore gating**: delete `sysbuild.conf`, update
   `Kconfig.sysbuild` with `NRF_DEFAULT_BLUETOOTH` default.
3. **docs**: update `AGENTS.md` (board target, key files, plan-of-record
   status) and `docs/flashing.md` (runner registration section, overlay
   section, TCL path).

Commit messages: no AI attribution. Match the repo's existing imperative
style.

## Verification commands (executor runs these)

```bash
# 1. nRF5340 build
fw-build-5340 2>&1 | tail -15

# 2. nRF54L15 build (may fail at link — report where)
fw-build-54l15 2>&1 | tail -30

# 3. DT_ALIAS resolved
grep -n 'i2s-audio\|i2s0' build/nrf5340/le-audio-receiver/zephyr/zephyr.dts | head
grep -n 'i2s-audio\|i2s20' build/nrf54l15/le-audio-receiver/zephyr/zephyr.dts 2>/dev/null | head || echo "no 54l15 zephyr.dts"

# 4. Netcore only on nRF5340
ls build/nrf5340/hci_ipc/ >/dev/null 2>&1 && echo "OK 5340 netcore" || echo "FAIL 5340 netcore"
ls build/nrf54l15/hci_ipc/ >/dev/null 2>&1 && echo "FAIL 54l15 netcore" || echo "OK 54l15 no netcore"

# 5. No leftover overlay hack
git grep -n 'DTC_OVERLAY_FILE\|nrf5340dk_nrf5340_cpuapp.overlay' -- CMakeLists.txt boards/ && echo "FAIL" || echo "OK clean"

# 6. No leftover DT_NODELABEL(i2s0)
git grep -n 'DT_NODELABEL(i2s0)' -- src/ && echo "FAIL" || echo "OK clean"

# 7. Custom board discovered
west boards | grep ebyte_e83

# git review
git status
git log --oneline -6
```

## Executor recap requirements

Return: files changed (per commit), behavior changed, verification command
output (paste actual output — at minimum the `fw-build-5340` tail, the
`fw-build-54l15` tail, the DT_ALIAS greps, and the netcore-presence checks),
commit hashes/messages, blockers or deviations from this handoff, and
suggested follow-up for Phase 2.

## Hard rules for the executor

- Implement ONLY the scoped Phase 1 work above. Do not fix F4 (PACS), F6
  (drift), or any other design.md finding.
- Do NOT touch `sysbuild.cmake`, `prj.conf`, `src/main.c`, `src/audio_i2s.c`
  logic (only the one-line `DT_ALIAS` change), `src/audio_drift.c/h`,
  `src/audio_stats.c/h`, `src/audio_volume.c/h`, `src/audio_shell.c`,
  `docs/design.md`, `docs/nrf54l15-drift-compensation.md`, or the nRF54L15
  overlay/conf files in `boards/` (they're auto-discovered, content unchanged).
- Do NOT run `fw-flash-5340` or any hardware command. No flashing, no reset,
  no serial. Build is the exit criterion.
- Do NOT push, merge, open PRs, amend, or force-push. Commit locally only.
- No AI/tool attribution in commit messages.
- If something is ambiguous and not covered above, STOP and report it in
  your recap rather than guessing at scope. In particular: if the nRF5340
  build fails for a reason not covered here (e.g. a missing label in the
  board DTS), report the error and what you tried — do not silently patch
  unrelated code to make it build.
- If the nRF54L15 build fails earlier than expected (e.g. devicetree error
  rather than link error), report the actual error — that's still useful
  progress data even if it means F1 isn't fully resolved yet.