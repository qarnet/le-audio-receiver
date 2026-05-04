# AGENTS.md — LE Audio Receiver (nRF5340 DK + PCM5102A)

## Gotchas

- **`ACCEPT_JLINK_LICENSE=1` kills non-interactive containers** — J-Link dpkg fails (no udev) and the container startup script exits. Used in `docker-run.sh` (interactive), omitted in `docker-build.sh`.
- **west init -l puts topdir at the manifest's *parent*** — this repo is mounted as `/workspace/app` inside the container, with `/workspace` bound to `~/.cache/le-audio-receiver-workspace/`. west clones (zephyr, nrf, modules) live in that cache, not in this repo. `west.yml` has `self.path: app` to match.
- **The sdk-nrf project must be named `nrf`** in `west.yml` (not `sdk-nrf`). `nrf/modules/modules.cmake` hardcodes `SYSBUILD_NRF_KCONFIG`, which derives from the west project name. Naming it `sdk-nrf` generates an undefined var `SYSBUILD_SDK_NRF_KCONFIG` that resolves to `$ZEPHYR_BASE/`, and cmake fails with "Could not open '/workspace/zephyr/' (EISDIR)".
- **Container runs as host UID:GID** (`--user $(id -u):$(id -g)` + `HOME=/tmp`). No root-owned build artifacts.

## Stack

- nRF Connect SDK **v3.3.0** (`west.yml`)
- Toolchain Docker image: `ghcr.io/nrfconnect/sdk-nrf-toolchain:911f4c5c26`
- Architecture: net core = `hci_ipc` sample (BLE controller with ISO), app core = BAP unicast server (sink-only) + LC3 decode → I2S

## Build & Flash

```bash
./scripts/docker-build.sh          # builds merged.hex
./scripts/docker-run.sh            # interactive shell (for flashing)
# inside the container:
west flash --build-dir build
```

## I2S (PCM5102A)

No MCK — internal PLL via hardware bridge. Pin-configured, no codec binding.

| Signal | nRF Pin | Arduino |
|--------|---------|---------|
| PCM5102A BCK → nRF SCK (BCLK) | P1.15 | D13 |
| PCM5102A LRCK → nRF LRCK (WS) | P1.12 | D10 |
| PCM5102A DIN → nRF SDOUT | P1.13 | D11 |
| PCM5102A SCK — NC (MCK bypass, internal PLL) | — | — |

ACLK 12.288 MHz, `CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y`, I2S master (nRF generates BCLK+LRCK).

## Audio Pipeline

BAP Unicast Server, sink-only. Codec cap: 48 kHz, 10 ms, `CHAN_COUNT_SUPPORT(2)`. Three supported topologies:

| Topology | How |
|---|---|
| 1 stereo ASE (chan_count ≥ 2) | `lc3_decode(..., stereo_out, stride=2)` — standard for Linux/BlueZ clients |
| 2 mono ASEs (chan_count = 1 each) | Separate L/R decode → interleave in `push_stereo()` — phones typically use this |
| 1 mono ASE | Duplicate to both I2S channels |

LC3 decoder memory is embedded in `struct audio_sink` as `lc3_decoder_mem_48k_t dec_mem` — per-stream, not a shared array.

## Key Files

- `src/main.c` — BAP server, ASCS callbacks, LC3 decode, I2S push
- `src/audio_i2s.c` — I2S TX driver (memory-slab + DMA, 48 kHz stereo)
- `boards/nrf5340dk_nrf5340_cpuapp.overlay` — I2S0 pins + ACLK
- `prj.conf` — Kconfig (2 sink ASEs, 0 source ASEs, liblc3, I2S, FPU)
- `west.yml` — manifest: self → app, nrf v3.3.0 with import: true
- `sysbuild.cmake` + `sysbuild.conf` — net core hci_ipc image
- `.clangd` — `--target=arm-none-eabi`, strips ARM GCC flags

## Reference samples

In the west cache at `~/.cache/le-audio-receiver-workspace/`:
- `zephyr/samples/bluetooth/bap_unicast_server/`
- `zephyr/samples/drivers/i2s/echo/`
- `nrf/applications/nrf5340_audio/src/modules/audio_i2s.c`
