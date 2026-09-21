# User guide

This guide covers the **Seeed XIAO nRF54L15**, this project's sole supported
final receiver: what you need, what to expect on boot, how to pair, and how
the pairing modes behave.

## What you need

- A receiver board with firmware installed (see
  [Getting firmware on the board](#getting-firmware-on-the-board)).
- An I2S DAC wired to the board (see
  [Hardware wiring](hardware-wiring.md)).
- A Bluetooth LE Audio **source** device: a phone, tablet, or computer that
  supports Bluetooth LE Audio with BAP unicast source. LE Audio source devices
  are still uncommon; a device that only supports classic Bluetooth audio
  (A2DP) will not work. See [Supported source
  devices](#supported-source-devices).

## Getting firmware on the board

**Ready-made release binaries are planned but not yet published.** Nothing is
available for download today; the firmware must be built from source using the
developer toolchain documented in the repository. This section explains what
flashing a build looks like so the expected flow is clear.

- **nRF54L15 build (Seeed XIAO):** flashing uses the board's **onboard
  debugger** (the SAMD11 USB bridge). A public-friendly flashing method is
  still under evaluation; the current route is the developer workflow
  (see [PB-008](product/backlog/tasks/pb-008%20-%20Provide-public-friendly-nRF54L15-flashing.md)). Expect this section to be
  updated once a simple method is settled.

## Power on and boot

After power is applied, the receiver boots, initializes Bluetooth, and starts
advertising under the name **"LE Audio Receiver"**. You should see the device
in a Bluetooth scan shortly after power-up. The board's LED reflects the
current pairing mode (below).

## Connecting and pairing

1. Power the receiver and wait for it to start advertising.
2. On your source device, scan for Bluetooth devices and look for **"LE Audio
   Receiver"**.
3. Select it and pair. Pairing is **Just Works**: no PIN or passkey is
   needed.
4. After pairing, the source device can start streaming audio to the
   receiver. Use the source's volume controls: the receiver implements the
   Volume Control Profile (VCP), so volume and mute changes on the source are
   applied to the playback.

## Pairing modes

The receiver has three modes. The Seeed XIAO nRF54L15 has a physical user
button and LED for this behavior.

| Mode | How to enter | LED | What it means |
|---|---|---|---|
| **NORMAL** | Boot default, or after a successful pairing | Off | Advertising is restricted to devices that are already bonded. Previously paired devices reconnect without re-pairing; new devices cannot pair. |
| **BONDING** | Press and hold the button through **3 seconds**, release **before 8 seconds** | Slow blink (~0.5 s on / 0.5 s off) | Advertising opens to allow a new device to connect and pair. Existing bonds are kept. |
| **RESET** | Hold the button through **8 seconds** (supersedes BONDING at 3 s) | Fast flash (~5 flashes in 1 second), then enters BONDING | All saved pairings are deleted, and the receiver returns to BONDING so you can pair fresh. |

Notes:

- A short press (released before 3 seconds) does nothing.
- **NORMAL → BONDING** also suspends advertising first and disconnects any
  active peer, so a BONDING advertisement never competes with an existing
  connection.
- After a successful BONDING pair, the receiver returns to NORMAL without
  dropping the new connection.
- If a previously bonded device reconnects during BONDING and completes
  security, the receiver treats that as successful completion and returns to
  NORMAL.

## Supported source devices

LE Audio is a newer standard and source devices remain uncommon. Capabilities
to look for:

- **LE Audio / BAP unicast source** support (check the spec sheet or
  settings, e.g. "LE Audio" or "Bluetooth LE Audio" toggle).
- **LC3** codec support at 48 kHz (the receiver currently accepts 48 kHz
  only; see [Known limitations](known-limitations.md)).

Classic Bluetooth audio capability (A2DP) **does not** imply LE Audio source
support.

For Linux specifically, see [Supported LE Audio sources on
Linux](supported-sources.md): it explains the BlueZ/PipeWire software
requirements, distinguishes native HCI adapters from self-contained USB
audio dongles, and lists researched hardware with honest status labels
(the Intel Wi-Fi 6E AX210 is project-validated with this receiver via the
generic desktop PipeWire/WirePlumber UI; most other hardware remains
unverified).

Two kinds of Linux sources exist. **Native HCI adapters** are the
recommended path: BlueZ/PipeWire on the PC runs the LE Audio stack, which
is what this project validates (the Intel AX210). **Self-contained USB
audio transmitters** (dongles) pair directly with the receiver while both
sides are in pairing mode: Linux just sees a USB sound card and is not
involved in the Bluetooth pairing. Dongles are untested secondary
candidates with this receiver.

### Development / test path

As a development and test path (not a consumer feature), the repository
includes a **central test tool** (`scripts/bap_central.py`) that streams LC3
test tones from a Linux PC with a compatible LE Audio controller and BlueZ.
Its setup is documented in the repository; it is intended for verifying
receiver builds during development.

## Troubleshooting

| Symptom | Likely cause / check |
|---|---|
| Device not visible in scan | Receiver still booting; check power. If previously paired, NORMAL mode only shows to bonded devices; enter BONDING to pair a new device. |
| Pairing fails or device won't connect | The source device may have a stale bond from an earlier firmware version. Remove the receiver from the source's paired-device list, or hold the button for 8 seconds (RESET) to clear bonds on the receiver. |
| No sound | Check DAC wiring (see [Hardware wiring](hardware-wiring.md)) and that the DAC board is powered. On some PCM5102A clones the SCK pad must be bridged to ground. |
| Volume does nothing | Use the source device's volume controls (VCP). If the source has no LE Audio volume integration, playback runs at the receiver's default volume. |
| Source connects but no stream | Confirm the source actually supports BAP unicast **source** (not just sink). See [Supported source devices](#supported-source-devices). |

See also the full [Known limitations](known-limitations.md) list.

## Related documents

- [Hardware wiring](hardware-wiring.md)
- [Known limitations](known-limitations.md)
- [Technology: nRF54L15](technology/nrf54l15.md)
