# Board Porting Status

## What was done

Created a custom Zephyr board definition for the Ebyte E83-2G4M03S-TB
(nRF5340 module on PCB 10414-V1.2) with PicoProbe debug probe.

Board target: `e83_2g4m03s_tb/nrf5340/cpuapp` (cpuapp) and `e83_2g4m03s_tb/nrf5340/cpunet` (cpunet).

14 files created in `boards/e83_2g4m03s_tb/`:
  board.yml                    — board metadata
  Kconfig.e83_2g4m03s_tb       — SoC selection per core
  Kconfig.defconfig            — HW stack protection, BT_HCI_IPC defaults
  board.cmake                  — PyOCD runner trigger (for app_set_runner_args)
  pre_dt_board.cmake           — DT warning suppression
  e83_2g4m03s_tb_nrf5340_cpuapp.yaml   — cpuapp identifier
  e83_2g4m03s_tb_nrf5340_cpunet.yaml   — cpunet identifier
  e83_2g4m03s_tb_common.dtsi          — shared DT (watchdog alias only)
  e83_2g4m03s_tb_nrf5340_cpuapp.dts   — cpuapp root DTS
  e83_2g4m03s_tb_nrf5340_cpuapp-pinctrl.dtsi  — cpuapp pin control
  e83_2g4m03s_tb_nrf5340_cpuapp_defconfig     — cpuapp boot config
  e83_2g4m03s_tb_nrf5340_cpunet.dts   — cpunet root DTS
  e83_2g4m03s_tb_nrf5340_cpunet-pinctrl.dtsi  — cpunet pin control (empty)
  e83_2g4m03s_tb_nrf5340_cpunet_defconfig     — cpunet boot config (no serial)
  support/.gitkeep                       — OpenOCD runner compat

Key pin assignments baked into board DTS:
  UART0:  P0.20 TX, P0.22 RX, P0.21 CTS (CH340X bridge)
  I2S0:   P1.15 BCK, P1.13 DIN, P1.12 LRCK (CJMCU-1334), hfclkaudio=12.288 MHz
  I2C1:   P1.02 SCL, P1.03 SDA (P2 header)
  LFXO:   external 12 pF caps
  USB:    enabled (nrf-usbd)
  QSPI:   not present (no node = disabled)

Removed from nRF5340 DK template: LEDs, buttons, Arduino header, QSPI flash,
nRF21540 FEM, nrfjprog/jlink/nrfutil runners.

## Build

Both cores compile and link successfully (verified). Build command from
this machine (requires NCS toolchain at ~/ncs/toolchains/911f4c5c26 and
nix for OpenOCD):

```bash
export TOOLCHAIN=~/ncs/toolchains/911f4c5c26
export PATH=$TOOLCHAIN/usr/local/bin:$TOOLCHAIN/usr/bin:$TOOLCHAIN/bin:$PATH
export LD_LIBRARY_PATH=$TOOLCHAIN/usr/local/lib:$TOOLCHAIN/usr/lib:$TOOLCHAIN/usr/lib/x86_64-linux-gnu:$TOOLCHAIN/lib
export ZEPHYR_BASE=~/ncs/v3.3.0/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=$TOOLCHAIN/opt/zephyr-sdk
cd ~/ncs/v3.3.0
west build -b e83_2g4m03s_tb/nrf5340/cpuapp \
  -s /home/thomas/repos/le-audio-receiver --sysbuild --pristine \
  -d /tmp/build_e83 -- -DBOARD_ROOT=/home/thomas/repos/le-audio-receiver
```

Build output (from last successful build):
  merged.hex:         991,728 B  (app core: 34.93% flash, 29.72% RAM)
  merged_CPUNET.hex:  400,192 B  (net core: 55.51% flash, 61.77% RAM)

## Flash

Uses OpenOCD via PicoProbe (CMSIS-DAP), NOT J-Link.

Flash runner is configured in two layers:
  1. CMakeLists.txt: forces BOARD_FLASH_RUNNER=openocd, defines
     app_set_runner_args() macro with project-specific TCL args
     (check_approtect, flash_west, NET_CORE_HEX path)
  2. board.cmake: includes pyocd.board.cmake as trigger so that
     board_finalize_runner_args() calls app_set_runner_args()

OpenOCD is provided by the project's nix flake (openocd-master.nix,
built from git master for nrf53 support).

Flash command (must be run from a machine with PicoProbe connected):
```bash
nix develop /home/thomas/repos/le-audio-receiver --command bash -c '
export TOOLCHAIN=~/ncs/toolchains/911f4c5c26
export PATH=$TOOLCHAIN/usr/local/bin:$TOOLCHAIN/usr/bin:$TOOLCHAIN/bin:$PATH
export LD_LIBRARY_PATH=$TOOLCHAIN/usr/local/lib:$TOOLCHAIN/usr/lib:$TOOLCHAIN/usr/lib/x86_64-linux-gnu:$TOOLCHAIN/lib:$LD_LIBRARY_PATH
export ZEPHYR_BASE=$HOME/ncs/v3.3.0/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=$TOOLCHAIN/opt/zephyr-sdk
cd $HOME/ncs/v3.3.0
west flash --build-dir /tmp/build_e83
'
```

Constructed OpenOCD command line (verified working, last tested 2026-05-31):
```
openocd
  -s .../boards/e83_2g4m03s_tb/support
  -s .../scripts
  -f interface/cmsis-dap.cfg
  -f target/nordic/nrf53.cfg
  -f .../scripts/flash_nrf5340.tcl
  -c 'cmsis_dap_serial E6635C08CB1F502B'
  -c 'transport select swd'
  -c 'adapter speed 100'
  -c 'set NET_CORE_HEX {/tmp/build_e83/merged_CPUNET.hex}'
  -c init -c targets
  -c check_approtect
  -c 'reset init'
  -c 'flash_west /tmp/build_e83/merged.hex'
  -c 'reset run'
  -c shutdown
```

Probe: PicoProbe CMSIS-DAP serial E6635C08CB1F502B
SWD pins: P3.7=SWDIO, P3.9=SWDCLK, P3.5=RESET, P3.1=GND

## What remains

1. Connect PicoProbe to target machine (currently not connected —
   "Error: unable to find a matching CMSIS-DAP device" on this machine)
2. Run `west flash --build-dir /tmp/build_e83` with combined nix+NCS env
3. Open serial terminal on /dev/ttyACM1 at 115200 8N1
4. Verify boot: "BLE ready", settings_load OK, "Advertising as LE Audio Receiver"
5. Connect phone, pair, stream audio, verify I2S output on CJMCU-1334

## Also fixed (pre-existing bugs)

- src/audio_volume.h: added missing #include <stdbool.h>
- prj.conf: added CONFIG_REBOOT=y (needed by sys_reboot in main.c)
- prj.conf: removed duplicate CONFIG_REBOOT line (merge artifact)

## Also removed

- CMakeLists.txt: removed nrf5340dk overlay reference (DTC_OVERLAY_FILE append)
  since board DTS bakes in all pin assignments

## Key references

- Project repo: /home/thomas/repos/le-audio-receiver (branch: board-porting)
- NCS version: v3.3.0 at ~/ncs/v3.3.0/
- Toolchain: ~/ncs/toolchains/911f4c5c26/
- Build tree: /tmp/build_e83/
- Hardware docs: ~/Nextcloud/Development-Resources/le-audio/Present-Hardware/
- Flashing docs: docs/flashing.md
- Flash TCL: scripts/flash_nrf5340.tcl
- Custom OpenOCD build: nix/openocd-master.nix
- Board definition: boards/e83_2g4m03s_tb/
- sysbuild config: sysbuild.cmake (hci_ipc BUILD_ONLY TRUE)
- sysbuild.conf: SB_CONFIG_NETCORE_HCI_IPC=y
- NCS board reference: ~/ncs/v3.3.0/zephyr/boards/nordic/nrf5340dk/
