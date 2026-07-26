# Phase 5.0 — audio performance instrumentation handoff

## Goal

Add low-overhead, resettable cycle and queue instrumentation needed to measure
current SAMPLE_ADJUST baseline and later ASRC cost. Preserve audio behavior.

## In scope

- Add unit-testable performance accumulator module, likely
  `src/audio_perf.{c,h}`.
- Measure LC3 decode, volume, sink/render/queue path, and complete ISO receive
  callback using Zephyr cycle APIs. Keep instrumentation Kconfig-gated.
- Track count, total cycles, maximum cycles, and deadline overruns. Avoid
  division in per-frame path.
- Track audio-path min/max slab-free, output frames, packet-repeat fallback,
  sink push failures, and capacity/render failures where current code exposes
  them. Do not rename established fault counters without migration.
- Expose bounded snapshots through existing `audio status` or new `audio perf`
  shell command. Include cycle-to-microsecond conversion and percentage of
  10 ms deadline at print time.
- Reset stats at stream/session boundary or explicit shell reset without races.
- Enable measurement config for nRF54L15 baseline only if overhead is bounded;
  keep nRF5340 production default unchanged unless shared code requires it.
- Add native unit tests for accumulator update, min/max, overflow-safe totals,
  deadline count, reset, and snapshot consistency.
- Update current docs/status with exact interface and verification results.
- Commit handoff with implementation as audit record.

## Out of scope

- No ASRC implementation or actuator change.
- No FLPR, BabbleSim, I2S format/rate, Bluetooth behavior, or controller tuning.
- No direct RADIO access.
- No package installation, push, PR, release, or destructive recovery.

## Invariants

- nRF5340 APLL path unchanged.
- nRF54L15 SAMPLE_ADJUST behavior unchanged.
- No heap and no floating point in firmware data path.
- Instrumentation cannot log per frame or add unbounded output.
- Counter snapshots must be race-safe for callback/shell contexts.
- Existing warning policy applies.
- `audio_sink_push()` return handling should become observable; fix ignored
  return-value accounting without changing normal flow.

## Verification

- New native unit suite passes by executing test binary.
- Existing drift, actuator, timing, lifecycle, decode, rate-convert suites pass.
- `fw-build-5340` and `fw-build-54l15` pass with no new warnings.
- Inspect generated configs to prove intended instrumentation enablement.
- If hardware is available, flash nRF54L15, capture console before reset, run
  central-only Mode A and Mode B baseline streams long enough to produce stable
  timing snapshots, and record results under `docs/development/`. Do not claim
  physical audio quality.

## Return contract

Inspect status/diff/log, stage only scoped files, commit with no attribution,
and return files, API/behavior, exact tests/build/hardware commands and results,
commit hash/message, warnings, blockers, and deviations. Do not push.
