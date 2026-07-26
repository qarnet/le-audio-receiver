# Phase 4c External I2S Analyzer Results

Date: 2026-07-26
Status: digital PASS — audibility UNAVAILABLE

## Test configuration

- Target: nRF54L15 (Seeed Xiao), ncs v3.3.0, SDC on cpuapp
- Actuator: SAMPLE_ADJUST (sample insert/drop)
- Controller: PCLK feedforward + phase PI (Phase 4b.2)
- Central: nRF5340DK hci_uart, bap_central.py
- Stream: 48 kHz LC3 stereo Mode A, 100 fps, 30 seconds
- Analyzer: fx2lafw, 24 MHz sample rate, D0=BCK, D1=LRCK, D2=SDOUT, D3=3V3

## Exact evidence

- central: `Done: 3000 frames in 30.00 s (100.0 fps)`
- analyzer connection: `conn=5.22`
- captured samples: 11,766,272
- capture duration: 0.490261333 s
- BCK D0: 747,961 rising edges, 1,525,637.347 Hz
- LRCK D1: 23,374 rising edges, 47,676.613 Hz
- BCK/LRCK ratio: 31.999701 (expected 32 for 16-bit stereo I2S)
- SDOUT D2: 324,633 transitions, high duty 0.493837 — nonconstant audio data
- D3 reference: continuously high (3V3)

## Gate status

| Gate | Result |
|---|---|
| BCK frequency (~1.526 MHz) | PASS |
| LRCK frequency (~47,677 Hz) | PASS |
| BCK/LRCK ratio (31.999701 ≈ 32) | PASS |
| SDOUT activity (nonconstant audio data) | PASS |
| External digital I2S at DAC pins | PASS |
| Audible quality | UNAVAILABLE (user did not provide listening report) |

## Notes

- External logic analyzer confirms valid I2S waveforms at the DAC pins during
  active BAP streaming. Clock frequencies match PCLK32M-derived I2S20 output.
- BCK/LRCK ratio of 31.999701 is within measurement tolerance of the expected
  32:1 ratio for 16-bit stereo I2S.
- Raw capture file lives outside repo at `/tmp/opencode/phase4c-i2s.sr`; not
  committed.
- Physical audibility marked UNAVAILABLE by user, not failed and not blocking
  further measurable work. Analog output quality is not claimed.
- Phase 5 quality ASRC cannot be justified by listening evidence; left
  conditional/deferred unless another measurable quality criterion is chosen.
  No decoder waveform evidence beyond edge/activity metrics was collected.
