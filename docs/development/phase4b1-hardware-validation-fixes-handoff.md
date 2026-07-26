# Phase 4b.1 Hardware Validation Fixes

Status: required after 2026-07-26 autonomous hardware run

## Evidence

- nRF54L15 probe: serial `8EE9B3FF`, DPIDR `0x6ba02477`, PART
  `0x00054b15`, variant `AAC0`.
- Boot clean; `Audio timing: GRTC+TIMER20+GPPI ready`.
- Autonomous nRF5340DK `hci_uart` central streamed 3,000 frames in 30.00 s.
- Timing output was consistently only 100–101 events/s:
  - `LRCK diag[5]: 100 frames in 1000000 us`
  - `LRCK diag[20]: 101 frames in 1000000 us`
- Therefore nRF54L15 I2S `FRAMESTART` fires at DMA audio-buffer boundaries in
  this configuration (~100 Hz), not every physical LRCK edge (~47,619 Hz).
  Counting FRAMESTART cannot measure sample-clock frequency.
- Stream teardown emitted:
  - `<wrn> bt_ascs: No callback for disabled set`
  - `<err> i2s_nrfx: Next buffers not supplied on time`

## Goal

Replace invalid FRAMESTART edge counting with measurement of TIMER20's PCLK-
derived free-running timer against absolute GRTC/controller-time compares. Fix
teardown warnings. Repeat builds/tests only; hardware rerun follows review.

## Timing design

1. TIMER20 and I2S20 are in local HF/PCLK clock domain. Configure TIMER20 in
   TIMER mode, 32-bit width, prescaler 0, then clear/start it.
2. Determine TIMER20 nominal base frequency with verified HAL macro
   `NRF_TIMER_BASE_FREQUENCY_GET(timer_reg)`; use compile-time/runtime-safe value,
   not a guessed literal. For this instance installed HAL falls back to 16 MHz.
3. Keep GRTC absolute one-second compares anchored to first validated
   `info->ts + qos->pd` presentation target.
4. Keep GPPI route `GRTC COMPARE -> TIMER20 CAPTURE[0]`. Remove
   `I2S20 FRAMESTART -> TIMER20 COUNT`, its resource handle, setup, cleanup, and
   all claims that FRAMESTART counts LRCK.
5. Compute unsigned TIMER capture delta over exact GRTC elapsed time. Compare
   against `timer_nominal_hz * elapsed_us / 1e6`; logged ppm is local HF/PCLK
   frequency error relative to controller/GRTC time. Since I2S derives from
   `PCLK32M`, this ppm is clock-source drift input for Phase 4b.2.
6. Diagnostic text must say PCLK timer ticks, not LRCK frames. Log actual
   nominal timer frequency and measured tick delta.
7. Keep existing timestamp validation, arithmetic order, ISR/work safety, one-
   second interval, bounded logs, and no PI integration.

## Teardown fixes

1. Add `bt_bap_stream_ops.disabled` callback so Zephyr no longer warns
   `No callback for disabled set`.
2. Stop audio sink immediately on first disabled stream. Mode A cannot produce
   stereo after either ASE disables, and waiting for ACL disconnect leaves I2S
   draining without buffers. `audio_sink_stop()` is idempotent, so later disable
   or disconnect may call it again.
3. Preserve existing ASCS server `lc3_disable` callback; stream lifecycle
   callback is separate.
4. Verify normal teardown produces no `i2s_nrfx: Next buffers not supplied on
   time`, no `I2S underrun`, and no ASCS missing-callback warning on next hardware
   run.

## Tests/docs

- Update timing math tests from LRCK-frame examples to generic timer-tick ppm
  examples, including 16,000,000 exact/+1/-1 and wrap delta.
- Update `docs/design.md`, `STATUS.md`, `AGENTS.md`, original Phase 4b.1 handoff,
  and comments: FRAMESTART evidence invalidated old assumption; production path
  is PCLK-derived TIMER captured at GRTC presentation references.
- Record hardware evidence above. Do not claim Phase 4b.1 hardware pass until
  rerun.
- Mark this handoff implemented after software verification.

## Verification

```bash
fw-build-5340
fw-build-54l15
west build -b native_sim tests/unit/timing -d /tmp/le-audio-timing-test --pristine
/tmp/le-audio-timing-test/timing/zephyr/zephyr.exe
west build -b native_sim tests/unit/drift -d /tmp/le-audio-drift-test --pristine
/tmp/le-audio-drift-test/drift/zephyr/zephyr.exe
git diff --check
git status --short
```

Fix every warning. No hardware, PI integration, RADIO access, ASRC work, push,
merge, PR, amend, force operation, or attribution. Commit scoped fixes and
return exact results/hash.

## Implementation status

**Implemented 2026-07-26.** All software changes landed:

1. `src/audio_timing_nrf54.c`: TIMER mode (not COUNTER), removed FRAMESTART→COUNT
   GPPI, uses `NRF_TIMER_BASE_FREQUENCY_GET`, PCLK tick diagnostics.
2. `src/audio_timing.h`: Comments updated (LRCK → PCLK timer).
3. `src/bt_bap.c`: `disabled` stream callback added, stops audio sink immediately.
4. `tests/unit/timing/src/test_timing.c`: LRCK-frame examples → timer-tick ppm
   (16,000,000 Hz exact/+1/-1/wrap delta).
5. `docs/design.md`, `STATUS.md`, `AGENTS.md`: FRAMESTART evidence recorded;
   PCLK timer path documented.
6. Both builds + all unit tests pass. Hardware rerun pending — do NOT claim
   Phase 4b.1 hardware pass.
