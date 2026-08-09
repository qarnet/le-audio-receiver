# Bluetooth adapter support and evaluation

This page defines this project's Bluetooth **adapter** support: which adapter
is currently accepted as a native Linux HCI source for this receiver, and the
reusable evaluation contract used to accept (or reject) future devices. It is
the companion to the [Linux LE Audio host setup](linux-le-audio-host-setup.md)
page (host OS setup and verification) and the [Supported LE Audio sources on
Linux](supported-sources.md) overview/source matrix.

Native HCI adapters are this project's primary source path: Linux owns the
Bluetooth stack, and BlueZ + PipeWire implement BAP, ISO, and LC3 on the host.
**Self-contained USB Audio Class transmitters** (which run the entire stack in
dongle firmware) are outside this native-HCI support contract — see
[supported-sources.md](supported-sources.md) for that separate, unverified
research matrix.

## Tested and supported adapters

### Intel Wi-Fi 6E AX210 — Supported (project-validated)

The **only** adapter this project has tested and supported as a native Linux
HCI source is the **Intel Wi-Fi 6E AX210**.

- **Form factor:** an M.2 combo card. Wi-Fi uses PCIe, while the Bluetooth
  function uses an **internal USB** connection. On a desktop, the M.2 carrier
  must wire a USB header to the motherboard; `lsusb` commonly shows
  `8087:0032 Intel Corp. AX210 Bluetooth`.
- **Validation:** validated end-to-end against this receiver over the generic
  desktop PipeWire/WirePlumber path (the normal desktop flow) and exercised
  with the repository's `scripts/bap_central.py` development/test tool. The
  exact tested host baseline is recorded on the [Linux LE Audio host
  setup](linux-le-audio-host-setup.md) page.
- **Status:** **Supported / project-validated.**

### Intel BE200 — Not supported

The Intel BE200 is **not supported** and is **not an active candidate**. This
project has not validated it, and it has public reports of reaching LE Audio
setup but crashing during streaming
([BlueZ issue 1149](https://github.com/bluez/bluez/issues/1149)) — so no
support or candidate claim is made.

### Plug-in USB HCI adapters — none validated yet

No plug-in USB HCI adapter is project-validated yet. Generic "Bluetooth 5.3"
or "Bluetooth 5.4" USB dongles do not prove ISO support — a marketing version
is not a capability claim (see [Mandatory controller
contract](#mandatory-controller-contract)). USB sticks are eligible for
evaluation on any bus (see [Bus independence](#bus-independence)); they just
have not passed acceptance yet. The current evaluation results follow: two
ASUS sticks and the Nordic development kits are candidates under evaluation,
the UGREEN model 75073 is incompatible with the native path, and the UGREEN
CM591 remains unverified (not a candidate).

### ASUS USB-BT540 — Candidate / project-tested with development tool

ASUS officially lists Linux, Bluetooth 5.4, LC3/LE Audio, and LE 2M
([product page](https://www.asus.com/networking-iot-servers/adapters/all-series/usb-bt540/),
[tech specs](https://www.asus.com/networking-iot-servers/wireless-adapters/all-series/usb-bt540/techspec/)).
Measured on 2026-08-09 (row in the [evaluation record](#evaluation-record)):

- **Identity:** USB ID `0b05:1bef`, USB manufacturer string `Realtek`,
  full-speed USB; Linux device `hci1` via `btusb`; controller address
  `A0:AD:9F:7B:C7:95`.
- **HCI/LMP:** Bluetooth 5.4; HCI revision 14; LMP subversion `0x8761`.
- **Test host (historical WirePlumber 0.5.14 environment, original
  measurement):** Linux 7.1.1, BlueZ 5.86, PipeWire 1.6.6, WirePlumber 0.5.14.
- **Static checks passed:** LE 2M; CIS Central and Peripheral; ISO
  Broadcaster and Sync Receiver; ISO MTU 251 and ISO packet count 20;
  required CIG/CIS and ISO data-path commands present; transparent codec
  reports LE CIS and LE BIS transports.
- **Dynamic checks passed with the development tool** (`scripts/bap_central.py
  --adapter hci1`): fresh discovery, pairing, encrypted GATT, and PACS/ASCS
  resolution; 48 kHz LC3, 10 ms, PHY 2M, SDU 120 per mono ASE; two-CIS Mode A
  for 30 seconds (3000 source frames at 100 fps); bonded two-CIS reconnect for
  15 seconds (1500 source frames at 100 fps); one-CIS Mode B for 15 seconds
  (1500 source frames at 100 fps, SDU 240). The receiver reported zero decode
  errors, zero I2S underruns, and zero stream resets; `btmon` observed the CIG
  parameters, Create CIS, both Setup ISO Data Path commands, continuous ISO
  TX, and completed-packet credits.
- **Headless production-stack run (2026-08-09, WirePlumber 0.5.14):** the
  normal BlueZ + PipeWire + WirePlumber production path (the same components
  KDE drives) was exercised headless and without `scripts/bap_central.py`:
  `bluetoothctl`/BlueZ -> WirePlumber BlueZ monitor -> PipeWire Bluetooth sink
  -> PipeWire LC3 encoder -> Linux ISO socket -> BT540 -> receiver. The BlueZ
  sink appeared and one-CIS stereo Mode B LC3 streamed (48 kHz, 7.5 ms frame,
  117 octets per channel, 234-byte SDU, ISO interval 7500 us, PHY 2M, RTN 13,
  transport latency 75 ms, presentation delay 40000 us) on the original test
  host (Linux 7.1.1, BlueZ 5.86, PipeWire 1.6.6, WirePlumber 0.5.14). Clean
  acceptance failed due 16 startup zero-length malformed SDUs, a stop-boundary
  I2S underrun with a diagnostic-accounting mismatch, and unresolved
  WirePlumber startup diagnostics.
- **Post-fix production-path rerun (2026-08-09, WirePlumber 0.5.15):** after
  receiver fixes (empty-SDU concealment `9dc0859`, deeper 11-block I2S startup
  reservoir `f2f9336`) and the WirePlumber 0.5.15 upgrade, the same headless
  one-CIS Mode B production path passed on `thomas-workstation` (Linux 7.1.5,
  BlueZ 5.86, PipeWire 1.6.6, WirePlumber 0.5.15).  The 16 startup
  zero-length malformed SDUs of the original run are gone: the rerun saw 15
  empty SDUs that were separately concealed (zero malformed-SDU errors).  The
  eleven queued 7.5 ms I2S blocks provide an 82.5 ms total startup queue
  (about 77 ms transition coverage after playback begins), and continued
  invalid/omitted callbacks generated PLC that kept the queue supplied through
  the observed ~1.013 s final host-TX to ASCS Disable interval — the reservoir
  alone does not span that whole second — with zero receiver
  warnings/errors/resets (`SDUs=1712 decoded=3678 plc=284
  decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=15`; `decoded` is a
  legacy field label carrying total rendered frames including PLC, so
  decoded = good decodes + plc, not 3678 good decodes plus 284 PLC), and
  WirePlumber
  shut down with `Result=success`, `ExecMainCode=0`, `ExecMainStatus=0`, zero
  `destroy_proxy` / `leaked proxy`. The original clean-failure items no longer
  block this headless production-path subset.
- KDE/Bluedevil on `thomas-main`, one-CIS mono, cold replug, and audible
  confirmation remain open. Full evidence: [BT540 headless production-stack
  results](development/bt540-headless-pipewire-results.md).

Verdict: **Candidate / project-tested with development tool** — not
**Supported / project-validated**. Remaining gates before recommendation:

1. KDE/Bluedevil UI route on `thomas-main` remains untested — the headless
   CLI run above used the same production audio components and data path, but
   the Plasma UI path is still open.
2. One-CIS mono scenario remains unrun; one-CIS evidence is stereo Mode B.
3. Physical cold unplug/replug repeat remains unrun.
4. Audible-output confirmation was not recorded.
5. Kernel initialization log: `Bluetooth: hci1: Failed to read codec
   capabilities (-22)`.
6. Receiver PLC remains unexplained: Mode A 30 s — 78 PLC frames; Mode A
   reconnect 15 s — 70; Mode B 15 s — 72; repeat Mode B 10 s — 76.

Issue analysis:

- **Codec warning:** `btmon` shows Read Local Supported Codecs V2 advertises
  the transparent codec for ACL, SCO/eSCO, LE CIS, and LE BIS. Linux v7.1
  `net/bluetooth/hci_codec.c::hci_read_codec_capabilities()` correctly queries
  every advertised transport. The controller succeeds through the LE CIS
  transport and returns `Invalid HCI Command Parameters (0x12)` for the LE
  BIS transport, which the kernel maps to `-EINVAL` / `-22`. This is a
  controller capability-report inconsistency. It did not prevent unicast CIS
  streaming, but it remains a real initialization warning and blocks
  unconditional recommendation.
- **PLC:** the repeat Mode B serial capture contained no `ISO seq gap` or
  resync logs, so the PLC came from delivered non-valid ISO SDU callbacks, not
  from omitted callbacks detected by the receiver's sequence tracker. Host
  `btmon` showed continuous ISO TX and completed-packet credits, which prove
  USB/controller submission, not over-air delivery. The cause could be an
  RF/controller/QoS/receiver interaction and is not assigned yet; compare
  against the AX210 under the same receiver placement, QoS, and RF conditions
  on the next branch.

### ASUS USB-BT600 — Candidate / under evaluation

ASUS officially lists Linux, Bluetooth 6.0, and LC3/LE Audio
([product page](https://www.asus.com/networking-iot-servers/wireless-adapters/all-series/usb-bt600/)).
Chipset, VID:PID, `cis-central`, ISO MTU/count, availability/maturity, and the
dynamic receiver sequence remain unrecorded. Status is **Candidate / under
evaluation**, not supported.

### UGREEN CM591 / product 90225 — Unverified / not a candidate

Public Linux USB evidence identifies the ATS2851 chipset and USB ID
`10d7:b012`
([linux-usb](https://www.spinics.net/lists/linux-usb/msg233858.html)), but no
authoritative CIS/ISO proof and no project test exist for this device. Normal
Bluetooth operation does not prove LE Audio (see the [Mandatory controller
contract](#mandatory-controller-contract)). Status: **Unverified / not a
candidate** — the identity evidence alone does not establish incompatibility,
and no support or candidate claim is made.

### UGREEN Bluetooth 6.0 model 75073 — Incompatible

The vendor product listing documents Windows only, Linux unsupported, SBC/AAC
codecs, and LE Audio unsupported
([Amazon listing](https://www.amazon.com/UGREEN-Bluetooth-Receiver-Headphone-Keyboard/dp/B0DYV5MPLF)).
Status: **Incompatible** for this documented Linux native-HCI path. This is a
different product from UGREEN's separate USB-C self-contained LE Audio
transmitter (which runs the stack in dongle firmware and is outside the
native-HCI contract); do not conflate the two.

### Nordic nRF5340 DK (HCI UART controller) — Candidate / under evaluation

NCS v3.3.0 supports running the Bluetooth controller on cpunet with H4 UART on
cpuapp, and this repository's `dongle/` directory is a working
source-controller implementation. USB caveat: NCS v3.3.0's Zephyr USB device
HCI class (`subsys/usb/device_next/class/bt_hci.c`) cannot carry LE HCI ISO —
the controller-to-host TX path handles EVT and ACL only and drops HCI ISO
packet type `0x05`, and the bulk OUT path is hard-coded to ACL buffers and ACL
header parsing. (This is not a missing-USB-isochronous-endpoints issue; those
descriptors concern SCO.) HCI UART is the supported route. Status: **Candidate
/ under evaluation** as a native HCI development adapter; the hardware source
path is not project-validated — existing public evidence does not establish
full dynamic acceptance.

### Nordic nRF54L15 DK (HCI UART controller) — Candidate / under evaluation

NCS v3.3.0's `samples/bluetooth/hci_uart` supports
`nrf54l15dk/nrf54l15/cpuapp`, and the H4 transport handles packet type `0x05`
(ISO). Status: **Candidate / under evaluation** as a native HCI development
adapter; dynamic Linux and receiver validation remains required.

### Seeed XIAO nRF54L15 hardware — Not an adapter candidate

The Seeed XIAO nRF54L15 **board** is distinct from the nRF54L15 DK above and
is **not** an HCI adapter candidate:

- The nRF54L15 has no USB device peripheral.
- The XIAO's USB-C D+/D- connect to the onboard SAMD11 CMSIS-DAP, not the
  nRF54L15.
- The existing SAMD11 CDC UART uses nRF P1.9/P1.8 only; there is no RTS/CTS
  wiring.
- Zephyr HCI UART requires hardware flow control; CDC + `btattach` is not
  robust or plug-and-play.
- A reliable lab route needs an external four-wire USB-UART plus `btattach`.
- True single-cable plug-and-play would require replacement SAMD11 firmware
  implementing USB Bluetooth HCI plus lossless custom UART bridging in 16 KiB
  flash / 4 KiB RAM, likely sacrificing the factory CMSIS-DAP/CDC and needing
  external SWD recovery. Prototype feasibility only; not an active adapter
  candidate.

## Adapter requirements and evaluation

### Bus independence

USB, UART, PCIe, SDIO, and integrated radios are all valid. The transport bus
does not matter; what matters is that Linux gets a **complete HCI interface
plus an ISO-capable transport** for the adapter.

### Mandatory controller contract

- **Bluetooth Core 5.2+ behavior** — but the marketing version is
  insufficient. Version numbers describe the radio generation, not the
  implemented feature set.
- **LE feature "Connected Isochronous Stream - Central"**: Linux checks
  `le_features[3] & HCI_LE_CIS_CENTRAL`; userspace sees it as the
  `cis-central` supported setting.
- **Nonzero `ISO_Data_Packet_Length` and `Total_Num_ISO_Data_Packets`**
  reported by `HCI LE Read Buffer Size V2`. Linux rejects CIS setup when
  `iso_mtu == 0`.
- The transport must handle the **HCI ISO packet type `0x05`**, including
  fragmentation and PB flags, sequence/status headers, credits, and Number of
  Completed Packets.
- Required Linux outgoing sequence: `LE Set CIG Parameters (0x2062)` →
  `LE Create CIS (0x2064)` → `LE CIS Established` → `LE Setup ISO Data Path
  (0x206e)` → ISO data; teardown via `LE Remove CIG (0x2065)`, with support
  for Remove ISO Data Path.
- ISO data path parameters for the source path: direction `0x00`
  (host-to-controller), HCI path `0x00`, transparent codec `0x03`.
- The selected PHY must actually work; **LE 2M is required for project adapter
  acceptance**.

### What the software-LC3 path does not require

The normal BAP path on Linux uses **software LC3 encoding in PipeWire**. It
does **not** require the controller to provide LC3 encoding, `Configure Data
Path`, codec offload, or `LE Read ISO TX Sync`. A controller that lacks those
is not disqualified.

### Resource capacity

- One-CIS mono stream and two-CIS one-CIG stereo (Mode A) sustained
  simultaneously with stable scheduling.
- Sufficient ISO MTU and credits for the negotiated QoS.
- Stable simultaneous ACL + ISO scheduling.

### Evaluated unit

The unit under evaluation is the whole chain: **silicon + firmware + Linux
driver + kernel**. A capability bit in one layer does not survive a broken
firmware or driver, so an adapter is judged as a system, not by a feature
list.

### Static evaluation checklist

- Identify the exact product, chipset, USB or PCI IDs, firmware version,
  driver, and kernel.
- Confirm clean firmware load.
- Confirm `cis-central` is present.
- Confirm the LE 2M PHY is supported.
- Capture controller initialization with `btmon`; inspect Supported Commands
  and the nonzero ISO values from `HCI LE Read Buffer Size V2`.
- `btmgmt info` alone cannot prove full support — it is a necessary, not
  sufficient, check.

### Dynamic acceptance checklist

An adapter is accepted only after the full dynamic sequence passes:

- fresh boot,
- pairing,
- PACS/ASCS resolution,
- 48 kHz configuration negotiation,
- mono sustained stream,
- two-CIS Mode A sustained stream,
- disconnect and reconnect,
- repeat the sequence after a cold boot.

Throughout, inspect the kernel, BlueZ, PipeWire, and `btmon` logs; reject
unexplained warnings, timeouts, resets, and underruns.

### Evaluation record

| Date | Product | Chipset | VID:PID or PCI ID | Bus | Kernel | Driver | Firmware | BlueZ | PipeWire | WirePlumber | `cis-central` | ISO MTU/count | Mono | Two-CIS stereo | Reconnect | Verdict | Evidence link |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| not recorded | Intel Wi-Fi 6E AX210 | Intel AX210 (Wi-Fi 6E) | `8087:0032` (Bluetooth function) | internal USB (M.2 combo card) | 7.1.5 | not recorded | not recorded | 5.86 | 1.6.6 | 0.5.14 | yes | not recorded | not recorded | not recorded | not recorded | **Supported / project-validated** | [host setup](linux-le-audio-host-setup.md#project-tested-baseline) + repo test history |
| 2026-08-09 | ASUS USB-BT540 | not recorded (USB manufacturer string `Realtek`) | `0b05:1bef` | USB (full-speed) | 7.1.5 | `btusb` | not recorded | 5.86 | 1.6.6 | 0.5.15 | yes | 251 / 20 | no (one-CIS run was stereo Mode B) | yes | yes | **Candidate / project-tested with development tool** | [BT540 section](#asus-usb-bt540--candidate--project-tested-with-development-tool) + [headless results](development/bt540-headless-pipewire-results.md) |

The table consolidates environments into one row per adapter.  For the BT540
row: the static capability checks and the development-tool dynamic
two-CIS/reconnect evidence came from the historical Linux 7.1.1 / WirePlumber
0.5.14 run; the kernel 7.1.5 and WirePlumber 0.5.15 columns identify the
post-fix headless one-CIS Mode B production-path rerun environment (see the
[BT540 section](#asus-usb-bt540--candidate--project-tested-with-development-tool)
for both runs' environments).

Note: the AX210 row's `not recorded` fields reflect that the prior project
validation predates this formal record template. The repository did not
capture those details retroactively, so missing retrospective fields do not
become fabricated — they are simply absent from the record. Future
re-validation of the AX210 (or evaluation of any new adapter) should populate
every field.

Template use: populate one row per evaluated adapter, fill every field, keep
the verdict consistent with [Status vocabulary](#status-vocabulary), and link
the evidence (test logs, `btmon` captures, result documents).

### Status vocabulary

- **Supported / project-validated** — the dynamic acceptance sequence passed
  end-to-end against this receiver.
- **Candidate / under evaluation** — being evaluated; not supported yet.
- **Candidate / project-tested with development tool** — the dynamic sequence
  passed against this receiver using the repository's `scripts/bap_central.py`
  development/test tool.  Development-tool validation alone does not
  establish the production BlueZ/PipeWire/WirePlumber path, so the candidate
  is not supported until the listed acceptance gates pass; a candidate may
  have additional production-path evidence (e.g. a headless production run)
  without changing that boundary.  A candidate, not supported yet.
- **Rejected** — the project evaluated the adapter against the acceptance
  sequence and it failed, or conclusive evidence documents incompatibility.
  A single public report does not qualify.
- **Unverified** — no project test and no vendor confirmation for the specific
  claim.
- **Incompatible / not eligible** — documented evidence shows a mandatory
  requirement is absent or the transport cannot support it, so the device
  cannot serve as a native Linux LE Audio HCI source. No project test is
  needed for this verdict. It is distinct from **Rejected** (project-evaluated
  and failed) and from **Unverified** (no project test and no vendor
  confirmation — absence of evidence alone does not establish
  incompatibility).

**No candidate gets Supported status from feature bits or vendor claim alone.**

## Sources

- BlueZ LE Audio support: <https://www.bluez.org/le-audio-support/>
- Bluetooth Core HCI functional specification:
  <https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host-controller-interface/host-controller-interface-functional-specification.html>
- Linux ISO implementation:
  <https://github.com/torvalds/linux/blob/master/net/bluetooth/iso.c> and
  <https://github.com/torvalds/linux/blob/master/net/bluetooth/hci_conn.c>
- Linux practical floor and hardware caveats (Collabora):
  <https://www.collabora.com/news-and-blog/blog/2025/11/24/implementing-bluetooth-le-audio-and-auracast-on-linux-systems/>
- BE200 instability evidence (BlueZ issue 1149):
  <https://github.com/bluez/bluez/issues/1149>
