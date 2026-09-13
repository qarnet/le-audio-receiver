# RH3-7p5 phase handoff: fixed-fixture 7.5 ms diagnostic (mono first)

Status: approved bounded diagnostic, the plan-of-record RH3-7p5 phase
(`docs/development/system-hil-milestones.md`: "root-cause and fix, then
reinstate the three `48_3_1` rows in the mandatory matrix, or remove 7.5 ms
advertisement from the production PACS"). The standing decision is binary:
7.5 ms must work or must not be supported.

## Why this is now actionable

All prior 7.5 ms evidence (H40, H42: `rx_valid=24` of `16859` submitted,
`rx_unreceived=18891`, `crc_error=0`) predates two now-settled fixture
defects that the 10 ms investigation closed:

1. The source app core ran at 64 MHz; two-LC3-encode workloads missed
   alternate events with zero send errors (the ModeA17 failure class,
   pinned in `AGENTS.md` "HIL source fixture timing").
2. The source scheduled against host-derived offsets, not the mirrored
   controller clock (the ModeA13-18 failure class).

The current fixture (128 MHz + mirrored CPUNET MPSL RTC + controller-clock
lead-window scheduling) is the one that passed the full 10 ms RH3 matrix
(`TRANSPORT_RUNTIME_ACCEPTED`, 2026-09-09,
`docs/development/system-hil-rh3-controller-clock-result.md`). The old
7.5 ms loss signature was attributed then to "the dedicated HIL source side
or the SW-split central" - both of which are now either fixed (source) or
replaced (SW-split central is gone; SDC has been the netcore controller
since ModeA10). Counter-evidence that the receiver CAN receive 7.5 ms at
all: the accepted BZ2 desktop gate streamed 7.5 ms from a Linux central
with nonzero decode and zero faults
(`docs/development/phase2-stock-desktop-gate-results.md`).

This phase therefore re-baselines 7.5 ms on the proven fixture before any
product decision.

## Expected receiver behavior at 7.5 ms (pre-declared, do not mislabel)

The FLPR payload contract accepts 480 input frames
(`FLPR_RING_PAYLOAD_MAX_INPUT == 480U`, `src/flpr_ring.h`); a 7.5 ms call
carries 360 frames. The documented, expected path is cpuapp ASRC fallback
(AGENTS.md "Known behavior question", `docs/known-limitations.md`). A
7.5 ms row therefore expects: FLPR offload submit/success at or near zero
during streaming, cpuapp ASRC active, and NO offload-fault counters. This
is correct behavior, not an offload failure. The receiver-side witnesses
already pin this contract (`tests/unit/audio_i2s`
`test_offload_reject_360_input_falls_back`, `tests/unit/audio_offload`
`test_asrc_invalid_frames` with its current 240-frame concrete input,
`tests/unit/flpr_ring` MAX_INPUT assertions). No receiver change is in
scope.

## Staged sequence (one row per run, classify before the next)

Stage 1 - mono `rh3.fresh_mono_48_3_1` (THIS handoff's run):

- Cheapest discriminator: one LC3 encode per SDU (half the Mode B CPU
  load), one ASE, one CIS. Isolates interval-fundamental effects from
  encode-budget effects.
- Prediction (falsifiable): if the fixed fixture carries 7.5 ms, mono
  passes the frozen limits (`rx_valid >= 90%` of 16859, `plc <= 5%` of
  decoded) with source `skip` at or near zero, `sf=0`, and lead telemetry
  healthy. If mono fails with the same near-total-collapse signature, the
  mechanism is interval-fundamental on the source/SDC central side at
  7.5 ms (CIG scheduling at 7500 us interval: ISO interval 6 x 1250 us,
  nse/bn/FT selection by the controller for the tighter interval) - the
  next stage is a controller-config diagnostic, not more host work.

Stage 2 (only after Stage 1 passes and is classified): Mode B 48_3_1 -
tests the two-encode-per-7.5 ms budget at 128 MHz against the pinned
throughput lesson.

Stage 3 (only after Stage 2 passes): Mode A 48_3_1 - tests the shared
Mode A timestamp grid at 7.5 ms (two CISes, one shared pin).

Only if all three stages pass does the reinstatement question open (add
the rows back to `RH3_PASS_ROWS` - a rows/limits change requiring a plan
revision, NOT part of these diagnostics). If any stage fails with a
classified interval-fundamental cause, the alternative branch opens:
remove 7.5 ms from the production PACS advertisement and re-run the BZ
desktop gate at 10 ms (plan-of-record exit wording).

## Fixed identity

```text
run ID: rh3-7p5-mono-20260911 (validate unused before invoking)
row:    rh3.fresh_mono_48_3_1
```

Receiver: normal current-HEAD build (APP_COMMIT-derived hash; FLPR
`45ab8d15...` expected unchanged; resolved config must prove
`CONFIG_AUDIO_OFFLOAD_ASRC=y`, `CONFIG_BT_ISO_RX_BUF_COUNT=3`).
Source: current-HEAD normal build, no fragment
(`CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`, SDC netcore unchanged
`2c3af526...`).

## Sequence

1. Preflight: disk gate 80 GiB; `git rev-parse HEAD` + `git status
   --porcelain` (expected: clean tracked tree, untracked ModeA3 handoff
   and `.cache/` dirs only); run-ID validation (both output paths absent,
   non-symlink, `.locks` empty); fixture validate returns the known JSON.
2. Build source twice (determinism proof), `fw-build-hil-source`, no
   fragment; record all four hashes; prove resolved app config
   (`TARGET=3`, QoS defaults) and CPUNET SDC block unchanged.
3. Build receiver once; record hashes; prove resolved config.
4. ONE runner run (outer timeout 3600000 ms):

   ```bash
   nix develop --command ./scripts/hil-runner.py run \
     --fixture tests/hil/fixture.json \
     --binding tests/hil/fixture.local.json \
     --output-root /tmp/opencode/hil-runs \
     --run-id rh3-7p5-mono-20260911 \
     --junit /tmp/opencode/hil-runs/rh3-7p5-mono-20260911.junit.xml \
     --row rh3.fresh_mono_48_3_1
   ```

5. Extract source final status (sub/sc/cb/sf/out/skip, lead min/max,
   sync telemetry) and receiver summary (per-slot values, FLPR active
   AND the expected cpuapp-ASRC fallback state, QoS, ISO tail including
   the controller-selected CIG layout at 7500 us).
6. Integrity review; classify per the stage table above; write the result
   doc; update resume-state; commit.

## Classification arms

- PASS (frozen limits met): Stage 1 passes. Record, commit the result
  doc, then Stage 2 handoff (Mode B) follows the same pattern. The 7.5 ms
  product question (reinstate vs remove) stays open until all three
  stages pass; reinstatement itself is a plan revision.
- FAIL with source starvation signature (large `skip`, lead-under events,
  `sf>0` or sub short of target): fixture defect at 7.5 ms; per the
  pinned lesson, re-check the encode budget at 128 MHz before any other
  theory; classify, fix, one rerun as fix-validation.
- FAIL with near-total receiver collapse AND source fully healthy
  (`skip=0`, `sf=0`, `sub=16859`, lead healthy): interval-fundamental on
  the air/controller side at 7500 us. Record; the next bounded
  diagnostic is controller-config (CIG layout / reserved time / latency
  parameter at 7.5 ms); STOP for user review before it because this
  borders the reopened-controller-causation territory the pinned
  discipline guards.
- Other boundary: record, classify, stop for user decision if not clean.

## Result documentation

`docs/development/system-hil-rh3-7p5-mono-result.md`: prediction vs
outcome, build proof, source status fields, receiver summary with the
pre-declared ASRC-fallback expectation explicitly evaluated, QoS/ISO tail
CIG layout at 7.5 ms, integrity, raw identity, restoration, stop point.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree notice, global `__ASSERT()`, receiver watchdog
empty-library). No NCS patches. No receiver firmware changes. No
rows/limits/matrix changes (diagnostic rows only). No evidence mutation.
Single run; no retry. Status 0/1/130 immutable. Do not rerun any prior
7.5 ms IDs (H40/H42 are immutable historical evidence). Preserve all
existing evidence roots.