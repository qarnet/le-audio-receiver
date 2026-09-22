# PB-033 phase 2 handoff: XIAO nRF54L15 source firmware

Status: implementation handoff prepared 2026-09-22 after phase 1 acceptance at
commits `0c4bb3c` and `5b19e5c`.

## Goal

Port the existing dedicated LE Audio HIL source firmware to the second XIAO
nRF54L15 as a warning-clean, single-image, integrated-SDC build. Add explicit
build and flash helpers for that target while preserving the accepted nRF5340DK
source build, firmware protocol, scheduling behavior, and flash helper.

Phase 2 proves both source targets build, the nRF54L15 image has the exact
controller, clock, UART, RF-switch, flash-settings, and single-image shape, and
the new flash helper fails closed around one explicit CMSIS-DAP serial. Phase 2
does not flash hardware or integrate session manifests into row execution.

## In scope

- Split controller-time implementation by SoC while preserving one public
  `hil_source_controller_time_*` interface.
- Keep the existing nRF5340 RTC0/IPC4 mirror unchanged in its own platform file.
- Add nRF54L15 direct GRTC system-counter access with no private GRTC channel,
  compare, GPPI, IPC, or controller initialization.
- Guard the nRF5340-only 128 MHz clock-divider call; nRF54L15 uses its normal
  128 MHz HFPLL configuration with no replacement call.
- Add one source-app board conf and one minimal source-app overlay for the XIAO
  nRF54L15 built through `nrf54l15dk/nrf54l15/cpuapp`.
- Add separate `fw-build-hil-source-54l15` and
  `fw-flash-hil-source-54l15` helpers. Preserve existing helper behavior and
  paths byte-for-byte unless a test-only harness must learn the new helper.
- Extend host tests for exact build/flash argv and all fail-closed serial and
  artifact cases.
- Build both source targets and inspect resolved nRF54L15 configuration,
  devicetree, source selection, and artifact shape.
- Update PB-033 implementation notes with exact phase evidence.

## Out of scope

- No `scripts/hil/runner.py`, session API/schema, fixture schema, row, matrix,
  artifact package, RH4 archive, or release change.
- No replacement or removal of the nRF5340DK source path, `hci_ipc`, or its
  dual-image archive contract.
- No receiver firmware, receiver overlay, receiver build/flash helper, FLPR,
  I2S, audio, pairing, or product behavior change.
- No source protocol, HIL1 record, LC3 signal, BAP preset, TX timing constant,
  scheduler, counter, or firmware identity change.
- No hardware flash, reset, serial open, Bluetooth action, or physical claim.
- No 7.5 ms product acceptance, RH4/FR4 claim, or publication work.

## Grounding evidence

- Current source entry point raises only nRF5340 CPUAPP with
  `nrfx_clock_divider_set()` before initializing controller time:
  `hil/source/app/src/main.c`.
- Current controller-time implementation is nRF5340-specific RTC0 + IPC4 +
  GPPI and is also registered through `SYS_INIT`:
  `hil/source/app/src/hil_source_controller_time.c`.
- NCS v3.3.0 selects controller-time source by SoC in
  `nrf/samples/bluetooth/iso_time_sync/CMakeLists.txt`: nRF5340 uses
  `controller_time_nrf53_app.c`; nRF54L/H use `controller_time_nrf54.c`.
- NCS nRF54 implementation returns `nrfx_grtc_syscounter_get()` directly:
  `nrf/samples/bluetooth/iso_time_sync/src/controller_time_nrf54.c`.
- `nrfx_grtc_syscounter_get()` returns a `uint64_t`; `nrfx_grtc_ready_check()`
  documents that an unready SYSCOUNTER may be corrupt:
  `modules/hal/nordic/nrfx/drivers/include/nrfx_grtc.h`.
- Zephyr's nRF54 GRTC system timer selects `NRFX_GRTC`, initializes the driver
  at EARLY priority, and nRF54L defaults `NRF_GRTC_START_SYSCOUNTER=y`:
  `zephyr/drivers/timer/Kconfig.nrf_grtc`,
  `zephyr/drivers/timer/nrf_grtc_timer.c`, and
  `zephyr/soc/nordic/nrf54l/Kconfig.defconfig`.
- nRF54L cpuapp runs from the 128 MHz HFPLL setup; no nRF5340 divider call is
  needed. Verified paths:
  `zephyr/dts/vendor/nordic/nrf54l_05_10_15.dtsi`,
  `zephyr/modules/hal_nordic/nrfx/CMakeLists.txt`, and
  `modules/hal/nordic/nrfx/bsp/stable/mdk/system_nrf54l.c`.
- Current TX timestamp APIs remain supported with integrated SDC:
  `nrf/include/bluetooth/hci_vs_sdc.h` declares
  `hci_vs_sdc_iso_read_tx_timestamp()` and the Nordic ISO time-sync sample uses
  it. `<bluetooth/hci_vs_sdc.h>` already supplies required SDC VS types.
- Official XIAO sources pin UART20 to P1.9/P1.8, RF switch control to P2.5
  active-low and P2.3 active-high, and both oscillators to 16 pF:
  `zephyr/boards/seeed/xiao_nrf54l15/`.
- Stock nRF54L15DK button1/button2 use P1.9/P1.8, and its external SPI flash
  chip-select uses P2.5. Source overlay must disable those stock consumers.
- Existing nRF54L15 receiver OpenOCD helper proves the XIAO RRAM sequence:
  explicit adapter serial, XIAO OpenOCD config, `reset halt`, `nrf54l-load`,
  `verify_image`, `reset run`, `shutdown`.

## Exact implementation decisions

### 1. Platform controller-time split

Touch `hil/source/app/CMakeLists.txt`.

Rename:

```text
hil/source/app/src/hil_source_controller_time.c
  -> hil/source/app/src/hil_source_controller_time_nrf53_app.c
```

Do not alter nRF5340 implementation logic during the move.

Add `hil/source/app/src/hil_source_controller_time_nrf54.c` and select exactly
one implementation after `find_package(Zephyr ...)`:

```cmake
if(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
  set(HIL_SOURCE_CONTROLLER_TIME_SOURCE
      src/hil_source_controller_time_nrf53_app.c)
elseif(CONFIG_SOC_SERIES_NRF54L)
  set(HIL_SOURCE_CONTROLLER_TIME_SOURCE
      src/hil_source_controller_time_nrf54.c)
else()
  message(FATAL_ERROR "Unsupported HIL source controller-time platform")
endif()
```

Use `${HIL_SOURCE_CONTROLLER_TIME_SOURCE}` in `target_sources` instead of a
fixed controller-time path. Update top comments from nRF5340-only wording to
dual-target wording. Keep compile-database link behavior unchanged.

Implement nRF54 file with these exact semantics:

```c
#include <errno.h>
#include <stdint.h>

#include <nrfx_grtc.h>

#include "hil_source_controller_time.h"

int hil_source_controller_time_init(void)
{
	return nrfx_grtc_init_check() ? 0 : -ENODEV;
}

int hil_source_controller_time_get(uint32_t *time_us)
{
	if (time_us == NULL) {
		return -EINVAL;
	}
	if (!nrfx_grtc_init_check()) {
		return -ENODEV;
	}
	if (!nrfx_grtc_ready_check()) {
		return -EAGAIN;
	}

	*time_us = (uint32_t)nrfx_grtc_syscounter_get();
	return 0;
}
```

The cast deliberately preserves the existing modulo-`2^32` microsecond API
used by signed-delta scheduling. Do not allocate a GRTC channel and do not add
`SYS_INIT`; Zephyr owns GRTC setup.

Update `hil_source_controller_time.h` comments to describe a platform controller
clock, modulo-`2^32` microseconds, `-EAGAIN` while synchronization/counter is not
ready, `-ENODEV` when platform clock infrastructure is unavailable, and the
nRF5340-specific `-EIO` epoch-fault result. Do not change function signatures.

Touch `hil/source/app/src/main.c`:

- Include `<nrfx_clock.h>` only under
  `CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP`.
- Guard only the current divider call and its fatal status with that same
  symbol.
- Keep output, controller-time, BAP, and app initialization order unchanged.

Touch `hil/source/app/src/hil_source_tx.c` only to remove redundant direct
`#include <sdc_hci.h>`. Keep TX functions and SDC command unchanged.

### 2. nRF54L15 source board configuration

Add:

```text
hil/source/app/boards/nrf54l15dk_nrf54l15_cpuapp.conf
```

Use these exact assignments:

```conf
CONFIG_REGULATOR=y
CONFIG_REGULATOR_FIXED_INIT_PRIORITY=45

CONFIG_BT_BUF_EVT_RX_SIZE=255
CONFIG_BT_BUF_ACL_RX_SIZE=255
CONFIG_BT_BUF_ACL_TX_SIZE=251
CONFIG_BT_BUF_CMD_TX_SIZE=255

CONFIG_BT_LL_SOFTDEVICE=y
CONFIG_BT_MAX_CONN=1
CONFIG_BT_CTLR_CENTRAL_ISO=y
CONFIG_BT_CTLR_PERIPHERAL_ISO=n
CONFIG_BT_CTLR_CONN_ISO_GROUPS=1
CONFIG_BT_CTLR_CONN_ISO_STREAMS=2
CONFIG_BT_CTLR_CONN_ISO_STREAMS_PER_GROUP=2
CONFIG_BT_CTLR_SDC_PERIPHERAL_COUNT=0
CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=6
CONFIG_BT_CTLR_SDC_ISO_TX_PDU_BUFFER_PER_STREAM_COUNT=6
CONFIG_BT_CTLR_TX_PWR_PLUS_3=y
```

Do not copy nRF5340 `NRFX_RTC`, `NRFX_GPPI`, MPSL IPC-start trigger, CPUNET,
silent-netcore settings, or `BT_CTLR_ASSERT_HANDLER`. Integrated SDC must use
NCS `hci_driver.c`'s default assertion handler. With common `CONFIG_ASSERT=y`,
that handler emits the controller assertion and enters Zephyr fatal handling;
enabling the custom-handler symbol would require an application-owned
`bt_ctlr_assert_handle()` and adds no source-fixture behavior required here.

Touch common `hil/source/app/prj.conf` to add
`CONFIG_FLASH_PAGE_LAYOUT=y` beside the other explicit flash/ZMS dependencies.
Do not move common settings into target confs.

### 3. Minimal source-only XIAO overlay

Add:

```text
hil/source/app/boards/nrf54l15dk_nrf54l15_cpuapp.overlay
```

This is source-specific. Do not include or modify repository root receiver
overlay.

Overlay requirements:

1. Root fixed regulators:
   - `rfsw_ctl`: P2.5, `GPIO_ACTIVE_LOW`, `regulator-boot-on`.
   - `rfsw_pwr`: P2.3, `GPIO_ACTIVE_HIGH`, `regulator-boot-on`.
2. Add uniquely named pinctrl nodes (do not reuse inherited stock labels):
   - `xiao_uart20_default`: TX P1.9, RX P1.8 with pull-up.
   - `xiao_uart20_sleep`: TX P1.9 and RX P1.8 with low-power enabled.
3. Point `&uart20` `pinctrl-0` and `pinctrl-1` only at those unique nodes.
4. Disable `&button0`, `&button1`, `&button2`, and `&button3`; source fixture
   has no physical input contract and button1/button2 collide with UART.
5. Disable `&mx25r64` and `&spi00`; stock flash CS collides with RF switch
   control and XIAO has no external SPI NOR.
6. Set `&lfxo` and `&hfxo` to internal 16000 fF load and status `okay`.

Do not add receiver I2S, PDM, TIMER20, FLPR IPC, reserved memory, aliases,
buttons, LEDs, ADC, IMU, battery, or user-pairing nodes.

### 4. Separate build helper

Add executable `scripts/bin/fw-build-hil-source-54l15` using `fw-common.sh` and
the same environment guard as existing helpers.

Exact behavior:

- Build directory: `build/hil-source-nrf54l15`.
- Board: `nrf54l15dk/nrf54l15/cpuapp`.
- Source: `hil/source/app`.
- Force ordinary single-image build with `--no-sysbuild`. NCS west may choose
  sysbuild by default when this source tree contains `Kconfig.sysbuild`, so
  merely omitting `--sysbuild` does not establish this contract.
- Always `--pristine`.
- Forward all caller CMake arguments after `--`.

Expected argv shape:

```text
west build -b nrf54l15dk/nrf54l15/cpuapp --no-sysbuild --pristine \
  -d <repo>/build/hil-source-nrf54l15 hil/source/app -- <caller args>
```

Result is one nonempty
`build/hil-source-nrf54l15/zephyr/zephyr.hex`. There must be no `hci_ipc`,
CPUNET hex, or `domains.yaml`.

Do not change `fw-build-hil-source`.

### 5. Separate explicit-serial flash helper

Add executable `scripts/bin/fw-flash-hil-source-54l15` using `fw-common.sh` and
the same environment guard as existing helpers.

Contract:

- Require nonempty `FW_HIL_SOURCE_NRF54L15_PROBE_SERIAL` matching
  `^[[:alnum:]_.:-]+$`; no automatic discovery fallback.
- Require nonempty regular non-symlink
  `build/hil-source-nrf54l15/zephyr/zephyr.hex` before OpenOCD.
- Use
  `$ZEPHYR_BASE/boards/seeed/xiao_nrf54l15/support/openocd.cfg`.
- Build argv as an array where applicable; preserve each `-c` command as one
  argv element.
- Exact OpenOCD order:

```text
-c "adapter serial <serial>"
-f <XIAO openocd.cfg>
-c init
-c "reset halt"
-c "nrf54l-load <source hex>"
-c "verify_image <source hex>"
-c "reset run"
-c shutdown
```

- Propagate OpenOCD status.
- No erase, recover, mass erase, line control, second image, artifact override,
  or `nrf-probes`/`nix-nrf probes` call.

Do not change `fw-flash-hil-source` or `fw-flash-54l15` in this phase.

### 6. Host tests

Touch `tests/hil/rh2_test.py` only as needed.

Extend existing helper harness rather than adding a second framework:

- Copy both new helpers into fake repository.
- Make fake `west` optionally retain NUL-separated argv.
- Add build-helper test for exact board, explicit `--no-sysbuild`, pristine
  mode, build directory, source path, and caller-argument forwarding.
- Add flash-helper happy-path test proving exact one-image OpenOCD argv and
  explicit serial.
- Add flash-helper tests for missing serial, unsafe serial, missing/empty or
  symlink image, missing dev shell, and nonzero OpenOCD status. Every validation
  failure must occur before fake OpenOCD writes argv.
- Assert helper never calls an identity-discovery executable.
- Keep all existing nRF5340 source and receiver helper tests unchanged and
  passing.

Test observable subprocess behavior and argv, not shell implementation details.

## Verification

Run from repository root. Retain build logs outside repository, inspect full
output, and treat every compiler warning, Kconfig assignment warning, CMake
warning, or unexpected diagnostic as a failure. Existing documented NCS
sysbuild informational/deprecation diagnostics from the unchanged nRF5340 path
may be recorded separately; they do not relax the warning-clean nRF54L15 gate.

```bash
nix develop -c python3 tests/hil/rh2_test.py
nix develop -c python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil
nix develop -c fw-build-hil-source-54l15
nix develop -c fw-build-hil-source
nix develop -c backlog doctor
git diff --check
```

After builds, verify:

1. `build/hil-source-nrf54l15/zephyr/zephyr.hex` and `.elf` are nonempty.
2. `build/hil-source-nrf54l15/domains.yaml` and
   `build/hil-source-nrf54l15/hci_ipc/` do not exist.
3. `build/hil-source-nrf54l15/build.ninja` contains only
   `hil_source_controller_time_nrf54.c`; legacy
   `build/hil-source/app/build.ninja` contains only
   `hil_source_controller_time_nrf53_app.c`.
4. Parse nRF54L15 `.config` and require these exact values:
   - `BT_LL_SOFTDEVICE=y`
   - `BT_CTLR_CENTRAL_ISO=y`
   - `BT_CTLR_CONN_ISO_GROUPS=1`
   - `BT_CTLR_CONN_ISO_STREAMS=2`
   - `BT_CTLR_CONN_ISO_STREAMS_PER_GROUP=2`
   - `BT_CTLR_SDC_PERIPHERAL_COUNT=0`
   - `BT_ISO_TX_BUF_COUNT=6`
   - `BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=6`
   - `BT_CTLR_SDC_ISO_TX_PDU_BUFFER_PER_STREAM_COUNT=6`
   - `NRFX_GRTC=y`
   - `NRF_GRTC_START_SYSCOUNTER=y`
   - `REGULATOR=y`
   - `FLASH_PAGE_LAYOUT=y`
5. Require nRF54L15 `.config` not to enable `NRFX_RTC`,
   `MPSL_TRIGGER_IPC_TASK_ON_RTC_START`, or `BT_LL_SW_SPLIT`.
6. Inspect resolved `zephyr.dts`: UART20 uses only XIAO P1.9/P1.8 pinctrl;
   both RF fixed-regulator nodes are enabled at P2.5/P2.3 with correct polarity;
   SPI00 and MX25R64 are disabled; LFXO/HFXO load is 16000 fF; no receiver I2S,
   FLPR IPC, or source-owned TIMER node was introduced.
7. Existing nRF5340 source still produces nonempty app and CPUNET hex images.

No physical flash or serial test belongs to phase 2.

## Files allowed

- `hil/source/app/CMakeLists.txt`
- `hil/source/app/prj.conf`
- `hil/source/app/src/main.c`
- `hil/source/app/src/hil_source_controller_time.h`
- `hil/source/app/src/hil_source_controller_time.c` only as rename source
- `hil/source/app/src/hil_source_controller_time_nrf53_app.c` (renamed)
- `hil/source/app/src/hil_source_controller_time_nrf54.c` (new)
- `hil/source/app/src/hil_source_tx.c`
- `hil/source/app/boards/nrf54l15dk_nrf54l15_cpuapp.conf` (new)
- `hil/source/app/boards/nrf54l15dk_nrf54l15_cpuapp.overlay` (new)
- `scripts/bin/fw-build-hil-source-54l15` (new)
- `scripts/bin/fw-flash-hil-source-54l15` (new)
- `tests/hil/rh2_test.py`
- PB-033 task file for notes
- this handoff only if correction is required

Do not touch runner/session integration, rows, fixtures, source protocol logic,
receiver code, root receiver overlay/conf, release/artifact code, accepted
historical evidence, or unrelated tests.

## Commit and escalation

After verification, inspect status, complete diff, and recent log. Stage only
allowed files and commit phase 2 with concise repository-style message
containing `PB-033`. Do not push, open/merge PR, amend, or add AI/tool
attribution.

Return files changed; public behavior; exact test/build/config/DTS results;
warning audit; commit hash/message; deviations; blockers; and recommended phase
3 follow-up.

Stop without committing partial work and report to Delegator if two materially
different fixes fail, repository/NCS/tool evidence contradicts this design,
warning-free build requires suppressing or tolerating a new warning, source
protocol/timing behavior would need to change, scope must expand, assertions
would need weakening, or any target-changing command appears necessary.

## Implementation correction

Initial nRF54L15 link evidence showed `CONFIG_BT_CTLR_ASSERT_HANDLER=y` requires
an external application function `bt_ctlr_assert_handle()` through
`nrf/subsys/bluetooth/controller/hci_driver.c`. That callback is appropriate
for HCI transport applications that forward a vendor fatal event, but this
integrated host/controller fixture has no such boundary. Do not add a custom
handler. Remove that assignment and retain common `CONFIG_ASSERT=y`, which
selects the NCS default SDC assertion path and fails fatally with file/line
diagnostics. This correction does not weaken assertion handling or change HIL1
protocol behavior.

Initial successful link evidence also showed that omitting `--sysbuild` did not
produce an ordinary build: west selected `sysbuild_default` because this source
tree contains `Kconfig.sysbuild`, producing `app/zephyr/`, `domains.yaml`, and a
merged image. The build helper must pass explicit `--no-sysbuild`; do not accept
or adapt to the accidental one-domain sysbuild artifact shape.
