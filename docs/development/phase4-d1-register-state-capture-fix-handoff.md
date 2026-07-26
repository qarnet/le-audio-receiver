# Phase 4 D1 Register-State Test — Required Analyzer Capture

Status: incomplete follow-up to `27c5648`

## Problem

`27c5648` flashed and restored the test app, but did not perform required
sigrok capture. Its conclusion also overstates what GPIO register readback
proves: OUT/IN agreement proves internal GPIO configuration and sampled level;
it does not independently prove absence of all external electrical contention.

The phase is not accepted until CH1 capture is collected while official-XIAO
test image runs.

## Scope

Edit only:

- `docs/development/phase4-d1-register-state-results.md`
- new `docs/development/phase4-d1-register-state-capture-fix-handoff.md`

No test-code or main-firmware changes.

## Required execution

1. Rebuild test only if needed; flash current
   `build/test-nrf54l15-d1-register-state/merged.hex` using dynamic
   `nrf-probes --find nrf54l` and existing OpenOCD/Xiao load path.
2. Capture analyzer with common ground and verified physical map:
   - CH0: D0 / P1.04
   - CH1: D1 / P1.05
   - CH2: D2 / P1.06
   - CH3: 3V3
3. Run `sigrok-cli` at 100 kHz for at least 12 seconds. Preserve raw output in
   `/tmp`. Expected: CH1 low for two seconds, high for two seconds; CH0/CH2 low;
   CH3 high.
4. If sigrok exits early, use bounded sample count that still covers at least
   8 seconds. Retry at most once. Do not call hardware unavailable merely
   because a first invocation used wrong output format.
5. Reflash current receiver with `fw-flash-54l15` after capture. Build first
   only if app source or configuration changed. Preserve warnings/errors.

## Results corrections

1. Add exact capture command, sample rate/duration, artifact path, raw channel
   edge/level evidence, and correlation-table third column.
2. If CH1 toggles, conclude first GPIO capture was analyzer setup/probe issue.
3. If CH1 stays high while OUT/IN toggle, conclude analyzer measurement path
   needs physical repair/replacement; do not claim SoC pad fault.
4. Replace unconditional “no physical contention” and “pin itself is not at
   fault” wording with appropriately bounded register evidence.
5. Fix erroneous “contact at D2 pad” to “contact at D1 pad.”

## Constraints

- Read-only OpenOCD only if register snapshots are repeated. No halt/reset in
  snapshot session, no `mww`, no recover.
- Preserve existing unstaged receiver diagnostics exactly.
- Stage only scoped docs and this handoff. New commit; do not amend/push/merge/PR.

## Verification

```bash
git diff --check
```

Return raw analyzer evidence, receiver restore evidence, corrected conclusion,
commit hash/message, and whether phase is accepted or blocked.
