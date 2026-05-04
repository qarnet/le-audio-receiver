# AGENTS.md — LE Audio Receiver (nRF5340 DK + PCM5102A)

## Gotchas

- **`ACCEPT_JLINK_LICENSE=1` kills non-interactive containers** — J-Link dpkg fails (no udev) and the container startup script exits. Used in `docker-run.sh` (interactive), omitted in `docker-build.sh`.
- **west init -l puts topdir at the manifest's *parent*** — so this repo is mounted as `/workspace/app` inside the container, with `/workspace` bound to a host cache dir (`~/.cache/le-audio-receiver-workspace`). west clones (zephyr, nrf, modules…) live in that cache, not in this repo. `west.yml` has `self.path: app` to match.
- **The sdk-nrf project must be named `nrf`** in `west.yml` (not `sdk-nrf`). `sdk-nrf/modules/modules.cmake` hardcodes `SYSBUILD_NRF_KCONFIG`, which derives from the west project name. Naming it `sdk-nrf` makes Zephyr generate `osource "$(SYSBUILD_SDK_NRF_KCONFIG)"` — an undefined var that resolves to `$ZEPHYR_BASE/`, and cmake fails with "Could not open '/workspace/zephyr/' (EISDIR)".
- **Container runs as host UID:GID** (`--user $(id -u):$(id -g)` + `HOME=/tmp`). Avoids root-owned build artifacts on the host.

## Stack

- nRF Connect SDK **v3.3.0** (`west.yml`)
- Toolchain Docker image: `ghcr.io/nrfconnect/sdk-nrf-toolchain:911f4c5c26`
- Architecture: net core = `hci_ipc` sample (BLE controller with ISO), app core = BAP unicast server (sink-only) + LC3 decode → I2S

## Build

```bash
./scripts/docker-build.sh
```

First run pulls the toolchain image and `west update`s ~5 GB of clones into `~/.cache/le-audio-receiver-workspace/`. Subsequent runs reuse the cache.

Flash / debug shell:
```bash
./scripts/docker-run.sh
# inside the container:
west flash --build-dir build
```

## I2S (PCM5102A)

No MCK — internal PLL enabled via hardware bridge. No codec binding needed (pin-configured).

| Signal | nRF Pin | Arduino |
|--------|---------|---------|
| SCK/BCLK | P1.15 | D13 |
| LRCK/WS | P1.12 | D10 |
| SDOUT/DIN | P1.13 | D11 |

Config: ACLK clock 12.288MHz, `CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y`, I2S master (nRF generates BCLK+LRCK).

## Audio Pipeline

BAP Unicast Server, sink-only (no source ASE). Codec cap declares `CHAN_COUNT_SUPPORT(2)` (stereo). Single stereo ASE: `lc3_decode()` with `channels=2` produces interleaved L/R PCM pushed to I2S via `audio_i2s.c` memory-slab/DMA driver. Two sink ASE slots configured for phones that send 2 mono streams.

48 kHz target, 10 ms frame duration, 16-bit signed PCM.

## Key Files

- `src/main.c` — BAP server + LC3 decode + I2S push
- `src/audio_i2s.c` — I2S TX driver (Zephyr I2S API, DMA, 48kHz stereo)
- `boards/nrf5340dk_nrf5340_cpuapp.overlay` — I2S0 pins + ACLK config
- `prj.conf` — full Kconfig
- `sysbuild.cmake` + `sysbuild.conf` — net core image integration
- `.clangd` — strips ARM GCC flags, sets `--target=arm-none-eabi`

Reference samples (in west cache, e.g. `~/.cache/le-audio-receiver-workspace/`):
- `zephyr/samples/bluetooth/bap_unicast_server/`
- `zephyr/samples/drivers/i2s/echo/`
- `nrf/applications/nrf5340_audio/src/modules/audio_i2s.c`

## Known broken (as of commit 801fb5c)

Build infrastructure works end-to-end; app source has unresolved errors:
- `src/main.c:66` — `unknown type name 'lc3_decoder_t'`. Missing `#include <lc3.h>`. Cascade of `-Wint-conversion` warnings on the `decoder` field (lines 225/275/305/319/398/497) confirms the field type is wrong (declared as `int`, should be `lc3_decoder_t`).
- `src/audio_i2s.c:44` — `.timeout = K_NO_WAIT` initializes an `int` field with a `k_timeout_t`. The `i2s_config.timeout` field is `int32_t` (milliseconds) — use `0` or `SYS_FOREVER_MS`.
