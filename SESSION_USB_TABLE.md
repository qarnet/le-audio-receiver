# SESSION_USB_TABLE — current session

> **This is a session snapshot, not a permanent reference.** Probes get
> replugged and docs rot. Hardware identity below is backed by the raw evidence
> shown in the "Evidence" column. Re-verify with the commands in "Re-verify"
> before trusting any mapping in a future session.

## Goal of this session

Test the **nRF54L15 (Seeed Xiao)** as an LE Audio receiver. Stream audio to
it from Linux via the nRF5340DK `hci_uart` central on **`/dev/ttyACM2`**
at 1 000 000 baud H4 with flow control. Debug via serial (serial-mcp) +
logic analyzer (sigrok-cli).

## USB devices present

| # | Role this session | Product (lsusb) | VID:PID | Serial / uniq | Driver(s) | Linux device(s) | Evidence |
|---|--------------------|-----------------|---------|---------------|-----------|------------------|---------|
| 1 | **nRF54L15 debug probe** (flash + SWD) | Seeed Studio XIAO nrf54 CMSIS-DAP | `2886:0066` | `8EE9B3FF` | `cmsis_dap` (HID) + `cdc_acm` | `/dev/ttyACM0` (CDC iface 2), SWD via HID | `nrf-probes` row: TARGET=nRF54L15, DPIDR `0x6ba02477`, PART `0x00054b15`, VARIANT `AAC0` |
| 2 | **Logic analyzer** (8-ch, I2S bring-up) | Lakeview Research Saleae Logic (fx2lafw) | `0925:3881` | `0925_3881` (no real serial) | `fx2lafw` (sigrok) | sigrok-cli `--driver fx2lafw` | `sigrok-cli --show -d fx2lafw` lists 8 channels D0–D7, supported rates incl. 24 MHz |
| 3 | **nRF5340DK** (hci_uart central + debug) | SEGGER J-Link OB-nRF5340 | `1366:1061` | `001050023938` | `jlink` (openocd) + `cdc_acm` | `/dev/ttyACM2` (hci_uart H4, iface 2) | `nrf-probes` row: TARGET=nRF5340, DPIDR `0x6ba02477` |

## Non-session devices (ignore)

- `046d:c548` Logitech Logi Bolt Receiver — keyboard/mouse. Don't touch.
- USB root hubs (1d6b:0002/0003) — ignore.

## Tool mapping

| Tool | Command this session | Notes |
|------|----------------------|------|
| Flash nRF54L15 | `fw-flash-54l15` (uses Xiao CMSIS-DAP, auto-detected) | OpenOCD via `interface/cmsis-dap.cfg`, RRAM write-enable `mww 0x5004b500 0x101`. No flash driver needed. |
| Flash nRF5340DK (dongle) | `fw-flash-dongle` (uses DK J-Link) | Flashes hci_uart firmware to both cores. |
| Serial console nRF54L15 | serial-mcp on `/dev/ttyACM0` @ 115200 8N1 | Xiao SAMD11 bridges UART20 (P1.9 TX / P1.8 RX) to USB CDC. |
| Serial console nRF5340DK | serial-mcp on `/dev/ttyACM1` @ 115200 8N1 (J-Link CDC iface 0) | DK app core console; ttyACM2 is the H4 HCI pipe. |
| Logic analyzer | `sigrok-cli --driver fx2lafw --channels D0,D1,D2,D3 ...` | Sample ≥10 MHz for 3.072 MHz BCK; 24 MHz ideal. CH0=D0, CH1=D1, CH2=D2, CH3=3V3. |
| BlueZ central | `btmgmt -i hci0`, `btmon -i hci0` | hci0 = nRF5340DK hci_uart, attached via btattach on `/dev/ttyACM2`. |

## Central setup (nRF5340DK hci_uart)

The nRF5340DK runs the Zephyr `hci_uart` sample with ISO central config
(`dongle/hci_uart/{app,netcore}.conf`). Attach:

```bash
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 </dev/null >/tmp/btattach.log 2>&1 &
sleep 3
sudo btmgmt --index hci0 power on
sudo btmgmt --index hci0 io-cap 3     # NINO — required for JustWorks receiver
sudo btmgmt --index hci0 sc on        # receiver requires SC pairing
```

Stream: `python3 scripts/bap_central.py --duration 15`

## Logic analyzer channel map (this session only)

| sigrok channel | Probe to | Signal |
|----------------|----------|--------|
| D0 | Xiao **D0** (P1.4) | I2S BCK (expect ~3.072 MHz while streaming) |
| D1 | Xiao **D1** (P1.5) | I2S LRCK (expect 48 kHz = BCK/64) |
| D2 | Xiao **D2** (P1.6) | I2S SDOUT/DIN (toggles while streaming) |
| D3 | Xiao **3V3** | power rail reference |
| D4–D7 | not connected | — |

## Re-verify commands (run before trusting this table later)

```bash
# USB devices + serials
lsusb
for d in /dev/bus/usb/*/*; do udevadm info -q property -n "$d" 2>/dev/null \
  | grep -E "^ID_VENDOR=|^ID_MODEL=|^ID_SERIAL=" | tr '\n' ' '; echo "  $d"; done

# CMSIS-DAP probe identity (nRF54L15)
nrf-probes
nrf-probes --find nrf54l15

# Bluetooth controllers
btmgmt -i hci0 info

# Serial ports
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
for d in /dev/ttyACM* /dev/ttyUSB*; do [ -e "$d" ] && echo "--- $d ---" \
  && udevadm info -q property -n "$d" | grep -E "^ID_MODEL=|^ID_SERIAL="; done

# sigrok fx2lafw LA
sigrok-cli --show -d fx2lafw
```