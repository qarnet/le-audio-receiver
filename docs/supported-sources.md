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
  WirePlumber with LC3 support (`liblc3` or LC3 built into the BlueZ
  plugin), plus BlueZ experimental configuration for the ISO socket.
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

### 2. Self-contained USB audio transmitters (dongles)

These adapters present themselves to the host as a **USB Audio Class sound
device** and run the entire Bluetooth stack (including BAP and LC3
encoding) **internally, in the dongle's own firmware**. The host just plays
audio to a USB sound card. This is a different model from the native HCI
path:

- They **may work on Linux with no BlueZ/PipeWire involvement at all** —
  the OS sees a plain USB audio device.
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
- **Recent BlueZ and PipeWire** versions (plus WirePlumber). BlueZ
  experimental features — including the kernel ISO socket — currently have
  to be enabled in `/etc/bluetooth/main.conf`; expect this to become
  simpler over time.
- LC3 support: PipeWire's BlueZ plugin needs `liblc3` (or LC3 compiled in).

This project does not pin a specific Linux distribution. The exact
package/version situation differs by distro, and desktop audio UIs are
still maturing.

### Project's tested path vs. desktop PipeWire

This project validated the native-adapter path using its own **custom
source tool**, `scripts/bap_central.py` (a BlueZ BAP source that streams
LC3 test tones), not the desktop PipeWire UI. The AX210 validation below is
therefore direct evidence for the **controller + Linux stack combination
that was tested** — it does not automatically guarantee that every desktop
distro's PipeWire/WirePlumber UI will work out of the box. Desktop LE
Audio UX is still evolving.

## Hardware matrix

| Hardware | Host model | Linux status | Receiver status | Notes |
|---|---|---|---|---|
| **Intel Wi-Fi 6E AX210** | M.2 (NGFF) PCIe Wi-Fi card — not a plug-in USB stick; the Bluetooth function is exposed to the host over internal USB, so `lsusb` may show `Intel Corp. AX210 Bluetooth` | **Project-validated** | **Project-validated** | Validated as a BAP unicast source with this receiver on Linux; project validation is the primary evidence for compatibility with this receiver. Intel's [specifications](https://www.intel.com/content/www/us/en/products/sku/239216/intel-wifi-6e-ax210-gig-embedded/specifications.html) list the Bluetooth function over USB. A practical Linux LE Audio report (Raspberry Pi 5 with an AX210 module, BlueZ/PipeWire/WirePlumber) is at [AK-Experiments](https://ak-experiments.blogspot.com/2025/08/bluetooth-le-audio-on-raspberry-pi-with.html). Needs the current kernel/BlueZ/PipeWire stack — see [Linux software requirements](#linux-software-requirements). |
| **FlooGoo FMA120** (Flairmesh) | USB-A dongle (composite USB audio; no driver needed on the host) | **Vendor-supported** | **Unverified** | Vendor explicitly claims Linux: no-driver USB audio, LE Audio unicast (LC3) and Auracast, with a Linux configuration app (FlooCast `.deb`/`.rpm`, also on GitHub). Strongest actual USB-dongle candidate found in research, but **not project-tested** with this receiver. See [Flairmesh FMA120](https://www.flairmesh.com/Dongle/FMA120.html). |
| **Creative BT-W6** | USB-C wireless audio transmitter | **Unverified** | **Unverified** | Vendor claims LC3 LE Audio unicast to a compatible receiver, plug-and-play on PC/Mac/consoles. Linux is **not** in the officially listed platforms, and the Creative configuration app downloads appear Windows/macOS-only. It may enumerate as generic USB audio on Linux, but Linux configuration and interoperability with this receiver are unverified. Not recommended as a confirmed Linux solution. See [Creative BT-W6](https://us.creative.com/p/speakers/creative-bt-w6) and [Creative support](https://support.creative.com/Products/ProductDetails.aspx?prodID=24280&prodName=Creative+BT-W6). |
| **Avantalk / Avantree C82 LEA** | USB-C adapter (USB-A adapter included) | **Unverified** | **Unverified** | Vendor claims no-driver USB audio and LE Audio/LC3 (plus classic Bluetooth), listing Windows/Mac/Android/consoles — but **not Linux**. Linux support and interoperability with this receiver are unverified. See [Avantree C82 LEA](https://avantree.com/products/c82-usb-le-audio-adapter). |
| **Nordic nRF5340 Audio DK** | Development kit (USB) | **Unverified** | **Unverified** | Official Nordic LE Audio development platform; Nordic documents that it is configurable as a USB dongle to send/receive PC audio. It is a large development kit, not a consumer adapter, Nordic lists "PC" rather than Linux specifically, and Linux USB Audio Class enumeration and interoperability with this receiver are **unverified** by this project. See [nRF5340 Audio DK](https://www.nordicsemi.com/Products/Development-hardware/nRF5340-Audio-DK/Download). |
| **Recent adapters: Intel BE200 and recent NXP / MediaTek / Qualcomm models** | Varies (M.2, USB) | **Unverified** | **Unverified** | Research candidates only, not confirmed. Controller/firmware LE Audio support varies by vendor and model: per [Collabora's overview](https://www.collabora.com/news-and-blog/blog/2025/11/24/implementing-bluetooth-le-audio-and-auracast-on-linux-systems/), recent Intel controllers (BE200) and several other vendors implement LE Audio in recent models. Check the controller's actual capabilities (`cis-central`) before relying on it. |

Notes:

- Vendor claims in this matrix are from vendor product pages as of
  August 2026 and are not independently verified by this project.
- For the dongle-based products, "receiver status" can only be settled by
  testing the dongle against this receiver; the Linux software stack is
  not the deciding factor for them.

## Related documents

- [User guide](user-guide.md) — pairing, boot, and general source-device
  notes
- [Known limitations](known-limitations.md) — 48 kHz only, and other
  current gaps
- [Hardware wiring](hardware-wiring.md)
