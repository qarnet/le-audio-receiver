# Phase 4a.1 — New-DAC Main Receiver Retest Results

Date: 2026-07-26

## Outcome: TECHNICAL FAILURE — PENDING USER LISTENING

Receiver streams but fails the handoff's steady-state error criteria (I2S slab
full + DMA underrun). Audible output not yet confirmed.

## Pre-flight inventory

```
git status --short
 M boards/nrf54l15dk_nrf54l15_cpuapp.overlay    (unstaged diagnostic)
 M src/bt_bap.c                                   (unstaged diagnostic)
?? docs/development/phase4a1-new-dac-main-pipeline-handoff.md
```

- nRF54L15 probe: Seeed Studio XIAO nrf54 CMSIS-DAP, serial `8EE9B3FF`,
  DPIDR `0x6ba02477`, PART `0x00054b15`, VARIANT `AAC0`
- hci0: `powered le secure-conn static-addr cis-central`
- Central: nRF5340DK `hci_uart` on `/dev/ttyACM2` (E3:C4:1A:96:D7:D2)
- Receiver: nRF54L15 Xiao on `/dev/ttyACM0` (DB:A6:0C:05:A2:AA)
- New DAC: connected D0/P1.04=BCK, D1/P1.05=LRCK, D2/P1.06=DIN, GND/AGND

## Build

`fw-build-54l15` — clean, no compile errors. Warnings: partition-manager
deprecation (known), `drivers__watchdog` no-sources (known), `__ASSERT`
globally enabled (expected for debug). Memory: flash 447 KB (30.75%), RAM
135 KB (70.20%).

## Boot — PASS

Serial log confirms required boot evidence:
- `*** Booting nRF Connect SDK v3.3.0 ***`
- `*** Using Zephyr OS v4.3.99 ***`
- `fs_zms: 2 Sectors of 4096 bytes`
- `SoftDevice Controller build revision: ...` (SDC with ISO)
- `main: BLE ready`
- `settings_load() OK`
- `audio_volume: VCP ready (default vol=195)`
- `audio_i2s: I2S ready (48 kHz, 16-bit, stereo, 12 blocks)`
- `main: Advertising as "LE Audio Receiver"`
- `bt_bap: Connected: E3:C4:1A:96:D7:D2 (random)`

No warnings, no errors, no surprise reboots.

## Stream — TECHNICAL FAILURE

30-second Mode A stereo stream: `scripts/bap_central.py --duration 30 --freq 1000`.

Central-side: `Done: 3000 frames in 30.00 s (100.0 fps)`, 2 transports,
SDU 120 bytes, no EAGAIN.

Receiver-side:

| Metric | Value |
|--------|-------|
| Stream setup | 2× ASE Config, 2× QoS, 2× Enable, 2× LC3 decoder (48 kHz / 10 ms / ch=1) |
| Stream started | Stream[1] at 12.51s (CIG 0 CIS 1), Stream[0] at 12.87s (CIG 0 CIS 0) |
| I2S DMA started | 12.87s |
| Valid ISO frames received | 5,621 (final tally: valid=5621 invalid=479, total≈6100 ≈ 2×3000) |
| I2S slab full events | **14** — first at 20.07s (7.2s into stream), every ~1.57s thereafter |
| DMA underrun | 1× `i2s_nrfx: Next buffers not supplied on time` at 43.24s (stream stop) |
| Stream stopped | Stream[0] at 43.13s, Stream[1] at 43.30s (reason 0x13 — remote user terminated) |
| Disconnected | 45.31s (reason 0x13) |

### Slab-full pattern

```
[01:05:20.068,255] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:21.618,185] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:23.198,149] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:24.748,042] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:26.307,972] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:27.877,913] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:29.457,841] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:31.027,757] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:32.607,685] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:34.187,610] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:35.767,541] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:37.357,469] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:38.947,397] <wrn> audio_i2s: I2S slab full — dropping frame
[01:05:40.507,327] <wrn> audio_i2s: I2S slab full — dropping frame
```

14 events at ~1.57s intervals. The I2S output queue backs up faster than
the 48 kHz DMA drains it. The PI clock recovery controller is active but
insufficient — the output-clamp ceiling (±500 ppm) or the SAMPLE_ADJUST
actuator's single-sample-per-block limit (±2083 ppm) cannot close the gap
between ISO arrival rate and I2S consumption rate.

### audio_stats shows 0 — measurement artifact, not data loss

`audio status` after the stream reported `Frames decoded: 0`. Root cause:
the `disconnected()` callback in `src/bt_bap.c` calls `audio_stats_reset()`
at line 541, zeroing all counters before the post-stream query. The stats
were correct during active streaming but lost on teardown. Not a pipeline
defect — a test-order artifact. The stream_recv tally counters (valid=5621)
confirm frames were received and decoded.

### stream_recv[0] logging gap

All diagnostic/tally log entries show `stream_recv[1]` (idx=1), never
`stream_recv[0]` (idx=0). The total frame count (~6100 ≈ 2×3000) confirms
both CISes delivered data; the logging pattern is explained by static
counters shared across both stream_recv call sites and the diagnostic/tally
thresholds always being reached on idx=1 calls due to the ISO sync order.
Sink-index matching (`sink_idx()`) is correct — confirmed by `Stream[0]` vs
`Stream[1]` started messages.

## Logic analyzer

**Not captured.** sigrok-cli 0.8.0 is installed but no fx2lafw hardware
was detected (`lsusb` found no FX2 device). External I2S waveform
measurement (BCK/LRCK/SDOUT/3V3) deferred.

## btmon

Capture failed: `Failed to bind channel: Operation not permitted`. The log
file is 16 bytes (empty). HCI-level ISO evidence not collected this run.

## Post-stream state

Receiver reconnects automatically after disconnect:
```
[01:05:45.313,987] Disconnected: E3:C4:1A:96:D7:D2 (random) reason 0x13
[01:05:45.314,086] Restarting advertising...
[01:05:45.314,328] Advertising again
[01:05:45.382,184] Connected: E3:C4:1A:96:D7:D2 (random)
```

Clean reconnect — no panic, no zombie state.

## Decision: TECHNICAL FAILURE

Per handoff decision rules, PASS requires:

> no steady-state slab-full/EIO/`Next buffers not supplied on time`

**14 slab-full events during steady-state streaming** and **1 DMA underrun
at stream stop** disqualify. The receiver firmware unchanged from the
pre-handoff state fails the technical criteria even with the new DAC.

Per handoff technical-failure path:
1. First-error time: slab-full at 20.07s (7.2s into 30s stream)
2. Standalone I2S test comparison: the standalone tone test (STATUS.md)
   fed 2,016 blocks with zero EIO/underrun. Main receiver with BLE
   streaming fails where standalone passed. The queue-producer timing
   differs: standalone feeds blocks in a tight loop from system work queue;
   streaming feeds blocks from ISO receive callbacks at 100–200 Hz.
3. Root cause: PI clock recovery controller output is insufficient to
   match the relative clock drift between the ISO arrival rate and the
   I2S48K clock. The ±500 ppm output clamp, or the SAMPLE_ADJUST actuator's
   single-sample-per-10ms-block limit, prevents full compensation.
4. No narrow fix attempted — tuning the PI controller gains or output
   clamp is not narrow (affects both platforms, requires empirical tuning,
   invokes system behavior the handoff explicitly excludes: GRTC/ASRC
   tuning).

## Artifacts

| Path | Description |
|------|-------------|
| `/tmp/phase4a1-receiver-serial.log` | Full receiver serial output (206 lines) |
| `/tmp/phase4a1-btmon.log` | btmon capture (empty — permission denied) |
| `build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf` | Firmware binary |

## PENDING USER LISTENING

Technical criteria failed. Do not claim audible output. The receiver
synthesizes audio from decoded LC3 frames and feeds DMA blocks to I2S, but
14 slab-full drops mean ~14 audio frames are dropped over 30s (~0.5%
frame loss). Whether this is audible depends on the dropout pattern and DAC
behavior. **User must listen and report.**

## Next actions

1. **User listening test** — play audio through DAC, report audible quality.
2. If audible output is acceptable despite slab drops: proceed with Phase 4b
   (GRTC/DPPI drift measurement) per design.md.
3. If audible output is unacceptable: tune PI controller gains (widen output
   clamp, adjust phase/frequency PI terms) or increase I2S queue depth
   (BLOCK_COUNT). Both require per-platform empirical tuning.
4. Re-run with fx2lafw logic analyzer when hardware available.
