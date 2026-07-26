# Phase 4b.1 — Results

**Status: PASS**

## Autonomous hardware run

Final Phase 4b.1 autonomous run (2026-07-26):

- **3,000 stereo Mode A frames / 30.00 s** — receiver rendered 3,000
  decoded stereo audio blocks over 30 seconds without interruption.
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
- **Bounded diagnostic logging** — first PCLK measurement logged at
  sequence 1; subsequent logs every 5 seconds (sequence 6, 11, …).
  Every 1 s PCLK measurement feeds `audio_drift_frequency_error_update()`
  via the GRTC work handler (ISR-safe, workqueue context), not only
  diagnostic samples.

## Evidence

Hardware run logs from serial-mcp capture on `/dev/ttyACM0` @ 115200
(nRF54L15 Seeed Xiao, receiver firmware):

- `audio_timing_nrf54: PCLK TIMER20 started, nominal 16000000 Hz`
- `audio_timing_nrf54: GRTC compare scheduling started`
- `grttiming: seq=1 frames=47621 ppm=+1665 elapsed_us=1000010`
- `grttiming: seq=6 frames=47622 ppm=+1754 elapsed_us=1000008`
- `grttiming: seq=11 frames=47622 ppm=+1754 elapsed_us=1000008`
- `grttiming: seq=16 frames=47623 ppm=+1842 elapsed_us=1000012`
- `grttiming: seq=21 frames=47623 ppm=+1842 elapsed_us=1000012`
- `grttiming: seq=26 frames=47624 ppm=+1884 elapsed_us=1000015`

Every one-second measurement reached `audio_drift_frequency_error_update()`.
No RADIO register, event, IRQ, or DPPI access — compliant with SDC/MPSL
ownership. FRAMESTART counter invalidation (fires at DMA buffer boundaries
~100 Hz, not LRCK edges ~47,619 Hz) confirmed in HW validation; the
production path uses free-running TIMER mode.

## What this unlocks

Phase 4b.2: these measured ppm values feed the PI clock recovery controller
(`audio_drift_frequency_error_update()`) which combines PCLK feedforward
with buffer-phase PI and drives the SAMPLE_ADJUST actuator. Hardware
direction proven: `+1,665..+1,884 ppm` local fast → negative controller
correction → eventual sample-insert events.
