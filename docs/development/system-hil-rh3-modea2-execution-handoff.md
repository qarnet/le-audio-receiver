# RH3-ModeA2 offload-disabled isolation rerun handoff

Status: approved one-run hardware diagnostic. Same binary isolation as
RH3-ModeA1/1b, now with the fully repaired runner: session-end scans the
complete segment raw evidence (RH3c, commit `d8a7ed2`) and understands
offload-disabled images (RH3-ModeA1b runner support, commit `eac880d`).
Runs `rh3-modea1-20260903-offload-disabled` and
`rh3-modea1b-20260903-offload-disabled` remain immutable fixture-defect
baselines; their raw telemetry already shows near-total dual-CIS starvation
with offload disabled, but the binary classification requires a
runner-validated limits verdict.

## Goal

Run fresh Mode A `48_4_1` with the offload-disabled receiver image
(`tests/hil/receiver-offload-disabled.conf`, SHA
`9d803fa50488b2058480d5888b2d09b2c87c1a2bef570b768f2b689984605b81`) and
`--allow-offload-disabled`:

- PASS under frozen limits: FLPR offload pipeline implicated in Mode A
  starvation (locus narrowed; next phase is FLPR dual-CIS investigation).
- FAIL on transport limits: transport/controller path implicated; FLPR
  cleared as primary suspect (next phase is controller/buffer diagnostics).
- Other boundary: record, classify, stop for review.

## Identity contract

Per the modea1b amendment: CPUAPP hashes are HEAD-dependent (APP_COMMIT
embedded). Record HEAD (`git rev-parse HEAD`), clean tree, build the
diagnostic image twice with no repo change, require byte-identical CPUAPP
hashes; the run's `images.json` is authoritative for flashed identity. FLPR
expected stable `45ab8d15...`; source expected `f0e1c5ab...`/`4e4b82f5...`.
No absolute CPUAPP pins from earlier records.

## Fixed identity

```text
run ID: rh3-modea2-20260904-offload-disabled (validated unused, length 36)
row:    rh3.fresh_mode_a_48_4_1
```

## Sequence

1. Preflight: disk gate; `git rev-parse HEAD` + clean `git status` (only
   this handoff untracked); run-ID validation; run-dir + external-JUnit
   ownership checks; fixture validate; source-image hash check.
2. Diagnostic build #1 (`fw-build-54l15 -DEXTRA_CONF_FILE=...`), prove
   config lines (offload unset, ASRC linear, acceptance diagnostics, traces
   unset, ISO RX 3), record hashes; diagnostic build #2 determinism proof.
3. ONE runner invocation (outer tool timeout 3600000 ms, status 0/1/130
   immutable, never rerun):

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-modea2-20260904-offload-disabled \
  --junit /tmp/opencode/hil-runs/rh3-modea2-20260904-offload-disabled.junit.xml \
  --row rh3.fresh_mode_a_48_4_1 \
  --allow-offload-disabled
```

4. Read-only integrity review; per-slot summaries (expect runner-validated
   this time - tail-swallowed summaries are now recoverable); limits verdict;
   FLPR active/post-stop snapshots (submit=0 expected); ISO tail.
5. Binary classification per the goal. The prior runs' raw telemetry
   (slot 1 `rx_valid=0`) predicts a FAIL-on-limits arm, but the verdict must
   come from THIS run's runner validation, not prediction.
6. Restore normal build; prove `CONFIG_AUDIO_OFFLOAD_ASRC=y`; record normal
   hash; disk gate.
7. Canonical result doc `system-hil-rh3-modea2-result.md`; resume-state
   update (run count, last flash identity, verdict); commit exactly:

```text
docs(hil): record Mode A isolation verdict with repaired runner
```

## Constraints

Runner owns all hardware; no manual target operations. Actionable build
warnings are errors (allowed: documented dirty-tree, nRF54L15 watchdog
empty-library, global `__ASSERT()`). No production source changes, no
evidence mutation, no retry. Escalate on: unexpected boundary,
classification ambiguity, determinism mismatch, integrity failure.