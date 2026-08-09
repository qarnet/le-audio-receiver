# BT540 headless production-stack results

Date: 2026-08-09
Host: `thomas-workstation`, headless/SSH user session.

## Purpose and verdict

- Goal: test the same production data path used by KDE, replacing GUI controls
  with CLI only.
- Path actually exercised:
  `bluetoothctl / BlueZ -> WirePlumber BlueZ monitor -> PipeWire Bluetooth
  sink -> PipeWire LC3 encoder -> Linux ISO socket -> BT540 -> receiver`.
- `scripts/bap_central.py` was not used for this run.
- Result: production stack reached and transmitted; **clean acceptance failed**.
  BT540 remains candidate, not recommended/supported.

## Exact environment and identities

- Linux 7.1.1
- BlueZ 5.86
- PipeWire 1.6.6, linked to `liblc3` 1.1.3
- WirePlumber 0.5.14
- BT540 `0b05:1bef`, Realtek manufacturer, `btusb`, `hci1`, address
  `A0:AD:9F:7B:C7:95`
- Receiver `DB:A6:0C:05:A2:AA`
- Competing Nordic HCI UART controller was `hci0`; it was temporarily powered
  off for isolation and restored after test.

## Headless WirePlumber discovery

- Normal WirePlumber `main` profile plus SSH session showed BlueZ device
  paired/connected and PACS/ASCS resolved, but `wpctl status --name` had no
  Bluetooth device/sink.
- Installed WirePlumber `bluez.lua` gates the BlueZ monitor through
  `monitor.bluez.seat-monitoring` when the logind plugin is active. The
  SSH/headless context did not expose Bluetooth graph objects.
- Temporary `wireplumber -p main-embedded` disables seat monitoring and
  immediately created:
  - `bluez_card.DB_A6_0C_05_A2_AA`
  - `bluez_output.DB_A6_0C_05_A2_AA.1`
- Main WirePlumber service was restored afterward. No persistent host config
  was changed.

## Production-path run

- Receiver was disconnected/reconnected through BlueZ after embedded
  WirePlumber start.
- `wpctl status --name` showed the sink as default.
- Ten seconds of generated 1 kHz, stereo, 48 kHz, signed-16 PCM were sent with
  `pw-play --raw --rate 48000 --channels 2 --format s16 --target
  bluez_output.DB_A6_0C_05_A2_AA.1 -`.
- `pw-play` exited zero.
- PipeWire/WirePlumber negotiated one-CIS stereo Mode B:
  - LC3, 48 kHz
  - 7.5 ms frame duration
  - 117 octets per channel
  - 234-byte SDU
  - ISO interval 7500 us
  - PHY 2M
  - RTN 13
  - transport latency 75 ms
  - presentation delay 40000 us
- Host `btmon` captured Set CIG Parameters, Create CIS, Setup ISO Data Path,
  continuous ISO TX on handle 23 from sequence 0 onward, and completed-packet
  credits. Initial host HCI ISO frames were length 238 including HCI header.
- Receiver started CIG 0 / CIS 0 and processed 1444 SDUs.

## Failures requiring investigation

1. **Startup zero-length SDUs:** receiver logged exactly 16
   `malformed SDU len 0 != expected 234` events immediately after stream
   start. Summary: `SDUs=1444 decoded=2856 plc=0 decode_err=16`. Host btmon
   showed continuous 238-byte HCI ISO submissions; that proves host submission,
   not over-air delivery. Do not assign cause yet.
2. **Stop-boundary underrun:** receiver logged
   `i2s_nrfx: Next buffers not supplied on time` when `pw-play` ended, just
   before stream disable. Summary still reported `i2s_underrun=0`, creating a
   diagnostic-accounting mismatch. Do not normalize either issue.
3. **WirePlumber startup diagnostics:** temporary embedded instance logged:
   - UPower `NameHasNoOwner` percentage error
   - `RegisterApplication() failed: org.bluez.Error.Failed`
   - legacy BlueZ A2DP fallback warning
   - two `RegisterEndpoint() failed: org.bluez.Error.InvalidArguments`
   - leaked-proxy warnings during forced temporary-instance shutdown
   BAP sink still appeared and streamed. Separate BAP relevance from unrelated
   A2DP/UPower/shutdown noise during research; do not call diagnostics
   harmless.
4. **KDE not directly exercised:** headless CLI used the same production audio
   components and data path, but Plasma/Bluedevil UI on `thomas-main` remains
   untested.

## Cleanup state and next research

- Normal WirePlumber user service restored active.
- Nordic `hci0` restored powered.
- No persistent host config changed.
- Next research order:
  1. classify WirePlumber/BlueZ registration diagnostics and obtain clean
     headless startup;
  2. correlate first 16 over host, controller, and receiver boundaries;
  3. analyze PipeWire stop timing versus ASCS Disable and receiver I2S drain;
  4. compare same QoS/path with AX210;
  5. run cold replug and physical audible confirmation;
  6. finally verify KDE/Bluedevil route on `thomas-main`.

Evidence note: temporary logs for this run live under `/tmp/opencode/` and are
not durable repository evidence; the exact excerpts and counts recorded above
are the durable record.
