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
have not passed acceptance yet.

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
- **Rejected** — the project evaluated the adapter against the acceptance
  sequence and it failed, or conclusive evidence documents incompatibility.
  A single public report does not qualify.
- **Unverified** — no project test and no vendor confirmation for the specific
  claim.

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
