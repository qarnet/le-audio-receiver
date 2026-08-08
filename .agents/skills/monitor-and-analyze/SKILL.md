---
name: monitor-and-analyze
description: Flash the receiver if changes exist, start background serial logging from the receiver console, reset the device so boot logs are captured, wait for user test feedback, then analyze logs for errors and suggest fixes.
---

# monitor-and-analyze

Use this skill when the user wants to test the physical receiver and have
captured output analyzed.  The receiver console is the E83 nRF5340 app core on
`/dev/ttyUSB0` (CH340X bridge) at 115200 8N1.  Monitoring runs in the
background so the user can still interact with the chat.

## Steps

### 1. Flash if there are pending changes

Check whether the working tree has modifications to build inputs:

```bash
cd /home/thomas-workstation/repos/le-audio-receiver
git status --short
```

If `src/`, `boards/`, `prj.conf`, `sysbuild.cmake`, or `CMakeLists.txt` show
modifications, build and flash **before** starting monitoring:

```bash
fw-build-5340     # from the repo root, in the dev shell (direnv allow / nix develop)
fw-flash-5340     # flashes app + net core; resolves the probe via nrf-probes
```

Use `--pristine` (the build helper already does) after any `prj.conf`,
overlay, or `sysbuild.cmake` change.

### 2. Clean up old logs and stop stale readers

```bash
# Stop any lingering reader process (targeted, never a global pkill -9)
pkill -f "read_acm.py ttyUSB0" 2>/dev/null || true
rm -f /tmp/e83.log /tmp/e83_mon.pid
```

### 3. Start background monitoring

The helper lives at `scripts/read_acm.py` in the repo and survives USB
disconnects (device resets).  Start the reader FIRST, then reset the device:

```bash
python3 /home/thomas-workstation/repos/le-audio-receiver/scripts/read_acm.py ttyUSB0 /tmp/e83.log 30 &
echo $! > /tmp/e83_mon.pid
```

### 4. Reset the device

**This is critical.** After the reader is started, reset the nRF5340 via
OpenOCD so boot logs are captured (the reader must be running before the
reset):

```bash
openocd -f interface/cmsis-dap.cfg \
  -c "adapter serial $(nrf-probes --find nrf53)" \
  -c "transport select swd" -c "adapter speed 1000" \
  -f target/nordic/nrf53.cfg -c init -c "reset run" -c shutdown
```

Wait ~3 s for the boot banner to appear.

### 5. Notify the user

Send a message telling the user monitoring is active and what they should do:

> Monitoring is active on the receiver console (/dev/ttyUSB0) for up to 30 seconds.
> The device has been reset so boot logs are captured.
> Go ahead and test — scan, connect, pair, stream audio, etc. Reply when you're
> done. The capture will stop automatically after 30 s even if you don't reply.

### 6. Wait for the user to finish testing

Pause for the user's next message.

### 7. Stop monitoring and collect logs

```bash
kill "$(cat /tmp/e83_mon.pid 2>/dev/null)" 2>/dev/null || true
echo "=== /dev/ttyUSB0 (receiver console) ==="
cat /tmp/e83.log
```

### 8. Analyze output

Look for the patterns below.  Present the relevant log lines and suggest fixes.

| Pattern | Meaning | Suggested fix |
|---------|---------|---------------|
| `Bluetooth init failed` (e.g. `-5`) | Controller stayed on SoftDevice (`bt_hci_sdc`) because the SW-Split devicetree overlay is missing. | Confirm `sysbuild.cmake` applies **both** the DTS overlay `bt-ll-sw-split.overlay` and the Kconfig overlay `nrf5340_cpunet_iso_peripheral-bt_ll_sw_split.conf` to the `hci_ipc` target. |
| `settings_load() failed` | ZMS/flash storage could not initialize. | Verify `CONFIG_FLASH=y`, `CONFIG_FLASH_PAGE_LAYOUT=y`, `CONFIG_FLASH_MAP=y`, and `CONFIG_SETTINGS_ZMS=y`. Without all four, the backend silently falls to `SETTINGS_NONE`. |
| Stale bond from a previous pairing session reloaded from settings | `west flash` does not erase the settings partition. | Mass-erase via openocd-master (`nrf53_recover` — nRF5340 only) before flashing, or delete the bond on the central (e.g. `bluetoothctl remove`). Never use `nrfutil device recover` (not part of this repo's tooling). |
| `bt_pacs_register: Failed to register ASCS in gatt DB: -22` | `settings_load()` was skipped or called too early/late. | `settings_load()` must be called **after** `bt_enable(NULL)` and **before** `bt_pacs_register()`. Do NOT skip `settings_load()`. |
| `Pairing failed`, `Security failed`, or central shows "incorrect PIN" / bond errors | MITM is enforced or there is a stale bond mismatch. | Set `CONFIG_BT_SMP_ENFORCE_MITM=n`. If a stale bond exists, mass-erase with openocd-master `nrf53_recover` (nRF5340 only) before flashing, or remove the bond on the central. |
| `Host buffer count mismatch` | App ACL/ISO TX counts differ from the SW Split controller report. | Ensure `CONFIG_BT_BUF_ACL_TX_COUNT=7` and `CONFIG_BT_ISO_TX_BUF_COUNT=6` match the SW Split controller's reported buffers. |
| `Adv start failed` or `Adv create failed` | Advertising could not be configured or is already running. | Check `BT_LE_EXT_ADV_START_DEFAULT` usage and whether advertising is restarted after `sem_disconnected`. |
| `LC3 decode error` | Corrupted bitstream or codec config mismatch. | Cross-check the source's codec config against the declared `lc3_codec_cap` (freq, frame duration, channel count, octets per frame). |
| `I2S.*underrun` / `Next buffers not supplied` | I2S TX starved. | **Never normalize this warning.** It is normal only before a client connects; if it appears during active streaming it is a real underrun — recovery is automatic once the PI clock recovery controller converges, and `TRIGGER_PREPARE` + re-arm handle transient underruns. Repeated steady-state underruns need investigation, not dismissal. |
| **Good signs:** `BLE ready`, `settings_load() OK`, `Advertising as "LE Audio Receiver"`, `Connected`, `Stream started`, `Pairing complete` | — | No action needed. |

If you find an error, **ask the user what they want to do next** and offer the
specific fix from the table above.  Do not apply the fix automatically unless
the user explicitly says so.

## Notes

- The Python reader (`scripts/read_acm.py`) uses pyserial and reopen logic to
  survive the USB disconnect/reconnect that happens when the receiver resets.
- Reset and recovery are **OpenOCD-only** (`openocd-master`): `fw-flash-5340`
  programs `UICR.APPROTECT` after flashing; `nrf53_recover` is the only
  mass-erase path and works on the nRF5340 only (the nRF54L15 has no recovery
  path in current tooling).  Never use probe-rs or `nrfutil device recover`.
- `west flash` alone only erases the firmware address ranges and **preserves**
  the settings partition.
- `stdbuf -oL timeout 30 cat /dev/ttyUSB0` is NOT recommended; it caused
  hangs and lost boot log when the device reset.
