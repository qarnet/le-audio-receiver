# RH3 Mode A summary-collection repair handoff

Status: host-only repair. Fix structured HIL evidence so one session segment
retains and validates every expected receiver stream summary. No hardware,
firmware, or diagnostic rerun belongs in this phase.

## Goal

Repair `scripts/hil/runner.py:Runner._step_session_end()` so it collects one
unique teardown summary for every `row.stream_count` stream before a segment
passes. Preserve the existing `summary.json` shape, including `last`, while
making `receiver_streams[segment].streams` complete for Mode A.

## Defect evidence

Fresh direct diagnostic `rh3-20260820-04-iso-status` passed with clean tail
checks and correct four-image identity. Its raw receiver console retained both
Mode A summaries:

```text
Stream[0] ... rx_valid=119 rx_error=0 rx_lost=14300 rx_unknown=0 rx_no_ts=50
Stream[1] ... rx_valid=0 rx_error=0 rx_lost=14390 rx_unknown=0 rx_no_ts=14390
```

But its structured `summary.json` / `result.json` retained only slot 0 under
`summary.receiver_streams[0].streams`. This follows directly from current
`_step_session_end()` behavior:

1. waits for first parseable summary line;
2. parses that one line;
3. validates only `summaries[-1]`;
4. returns before slot 1 teardown arrives.

This makes direct and matrix Mode A structured evidence incomplete and lets a
nonzero summary fault on a later stream escape session-summary validation. Raw
evidence remains complete, but structured evidence must be complete too.

## Scope

In scope:

1. Change `Runner._step_session_end()` to collect and validate every stream
   summary expected by the frozen `RowSpec`.
2. Extend fake receiver-wire support only as needed to emit multiple summaries.
3. Add public-boundary fake-lab regression coverage for complete Mode A
   evidence and rejected duplicate/later-stream fault evidence.

Out of scope:

- receiver firmware, ISO status counters/parser grammar, HIL row definitions,
  fixture/binding, image hashes, build settings, source behavior, warning
  policy, threshold, acceptance verdict, or evidence already written;
- physical HIL, serial, flash, reset, recovery, pairing, RF, source build,
  firmware build, full gate, matrix execution, or diagnostic retry;
- changing legacy stream-summary grammar or requiring ISO-status suffixes in
  general runner behavior;
- unrelated dirty worktree changes, commit, push, merge, PR, tag, release, or
  `STATUS.md` edit.

## Exact implementation

### `scripts/hil/runner.py`

Replace the one-line summary wait in `Runner._step_session_end()` with a
single total deadline of `self.deps.summary_timeout` for the whole segment.

1. Set `deadline = self.deps.clock() + self.deps.summary_timeout` once.
2. Repeatedly call `_wait_console_line()` with remaining time until exactly
   `row.stream_count` unique parsed stream summaries are collected.
3. A parsed summary slot must be in `range(row.stream_count)` and occur once.
   Fail `HilRunnerError("session end", ...)` on an out-of-range or duplicate
   slot. Do not silently overwrite or ignore it.
4. On timeout, fail `session end` and identify missing expected slots.
5. Preserve arrival order in returned `streams`. Keep `last` as final received
   summary for compatibility with existing consumers.
6. Validate `decode_err`, `i2s_underrun`, and `stream_reset` equal zero for
   **every** collected summary. Failure text must identify slot, field, and
   value.
7. Do not require `rx_*` fields, compare `SDUs` and `rx_valid`, alter raw-log
   scanning, or change any source/receiver behavior. Legacy summaries without
   suffix remain valid because parser compatibility is intentional.

### `tests/hil/rh2_test.py`

Extend `_receiver_passing_wire()` with an optional, test-only collection of
summary lines. Default remains one legacy slot-0 summary so existing single
stream tests stay byte-for-byte behaviorally equivalent. Encode and append
every supplied summary line after the normal tail transcripts.

Add tests in `TestRunnerPassingRow`:

1. **Mode A complete structured evidence:** use
   `rows.RH3_HEALTHY_ROWS[1]`, matching `_source_wire_for_row()`, and two
   extended fake summaries for slots 0 and 1 with distinct ISO-status values.
   Assert passed outcome, one segment entry, `streams` contains `[0, 1]` in
   arrival order, each full `rx_*` snapshot is retained, and `last` is slot 1.
2. **Later-stream validation:** use same Mode A row and a clean slot-0 summary
   followed by slot-1 summary with `decode_err=1`. Assert failed outcome,
   `session end` boundary, and error text identifies slot 1 plus
   `decode_err=1`. This proves later summaries cannot escape validation.
3. **Duplicate protection:** use same Mode A row and two slot-0 summaries.
   Assert failed `session end` outcome and duplicate-slot diagnostic. This
   proves first summary cannot satisfy both expected streams.

Tests must observe result/evidence structures and failure boundaries, not
private queues, calls, locks, or collection internals.

## Verification

Run sequentially from repository root. Use only host fake-lab commands:

```bash
nix develop --command python3 tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/runner.py tests/hil/rh2_test.py tests/hil/hil_fakes.py
```

Do not run any hardware, build, HIL runner, flash helper, source test that
touches `build/hil-source`, or full canonical gate.

## Executor rules

Implement only this repair. Preserve all unrelated dirty changes and current
physical evidence. Do not commit. Return changed files, behavior, verification
output, final `git diff --check`, final `git status --short`, deviations, and
blockers. If a requirement needs an API/runner architecture change beyond this
bounded collection behavior, stop and return evidence instead of guessing.
