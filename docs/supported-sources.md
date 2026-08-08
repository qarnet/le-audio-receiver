# Supported LE Audio sources on Linux

This page explains what it takes for a Linux computer to act as an LE Audio
**source** for this receiver, and lists the hardware options researched so
far, with honest status labels. It complements the general notes in the
[User guide](user-guide.md#supported-source-devices) and the
[README](../README.md#supported-source-devices).

Status labels used in the matrix below:

| Label | Meaning |
|---|---|
| **Project-validated** | This project tested the hardware end-to-end as an LE Audio source streaming to this receiver, on Linux. |
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
- A **current Linux software stack**: Linux kernel 6.4 or later (Collabora
  recommends newer versions), a recent BlueZ, and a recent PipeWire/
  WirePlumber whose BlueZ SPA plugin provides LC3 encoding (via
  `liblc3`), plus BlueZ experimental configuration for the ISO socket.
  See [Linux software requirements](#linux-software-requirements).
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
adapter itself.

Minimal distro-neutral runtime checks for the native path:

- The controller reports `cis-central` (`btmgmt --index hci0 info`).
- BlueZ's experimental BAP/ISO path is enabled (e.g. `Experimental=true`
  and `KernelExperimental=true` in `/etc/bluetooth/main.conf`).
- PipeWire's BlueZ SPA plugin provides LC3 and the `bap_source` role.
- WirePlumber is running.

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

## Linux software requirements

For the native-adapter path, the current Linux LE Audio architecture is:

- **BlueZ** implements the Bluetooth side (BAP, ISO, VCP coordination).
- **PipeWire** implements the audio side. Since version 0.3.59 PipeWire has
  supported the Basic Audio Profile (BAP) over connected isochronous
  streams with LC3 ([BlueZ: LE Audio support in
  PipeWire](https://www.bluez.org/le-audio-support-in-pipewire/)); the
  PipeWire roles documentation lists `bap_source` (LE Audio Basic Audio
  Profile Source) among the enabled
  [BlueZ roles](https://docs.pipewire.org/page_man_pipewire-props_7.html).

Version guidance (from [Collabora's Linux LE Audio
overview](https://www.collabora.com/news-and-blog/blog/2025/11/24/implementing-bluetooth-le-audio-and-auracast-on-linux-systems/)):

- **Linux kernel 6.4 or later**, with newer versions strongly recommended
  due to ongoing ISO fixes.
- **BlueZ 5.85 or later.** BlueZ 5.85 is the first stable release that
  includes the PAC configuration callback fix (upstream
  [commit 6b0a087](https://github.com/bluez/bluez/commit/6b0a08776ae44a9102d7c6875a77e83dc6a11a37)).
  Earlier releases are not recommended, and this project does not
  recommend building or patching older BlueZ releases yourself.
- **Recent PipeWire and WirePlumber** versions. BlueZ experimental
  features — including the kernel ISO socket — currently have to be
  enabled in `/etc/bluetooth/main.conf`; expect this to become simpler
  over time.
- LC3 support: PipeWire's BlueZ SPA plugin (part of PipeWire, not BlueZ
  itself) handles the LC3 encoding for the BAP source role and needs
  `liblc3` available to it.

This project does not pin a specific Linux distribution. The exact
package/version situation differs by distro, and desktop audio UIs are
still maturing.

### Project's tested path vs. desktop PipeWire

This project validated the native-adapter path with the **Intel AX210**
using a generic desktop **PipeWire/WirePlumber UI** streaming to this
receiver — the same user-facing flow a normal Linux desktop uses. The
validated host ran:

- Linux kernel **7.1.5**
- BlueZ **5.86**
- PipeWire **1.6.6**
- WirePlumber **0.5.14**
- BlueZ configured with `Experimental=true` and `KernelExperimental=true`
  in `/etc/bluetooth/main.conf` (the daemon runs without explicit `-E`/`-K`
  flags)
- PipeWire and WirePlumber enabled, PulseAudio disabled, rtkit enabled

The AX210 validation below is therefore direct evidence that a normal
desktop PipeWire/WirePlumber path works as an LE Audio source with this
receiver, not only a custom script.

The validated host also carries a small BlueZ **5.86** patch that fixes a
QoS-property spelling in `profiles/audio/bap.c`: it changes `MimimumDelay`
to `MinimumDelay` and `PreferredMimimumDelay` to `PreferredMinimumDelay`.
This is separate from the BlueZ `pac_config_cb()` pointer/value fix that
landed in 5.85 (see
[Linux software requirements](#linux-software-requirements)). The
misspelled keys are still present in upstream BlueZ master as of
2026-08-09:
<https://kernel.googlesource.com/pub/scm/bluetooth/bluez/+/master/profiles/audio/bap.c>.
We report the patch as a disclosure about the host we tested, not as proof
that every distribution needs it.

The repository's own **`scripts/bap_central.py`** (a BlueZ BAP source that
streams LC3 test tones) remains a separate, deterministic
development/test path for verifying receiver builds without hunting for a
consumer source device — it is not the basis for the AX210
project-validation claim above. Desktop LE Audio UX is still evolving, so
keep the software stack current (see
[Linux software requirements](#linux-software-requirements)).

## Hardware matrix

### Native HCI adapters (primary candidates)

| Hardware | Host model | Linux status | Receiver status | Notes |
|---|---|---|---|---|
| **Intel Wi-Fi 6E AX210** | M.2 (NGFF) PCIe Wi-Fi card — not a plug-in USB stick; the Bluetooth function is exposed to the host over internal USB, so `lsusb` may show `Intel Corp. AX210 Bluetooth` | **Project-validated** | **Project-validated** | Validated as a BAP unicast source with this receiver on Linux via a **generic desktop PipeWire/WirePlumber UI** (the normal desktop flow), and also exercised with the repository's `scripts/bap_central.py` development/test tool. Project validation is the primary evidence for compatibility with this receiver. Intel's [specifications](https://www.intel.com/content/www/us/en/products/sku/239216/intel-wifi-6e-ax210-gig-embedded/specifications.html) list the Bluetooth function over USB. A practical Linux LE Audio report (Raspberry Pi 5 with an AX210 module, BlueZ/PipeWire/WirePlumber) is at [AK-Experiments](https://ak-experiments.blogspot.com/2025/08/bluetooth-le-audio-on-raspberry-pi-with.html). Needs a current kernel/BlueZ/PipeWire stack (BlueZ 5.85 or later) — see [Linux software requirements](#linux-software-requirements). |
| **Recent adapters: Intel BE200 and recent NXP / MediaTek / Qualcomm models** | Varies (M.2, USB) | **Unverified** | **Unverified** | Research candidates only, not confirmed. Controller/firmware LE Audio support varies by vendor and model: per [Collabora's overview](https://www.collabora.com/news-and-blog/blog/2025/11/24/implementing-bluetooth-le-audio-and-auracast-on-linux-systems/), recent Intel controllers (BE200) and several other vendors implement LE Audio in recent models. Check the controller's actual capabilities (`cis-central`) before relying on it. |

This project found **no plug-in USB HCI stick with verified Linux CIS
support**. Generic "Bluetooth 5.3" or "Bluetooth 5.4" USB dongles do not
prove LE Audio support — see the version-numbers caveat above — so do not
buy one on that basis alone. Many cheap USB Bluetooth dongles use the
Realtek **RTL8761BU** controller; community runtime reports show no
`cis-central` support on it, so it is useful only as a counterexample and
should not be treated as LE Audio-capable.

### Self-contained USB transmitters (secondary candidates)

| Hardware | Host model | Linux status | Receiver status | Notes |
|---|---|---|---|---|
| **FlooGoo FMA120** (Flairmesh) | USB-A dongle (composite USB audio; no driver needed on the host) | **Vendor-supported** | **Unverified** | Vendor explicitly claims Linux: no-driver USB audio, LE Audio unicast (LC3) and Auracast, with a Linux configuration app (FlooCast `.deb`/`.rpm`, also on GitHub). Pairing is set up through the FlooCast app: enable "Prefer LE Audio", scan and add the nearest device (the receiver), and the bond/settings are stored in the dongle — the app is not needed after initial pairing. Strongest actual USB-dongle candidate found in research, but **not project-tested** with this receiver. See [Flairmesh FMA120](https://www.flairmesh.com/Dongle/FMA120.html) and the [FMA120 user guide](https://www.flairmesh.com/support/FMA120UG.pdf). |
| **Creative BT-W6** | USB-C wireless audio transmitter | **Unverified** | **Unverified** | Vendor claims LC3 LE Audio unicast to a compatible receiver, plug-and-play on PC/Mac/consoles. Linux is **not** in the officially listed platforms, and the Creative configuration app downloads appear Windows/macOS-only — but pairing and mode selection are driven from the hardware: a multifunction button starts pairing on first use, and the transmitter's controls switch LE Audio/unicast mode. It may enumerate as generic USB audio on Linux, but interoperability with this receiver is untested. Not recommended as a confirmed Linux solution. See [Creative BT-W6](https://us.creative.com/p/speakers/creative-bt-w6) and [Creative support](https://support.creative.com/Products/ProductDetails.aspx?prodID=24280&prodName=Creative+BT-W6). |
| **Avantalk / Avantree C82 LEA** | USB-C adapter (USB-A adapter included) | **Unverified** | **Unverified** | Vendor claims no-driver USB audio and LE Audio/LC3 (plus classic Bluetooth), listing Windows/Mac/Android/consoles — but **not Linux**. A physical button switches it into LE Audio mode and enters pairing; no custom Linux Bluetooth stack is required. Linux support and interoperability with this receiver are unverified. See [Avantree C82 LEA](https://avantree.com/products/c82-usb-le-audio-adapter) and the [C82 LEA user guide](https://iug.avantree.com/C82-LEA/EN/C82-LEA-IUG.pdf). |
| **Nordic nRF5340 Audio DK** | Development kit (USB) | **Unverified** | **Unverified** | Official Nordic LE Audio development platform; Nordic documents that it is configurable as a USB dongle to send/receive PC audio. It is a large development kit, not a consumer adapter, Nordic lists "PC" rather than Linux specifically, and Linux USB Audio Class enumeration and interoperability with this receiver are **unverified** by this project. See [nRF5340 Audio DK](https://www.nordicsemi.com/Products/Development-hardware/nRF5340-Audio-DK/Download). |

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

- [User guide](user-guide.md) — pairing, boot, and general source-device
  notes
- [Known limitations](known-limitations.md) — 48 kHz only, and other
  current gaps
- [Hardware wiring](hardware-wiring.md)
