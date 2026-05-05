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
- **`ACCEPT_JLINK_LICENSE=1` kills non-interactive containers** — J-Link dpkg fails (no udev) and the script exits. Used in `docker-run.sh` (interactive TTY), omitted in `docker-build.sh`.
- **Native toolchain ≠ Docker workspace layout** — `west init -l` sets topdir to the manifest's *parent*. In Docker, this repo is mounted as `/workspace/app` with `self.path: app` in `west.yml`. Natively (`~/ncs/v3.3.0`), this repo is a freestanding app — do not `west init -l` here.
- **The sdk-nrf west project must be named `nrf`** — `nrf/modules/modules.cmake` hardcodes `SYSBUILD_NRF_KCONFIG`. Naming it `sdk-nrf` breaks cmake with "Could not open '/workspace/zephyr/' (EISDIR)".
- **Container runs as host UID:GID** (`--user $(id -u):$(id -g)` + `HOME=/tmp`). No root-owned build artifacts.
- **WSL2 requires `usbipd`** on the Windows host to bind the J-Link USB device to WSL2. Without it, `west flash` and JLinkExe fail with "Cannot connect to the probe/programmer".

## Stack

- nRF Connect SDK **v3.3.0**
- Net core: `hci_ipc` (Zephyr sample) with `nrf5340_cpunet_iso_peripheral-bt_ll_sw_split.conf` + `bt-ll-sw-split` DTS overlay
- App core: BAP Unicast Server (sink-only) + LC3 decode → I2S
- Link Layer: **BT_LL_SW_SPLIT** (open-source Zephyr controller, required for ISO on net core)
- Toolchain:
  - Native: nrfutil `sdk-manager install v3.3.0` → `~/ncs/`
  - Docker: `ghcr.io/nrfconnect/sdk-nrf-toolchain:911f4c5c26`

## Build

Native (recommended):
```bash
nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 -- bash -c "cd ~/ncs/v3.3.0 && west build -b nrf5340dk/nrf5340/cpuapp --sysbuild"
```
(The app is a freestanding source directory; build from the NCS root to resolve `ZEPHYR_BASE` and `samples`.)

Docker:
```bash
./scripts/docker-build.sh    # builds merged.hex
```

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
| 1 mono ASE | Duplicate to both I2S channels |

LC3 decoder memory is per-stream (`lc3_decoder_mem_48k_t dec_mem` inside `struct audio_sink`), not a shared array.

## Key Files

- `src/main.c` — BAP server, ASCS callbacks, LC3 decode, I2S push, pairing callbacks (lines 583-607)
- `src/audio_i2s.c` — I2S TX driver (slab + DMA, 48 kHz stereo)
- `boards/nrf5340dk_nrf5340_cpuapp.overlay` — I2S0 pins, ACLK
- `prj.conf` — Kconfig (ACL buffers, SMP/pairing, 2 sink ASEs, liblc3, FPU, ZMS)
- `sysbuild.cmake` — applies `bt-ll-sw-split` DTS overlay + `iso_peripheral` Kconfig overlay to hci_ipc
- `sysbuild.conf` — `SB_CONFIG_NETCORE_HCI_IPC=y`
- `CMakeLists.txt` — `find_package(Zephyr ...)`; targets `src/main.c` and `src/audio_i2s.c`
