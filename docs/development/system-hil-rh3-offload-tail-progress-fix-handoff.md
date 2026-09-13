# RH3 FLPR offload-tail progress fix handoff

Status: proposed software-only host-tool repair. This phase corrects a
false-negative receiver-tail verdict exposed by RH3-11. It does not authorize
hardware execution, a build, flash, reset, pairing, radio change, or a new
RH3 matrix.

## Goal

Keep strict healthy-row FLPR validation while distinguishing one genuinely
live serialized ASRC transaction from a stuck transaction. A 10 ms healthy
row may pass only when its final `flpr offload` evidence is either fully
settled (`submit == success`) or proves forward progress through a bounded
sequence of one-pending snapshots.

## Grounding

RH3-11 Mode A retained this receiver-tail evidence:

```text
submit=14039 success=14038 fallback=0 busy=0
submit=14069 success=14068 fallback=0 busy=0
submit=14094 success=14093 fallback=0 busy=0
```

All snapshots were `ACTIVE`, with zero FLPR fault, recovery, fallback, and
busy counters. `success` advanced by 30 and then 25, so a distinct pending
transaction was completing between snapshots. The exact evidence is retained
at:

```text
/tmp/opencode/hil-runs/rh3-20260820-03.children.a62d866f712c/
  rh3-20260820-03.p1.r2.rh3.fresh_mode_a_48_4_1.f92e84021d7c/
    receiver-status.txt
```

The production source establishes why one pending transaction is a normal
observation:

- `src/audio_offload.c:1085` increments generic `submit_count` in
  `asrc_precheck()` before the FLPR round trip completes.
- `src/audio_offload.c:1379` increments `success_count` only at
  `asrc_commit()` after the round trip and final lifecycle guard.
- `src/audio_offload.c:103` defines `g_submit_lock`; the successful FLPR
  round trip holds that mutex, so later success progress proves an earlier
  pending transaction was not stuck.
- `src/audio_offload.c:64` bounds one FLPR wait at 8 ms. A stuck transaction
  must fault/recover rather than remain a healthy moving pending snapshot.

Current host behavior already recognizes `submit == success + 1` in
`scripts/hil/runner.py:_offload_counters_are_single_pending()`, but
`_settle_receiver_offload()` retries only until equality. RH3-11 showed that
host command timing can repeatedly sample the moving pending window.

This is not permission to accept warnings, faults, recovery, fallback,
multiple outstanding submissions, or static counter mismatch. The real RH3-11
ACL establishment warning and 99 percent PLC evidence remain separate
unresolved transport findings.

## In scope

1. Change host-only tail-settle logic in `scripts/hil/runner.py`.
2. Change host-only receiver-tail validation in `scripts/hil/receiver.py`.
3. Add focused fake-lab regression coverage in `tests/hil/rh2_test.py`.
4. Record this implementation contract in this handoff only.

## Out of scope

- `src/audio_offload.c`, `src/audio_offload.h`, FLPR firmware, shell grammar,
  parser grammar, Kconfig, I2S, PLC, timing, source fixture, BAP, radio, and
  Bluetooth controller changes.
- Any warning-scanner exception, threshold relaxation, fault-row relaxation,
  or static-pending acceptance.
- PLC/received-SDU fixture limits. `docs/development/system-hil-milestones.md`
  requires frozen limits, but current `scripts/hil/rows.py` has none. Do not
  invent values in this phase.
- HIL execution, source or receiver build, flash, serial access, reset,
  pairing, OpenOCD, `serial-mcp`, or manual hardware work.
- Edits to `STATUS.md`, existing evidence, image hashes, matrix IDs, public
  documentation, or unrelated dirty files.
- Commit, push, merge, tag, release, or PR creation.

## Exact implementation

### 1. Preserve bounded settle window, add progress proof

In `scripts/hil/runner.py`:

1. Keep `RECEIVER_OFFLOAD_SETTLE_TIMEOUT = 0.5` and
   `RECEIVER_OFFLOAD_SETTLE_POLL_INTERVAL = 0.05` unchanged.
2. Keep `_offload_counters_are_equal()` and
   `_offload_counters_are_single_pending()` strict:
   - equal means nonnegative `submit == success`;
   - single pending means nonnegative `submit == success + 1`.
3. Change `_settle_receiver_offload()` to return both final parsed offload
   status and explicit settle evidence. Use exactly these outcome strings:
   - `"equal"` when initial or retry snapshot reaches equality;
   - `"moving_single_pending"` only when initial snapshot and a later retry
     are both single-pending and both `submit` and `success` strictly increase;
   - `"unproven"` when the bounded loop ends without either proof.
4. Settle evidence must include `outcome`, integer `retries`, and the initial
   and final `submit`/`success` values. Do not copy full mutable status dicts
   into this compact record.
5. Return immediately for an equal initial snapshot. For a non-single-pending
   initial snapshot, return `unproven` without a retry. For an initial
   single-pending snapshot, issue the existing bounded `flpr offload` retries.
6. During retries:
   - equality succeeds immediately with outcome `equal`;
   - a later single-pending snapshot succeeds only when both counters are
     greater than initial values, with outcome `moving_single_pending`;
   - any other snapshot remains unproven and continues only until current
     deadline logic expires.
7. Preserve cancellation checks, raw transcript recording, and named
   recovery-row bypass exactly. Do not issue a different shell command.

The proof is deliberately stronger than accepting one `+1` snapshot: a static
`151/150` fake remains a hard failure, while `151/150` followed by `181/180`
proves completed FLPR work under the serialized mutex.

### 2. Keep receiver validator strict by default

In `scripts/hil/receiver.py`:

1. Add keyword-only or ordinary optional parameter
   `allow_moving_single_pending=False` to `validate_receiver_blocks()`.
2. For a healthy `48_4_1` row with no recovery evidence, accept generic
   counters only when either:
   - `submit == success`; or
   - `allow_moving_single_pending` is true and `submit == success + 1`.
3. Preserve current error text `offload submit/success mismatch` for every
   other mismatch. Do not accept negative, missing, `+2`, or larger gaps.
4. Preserve every existing zero requirement for state, fallback, busy,
   recovery, probation, faults, and handshake counters.
5. Preserve 7.5 ms and named recovery validation unchanged. The new flag is
   only supplied by the runner after its moving-progress proof.

### 3. Wire auditable result evidence

In `scripts/hil/runner.py` `_collect_receiver_tail()`:

1. Destructure the status and settle evidence returned by
   `_settle_receiver_offload()`.
2. Pass `allow_moving_single_pending=True` only when settle outcome equals
   `"moving_single_pending"`.
3. On successful validation, retain settle evidence under stable key
   `offload_settle` beside existing `offload` in returned receiver-tail
   summary. This makes a permitted pending result reviewable from
   `summary.json`; raw `receiver-status.txt` remains complete transcript
   evidence.
4. Do not alter result schema version, run-ID logic, image evidence, log scan,
   source terminal validation, or matrix schedule.

### 4. Regression tests

In `tests/hil/rh2_test.py`:

1. Keep `test_healthy_10ms_tail_settles_one_live_flpr_submit`. It must still
   pass when retry reaches equality and record `offload_settle.outcome ==
   "equal"`.
2. Keep `test_healthy_10ms_tail_persistent_live_submit_fails_bounded`. It must
   still fail after bounded retries when every snapshot stays `151/150`; it
   must not gain an exemption.
3. Add one fake-lab runner test with initial `151/150` and later `181/180`.
   Assert public result is passed, receiver transcript contains one settle
   retry, final summary preserves `181/180`, and
   `offload_settle.outcome == "moving_single_pending"`.
4. Add direct receiver-validator coverage proving a `+1` mismatch fails with
   default arguments and succeeds only with
   `allow_moving_single_pending=True`, while a `+2` mismatch still fails even
   with that flag.
5. Update fake transcript helpers only if needed for test readability. Keep
   their default shell grammar unchanged because firmware output is untouched.

## Verification

Run from repository root, sequentially, inside existing NCS v3.3.0 dev shell:

```bash
nix develop --command python3 tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/runner.py scripts/hil/receiver.py tests/hil/rh2_test.py
git diff --check
git status --short
```

Expected: all fake-lab tests pass; Python files compile; whitespace check is
clean; unrelated dirty files remain untouched. Do not run any HIL, build,
flash, or hardware command as part of verification.

## Executor rules

Implement only this handoff. Preserve unrelated dirty changes. Stop and report
before expanding scope if tests require firmware output changes, if validation
would need a scanner exception, if static-pending behavior cannot remain a
failure, or if the 99 percent PLC/radio issue appears to require a decision.
Do not commit because user did not request a commit. Return changed files,
exact test results, `git diff --check` result, final `git status --short`,
deviations, and blockers.
