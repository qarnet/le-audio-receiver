# System HIL RH0 review-fix handoff

Status: focused correction handoff. RH0 remains open. Apply only fixes below to
implementation created from `docs/development/system-hil-rh0-handoff.md`.

## Goal

Close review defects in strict schema handling, HIL1 fail-closed state tracking,
cleanup ownership, lock-path safety, evidence failure handling, and warning-free
tests. Preserve RH0 host-only scope.

## Review evidence

Focused verification otherwise passes: 52 RH0 tests, pytest 8.4.2, pyserial
3.5, inventory 63, BlueZ/WirePlumber gates 76 and 89 tests, compileall, and
`git diff --check`. Full canonical gate remains intentionally unrun on dirty
tree.

Review reproduced these defects against current implementation:

1. Logical fixture accepts extra role beyond exact `receiver`/`source` plus
   capability-dependent `capture` contract.
2. Rejected HIL1 state transition mutates `firmware_id` and
   `last_monotonic`, causing later valid input to fail. A status record at
   segment 9 followed by segment 0 is also accepted, violating segment
   monotonicity.
3. Cleanup callback raising `KeyboardInterrupt` prevents older callbacks from
   running. Body `KeyboardInterrupt` combined with cleanup failure would also
   be invalid inside `ExceptionGroup` rather than `BaseExceptionGroup`.
4. Pre-existing `<output-root>/.locks` symlink is followed, allowing lock file
   creation outside selected output root.
5. RH0 test run emits four `ResourceWarning` diagnostics for unclosed evidence
   files. Project warning policy forbids accepting them.
6. Evidence tests cover validation failure before writes but not a write-stage
   failure after one metadata file has been updated. `finalize_evidence()` can
   leave a new `SHA256SUMS` beside an old or failed `MANIFEST.md`.

## In scope

Update only:

- `scripts/hil/model.py`
- `scripts/hil/protocol.py`
- `scripts/hil/lifecycle.py`
- `scripts/hil/evidence.py`
- `scripts/test_hil_runner.py`
- this handoff only if implementation facts need a small correction

Do not edit source firmware, hardware scripts, fixture JSON content, milestone
scope, dependencies, historical status, or user-owned
`docs/development/firmware-release-fr4-summary-wait-fix-handoff.md`.

## Required corrections

### 1. Exact and frozen fixture models

- For capability `none`, role names must equal `receiver` and `source`.
- For capability `mono` or `stereo`, role names must equal `receiver`, `source`,
  and `capture`.
- Reject every extra role with `HilSchemaError`.
- Return `LogicalRole.board is None` for capture role, matching dataclass
  contract.
- Store `LogicalFixture.roles` and `PhysicalBinding.roles` in immutable mapping
  views so frozen dataclasses cannot be mutated through nested role maps. Keep
  public mapping lookup/iteration behavior.
- Add public-boundary tests for extra-role rejection, capture board value, and
  attempted role-map mutation.

### 2. Atomic HIL1 tracker acceptance

`HilRunTracker.accept(record)` must either accept and commit whole transition or
raise `HilProtocolError` with every tracker field unchanged. Do not solve this
by cloning arbitrary object state after mutation; validate into local proposed
values, then commit once.

Segment contract:

- Before first state, accepted `ack` or `status` records must use segment 0.
- Once current state segment exists, `ack`, `status`, and `terminal` must use
  exactly current segment.
- State records remain only legal transition to current segment or exactly
  current+1 under existing teardown/reconnect rules.
- Segment skips and regressions fail for every record kind.
- Terminal segment must equal current segment.
- `status` must use a command ID different from start command ID, because it is
  response to separate status query. Async `ack`, `state`, and `terminal` keep
  start command ID.
- Post-terminal status remains accepted under a new command ID and immutable
  current segment. No status changes lifecycle state.

Add regression tests proving rejected run, firmware, command, monotonic,
segment, state, and terminal records leave all tracker fields unchanged. Add
tests for future-segment ack/status/terminal rejection, pre-state nonzero
segment rejection, and same-command status rejection.

### 3. Cleanup must survive BaseException

- `CleanupStack.close()` must catch callback `BaseException`, continue running
  all older callbacks in LIFO order, then expose all failures through
  `CleanupFailure` with original objects retained.
- Context body `Exception` plus cleanup failure uses `ExceptionGroup` on Python
  3.11+.
- Context body `BaseException` such as `KeyboardInterrupt` plus cleanup failure
  uses `BaseExceptionGroup` on Python 3.11+.
- Preserve existing fallback behavior only if needed for an explicitly
  supported pre-3.11 interpreter.
- Add tests for callback `KeyboardInterrupt` not skipping remaining cleanup and
  body `KeyboardInterrupt` plus cleanup failure exposing both causes.

### 4. Lock directory must not escape

- `FixtureLock.acquire()` must reject an existing `.locks` symlink and any
  non-directory `.locks` entry before creating lock file.
- Ensure canonical `.locks` directory remains directly under canonical output
  root. Do not follow a symlink outside root.
- Create missing `.locks` as one directory under already-existing output root;
  no recursive ancestor creation is needed.
- Fail closed with a lifecycle/lock error and write nothing outside output
  root.
- Add tests using a `.locks` symlink to an external temporary directory and a
  non-directory `.locks` entry. Assert external directory stays empty.

### 5. Transactional evidence metadata

Keep evidence payload files immutable. Strengthen finalization so a failure
while updating `SHA256SUMS` and `MANIFEST.md` cannot be reported as success and
does not leave a mixed new/old metadata pair.

Required behavior:

- Validate and hash all payload evidence before changing either metadata file.
- Stage complete new contents for both metadata files before replacing either.
- If staging fails, preserve prior metadata bytes exactly.
- If replacement fails after one replacement, restore both metadata files to
  their exact prior state (including prior absence) when possible, then raise
  `EvidenceError`.
- When no prior manifest existed and rollback succeeds, best-effort failed
  manifest may be written. Never overwrite a valid prior manifest merely to
  label a failed re-finalization.
- Temporary/backup files must not remain in run directory after handled
  failure.
- Add injected write-stage and second-replacement failure tests. Assert prior
  metadata bytes remain exact, payload files remain, no temp files remain, and
  failure is raised.

Use module-local replace/write seams or `unittest.mock` at public behavior
boundary. Do not assert private helper call counts.

### 6. Warning-free tests

Replace raw `open(...).read()` expressions with context managers. Run RH0 tests
with warnings promoted to errors so resource warnings cannot recur:

```bash
nix develop --command python3 -W error scripts/test_hil_runner.py
```

## Verification

Run no hardware commands. Run:

```bash
nix develop --command python3 -W error scripts/test_hil_runner.py
nix develop --command pytest --version
nix develop --command python3 -c 'import serial; print(serial.VERSION)'
python3 scripts/test_inventory.py --count
python3 scripts/test_inventory.py --python
python3 scripts/test_inventory.py --json
python3 scripts/test_bluez_wireplumber_gate.py
python3 scripts/test_bluez_wireplumber_phase3_gate.py
python3 -m compileall -q scripts/hil scripts/hil-runner.py scripts/test_hil_runner.py
git diff --check
git status --short
```

Expected inventory remains 63. Do not run full canonical gate on dirty tree.

## Constraints

- Host-only. No probes, serial ports, Bluetooth, ALSA, sudo, flashing, reset,
  DTR/RTS, recovery, or hardware access.
- No warning may be ignored or normalized.
- Do not weaken existing tests or error handling.
- Do not commit, push, merge, amend, open a PR, or edit git configuration.
- Preserve all intended RH0 changes and unrelated user-owned files.

## Escalation and recap

Stop after two materially different failed attempts or if correction requires a
new architecture. Return exact blocker evidence and one question. Otherwise
return files changed, behavior fixed, exact verification results, deviations,
remaining blockers, and current git status.

## Review round 2 corrections

First correction pass fixed core defects and reports 68 passing tests. RH0 still
remains open until following review issues are closed:

1. `scripts/hil/protocol.py` class documentation still says status may use any
   command ID. Change it to frozen contract: status uses command ID different
   from start command ID.
2. `scripts/hil/lifecycle.py` cleanup documentation names only
   `ExceptionGroup`. Document `ExceptionGroup` for an `Exception` body and
   `BaseExceptionGroup` for a `BaseException` body.
3. Cleanup regression tests currently register failing callback before
   successful callback, so LIFO executes success before failure and does not
   prove cleanup continues after failure. Register successful older callback
   first and failing newer callback second. Assert failure executes first and
   older callback still executes afterward. Apply this to ordinary exception
   and `KeyboardInterrupt` callback tests.
4. Evidence temporary-file assertion uses `endswith(".tmp")`, but actual
   staged names look like `.SHA256SUMS.tmpXXXX`. Make assertion detect any
   hidden staging file containing `.tmp`, or compare directory against exact
   expected retained names.
5. Staging-failure test currently fails first staging call, so it does not prove
   first staged file is removed when second staging call fails. Let first call
   execute real staging and inject failure on second call. Assert prior metadata
   and payload bytes remain exact and no staging files remain.
6. `finalize_evidence()` catches `Exception` around staging and commit. A
   `KeyboardInterrupt` during second replacement can leave mixed metadata and a
   staged file. Catch `BaseException` for transactional cleanup and rollback.
   After cleanup, wrap normal `Exception` as `EvidenceError`, but re-raise
   original non-`Exception` `BaseException` so interruption semantics remain.
   `_restore_metadata()` and best-effort failed-manifest handling must not let a
   secondary `BaseException` hide original finalization failure; always remove
   any temp they own.
7. Public `finalize_evidence()` contract says finalization failures raise
   `EvidenceError`. Convert ordinary `OSError` from hashing, metadata snapshots,
   staging, or replacement to `EvidenceError`; do not leak raw ordinary I/O
   exceptions. Preserve non-`Exception` cancellation after cleanup as above.
8. Add public-behavior tests for second-stage `KeyboardInterrupt` and
   second-replacement `KeyboardInterrupt`: prior metadata restored, payload
   retained, no temp remains, and `KeyboardInterrupt` propagates. Add an
   ordinary hashing/snapshot I/O failure test proving `EvidenceError` boundary.

Rerun full verification list with `python3 -W error`. Inventory must remain 63.
No hardware commands, full dirty-tree canonical gate, commit, or unrelated
edits.

## Review round 3 corrections

Second correction pass fixed runtime handling and reports 72 passing tests.
Close these final test/documentation mismatches:

1. Cleanup continuation tests must record failing callback execution as well as
   later successful cleanup. Use callbacks that append `boom`/`ki` before
   raising, then assert exact LIFO observed order `['boom', 'ok']` and
   `['ki', 'ok']`. Current `['ok']` assertion proves success happened but does
   not directly prove execution order.
2. `test_snapshot_io_failure_raises_evidence_error` claims snapshot-boundary
   coverage, but replacing `SHA256SUMS` with a directory fails earlier during
   evidence enumeration. Inject ordinary `OSError` through snapshot I/O seam
   after valid payload enumeration, then assert `EvidenceError`, exact prior
   payload/metadata preservation, and no temporary files.
3. `scripts/test_hil_runner.py` module docstring says tests never use private
   helpers, but transactional failure tests patch module-local I/O seams.
   Document actual policy: tests assert public outcomes and may patch narrow
   module-local I/O seams for deterministic failure injection, without helper
   call-count or internal-shape assertions.
4. `scripts/hil/evidence.py` module/function docs say every post-replacement
   failure ends in `EvidenceError`; code correctly preserves non-`Exception`
   cancellation such as `KeyboardInterrupt`. Document exact boundary: ordinary
   failures become `EvidenceError`; non-`Exception` cancellation rolls back and
   propagates unchanged.

No intended runtime behavior change. Rerun full verification list with
`python3 -W error`; inventory remains 63. No hardware, full dirty-tree
canonical gate, commit, or unrelated edits.
