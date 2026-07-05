# nRF5340 Flashing

## Hardware

- **SoC**: nRF5340 (dual Cortex-M33)
  - `cpuapp`: application core, 1 MB flash, 512 KB RAM
  - `cpunet`: network core, 256 KB flash, 64 KB RAM
- **Module**: Ebyte E83-2G4M03S-TB (nRF5340 module, no external QSPI flash)
- **Dev board base**: nRF5340DK footprint, custom UART0 pinout (CH340X USB-serial), no QSPI
- **Debug probe**: Raspberry Pi Pico running CMSIS-DAP firmware (probe serial configured via scripts/probe-serial.local, see AGENTS.md)

## Build system

Project uses Zephyr sysbuild (multi-image). Two images built:

| Domain | Target | Output |
|--------|--------|--------|
| `le-audio-receiver` | `ebyte_e83_nrf5340/nrf5340/cpuapp` | `build/merged.hex` |
| `hci_ipc` | `ebyte_e83_nrf5340/nrf5340/cpunet` | `build/merged_CPUNET.hex` |

`build/merged.hex` = MCUboot + app image merged for app core.
`build/merged_CPUNET.hex` = MCUboot + hci_ipc image merged for net core.

`hci_ipc` is marked `BUILD_ONLY TRUE` in `sysbuild.cmake` — excluded from `flash_order`,
flashed by the app domain runner instead of by a separate west domain flash.

## Flash workflow

```
fw-flash-5340
```

This helper runs `west flash --build-dir build/nrf5340`, which uses OpenOCD via CMSIS-DAP probe. Flashes both cores in one session.

OpenOCD command constructed by west runner:

```
openocd
  -f interface/cmsis-dap.cfg
  -f target/nordic/nrf53.cfg
  -f boards/support/flash_nrf5340.tcl
  -c 'cmsis_dap_serial <from scripts/probe-serial.local or auto-detect>'
  -c 'transport select swd'
  -c 'adapter speed 100'
  -c 'set NET_CORE_HEX {/path/to/build/merged_CPUNET.hex}'
  -c init
  -c targets
  -c check_approtect          # pre-load: recover if APPROTECT locked
  -c 'reset init'
  -c 'flash_west merged.hex'  # cmd-load: flashes both cores
  -c 'reset run'
  -c shutdown
```

`flash_west` proc (in `boards/support/flash_nrf5340.tcl`):

1. Flashes app core (`merged.hex`, passed as arg by runner)
2. Releases net core from FORCEOFF (`nrf53_cpunet_release`)
3. Switches target to `nrf53.cpunet`, halts, probes flash bank 2
4. Flashes net core (`merged_CPUNET.hex`, from `global NET_CORE_HEX`)
5. `reset run` — both cores start

`check_approtect` proc: if app core APPROTECT is engaged, runs `nrf53_recover` before
the runner's `reset init`, otherwise a no-op.

## CMake runner registration

The custom board `boards/ebyte/e83_nrf5340/board.cmake` registers OpenOCD as the
flash runner and configures all runner arguments. This replaces the previous
`BOARD_FLASH_RUNNER` CACHE hack and `app_set_runner_args()` macro that lived
in `CMakeLists.txt`.

**board.cmake (cpuapp)** calls `board_set_flasher(openocd)` (not
`board_set_flasher_ifnset`) — OpenOCD is the sole flasher for this board. The
runner args (`--config`, `--cmd-pre-init`, `--cmd-pre-load`, `--cmd-load`) are
set directly via `board_runner_args(openocd ...)`. Two values come from
`CMakeLists.txt` as CMake variables (set before `find_package(Zephyr)`):

- `_PROBE_SERIAL` — read from `scripts/probe-serial.local` at configure time.
  Empty → OpenOCD auto-detects the CMSIS-DAP probe.
- `_NET_CORE_HEX` — derived as `${CMAKE_BINARY_DIR}/../merged_CPUNET.hex`,
  resolving to the sysbuild top-level net core hex.

The cpunet board target does not register a flasher — the app-domain runner
flashes both cores in one OpenOCD session (`BUILD_ONLY TRUE` on hci_ipc in
`sysbuild.cmake`).

Items that remain project-level in `CMakeLists.txt` (sysbuild/project-specific,
not board-specific):

- Probe-serial read from `scripts/probe-serial.local`
- `_NET_CORE_HEX` path derivation (depends on sysbuild output layout)
- `BOARD_ROOT` — exposes the project's `boards/` dir for custom board discovery

## Board definition (`boards/ebyte/e83_nrf5340/`)

The custom board definition for the Ebyte E83-2G4M03S module now absorbs what
was previously an overlay on the nRF5340DK. The board files are:

| File | Absorbs |
|------|---------|
| `*_cpuapp.dts` | Pinctrl (UART0, I2S0), I2S0 enable, QSPI disable, `i2s-audio` alias, clock config — previously in `boards/nrf5340dk_nrf5340_cpuapp.overlay` |
| `*_cpunet.dts` | Net core pinctrl, flash partitions, shared RAM layout |
| `board.cmake` | Runner registration — previously the `BOARD_FLASH_RUNNER` CACHE hack + `app_set_runner_args()` macro in `CMakeLists.txt` |
| `Kconfig.defconfig` | `BT_HCI_IPC` default, heap pool sizing for cpuapp |
| `Kconfig.ebyte_e83_nrf5340` | SoC selection for cpuapp/cpunet |
| `pre_dt_board.cmake` | DTC warning suppression (overlapping unit addresses) |

The `boards/support/flash_nrf5340.tcl` (moved from `scripts/`) provides the
`flash_west` and `check_approtect` TCL procs used by the OpenOCD runner.

