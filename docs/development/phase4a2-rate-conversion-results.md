# Phase 4a.2 — Rate Conversion Results

Date: 2026-07-26

## Result: PASS

## Root cause confirmed

nRF54L15 I2S20 with `clock-source = "PCLK32M"` produces approximately
47,619 Hz LRCK. The receiver decoded 480 stereo frames every 10 ms (48 kHz LC3)
but always wrote fixed 1,920-byte (480-frame) blocks via I2S. Queue filled at
~100 blocks/s while drain ran at ~99.2 blocks/s — observed slab-full every
~1.57 s in Phase 4a.1.

Fix: bounded nearest-neighbor, variable-output-frame conversion maps each
nominal 480-input-frame block to a 476/477-output-frame sequence averaging 47,619
output frames per 100 input blocks. A remainder accumulator ensures exact total
output over many calls.

## Unit tests — PASS

```
tests/unit/rate_convert — native_sim
SUITE PASS - 100.00% [rate_convert]: pass = 10, fail = 0, skip = 0
```

Covered:
- Identity: 100 × 480 at 48k→48k yields 48,000 total, each block 480
- nRF54L15 baseline: 100 × 480 at 48k→47,619 yields exactly 47,619 total, each
  block 476 or 477
- Nearest-neighbor preserves L/R pairing and endpoints
- Remainder determinism and re-init
- Empty/edge-case no-ops

## Build — PASS

| Target | Result | Flash | RAM |
|--------|--------|-------|-----|
| nRF54L15 | PASS (0 warnings) | 447,420 B / 1,420 KB (30.77%) | 135,204 B / 188 KB (70.23%) |
| nRF5340 | PASS (0 warnings) | 361,348 B / 1,008 KB (35.01%) | 136,448 B / 448 KB (29.74%) |

## Flash — PASS

nRF54L15 flashed via OpenOCD (CMSIS-DAP `8EE9B3FF`), verified 447,408 bytes.

## Stream retest — PASS

**Test**: Mode A stereo (2 ASEs), nRF5340DK `hci_uart` central via
`scripts/bap_central.py --duration 35`, 3,500 frames at 100 fps.

**Boot log** (complete stream setup):
```
[00:36:35] <inf> bt_bap: ASE Config → ASE[0] (L) + ASE[1] (R)
[00:36:45] <inf> bt_bap: LC3 decoder: 48000 Hz 10000 us ch=1 (both ASEs)
[00:36:48] <inf> bt_bap: Stream[1] started: CIG 0 CIS 1
[00:36:48] <inf> bt_bap: Stream[0] started: CIG 0 CIS 0
[00:36:48] <inf> audio_i2s: I2S DMA started
[00:36:48] <inf> bt_bap: push_stereo: n=480 push_ret=0 ← first push OK
```

**Stream evidence** (35-second run):
- `push_stereo: push_ret=0` consistently — no push errors
- Zero `slab full` messages — no I2S slab-full drops
- Zero `underrun` / `EIO` messages — no DMA underruns
- `stream_recv tally`: valid count climbed from 50→191+ in partial log capture
  (ring buffer truncated at 32 KiB)
- Drift state advancing (PID controller running)
- Clean disconnect: `Stream[1] stopped: reason 0x13`, `Disconnected: reason 0x13`

**Comparison to Phase 4a.1**:
| Metric | Phase 4a.1 (before fix) | Phase 4a.2 (after fix) |
|--------|------------------------|------------------------|
| Slab-full drops | 14× in 30 s (~every 1.57 s) | **0** in 35 s |
| DMA underruns | 1× (at stream stop) | **0** |
| Root cause | PI insufficient | PCLK32M rate mismatch + fixed writes |

## What changed

| File | Change |
|------|--------|
| `src/audio_rate_convert.h` | New pure helper API (struct, init, next_frames, nearest_stereo) |
| `src/audio_rate_convert.c` | New nearest-neighbor resampler with remainder accumulator |
| `src/audio_i2s.c` | Replace fixed 1920-byte writes with variable output frames via rate converter |
| `Kconfig` | New `CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ` (default 48000) |
| `boards/nrf54l15dk_nrf54l15_cpuapp.conf` | Set `CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ=47619` |
| `CMakeLists.txt` | Add `audio_rate_convert.c` to build |
| `tests/unit/rate_convert/` | New 10-test unit suite (native_sim) |

## Residual requirements

- **Phase 4b** — required for peer-drift correction. The rate converter
  handles the fixed PCLK32M hardware-rate mismatch. Residual drift between
  the BLE controller clock and the PCLK32M-derived I2S clock still needs
  the supported ISO-timestamp/GRTC presentation-reference path (see
  `docs/design.md` §Phase 4b).
- **Phase 5 ASRC quality** — the nearest-neighbor conversion removes
  ~381 output frames/s at nominal 47,619-vs-48,000 mismatch — artifact
  audibility remains unmeasured. Phase 5 quality work is now planned
  implementation work *(superseded 2026-07-27 per `docs/design.md`)*.
  A proper ASRC (linear/cubic interpolation) would be needed
  for production quality.
- **User listening** — not yet performed. DAC wired and functional; audible
  confirmation pending.
- **fx2lafw logic analyzer** — not available. No frequency-domain measurement
  of LRCK/RX rate. LRCK measured empirically at ~47,619 Hz from standalone
  GPIO pattern test.

*(Supersession 2026-07-27: Phase 5 ASRC is now planned implementation work
per `docs/design.md` revision — no longer conditional on listening evidence.
The engineering risk of ~86 discontinuous samples/s is sufficient rationale.)*

## Math

- Input: 48,000 Hz × 480 frames/block ÷ 100 blocks/s = 48,000 frames/s
- Output: 47,619 Hz drain
- 100 × 480 = 48,000 input → 48,000 × 47,619 / 48,000 = 47,619 output
- Ratio: 476.19 output per 480 input → 81% at 476, 19% at 477
- Slab required: max 481 frames × 2 × 2 = 1,924 bytes (was 1,920)
