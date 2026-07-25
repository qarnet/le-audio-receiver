# SESSION_USB_TABLE — 2026-07-25 bring-up session

> **This is a session snapshot, not a permanent reference.** Probes get
> replugged and docs rot. Hardware identity below is backed by the raw evidence
> shown in the "Evidence" column. Re-verify with the commands in "Re-verify"
> before trusting any mapping in a future session.

## Goal of this session

Bring up the **nRF54L15 (Seeed Xiao)** as an LE Audio receiver. Stream audio to
it from Linux via BlueZ. Primary central: **hci0 = host Realtek RTL8761BU**
(the "BT540", `0b05:1bef`). Fallback central: **hci1 = nRF5340DK USB BLE HCI**
(`2fe3:000b`) if hci0 fails (Realtek ISO/CIS quirks — see Arch threads on
RTL8761B LE Audio). Fallback receiver: **nRF5340DK application core** (flashed
via the J-Link, `1366:1061`) if the nRF54L15 path fails. Debug via serial
(serial-mcp) + logic analyzer (sigrok-cli).

## USB devices present

| # | Role this session | Product (lsusb) | VID:PID | Serial / uniq | Driver(s) | Linux device(s) | Evidence |
|---|--------------------|-----------------|---------|---------------|-----------|------------------|---------|
| 1 | **nRF54L15 debug probe** (flash + SWD) | Seeed Studio XIAO nrf54 CMSIS-DAP | `2886:0066` | `8EE9B3FF` | `cmsis_dap` (HID) + `cdc_acm` | `/dev/ttyACM0` (CDC iface 2), SWD via HID | `nrf-probes` row: TARGET=nRF54L15, DPIDR `0x6ba02477`, PART `0x00054b15`, VARIANT `AAC0` |
| 2 | **Logic analyzer** (8-ch, I2S bring-up) | Lakeview Research Saleae Logic (fx2lafw) | `0925:3881` | `0925_3881` (no real serial) | `fx2lafw` (sigrok) | sigrok-cli `--driver fx2lafw` | `sigrok-cli --show -d fx2lafw` lists 8 channels D0–D7, supported rates incl. 24 MHz |
| 3 | **nRF5340DK debug probe** (fallback flash path) | SEGGER J-Link | `1366:1061` | `001050023938` | `jlink` (openocd `interface/jlink.cfg`) | `/dev/ttyACM1` (CDC iface 0), `/dev/ttyACM2` (CDC iface 2) | lsusb udev `ID_SERIAL=SEGGER_J-Link_001050023938`; udev rule being added to `nixos-config-flake/data/usb-device-extras.json` (VID 1366 PID 1061, tagged debug-probe) — needs `nixos-rebuild` to apply |
| 4 | **nRF5340DK nRF USB → BLE HCI** (fallback central; what earlier analysis mis-called "BT540") | NordicSemiconductor nRF5340 DK BT HCI | `2fe3:000b` | `22C50DB24785D087` | `btusb` | `hci1` (BD `00:00:00:00:00:00`, manufacturer 89 = Nordic Semiconductor) | `btmgmt -i hci1 info`: manufacturer 89, supports cis-central/cis-peripheral/iso-broadcaster/sync-receiver (LE Audio ISO). Currently UP, BD all-zeros. **Fallback only** — try hci0 first. |
| 5 | **BT540 / primary Bluetooth central** (host built-in) | ASUSTek Bluetooth Controller (Realtek RTL8761BU) | `0b05:1bef` | `Realtek_Bluetooth_Controller` | `btusb` | `hci0` (BD `A0:AD:9F:7B:C7:95`, manufacturer 93 = Realtek) | `btmgmt -i hci0 info`: manufacturer 93, BR/EDR+LE, supports cis-central/cis-peripheral/iso-broadcaster/sync-receiver. Already in nixos-config catalog tagged `bluetooth`. **This is the "BT540" — try hci0 first.** |

## Non-session devices (ignore)

- `046d:c548` Logitech Logi Bolt Receiver — keyboard/mouse. Don't touch.
- USB root hubs (1d6b:0002/0003) — ignore.

## Tool mapping

| Tool | Command this session | Notes |
|------|----------------------|------|
| Flash nRF54L15 | `fw-flash-54l15` (uses Xiao CMSIS-DAP, auto-detected) | OpenOCD via `interface/cmsis-dap.cfg`, RRAM write-enable `mww 0x5004b500 0x101`. No flash driver needed. |
| Flash nRF5340DK (fallback) | `fw-flash-5340` (auto-detects probe via `nrf-probes`) — BUT J-Link udev needs `nixos-rebuild` first | `fw-flash-5340` calls `nrf-probes --find nrf53`; if the J-Link isn't recognized as the nRF53 probe, fall back to explicit J-Link openocd (`interface/jlink.cfg`, serial `001050023938`). |
| Serial console nRF54L15 | serial-mcp on `/dev/ttyACM0` @ 115200 8N1 | Xiao SAMD11 bridges UART20 (P1.9 TX / P1.8 RX) to USB CDC. |
| Serial console nRF5340DK | serial-mcp on `/dev/ttyACM1` or `/dev/ttyACM2` @ 115200 8N1 (J-Link CDC) — confirm which iface exposes the console | Not yet verified this session; the E83 module's CH340X (`/dev/ttyUSB0`) is the documented console per AGENTS.md, but the DK is connected via J-Link CDC here. |
| Logic analyzer | `sigrok-cli --driver fx2lafw --channels D0,D1,D2,D3 ...` | Sample ≥10 MHz for 3.072 MHz BCK; 24 MHz ideal. CH0=D0, CH1=D1, CH2=D2, CH3=3V3. |
| BlueZ central (primary) | `btmgmt -i hci0`, `bluetoothctl --agent` selecting hci0, `btmon -i hci0` | hci0 = Realtek BT540. Default controller, but force LE Audio work onto it explicitly to avoid BlueZ binding to hci1. |
| BlueZ central (fallback) | same with `-i hci1` | hci1 = nRF5340 USB HCI. May need static/privacy address due to all-zeros BD. |

## Logic analyzer channel map (this session only)

| sigrok channel | Probe to | Signal |
|----------------|----------|--------|
| D0 | Xiao **D0** (P1.4) | I2S BCK (expect ~3.072 MHz while streaming) |
| D1 | Xiao **D1** (P1.5) | I2S LRCK (expect 48 kHz = BCK/64) |
| D2 | Xiao **D2** (P1.6) | I2S SDOUT/DIN (toggles while streaming) |
| D3 | Xiao **3V3** | power rail reference |
| D4–D7 | not connected | — |

## Open access issues to resolve before flashing/central work

### J-Link access (nRF5340DK fallback flash path)

The J-Link USB node is `crw-rw-r-- root:root` with no udev tag for the
`users`/`dialout` groups — `openocd` fails with `LIBUSB_ERROR_ACCESS`. Fix is
**not a hotfix**: a new entry was added to
`~/nixos-config-flake/data/usb-device-extras.json` (VID `1366` PID `1061`,
tagged `debug-probe`, accessKinds `usb`+`tty`). The shared `embedded-usb-access`
module on `thomas-workstation` already includes the `debug-probe` tag, so the
next `nixos-rebuild switch --flake .#thomas-workstation` will generate the udev
rule. Run the rebuild before attempting the fallback flash path.

The nRF54L15 path (Xiao CMSIS-DAP) does NOT hit this — the Xiao probe already
has the right group/perms (`nrf-probes` reads it fine).

### hci0 (Realtek BT540) — primary central

`btmgmt -i hci0 info` shows manufacturer 93 (Realtek), BR/EDR+LE, and the ISO
settings LE Audio needs (cis-central, cis-peripheral, iso-broadcaster,
sync-receiver). Firmware `rtl_bt/rtl8761bu_fw.bin` loaded fine at boot. Known
risk: some Realtek LE Audio ISO paths are flaky on mainline kernels — Arch
threads (`bbs.archlinux.org/viewtopic.php?id=309671`, `282351`) document
streaming glitches with RTL8761B. Try hci0 first; if ISO connect/setup fails,
fall back to hci1.

### hci1 (nRF5340 USB BLE HCI) — fallback central

`btmgmt -i hci1 info` shows manufacturer 89 (Nordic Semiconductor), LE only,
supports the ISO/CIS settings. BD address is `00:00:00:00:00:00` until BlueZ
assigns one — LE Audio central role may need a static address set or
`bluetoothctl` may refuse to scan. If the all-zeros BD blocks pairing, set a
static address via `btmgmt -i hci1 public-addr <XX:XX:...>` (needs the
controller to allow it) or use `btmgmt -i hci1 privacy on` to get a generated
address.

## Re-verify commands (run before trusting this table later)

```bash
# USB devices + serials
lsusb
for d in /dev/bus/usb/*/*; do udevadm info -q property -n "$d" 2>/dev/null \
  | grep -E "^ID_VENDOR=|^ID_MODEL=|^ID_SERIAL=" | tr '\n' ' '; echo "  $d"; done

# CMSIS-DAP probe identity (nRF54L15)
nrf-probes
nrf-probes --find nrf54l15

# Bluetooth controllers + manufacturers
btmgmt -i hci0 info
btmgmt -i hci1 info
hciconfig -a

# Serial ports
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
for d in /dev/ttyACM* /dev/ttyUSB*; do [ -e "$d" ] && echo "--- $d ---" \
  && udevadm info -q property -n "$d" | grep -E "^ID_MODEL=|^ID_SERIAL="; done

# sigrok fx2lafw LA
sigrok-cli --show -d fx2lafw
```