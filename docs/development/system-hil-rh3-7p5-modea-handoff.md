# RH3-7p5 Stage 3 handoff: 7.5 ms Mode A shared-grid diagnostic

Status: approved bounded diagnostic, Stage 3 (final) of the RH3-7p5
phased sequence. Stage 1 (mono) and Stage 2 (Mode B) both PASSED
(`docs/development/system-hil-rh3-7p5-mono-result.md`,
`docs/development/system-hil-rh3-7p5-modeb-result.md`). Stage 3 tests
the remaining 7.5 ms variable: Mode A's two CISes sharing ONE
controller-clock timestamp grid at the tighter interval (the 10 ms
matrix proved the shared-grid mechanism; this row asks whether the
controller's CIG scheduling holds two CISes at `iso_interval=6 x 1250`
with the shared pin discipline, and whether the pair's serial
connect/enable choreography fits the shorter window).

## Fixed identity

```text
run ID: rh3-7p5-modea-20260911 (validate unused before invoking)
row:    rh3.fresh_mode_a_48_3_1
```

Receiver: normal current-HEAD build; the receiver-side Mode A 7.5 ms
expectation matches the 10 ms Mode A shape (assembled slot-0 output,
slot-1 zero decoded count is expected pair behavior) plus the
360-frame ASRC fallback. Source: current-HEAD normal build, no
fragment.

## Prediction (falsifiable)

- PASS shape: both source streams `sub=16859, sc=16000, sf=0, out=0`,
  shared-grid `skip=0`, both lead arrays healthy (`under=0`), both sync
  polls returning the shared reference; receiver slot 0
  `rx_valid >= 90%` of 16859 with `plc <= 5%`, slot 1 valid with zero
  decoded (pair behavior); FLPR `ACTIVE` with zero submit/success
  (ASRC fallback); controller CIG holds both CISes with per-CIS sync
  values.
- FAIL starvation (skip growth / lead-under): fixture defect at the
  7.5 ms pair budget; re-check encode+pair choreography at 128 MHz
  first (pinned lesson).
- FAIL clean collapse with healthy source: interval-fundamental at the
  two-CIS CIG layout; STOP for user review before controller-config
  work (pinned discipline).

## Sequence

1. Preflight as usual; verify the proven image hashes unchanged.
2. ONE runner run (outer timeout 3600000 ms):

   ```bash
   nix develop --command ./scripts/hil-runner.py run \
     --fixture tests/hil/fixture.json \
     --binding tests/hil/fixture.local.json \
     --output-root /tmp/opencode/hil-runs \
     --run-id rh3-7p5-modea-20260911 \
     --junit /tmp/opencode/hil-runs/rh3-7p5-modea-20260911.junit.xml \
     --row rh3.fresh_mode_a_48_3_1
   ```

3. Extract both streams' status fields (shared pin, skip, lead arrays,
   sync), both receiver slot summaries, QoS/ISO tail (two CISes);
   classify; write the result doc; update resume-state; commit.

## Result documentation

`docs/development/system-hil-rh3-7p5-modea-result.md`: prediction vs
outcome, build proof, per-stream source status, both receiver slots
with the pair-attribution note, QoS/ISO tail, integrity, raw identity,
restoration, stop point. If PASS, the result doc also records the
now-complete three-stage evidence and frames the reinstatement
question for the user (plan revision required to move the `48_3_1`
rows back into `RH3_PASS_ROWS`; no code or rows change in this phase).

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, global `__ASSERT()`, receiver watchdog
empty-library). No NCS patches. No receiver firmware changes. No
rows/limits/matrix changes. No evidence mutation. Single run; no
retry. Status 0/1/130 immutable. Preserve all prior evidence roots.