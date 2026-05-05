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
directory there. `ZEPHYR_BASE` and sample paths must resolve.

```bash
cd ~/ncs/v3.3.0
nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 -- \
  bash -c "cd ~/ncs/v3.3.0 && west build -b nrf5340dk/nrf5340/cpuapp --sysbuild --pristine"
```

Use `--pristine` after any `prj.conf`, overlay, or `sysbuild.cmake` change.
The build tree is at `~/ncs/v3.3.0/build/` (not in the repo).

## Flash

Both app core and hci_ipc network core must be flashed:

```bash
cd ~/ncs/v3.3.0
nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 -- \
  bash -c "cd ~/ncs/v3.3.0 && west flash --build-dir build"
```

This flashes `build/merged.hex` (app) then `build/merged_CPUNET.hex` (net).

## Serial

App core output: `/dev/ttyACM1` (not ACM0) at **115200 8N1**.
Net core output: `/dev/ttyACM1` sometimes forwards both.

```bash
stty -F /dev/ttyACM1 115200 raw -echo && cat /dev/ttyACM1
```

Expected after boot: `BLE ready`, `settings_load() OK`,
`Advertising as "LE Audio Receiver"`. The `i2s_nrfx: Next buffers not
supplied on time` error is normal until a client connects and starts
streaming.

## Gotchas

### SW Split LL requires BOTH a DT overlay AND a Kconfig overlay

`add_overlay_config()` alone sets Kconfig, but the nRF5340 cpunet DTS
defaults to `bt_hci_sdc` (SoftDevice). Without `add_overlay_dts(...,
bt-ll-sw-split.overlay)` the net core quietly stays on SoftDevice and
`bt_enable()` fails with `Bluetooth init failed: -5`
(`HOST_BUFFER_SIZE` returns `UNSUPPORTED_FEATURE`).

See `sysbuild.cmake` for how both overlays are applied to `hci_ipc`.

### `settings_load()` must run after `bt_enable()`

`CONFIG_BT_GATT_DYNAMIC_DB=y` registers PACS/ASCS dynamically. Without
`settings_load()` these characteristics are invisible to remote peers.
The call must be after `bt_enable(NULL)` and before `bt_pacs_register()`.

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

## Stack

- App: BAP Unicast Server sink-only, 2 sink ASEs, LC3 decode → I2S
- Net: `hci_ipc` with `nrf5340_cpunet_iso_peripheral-bt_ll_sw_split.conf`
- Link Layer: BT_LL_SW_SPLIT (Zephyr open-source controller, ISO required)
- DAC: PCM5102A, no MCK, `CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y`

## Key Files

| File | Purpose |
|------|---------|
| `src/main.c` | BAP server, ASCS callbacks, LC3 decode, I2S push, pairing |
| `src/audio_i2s.c` | I2S TX driver (slab + DMA, 48 kHz stereo) |
| `boards/nrf5340dk_nrf5340_cpuapp.overlay` | I2S0 pins, ACLK 12.288 MHz |
| `prj.conf` | App Kconfig (ACL/ISO buffers, SMP, 2 ASEs, liblc3, FPU, ZMS) |
| `sysbuild.cmake` | Applies SW Split DT overlay + Kconfig overlay to hci_ipc |
| `sysbuild.conf` | `SB_CONFIG_NETCORE_HCI_IPC=y` |
