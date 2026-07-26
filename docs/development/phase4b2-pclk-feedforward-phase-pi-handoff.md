# Phase 4b.2 — PCLK Feedforward + Buffer-Phase PI

Status: implemented (commit e066ab6; review fixes in phase4b2-review-fixes)

## Goal

Feed validated nRF54L15 PCLK-vs-GRTC measurement into SAMPLE_ADJUST with correct
sign/range, while applying buffer-phase PI exactly once per rendered audio block.
Preserve nRF5340 behavior and buildability.

## Hardware basis

Final Phase 4b.1 autonomous run passed:

- 3,000 stereo Mode A frames / 30.00 s.
- PCLK TIMER20 measurement about +1,665 to +1,884 ppm vs GRTC.
- Both ASE gate opens correctly; clean first-disable teardown; no warning,
  underrun, or post-disable restart.
- Positive measured ppm means local PCLK/I2S runs faster than controller time.
  Required sample-adjust correction is therefore negative (insert samples / make
  each rendered source block occupy more local output frames).

## Current defects to remove

1. `audio_drift_controller_update(info->ts, slab_free)` derives frequency from
   ISO timestamp intervals. Both timestamps live on controller timeline; this
   does not measure local audio-clock frequency.
2. `audio_sink_sdu_ref_update()` is called for both Mode A ASE callbacks and
   applies ppm twice per one stereo output block.
3. Buffer phase sign is reversed. Higher slab-free count means fewer queued DMA
   blocks / queue draining, requiring negative correction. Correct phase error
   is `setpoint - slab_free`, not `slab_free - setpoint`.
4. ±500 ppm cannot correct measured ~+1,775 ppm nRF54 PCLK error. SAMPLE_ADJUST
   capacity is one sample per 480-frame block, about ±2,083 ppm.

## API and controller refactor

Replace timestamp-driven frequency estimation with two explicit inputs:

```c
/* local_clock_error_ppm: positive means local PCLK/audio clock is fast. */
void audio_drift_frequency_error_update(int32_t local_clock_error_ppm);

/* Called once per rendered stereo block. Returns actuator correction:
 * positive = consume source faster/drop one eventually;
 * negative = consume source slower/insert one eventually.
 */
int32_t audio_drift_controller_update(int slab_free_count);
```

- Remove SDU timestamp parameter and timestamp-window state from controller.
- Frequency feedforward correction = `-local_clock_error_ppm`.
- Filter measurement enough to reject one-second jitter without hiding fixed
  offset. Use small integer IIR or bounded moving average; document exact rule
  and test convergence/step response. No floating point required for filter.
- Phase error = `PHASE_SETPOINT - slab_free_count`.
- Keep phase PI only; combine:
  `output = filtered_frequency_correction + phase_PI`.
- Add anti-windup: phase integrator must not continue winding farther into an
  already saturated output. Test both signs.
- Make output clamp Kconfig-backed:
  - default/nRF5340: 500 ppm;
  - nRF54L15 board config: 2000 ppm (below one-adjustment-per-block capacity).
- Make phase-integral clamp Kconfig-backed or otherwise constrain nRF54 phase
  authority so it cannot consume all headroom around measured ~1,775 ppm.
  Recommended nRF54 phase authority/integral clamp: 150–200 ppm. Default may
  retain existing nRF5340 behavior.
- Keep `audio_drift_reset()`, state string, and `audio_drift_get_ppm()`; reset
  filter/feedforward/PI state.

## Data-path wiring

### nRF54 timing

- In deferred timing work, after valid PCLK ppm computation, call
  `audio_drift_frequency_error_update(measured_local_ppm)` on every measurement,
  not only when diagnostic logging is due.
- Current ISR publishes diagnostics only at sequence 1/every 5 seconds. Refactor
  so every one-second measurement reaches controller while bounded log cadence
  stays unchanged.
- Never call drift controller from ISR. Work/thread context only.
- Ignore stale generation/inactive-session measurements exactly as now.

### Audio sink

- Remove `audio_sink_sdu_ref_update()` from `audio_sink.h`, implementations, BAP
  callbacks, and test stubs. ISO timestamp remains consumed only by
  `audio_timing_sdu_ref_update()` after VALID+TS checks.
- In `audio_sink_push()`, once per rendered stereo block:
  1. read slab free count;
  2. call `audio_drift_controller_update(slab_free)`;
  3. call `audio_clock_actuator_apply_ppm(ppm)` once;
  4. consume at most one staged sample adjustment;
  5. derive rate-converted output frame count.
- Choose and document whether update occurs before slab allocation; use same
  point consistently so setpoint meaning stays stable.
- No actuator update on closed lifecycle gate because no `audio_sink_push()`.

### Actuator semantics

- Preserve convention already encoded by SAMPLE_ADJUST:
  - positive correction eventually returns `+1` → drop one output frame;
  - negative correction returns `-1` → insert one output frame.
- Add integration tests proving measured local `+1775 ppm` produces negative
  controller correction and eventual insert events, not drops.
- Test one actuator apply per stereo output block in Mode A logic where practical.

## nRF5340 constraint

nRF5340 lacks Phase 4b PCLK measurement. Its timing implementation remains
no-op and feedforward stays zero; buffer-phase PI drives APLL. Correct phase sign
must be regression-tested and both firmware targets must build clean. Do not
change APLL register conversion.

## Tests

Update `tests/unit/drift` to production API and cover:

1. reset/INIT behavior;
2. +1775 local error → filtered correction negative;
3. -local error → positive correction;
4. filter convergence and bounded step response;
5. high slab-free/draining → negative phase correction;
6. low slab-free/filling → positive phase correction;
7. combined frequency + phase output;
8. configurable clamp behavior;
9. anti-windup at both clamps;
10. zero feedforward supports nRF5340 phase-only operation.

Update actuator tests/integration tests for sign and per-block application.
Update BSim stub API after removing SDU-ref update.

## Documentation

- Record final Phase 4b.1 hardware PASS in `STATUS.md`, `docs/design.md`, and a
  results document under `docs/development/` with exact timing range and clean
  teardown evidence.
- Update architecture/API descriptions for explicit PCLK feedforward + phase PI.
- Remove stale claims that controller frequency comes from ISO timestamp deltas.
- Update AGENTS.md gotcha with output sign/range and once-per-output-block rule.
- Mark this handoff implemented only after software verification.

## Verification

```bash
fw-build-5340
fw-build-54l15
west build -b native_sim tests/unit/drift -d /tmp/le-audio-drift-test --pristine
/tmp/le-audio-drift-test/drift/zephyr/zephyr.exe
west build -b native_sim tests/unit/actuator -d /tmp/le-audio-actuator-test --pristine
/tmp/le-audio-actuator-test/actuator/zephyr/zephyr.exe
west build -b native_sim tests/unit/timing -d /tmp/le-audio-timing-test --pristine
/tmp/le-audio-timing-test/timing/zephyr/zephyr.exe
west build -b native_sim tests/unit/lifecycle -d /tmp/le-audio-lifecycle-test --pristine
/tmp/le-audio-lifecycle-test/lifecycle/zephyr/zephyr.exe
git diff --check
git status --short
```

Fix every warning. No hardware in Executor handoff. No direct RADIO access,
ASRC/Phase 5, push, merge, PR, amend, force, or attribution. Commit scoped work
and return exact files/results/hash/deviations. Orchestrator runs hardware gate.
