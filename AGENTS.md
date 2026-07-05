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

# AGENTS.md — LE Audio Receiver (nRF5340 + UDA1334A)

## Plan of record

`docs/design.md` is the accepted design doc and phased plan (Phases 0–6) for
supporting both nRF5340 and nRF54L15. Read it before structural changes.
Current status: **Phases 0–1 complete** — the build workflow (Phase 0) and
the custom board foundation (Phase 1) have landed. nRF54L15 now compiles;
audio bring-up is Phase 4. The gotchas below that describe runtime behavior
(SW Split LL, settings_load, pairing, I2S DMA, etc.) remain valid.

Consequences for work in this repo today:

- **Known bug**: PACS advertises 16/24/48 kHz but the pipeline is hardcoded
  to 48 kHz (design.md F4). Resolution is decided (restrict to 48 kHz,
  Phase 2) — do not patch differently.
- `docs/nrf54l15-drift-compensation.md` is superseded — reference only,
  never update it.
- Tooling reference: `~/repos/serial-mcp` holds the direnv + nrfutil
  workflow that Phase 0 ports here.
- Every change must keep the nRF5340 target building, flashing, streaming.

## Build

Build **from the repo root**. Enter the dev shell first, then run the build
helper:

```bash
cd <repo>
direnv allow         # or: nix develop
fw-build-5340
```

The build runs `west build -b ebyte_e83_nrf5340/nrf5340/cpuapp --sysbuild --pristine`
into `build/nrf5340/`. Use `--pristine` after any `prj.conf`, overlay, or
`sysbuild.cmake` change.  Pass extra cmake args through:

```bash
fw-build-5340 -- -DCONFIG_FOO=y
```

The nRF54L15 target builds with `fw-build-54l15` into `build/nrf54l15/`
(Phase 1) and flashes with `fw-flash-54l15` (probe-rs via the Xiao's
built-in CMSIS-DAP; probe auto-detected by target identity). The build
targets `nrf54l15dk` pins, so the Xiao's console is silent until the
custom Xiao board port lands.

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

Expected after boot: `BLE ready`, `settings_load() OK`,
`Advertising as "LE Audio Receiver"`. During streaming,
`i2s_nrfx: Next buffers not supplied on time` occurs periodically due to
HFCLKAUDIO clock drift vs. the BLE ISO clock — recovery is automatic
(`TRIGGER_PREPARE` + re-arm). Increase pre-fill depth in `audio_i2s.c`
to reduce frequency.

### Probe identification — NEVER assume the probe↔board mapping

Probes get replugged; documentation rots. `fw-probes` is the source of truth:

```bash
fw-probes            # table: probe serial → chip behind it (read-only)
fw-probes --find nrf53   # serial of the probe wired to an nRF53
```

It fingerprints each CMSIS-DAP probe's target over SWD (DPIDR → AP IDR map →
FICR INFO.PART/VARIANT) and works even when the chip is APPROTECT-locked
(identity from the DP/AP signature). `fw-flash-5340` calls it automatically
to pick the right probe at flash time.

`scripts/probe-serial.local` (gitignored) is now only a manual **override**
for when auto-detection must be bypassed. Normally it should not exist.

**Doc hygiene rule:** never write a static probe-serial↔board table into
docs or handoffs — reference `fw-probes` instead. Any hardware-identity
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
  -c "adapter serial $(fw-probes --find nrf53)" \
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
openocd -f interface/cmsis-dap.cfg -c "adapter serial $(fw-probes --find nrf53)" \
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

### Do NOT use probe-rs on the nRF5340

Evaluated 2026-07-05 (probe-rs 0.31.0): its attach sequence reset-catches the
core *before* SystemInit runs the APPROTECT soft-unlock, concludes the chip
is locked, and its only remedy is `--allow-erase-all` — a full mass erase
that also wipes UICR, re-creating the lock for the next invocation. Any
mid-flash fault leaves a blank, locked chip. It also has no notion of the
dual-core flash ordering (net FORCEOFF release). The openocd-master flow in
this repo handles all of this; use `fw-flash-5340`.

probe-rs on the **nRF54L15** is fine (evaluated same day: fast, repeatable,
verify passes, chip stays debuggable across resets) — that is what
`fw-flash-54l15` uses.

### Stale bonds cause pairing failures that block PACS/ASCS reads

If a phone was previously bonded and the bond info is reloaded from the
settings partition on boot (`settings_load()`), but the phone still tries
to pair fresh or the firmware version changed security params, pairing
will fail.  The phone then disconnects before it can read the encrypted
PACS/ASCS services.

**Fix:** Either do a full chip erase (`nrfutil device recover`) before
flashing, or update the phone (delete device in Bluetooth settings → re-scan).

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

### ACL/ISO TX buffer counts must match the controller

The SW Split controller reports 7 ACL and 6 ISO TX buffers. If the
app core `CONFIG_BT_BUF_ACL_TX_COUNT` / `CONFIG_BT_ISO_TX_BUF_COUNT`
don't match, the host emits `bt_hci_core` mismatch warnings that can
cause connection throttling. See `prj.conf` for the matched values.

### Phones require Just Works pairing

Default `CONFIG_BT_SMP_ENFORCE_MITM=y` forces authenticated pairing.
Without a passkey UI the phone shows "incorrect PIN". Disable MITM
(`CONFIG_BT_SMP_ENFORCE_MITM=n`) and add `pairing_accept` /
`pairing_complete` / `pairing_failed` callbacks returning
`BT_SECURITY_ERR_SUCCESS`. See `main.c` lines ~583–607.

### The `sdk-nrf` west project must be named `nrf`

`nrf/modules/modules.cmake` hardcodes `SYSBUILD_NRF_KCONFIG`. Naming it
`sdk-nrf` breaks cmake with "Could not open '/workspace/zephyr/' (EISDIR)".

### WSL2 J-Link

WSL2 requires `usbipd` on the Windows host to bind the J-Link to WSL2.
Without it, `west flash` fails with "Cannot connect to the probe".

### `bt_audio_codec_cfg_get_chan_allocation` returns 0 on success

The API fills `*chan_allocation` via pointer and returns **0 on success**,
negative errno on failure. Checking `if (ret > 0)` silently falls through
to the mono default for every phone that sends a valid channel allocation
LTV — making all stereo ASEs appear mono. Use `if (ret == 0)`.

### Stereo single-ASE (Mode B) needs two LC3 decoders

A phone may send one ASE with `chan_count=2` (stereo) rather than two
mono ASEs. In that case, the SDU is `[L_frame][R_frame]` concatenated.
One `lc3_decode` call with stride=2 only fills even (L) positions;
odd (R) positions stay zero → right channel silent. Two independent
`lc3_decoder_t` instances are required: decode L into `stereo_out[0]`
stride 2, R into `stereo_out[1]` stride 2.

Per-channel octets = `(sdu_len / frames_per_sdu) / chan_count`.

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

### `audio_i2s_stop` must not clear `configured`

After disconnect, `audio_i2s_stop` drops the DMA (`TRIGGER_DROP`) and
resets `started`. Clearing `configured` causes every subsequent
`audio_i2s_push` on reconnect to return `-EIO`. Keep `configured = true`
so reconnect works without re-calling `audio_i2s_init`.

### CJMCU-1334 (UDA1334A) wiring

| nRF5340 pin | CJMCU-1334 pin |
|-------------|----------------|
| P1.15 BCK   | BCLK           |
| P1.13 DIN   | DIN            |
| P1.12 LRCK  | WSEL           |
| 3.3 V       | VIN            |
| GND         | GND + AGND     |

Config pins: **SF0 → GND**, **SF1 → GND** (I2S format), **MUTE → GND or
float** (LOW = unmuted — opposite of most mute pins), SCLK/PLL leave
unconnected (internal PLL locks to BCLK). Audio out: Lout / Rout to
headphone L/R, AGND to sleeve.

## Stack

- App: BAP Unicast Server sink-only, 2 sink ASEs, LC3 decode → I2S
- Net: `hci_ipc` with `nrf5340_cpunet_iso_peripheral-bt_ll_sw_split.conf`
- Link Layer: BT_LL_SW_SPLIT (Zephyr open-source controller, ISO required)
- DAC: CJMCU-1334 (UDA1334A), no MCK, `CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y`

## Key Files

| File | Purpose |
|------|---------|
| `src/main.c` | BAP server, ASCS callbacks, LC3 decode, I2S push, pairing |
| `src/audio_i2s.c` | I2S TX driver (slab + DMA, 48 kHz stereo) |
| `boards/ebyte/e83_nrf5340/` | Custom board definition for Ebyte E83-2G4M03S: I2S0 pins, ACLK 12.288 MHz, QSPI disabled, i2s-audio alias, OpenOCD flash runner |
| `prj.conf` | App Kconfig (ACL/ISO buffers, SMP, 2 ASEs, liblc3, FPU, ZMS) |
| `sysbuild.cmake` | Applies SW Split DT overlay + Kconfig overlay to hci_ipc |
| `Kconfig.sysbuild` | `NRF_DEFAULT_BLUETOOTH=y` conditional on nRF5340, gates netcore |
