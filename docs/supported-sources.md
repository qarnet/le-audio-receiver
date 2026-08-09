# Supported LE Audio sources on Linux

This page is the overview and source matrix for making a Linux computer act as
an LE Audio **source** for this receiver. The detail lives in two focused
guides:

- [Linux LE Audio host setup](linux-le-audio-host-setup.md) — host OS setup,
  configuration, and verification for the BlueZ + PipeWire path.
- [Bluetooth adapter support and evaluation](bluetooth-adapter-evaluation.md) —
  which adapter is supported, and how adapters get accepted or rejected.

This page complements the general notes in the [User
guide](user-guide.md#supported-source-devices) and the
[README](../README.md#supported-source-devices).

Status labels used in the matrix below:

| Label | Meaning |
|---|---|
| **Project-validated** | This project tested the hardware end-to-end as an LE Audio source streaming to this receiver, on Linux. |
| **Project-tested (dev tool)** | This project exercised the hardware end-to-end with its development/test tool (`scripts/bap_central.py`) streaming to this receiver. Development-tool validation alone does not establish the production BlueZ/PipeWire/WirePlumber path, so the candidate remains not supported until the listed acceptance gates pass; a candidate may additionally have production-path evidence (for example the BT540 headless run) without changing that boundary. |
| **Vendor-supported** | The vendor officially documents the capability (LE Audio unicast, Linux support, or both). Vendor claims are not independently verified here. |
| **Unverified** | No project test and no vendor confirmation for the specific claim (Linux support and/or interoperability with this receiver). |

The receiver itself accepts **LC3 at 48 kHz** only (see
[Known limitations](known-limitations.md)), so any source — native or
dongle-based — must be able to negotiate BAP unicast with LC3 at 48 kHz.

## Two kinds of Linux source hardware

There are two fundamentally different ways a Linux machine can become an LE
Audio source. They have different requirements and different failure modes.

### 1. Native Linux HCI adapters (BlueZ/PipeWire path)

A regular Bluetooth adapter (M.2 card, USB stick, or onboard radio) exposes
an HCI interface; **Linux owns the Bluetooth stack**. The LE Audio work
happens in host software:

- **BlueZ** (the Linux Bluetooth host stack) implements BAP stream control
  and the ISO transport.
- **PipeWire** (with the WirePlumber session manager) implements the audio
  side, including the **BAP source role** (`bap_source`), encoding LC3.

What this path needs:

- A **controller with ISO support** (the LE Audio transport is
  isochronous). The radio must support connected isochronous streams
  (`cis-central` for a source).
- A **current Linux software stack**: Linux kernel 6.4 or later, BlueZ 5.85
  or later, and a current PipeWire/WirePlumber whose BlueZ SPA plugin
  provides LC3 encoding (via `liblc3`) — WirePlumber **0.5.15 or newer** for
  a project-accepted clean host lifecycle — plus BlueZ experimental
  configuration for the ISO socket. Full setup and verification are on the
  [host setup page](linux-le-audio-host-setup.md).
- **Bluetooth version numbers do not prove LE Audio support.** "Bluetooth
  5.3" or "5.4" on a spec sheet describes the radio generation, not the
  profiles. LE Audio is a feature set; the controller must actually
  implement ISO and BAP. A well-known example: the Raspberry Pi 5's
  onboard radio is Bluetooth 5.0, and LE Audio is not available on it.

Checking a native adapter's controller capabilities is useful runtime
evidence, but it is not a universal purchasing guarantee:

```console
$ btmgmt --index hci0 info
# look for "cis-central" in the supported settings (unicast ISO support)
```

`cis-central` means the controller supports the isochronous transport a
BAP source needs. Treat it as runtime evidence that the adapter is capable
on the stack it was checked with — capability reporting varies by
controller, and LE Audio still depends on the whole
firmware/driver/BlueZ/PipeWire combination. Note that **Published Audio
Capabilities (PACS) is a GATT service observed on remote devices**, not a
property of the local adapter: seeing it while scanning a peer says
something about that peer, so it is not a reliable pre-buy check of the
adapter itself. The [adapter evaluation
page](bluetooth-adapter-evaluation.md) defines what an adapter must prove
to be accepted.

### 2. Self-contained USB audio transmitters (dongles)

These adapters present themselves to the host as a **USB Audio Class sound
device** and run the entire Bluetooth stack (including BAP and LC3
encoding) **internally, in the dongle's own firmware**. The host just plays
audio to a USB sound card. This is a different model from the native HCI
path:

- They **may work on Linux with no BlueZ/PipeWire involvement at all** —
  the OS sees a plain USB audio device, and the dongle handles the whole
  Bluetooth side itself.
- Pairing is **generic standards-based**: the dongle pairs directly with
  the receiver while both sides are in their pairing mode, and stores the
  bond in its own firmware. The transmitter and receiver do **not** need
  to be shipped or sold as a matched pair — but actual interoperability
  still has to be tested.
- Interoperability with a specific receiver depends on the **dongle
  firmware**, not on the Linux stack, so it cannot be verified from
  documentation alone.

## Software requirements summary

The full host requirements, configuration, and verification procedure are on
the [Linux LE Audio host setup](linux-le-audio-host-setup.md) page. In brief,
the native path needs Linux kernel 6.4+, BlueZ 5.85+ (the first stable release
with the PAC configuration callback fix), and a current PipeWire built with
the BlueZ SPA and LC3 (`liblc3`), with the `bap_source` role enabled in
WirePlumber **0.5.15 or newer** (the project-accepted clean host lifecycle
floor; see below).

This project validated the native path end-to-end with the **Intel AX210** on
the exact baseline recorded on the [host setup
page](linux-le-audio-host-setup.md#project-tested-baseline) (including the
disclosed BlueZ QoS-property spelling patch, fixed upstream 2026-05-07).  That
historical AX210 validation used WirePlumber **0.5.14** and demonstrated
functional BAP streaming; the current project acceptance floor is WirePlumber
**0.5.15 or newer** for a clean host lifecycle (the 0.5.14→0.5.15 rationale
and the project-tested baseline are on the [host setup
page](linux-le-audio-host-setup.md)).  The AX210 adapter validation stands;
the floor governs the host lifecycle under which new acceptance runs are
accepted.  The repository's own `scripts/bap_central.py` (a BlueZ BAP source
that streams LC3 test tones) remains a separate, deterministic
development/test path for verifying receiver builds — it is not the basis for
the AX210 project-validation claim above.

## Hardware matrix

### Native HCI adapters (primary candidates)

| Hardware | Host model | Linux status | Receiver status | Notes |
|---|---|---|---|---|
| **Intel Wi-Fi 6E AX210** | M.2 (NGFF) combo card — Wi-Fi over PCIe, Bluetooth function over internal USB; on a desktop the carrier needs a USB header connection, and `lsusb` commonly shows `8087:0032 Intel Corp. AX210 Bluetooth` | **Project-validated** | **Project-validated** | Validated as a BAP unicast source with this receiver on Linux via a **generic desktop PipeWire/WirePlumber UI** (the normal desktop flow) and exercised with the repository's `scripts/bap_central.py` development/test tool. Intel's [specifications](https://www.intel.com/content/www/us/en/products/sku/239216/intel-wifi-6e-ax210-gig-embedded/specifications.html) list the Bluetooth function over USB. A practical Linux LE Audio report (Raspberry Pi 5 with an AX210 module, BlueZ/PipeWire/WirePlumber) is at [AK-Experiments](https://ak-experiments.blogspot.com/2025/08/bluetooth-le-audio-on-raspberry-pi-with.html). Needs the current kernel/BlueZ/PipeWire stack — see [host setup](linux-le-audio-host-setup.md) and [adapter evaluation](bluetooth-adapter-evaluation.md). |
| **ASUS USB-BT540** | USB adapter | **Vendor-supported** (Linux) | **Project-tested (dev tool)** | Measured 2026-08-09: project development-tool tests passed via `scripts/bap_central.py --adapter hci1` — fresh discovery, pairing, encrypted GATT, PACS/ASCS resolution, 48 kHz LC3 (10 ms, PHY 2M), two-CIS Mode A, bonded reconnect, and one-CIS Mode B; the receiver reported zero decode errors, zero I2S underruns, and zero stream resets. The headless BlueZ + PipeWire + WirePlumber production path also passed one-CIS Mode B (48 kHz, 7.5 ms, 234-byte SDU, PHY 2M) after receiver fixes (empty-SDU concealment, deeper I2S startup reservoir) and the WirePlumber 0.5.15 upgrade, including a clean host shutdown. Not Supported / project-validated: the desktop (KDE/Bluedevil) UI route is untested, one-CIS mono is unrun, cold unplug/replug repeat is unrun, audible-output confirmation was not recorded, a kernel codec-capability init warning (`-22`) remains, and receiver PLC from the development-tool runs is unexplained. Status is **candidate**, not recommendation. ASUS officially lists Linux, Bluetooth 5.4, LC3/LE Audio, and LE 2M. See [ASUS USB-BT540](https://www.asus.com/networking-iot-servers/adapters/all-series/usb-bt540/), [tech specs](https://www.asus.com/networking-iot-servers/wireless-adapters/all-series/usb-bt540/techspec/), and [adapter evaluation](bluetooth-adapter-evaluation.md). |
| **ASUS USB-BT600** | USB adapter | **Vendor-supported** (Linux) | **Unverified** | **Candidate / under evaluation** — not supported. ASUS officially lists Linux, Bluetooth 6.0, and LC3/LE Audio. Chipset, VID:PID, `cis-central`, ISO MTU/count, availability/maturity, and dynamic receiver tests remain unrecorded. See [ASUS USB-BT600](https://www.asus.com/networking-iot-servers/wireless-adapters/all-series/usb-bt600/). |
| **Nordic nRF5340 DK (HCI UART)** | Development kit — Bluetooth controller on cpunet, H4 UART on cpuapp | **Unverified** | **Unverified** | **Candidate / under evaluation** as a native HCI development adapter. NCS v3.3.0 supports the controller on cpunet with H4 UART on cpuapp; this repository's `dongle/` is a working source-controller implementation. The hardware source path is not project-validated (existing public evidence does not establish full dynamic acceptance). HCI UART is the supported route — NCS v3.3.0's USB HCI class cannot carry LE ISO. See [adapter evaluation](bluetooth-adapter-evaluation.md). |
| **Nordic nRF54L15 DK (HCI UART)** | Development kit — `samples/bluetooth/hci_uart` on `nrf54l15dk/nrf54l15/cpuapp` | **Unverified** | **Unverified** | **Candidate / under evaluation** as a native HCI development adapter. NCS v3.3.0's `samples/bluetooth/hci_uart` supports `nrf54l15dk/nrf54l15/cpuapp`; the H4 transport handles packet type `0x05` (ISO). Dynamic Linux and receiver validation remains required. See [adapter evaluation](bluetooth-adapter-evaluation.md). |

The **Intel AX210 is the only project-validated native adapter** — see the
[adapter evaluation page](bluetooth-adapter-evaluation.md) for the
unsupported-device notes and the evaluation contract. This project found **no
plug-in USB HCI stick that is project-validated**: the ASUS USB-BT540 has
passed static capability checks, development-tool dynamic tests, and a
headless production-path one-CIS Mode B run, but its desktop (KDE/Bluedevil)
UI route and remaining acceptance items are still open. Generic
"Bluetooth 5.3" or "Bluetooth 5.4" USB dongles do not prove LE Audio support —
see the version-numbers caveat above — so do not buy one on that basis alone.
The ASUS USB-BT540 (candidate / project-tested with the development tool) and
USB-BT600 are listed as candidates under evaluation, and the Nordic
nRF5340/nRF54L15 development kits as HCI UART candidates, on the
[adapter evaluation page](bluetooth-adapter-evaluation.md).

### Self-contained USB transmitters (secondary candidates)

| Hardware | Host model | Linux status | Receiver status | Notes |
|---|---|---|---|---|
| **FlooGoo FMA120** (Flairmesh) | USB-A dongle (composite USB audio; no driver needed on the host) | **Vendor-supported** | **Unverified** | Vendor explicitly claims Linux: no-driver USB audio, LE Audio unicast (LC3) and Auracast, with a Linux configuration app (FlooCast `.deb`/`.rpm`, also on GitHub). Pairing is set up through the FlooCast app: enable "Prefer LE Audio", scan and add the nearest device (the receiver), and the bond/settings are stored in the dongle — the app is not needed after initial pairing. Strongest actual USB-dongle candidate found in research, but **not project-tested** with this receiver. See [Flairmesh FMA120](https://www.flairmesh.com/Dongle/FMA120.html) and the [FMA120 user guide](https://www.flairmesh.com/support/FMA120UG.pdf). |
| **Creative BT-W6** | USB-C wireless audio transmitter | **Unverified** | **Unverified** | Vendor claims LC3 LE Audio unicast to a compatible receiver, plug-and-play on PC/Mac/consoles. Linux is **not** in the officially listed platforms, and the Creative configuration app downloads appear Windows/macOS-only — but pairing and mode selection are driven from the hardware: a multifunction button starts pairing on first use, and the transmitter's controls switch LE Audio/unicast mode. It may enumerate as generic USB audio on Linux, but interoperability with this receiver is untested. Not recommended as a confirmed Linux solution. See [Creative BT-W6](https://us.creative.com/p/speakers/creative-bt-w6) and [Creative support](https://support.creative.com/Products/ProductDetails.aspx?prodID=24280&prodName=Creative+BT-W6). |
| **Avantalk / Avantree C82 LEA** | USB-C adapter (USB-A adapter included) | **Unverified** | **Unverified** | Vendor claims no-driver USB audio and LE Audio/LC3 (plus classic Bluetooth), listing Windows/Mac/Android/consoles — but **not Linux**. A physical button switches it into LE Audio mode and enters pairing; no custom Linux Bluetooth stack is required. Linux support and interoperability with this receiver are unverified. See [Avantree C82 LEA](https://avantree.com/products/c82-usb-le-audio-adapter) and the [C82 LEA user guide](https://iug.avantree.com/C82-LEA/EN/C82-LEA-IUG.pdf). |
| **Nordic nRF5340 Audio DK** | Development kit (USB) | **Unverified** | **Unverified** | Official Nordic LE Audio development platform; Nordic documents that it is configurable as a USB dongle to send/receive PC audio. It is a large development kit, not a consumer adapter, Nordic lists "PC" rather than Linux specifically, and Linux USB Audio Class enumeration and interoperability with this receiver are **unverified** by this project. See [nRF5340 Audio DK](https://www.nordicsemi.com/Products/Development-hardware/nRF5340-Audio-DK/Download). |
| **SUNITEC BT400T** | USB transmitter (vendor: USB Audio Class-style plug-and-play) | **Unverified** | **Unverified** | Vendor marketplace material says USB Audio Class-style plug-and-play, LE Audio/Auracast, and LC3, but does not clearly prove BAP unicast pairing with arbitrary receivers or 48 kHz negotiation. This receiver has no broadcast-sink support, so Auracast alone is incompatible. See [SUNITEC BT400T listing](https://www.amazon.com/Auracast-Bluetooth-Transmitter-Adapter-PS5/dp/B0H4QWJQVL). |

### Project priority: native HCI adapters

Native HCI adapters are this project's **primary** candidates. The
BlueZ/PipeWire path provides an observable, controllable, standard BAP
source: stream state, QoS, codec configuration and diagnostics are visible
(and scriptable) on the host. Self-contained USB transmitters hide
Bluetooth state, QoS and profile behavior inside proprietary firmware,
offer little or no host-side diagnostics, and remain untested with this
receiver. They are **secondary interoperability candidates**, not
recommended or confirmed sources. The **Intel AX210 is the only
project-validated source** listed here.

Notes:

- Vendor claims in this matrix are from vendor product pages as of
  August 2026 and are not independently verified by this project.
- For the dongle-based products, "receiver status" can only be settled by
  testing the dongle against this receiver; the Linux software stack is
  not the deciding factor for them.
- Self-contained transmitters do **not** require a matched
  receiver/transmitter pair: generic standards-based pairing is intended.
  Put both the dongle and the receiver into their pairing modes and they
  bond directly — but actual interoperability with this receiver must
  still be tested.

## Related documents

- [Linux LE Audio host setup](linux-le-audio-host-setup.md) — host OS setup,
  configuration, and verification for the BlueZ + PipeWire path
- [Bluetooth adapter support and evaluation](bluetooth-adapter-evaluation.md) —
  supported adapters and the active device-evaluation contract
- [User guide](user-guide.md) — pairing, boot, and general source-device
  notes
- [Known limitations](known-limitations.md) — 48 kHz only, and other
  current gaps
- [Hardware wiring](hardware-wiring.md)
