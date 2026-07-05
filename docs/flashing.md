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
| `le-audio-receiver` | `nrf5340dk/nrf5340/cpuapp` | `build/merged.hex` |
| `hci_ipc` | `nrf5340dk/nrf5340/cpunet` | `build/merged_CPUNET.hex` |

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
  -f scripts/flash_nrf5340.tcl
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

`flash_west` proc (in `scripts/flash_nrf5340.tcl`):

1. Flashes app core (`merged.hex`, passed as arg by runner)
2. Releases net core from FORCEOFF (`nrf53_cpunet_release`)
3. Switches target to `nrf53.cpunet`, halts, probes flash bank 2
4. Flashes net core (`merged_CPUNET.hex`, from `global NET_CORE_HEX`)
5. `reset run` — both cores start

`check_approtect` proc: if app core APPROTECT is engaged, runs `nrf53_recover` before
the runner's `reset init`, otherwise a no-op.

## CMake runner registration

`nrf5340dk/board.cmake` in NCS does not include `openocd.board.cmake` — openocd is not
a supported runner for this board by default. The project registers it in `CMakeLists.txt`.

Two mechanisms required:

**1. Force openocd as flash runner (before `find_package`):**

```cmake
set(BOARD_FLASH_RUNNER "openocd" CACHE STRING "Default flash runner" FORCE)
```

Must be a CACHE variable. Board cmake uses `board_set_flasher_ifnset()` which checks
`NOT DEFINED` — a CACHE var is defined, so the board cannot override it. A normal CMake
variable would not survive the scope boundary.

**2. Register runner args via `app_set_runner_args()` macro:**

```cmake
macro(app_set_runner_args)
  get_property(_runner_args_set GLOBAL PROPERTY LE_AUDIO_RUNNER_ARGS_SET)
  if(NOT _runner_args_set)
    set_property(GLOBAL PROPERTY LE_AUDIO_RUNNER_ARGS_SET TRUE)
    get_filename_component(_net_hex "${CMAKE_BINARY_DIR}/../merged_CPUNET.hex" ABSOLUTE)

    # Read probe serial from local (gitignored) file if present.
    set(_probe_serial "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/probe-serial.local")
      file(READ "${CMAKE_CURRENT_SOURCE_DIR}/scripts/probe-serial.local" _probe_serial_raw)
      string(STRIP "${_probe_serial_raw}" _probe_serial)
    endif()

    if(_probe_serial)
      message(STATUS "Using CMSIS-DAP probe serial: ${_probe_serial}")
      board_runner_args(openocd
        "--config=interface/cmsis-dap.cfg"
        "--config=target/nordic/nrf53.cfg"
        "--config=${CMAKE_SOURCE_DIR}/scripts/flash_nrf5340.tcl"
        "--cmd-pre-init=cmsis_dap_serial ${_probe_serial}"
        "--cmd-pre-init=transport select swd"
        "--cmd-pre-init=adapter speed 100"
        "--cmd-pre-init=set NET_CORE_HEX {${_net_hex}}"
        "--cmd-pre-load=check_approtect"
        "--cmd-load=flash_west"
      )
    else()
      message(STATUS "No probe-serial.local — OpenOCD will auto-detect CMSIS-DAP probe")
      board_runner_args(openocd
        "--config=interface/cmsis-dap.cfg"
        "--config=target/nordic/nrf53.cfg"
        "--config=${CMAKE_SOURCE_DIR}/scripts/flash_nrf5340.tcl"
        "--cmd-pre-init=transport select swd"
        "--cmd-pre-init=adapter speed 100"
        "--cmd-pre-init=set NET_CORE_HEX {${_net_hex}}"
        "--cmd-pre-load=check_approtect"
        "--cmd-load=flash_west"
      )
    endif()

    include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
  endif()
endmacro()
```

`app_set_runner_args()` is the official Zephyr project-level runner hook. It is called by
`board_finalize_runner_args()` inside `cmake/flash/CMakeLists.txt`, which runs **during**
`find_package(Zephyr)`. Code after `find_package` is too late to affect `runners.yaml`.

The macro is called once per runner finalized (nrfutil, jlink, nrfjprog) — three times total.
The global property guard `LE_AUDIO_RUNNER_ARGS_SET` ensures the body executes only once.

`CMAKE_BINARY_DIR` inside the macro resolves to `build/nrf5340/le-audio-receiver/`, so
`../merged_CPUNET.hex` = `build/nrf5340/merged_CPUNET.hex` (sysbuild top-level net core hex).
The TCL curly braces around the path (`{${_net_hex}}`) prevent OpenOCD from splitting on
spaces if the path contains them.

## Current overlay (`boards/nrf5340dk_nrf5340_cpuapp.overlay`)

Patches the stock nRF5340DK board definition for the E83 module hardware:

| Node | Change |
|------|--------|
| `&uart0` | Pinctrl remapped: TX=P0.20, RX=P0.22, CTS=P0.21 (CH340X wiring) |
| `&i2s0` | Enabled; SCK=P1.15, LRCK=P1.12, SDOUT=P1.13; audio clock 12.288 MHz |
| `&qspi` | `status = "disabled"` (E83 has no external QSPI flash) |
| `/aliases` | `i2s-audio = &i2s0` |

`CMakeLists.txt` appends this overlay explicitly via `DTC_OVERLAY_FILE` because the file
uses the short board name (`nrf5340dk_nrf5340_cpuapp.overlay`) which Zephyr auto-discovers,
but the explicit append makes the dependency unambiguous.

## What a custom board file would absorb

If a custom board definition is created for this hardware, it can take over:

| Currently in | Moves to |
|---|---|
| `boards/*.overlay` (pinctrl, I2S, QSPI disable, alias) | Board `.dts` |
| `CMakeLists.txt` `BOARD_FLASH_RUNNER` CACHE var | `board.cmake`: `board_set_flasher(openocd)` (works correctly at board cmake layer) |
| `CMakeLists.txt` `app_set_runner_args()` macro + guard | `board.cmake`: direct `board_runner_args(openocd ...)` + `include(openocd.board.cmake)` |
| `DTC_OVERLAY_FILE` append | Removed entirely |

Items that remain project-level even with a custom board (they are sysbuild/project-specific,
not board-specific):

- `--cmd-pre-init=set NET_CORE_HEX {...}` — path depends on sysbuild output layout
- `--cmd-pre-load=check_approtect` and `--cmd-load=flash_west` — custom TCL procs
- `sysbuild.cmake` `BUILD_ONLY TRUE` on hci_ipc

`scripts/flash_nrf5340.tcl` stays as-is regardless — it is OpenOCD logic, not board definition.
