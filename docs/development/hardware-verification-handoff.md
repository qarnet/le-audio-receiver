# Hardware Verification Handoff — Phase 0/1 E83 + Xiao nRF54L15

Status: **blocked — needs different model/operator**
Date: 2026-07-05

## Context

Phases 0–1 of the LE Audio Receiver project are implemented (10 commits on
`main`, local only, not pushed). Both targets compile:
- nRF5340 (`ebyte_e83_nrf5340/nrf5340/cpuapp`) — builds, produces
  `build/nrf5340/merged.hex` + `merged_CPUNET.hex`
- nRF54L15 (`nrf54l15dk/nrf54l15/cpuapp`) — builds fully, links, produces
  `build/nrf54l15/merged.hex`

Hardware verification was attempted. Two issues found: one Phase 1 code bug
(fixed locally, uncommitted) and a deeper probe/APPROTECT recovery problem that
the previous model could not solve. This handoff covers both.

## Hardware setup (verified)

| Device | Probe serial | SWD target | Console port |
|--------|-------------|------------|--------------|
| Ebyte E83 nRF5340 | `554D45060B913E6A` (ACM0) | nRF5340 (Cortex-M4, DPIDR 0x2ba01477) | `/dev/ttyUSB0` (CH340X, 115200 8N1) |
| Seeed Xiao nRF54L15 | `E6635C08CB1F502B` (ACM2) | nRF54L15 (Cortex-M33, DPIDR 0x6ba02477) | `/dev/ttyACM1` (USB CDC) |

**Important**: The probes have been **swapped** since the 2026-05-31 board-porting
verification. `docs/STATUS.md` (on `origin/board-porting`) records
`E6635C08CB1F502B` as the E83's probe. It is now on the Xiao. `554D45060B913E6A`
is now on the E83. The `554D...` probe has CMSIS-DAP stability issues (see below).

`scripts/probe-serial.local` (gitignored) currently contains `554D45060B913E6A`.

## Uncommitted local changes (Phase 1 bug fix)

These changes are in the working tree but NOT committed. They fix a real Phase 1
bug found during the flash attempt. **Commit these regardless of the recovery
outcome.**

### The bug

`boards/ebyte/e83_nrf5340/board.cmake` referenced the flash TCL via
`${BOARD_DIR}/../../support/flash_nrf5340.tcl` (i.e. `boards/support/`). The
west OpenOCD runner (`~/ncs/v3.3.0/zephyr/scripts/west_commands/runners/openocd.py`
line 88) does:

```python
if path.exists(i) and not path.samefile(path.dirname(i), support):
```

where `support = path.join(cfg.board_dir, 'support')` =
`boards/ebyte/e83_nrf5340/support/`. `path.samefile()` raises
`FileNotFoundError` if the `support` directory doesn't exist. The runner crashes
with:

```
FileNotFoundError: [Errno 2] No such file or directory:
'/home/thomas-workstation/repos/le-audio-receiver/boards/ebyte/e83_nrf5340/support'
```

### The fix (already applied to working tree)

- Moved `boards/support/flash_nrf5340.tcl` →
  `boards/ebyte/e83_nrf5340/support/flash_nrf5340.tcl` (via `git mv`)
- Updated `board.cmake` paths: `${BOARD_DIR}/../../support/` → `${BOARD_DIR}/support/`
  (two occurrences — the `_PROBE_SERIAL` branch and the auto-detect branch)
- Updated `AGENTS.md` line 108: `boards/support/` → `boards/ebyte/e83_nrf5340/support/`
- Updated `docs/flashing.md` lines 41, 55, 109: `boards/support/` →
  `boards/ebyte/e83_nrf5340/support/`

### Suggested commit

```
fix: move flash TCL into board support dir for OpenOCD runner compatibility

The west OpenOCD runner (openocd.py) calls path.samefile(path.dirname(i),
<board_dir>/support) which raises FileNotFoundError if the board's support/
dir doesn't exist. Move flash_nrf5340.tcl from boards/support/ (project-level)
into boards/ebyte/e83_nrf5340/support/ (board-level) so the runner's
assumption holds. Update board.cmake and doc references accordingly.
```

## The recovery problem (blocking)

### Symptom

`fw-flash-5340` fails. The OpenOCD TCL `flash_west` proc flashes the app core
successfully but then fails on the cpunet:

```
Error: [nrf53.cpunet] Cortex-M CPUID: 0x1 is unrecognized
****** WARNING ******
[nrf53.cpuapp] device has AP lock engaged (see UICR APPROTECT register).
Error: [nrf53.cpunet] not examined
```

The `check_approtect` proc calls `nrf53_recover` which calls
`_nrf_ctrl_ap_recover 3 1` — this reads CTRL-AP #3 IDR and expects 0x12880000.
It gets 0x00000000 → "Cannot access nRF CTRL-AP!" → recovery fails.

### Diagnosis (verified by extensive probing)

1. **App core APPROTECT is OFF.** The app core (AP #0) examines successfully
   (Cortex-M4 r0p1), flash reads correctly (real data at 0x00000000), and the
   app core can be halted/resumed. The "AP lock engaged" warning is a **false
   positive** — `_nrf_check_ap_lock` reads CTRL-AP #3 APPROTECTSTATUS, gets 0
   (CTRL-AP absent), and `0 < 3` evaluates true → prints the warning. The app
   core is actually fine.

2. **Cpunet APPROTECT is ON.** AP #1 (net AHB-AP) exists (IDR=0x02880000) but
   reading cpunet memory returns garbage (CPUID=0x1). UICR.APPROTECT on both
   cores reads 0x00000000 (factory default = enabled on nRF5340 per
   `~/ncs/v3.3.0/nrf/doc/nrf/security/ap_protect.rst` line 34: "Devices ship
   with AP-Protect enabled"). The app core's firmware (SystemInit) disables
   APPROTECT in software at boot; the cpunet has no such firmware running
   (it's in APPROTECT-locked reset state).

3. **CTRL-APs are NOT visible at APSEL 2/3.** Full AP scan (APSEL 0-15) shows
   only AP #0 (IDR=0x24770011, AHB-AP app) and AP #1 (IDR=0x02880000, AHB-AP
   net). No CTRL-APs. This was tested with and without `SWD_MULTIDROP=1`.

4. **SWD multidrop does NOT work with this Pico probe.** Setting
   `SWD_MULTIDROP=1` + `_SWD_INSTANCE_ID=1` (net core DP) produces "Failed to
   connect multidrop nrf53.dap". The Pico CMSIS-DAP firmware v2.0.0 does not
   properly implement the TARGETSEL metacmd needed for SWDv2 multidrop. The
   OpenOCD cmsis-dap driver has `cmsis_dap_metacmd_targetsel()` but the Pico
   firmware doesn't handle it.

5. **The CTRL-APs likely require multidrop targeting.** On nRF5340, the CTRL-APs
   are on the core-specific DPs (app DP = instance 0, net DP = instance 1).
   Without multidrop, only the default DP (app, instance 0) is addressable, and
   its CTRL-AP may not be at APSEL 2 (the OpenOCD nrf53.cfg assumes it is). The
   net core's CTRL-AP is on the net DP (instance 1) which is unreachable.

6. **Pico CMSIS-DAP USB instability during flash writes.** Even flashing just
   the app core (which should work — APPROTECT is off on app) fails partway
   through with:
   ```
   Error: error handling USB events: System call interrupted
   Error: CMSIS-DAP command mismatch. Expected 0x5 received 0x6
   openocd: src/jtag/drivers/cmsis_dap.c:799: assertion failed
   ```
   This happens at both 100 kHz and 50 kHz adapter speed. The `554D...` Pico
   probe desyncs under USB load during flash write operations. Basic SWD
   operations (examine, halt, read memory) work fine — only large flash writes
   crash.

### What was NOT tried (potential paths forward)

1. **Swap the Pico probes back.** The 2026-05-31 STATUS.md (`origin/board-porting`)
   records `E6635C08CB1F502B` as the E83's probe — and it flashed successfully
   then. That probe is now on the Xiao. Swapping the probes physically (put
   `E6635...` back on the E83, `554D...` on the Xiao) might solve both the
   CTRL-AP access and the flash USB stability. The two Pico probes may have
   different firmware versions despite both reporting "FW Version = 2.0.0".

2. **Add J-Link support back to the flake.** The `origin/board-porting` branch
   commit `4572d25` added `pkgs.segger-jlink` + `pkgs.nrfutil` to the devShell
   specifically for `nrfutil device recover` via J-Link. Phase 0 removed both
   (replaced nixpkgs `nrfutil` with `nrfutil-core` minimal binary). If a J-Link
   is available, this is the Nordic-recommended recovery path:
   ```
   nrfutil device recover --core network    # recover cpunet first
   nrfutil device recover --core application # then app core
   ```
   Note: nrfutil only supports J-Link for recover, not CMSIS-DAP. No J-Link was
   detected by `nrfutil device list` or `lsusb` during this session.

3. **Update the Pico probe firmware.** A newer "Raspberry Pi Debug Probe"
   firmware (vs the older "Debugprobe on Pico") might handle SWD multidrop
   TARGETSEL and flash writes better. The Pico can be reflashed with the
   official debug probe firmware from
   https://github.com/raspberrypi/debugprobe/releases

4. **Try openocd with `SWD_MULTIDROP` on a probe that supports it.** The
   CTRL-APs may become visible at APSEL 2/3 once proper multidrop targeting
   works. This requires a probe with full CMSIS-DAP v2 SWDv2 support (J-Link,
   or a CMSIS-DAP probe with known multidrop support).

5. **Flash the Xiao nRF54L15 instead.** The Xiao uses probe `E6635C08CB1F502B`
   which may have better firmware. The nRF54L15 is single-core (no APPROTECT
   complication on a second core) and the Phase 1 build links successfully.
   The Xiao's OpenOCD config is at
   `~/ncs/v3.3.0/zephyr/boards/seeed/xiao_nrf54l15/support/openocd.cfg`.
   Flashing it would at least verify the Phase 1 nRF54L15 build runs on
   hardware. The Xiao board target is `nrf54l15dk/nrf54l15/cpuapp` and
   `fw-build-54l15` builds it into `build/nrf54l15/`.

### Key files / commands for the next attempt

```
# Probe serial file (gitignored, currently set for E83):
scripts/probe-serial.local  → 554D45060B913E6A

# Build + flash:
fw-build-5340                    # builds into build/nrf5340/
fw-flash-5340                    # west flash via OpenOCD runner

# Manual OpenOCD (for debugging probe issues):
openocd -f interface/cmsis-dap.cfg \
  -c "cmsis_dap_serial 554D45060B913E6A" \
  -c "transport select swd" \
  -f target/nordic/nrf53.cfg \
  -c "adapter speed 100" \
  -c "init" -c "targets" -c "shutdown"

# Scan all APSEL values:
# (see /tmp/opencode/scan_aps.tcl — may be deleted, recreate if needed)
# TCL: for {set i 0} {$i < 16} {incr i} { puts "AP$i: [$dap apreg $i 0xfc]" }

# Xiao nRF54L15 OpenOCD:
openocd -f interface/cmsis-dap.cfg \
  -c "cmsis_dap_serial E6635C08CB1F502B" \
  -f ~/ncs/v3.3.0/zephyr/boards/seeed/xiao_nrf54l15/support/openocd.cfg \
  -c "init" -c "targets" -c "shutdown"

# Serial reader helper (survives USB disconnect during reset):
python3 scripts/read_acm.py ttyUSB0 /tmp/e83.log 60  # E83 console
python3 scripts/read_acm.py ttyACM1 /tmp/xiao.log 60 # Xiao console

# nrfutil device list (shows all probes + serial ports):
nrfutil device list
```

### Reference docs / files

- `docs/design.md` — plan of record (Phases 0–6)
- `docs/flashing.md` — flash workflow (updated for Phase 1, includes the
  uncommitted TCL path fix)
- `docs/development/phase0-handoff.md` — Phase 0 handoff (completed)
- `docs/development/phase1-handoff.md` — Phase 1 handoff (completed)
- `~/ncs/v3.3.0/nrf/doc/nrf/security/ap_protect.rst` — NCS APPROTECT doc
- `~/Nextcloud/Development-Resources/le-audio/Reference-Hardware/CTRL-AP - Control access port.pdf`
  — nRF5340 CTRL-AP product spec (not yet read — could not extract PDF text)
- `~/Nextcloud/Development-Resources/le-audio/Present-Hardware/nRF5340_PS_reference.md`
  — nRF5340 PS reference (no CTRL-AP APSEL info)
- `origin/board-porting:STATUS.md` — 2026-05-31 verified flash record
- `origin/board-porting:flake.nix` — had `pkgs.segger-jlink` + `pkgs.nrfutil`
  for recovery (removed in Phase 0)
- `~/ncs/v3.3.0/zephyr/scripts/west_commands/runners/openocd.py` line 88 —
  the `path.samefile()` call that requires `<board_dir>/support/` to exist

### OpenOCD build

The project uses a custom OpenOCD built from master via `nix/openocd-master.nix`:
- Commit: `e6752ecbcf72efe4e213e8418e381ff2e0ffdf54` (openocd-org/openocd)
- Path: `/nix/store/6vbb2wflrkxh2z1b1ypragrgxpwadsf3-openocd-master-0.12.0/`
- The `nrf53.cfg` and `common.cfg` CTRL-AP recovery procs are in
  `share/openocd/scripts/target/nordic/` under that store path
- The `_nrf_ctrl_ap_recover` proc reads IDR with a single `apreg` call (no
  ADIv5 double-read discard) — confirmed identical to upstream

### What the user said about the recovery

> "We solved the nrf5340 recovery via openocd in the past. We used a nix flake
> to create commands that then automatically unlock the device via using
> openocd built from the main branch."

The openocd-master derivation IS present and unchanged from `origin/main`. The
recovery TCL (`nrf53_recover` → `_nrf_ctrl_ap_recover`) IS unchanged. The
difference is the **probe**: `554D...` (current) vs `E6635...` (2026-05-31).
The `E6635...` probe successfully flashed the E83 on 2026-05-31 without
APPROTECT issues (the device may not have been locked then, or that probe can
access CTRL-APs). Later, `4572d25` added J-Link support — suggesting APPROTECT
became an issue between 2026-05-31 and 2026-06-04, and J-Link was the recovery
method. Phase 0 removed J-Link.

### The user also said

> "Please also check the ~/Nextcloud development resources if you need any help
> with the cmsis-dap commands. There is a lot of documentation there"

The Nextcloud `Development-Resources/le-audio/` dir was explored. It contains:
- `Present-Hardware/` — E83 module/datasheets/schematics
- `Reference-Hardware/` — nRF5340 PS PDFs including "CTRL-AP - Control access
  port.pdf" and "Debug and trace.pdf" (not yet extracted — no pdftotext/markitdown
  available in the environment)
- `cmsis-toolbox/` — CMSIS tooling (not relevant to SWD recovery)
- No markdown docs mentioning CMSIS-DAP recovery, picoprobe, or openocd recover
  were found via grep.

The CTRL-AP PDF may contain the APSEL mapping that explains why AP #2/#3 return
0. It could not be extracted to text in this session. **Read it next.**

## What to do next (suggested order)

1. **Commit the Phase 1 TCL path fix** (uncommitted in working tree — see above).
2. **Read the CTRL-AP PDF** (`~/Nextcloud/.../CTRL-AP - Control access port.pdf`)
   to understand the APSEL mapping and whether CTRL-AP is accessible without
   multidrop.
3. **Try swapping the Pico probes** (put `E6635...` on E83, `554D...` on Xiao)
   and retry `fw-flash-5340`. This is the cheapest test.
4. **If swapping doesn't help, try flashing the Xiao nRF54L15** to at least
   verify the Phase 1 nRF54L15 build boots. Use `E6635...` probe + the Xiao
   OpenOCD cfg.
5. **If recovery is needed and no J-Link is available**, consider adding
   `pkgs.segger-jlink` + `pkgs.nrfutil` back to `flake.nix` (revert part of
   Phase 0) or finding a CMSIS-DAP probe with proper SWDv2 multidrop support.
6. **Once flashing works**, capture boot logs from `/dev/ttyUSB0` (E83) and
   `/dev/ttyACM1` (Xiao) using `scripts/read_acm.py`. Expected E83 boot:
   `BLE ready`, `settings_load() OK`, `Advertising as "LE Audio Receiver"`.
   The Xiao has no expected output yet (Phase 4 territory).

## No-phone constraint

The user confirmed: **no phone is available for BLE LE Audio testing**. The
computer itself + a USB Bluetooth adapter are available for future BlueZ-based
testing. **Do not suggest phone pairing/streaming tests.** Scrub any phone-test
mentions from plans going forward.