# nRF Connect SDK — Knowledge Lookup Rules

The nRF Connect SDK is installed at `~/ncs/`. Resolve the exact version with:
  `ls -d ~/ncs/v*/ | sort -V | tail -1`

Treat the installed source tree as the authoritative reference. Do NOT guess
at Kconfig symbols, devicetree compatibles, or API signatures — grep the
source. The web docs are a JavaScript SPA and cannot be fetched.

## Kconfig discovery

Workflow when you need a Kconfig symbol:
  1. Grep the definition (NOT just usages):
       `grep -rn "^config FOO\b" ~/ncs/v*/nrf ~/ncs/v*/zephyr ~/ncs/v*/modules`
  2. View the surrounding Kconfig block to read deps and help text.
  3. If searching by topic, grep Kconfig* files for keywords:
       `grep -rn -i "lte modem" ~/ncs/v*/nrf --include="Kconfig*"`

Prefer the resolved config for a built project:
  `build/zephyr/.config` — final merged config (post-Kconfig)
  `build/zephyr/include/generated/zephyr/autoconf.h`
These show what is ACTUALLY enabled, vs. what is merely declared.

## Devicetree

Bindings live at `~/ncs/v*/zephyr/dts/bindings/` (upstream) and
`~/ncs/v*/nrf/dts/bindings/` (Nordic-specific).

For a built project, the resolved DT is at:
  `build/zephyr/zephyr.dts`
  `build/zephyr/include/generated/zephyr/devicetree_generated.h`

## Headers / APIs

Public Zephyr headers:  `~/ncs/v*/zephyr/include/zephyr/`
Public Nordic headers:  `~/ncs/v*/nrf/include/`

When asked "how do I use X", grep the header for the function declaration
and read the surrounding `/** ... */` Doxygen block.

## Samples

Nordic samples are the best learning resource:
  `~/ncs/v*/nrf/samples/`
  `~/ncs/v*/zephyr/samples/`

## What NOT to do

- Don't fabricate Kconfig symbol names. If you cannot grep it, say so.
- Don't web-search for nRF Connect SDK docs — fetches fail. Use the local tree.
- Don't suggest API calls without verifying the function exists in a header.

---

# AGENTS.md — LE Audio Receiver (nRF5340 + nRF54L15)

## Policy — never ignore warnings

Compiler warnings and Kconfig "assigned value but got" warnings are hard errors:
fix the source or suppress with a recorded reason. Boot-time `LOG_WRN` and
openocd/flashing warnings are treated the same — don't normalize noise.

NCS v3.3.0 emits diagnostics that are NOT actionable at repo level:
deprecation notices (`PARTITION_MANAGER`, sysbuild), informational config
messages (`__ASSERT()`), experimental-symbol notices required for ISO on
nRF5340 (BT_LL_SW_SPLIT, PERIPHERAL_ISO), upstream Kconfig gaps (SW Split
`CONN_ISO_LOW_LATENCY_POLICY` choice has no NONE fallback), and CMake
"No SOURCES given" where a subsystem is enabled but no driver exists for
a particular board (e.g. watchdog on nRF54L15 — wdt30/wdt31 are disabled
in DT when SDC is active, so `CONFIG_WATCHDOG=y` from prj.conf creates an
empty library; not fixable without an unsupported DT node or losing
watchdog on nRF5340). These are documented in `STATUS.md` "Build warning
diagnostics", not tolerated as warnings.

## Style rule: no em dashes in user-facing documentation

User-facing documentation must not contain the Unicode em dash (U+2014).
Rewrite em dashes with commas, parentheses, colons, semicolons, or separate
sentences, preserving meaning and formatting (links, tables, code spans,
numeric ranges, and warning strength).

User-facing scope: `README.md`, `PLANNED_FEATURES.md`, and the public docs
listed in the README Documentation table (`docs/user-guide.md`,
`docs/supported-sources.md`, `docs/linux-le-audio-host-setup.md`,
`docs/bluetooth-adapter-evaluation.md`, `docs/hardware-wiring.md`,
`docs/known-limitations.md`, `docs/technology/nrf5340.md`,
`docs/technology/nrf54l15.md`, `docs/flashing.md`), plus
`release/flashing/*.md`.

Internal and historical contributor docs (for example `docs/development/`,
`docs/testing/`, `STATUS.md`) are outside this style rule unless explicitly
requested.

## Plan of record

`docs/development/refactor-plan.md` is the accepted plan of record for the
current refactoring track R0–R10. Read it before structural changes.
`docs/design.md` remains the historical architecture and evidence document,
not the active structural plan.

Current status: **canonical gate 65 PASS / 0 FAIL / 65 TOTAL** on the
clean tree (35 twister + 5 exec-only + 22 Python + coverage + matrix +
BSim; the FR2 clean-tree run at `75a8093`, the FR1 clean run at
`1671a9f`, and earlier clean runs recorded in
`docs/development/documentation-hygiene-behavior-fix-results.md` at
`b8bd633` and the production-fix canonical run at `f2f9336`, after the
empty-SDU concealment (`9dc0859`) and 11-block startup reservoir
(`f2f9336`) fixes — the committed coverage baseline is unchanged),
coverage population **36** (4777/5234 lines, 2091/2896
branches, 363/363 functions, gcovr 8.4 / gcov (GCC) 14.3.0, committed
baseline unchanged), builds 3/3, build contract **96/96**, BSim Stage 1
pins byte-identical, P1–P8 user pairing control ACCEPTED (nRF54L15
enabled, nRF5340 feature-off), FR1 deterministic firmware packager
ACCEPTED, FR2 firmware-build CI ACCEPTED (hosted run 31326612845
PASS; workflow artifacts only, no tag/release/hardware acceptance), and
FR3 automatic draft-release creation ACCEPTED (final merged hosted run
`b70b978` PASS with release SKIPPED on unchanged `VERSION`).  FR4
exact-artifact hardware acceptance is **BLOCKED**: the exact draft
`v0.1.0` FAILED mandatory nRF5340 mono acceptance and remains private,
unpublished, and untagged; the local replacement preflight passed both
targets at `5e7f502` but is not exact-artifact acceptance; a replacement
candidate must be created through the trusted-main lifecycle and its
exact assets must pass FR4 before FR5 can publish anything; nothing
published.  Historical baselines: T0–T8 locked
behavior on production code `971e6a4` (T8 canonical gate **47 PASS /
0 FAIL / 47 TOTAL**, coverage baseline `1a5842d` (26 files), build
contract 76/76 — `docs/testing/pre-refactor-hardware-baseline.md`); the
R0–R10 refactor track closed 2026-08-06 with gate **55 PASS / 0 FAIL /
55 TOTAL** (31 twister + 5 exec-only + 16 Python + coverage + matrix +
BSim), coverage population **33** (4024/4402 lines, 1695/2356 branches,
289/289 functions, committed baseline `54a6b8e`), build contract
**79/79**, and the full R10 hardware matrix passed on
both targets.  R8/R9 acceptance details and the final evidence:
`docs/development/refactor-r10-results.md`, `refactor-r9-results.md`,
`refactor-r8-results.md`, and `STATUS.md`.
**BabbleSim Stage 1 is an accepted regular local gate** — the
**17-scenario** T4+R7 BAP matrix via `scripts/bsim-stage1-run.sh`
(scenarios 1–9 run twice, 10–17 once = 26 runs), strict PCM oracle,
deterministic across runs (mono 10 ms `0x22AB5C0D`, Mode A/B 10 ms
`0xBAE24F7E`, reconnect = fresh mono oracle, `duplicate_release_10ms`).
Official upstream smoke remains PARTIAL (documented upstream teardown
disable-race) and is **not** production acceptance.

Known behavior question (see `STATUS.md`): nRF54L15 360-frame (7.5 ms) calls
fall back to cpuapp ASRC because the FLPR payload contract is 480 frames
(`FLPR_RING_PAYLOAD_MAX_INPUT == 480U` in `src/flpr_ring.h`).  Witnesses:
`tests/unit/audio_i2s` `test_offload_reject_360_input_falls_back` pins the
exact 360-frame caller fallback; `tests/unit/audio_offload`
`test_asrc_invalid_frames` pins general non-480 rejection (its current
concrete input is 240); `tests/unit/flpr_ring` MAX_INPUT assertions pin the
480 contract.  Not a new failure and not permission to implement 360-frame
offload.

Consequences for work in this repo today:

- Every change must keep the nRF5340 target building, flashing, streaming.

## Central-only test rule

All agents run the LE Audio stream autonomously via the nRF5340DK `hci_uart`
central attached to Linux as `hci0` (over `/dev/ttyACM2` at 1 000 000 baud H4
with flow control). Use `scripts/bap_central.py` to connect to the receiver
and stream LC3 audio. No human-operated central is allowed in any test
procedure.

The only allowed user input is a true physical observation that an agent
cannot make: whether sound is audible from connected speakers/headphones
after the agent has completed its test run.

### Central setup (required before every test session)

The nRF5340DK `hci_uart` central attaches to the kernel via `btattach`.
Run this BEFORE `scripts/bap_central.py`:

```bash
# Attach the HCI UART dongle (nRF5340DK as central) — one-time per boot:
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 </dev/null >/tmp/btattach.log 2>&1 &
sleep 5
sudo btmgmt --index hci0 power off
sudo btmgmt --index hci0 power on
sleep 2
sudo btmgmt --index hci0 io-cap 3
sudo btmgmt --index hci0 sc on
```

Verify with `sudo btmgmt --index hci0 info`. Current settings must include
`powered le secure-conn cis-central`. The dongle's BD_ADDR must be
`C0:AA:BB:CC:DD:EE` (compile-time identity — see dongle firmware fix below).

**Dongle firmware compile-time identity (Stage0)**:
The nRF5340DK FICR DEVICEADDR is unprogrammed (all zeros). Instead of
the runtime `btmgmt static-addr` workaround, the hci_ipc netcore firmware
now calls `bt_ctlr_set_public_addr()` before `bt_enable_raw()` via a
repo-owned copy of the hci_ipc sample (`dongle/hci_ipc/`). The address
`C0:AA:BB:CC:DD:EE` is defined in `dongle/hci_identity.h` (lab-only,
not a production-assigned OUI). Build with `fw-build-dongle`.

### --peer-addr bypass

When the dongle cannot scan, pass the receiver's BLE address directly:

```bash
# Get receiver address from boot log: "Identity: XX:XX:XX:XX:XX:XX (random)"
python3 scripts/bap_central.py --peer-addr DB:A6:0C:05:A2:AA --duration 30
```

This skips BlueZ discovery, creates the device via brief raw-HCI connect,
and calls `device.Pair()` to establish the bond + encrypted link.

Then run `bap_central.py` **without sudo** — the main script needs
dbus-python from the nix-shell (Python path stripped by sudo).  Only the
raw-HCI connect subprocess uses sudo internally.

```bash
python3 scripts/bap_central.py --duration 30   # Mode A (default)
python3 scripts/bap_central.py --stereo --duration 30  # --stereo flag
```

## Build

Build **from the repo root**. Enter the dev shell first, then run the build
helper:

```bash
cd <repo>
direnv allow         # or: nix develop
fw-build-5340
```

The build runs `west build -b ebyte_e83_nrf5340/nrf5340/cpuapp --sysbuild --pristine`
into `build/nrf5340/`. Sysbuild produces images under `build/nrf5340/le-audio-receiver/`
(app) and `build/nrf5340/hci_ipc/` (net core); top-level merged hexes are
`build/nrf5340/merged.hex` and `build/nrf5340/merged_CPUNET.hex`. Use
`--pristine` after any `prj.conf`, overlay, or `sysbuild.cmake` change.
Pass extra cmake args through:

```bash
fw-build-5340 -- -DCONFIG_FOO=y
```

The nRF54L15 target builds with `fw-build-54l15` into `build/nrf54l15/`
(Phase 1) and flashes with `fw-flash-54l15` (OpenOCD via the Xiao's
built-in CMSIS-DAP; probe auto-detected by target identity). nRF54L15
RRAM needs no flash driver — with RRAMC write-enable (`mww 0x5004b500
0x101`) it is plain writable memory, so `load_image` + `verify_image`
suffice. **FLPR firmware flashes the same way**: the FLPR code partition
is a RRAM slice at `0x165000` in the app core address space (verified by
write/read-back with both OpenOCD and probe-rs) — relevant for Phase 6
FLPR offload. The build targets the stock `nrf54l15dk` board + a small
overlay (`boards/nrf54l15dk_nrf54l15_cpuapp.overlay`) that remaps UART20
to the Xiao SAMD11 USB CDC bridge (P1.9 TX / P1.8 RX) and I2S20 to Xiao
D0/D1/D2 (P1.4/P1.5/P1.6). Console works over `/dev/ttyACM0` @ 115200.

## Flash

Both app core and hci_ipc network core must be flashed:

```bash
fw-flash-5340
```

The OpenOCD runner config in `boards/ebyte/e83_nrf5340/board.cmake` chains the
dual-core flash TCL (`boards/ebyte/e83_nrf5340/support/flash_nrf5340.tcl`).
The probe is resolved **at flash time** (see "Probe identification" below) —
no serial is baked into the build.

## Serial

App core (E83) console: **`/dev/ttyUSB0`** (CH340X bridge) at **115200 8N1**.
The picoprobe's own CDC ports (`/dev/ttyACM*`) are NOT the nRF5340 console.

```bash
stty -F /dev/ttyUSB0 115200 raw -echo && cat /dev/ttyUSB0
```

nRF54L15 (Xiao) console: **`/dev/ttyACM0`** @ 115200 8N1 (SAMD11 USB CDC
bridge of UART20). Use serial-mcp or `scripts/read_acm.py ttyACM0`.

Expected after boot on either target: `BLE ready`, `settings_load() OK`,
`Advertising as "LE Audio Receiver"`. During streaming,
`i2s_nrfx: Next buffers not supplied on time` should no longer occur
in steady-state once the PI clock recovery controller converges
(Phase 3). Recovery is automatic (`TRIGGER_PREPARE` + re-arm) if
transient underruns happen.

### Probe identification — NEVER assume the probe↔board mapping

Probes get replugged; documentation rots. `nrf-probes` (provided on PATH by
the [nix-nrf-dev](https://github.com/qarnet/nix-nrf-dev) flake, along with
openocd-master and the NCS toolchain shell) is the source of truth:

```bash
nrf-probes            # table: probe serial → chip behind it (read-only)
nrf-probes --find nrf53   # serial of the probe wired to an nRF53
```

It fingerprints each CMSIS-DAP probe's target over SWD (DPIDR → AP IDR map →
FICR INFO.PART/VARIANT) and works even when the chip is APPROTECT-locked
(identity from the DP/AP signature). `fw-flash-5340` calls it automatically
to pick the right probe at flash time.

`scripts/probe-serial.local` (gitignored) is now only a manual **override**
for when auto-detection must be bypassed. Normally it should not exist.

**Doc hygiene rule:** never write a static probe-serial↔board table into
docs or handoffs — reference `nrf-probes` instead. Any hardware-identity
claim in a handoff MUST include the raw evidence it rests on (DPIDR, AP IDR
map, FICR PART value), not just the conclusion. A 2026-07-05 session lost a
day chasing a phantom APPROTECT problem because a handoff asserted an
inverted probe mapping without evidence.

### Capturing boot logs during testing

The console must be captured **before** the device resets. The
`scripts/read_acm.py` helper (pyserial + auto-reopen) survives USB
disconnects during reset:

```bash
# Start the reader first (E83 console = ttyUSB0), THEN reset via OpenOCD
python3 scripts/read_acm.py ttyUSB0 /tmp/e83.log 30 &
sleep 2
openocd -f interface/cmsis-dap.cfg \
  -c "adapter serial $(nrf-probes --find nrf53)" \
  -c "transport select swd" -c "adapter speed 1000" \
  -f target/nordic/nrf53.cfg -c init -c "reset run" -c shutdown
```

Always reset **after** the reader has opened the port.

## Gotchas

### SW Split LL requires BOTH a DT overlay AND a Kconfig overlay

`add_overlay_config()` alone sets Kconfig, but the nRF5340 cpunet DTS
defaults to `bt_hci_sdc` (SoftDevice). Without `add_overlay_dts(...,
bt-ll-sw-split.overlay)` the net core quietly stays on SoftDevice and
`bt_enable()` fails with `Bluetooth init failed: -5`
(`HOST_BUFFER_SIZE` returns `UNSUPPORTED_FEATURE`).

See `sysbuild.cmake` for how both overlays are applied to `hci_ipc`.

### `settings_load()` must run after `bt_enable()` and before `bt_pacs_register()`

`CONFIG_BT_GATT_DYNAMIC_DB=y` registers PACS/ASCS dynamically. Without
`settings_load()` these characteristics are invisible to remote peers.
The call must be after `bt_enable(NULL)` and before `bt_pacs_register()`.
**Do NOT skip `settings_load()`** to "clear bonds" — it will break PACS registration.

### `west flash` does NOT erase the settings partition

`west flash` only erases the firmware address ranges. The ZMS settings
partition (bonds, PACS registered handles) persists across flashes.
If you suspect a stale bond or corrupted settings, mass-erase via the
CTRL-AP with the openocd-master build (no J-Link needed), then reflash:

```bash
openocd -f interface/cmsis-dap.cfg -c "adapter serial $(nrf-probes --find nrf53)" \
  -c "transport select swd" -c "adapter speed 1000" -f target/nordic/nrf53.cfg \
  -c init -c nrf53_recover -c shutdown
fw-flash-5340
```

`nrf53_recover` wipes ALL non-volatile memory (both cores, incl. UICR and
settings). `fw-flash-5340` afterwards re-programs UICR.APPROTECT (see the
APPROTECT gotcha below), so the chip stays debuggable.

### nRF5340 APPROTECT is a SOFT branch — an erased UICR bricks debug access

On the nRF5340, debug access after any reset is only open if
`UICR.APPROTECT == 0x50FA50FA` (Unprotected): SystemInit copies that UICR
word into `CTRLAP.APPROTECT.DISABLE` at boot. After a mass erase, UICR reads
`0xFFFFFFFF` → the AP hard-locks at every reset **even though the firmware
boots and runs fine**. Symptoms: `Examination failed` /
`Failed to read memory at 0xe000ed00` on connect while the board happily
advertises. The only way back in is a CTRL-AP recovery (= another mass erase).

`flash_nrf5340.tcl` therefore programs `UICR.APPROTECT`,
`UICR.SECUREAPPROTECT` (app, `0x00FF8000`/`0x00FF801C`) and net
`UICR.APPROTECT` (`0x01FF8000`) to `0x50FA50FA` after every flash
(`uicr_unprotect_app` / `uicr_unprotect_net`). Do not remove these calls.

### Recovery coverage: nRF5340 only — the nRF54L15 has NO recovery path

Known gap (documented 2026-07-05, deliberately not fixed yet):

- **nRF5340**: recovery works but is nRF53-specific — `nrf53_recover` /
  `check_approtect` chain to `_nrf_ctrl_ap_recover` in openocd's
  `common.cfg`, which hardcodes the nRF53 CTRL-AP IDR (`0x12880000`).
- **nRF54L15**: **no valid recovery exists in our tooling.** Upstream
  OpenOCD (master) has no `nrf54l_recover`, no flash bank, nothing; the
  generic CTRL-AP proc rejects the 54L's CTRL-AP (different IDR, AP#2).
  If a 54L15 ever ends up APPROTECT-locked, current options are Nordic's
  official path (`nrfutil device recover` — requires a J-Link) or writing
  and testing an adapted CTRL-AP TCL proc against a sacrificial board.
  Nothing we do in normal operation locks the 54L15 (its APPROTECT is not
  the 5340's soft-branch design), but treat this as unprotected territory.

### Do NOT use probe-rs — openocd-master is the only flash backend

Project policy: all flashing goes through openocd-master (`fw-flash-5340`,
`fw-flash-54l15`). probe-rs was evaluated 2026-07-05 (0.31.0) and rejected:
on the nRF5340 its attach sequence reset-catches the core *before*
SystemInit runs the APPROTECT soft-unlock, concludes the chip is locked,
and its only remedy is `--allow-erase-all` — a full mass erase that also
wipes UICR, re-creating the lock for the next invocation (it bricked debug
access three times during the eval; openocd recovered it each time). It
also has no notion of the dual-core flash ordering (net FORCEOFF release).
It did work on the nRF54L15, but a second backend for one chip is not
worth the complexity.

### Stale bonds cause pairing failures that block PACS/ASCS reads

If a central was previously bonded and the bond info is reloaded from the
settings partition on boot (`settings_load()`), but the central still tries
to pair fresh or the firmware version changed security params, pairing
will fail.  The central then disconnects before it can read the encrypted
PACS/ASCS services.

**Fix:** Either do a full chip erase (`nrf53_recover` via openocd-master) before
flashing, or delete the bond on the central (e.g. `bluetoothctl remove`).

### printk and LOG output race on the same UART

When both `printk()` and `LOG_*()` macros write to the same UART console
simultaneously, lines can interleave and become unreadable. Add the
following to `prj.conf` to route `printk()` through the same backend as
`LOG_*()`:

```
CONFIG_LOG_PRINTK=y
```

This serializes output and eliminates garbled lines.

### ZMS settings backend requires explicit flash deps

ZMS needs `CONFIG_FLASH=y`, `CONFIG_FLASH_PAGE_LAYOUT=y`, and
`CONFIG_FLASH_MAP=y`. Without all three, `SETTINGS_ZMS` silently falls
to `SETTINGS_NONE` (no storage, no bond persistence across reboots).

### Board-specific Kconfig belongs in board conf, not prj.conf

`prj.conf` applies to ALL targets. A symbol that only one board needs
(e.g. `CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y`, meaningful only where
HFCLKAUDIO exists — nRF5340) causes a Kconfig "assigned value but got"
warning on the other board if left in `prj.conf`. Move board-specific
symbols to `boards/<board_target>.conf` (app-level, auto-discovered
from the repo `boards/` dir — NOT inside the board def dir). Example:
`boards/ebyte_e83_nrf5340_nrf5340_cpuapp.conf` for the nRF5340 target.

### ACL/ISO TX buffer counts must match the controller

The SW Split controller (nRF5340) reports 7 ACL and 6 ISO TX buffers. If the
app core `CONFIG_BT_BUF_ACL_TX_COUNT` / `CONFIG_BT_ISO_TX_BUF_COUNT`
don't match, the host emits `bt_hci_core` mismatch warnings that can
cause connection throttling. See `prj.conf` for the matched values.

On nRF54L15 the SDC controller defaults `BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=3`.
For sink-only, the board conf sets both `CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=1`
and `CONFIG_BT_ISO_TX_BUF_COUNT=1` so they match — the host's
`Num of Controller's ISO packets != ISO bt_conn_tx contexts` warning is
silenced at the source, not tolerated.

### Centrals require Just Works pairing

Default `CONFIG_BT_SMP_ENFORCE_MITM=y` forces authenticated pairing.
Without a passkey UI the central shows "incorrect PIN". Disable MITM
(`CONFIG_BT_SMP_ENFORCE_MITM=n`) and add `pairing_accept` /
`pairing_complete` / `pairing_failed` callbacks returning
`BT_SECURITY_ERR_SUCCESS`. See `src/bt_bap.c` pairing callbacks.

### The `sdk-nrf` west project must be named `nrf`

`nrf/modules/modules.cmake` hardcodes `SYSBUILD_NRF_KCONFIG`. Naming it
`sdk-nrf` breaks cmake with "Could not open '/workspace/zephyr/' (EISDIR)".

### WSL2 J-Link

WSL2 requires `usbipd` on the Windows host to bind the J-Link to WSL2.
Without it, `west flash` fails with "Cannot connect to the probe".

### `bt_audio_codec_cfg_get_chan_allocation` returns 0 on success

The API fills `*chan_allocation` via pointer and returns **0 on success**,
negative errno on failure. Checking `if (ret > 0)` silently falls through
to the mono default for every source that sends a valid channel allocation
LTV — making all stereo ASEs appear mono. Use `if (ret == 0)`. See
`lc3_config` in `src/bt_bap.c`.

### Stereo single-ASE (Mode B) needs two LC3 decoders

A BAP source may send one ASE with `chan_count=2` (stereo) rather than two
mono ASEs. In that case, the SDU is `[L_frame][R_frame]` concatenated.
One `lc3_decode` call with stride=2 only fills even (L) positions;
odd (R) positions stay zero → right channel silent. Two independent
`lc3_decoder_t` instances are required: decode L into `stereo_out[0]`
stride 2, R into `stereo_out[1]` stride 2.

Per-channel octets = `(sdu_len / frames_per_sdu) / chan_count`.

This logic lives in `audio_decode_sdu` (`src/audio_decode.c`).

### I2S double-write of same slab block causes DMA corruption

Passing the same `void *block` pointer to `i2s_write` twice queues the
same DMA buffer twice. When the first DMA transfer completes the driver
frees the slab block; the second DMA transfer then operates on freed
memory → underrun or heap corruption. Always allocate a separate slab
block for each `i2s_write` call.

### I2S DMA underrun recovery requires `TRIGGER_PREPARE`

After `i2s_nrfx: Next buffers not supplied on time`, subsequent
`i2s_write` calls return `-EIO` (state 4 = ERROR). Call
`i2s_trigger(dev, TX, I2S_TRIGGER_PREPARE)` to reset to READY, then
re-arm: set `started = false` so the next `audio_i2s_push` pre-fills
and re-triggers.

### `audio_sink_stop` must not clear `configured`

After disconnect, `audio_sink_stop` drops the DMA (`TRIGGER_DROP`) and
resets `started`. Clearing `configured` causes every subsequent
`audio_sink_push` on reconnect to return `-EIO`. Keep `configured = true`
so reconnect works without re-calling `audio_sink_init`.

### Clock recovery actuator must match platform

The `AUDIO_CLOCK_ACTUATOR` Kconfig choice selects the actuator. Two production options:
- `APLL` (default, nRF5340) — `audio_clock_actuator_apll.c`, trims HFCLKAUDIO APLL.
- `NONE` — `audio_clock_actuator_none.c`, nRF54L15 production. ASRC consumes
  controller ppm directly (no physical actuator on nRF54L15).

The production actuator API is init/apply_ppm/reset only (clock steering,
no data-path adjustment).  The historical SAMPLE_ADJUST actuator — including
its retired `audio_clock_actuator_consume_sample_adjustment()` symbol — is
retained for regression testing only as a test-local copy under
`tests/unit/actuator_sample_adjust_historical/src/`; no longer selectable in
production Kconfig.

### Drift controller: PCLK feedforward + per-block phase PI (Phase 4b.2)

Controller has two explicit inputs:
- `audio_drift_frequency_error_update(local_clock_error_ppm)` — from platform
  timing (nRF54L15: PCLK TIMER20 vs GRTC; nRF5340: never called, stays zero).
  Positive = local PCLK/I2S runs faster than controller. Feedforward correction
  = `-measured` (local fast → negative correction → eventual insert).
- `audio_drift_controller_update(slab_free)` — called ONCE per rendered stereo
  block in `audio_sink_push()`, before slab allocation. Phase error =
  `PHASE_SETPOINT - slab_free` (corrected sign vs earlier code). Combines
  filtered frequency feedforward + phase PI. No floating point; pure 32-bit
  integer with 64-bit intermediate multiplication.

Output sign: positive ppm = consume source faster / drop frame eventually;
negative ppm = consume source slower / insert frame eventually.
Output clamp: `CONFIG_AUDIO_DRIFT_OUTPUT_CLAMP` (default 500; nRF54L15: 2000).
Phase integral clamp: `CONFIG_AUDIO_DRIFT_PHASE_INTEGRAL_CLAMP` (default 500;
nRF54L15: 150).

`audio_sink_sdu_ref_update()` is REMOVED. ISO timestamps go ONLY to
`audio_timing_sdu_ref_update()` for GRTC scheduling. Never call drift
controller from ISR — work/thread context only.

### SDC/MPSL owns RADIO — never access RADIO directly

On nRF54L15 (SDC on cpuapp), MPSL owns the RADIO peripheral. Never configure
or read RADIO registers, RADIO events, RADIO IRQ, or RADIO DPPI publication
subscriber. Any direct RADIO access will conflict with the SoftDevice
Controller runtime. For drift measurement on nRF54L15, use ISO `info->ts` with
`BT_ISO_FLAGS_TS` (controller-clock ISO SDU reference), GRTC future
compare/action (Nordic ISO-time-sync pattern; sample at
`nrf/samples/bluetooth/iso_time_sync/`), and TIMER20 in TIMER mode (PCLK-
derived free-running ticks) with GRTC compare → GPPI → TIMER20 CAPTURE
(hardware-snapshotted counter). Phase 4b.1 logs diagnostics; Phase 4b.2
feeds measured ppm into the PI controller. **Historical: I2S20 FRAMESTART
→ GPPI → TIMER20 COUNT was invalidated — HW validation on 2026-07-26 showed
FRAMESTART fires at DMA buffer boundaries (~100 Hz), not LRCK edges.**
`sdc_hci_cmd_vs_set_event_start_task()` is an ACL-event diagnostic,
not a CIS RX timestamp.

### Zephyr does NOT detect devicetree pinctrl overlaps

Two peripherals claiming the same pin in their `pinctrl-N` default groups produce
**no compile error and no runtime warning**. The last peripheral to init a
contested pin wins the PSEL; the loser silently corrupts. Verify pin assignments
against all enabled peripherals by decoding the resolved `zephyr.dts` (psel
encoding: `NRF_PSEL(fun, port, pin)` = `(fun << 24) | ((port*32+pin) & 0x1ff)`;
see `nrf-pinctrl.h`). This is how the nRF54L15 I2S20 pin conflict (P1.10/P1.11/P1.12
vs pwm20/pdm20) went unnoticed — fixed by moving I2S20 to D0/D1/D2 (P1.4/P1.5/P1.6)
and disabling `&pdm20`.

### CJMCU-1334 (UDA1334A) wiring

| Board | BCK | DIN | LRCK | VIN | GND |
|-------|-----|-----|------|-----|-----|
| nRF5340 (Ebyte E83) | P1.15 | P1.13 | P1.12 | 3.3 V | GND + AGND |
| nRF54L15 (Seeed Xiao) | D0 (P1.4) | D2 (P1.6) | D1 (P1.5) | 3V3 | GND + AGND |

Config pins (SF0/SF1/MUTE): on the Adafruit UDA1334A breakout these are
**pre-pulled to GND by on-PCB resistors** (R10/R2/R9 — verified against the
Adafruit PCB schematic), so leaving them floating = I2S format + unmuted. On
a bare clone without the pulldowns, wire all three to GND explicitly. MUTE
is LOW = unmuted (inverted vs most mute pins). SCLK/PLL leave unconnected
(internal PLL locks to BCLK). Audio out: Lout / Rout to headphone L/R,
AGND to sleeve.

Either UDA1334A (CJMCU-1334) or PCM5102A works — same 3-wire no-MCK topology.
PCM5102A's spec lead (112 dB / 32-bit / 384 kHz vs 100 dB / 16-bit) is
inaudible at the 48 kHz/16-bit LC3 floor. PCM5102A cheap breakouts need the
SCK pad solder-bridged to GND for 3-wire mode or you get silence/hiss.

## Stack

- App: BAP Unicast Server sink-only, 2 sink ASEs, LC3 decode → I2S
- Receive/session: `audio_stream_session.c` (R6 — exclusive owner of app
  audio receive state: validated codec shape, decoder contexts, per-CIS
  ISO sequence trackers, Mode A assembler, receive counters, mode
  inference, decode/conceal/volume/push with admission/lease discipline);
  `bt_bap.c` keeps only Bluetooth service/lifecycle orchestration plus
  the R7 private teardown transition owner (first close wins, per-slot
  release once, universal close→drain→sink-stop→offload-stop→reset)
- Audio: `audio_sink.h` interface → `audio_i2s.c` (slab/DMA backend)
- Clock recovery: `audio_drift.c` (PI controller, ppm output) → actuator interface (`audio_clock_actuator.h`) → `audio_clock_actuator_apll.c` (nRF5340 APLL) or `audio_clock_actuator_none.c` (nRF54L15, ASRC consumes ppm)
- ASRC: `audio_asrc.c` (fixed-point linear stereo, cpuapp) + FLPR offload (`src/flpr/`, handshake/runtime/rings)
- Decode: `audio_decode.c` (LC3 decode + channel routing, unit-testable)
- Net (nRF5340): `hci_ipc` with `nrf5340_cpunet_iso_peripheral-bt_ll_sw_split.conf`
- Link Layer: nRF5340 = BT_LL_SW_SPLIT (Zephyr open-source controller, ISO required); nRF54L15 = SDC (SoftDevice Controller, single-core)
- DAC: CJMCU-1334 (UDA1334A) or PCM5102A, no MCK, `CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y`

## Key Files

| File | Purpose |
|------|---------|
| `src/main.c` | Hardware wiring, watchdog, and advertising-loop adapter (fatal boot order lives in `app_lifecycle.c`) |
| `src/app_lifecycle.c` | Pure fatal boot coordinator: ordered init, cold reboot, advertising restart |
| `src/bt_bap.c` | BAP unicast server, ASCS callbacks, PACS, pairing, advertising, the thin recv adapter (R6: app audio receive state lives in `audio_stream_session.c`), and the R7 private teardown transition owner (`teardown_transition`/`teardown_close_path`: first close wins, per-slot release once, universal close→drain→sink-stop→offload-stop→reset order) |
| `src/bt_pairing_policy.c` | Pure OPEN/BONDED_ONLY policy snapshot; Bluetooth controller work stays in `bt_bap.c` |
| `src/audio_stream_session.c` | Exclusive owner of app audio receive/session state (R6): validated codec shape, decoder ctx, per-CIS ISO seq trackers, Mode A assembler, recv counters, decode/conceal/volume/push, admission/lease (rx_open/rx_close) |
| `src/audio_modea.c` | Bounded two-CIS event assembler and per-channel PLC |
| `src/audio_iso_seq.c` | Pure per-CIS omitted-callback sequence tracker |
| `src/audio_decode.c` | LC3 decode + channel routing (Mode A / Mode B / mono) |
| `src/audio_sink.h` | Platform-neutral audio-sink interface (init, push, stop; R1 stream_open/stream_close admission + drain) |
| `src/audio_i2s.c` | I2S TX driver (slab + DMA, 48 kHz stereo) — implements audio_sink.h |
| `src/audio_shell.c` | Audio diagnostics shell commands (`audio status`, `audio perf`, reset-stats/perf-reset/stop) |
| `src/bt_shell.c` | `bt unpair` pairing-mode reset command (R4) |
| `src/flpr_shell.c` | FLPR production diagnostics (`flpr status/offload/runtime/restart`, R4) |
| `src/flpr_acceptance_shell.c` | FLPR acceptance-harness commands (`flpr ring *`, `flpr stress`, `flpr hang`) — `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`-gated (R4) |
| `src/audio_drift.c` | PI clock recovery controller (dual-term, ppm output) |
| `src/audio_drift.h` | Controller API + APLL register constants |
| `src/audio_rate_convert.c` | Fixed-rate frame-count/remainder converter (I2S drain-rate matching; init/next_frames only, no resampling/copy API) |
| `src/audio_rate_convert.h` | Rate converter public API (unit-testable) |
| `src/audio_timing.h` | Platform timing interface (frequency error, GRTC scheduling) |
| `src/audio_timing_math.c` | Timing math shared across platforms |
| `src/audio_timing_nrf54.c` | nRF54L15 TIMER20-vs-GRTC PCLK frequency measurement |
| `src/audio_timing_none.c` | nRF5340 no-op timing (no GRTC/TIMER20) |
| `src/stream_lifecycle.c` | Stream start/stop lifecycle (unit-testable) |
| `src/audio_clock_actuator.h` | Actuator interface (init, apply_ppm, reset) |
| `src/audio_clock_actuator_apll.c` | nRF5340 HFCLKAUDIO APLL actuator (ppm → register trim) |
| `tests/unit/actuator_sample_adjust_historical/src/audio_clock_actuator_sample_adjust_historical.c` | Historical sample insert/drop actuator, test-local copy (regression testing only) |
| `src/audio_clock_actuator_none.c` | nRF54L15 no-op actuator (ASRC consumes ppm directly) |
| `src/audio_asrc.c` | Fixed-point linear stereo ASRC (cpuapp + FLPR fallback) |
| `src/audio_offload.c` | FLPR offload manager (handshake, IPC, fallback path) |
| `src/flpr/` | FLPR firmware (RISC-V VPR): ASRC, ICMsg/VEVIF IPC |
| `src/flpr_handshake.c` | cpuapp↔FLPR boot handshake + VEVIF (R8: production slot reset/consumer + diagnostic slot registration; stress/fault-hang state moved to flpr_acceptance) |
| `src/flpr_protocol.h` | Shared protocol constants (ring layout, commands) |
| `src/flpr_ring.c` | SPSC ring buffer (shared SRAM, cache-safe) |
| `src/flpr_ring_mgr.c` | Ring manager production core: paired rings, reset, typed ASRC produce/consume, notify, wait, remote restart (R8) |
| `src/flpr_acceptance.c` | Cpuapp FLPR acceptance module (R8): ring test, stalls + ACK correlation, stale produce, report aggregation, stress, fault hang, gates 1–6 — `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS` |
| `src/flpr_control_ack.c` | Shared control-ACK correlation engine (R8): ONE owner for reset + stall ACK correlation |
| `src/flpr/acceptance.c` | FLPR-image acceptance handlers (R8): RING_TEST/STALL/STRESS/FAULT_HANG + diagnostic hooks — `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS` |
| `src/flpr_runtime.c` | FLPR runtime: IPC submit, watchdog, fault detection |
| `src/flpr_audio_process.c` | FLPR audio block wrapper (metadata + PCM) |
| `boards/ebyte/e83_nrf5340/` | Custom board definition for Ebyte E83-2G4M03S: I2S0 pins, ACLK 12.288 MHz, QSPI disabled, i2s-audio alias, OpenOCD flash runner |
| `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | Xiao nRF54L15 remap: UART20 to SAMD11, I2S20 to D0/D1/D2 (MCK on D3/P1.7 — peripheral-needed routing, DAC does not consume it; 3-wire no-MCK at the DAC), pdm20 disabled, TIMER20 reserved, FLPR IPC SRAM regions |
| `prj.conf` | App Kconfig (ACL/ISO buffers, SMP, 2 ASEs, liblc3, FPU, ZMS) |
| `sysbuild.cmake` | Applies SW Split DT overlay + Kconfig overlay to hci_ipc |
| `Kconfig.sysbuild` | `NRF_DEFAULT_BLUETOOTH=y` conditional on nRF5340, gates netcore |
| `scripts/bap_central.py` | BAP central test driver — thin CLI coordinator (argparse + wiring + flow) plus the `CentralCleanup` idempotent resource owner (fixed teardown order, safe from `finally`; every fatal path raises a module `CentralError` with the message already printed and exit 1 preserved) |
| `scripts/bap_central_device.py` | Central device resolution (R9): adapter power, `--peer-addr` exact-peer path, existing Device1 enumeration, bounded `InterfacesAdded` discovery — `DiscoverySession` owns its signal match and StopDiscovery exactly once |
| `scripts/bap_central_security.py` | Central agent/pairing/connect (R9): JustWorks agent factory, raw-HCI fresh-connect strategy (exact `sudo -n` argv, ready + Connected gates), BlueZ preserve-bond Connect strategy, `wait_for_helper_ready` (READY_PREFIX from `hci_raw_connect.py`), RemoveDevice fresh-only, Pairable/Trusted/async Pair, services-resolved, cleanup Disconnect |
| `scripts/bap_central_endpoint.py` | Central BAP source endpoint (R9): constants/LC3 blobs, `MediaEndpoint1` class factory, registration, deferred async Acquire, pending/acquired fd ownership, second-ASE grace, all-or-nothing, mode inference |
| `scripts/bap_central_session.py` | Central LC3 source/writer (R9): lazy liblc3 loader + encoder (stdlib-safe import), sine, per-mode payloads, `StreamSession` writer lifecycle + exact teardown tail |
| `README.md` | Public LE Audio explainer: what the project does, board tradeoff matrix, supported sources, docs links |
| `docs/user-guide.md` | Public user guide: boot, pairing modes (NORMAL/BONDING/RESET), flashing notes, troubleshooting |
| `docs/hardware-wiring.md` | Public wiring: DAC choice, verified E83/Xiao I2S pin tables, config pins, line-level warning |
| `docs/known-limitations.md` | Public known-limitations list (48 kHz only, 360-frame FLPR fallback, linear volume, pop, duplicate adv, etc.) |
| `docs/supported-sources.md` | Public researched Linux LE Audio source hardware + software requirements (status labels Project-validated / Vendor-supported / Unverified; Intel AX210 project-validated, dongles unverified) |
| `docs/technology/nrf5340.md`, `docs/technology/nrf54l15.md` | Public per-platform technology notes (architecture, clock recovery/rate matching) |
