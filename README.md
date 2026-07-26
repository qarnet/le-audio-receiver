# LE Audio Receiver

Bluetooth LE Audio BAP Unicast Server — a sink-only receiver that decodes LC3
audio from a BAP unicast source and plays it out over I2S to an external DAC. Built on the
nRF Connect SDK (Zephyr) for the **nRF5340** (Ebyte E83-2G4M03S module), with a
secondary **nRF54L15** (Seeed Xiao) target in progress.

- 2 sink ASEs (mono / stereo Mode A / stereo Mode B)
- LC3 decode via liblc3 → I2S 48 kHz stereo
- SoftDevice-free link layer: BT_LL_SW_SPLIT (Zephyr open-source controller,
  required for ISO) on nRF5340; SDC controller on nRF54L15
- PI clock-recovery controller steering the HFCLKAUDIO APLL from ISO timestamps
  (nRF5340); sample insert/drop actuator on nRF54L15
- VCP volume, CAS, shell diagnostics, watchdog

---

## Bill of materials

| Role | nRF5340 build | nRF54L15 build |
|------|---------------|----------------|
| SoC module | Ebyte **E83-2G4M03S-TB** (nRF5340, no external QSPI) | Seeed **Xiao nRF54L15** |
| Board port | `boards/ebyte/e83_nrf5340/` (custom) | `nrf54l15dk` target + `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` (Xiao remap) |
| I2S DAC | CJMCU-1334 / UDA1334A breakout (see "DAC choice" below) | same |
| Debug probe | Raspberry Pi Pico running **CMSIS-DAP** firmware | Xiao's onboard **SAMD11 CMSIS-DAP** |
| Console | `/dev/ttyUSB0` @ 115200 8N1 (CH340X on E83 module) | Xiao USB CDC (SAMD11 bridge of UART20) |

---

## DAC choice — UDA1334A or PCM5102A

Either works. The project was developed on the **CJMCU-1334 (UDA1334A)** and that
is the default wiring documented below. The **PCM5102A** is the better DAC on
paper (112 dB SNR, 32-bit/384 kHz vs the UDA1334A's 100 dB / 16-bit), but at the
LE Audio sink floor of **48 kHz / 16-bit LC3**, both DACs exceed the codec's
dynamic range by a wide margin — the spec advantage is inaudible here.

Practical differences when wiring to this project's 3-wire no-MCK topology:

- **UDA1334A (CJMCU-1334 / Adafruit #3678):** Format (SF0/SF1) and MUTE are
  pre-pulled to GND on the Adafruit breakout — no config wires needed on that
  specific board. Not all clones or assemblies are equivalent.
- **PCM5102A (GY-PCM5102 and clones):** the **SCK pad must be solder-bridged to
  GND** to enable internal-PLL 3-wire mode. If the pad is open you get silence or
  hiss — the single most-reported PCM5102A "no sound" cause. Adafruit's own
  PCM5102 breakout (#6250) has this handled; cheap clones often don't.

### Hardware validation before trusting a DAC breakout

Before treating a new DAC breakout/wiring assembly as working, validate the I2S
waveform with a standalone test (e.g. a tone loop that drives BCK/LRCK/SDOUT
without the full BAP stack). An old CJMCU-1334-compatible breakout tested with
this project **held LRCK high when unmuted** and is not suitable — the breakout
or wiring assembly was incompatible or defective.

**MUTE high mutes the analog output** (inverted logic — LOW = unmuted). Raising
MUTE is a silence/diagnostic control, not a fix for an I2S-line anomaly.

Use whichever DAC you prefer. Pinout below is identical for both — 3 wires, no
MCK.

---

## I2S wiring — nRF5340 (Ebyte E83-2G4M03S)

Verified against `boards/ebyte/e83_nrf5340/ebyte_e83_nrf5340_nrf5340_cpuapp.dts`
(`i2s0_default` pinctrl). Matches the table in `AGENTS.md`.

| E83 pin | nRF5340 GPIO | I2S signal | → CJMCU-1334 |
|---------|--------------|-----------|---------------|
| — | P1.15 | SCK_M (BCK)  | **BCLK** |
| — | P1.13 | SDOUT (DIN)  | **DIN**  |
| — | P1.12 | LRCK_M (WSEL)| **WSEL** |
| 3V3 | — | — | **VIN** |
| GND | — | — | **GND** + **AGND** (tie both) |

No MCK — UDA1334A internal PLL locks to BCLK. Corresponds to
`CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y` in `prj.conf`.

## I2S wiring — nRF54L15 (Seeed Xiao)

Uses three free Xiao header pins, **D0 / D1 / D2**, all on GPIO port 1 (same
power domain as the working UART20). Avoids the pin-conflict trap in the
previous overlay (P1.10/P1.11/P1.12 collided with i2c22 and pdm20 — Zephyr does
not detect pinctrl overlaps, it silently corrupts the loser).

| Xiao pin | nRF54L15 GPIO | I2S signal | → CJMCU-1334 |
|----------|---------------|-----------|---------------|
| **D0** | P1.4 | SCK_M (BCK)  | **BCLK** |
| **D1** | P1.5 | LRCK_M (WSEL)| **WSEL** |
| **D2** | P1.6 | SDOUT (DIN)  | **DIN**  |
| 3V3 | — | — | **VIN** |
| GND | — | — | **GND** + **AGND** (tie both) |

### CJMCU-1334 / UDA1334A config pins

On the Adafruit breakout (and faithful clones), these are pre-pulled to GND by
on-PCB resistors — verified against the Adafruit PCB schematic
(`adafruit/Adafruit-UDA1334A-I2S-Stereo-DAC-PCB`):

- **SF0** → GND via R10 (format bit 0 → I2S)
- **SF1** → GND via R2  (format bit 1 → I2S)
- **MUTE** → GND via R9 (LOW = unmuted; inverted vs most mute pins)

Leave them unconnected on the Adafruit board. On a bare clone without the
pulldowns, wire all three to GND explicitly.

### Leave unconnected

| CJMCU-1334 pin | Why |
|----------------|-----|
| **SCLK** | system-clock output in video mode; unused in audio mode |
| **PLL** | pulled low by default = audio PLL mode; don't tie high |
| **DEEM** | de-emphasis off; float or GND |
| **3V0** | regulated 3.3 V output from the board's own LDO — do not feed in |

### Audio out

CJMCU-1334 outputs **line level** (no headphone amp on the breakout). Connect:

| CJMCU-1334 | → |
|------------|---|
| **Lout** | left channel → line-in L / headphone L via amp |
| **Rout** | right channel → line-in R / headphone R via amp |
| **AGND** | sleeve / line ground (already tied to GND above) |

### If using the PCM5102A instead

Same D0/D1/D2 (nRF54L15) or P1.15/P1.13/P1.12 (nRF5340) → BCLK / DIN / LRCK
mapping. **Solder-bridge SCK to GND** on cheap GY-PCM5102 clones (see DAC choice
above). No MCK wire.

---

## Build & flash

Requires the Nix flake toolchain (`direnv` or `nix develop`), which pulls the
NCS v3.3.0 toolchain, `openocd-master`, and `nrf-probes` via
[`nix-nrf-dev`](https://github.com/qarnet/nix-nrf-dev).

### nRF5340

```bash
direnv allow          # or: nix develop
fw-build-5340         # west build -b ebyte_e83_nrf5340/nrf5340/cpuapp --sysbuild --pristine
fw-flash-5340         # flashes app + net core, programs UICR.APPROTECT
```

Both cores must be flashed together (the network core runs the `hci_ipc` image
with the SW Split controller). `fw-flash-5340` resolves the probe at flash time
via `nrf-probes` — no probe serial is baked into the build.

After boot, the console (`/dev/ttyUSB0`, 115200 8N1) prints:
```
BLE ready
settings_load() OK
Advertising as "LE Audio Receiver"
```

### nRF54L15

```bash
fw-build-54l15         # builds into build/nrf54l15/
fw-flash-54l15         # OpenOCD via Xiao built-in CMSIS-DAP; RRAM, no flash driver
```

Single-core, no netcore. Console comes over Xiao USB CDC (UART20 bridged by the
onboard SAMD11).

### Passing extra Kconfig

```bash
fw-build-5340 -- -DCONFIG_FOO=y
```

Use `--pristine` (the helpers already do) after any `prj.conf`, overlay, or
`sysbuild.cmake` change.

---

## Pairing

Just Works — MITM enforcement is disabled (`CONFIG_BT_SMP_ENFORCE_MITM=n`) so
centrals that require a passkey UI can still pair. If a central was previously
bonded and now fails to pair after a firmware change, delete the bond on the
central and re-scan, or mass-erase the chip (`nrf53_recover` via `openocd-master`)
before reflashing — `west flash` does not erase the settings partition.

---

## Repository layout

| Path | Purpose |
|------|---------|
| `src/main.c` | Lifecycle wiring, watchdog, advertising restart loop |
| `src/bt_bap.c` | BAP unicast server, ASCS callbacks, PACS, pairing, advertising |
| `src/audio_decode.c` | LC3 decode + channel routing (Mode A / Mode B / mono) |
| `src/audio_sink.h` | Platform-neutral audio-sink interface |
| `src/audio_i2s.c` | I2S TX driver (slab + DMA) — implements `audio_sink.h` |
| `src/audio_drift.c` | PI clock-recovery controller (ppm output) |
| `src/audio_clock_actuator_apll.c` | nRF5340 HFCLKAUDIO APLL actuator |
| `boards/ebyte/e83_nrf5340/` | Custom nRF5340 board: I2S0 pins, ACLK 12.288 MHz, QSPI disabled |
| `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | Xiao nRF54L15 remap: UART20 to SAMD11, I2S20 to D0/D1/D2 |
| `prj.conf` | App Kconfig |
| `sysbuild.cmake` | Applies SW Split DT + Kconfig overlays to `hci_ipc` |
| `tests/` | Unit (`drift`, `audio_decode`) and bsim tests |
| `docs/design.md` | Accepted design doc + phased plan (Phases 0–6) |
| `docs/flashing.md` | Dual-core flash workflow in depth |

---

## Further reading

- **`AGENTS.md`** — the working knowledge base for this repo (gotchas, stack
  summary, key files, build/flash/console conventions). Also the source the
  agents read; `CLAUDE.md` is a symlink to it.
- **`docs/design.md`** — what works, what is in flight, the target architecture,
  and the phased plan. Start here for the "why".
- **`docs/flashing.md`** — OpenOCD, dual-core ordering, APPROTECT, recovery.

---

## License

Apache-2.0 (SPDX header in each source file). Portions adapted from Nordic
Semiconductor ASA samples retain their copyright notices.