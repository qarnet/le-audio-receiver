# RH3 lifecycle-split recovery review fix handoff

Status: focused host-only review repair. No hardware action.

## Review finding

The lifecycle-split repair correctly separates active, ISO-tail, and post-stop
collection, but its recovery post-stop comparison creates a false failure for a
real stall row.

`src/flpr_shell.c::cmd_offload_status()` prints `Runtime` only when either
runtime restart or runtime restart failure is nonzero. A successful stall
recovery has `runtime_restarts == 0`, so real post-stop `flpr offload` omits the
line and `scripts/flpr_status.py::parse_offload_status()` returns `-1` for that
optional field. In contrast, `_run_fault_window()` already calls
`receiver.normalized_recovery_baseline()`, converting absent optional runtime
fields to zero before it stores `recovery["window_final"]`.

Current `_validate_post_stop_offload()` compares raw final
`runtime_restarts == -1` against active recovered `runtime_restarts == 0` and
fails `post-stop offload runtime_restarts changed`. Its fake stall transcript
currently prints a synthetic zero Runtime line, masking this real-shell shape.

Review also found two small strictness/documentation defects:

1. recovery final validation checks only success non-regression, not submit
   non-regression, despite the accepted lifecycle contract requiring both;
2. the settle comment still says it runs during the tail although it now runs
   during active capture.

## Scope

### In scope

1. Normalize optional post-stop runtime fields only for named recovery
   comparison.
2. Enforce final submit non-regression.
3. Make fault fake transcripts match real omitted-zero Runtime output.
4. Add regressions for the real stall transcript shape.
5. Correct stale local runner comment.

### Out of scope

- all hardware/HIL/build/flash/serial/RF action;
- source/receiver/FLPR firmware, parser grammar, Kconfig, devicetree, warning
  policy, thresholds, lifecycle design, docs, schema, or source-image change;
- weakening recovery/fault validation, accepting optional fields as missing for
  healthy rows, commit, push, merge, PR, tag, release, or `STATUS.md` edit.

## Exact repair

### `scripts/hil/receiver.py`

In `_validate_post_stop_offload()`:

1. Keep raw `post_stop_offload` validation and evidence unchanged.
2. For `recovery is not None` only, compare lifecycle/recovery fields against
   normalized copies:

   ```python
   active_compare = normalized_recovery_baseline(active_offload)
   final_compare = normalized_recovery_baseline(post_stop_offload)
   ```

   This maps only absent optional `runtime_restarts`, `runtime_fails`, and
   `hb_dedup` values from `-1` to `0`. Do not normalize any required health,
   fault, recovery, submit, success, state, epoch, or generation field.
3. Add nonnegative `active_submit` validation beside existing
   `active_success` validation. For every 48_4_1 post-stop snapshot require
   final `submit >= active_submit`. On regression append exact error
   `post-stop offload submit regressed`.
4. In named recovery comparison, include `busy` with fallback, recovery,
   probation, runtime, and fault fields. Compare normalized values exactly.
   A real omitted zero Runtime line passes; a nonzero late restart or changed
   fault/busy/recovery counter remains a failure.
5. Preserve all healthy-row behavior: optional runtime absence is not a new
   general exemption, and healthy final zero/fault requirements stay exact.

### `scripts/hil/runner.py`

Change only the stale settle comment at current constants so it says the
short settle window applies to the active capture, not a five-second tail.
No behavior/constant change.

### `tests/hil/rh2_test.py`

1. Make `_receiver_recovery_wire()` model real stall output: when fault is
   `stall`, build both recovery and post-stop `flpr offload` transcripts with
   `runtime_restarts=None`, so `Runtime` line is absent. Existing recovery
   window normalization should still retain active recovered runtime as zero.
2. Update stall post-stop assertions to compare normalized final status against
   active status for optional runtime fields, while preserving raw `-1` evidence
   assertion where useful.
3. Add direct lifecycle-validator coverage where an active stall-recovery
   snapshot has `runtime_restarts=0`, a raw post-stop parsed snapshot omits
   Runtime (`runtime_restarts=-1`), and validation passes. Mutate final submit
   below active submit and assert exact submit-regressed error. Mutate final
   busy and assert changed-counter failure.
4. Keep hang recovery and all existing fault/warning regressions strict.

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

No hardware/HIL/build/flash/serial/Bluetooth/RF action, no commit, no docs
update, and no unrelated edits.

## Executor return

Return changed files, exact regression behavior, all command results, final
status, no-hardware/no-build/no-commit confirmation, deviations, and blockers.
