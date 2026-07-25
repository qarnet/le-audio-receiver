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

Always attempt to fix build/boot warnings. Ignoring them lets real bugs hide
in the noise — a warning that is "expected" today becomes the one you miss
when it turns into a real failure. If a warning is genuinely unfixable in this
build configuration, suppress it explicitly (Kconfig `default n` with a
comment, or a targeted `#pragma`) — never just leave it printing.

This applies to: compiler warnings, Kconfig "assigned value but got" warnings,
boot-time `LOG_WRN` lines, and openocd/flashing warnings. Fix the source, or
suppress with a recorded reason. Do not normalize noise.

## Plan of record

`docs/design.md` is the accepted design doc and phased plan (Phases 0–6) for
supporting both nRF5340 and nRF54L15. Read it before structural changes.
Current status: **Phase 4 landed** — PI clock recovery controller (dual-term,
ppm output) + actuator interface with two actuators: APLL (nRF5340) and
SAMPLE_ADJUST (nRF54L15, sample insert/drop). The nRF54L15 target now builds,
flashes, and boots with I2S + BT working. Phase 5 (ASRC on cpuapp) and
Phase 6 (FLPR offload) remain.

Consequences for work in this repo today:

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

### Phones require Just Works pairing

Default `CONFIG_BT_SMP_ENFORCE_MITM=y` forces authenticated pairing.
Without a passkey UI the phone shows "incorrect PIN". Disable MITM
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
to the mono default for every phone that sends a valid channel allocation
LTV — making all stereo ASEs appear mono. Use `if (ret == 0)`. See
`lc3_config` in `src/bt_bap.c`.

### Stereo single-ASE (Mode B) needs two LC3 decoders

A phone may send one ASE with `chan_count=2` (stereo) rather than two
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

The `AUDIO_CLOCK_ACTUATOR` Kconfig choice selects the actuator. Three options:
- `APLL` (default, nRF5340) — `audio_clock_actuator_apll.c`, trims HFCLKAUDIO APLL.
- `SAMPLE_ADJUST` (nRF54L15) — `audio_clock_actuator_sample_adjust.c`, inserts/drops
  single PCM samples in the I2S block (degenerate ASRC). The nRF54L15 board conf
  sets this. No HFCLKAUDIO on nRF54L15 → APLL is not an option there.
- `NONE` — `audio_clock_actuator_none.c`, controller runs but output is discarded
  (testing only). Do NOT set on nRF5340 (controller output needs the APLL) and
  do NOT set on nRF54L15 in production (use SAMPLE_ADJUST).

`audio_clock_actuator_consume_sample_adjustment()` returns ±1/0; APLL and NONE
always return 0 (data-path adjustment is a no-op for clock-steering actuators).

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
- Audio: `audio_sink.h` interface → `audio_i2s.c` (slab/DMA backend)
- Clock recovery: `audio_drift.c` (PI controller, ppm output) → actuator interface (`audio_clock_actuator.h`) → `audio_clock_actuator_apll.c` (nRF5340 APLL) or `audio_clock_actuator_sample_adjust.c` (nRF54L15 sample insert/drop)
- Decode: `audio_decode.c` (LC3 decode + channel routing, unit-testable)
- Net (nRF5340): `hci_ipc` with `nrf5340_cpunet_iso_peripheral-bt_ll_sw_split.conf`
- Link Layer: nRF5340 = BT_LL_SW_SPLIT (Zephyr open-source controller, ISO required); nRF54L15 = SDC (SoftDevice Controller, single-core)
- DAC: CJMCU-1334 (UDA1334A) or PCM5102A, no MCK, `CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y`

## Key Files

| File | Purpose |
|------|---------|
| `src/main.c` | Lifecycle wiring + watchdog + advertising restart loop |
| `src/bt_bap.c` | BAP unicast server, ASCS callbacks, PACS, pairing, advertising |
| `src/audio_decode.c` | LC3 decode + channel routing (Mode A / Mode B / mono) |
| `src/audio_sink.h` | Platform-neutral audio-sink interface (init, push, stop, sdu_ref) |
| `src/audio_i2s.c` | I2S TX driver (slab + DMA, 48 kHz stereo) — implements audio_sink.h |
| `src/audio_drift.c` | PI clock recovery controller (dual-term, ppm output) |
| `src/audio_drift.h` | Controller API + APLL register constants |
| `src/audio_clock_actuator.h` | Actuator interface (init, apply_ppm, reset, consume_sample_adjustment) |
| `src/audio_clock_actuator_apll.c` | nRF5340 HFCLKAUDIO APLL actuator (ppm → register trim) |
| `src/audio_clock_actuator_sample_adjust.c` | nRF54L15 sample insert/drop actuator (ppm → ±1 sample) |
| `src/audio_clock_actuator_none.c` | No-op actuator (testing only) |
| `boards/ebyte/e83_nrf5340/` | Custom board definition for Ebyte E83-2G4M03S: I2S0 pins, ACLK 12.288 MHz, QSPI disabled, i2s-audio alias, OpenOCD flash runner |
| `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | Xiao nRF54L15 remap: UART20 to SAMD11, I2S20 to D0/D1/D2, pdm20 disabled |
| `prj.conf` | App Kconfig (ACL/ISO buffers, SMP, 2 ASEs, liblc3, FPU, ZMS) |
| `sysbuild.cmake` | Applies SW Split DT overlay + Kconfig overlay to hci_ipc |
| `Kconfig.sysbuild` | `NRF_DEFAULT_BLUETOOTH=y` conditional on nRF5340, gates netcore |
| `scripts/bap_central.py` | BAP central test driver (Linux → receiver, LC3 sine stream) |
| `README.md` | Human-facing project overview, BOM, I2S wiring for both boards |
