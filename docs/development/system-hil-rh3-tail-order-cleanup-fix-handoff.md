# RH3 active-tail ordering and source-cleanup fix handoff

Status: host-only repair phase. No hardware action belongs in this phase.

## Goal

Repair two host-side HIL evidence defects exposed by immutable direct diagnostic
`rh3-20260821-03-modea-depth-fix`:

1. collect `bt iso quality` while both CISes can still exist;
2. consume an already queued source terminal before cleanup decides to send
   `stop`.

Keep all strict failure behavior. This phase does not diagnose or fix the
underlying Mode A packet loss.

## Grounding

In `/tmp/opencode/hil-runs/rh3-20260821-03-modea-depth-fix/`:

- source emitted `scored_complete` at `160547 ms`, teardown at `164611 ms`, and
  a PASS terminal at `165419 ms`;
- current `LIVE_TAIL_COMMANDS` queried `bt iso quality` last, after audio,
  performance, offload, and FLPR commands;
- receiver then printed `ISO link quality unavailable: -128`, so strict parser
  failed with missing header and two missing slots;
- tail failure interrupted `SourceClient.start()` before it consumed queued
  teardown and terminal records; cleanup then attempted source control and
  recorded `source diagnostic HIL1 record: command_id='parse-error'
  run_id='unbound'`;
- evidence integrity passed `23/23` SHA-256 entries; evidence is immutable and
  must not be changed or reused.

`scripts/hil/runner.py::_step_run_row()` invokes receiver-tail collection from
the source `scored_complete` hook. `source_client.start()` resumes source record
consumption only after that hook returns. `SerialConsole.next_line(0)` consumes
only already queued decoded lines, so it supplies a bounded, no-wait cleanup
drain.

## Scope

### In scope

1. Reorder active receiver-tail commands in `scripts/hil/runner.py`.
2. Add queued-record draining before source cleanup STOP decision in
   `scripts/hil/source_client.py`.
3. Update fake transcripts and host regression coverage in
   `tests/hil/hil_fakes.py` and `tests/hil/rh2_test.py`.
4. Correct stale physical-run facts in:
   - `docs/development/system-hil-resume-state.md`;
   - `docs/development/system-hil-rh3-software-status.md`.

### Out of scope

- any receiver, source firmware, controller, Kconfig, devicetree, warning
  scanner, parser grammar, threshold, PLC, audio, RF, pairing, or image change;
- treating nonzero ISO link-quality counters as pass/fail thresholds;
- source build, production build, flash, reset, serial, Bluetooth, OpenOCD,
  probe, power, or HIL execution;
- a new physical diagnostic run, full matrix, evidence deletion, ID reuse,
  acceptance claim, `STATUS.md` edit, commit, push, merge, PR, tag, or release.

Preserve all unrelated dirty worktree content.

## Exact implementation

### 1. Query link quality first

In `scripts/hil/runner.py`, change `LIVE_TAIL_COMMANDS` to this exact order:

```python
LIVE_TAIL_COMMANDS = (
    "bt iso quality",
    "audio status",
    "audio perf",
    "flpr offload",
    "flpr status",
)
```

Do not change `_collect_receiver_tail()` parsing, validation, settle behavior,
or its failure boundary. Link quality remains strict about grammar and expected
slots, but counter values remain evidence only. The change makes the first
bounded shell request after `scored_complete` the CIS-dependent request.

### 2. Drain queued source records before STOP

In `scripts/hil/source_client.py`, add a private no-wait helper used only by
`_bounded_stop()`:

```python
def _drain_queued_records(self):
    while True:
        record, line = self._next_record(0.0, check_cancel=False)
        if line is None:
            return
        if record is not None:
            self._dispatch(record)
```

Call it after `_bounded_stop()` confirms a tracker exists and before it checks
`self._tracker.terminal`. This must:

- consume only already decoded source lines, never add a grace wait;
- append every consumed line to normal source evidence through `_next_record()`;
- preserve normal tracker transition validation;
- preserve failure for malformed, diagnostic, unsolicited, duplicate, or
  otherwise invalid HIL1 records;
- skip STOP when queued teardown plus terminal establish that source has already
  finished; then retain normal bounded `idle` cleanup;
- continue sending bounded STOP and waiting for an abort terminal when no
  terminal is queued.

Do not catch or downgrade errors from this drain. In particular, a queued
`parse-error/unbound` diagnostic remains a cleanup failure.

### 3. Host regression coverage

Update every full fake receiver wire and exact shell-write assertion to use the
new tail order. Preserve all existing transcript payloads and validations.

Add focused regressions that prove public behavior:

1. A normal Mode A fake run retains two valid ISO link-quality records, with
   `bt iso quality` written and recorded before `audio status`.
2. A receiver-tail failure after source `scored_complete`, with valid source
   teardown and PASS terminal already queued, retains failure boundary
   `receiver tail`, has no cleanup failure, records the source terminal, sends
   no `stop`, and sends exactly one bounded `idle` command.
3. A queued source diagnostic still fails cleanup. Do not allow the new drain to
   hide diagnostics.

For regression 2, extend `hil_fakes.SourceTranscript.start()` only if needed
with an opt-in final-status omission. Keep default behavior unchanged. Arrange
the fake source `idle` status response only after the actual cleanup `idle`
write, not in the prequeued terminal stream. A test-local `Wire` subclass is
acceptable for this delayed response. Do not weaken write-order assertions.

### 4. Documentation truth

Update both resume/status documents with concise factual records of
`rh3-20260821-03-modea-depth-fix`:

- immutable direct Mode A diagnostic, not acceptance;
- exact evidence root and `23/23` checksum result;
- strict receiver-tail failure caused by unavailable post-teardown ISO query;
- source timing values `160547`, `164611`, and `165419 ms`;
- cleanup parse-error as an unresolved host-cleanup diagnostic, not a firmware
  root-cause claim;
- persistent high loss: slot 0 `rx_valid=137`, `rx_lost=14310`; slot 1
  `rx_valid=135`, `rx_lost=14249`;
- target-three source image received one physical diagnostic, so remove claims
  that no such execution occurred;
- next physical action stays blocked on this host-only repair and review.

Keep existing image hashes and no-acceptance language. Do not alter historical
evidence statements other than stale total/count wording required by this new
run.

## Verification

Run sequentially from repository root. These commands are host-only and must
not run concurrently with a source build:

```bash
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 -m py_compile \
  scripts/hil/runner.py \
  scripts/hil/source_client.py \
  tests/hil/hil_fakes.py \
  tests/hil/rh2_test.py
```

No hardware command may run. Do not commit.

## Executor return format

Return:

1. files changed and exact behavioral effect;
2. new regression names and what each proves;
3. every verification command and result;
4. final `git status --short` summary;
5. confirmation of no hardware action and no commit;
6. blocker, deviation, or next step, if any.

Stop and escalate instead of guessing if preserving strict HIL1 lifecycle
validation requires a broader redesign.
