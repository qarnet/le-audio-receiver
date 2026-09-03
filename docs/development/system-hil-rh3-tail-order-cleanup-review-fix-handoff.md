# RH3 tail-order cleanup review fix handoff

Status: host-only review repair. No hardware action.

## Review finding

`scripts/hil/source_client.py::_drain_queued_records()` correctly calls
`_next_record()` and `_dispatch()`, but current new regression covers only a
queued PASS terminal. It does not prove that a queued
`parse-error/unbound` diagnostic fails through this new cleanup path.

The accepted phase explicitly requires that new drain must not hide queued
diagnostics. Existing preflight and late-log diagnostic tests do not exercise
`_bounded_stop()` draining records after a receiver-tail failure.

## Exact repair

In `tests/hil/rh2_test.py`, add one runner-level test beside
`test_receiver_tail_failure_drains_queued_pass_terminal_before_cleanup`.

Build same fresh Mode A source transcript through `scored_complete` with
`omit_final_status=True`. After its valid queued teardown and PASS terminal,
append one valid HIL1 `status` record with:

```text
command_id: parse-error
run_id:     unbound
data:       {"command":"status","ok":false,"error":"parse_error","parse_result":"syntax"}
```

Make receiver tail fail through exact unavailable `bt iso quality` output, as
existing adjacent regression does. Do not provide STOP or idle responses. Then
assert:

1. row outcome remains `failed` with first failed boundary `receiver tail`;
2. cleanup contains exactly `source stop/idle` and its error contains
   `source diagnostic HIL1 record`;
3. no source `stop` or `idle` command was written after cleanup starts;
4. source evidence retains the parse-error line.

This proves queued diagnostics are neither ignored nor downgraded by the new
no-wait drain. Do not alter production code, fake protocol rules, strict
validation, documentation, or unrelated worktree content.

## Verification

Run sequentially from repository root:

```bash
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 -m py_compile \
  scripts/hil/runner.py \
  scripts/hil/source_client.py \
  tests/hil/hil_fakes.py \
  tests/hil/rh2_test.py
```

No hardware, build, flash, serial, HIL, commit, push, merge, or PR action.
