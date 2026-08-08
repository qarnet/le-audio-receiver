# LE Audio Receiver

A Bluetooth **LE Audio** receiver: it connects to an LE Audio source device
(such as a phone or computer), receives audio over Bluetooth, decodes it, and
plays it out through a small digital-to-analog converter (DAC) into speakers,
headphones, or an amplifier.

This is a hobbyist/open-source project built with the [nRF Connect
SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK)
(Zephyr RTOS). It is a **sink-only** receiver — it plays audio that another
device sends to it; it does not transmit audio itself.

---

## What is Bluetooth LE Audio?

Bluetooth LE Audio is the modern generation of Bluetooth audio. Unlike
"classic" Bluetooth audio (A2DP), LE Audio:

- Uses the **LC3** codec, which sounds good at low bit rates and low power.
- Uses **Isochronous Channels (ISO)**, a low-latency Bluetooth transport
  designed for streaming.
- Streams through the **BAP** (Basic Audio Profile) unicast model: a *source*
  device sends audio to one or more *sink* (receiver) devices.

LE Audio is still new: source devices are uncommon today (see
[Supported source devices](#supported-source-devices)). This project follows
the BAP unicast model — a phone or computer acts as the source and streams
LC3 audio to this receiver.

## What this project does

- Runs as a **BAP Unicast Server (sink)**: it advertises its audio services
  and accepts audio from a BAP unicast source.
- Decodes **LC3** audio with the open-source `liblc3` codec and plays it out
  over **I2S at 48 kHz stereo** to an external DAC board.
- Supports the three common LE Audio stream shapes:
  - **Mono** (one channel),
  - **Stereo Mode A** (two separate mono streams, one per channel),
  - **Stereo Mode B** (one stream carrying both channels).
- Implements **Volume Control Profile (VCP)**: volume and mute are controlled
  from the source device.
- Keeps playback in sync with the source clock using *clock recovery* — the
  details differ by chip and are explained in the
  [technology notes](docs/technology/).
- Provides a developer shell for diagnostics (firmware built by developers
  only).

## Why two hardware paths exist

Building an LE Audio receiver with hobby hardware means choosing between two
imperfect options:

- The **nRF5340** is the chip Nordic Semiconductor targets for LE Audio — it is
  the basis of Nordic's LE Audio reference applications and qualification
  work. But Nordic's own development kit is large and expensive. This project
  instead uses a compact **Ebyte E83** module containing an nRF5340. That
  module is practical for hobby use, but it has no onboard debugger: flashing
  requires an **external debug probe** (for example a Raspberry Pi Pico
  running CMSIS-DAP firmware), and this repository's tested flashing path uses
  an OpenOCD build from mainline/master. This is not a claim that no other
  nRF5340 modules exist — the E83 was simply the compact module found for this
  project.
- The **nRF54L15** (on the small **Seeed Xiao** board) is the opposite choice:
  tiny, inexpensive, broadly available in hobby shops, with an **onboard
  debugger** and battery charging. The catch: it has **no dedicated Audio
  PLL**, and Nordic's official position is that the nRF54L series is therefore
  **not its ideal/recommended platform for all LE Audio / audio-streaming
  uses** — Nordic states that a subset of LE Audio use cases can be supported,
  and recommends the nRF5340 for audio today ([Nordic DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/117778/nrf54l15-support-le-audio)).
  Bluetooth ISO/BAP streaming is not impossible here: point-to-point LE Audio
  works on it, while use cases that depend on a tunable audio clock (such as
  TWS-style synchronized playback between two earbuds) are not covered. This
  project makes up for the missing hardware clock with a custom digital
  *clock-recovery / rate-matching* path in firmware.

The two build targets are compared below.

## Choosing a platform

| | nRF5340 build (Ebyte E83) | nRF54L15 build (Seeed Xiao) |
|---|---|---|
| **Nordic LE Audio reference platform** | ✔ Yes | ✘ No |
| **Compact hobby board with onboard debugger** | ✘ No | ✔ Yes |

Legend: **✔ Yes** = a strength of this target, **✘ No** = a limitation of
this target. The words are the meaning; the marks are a quick visual cue.

**Choose the nRF5340 (Ebyte E83) if** you want the chip Nordic recommends for
LE Audio, and you can supply an external CMSIS-DAP debug probe and follow the
repository's tested flashing workflow. It is the more "reference-like" path.

**Choose the nRF54L15 (Seeed Xiao) if** you want a compact, low-cost board
with an onboard debugger and battery charging, and you accept that this is
not Nordic's LE Audio reference platform — the firmware does its own digital
clock recovery instead. The nRF54L15 is not "unsupported" here; it is a fully
supported build target of this project.

See [Hardware wiring](docs/hardware-wiring.md) for what to wire on either
board, and the [technology notes](docs/technology/) for how each chip's audio
path works.

## Feature list

- BAP unicast sink (2 sink ASEs), mono / stereo Mode A / stereo Mode B.
- LC3 decode (`liblc3`) → 48 kHz stereo I2S → external DAC.
- VCP volume and mute control from the source device.
- Clock recovery on both platforms (audio PLL trimming on nRF5340, digital
  rate matching on nRF54L15).
- Watchdog, developer shell diagnostics, board-specific pairing control.

## Quick start

1. **Pick a platform** — see [Choosing a platform](#choosing-a-platform).
2. **Wire a DAC** — both boards use a simple 3-wire I2S connection to a
   common DAC breakout; see [Hardware wiring](docs/hardware-wiring.md).
3. **Get firmware on the board** — see the [User guide](docs/user-guide.md).
   Ready-made release binaries are planned but **not yet published**; today
   you build from source using the developer workflow documented there.
4. **Pair and play** — put the receiver into pairing mode (see the user
   guide), then connect from an LE Audio source device.

## Supported source devices

LE Audio source devices are still uncommon. Important: a device that plays
**classic Bluetooth audio** (A2DP) is **not automatically an LE Audio
source** — classic audio capability does not imply BAP unicast-source
support. Check that the phone, tablet, or computer you want to stream from
actually supports Bluetooth LE Audio with BAP unicast.

On Linux, this project has validated the **Intel Wi-Fi 6E AX210** as a BAP
unicast source with this receiver — using the repository's own custom
BlueZ source tool (`scripts/bap_central.py`), not the desktop PipeWire UI
(Linux/BlueZ/PipeWire path; the AX210 is an M.2 Wi-Fi card whose Bluetooth
function is exposed over internal USB — not a plug-in USB stick). Desktop
LE Audio on Linux needs a recent kernel/BlueZ/PipeWire stack, and most
consumer adapters — including self-contained USB audio dongles — remain
unverified with this receiver. See [Supported LE Audio sources on
Linux](docs/supported-sources.md) for the researched hardware matrix and
software requirements.

As a development and test path, this repository includes a **central test
tool** (`scripts/bap_central.py`) that streams LC3 test tones from a Linux PC
with a compatible LE Audio controller and BlueZ — useful for verifying a
receiver build without hunting for a consumer source device. The repository
documentation describes the tested Linux setup; it is a developer workflow,
not a consumer feature.

## Documentation

| Document | What it covers |
|---|---|
| [User guide](docs/user-guide.md) | What you need, what to expect, pairing modes, troubleshooting |
| [Supported sources on Linux](docs/supported-sources.md) | Researched Linux LE Audio source hardware and software requirements (AX210 project-validated) |
| [Hardware wiring](docs/hardware-wiring.md) | DAC choice and verified I2S pin wiring for both boards |
| [Known limitations](docs/known-limitations.md) | Honest list of current gaps and caveats |
| [Technology: nRF5340](docs/technology/nrf5340.md) | Dual-core architecture, controller, audio PLL, flashing constraints |
| [Technology: nRF54L15](docs/technology/nrf54l15.md) | Single-core SDC path, fixed clock, rate matching, ASRC |
| [Flashing (developers)](docs/flashing.md) | Detailed nRF5340 dual-core flashing workflow |
| [Planned features](PLANNED_FEATURES.md) | Structured backlog of planned work |

## License

Apache-2.0 (SPDX headers in each source file). Portions adapted from Nordic
Semiconductor ASA samples retain their copyright notices.
