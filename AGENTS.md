# AGENTS.md — LE Audio Receiver (nRF5340 DK + PCM5102A)

## Gotchas

- **`ACCEPT_JLINK_LICENSE=1` kills non-interactive containers** — J-Link dpkg fails (no udev) and the script exits. Used in `docker-run.sh` (interactive TTY), omitted in `docker-build.sh`.
- **Sysbuild netcore selection** — `NET_CORE_IMAGE_HCI_IPC` Kconfig string comparison never resolves in sysbuild context. Use `SB_CONFIG_NETCORE_HCI_IPC=y` in `sysbuild.conf` + `add_overlay_config(..., nrf5340_cpunet_iso-bt_ll_sw_split.conf)` in `sysbuild.cmake` instead.
- **Native toolchain ≠ Docker workspace layout** — `west init -l` sets topdir to the manifest's *parent*. In Docker, this repo is mounted as `/workspace/app` with `self.path: app` in `west.yml`. Natively (`~/ncs/v3.3.0`), this repo is a freestanding app — do not `west init -l` here; just use `nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 --shell`.
- **The sdk-nrf west project must be named `nrf`** — not `sdk-nrf`. `nrf/modules/modules.cmake` hardcodes `SYSBUILD_NRF_KCONFIG`. Naming it `sdk-nrf` breaks cmake with "Could not open '/workspace/zephyr/' (EISDIR)".
- **Container runs as host UID:GID** (`--user $(id -u):$(id -g)` + `HOME=/tmp`). No root-owned build artifacts.

## Stack

- nRF Connect SDK **v3.3.0**
- Net core: `hci_ipc` with `nrf5340_cpunet_iso-bt_ll_sw_split.conf` (ISO over BT LL)
- App core: BAP Unicast Server (sink-only) + LC3 decode → I2S
- Toolchain:
  - Native: nrfutil `sdk-manager install v3.3.0` → `~/ncs/`
  - Docker: `ghcr.io/nrfconnect/sdk-nrf-toolchain:911f4c5c26`

## Build

Native (recommended):
```bash
nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 --shell
# inside toolchain shell:
west build -b nrf5340dk/nrf5340/cpuapp --sysbuild
```

Docker (for J-Link or isolation):
```bash
./scripts/docker-build.sh    # builds merged.hex
```

## Flash

Both cores (app + hci_ipc network core) must be flashed.

Native:
```bash
# inside toolchain shell
west flash --build-dir build
```

Docker interactive:
```bash
./scripts/docker-run.sh      # privileged container with /dev
west flash --build-dir build
```

## Debugging

After flashing both cores, verify via UART/RTT that the BLE stack initialises correctly before connecting a client.

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

- `src/main.c` — BAP server, ASCS callbacks, LC3 decode, I2S push
- `src/audio_i2s.c` — I2S TX driver (slab + DMA, 48 kHz stereo)
- `boards/nrf5340dk_nrf5340_cpuapp.overlay` — I2S0 pins, ACLK
- `prj.conf` — Kconfig (2 sink ASEs, 0 source ASEs, liblc3, FPU)
- `sysbuild.cmake` / `sysbuild.conf` — net core hci_ipc ISO config
- `west.yml` — manifest: `self.path: app`, `nrf` v3.3.0
- `.clangd` — `--target=arm-none-eabi`, strips ARM GCC flags

## Reference Samples

In the west cache (`~/.cache/le-audio-receiver-workspace/`):
- `zephyr/samples/bluetooth/bap_unicast_server/`
- `zephyr/samples/drivers/i2s/echo/`
- `nrf/applications/nrf5340_audio/src/modules/audio_i2s.c`
