# Phase 4b.1 — Results

**Status: PASS**

## Autonomous hardware run

Final Phase 4b.1 autonomous run (2026-07-26):

- **3,000 stereo Mode A frames / 30.00 s** — central transmitted 3,000
  LC3-encoded stereo audio frames over 30 seconds. Receiver decoded and
  rendered them without interruption. Central log:
  `Done: 3000 frames in 30.00 s (100.0 fps)`.
- **PCLK TIMER20 measurement: +1,665 to +1,884 ppm vs GRTC** — TIMER20
  free-running (PCLK-derived, nominal 16 MHz) hardware-snapshotted by
  GRTC compare → GPPI → TIMER20 CAPTURE at 1 s intervals. Measured local
  PCLK/I2S clock runs approximately +1,665 to +1,884 ppm faster than
  controller/GRTC time.
- **Two-ASE gate opens correctly** — both sink ASEs (Front Left, Front
  Right, Mode A stereo) stream state reached STREAMING; `BT_ISO_FLAGS_VALID`
  and `BT_ISO_FLAGS_TS` set on received SDUs; `info->ts` consumed for GRTC
  presentation scheduling.
- **Clean first-disable teardown** — no warning, no underrun, no
  post-disable restart. `audio_sink_stop()` ran PREPARE → DROP cleanly;
  drift controller and timing state reset correctly.
- **Diagnostic-only logging** — firmware logged PCLK timer diagnostics
  (every 1 s measurement captured, with sparse diagnostic print every
  5th measurement at indices 1, 5, 10, 15, 20, 25). Phase 4b.1 did not
  feed `audio_drift_frequency_error_update()`; that API became wired in
  Phase 4b.2.

## Evidence

Hardware run logs from serial-mcp capture on `/dev/ttyACM0` @ 115200
(nRF54L15 Seeed Xiao, receiver firmware):

Init:
- `Audio timing: GRTC+TIMER20+GPPI ready (timer 16000000 Hz)`

Timing anchor:
- `Timing anchor: ts=158148850 pd=40000 anchor_grtc=17338058034 first_cmp=17339058034`

PCLK timer diagnostics (sparse, every 5th measurement):
- `PCLK timer diag[1]: 16028227 ticks in 1000000 us ... → 1764 ppm`
- `PCLK timer diag[5]: 16028409 ticks in 1000000 us ... → 1775 ppm`
- `PCLK timer diag[10]: 16028696 ticks in 1000000 us ... → 1793 ppm`
- `PCLK timer diag[15]: 16026646 ticks in 1000000 us ... → 1665 ppm`
- `PCLK timer diag[20]: 16030149 ticks in 1000000 us ... → 1884 ppm`
- `PCLK timer diag[25]: 16028436 ticks in 1000000 us ... → 1777 ppm`

Lifecycle:
- `Audio path gate OPEN ...`
- `Audio path gate CLOSED ...`

No teardown warning or error observed.

No RADIO register, event, IRQ, or DPPI access — compliant with SDC/MPSL
ownership. FRAMESTART counter invalidation (fires at DMA buffer boundaries
~100 Hz, not LRCK edges ~47,619 Hz) confirmed in HW validation; the
production path uses free-running TIMER mode.

## What this unlocks

Phase 4b.2: these measured ppm values became the feedforward input to the
PI clock recovery controller (`audio_drift_frequency_error_update()`) which
combines PCLK feedforward with buffer-phase PI and drives the SAMPLE_ADJUST
actuator. Hardware direction proven: `+1,665..+1,884 ppm` local fast → negative
controller correction → eventual sample-insert events.
