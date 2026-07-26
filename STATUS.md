# STATUS — le-audio-receiver — 2026-07-26

> Snapshot of what works and what's open. Debug history lives in
> `SESSION_DEBUG_2026-07-25.md`; USB/probe map in `SESSION_USB_TABLE.md`.
> This file supersedes those two for "current state" questions.

## Bottom line

**LE Audio transmission works end-to-end** from this Linux PC to the
nRF54L15 receiver via an nRF5340DK flashed with Zephyr `hci_uart` firmware
(USB-attached J-Link VCOM). Two CISes (Mode A stereo) stream 48 kHz LC3 at
100 fps for the full duration with zero flow-control stalls. Bluetooth
discovery, connect, JustWorks pairing (bonded), and BAP negotiation all
work. The I2S/audio-out path on the receiver is out of scope here.

The dongle firmware config is now **folded into the repo** —
`dongle/hci_uart/{app,netcore}.conf` + `fw-build-dongle` /
`fw-flash-dongle` build and flash the upstream hci_uart sample with the
SDC netcore tuned as an LE Audio central. No more `/tmp` conf fragments.
The receiver is flashed with a clean build (no SMP debug logging).

## Hardware in use

| Role | Board | Console | Notes |
|------|-------|---------|-------|
| LE Audio central (USB BT dongle replacement) | nRF5340DK (J-Link `001050023938`) | none | runs `hci_uart`, attached to PC over J-Link VCOM |
| LE Audio receiver | nRF54L15 (Seeed Xiao, CMSIS-DAP `8EE9B3FF`) | `/dev/ttyACM0` @ 115200 | runs this repo's firmware |
| Logic analyzer | fx2lafw | — | D0=SCK, D1=LRCK, D2=SDOUT |

- PC-side BT controller: `hci0` = nRF5340DK `hci_uart` on
  **`/dev/ttyACM2`** (J-Link VCOM, USB iface 02 — not ttyACM1/iface-00)
  @ 1 000 000 baud, H4, HW flow control.
- Receiver advertises as "LE Audio Receiver", static addr
  `DB:A6:0C:05:A2:AA`. BlueZ assigns the central a static random
  `E3:C4:1A:96:D7:D2`.

## What works (verified this session)

- Discovery (raw unfiltered scan), connect (raw direct LE Extended Create
  Connection — kernel accept-list connect path is broken on SDC).
- JustWorks pairing, bonded, persists across reboots. Receiver logs
  `Pairing complete, bonded: 1`.
- BAP unicast server negotiation: 2× SelectProperties, 2×
  SetConfiguration, 2× Acquire (Mode A, FL/FR, SDU 120 @ 10 ms, 2M PHY).
- 2× CIS established, data path HCI both directions.
- ISO data TX: **3000 ISO Data TX packets over 15 s** (= 2 streams ×
  1500 frames) with **3024 Number of Completed Packets** events returned.
  No EAGAIN, no stall. `bap_central.py --duration 15` reports
  `Done: 1500 frames in 15.00 s (100.0 fps)`.
- Receiver-side recovery from the post-stream disconnect panic
  (`audio_sink_stop`: PREPARE before DROP in `src/audio_i2s.c`).

## What does NOT work / open

- **hci_usb firmware cannot do ISO** (Zephyr `bt_hci.c` device_next class
  has no ISO data path; endpoints are descriptor stubs). Don't try to go
  back to it. hci_uart is the only working transport. Patching hci_usb for
  ISO would mean SDK surgery + nRF UDC EP8+ remap — declined. The legacy
  USB BT class (`subsys/usb/device/class/bluetooth.c`) has no
  isochronous endpoints at all either, so there is no USB option in v3.3.0.
- **I2S audio output on the receiver** is explicitly out of scope for the
  current task. The receiver still gets SDUs but the analog path is not
  verified here.
- **btattach not persistent**: runs as a background process from the
  session. Needs a udev rule / systemd unit so it survives reboot and
  re-enumeration.
- **Dongle netcore zombie connection slots**: repeated raw-HCI connects
  without clean disconnects can exhaust the SDC's 2 connection slots
  (`Connection Rejected due to Limited Resources (0x0d)`). Fix is to
  reset the DK (openocd `reset run` on J-Link `001050023938`) and restart
  btattach. A cleaner disconnect path in `bap_central.py` would help.

## Reproduce

### 1. Build + flash the nRF5340DK central (hci_uart)

The dongle config lives in `dongle/hci_uart/{app,netcore}.conf`; the
build helpers build the upstream Zephyr hci_uart sample with those
fragments applied:

```bash
fw-build-dongle      # builds into build/dongle/
fw-flash-dongle      # flashes both cores via the DK's onboard J-Link
```

The netcore conf (`dongle/hci_uart/netcore.conf`) is the load-bearing
part: ISO central, 2 conns / 2 CISes, ext adv, no Coded PHY, no privacy.
See `dongle/README.md` for the full rationale.

### 2. Attach + set up hci0

```bash
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 </dev/null >/tmp/btattach.log 2>&1 &
sleep 3
sudo btmgmt --index hci0 power on
sudo btmgmt --index hci0 io-cap 3     # NINO — required for JustWorks receiver
sudo btmgmt --index hci0 sc on        # receiver requires SC pairing
# verify: settings should include "powered le secure-conn static-addr cis-central"
```

If `btmgmt` reports no adapter, btattach isn't running or the DK
re-enumerated — re-run the btattach line.

### 3. Stream

```bash
python3 scripts/bap_central.py --duration 15
```

Expected: `ACL link up` → `ServicesResolved` → 2× SelectProperties →
2× SetConfiguration → 2× Acquired → `Streaming 1000 Hz sine` →
`Done: 1500 frames in 15.00 s (100.0 fps)`.

Receiver console (`/dev/ttyACM0`, separate terminal) during a good run:
`Pairing complete, bonded: 1`, 2× `ASE Config`, `LC3 decoder[0/1]`,
`Stream[x] started`, `audio_i2s: I2S DMA started`.

### 4. Verify ISO actually crossed HCI (optional)

```bash
setsid sudo btmon -i hci0 -w /tmp/btmon.btsnoop </dev/null >/tmp/btmon.log 2>&1 &
python3 scripts/bap_central.py --duration 15
sudo pkill -f "btmo[n] -i hci0 -w"
sudo btmon -i hci0 -r /tmp/btmon.btsnoop 2>/dev/null | grep -c "ISO Data TX"
sudo btmon -i hci0 -r /tmp/btmon.btsnoop 2>/dev/null | grep -c "Number of Completed Packets"
# expect: ~3000 ISO Data TX, ~3000+ Number of Completed Packets
```

## Why it works now (root causes fixed this session)

| # | Problem | Fix |
|---|---------|-----|
| 1 | hci_usb firmware had no ISO path at all — ISO packets died at the USB layer, no completions ever returned → 3-packet stall | Switched to `hci_uart` (app core is a plain H4 pipe; ISO passes as H4 type 0x05) |
| 2 | Dongle netcore had no ISO / ext-adv / coded-PHY tuning | Netcore conf with `BT_ISO_CENTRAL=y`, `BT_MAX_CONN=2`, `CONN_ISO_STREAMS=2`, `BT_EXT_ADV=y`, `BT_CTLR_PHY_CODED=n`, `BT_CTLR_PRIVACY=n` |
| 3 | Kernel LE connect uses accept-list filtered scan — broken on SDC (zero reports) | Raw-HCI direct `LE Extended Create Connection` via `scripts/hci_raw_connect.py`, wired into `bap_central.py` |
| 4 | BlueZ demanded MITM; receiver is JustWorks-only | NINO agent + `btmgmt io-cap 3` |
| 5 | Kernel mgmt `Pair Device` on raw-created conn completes instantly → BlueZ clears bonding early → auto-rejects JustWorks confirm | Script no longer calls `Pair()`; relies on BlueZ auto-security via GATT |
| 6 | Stale Realtek-era bond on PC vs wiped receiver keys | Deleted stale bond dir, power-cycled adapter |
| 7 | Receiver kernel panic on disconnect after stream (nrfx_i2s ASSERT on de-initialized instance) | `audio_sink_stop()` sends `TRIGGER_PREPARE` before `TRIGGER_DROP` (`src/audio_i2s.c`) |

Full evidence + the dead-ends explored are in
`SESSION_DEBUG_2026-07-25.md`.

## Repo changes this session (all committed)

| File | Change |
|------|--------|
| `src/audio_i2s.c` | `audio_sink_stop()` PREPARE-before-DROP panic fix |
| `scripts/bap_central.py` | NINO agent; raw-HCI connect step; no explicit `Pair()` (auto-security via GATT); stale-conn fallthrough to raw reconnect |
| `scripts/hci_raw_connect.py` | **New.** Raw-HCI direct LE Extended Create Connection; holds socket open |
| `dongle/hci_uart/app.conf` | **New.** App-core conf fragment pinning ISO central + buffer counts + ext adv |
| `dongle/hci_uart/netcore.conf` | **New.** Net-core (hci_ipc/SDC) conf: ISO central, 2 conns / 2 CISes, ext adv, no Coded PHY, no privacy |
| `dongle/README.md` | **New.** Why hci_uart, build/flash/attach instructions |
| `scripts/bin/fw-build-dongle` | **New.** Builds the upstream hci_uart sample with the repo conf fragments |
| `scripts/bin/fw-flash-dongle` | **New.** Flashes both cores via the DK's onboard J-Link (OpenOCD) |
| `STATUS.md` | **New.** This file |
| `SESSION_DEBUG_2026-07-25.md` | **New.** Debug session write-up |

The dongle firmware config is now in the repo — no more `/tmp` fragments.

## Gotchas to remember

- **`pkill -f <pattern>` kills your own shell** when the pattern appears in
  the command line. Use a bracket: `pkill -f "btmo[n] -i hci0 -w"`, or
  `pkill -x btattach`.
- **UART0 on the nRF5340DK is on ttyACM2 (USB iface 02)**, not ttyACM1.
  ttyACM1 stays silent. Verified by sending HCI Reset manually.
- **Raw HCI RX sockets are deaf on this kernel** — observe via
  `btmon -i hci0 -w <file>`, not by reading a raw socket.
- `hcitool lescan` fails with `I/O error` — legacy scan interface not
  supported by this controller build. Not a bug; use `btmon` or
  `bap_central.py` discovery.
- `bluetoothctl scan le` in the background exits instantly and stops
  discovery — useless for scan tests.
- `btmgmt io-cap 3` and `sc on` must be re-applied after adapter power
  loss / USB re-enumeration. Put them next to `btattach` in any
  persistent setup script.