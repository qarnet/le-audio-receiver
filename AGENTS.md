# nRF Connect SDK — Knowledge Lookup Rules

The nRF Connect SDK is installed at `~/ncs/`. The exact version directory
varies (e.g. `~/ncs/v2.7.0/`). Resolve it with:
  ls -d ~/ncs/v*/ | sort -V | tail -1

Treat the installed source tree as the authoritative reference. Do NOT
guess at Kconfig symbols, devicetree compatibles, or API signatures —
grep the source. The web docs are a JavaScript SPA and cannot be fetched.

## Kconfig discovery

Every CONFIG_FOO is defined in a `Kconfig*` file with format:
    config FOO
        bool "Short description"
        default n
        depends on BAR
        help
          Multi-line help text explaining the option.

Workflow when you need a Kconfig symbol:
  1. Grep for the symbol definition (NOT just usages):
       grep -rn "^config FOO\b" ~/ncs/v*/nrf ~/ncs/v*/zephyr ~/ncs/v*/modules
  2. View the surrounding Kconfig block to read the help text and deps.
  3. If searching by topic, grep `Kconfig*` files for keywords:
       grep -rn -i "lte modem" ~/ncs/v*/nrf --include="Kconfig*"

When the user has a built project, prefer the resolved config:
  build/zephyr/.config           — final merged config (post-Kconfig)
  build/zephyr/include/generated/zephyr/autoconf.h
These show what is ACTUALLY enabled, vs. what is merely declared.

For interactive exploration of available options for a given app/board:
    west build -t menuconfig
    west build -t guiconfig
Only suggest these to the user; do not run them headlessly.

## Devicetree

Bindings (the "schema" for `compatible = "..."` strings) live in:
  ~/ncs/v*/zephyr/dts/bindings/    — upstream
  ~/ncs/v*/nrf/dts/bindings/       — Nordic-specific
  ~/ncs/v*/modules/**/dts/bindings/ — module-provided

To find a binding by compatible string:
  grep -rln 'compatible: *"nordic,nrf-spim"' ~/ncs/v*/{zephyr,nrf,modules}/dts/bindings

Board DTS files: ~/ncs/v*/{zephyr,nrf}/boards/**/*.dts
SoC-level DTSI:  ~/ncs/v*/{zephyr,nrf}/dts/

For a built project, the merged/resolved devicetree is at:
  build/zephyr/zephyr.dts             — full resolved DT (very useful)
  build/zephyr/include/generated/zephyr/devicetree_generated.h

When suggesting a node, always check the binding's `properties:` block
for required vs. optional fields.

## Headers / APIs

Public Zephyr headers:  ~/ncs/v*/zephyr/include/zephyr/
Public Nordic headers:  ~/ncs/v*/nrf/include/
nrfxlib headers:        ~/ncs/v*/nrfxlib/**/include/
HAL (nrfx):             ~/ncs/v*/modules/hal/nordic/nrfx/

Doxygen comments in the headers are typically more current than the
rendered docs. When asked "how do I use X", grep the header for the
function declaration and read the surrounding /** ... */ block.

## Samples — the best learning resource

  ~/ncs/v*/nrf/samples/        — Nordic samples (start here)
  ~/ncs/v*/nrf/applications/   — fuller reference apps
  ~/ncs/v*/zephyr/samples/     — upstream Zephyr samples

When the user asks "how do I do X", search samples for a working
example before writing one from scratch:
  grep -rln "<api or kconfig>" ~/ncs/v*/nrf/samples ~/ncs/v*/zephyr/samples

Each sample has a `prj.conf`, `sample.yaml`, and often board-specific
overlays in `boards/`. These are concrete, working references.

## Doc sources (RST/MD)

The web docs are unreachable, but their source lives at:
  ~/ncs/v*/nrf/doc/nrf/         — nRF Connect SDK docs source
  ~/ncs/v*/zephyr/doc/          — Zephyr docs source
Grep these as a fallback for conceptual/overview content that isn't
captured in code comments.

## What NOT to do

- Don't fabricate Kconfig symbol names. If you cannot grep it, say so.
- Don't propose a `compatible` string without confirming a binding exists.
- Don't web-search for nRF Connect SDK docs — fetches will fail or
  return empty. Use the local tree.
- Don't suggest API calls without verifying the function exists in a
  header in the local tree.

---

# AGENTS.md — LE Audio Receiver (nRF5340 DK + PCM5102A)

## Current Status (2026-05-05)

### Problem found: PACS/ASCS characteristics unreadable in nRF Connect app
Generic Access and Generic Attribute services are readable, but PACS and ASCS are not.
Root cause: `settings_load()` was missing from `main.c` after `bt_enable()`.
`CONFIG_BT_GATT_DYNAMIC_DB=y` requires `settings_load()` to be called before GATT dynamic attributes (PACS, ASCS) become visible to remote peers.
Also: ZMS settings backend was silently falling through to `SETTINGS_NONE` (all `settings_save` returned `-2`). Switched to NVS (`CONFIG_NVS=y` + `CONFIG_SETTINGS_NVS=y` in `prj.conf`).

### Fix applied — needs verification
- Added `#include <zephyr/settings/settings.h>` to `src/main.c`
- Added `settings_load()` call after `bt_enable()` in `main.c:625`
- Switched `prj.conf` from ZMS to NVS (see Kconfig section)
- Rebuilt and flashed both cores successfully (2026-05-05)

### Next steps
1. Monitor `/dev/ttyACM0` (app core, 115200 8N1) after boot — confirm no errors from `bt_pacs_register`, `bt_bap_unicast_server_register`, `set_location`, `set_supported_contexts`, `set_available_contexts`, or `settings_load`.
2. Connect with **nRF Connect app** — PACS and ASCS characteristics should now be readable.
3. Re-pair with **Pixel 6a (Android 16)** — go to Bluetooth settings, forget the device, reconnect. Android LE Audio service requires bonding (LTK stored via NVS) to route audio.
4. If PACS still unreadable: check `bt_pacs_register` return value (currently checked), and `bt_bap_unicast_server_register` return value (currently unchecked — add error check).
5. If Android still won't route audio after bonding works: check that `BT_AUDIO_CONTEXT_TYPE_MEDIA` is in both supported and available contexts, and that the advertisement includes `BT_UUID_ASCS_VAL` with `BT_AUDIO_UNICAST_ANNOUNCEMENT_GENERAL`.

## Gotchas

- **SW Split LL requires BOTH a DT overlay AND a Kconfig overlay** — `add_overlay_config()` alone sets Kconfig, but the nRF5340 cpunet DTS defaults to `bt_hci_sdc` (SoftDevice). Without `add_overlay_dts(..., bt-ll-sw-split.overlay)` the net core quietly stays on SoftDevice and `bt_enable()` fails with `Bluetooth init failed: -5` (`HOST_BUFFER_SIZE` opcode `0x0c33` returns `UNSUPPORTED_FEATURE`).
- **ACL buffer sizes must match the SW Split controller** — the overlay uses `CONFIG_BT_CTLR_DATA_LENGTH_MAX=251`. If the app core's `CONFIG_BT_BUF_ACL_RX_SIZE` is too small (default 27), `bt_enable()` fails with `Invalid Param` (status `0x12`) for `HOST_BUFFER_SIZE`. Set `CONFIG_BT_BUF_ACL_RX_SIZE=255`, `CONFIG_BT_BUF_ACL_TX_SIZE=251`, `CONFIG_BT_BUF_CMD_TX_SIZE=255`.
- **The full `iso-bt_ll_sw_split.conf` overflows the 64 KB net core RAM by ~8 KB**. Use the trimmed `nrf5340_cpunet_iso_peripheral-bt_ll_sw_split.conf` for sink-only unicast.
- **Phones require Just Works pairing** — the default `CONFIG_BT_SMP_ENFORCE_MITM=y` forces authenticated pairing, but without passkey/NumCompare UI the phone shows "incorrect PIN". Disable MITM and add `pairing_accept` / `pairing_complete` / `pairing_failed` callbacks returning `BT_SECURITY_ERR_SUCCESS`. `main.c:583-607` has these.
- **ZMS bond storage is configured (`SETTINGS_ZMS=y`) but may fall through to `SETTINGS_NONE`** if the storage partition isn't correctly defined in DT. Bonds won't persist across reboots until this is fully wired.
- **The sdk-nrf west project must be named `nrf`** — `nrf/modules/modules.cmake` hardcodes `SYSBUILD_NRF_KCONFIG`. Naming it `sdk-nrf` breaks cmake with "Could not open '/workspace/zephyr/' (EISDIR)".
- **WSL2 requires `usbipd`** on the Windows host to bind the J-Link USB device to WSL2. Without it, `west flash` and JLinkExe fail with "Cannot connect to the probe/programmer".

## Stack

- nRF Connect SDK **v3.3.0**
- Net core: `hci_ipc` (Zephyr sample) with `nrf5340_cpunet_iso_peripheral-bt_ll_sw_split.conf` + `bt-ll-sw-split` DTS overlay
- App core: BAP Unicast Server (sink-only) + LC3 decode → I2S
- Link Layer: **BT_LL_SW_SPLIT** (open-source Zephyr controller, required for ISO on net core)
- Toolchain:
  - Native: nrfutil `sdk-manager install v3.3.0` → `~/ncs/`

## Build

```bash
nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 -- bash -c "cd ~/ncs/v3.3.0 && west build -b nrf5340dk/nrf5340/cpuapp --sysbuild"
```
(The app is a freestanding source directory; build from the NCS root to resolve `ZEPHYR_BASE` and `samples`.)

## Flash

Both cores (app + hci_ipc network core) must be flashed. `west flash` uses the **nrfutil** runner.

```bash
nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 -- bash -c "cd ~/ncs/v3.3.0 && west flash --build-dir build"
```

## Serial Debugging

App core UART: `/dev/ttyACM0`, 115200 8N1
Net core UART: `/dev/ttyACM1`, 115200 8N1

```bash
stty -F /dev/ttyACM0 115200 raw -echo && cat /dev/ttyACM0
```

After flashing, the boot log should show `BLE ready` and `Advertising as "LE Audio Receiver"`. The `i2s_nrfx: Next buffers not supplied on time` error is expected until an LE Audio client connects and starts streaming.

## I2S (PCM5102A)

No MCK — internal PLL via hardware bridge. Pin-configured, no codec binding.

| Signal | nRF Pin | Arduino |
|---|---|---|
| BCK (BCLK) | P1.15 | D13 |
| LRCK (WS) | P1.12 | D10 |
| DIN (SDOUT) | P1.13 | D11 |
| SCK (MCK) | NC | — |

ACLK 12.288 MHz. `CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y`, I2S master (nRF generates BCLK+LRCK).

## Audio Pipeline

BAP Unicast Server, sink-only. Codec cap: 48 kHz, 10 ms, `CHAN_COUNT_SUPPORT(2)`.

| Topology | How |
|---|---|
| 1 stereo ASE | `lc3_decode(..., stereo_out, stride=2)` — Linux/BlueZ clients |
| 2 mono ASEs | Separate L/R decode → interleave in `push_stereo()` — phones |
| Mono | Duplicate to both I2S channels |

LC3 decoder memory is per-stream (`lc3_decoder_mem_48k_t dec_mem` inside `struct audio_sink`), not a shared array.

## Key Files

- `src/main.c` — BAP server, ASCS callbacks, LC3 decode, I2S push, pairing callbacks (lines 583-607)
- `src/audio_i2s.c` — I2S TX driver (slab + DMA, 48 kHz stereo)
- `boards/nrf5340dk_nrf5340_cpuapp.overlay` — I2S0 pins, ACLK
- `prj.conf` — Kconfig (ACL buffers, SMP/pairing, 2 sink ASEs, liblc3, FPU, ZMS)
- `sysbuild.cmake` — applies `bt-ll-sw-split` DTS overlay + `iso_peripheral` Kconfig overlay to hci_ipc
- `sysbuild.conf` — `SB_CONFIG_NETCORE_HCI_IPC=y`
- `CMakeLists.txt` — `find_package(Zephyr ...)`; targets `src/main.c` and `src/audio_i2s.c`
