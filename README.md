# LE Audio Receiver

A Bluetooth **LE Audio** receiver: it connects to an LE Audio source device
(such as a phone or computer), receives audio over Bluetooth, decodes it, and
plays it out through a small digital-to-analog converter (DAC) into speakers,
headphones, or an amplifier.

This is a hobbyist/open-source project built with the [nRF Connect
SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK)
(Zephyr RTOS). It is a **sink-only** receiver. Meaning it plays audio that another
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
the BAP unicast model: a phone or computer acts as the source and streams
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
- Keeps playback in sync with the source clock using nRF54L15 digital *clock
  recovery / rate matching*, explained in the
  [nRF54L15 technology note](docs/technology/nrf54l15.md).
- Provides a developer shell for diagnostics.

## Supported receiver hardware

The **Seeed XIAO nRF54L15** is this project's sole supported final receiver
hardware. It is small, inexpensive, broadly available in hobby shops, and has
an **onboard debugger** and battery charging. See [Hardware
wiring](docs/hardware-wiring.md) for its DAC connection and
[Technology: nRF54L15](docs/technology/nrf54l15.md) for its audio path.

### Nordic LE Audio platform caveat

The nRF54L15 has **no dedicated Audio PLL**. Nordic states that the nRF54L
series is **not its ideal/recommended platform for all LE Audio /
audio-streaming uses**, that a subset of LE Audio use cases can be supported,
and recommends the nRF5340 for audio today ([Nordic DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/117778/nrf54l15-support-le-audio)).
This is scope context, not a second project hardware choice. Point-to-point
Bluetooth ISO/BAP streaming works on the XIAO; use cases that need a tunable
audio clock, such as TWS-style synchronized playback between two earbuds, are
not covered. Firmware uses a custom digital *clock-recovery / rate-matching*
path for the fixed hardware clock.

The nRF5340 Ebyte receiver is a **retired historical implementation**. Its
former build and flashing helpers are not runnable in the current tree; it is
not supported hardware and has no current build, helper, or release asset.
Historical design and wiring facts remain documented.

## Feature list

- BAP unicast sink (2 sink ASEs), mono / stereo Mode A / stereo Mode B.
- LC3 decode (`liblc3`) → 48 kHz stereo I2S → external DAC.
- VCP volume and mute control from the source device.
- Digital clock recovery and rate matching on nRF54L15.
- Watchdog, developer shell diagnostics, XIAO button/LED pairing control.

## Development and test hardware

Active firmware and lab workflows use **nRF54L15 only**, with **NCS v3.4.1**.
A second XIAO alternates between standalone LE Audio source firmware and the
session-bound Linux HCI UART controller; it never runs both roles at once.
Canonical BabbleSim testing uses two `nrf54l15bsim` peers, while native and
Python unit tests remain portable host tests. No active build, test, flash or
release workflow requires nRF5340 hardware.

The XIAO HCI role is a bounded, lab-qualified development fixture, not a
consumer adapter or a claim of generic no-flow-control UART reliability.
Analog qualification and publication of release binaries remain separate.
See [adapter evaluation](docs/bluetooth-adapter-evaluation.md) and
[platform audit](docs/development/nrf54l15-final-reference-audit-20261001.md).

## Quick start

1. **Use a Seeed XIAO nRF54L15**: it is the sole supported final receiver.
2. **Wire a DAC**: use its simple 3-wire I2S connection to a common DAC
   breakout; see [Hardware wiring](docs/hardware-wiring.md).
3. **Get firmware on the XIAO**: see the [User guide](docs/user-guide.md).
   Ready-made release binaries are planned but **not yet published**; today
   you build from source using the developer workflow documented there.
4. **Pair and play**: put the receiver into pairing mode (see the user
   guide), then connect from an LE Audio source device.

## Supported source devices

LE Audio source devices are still uncommon. Important: a device that plays
**classic Bluetooth audio** (A2DP) is **not automatically an LE Audio
source**. Classic audio capability does not imply BAP unicast-source
support. Check that the phone, tablet, or computer you want to stream from
actually supports Bluetooth LE Audio with BAP unicast.

On Linux, this project has validated the **Intel Wi-Fi 6E AX210** as a BAP
unicast source with this receiver, streaming through a generic desktop
**PipeWire/WirePlumber UI**. The normal Linux/BlueZ/PipeWire desktop path
(the AX210 is an M.2 Wi-Fi card whose Bluetooth function is exposed over
internal USB, so not a plug-in USB stick). Desktop LE Audio on Linux needs a
recent kernel/BlueZ/PipeWire stack (BlueZ 5.85 or later), and most consumer
adapters, including self-contained USB audio dongles, remain unverified
with this receiver. See [Supported LE Audio sources on
Linux](docs/supported-sources.md) for the researched hardware matrix and
software requirements.

Linux sources come in two kinds. **Native HCI adapters** are the
BlueZ/PipeWire path: the LE Audio stack runs on the PC, which is what this
project validates. **Self-contained USB audio transmitters** (dongles) run
the entire Bluetooth stack in their own firmware; the PC just plays audio
to a USB sound card, and the dongle pairs directly with the receiver while
both sides are in pairing mode. Thus no matched transmitter/receiver pair is
required. This project's priority is the native HCI path.

## Documentation

| Document | What it covers |
|---|---|
| [User guide](docs/user-guide.md) | What you need, what to expect, pairing modes, troubleshooting |
| [Supported sources on Linux](docs/supported-sources.md) | Overview/source matrix of researched Linux LE Audio source hardware, with links to the host setup and adapter evaluation guides |
| [Linux LE Audio host setup](docs/linux-le-audio-host-setup.md) | Host OS setup, configuration, and verification for transmitting BAP unicast audio via BlueZ + PipeWire |
| [Bluetooth adapter evaluation](docs/bluetooth-adapter-evaluation.md) | Which Bluetooth adapters are supported and how new adapters get accepted (Intel AX210 project-validated) |
| [Hardware wiring](docs/hardware-wiring.md) | DAC choice, supported XIAO wiring, and historical E83 pin reference |
| [Known limitations](docs/known-limitations.md) | Honest list of current gaps and caveats |
| [Technology: nRF5340](docs/technology/nrf5340.md) | Historical dual-core architecture, controller, audio PLL |
| [Technology: nRF54L15](docs/technology/nrf54l15.md) | Sole final receiver: single-core SDC path, fixed clock, rate matching, ASRC |
| [Developer flashing](docs/flashing.md) | Current XIAO developer workflow with historical nRF5340 appendix |
| [Product backlog](docs/product/README.md) | Backlog.md tasks, lifecycle, and current product work |

## License

Apache-2.0 (SPDX headers in each source file). Portions adapted from Nordic
Semiconductor ASA samples retain their copyright notices.
