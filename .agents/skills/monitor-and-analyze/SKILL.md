---
name: monitor-and-analyze
description: Flash the nRF5340 if changes exist, start background serial logging from both ACM ports, reset the device so boot logs are captured, wait for user test feedback, then analyze logs for errors and suggest fixes.
---

# monitor-and-analyze

Use this skill when the user wants to test the physical nRF5340 DK and have
captured output analyzed.  The device is monitored via the two J-Link virtual
COM ports (`/dev/ttyACM0` and `/dev/ttyACM1`).  ACM1 is the application core;
ACM0 is the network core (`hci_ipc`).  Monitoring runs in the background so the
user can still interact with the chat.

## Steps

### 1. Flash if there are pending changes

Check whether the working tree has modifications to build inputs:

```bash
cd /home/thomas-win/opencode/le-audio-receiver
git status --short
```

If `src/`, `boards/`, `prj.conf`, `sysbuild.cmake`, `sysbuild.conf`, or
`CMakeLists.txt` show modifications, do a pristine build and flash **before**
starting monitoring:

```bash
cd ~/ncs/v3.3.0
nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 -- \
  bash -c "cd ~/ncs/v3.3.0 && west build -b nrf5340dk/nrf5340/cpuapp --sysbuild --pristine -s /home/thomas-win/opencode/le-audio-receiver"

cd ~/ncs/v3.3.0
nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 -- \
  bash -c "cd ~/ncs/v3.3.0 && west flash --build-dir build"
```

Use `--pristine` after any `prj.conf`, overlay, or `sysbuild.cmake` change.

### 2. Clean up old logs and kill stale readers

```bash
# Kill any lingering reader processes or tmux sessions
for s in acm0 acm1; do tmux has-session -t "$s" 2>/dev/null && tmux kill-session -t "$s"; done
for pid in $(cat /tmp/acm_mon.pid 2>/dev/null); do kill -9 "$pid" 2>/dev/null; done
pkill -9 -f "python3 .*scripts/read_acm.py" 2>/dev/null || true
rm -f /tmp/acm0.log /tmp/acm1.log /tmp/acm_mon.pid
```

### 3. Start background monitoring

Use a Python + pyserial reader that survives USB disconnects (device resets).
The helper lives at `scripts/read_acm.py` in the repo:

```bash
# Start ACM0 reader in detached tmux session
tmux new-session -d -s acm0 \
  "python3 /home/thomas-win/opencode/le-audio-receiver/scripts/read_acm.py ttyACM0 /tmp/acm0.log"

# Start ACM1 reader in detached tmux session
tmux new-session -d -s acm1 \
  "python3 /home/thomas-win/opencode/le-audio-receiver/scripts/read_acm.py ttyACM1 /tmp/acm1.log"

echo "ACM readers in tmux: acm0, acm1"
```

**`scripts/read_acm.py` helper** (already in repo):
```python
#!/usr/bin/env python3
"""Robust ACM reader: reopens port if device resets."""
import serial, time, os, sys
PORT = sys.argv[1]
OUT  = sys.argv[2]
DEV = f"/dev/{PORT}"
T_START = time.time()
with open(OUT, "wb") as f:
    while time.time() - T_START < 30:
        try:
            if not os.access(DEV, os.R_OK):
                time.sleep(0.2)
                continue
            s = serial.Serial(DEV, 115200, timeout=0.5)
            while time.time() - T_START < 30:
                data = s.read(1024)
                if data:
                    f.write(data)
                    f.flush()
                elif not os.access(DEV, os.R_OK):
                    break
            s.close()
        except serial.SerialException:
            time.sleep(0.2)
```

### 4. Reset the device

**This is critical.** After the readers are started, reset the nRF5340 so boot logs are captured:

```bash
nrfutil device reset
```

Wait ~3 s for the boot banner to appear on both ports.

### 5. Notify the user

Send a message telling the user monitoring is active and what they should do:

> Monitoring is active on both cores (ACM0 = net core boot, ACM1 = app core) for up to 30 seconds.
> The device has been reset so boot logs are captured.
> Go ahead and test — scan, connect, pair, stream audio, etc. Reply when you're
> done. The capture will stop automatically after 30 s even if you don't reply.

### 6. Wait for the user to finish testing

Pause for the user's next message.

### 7. Stop monitoring and collect logs

Kill the tmux sessions and dump the logs:

```bash
for s in acm0 acm1; do tmux has-session -t "$s" 2>/dev/null && tmux kill-session -t "$s"; done

echo "=== /dev/ttyACM0 (net core) ==="
cat /tmp/acm0.log
echo ""
echo "=== /dev/ttyACM1 (app core) ==="
cat /tmp/acm1.log
```

### 8. Analyze output

Look for the patterns below.  Present the relevant log lines and suggest fixes.

| Pattern | Meaning | Suggested fix |
|---------|---------|---------------|
| `Bluetooth init failed` (e.g. `-5`) | Controller stayed on SoftDevice (`bt_hci_sdc`) because the SW-Split devicetree overlay is missing. | Confirm `sysbuild.cmake` applies **both** the DTS overlay `bt-ll-sw-split.overlay` and the Kconfig overlay `nrf5340_cpunet_iso_peripheral-bt_ll_sw_split.conf` to the `hci_ipc` target. |
| `settings_load() failed` | ZMS/flash storage could not initialize. | Verify `CONFIG_FLASH=y`, `CONFIG_FLASH_PAGE_LAYOUT=y`, `CONFIG_FLASH_MAP=y`, and `CONFIG_SETTINGS_ZMS=y`. Without all four, the backend silently falls to `SETTINGS_NONE`. |
| `settings_load() OK` followed by `add_bonded_addr_to_client_list` with an old peer address | Stale bond from a previous pairing session is being reloaded from the settings partition. | **Use `nrf53_recover` via openocd-master before flashing** to erase ALL non-volatile memory including the settings partition. Alternatively, delete the bond on the central (e.g. `bluetoothctl remove`). |
| `bt_pacs_register: Failed to register ASCS in gatt DB: -22` | `settings_load()` was skipped or called too early/late. | `settings_load()` must be called **after** `bt_enable(NULL)` and **before** `bt_pacs_register()`. Do NOT skip `settings_load()`. |
| `Pairing failed`, `Security failed`, or central shows "incorrect PIN" / bond errors | MITM is enforced or there is a stale bond mismatch. | Set `CONFIG_BT_SMP_ENFORCE_MITM=n`. If a stale bond exists, do a full chip erase with `nrf53_recover` via openocd-master before flashing. |
| `Host buffer count mismatch` | App ACL/ISO TX counts differ from the SW Split controller report. | Ensure `CONFIG_BT_BUF_ACL_TX_COUNT=7` and `CONFIG_BT_ISO_TX_BUF_COUNT=6` match the SW Split controller's reported buffers. |
| `Adv start failed` or `Adv create failed` | Advertising could not be configured or is already running. | Check `BT_LE_EXT_ADV_START_DEFAULT` usage and whether advertising is restarted after `sem_disconnected`. |
| `LC3 decode error` | Corrupted bitstream or codec config mismatch. | Cross-check the source's codec config against the declared `lc3_codec_cap` (freq, frame duration, channel count, octets per frame). |
| `I2S.*underrun` / `Next buffers not supplied` | I2S TX starved. | This is **normal before a client connects**. If it persists during active streaming, increase buffer depth in `audio_i2s.c` or check for missed ISO SDUs. |
| **Good signs:** `BLE ready`, `settings_load() OK`, `Advertising as "LE Audio Receiver"`, `Connected`, `Stream started`, `Pairing complete` | — | No action needed. |

If you find an error, **ask the user what they want to do next** and offer the
specific fix from the table above.  Do not apply the fix automatically unless
the user explicitly says so.

## Notes

- The Python reader (`scripts/read_acm.py`) uses pyserial and reopen logic to
  survive the USB disconnect/reconnect that happens when the nRF5340 resets.
- `nrfutil device recover` performs ERASEALL via CTRL-AP and wipes **all**
  non-volatile storage (flash code + settings partition). Use this when you
  suspect stale bonds or corrupted settings.
- `west flash` alone only erases the firmware address ranges and **preserves**
  the settings partition.
- `stdbuf -oL timeout 30 cat /dev/ttyACM...` is NOT recommended; it caused
  hangs and lost boot log when the device reset.
