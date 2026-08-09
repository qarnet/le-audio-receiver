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
- Result: production stack reached and transmitted; **clean acceptance
  failed** on the original 2026-08-09 run (WirePlumber 0.5.14) documented
  below. A post-fix rerun on WirePlumber 0.5.15 (receiver fixes plus the
  0.5.15 upgrade) passed clean acceptance for the headless production-path
  subset — see [Post-fix validation](#post-fix-validation-2026-08-09-wireplumber-0515).
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

## Post-fix validation (2026-08-09, WirePlumber 0.5.15)

After the original run, receiver fixes `9dc0859` (conceal empty ISO SDUs) and
`f2f9336` (deepen I2S startup reservoir) removed the empty-SDU malformation and
the stop-boundary I2S starvation, and the host upgraded to WirePlumber 0.5.15
(NixOS generation 19, `nixpkgs-unstable`; the PipeWire daemon stayed at 1.6.6).
A controlled rerun on `thomas-workstation` (Linux 7.1.5, BlueZ 5.86, PipeWire
1.6.6, WirePlumber 0.5.15) repeated the same headless production path:

- **Negotiated parameters:** one-CIS stereo Mode B — LC3 48 kHz, 7.5 ms frame,
  117 octets per channel, 234-byte SDU, ISO interval 7500 us, PHY 2M, RTN 13,
  transport latency 75 ms, presentation delay 40000 us.
- Bonded auto-reconnect (156 ms, Security level 2, no re-pair); `pw-play`
  exited 0 after 10.060 s of deterministic 1 kHz stereo s16.
- Host ISO TX 1697 packets (SN 0..1696); **final TX → ASCS Disable gap
  1.0128 s**.
- **Receiver summary (exact):**
  `Stream[0] summary: SDUs=1712 decoded=3678 plc=284 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=15`
  with zero `<wrn>` / `<err>` / `Next buffers not supplied` / resets in the
  complete stream-time serial. The startup zero-length SDUs are separately
  concealed (15 empty SDUs, zero malformed-SDU errors); PLC 284 = 2×15 (empty)
  + 2×127 (omitted) reconciles exactly; the 11-block I2S startup reservoir
  survived the ~1.013 s TX-to-Disable gap without underrun.
- **WirePlumber shutdown (exact systemd properties):** run as a transient
  systemd user service with direct ExecStart (MainPID exe verified as the
  0.5.15 store binary), `systemctl --user stop` produced **`Result=success`,
  `ExecMainCode=0` (exited), `ExecMainStatus=0`** with **zero**
  `destroy_proxy` / `leaked proxy` and zero shutdown warning/error/fault lines
  in the complete journal through shutdown (baseline 0.5.14: three leaked
  proxies at shutdown). The three classified startup diagnostics (1× UPower
  `NameHasNoOwner`, 2× `No available A2DP codecs`) are unchanged and allowed.
- **Remaining adapter gates (unchanged):** KDE/Bluedevil UI route on
  `thomas-main`, one-CIS mono, cold unplug/replug repeat, audible-output
  confirmation, the kernel codec-capability init warning (`-22`), and the
  development-tool PLC investigation. Verdict stays **candidate /
  project-tested with development tool**; no recommendation. Of the original
  next-research items above, 1–3 (startup diagnostics, first-16 correlation,
  stop-timing analysis) are addressed by this rerun; 4–6 (AX210 comparison,
  cold replug/audible, KDE/Bluedevil) remain open.

Evidence note (rerun): the controlled rerun artifacts live under
`/tmp/opencode/bt540-wp0515-sd-exit-20260809-120153/` and are local, not
durable repository evidence; the exact summary line, gap timing, counts, and
systemd exit properties recorded above are the durable prose record.
