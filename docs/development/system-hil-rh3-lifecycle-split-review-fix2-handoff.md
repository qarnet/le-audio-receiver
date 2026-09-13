# RH3 lifecycle-split recovery realism review fix handoff

Status: focused host-only review repair. No hardware action.

## Review finding

One recovery edge remains wrong after the first review fix.

1. `_receiver_recovery_wire()` still builds stall recovery and post-stop
   transcripts with `runtime_restarts=0`. `flpr_offload_transcript()` prints a
   Runtime line whenever that argument is not `None`, so this fake still does
   not match real `cmd_offload_status()` behavior. Real successful stall output
   omits Runtime; parser returns raw `runtime_restarts=-1`.
2. `_validate_post_stop_offload()` compares `probation_success` exactly during
   a named recovery. `audio_offload_stream_stop()` deliberately resets
   `g_probation_success` and `g_status.probation_success` to zero, while a
   recovered active snapshot may retain the completed probation count. This
   comparison would falsely reject a real normal post-stop fault row.

`probation_cleared` is cumulative and must still compare exactly. Final
`probation_active` remains required zero. This is a narrow expected state reset,
not a validation exemption.

## Exact repair

### `scripts/hil/receiver.py`

In the named-recovery normalized-comparison tuple in
`_validate_post_stop_offload()`:

1. Remove only `probation_success` from exact active/final equality comparison.
2. Retain comparison for `probation_active` and `probation_cleared`.
3. Add a concise local comment explaining that normal stream stop resets
   probation-success progress, while cleared count remains lifetime evidence.
4. Do not relax any healthy-row or recovery fault/restart/counter rule.

### `tests/hil/rh2_test.py`

1. In `_receiver_recovery_wire()`, when `fault == "stall"`, create separate
   wire-only copies for both recovery and post-stop offload transcripts with
   `runtime_restarts=None`. Keep internal `recovered`/`post_stop` values at zero
   for expected normalized active state. Hang continues to print its nonzero
   Runtime line.
2. Preserve and strengthen stall integration evidence assertion: raw post-stop
   parser result has `runtime_restarts == -1`, while
   `normalized_recovery_baseline()` returns zero matching active recovery.
3. Extend direct recovery lifecycle validation with an active snapshot having
   `probation_success=100`, a stopped final snapshot with
   `probation_success=0`, and unchanged `probation_cleared`; it must pass.
   Mutating final `probation_cleared` must still fail with its changed-counter
   error.

### `scripts/hil/runner.py`

Wrap the active-capture settle comment to repository-style line width only.
No behavior change.

## Scope limits

Only `scripts/hil/receiver.py`, `scripts/hil/runner.py`, and
`tests/hil/rh2_test.py` may change beyond this handoff. No docs, hardware/HIL,
build, flash, serial, Bluetooth, RF, source/receiver firmware, parser grammar,
Kconfig, devicetree, `STATUS.md`, commit, push, merge, PR, tag, or release.

## Verification

Run sequentially from repository root, host-only:

```bash
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 tests/hil/capture_runner_test.py
nix develop --command python3 -m py_compile \
  scripts/hil/runner.py \
  scripts/hil/receiver.py \
  tests/hil/rh2_test.py \
  tests/hil/capture_runner_test.py
git diff --check
```

Return changed files, exact regression result, all verification output, final
status, no-hardware/no-build/no-commit confirmation, deviations, and blockers.
