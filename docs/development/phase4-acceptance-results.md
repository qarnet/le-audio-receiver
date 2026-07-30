# Phase 4 — nRF54L15 Audio Bring-Up: Consolidated Acceptance Results

Date: 2026-07-26 (consolidated 2026-07-30)
Status: **PASS** — all sub-gates green; audibility UNAVAILABLE

---

## Hardware characterization

### GPIO pin map
- D0/P1.4 = BCK, D1/P1.5 = LRCK, D2/P1.6 = SDOUT
- I2S20 on D0/D1/D2 — confirmed by register state (PSEL, FRAMESTART)
- P1.8/P1.9 = UART20 to SAMD11 USB bridge (untouched)

### Clock sources
- PCLK32M source works; `PCLK32M_HFXO` UsageFault tracked separately
- Actual PCLK produces ~47,619 Hz LRCK vs nominal 48,000 Hz
- HFINT-grade accuracy (~±250 ppm) — fine for ASRC; no external MCK

### Standalone I2S20 DMA test
- 20.001 s, 2,016 blocks fed, zero EIO/underrun
- ENABLE=1, TASKS_START triggered, PSEL correct, FRAMESTART firing
- I2S20 hardware confirmed working

### Old DAC failure evidence
- Old DAC breakout with MUTE low: D1/LRCK held high (no toggling)
- Digital wires removed: D1/LRCK toggles normally
- Conclusion: breakout/wiring incompatible or defective; replaced

---

## BLE ISO delivery

- nRF5340DK `hci_uart` central: 3,000 ISO Data TX over 15 s
- Two CISes (Mode A stereo), 48 kHz LC3, 100 fps, zero flow-control stalls
- BlueZ connect + JustWorks pairing + BAP negotiation all work
- Clean ACL teardown; three consecutive runs, no zombie slots

---

## Sub-gate results

### 4a — First end-to-end
- I2S20 pinctrl fix landed; SDC buffer counts matched
- Boot + PACS/ASCS + BlueZ bonding verified

### 4a.1 — New-DAC main pipeline retest
- Technical stability gate: 60,000 frames / 600 s, zero faults
- Clean teardown, no slab-full/underrun/warning/error

### 4a.2 — Rate conversion
- Root cause: PCLK32M produces ~47,619 Hz LRCK; decoder output fixed 48,000 Hz
- Fix: bounded nearest-neighbor rate converter (480→476/477 frames/block)
- 10/10 unit tests (native_sim), both builds pass
- 35 s Mode A: zero slab-full, zero underruns
- Residual: ~381 frames/s removed at nominal mismatch

### 4b.1 — GRTC timing foundation
- TIMER20 in TIMER mode, GRTC 1 s capture via GPPI → TASKS_CAPTURE
- PCLK diagnostics logged: +1,665..+1,884 ppm (HW measurement)
- 3,000 frames / 30 s stream, zero faults
- I2S20 FRAMESTART INVALIDATED: fires at DMA boundaries (~100 Hz), not LRCK edges

### 4b.2 — PCLK feedforward + phase PI
- `audio_drift_frequency_error_update()` fed from GRTC ISR
- `audio_drift_controller_update(slab_free)` — per-block PI
- 4,500 frames / 45 s at 100 fps
- PCLK diagnostics +1,500..+1,757 ppm
- Insert-to-drop ratio 186:1, channel-pair gate correct
- 16 drops before first PCLK measurement, inserts only thereafter
- Clean teardown, no slab-full/I2S underrun/warning/fault

### 4c — Stability gate
- 10-minute uninterrupted stream: 60,000 frames / 600 s, 100.0 fps
- No disconnect, no slab exhaustion, no underrun storms
- PCLK diagnostics: ~+1,523 to +2,058 ppm
- SAMPLE_ADJUST: ~86 inserts/s (51,487 inserts / 13 drops over 10 min)
  — expected for HFINT/PCLK offset at the time; not non-convergence

### 4c — External I2S analyzer
- fx2lafw logic analyzer at DAC pins, 30 s Mode A stream
- BCK: 1,525,637 Hz, LRCK: 47,677 Hz
- BCK/LRCK ratio: 31.999701 (expected 32)
- SDOUT: 324,633 transitions, high duty 0.494 → nonconstant activity
- Digital I2S gate: **PASS**

---

## Audibility

Physical audibility UNAVAILABLE by user — not failed, not blocking.
Analog audio quality is not claimed. Phase 4 closure is on measurable
technical gates.

---

## Supporting detailed evidence retained

- `phase4-d1-register-state-results.md`
- `phase4-i2s-gpio-pattern-results.md`
- `phase4a2-rate-conversion-results.md`
- `phase4b2-results.md`
- `phase4c-i2s-analyzer-results.md`
- `phase4c-technical-results.md`

Superseded intermediate standalone-DAC, Phase 4a/4a1, and Phase 4b1 reports
were consolidated here and removed after final acceptance.
