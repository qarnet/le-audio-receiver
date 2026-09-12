# System HIL bounded-abort transition handoff

Status: contract correction required before RH1B runtime. RH0 and RH1A remain
otherwise accepted. Existing exact-only state progression cannot represent
required `stop`, timeout, or runtime-error teardown before `scored_complete`
without emitting false states or producing a terminal record RH0 rejects.

## Goal

Add one explicit, fail-closed abort edge to current segment:

```json
{"state":"teardown","cause":"stop"}
```

This is not a general state skip. It is legal only from an already-started
non-teardown segment, only to `teardown`, and only for cause `stop`, `timeout`,
or `error`. Terminal verdict after abort must be `fail`.

## Files

Update only:

- `scripts/hil/protocol.py`
- `scripts/test_hil_runner.py`
- `hil/source/core/hil_source_types.h`
- `hil/source/core/hil_source_record.h`
- `hil/source/core/hil_source_record.c`
- `hil/source/core/hil_source_state.h`
- `hil/source/core/hil_source_state.c`
- `tests/unit/hil_source_control/src/test_hil_source_control.c`
- `docs/development/system-hil-milestones.md`, source protocol paragraph only
- this handoff only for factual correction

Do not alter signal, parser, firmware runtime, inventory, receiver, BSim, or
unrelated files.

## RH0 tracker contract

For `state` records:

- Ordinary records keep exact current progression and use data object with
  `state` only.
- Abort record has exact data keys `state` and `cause`, state exactly
  `teardown`, cause exactly `stop`, `timeout`, or `error`.
- Abort is legal only after current segment has accepted at least its initial
  state (`idle` for segment 0, `connecting` for reconnect segment), before that
  segment has reached teardown, and on current segment/async start command ID.
- It changes tracker current state to teardown without synthesizing skipped
  records. Time/firmware/run/command/segment checks remain unchanged.
- Repeated abort or ordinary teardown after abort is state repeat. Terminal
  `pass` after abort is rejected; terminal `fail` is accepted once.
- A normal terminal pass/fail after ordinary teardown remains legal.
- Reconnect after aborted first segment is rejected. Abort is terminal for
  whole run even before terminal record arrives.
- Every rejected abort leaves tracker fields byte-for-byte/logically unchanged.

Make `parse_hil1_line()` validate exact state-data key set and shape:

- ordinary state: exactly `{"state": <valid state>}`;
- abort state: exactly `{"state":"teardown","cause":<valid cause>}`.

This closes current permissive extra-data gap. Existing record helpers already
emit ordinary exact shape.

Track explicit `aborted` boolean and `abort_cause` string/enum in
`HilRunTracker` public observable state.

## RH1A source contract

Add:

```c
enum hil_source_abort_cause {
    HIL_SOURCE_ABORT_STOP,
    HIL_SOURCE_ABORT_TIMEOUT,
    HIL_SOURCE_ABORT_ERROR,
};
```

Add canonical helper returning `stop`, `timeout`, `error`, or `NULL`.

State object adds `bool aborted` and abort cause. `start()` clears both.

Expose:

```c
int hil_source_state_abort_to_teardown(
    struct hil_source_state *st,
    enum hil_source_abort_cause cause);
```

Behavior:

- requires active run and valid cause;
- legal from any current state before teardown;
- sets current state to teardown, `aborted=true`, and cause atomically;
- repeat/after-teardown/invalid cause/inactive fail unchanged;
- pass terminal after abort is rejected; fail terminal accepted;
- next reconnect segment after abort rejected;
- `stop_requested` is independent: runtime sets it first for host stop, then
  aborts with STOP. Timeout/error callers need not set stop flag.

Add record convenience:

```c
int hil_source_record_format_abort_teardown(..., enum hil_source_abort_cause cause);
```

It emits exact state record data `{"state":"teardown","cause":"..."}`.

## Tests

RH0 Python:

- abort from each pre-teardown state in segment 0, then fail terminal, passes;
- abort in reconnect segment passes after ordinary first segment teardown;
- first record abort, wrong target state, missing/unknown cause, extra data key,
  ordinary state extra key, repeat abort, pass terminal, reconnect after abort,
  wrong command/segment all reject;
- rejected input leaves tracker snapshot unchanged;
- generated RH1A abort records parse and track through RH0.

RH1A control native:

- canonical cause names and exact abort record bytes;
- abort from each pre-teardown state;
- invalid/repeat/inactive/after-teardown atomic rejection;
- pass and reconnect blocked after abort; fail terminal accepted;
- start after terminal clears abort snapshot while preserving configuration.

## Verification

```bash
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_control -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-abort-control-twister
nix develop --command python3 -W error scripts/test_hil_runner.py
python3 scripts/test_inventory.py --count
python3 scripts/test_inventory.py --json
python3 -m compileall -q scripts/hil scripts/test_hil_runner.py
git diff --check
git status --short
```

Inventory remains 65. No hardware, full dirty-tree canonical gate, commit,
push, merge, PR, or unrelated edits.

## Escalation and recap

Stop after two materially different failed attempts or any need for a broader
protocol redesign. Return exact blocker evidence and one question. Otherwise
return files changed, contracts, exact tests, warnings, deviations, blockers,
and status.
