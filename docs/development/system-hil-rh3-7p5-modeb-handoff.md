# RH3-7p5 Stage 2 handoff: 7.5 ms Mode B diagnostic

Status: approved bounded diagnostic, Stage 2 of the RH3-7p5 phased
sequence (`docs/development/system-hil-rh3-7p5-mono-handoff.md`).
Stage 1 (mono) passed with near-perfect delivery
(`docs/development/system-hil-rh3-7p5-mono-result.md`). Stage 2 tests
the pinned throughput lesson at the tighter interval: Mode B encodes
TWO LC3 channels per SDU, the exact workload that starved the old 64 MHz
fixture at 10 ms (`sub=10907, skip=10185` baseline). At 7.5 ms the same
budget shrinks by 25% while the encode work stays constant; 128 MHz must
absorb it. This row is the direct discriminator between "encode budget
fits at 7.5 ms" and "interval-fundamental Mode B loss".

## Fixed identity

```text
run ID: rh3-7p5-modeb-20260911 (validate unused before invoking)
row:    rh3.fresh_mode_b_48_3_1
```

Receiver: normal current-HEAD build (expect FLPR `ACTIVE` with
`submit=0/success=0/fallback=0` - the documented 360-frame ASRC
fallback; validated by the runner's 48_3_1 branch). Source: current-HEAD
normal build, no fragment.

## Prediction (falsifiable)

Mode B at 7.5 ms encodes 2 x 90-octet channels into one 180-byte SDU
per 7500 us interval. At 128 MHz with encode-before-send-window
scheduling:

- PASS shape: source `sub=16859, sc=16000, sf=0, skip=0`, lead healthy
  (`under=0`), receiver `rx_valid >= 90%` of 16859, `plc <= 5%` of
  decoded; FLPR active counters zero (ASRC fallback).
- FAIL starvation shape: `skip` grows (missed events with zero send
  errors, the pinned 64 MHz signature) - fixture defect; re-check the
  encode budget before any other theory.
- FAIL clean-collapse shape: source fully healthy but receiver
  near-total loss - interval-fundamental on the air/controller side at
  Mode B's 180-byte SDU / nse selection; STOP for user review before
  any controller-config work (pinned discipline).

## Sequence

1. Preflight as usual (disk gate, HEAD + tree check, run-ID validation,
   fixture validate).
2. Source and receiver: builds from the current session are already
   proven (Stage 1 hashes; docs-only delta cannot change them). Verify
   `fw-build-hil-source` and `fw-build-54l15` outputs still hash to the
   Stage 1 values before invoking; rebuild only if the tree changed.
3. ONE runner run (outer timeout 3600000 ms):

   ```bash
   nix develop --command ./scripts/hil-runner.py run \
     --fixture tests/hil/fixture.json \
     --binding tests/hil/fixture.local.json \
     --output-root /tmp/opencode/hil-runs \
     --run-id rh3-7p5-modeb-20260911 \
     --junit /tmp/opencode/hil-runs/rh3-7p5-modeb-20260911.junit.xml \
     --row rh3.fresh_mode_b_48_3_1
   ```

4. Extract source status fields and receiver summary; classify per the
   prediction table; write the result doc; update resume-state; commit.

## Result documentation

`docs/development/system-hil-rh3-7p5-modeb-result.md`: prediction vs
outcome, build proof, source status fields, receiver summary with the
ASRC-fallback expectation evaluated, QoS/ISO tail CIG layout at Mode B
7.5 ms, integrity, raw identity, restoration, stop point.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, global `__ASSERT()`, receiver watchdog
empty-library). No NCS patches. No receiver firmware changes. No
rows/limits/matrix changes. No evidence mutation. Single run; no retry.
Status 0/1/130 immutable. Preserve the Stage 1 evidence root and all
prior roots.