# Phase 4b.2 Review Fixes

Status: required after review of `e066ab6`

## Required fixes

1. **Correct anti-windup**
   - At positive output saturation, block phase integral only when
     `phase_inc > 0` (farther positive). Allow negative phase increments to
     unwind/move toward range even if one increment does not immediately leave
     clamp.
   - Mirror rule at negative saturation.
   - Rewrite tests that currently assert opposite-direction integral should be
     blocked. Test actual integral unwind through observable output after
     feedforward returns inside range.

2. **Synchronize controller cross-context state**
   - `audio_drift_frequency_error_update()` runs from system workqueue;
     `audio_drift_controller_update()` runs from Bluetooth/audio path; reset can
     race pending work.
   - Use Zephyr atomics or spinlock for feedforward/filter state and reset.
     No C data races. Keep controller call bounded and nonblocking.
   - Tests should cover update/reset/update consistency where possible.

3. **Correct occupancy terminology**
   - High `slab_free` = fewer queued blocks / queue draining → negative
     correction.
   - Low `slab_free` = more queued blocks / queue filling → positive correction.
   - Fix source/header/docs comments. Preserve implemented sign
     `PHASE_SETPOINT - slab_free`.

4. **Add bounded runtime actuator evidence**
   - In audio sink, count consumed insert/drop adjustments.
   - Log first adjustment and then no more often than every 500 total
     adjustments (or equivalent ~5 s cadence at measured offset).
   - Log cumulative inserts and drops so hardware validation proves +PCLK error
     produces inserts, not drops.
   - Reset counters per stream in sink stop/start lifecycle as appropriate;
     avoid log spam and do not alter output behavior.

5. **Complete docs/artifacts**
   - Add Phase 4b.1 results document with final passed evidence: 3,000/30 s,
     +1,665..+1,884 ppm, correct two-ASE gate, clean teardown/no warnings.
   - Track and mark
     `docs/development/phase4b2-pclk-feedforward-phase-pi-handoff.md`
     implemented.
   - Track this review handoff and mark implemented after verification.
   - STATUS must say Phase 4b.2 software complete, hardware pending; no premature
     Phase 4c claim.

## Verification

```bash
fw-build-5340
fw-build-54l15
west build -b native_sim tests/unit/drift -d /tmp/le-audio-drift-test --pristine
/tmp/le-audio-drift-test/drift/zephyr/zephyr.exe
west build -b native_sim tests/unit/actuator -d /tmp/le-audio-actuator-test --pristine
/tmp/le-audio-actuator-test/actuator/zephyr/zephyr.exe
git diff --check
git status --short
```

No hardware, RADIO, ASRC, push, merge, PR, amend, force, attribution. Fix every
warning, commit scoped files/handoffs, return exact results/hash.
