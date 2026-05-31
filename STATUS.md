# Board Porting Status

**Last verified: 2026-05-31 on `thomas-workstation` (working PicoProbe)**

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

Both cores compile and link successfully (verified on two machines).
Replace repo and NCS paths below to match your install location.

```bash
export TOOLCHAIN=~/ncs/toolchains/911f4c5c26
export PATH=$TOOLCHAIN/usr/local/bin:$TOOLCHAIN/usr/bin:$TOOLCHAIN/bin:$PATH
export LD_LIBRARY_PATH=$TOOLCHAIN/usr/local/lib:$TOOLCHAIN/usr/lib:$TOOLCHAIN/usr/lib/x86_64-linux-gnu:$TOOLCHAIN/lib
export ZEPHYR_BASE=~/ncs/v3.3.0/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=$TOOLCHAIN/opt/zephyr-sdk
cd ~/ncs/v3.3.0
west build -b e83_2g4m03s_tb/nrf5340/cpuapp \
  -s /path/to/le-audio-receiver --sysbuild --pristine \
  -d /tmp/build_e83 -- -DBOARD_ROOT=/path/to/le-audio-receiver
```

Build output (2026-05-31):
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

OpenOCD is available system-wide (nix flake provides openocd-master with
nrf53 support).

### west flash (simplest)

```bash
export TOOLCHAIN=~/ncs/toolchains/911f4c5c26
export PATH=$TOOLCHAIN/usr/local/bin:$TOOLCHAIN/usr/bin:$TOOLCHAIN/bin:$PATH
export LD_LIBRARY_PATH=$TOOLCHAIN/usr/local/lib:$TOOLCHAIN/usr/lib:$TOOLCHAIN/usr/lib/x86_64-linux-gnu:$TOOLCHAIN/lib
export ZEPHYR_BASE=$HOME/ncs/v3.3.0/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=$TOOLCHAIN/opt/zephyr-sdk
cd $HOME/ncs/v3.3.0
west flash --build-dir /tmp/build_e83
```

### Direct OpenOCD flash (what `west flash` expands to)

```bash
openocd \
  -s .../ncs/v3.3.0/zephyr/scripts \
  -s .../boards/e83_2g4m03s_tb/support \
  -f interface/cmsis-dap.cfg \
  -f target/nordic/nrf53.cfg \
  -f .../scripts/flash_nrf5340.tcl \
  -c 'cmsis_dap_serial E6635C08CB1F502B' \
  -c 'transport select swd' \
  -c 'adapter speed 100' \
  -c 'set NET_CORE_HEX {/tmp/build_e83/merged_CPUNET.hex}' \
  -c init -c targets \
  -c check_approtect \
  -c 'reset init' \
  -c 'flash_west /tmp/build_e83/merged.hex' \
  -c 'reset run' \
  -c shutdown
```

Probe: PicoProbe CMSIS-DAP serial E6635C08CB1F502B
SWD pins: P3.7=SWDIO, P3.9=SWDCLK, P3.5=RESET, P3.1=GND

## Verification (2026-05-31, thomas-workstation)

Items 1-4 completed and verified:

1. PicoProbe connected: `Raspberry Pi Debugprobe on Pico (CMSIS-DAP)` at serial
   `E6635C08CB1F502B`
2. Flash both cores: OpenOCD direct command (see above) — app + net core flashed,
   reset, running.
3. Serial terminal: **`/dev/ttyUSB0` (CH340X bridge) at 115200 8N1** —
   NOT `/dev/ttyACM1`. The PicoProbe ACM0 port is the debug probe's own UART,
   not the nRF5340 console.
4. Boot verified:

```
*** Booting nRF Connect SDK v3.3.0-ba167d9f3db4 ***
*** Using Zephyr OS v4.3.99-fd9204a02d52 ***
[00:00:00.253,265] <inf> main: Watchdog started (5 s timeout)
[00:00:00.254,180] <inf> fs_zms: 2 Sectors of 4096 bytes
[00:00:00.277,282] <inf> bt_hci_core: HW Platform: Nordic Semiconductor (0x0002)
[00:00:00.277,343] <inf> bt_hci_core: HW Variant: nRF53x (0x0003)
[00:00:00.278,564] <inf> bt_hci_core: No ID address. App must call settings_load()
[00:00:00.278,564] <inf> main: BLE ready
[00:00:00.279,876] <inf> bt_hci_core: HCI transport: IPC
[00:00:00.279,968] <inf> bt_hci_core: Identity: E8:54:F0:E0:D9:42 (random)
[00:00:00.279,998] <inf> bt_hci_core: HCI: version 5.4 (0x0d) revision 0x0000
[00:00:00.280,029] <inf> bt_hci_core: LMP: version 5.4 (0x0d) subver 0xffff
[00:00:00.281,280] <inf> main: settings_load() OK
[00:00:00.281,341] <inf> audio_volume: VCP ready (default vol=195)
[00:00:00.281,494] <inf> audio_i2s: I2S ready (48 kHz, 16-bit, stereo, 12 blocks)
[00:00:00.283,874] <inf> main: Advertising as "LE Audio Receiver"
```

All expected milestones present: BLE ready, settings_load() OK, I2S init,
advertising started. Shell prompt active at `uart:~$ `.

## What remains

5. Connect phone, pair to "LE Audio Receiver", stream audio, verify I2S
   output on CJMCU-1334 DAC.

## Also fixed (pre-existing bugs)

- src/audio_volume.h: added missing #include <stdbool.h>
- prj.conf: added CONFIG_REBOOT=y (needed by sys_reboot in main.c)
- prj.conf: removed duplicate CONFIG_REBOOT line (merge artifact)

## Also removed

- CMakeLists.txt: removed nrf5340dk overlay reference (DTC_OVERLAY_FILE append)
  since board DTS bakes in all pin assignments

## Key references

- Project repo: `le-audio-receiver` (branch: `board-porting`)
- NCS version: v3.3.0 at `~/ncs/v3.3.0/`
- Toolchain: `~/ncs/toolchains/911f4c5c26/`
- Build tree: `/tmp/build_e83/`
- Serial console: `/dev/ttyUSB0` (CH340X bridge), 115200 8N1
- Hardware docs: `~/Nextcloud/Development-Resources/le-audio/Present-Hardware/`
- Flashing docs: `docs/flashing.md`
- Flash TCL: `scripts/flash_nrf5340.tcl`
- Custom OpenOCD build: `nix/openocd-master.nix`
- Board definition: `boards/e83_2g4m03s_tb/`
- sysbuild config: `sysbuild.cmake` (hci_ipc BUILD_ONLY TRUE)
- sysbuild.conf: `SB_CONFIG_NETCORE_HCI_IPC=y`
- NCS board reference: `~/ncs/v3.3.0/zephyr/boards/nordic/nrf5340dk/`
