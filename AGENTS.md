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

# AGENTS.md — LE Audio Receiver (nRF5340 DK + PCM5102A)

## Build

Build **from the NCS root** (`~/ncs/v3.3.0`). The app is a freestanding source
directory there. `ZEPHYR_BASE` and sample paths must resolve. The custom board
definition lives in the app's `boards/` directory — pass `BOARD_ROOT` so the
build system finds it.

```bash
cd ~/ncs/v3.3.0
west build -b e83_2g4m03s_tb/nrf5340/cpuapp -s /home/thomas/repos/le-audio-receiver \
  --sysbuild --pristine -d /tmp/build_e83 -- \
  -DBOARD_ROOT=/home/thomas/repos/le-audio-receiver
```

Use `--pristine` after any `prj.conf`, overlay, or `sysbuild.cmake` change.
The build tree is at `/tmp/build_e83/`.

## Flash

Both app core and hci_ipc network core must be flashed **via PyOCD** (PicoProbe).
J-Link/nrfjprog/nrfutil runners are NOT available on this board.

```bash
pyocd flash -t nrf5340_xxaa_app --erase chip build/zephyr/merged.hex
pyocd flash -t nrf5340_xxaa_net --erase chip build/hci_ipc/zephyr/zephyr.hex
```

With sysbuild output at `/tmp/build_e83/`:
```bash
pyocd flash -t nrf5340_xxaa_app --erase chip /tmp/build_e83/merged.hex
pyocd flash -t nrf5340_xxaa_net --erase chip /tmp/build_e83/merged_CPUNET.hex

## Serial

App core output: `/dev/ttyACM1` (not ACM0) at **115200 8N1**.
Net core output: `/dev/ttyACM1` sometimes forwards both.

```bash
stty -F /dev/ttyACM1 115200 raw -echo && cat /dev/ttyACM1
```

Expected after boot: `BLE ready`, `settings_load() OK`,
`Advertising as "LE Audio Receiver"`. During streaming,
`i2s_nrfx: Next buffers not supplied on time` occurs periodically due to
HFCLKAUDIO clock drift vs. the BLE ISO clock — recovery is automatic
(`TRIGGER_PREPARE` + re-arm). Increase pre-fill depth in `audio_i2s.c`
to reduce frequency.

### Capturing dual-core logs during testing

When debugging, both ACM ports must be captured **before** the device resets.
The `scripts/read_acm.py` helper (pyserial + auto-reopen) handles the USB
disconnect during reset.  Use `tmux` to keep readers alive between tool calls:

```bash
# Start background readers
tmux new-session -d -s acm0 \
  "python3 scripts/read_acm.py ttyACM0 /tmp/acm0.log"
tmux new-session -d -s acm1 \
  "python3 scripts/read_acm.py ttyACM1 /tmp/acm1.log"

# Reset so boot capture is clean
nrfutil device reset
```

Always reset **after** starting the readers.  The readers auto-exit after 30 s.
Stop with `tmux kill-session -t acm0` / `acm1`.

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
If you suspect a stale bond or corrupted settings:

```bash
nrfutil device recover  # ERASEALL via CTRL-AP: wipes ALL non-volatile memory
west flash --build-dir build
```

The recover command disables AP-Protect and triggers an ERASEALL, clearing
both firmware and the settings partition. This is the correct way to get a
clean slate for testing.

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
| `boards/nrf5340dk_nrf5340_cpuapp.overlay` | I2S0 pins, ACLK 12.288 MHz |
| `prj.conf` | App Kconfig (ACL/ISO buffers, SMP, 2 ASEs, liblc3, FPU, ZMS) |
| `sysbuild.cmake` | Applies SW Split DT overlay + Kconfig overlay to hci_ipc |
| `sysbuild.conf` | `SB_CONFIG_NETCORE_HCI_IPC=y` |

---

# Board Porting — Custom Board (E83-2G4M03S-TB / 10414-PCB-V1.2)

## Hardware Identity

| Item | Value |
|------|-------|
| **Board name** (proposed) | `e83_2g4m03s_tb` |
| **SoC** | nRF5340-QKAA (same as nRF5340 DK) |
| **Module** | Ebyte E83-2G4M03S — nRF5340 SMD module, 16×16 mm, IPEX antenna |
| **Custom PCB** | 10414-PCB-V1.2 — integrates E83 module, LDO, CH340X UART bridge |
| **Debug probe** | PicoProbe (CMSIS-DAP via PyOCD) — **NOT J-Link** |
| **SoC DTSI files** | Same as DK: `nrf5340_cpuapp_qkaa.dtsi`, `nrf5340_cpunet_qkaa.dtsi` |

## Architecture (same dual-core split as DK)

| Core | Target identifier | RAM | Flash |
|------|-------------------|-----|-------|
| Application | `e83_2g4m03s_tb/nrf5340/cpuapp` | 512 KB | 1 MB |
| Network | `e83_2g4m03s_tb/nrf5340/cpunet` | 64 KB | 256 KB |

## Hardware Differences from nRF5340 DK

| Feature | nRF5340 DK | Custom board |
|---------|-----------|--------------|
| **UART** | P0.29 TX, P0.28 RX (VCOM) | P0.20 TX, P0.22 RX, P0.21 CTS (CH340X bridge) |
| **USB** | J3 nRF USB port | JK1 Micro USB direct to D+/D- |
| **QSPI flash** | 8 MB external MX25R64 | None |
| **Arduino headers** | Yes | No (P2/P3 GPIO expansion headers) |
| **Buttons** | 4 (BTN1–BTN4) + RESET | RESET only (S1) |
| **LEDs** | 4 (LED1–LED4) | 1 power LED (D1) |
| **I2S DAC** | Cirrus CS47L63 on I2S0 | External CJMCU-1334 wired to P1.15/P1.13/P1.12 |
| **32.768 kHz XTAL** | P0.00/P0.01, internal caps | P0.00/P0.01, external 12 pF caps |
| **Flashing** | J-Link OB (nrfjprog/nrfutil) | PicoProbe SWD → PyOCD runner |
| **Power** | USB/VIN/LiPo regulators | USB VBUS → ME6214C33 LDO → 3.3V |

## Pin Assignments (critical for overlay)

### UART0 (CH340X bridge, app core console)
| Signal | Pin |
|--------|-----|
| TXD | P0.20 |
| RXD | P0.22 |
| CTS | P0.21 (flow control) |
| RTS | not connected |

### SWD Debug (P3 header)
| Signal | P3 pin |
|--------|--------|
| SWDIO | P3.7 |
| SWDCLK | P3.9 |
| RESET | P3.5 |
| GND | P3.1 |

### I2S0 (CJMCU-1334 DAC — wired via P3 header)
| Signal | nRF5340 pin | P3 pin |
|--------|-------------|--------|
| BCK (SCK) | P1.15 | P3.27 |
| LRCK (WS) | P1.12 | P3.21 |
| DIN (SDIN) | P1.13 | P3.23 |

### I2C (available on P2 header for codec control)
| Signal | Pin | P2 pin |
|--------|-----|--------|
| SDA | P1.03 | P2.21 |
| SCL | P1.02 | P2.23 |

## Flashing with PicoProbe (NOT J-Link)

The board uses a Raspberry Pi Pico running picoprobe firmware (CMSIS-DAP).
Standard `CONFIG_BOARD_NRF5340DK_NRF5340_CPUAPP` in `board.cmake` sets
J-Link device args — this won't work. The new board definition must:
- **board.cmake**: Set `pyocd` runner args with `--target nrf5340_xxaa_app` / `nrf5340_xxaa_net`
- **No nrfjprog/jlink/nrfutil**: Those runners are J-Link only

PyOCD flash commands (already documented):
```bash
pyocd flash -t nrf5340_xxaa_app --erase chip build/zephyr/merged.hex
pyocd flash -t nrf5340_xxaa_net --erase chip build/hci_ipc/zephyr/zephyr.hex
```

## What a Board Definition Needs (nRF5340 dual-core)

For each board target (cpuapp, cpunet):

| File | Purpose |
|------|---------|
| `board.yml` | Top-level: board name, vendor, SoC variants |
| `Kconfig.<board>` | Defines `BOARD_<NAME>`, selects SoC per core |
| `Kconfig.defconfig` | Board-level defaults (IPC, memory, MPU) |
| `board.cmake` | Runner args — **pyocd**, not jlink |
| `<board>_<soc>_cpuapp.yaml` | cpuapp target: identifier, RAM/flash, features |
| `<board>_<soc>_cpunet.yaml` | cpunet target: identifier, RAM/flash, features |
| `<board>_<soc>_cpuapp.dts` | cpuapp DT: includes SoC DTSI + board common + partition |
| `<board>_<soc>_cpunet.dts` | cpunet DT: includes SoC DTSI + board common + pinctrl |
| `<board>_<soc>_cpuapp_defconfig` | cpuapp minimal boot config (MPU, TrustZone, GPIO, serial) |
| `<board>_<soc>_cpunet_defconfig` | cpunet minimal boot config (MPU, GPIO, serial) |
| `<board>_common.dtsi` | Shared DT: LEDs, buttons, aliases |
| `*-pinctrl.dtsi` | Pin control — split per core (different pins per core) |

## Plan

1. Create board directory: `boards/e83_2g4m03s_tb/` in repo
2. Create all board definition files (listed above)
3. Replace current `boards/nrf5340dk_nrf5340_cpuapp.overlay` with board DTS
4. Update `prj.conf` → remove DK-specific CONFIGs, add board-appropriate ones
5. Update `sysbuild.cmake` → use new board name for cpunet overlay
6. Update `sysbuild.conf` → ensure it works with new board
7. Test build: `west build -b e83_2g4m03s_tb/nrf5340/cpuapp --sysbuild`
8. Flash with PyOCD (not west flash with jlink runner)
