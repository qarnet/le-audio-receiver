# Phase 4b.1 Teardown Race Fix

Status: required after PCLK timing hardware rerun

## Evidence

- PCLK measurement now valid: 16,026,090–16,029,103 TIMER ticks per exact GRTC
  second, about +1,630 to +1,818 ppm against 16 MHz nominal.
- Autonomous central completed 3,000 frames in 30.00 s.
- Missing ASCS disabled-callback warning is fixed.
- Teardown still fails: first ASE disabled stops sink, but second ASE continues
  RX briefly; stale L/R state causes `I2S DMA started`, followed by
  `i2s_nrfx: Next buffers not supplied on time`.

## Required fix

1. Add explicit stream lifecycle state per sink (`started`) and one global
   audio-path gate.
2. In stream-started callback, mark sink started. Enable audio path only when:
   - Mode B/single ASE: that ASE is started; or
   - Mode A/two ASEs: both configured ASEs are started.
3. In first stream-disabled/stopped/released transition, close global audio-path
   gate **before** calling `audio_sink_stop()`. Clear `l_received` and
   `r_received` at same transition so stale channel halves cannot pair.
4. In `stream_recv`, keep any required counters/diagnostics, but do not decode,
   interleave, push, or update audio timing/drift after audio-path gate closes.
   No late callback may restart I2S.
5. Reset all lifecycle flags on disconnect/release/reconfiguration. Reconnect
   must still work without re-running `audio_sink_init()`.
6. Keep `audio_sink_stop()` idempotent. Remove redundant direct
   `audio_timing_reset()` call from disabled callback because sink stop already
   resets timing.
7. Add unit-testable lifecycle helper/state logic if practical. At minimum add
   assertions or focused tests for one-ASE and two-ASE start/disable decisions;
   do not copy production logic into tests.
8. Update STATUS/design/handoff results with PCLK measurement evidence and note
   teardown rerun pending. Do not claim full pass yet.

## Verification

```bash
fw-build-5340
fw-build-54l15
west build -b native_sim tests/unit/timing -d /tmp/le-audio-timing-test --pristine
/tmp/le-audio-timing-test/timing/zephyr/zephyr.exe
git diff --check
git status --short
```

Fix every warning. No hardware in Executor handoff, PI integration, RADIO,
ASRC, push, merge, PR, amend, force, or attribution. Commit scoped fix and
return exact results/hash. Orchestrator performs final hardware teardown rerun.
