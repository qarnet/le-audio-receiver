# Refactor R1 review-fix handoff

## Goal

Repair defects found after commit `a79ac71` invalidated R1 acceptance.  Produce
one new implementation/test/metadata commit.  Do not amend or rewrite
`bcd623b`, `4f426f3`, or `a79ac71`.

This handoff covers repair and focused verification only.  Full clean G1 and
both-target G2 happen after Orchestrator reviews repair commit.

## Grounded review findings

1. `src/audio_i2s.c:606-620`: stop owner clears `stop_finalizing` but broadcasts
   only when its caller-count decrement reaches zero.  With one sleeping
   non-owner, owner decrements 2→1 without broadcast; non-owner can sleep
   forever.
2. `src/stream_lifecycle.c:117-127`: idle `stream_lifecycle_force_close()` sets
   `force_closed=true` when no slots are configured.  Future first configured
   lifecycle remains closed because no release/reset boundary clears latch.
3. `tests/unit/audio_i2s_common/test_sink_concurrent.c:42-43,130-132`: plain
   cross-thread bool flags are data races and do not prove non-owner entered
   condition wait before owner completion.
4. `src/audio_i2s.c:151-164`: idempotence check reads `configured` outside
   stream mutex despite R1 contract requiring publication/check at API
   boundaries.
5. Current tests do not deterministically prove an admitted push uses one
   `input_frames` snapshot when setter runs during that push.
6. `tests/bsim/src/audio_sink_stub.c:235-246`: `goal_finalized` success occurs
   before closed-admission check, so late closed push can return 0 instead of
   `-EBUSY`.
7. `src/flpr_protocol.h:221-243`: changed READY epoch resets heartbeat ACK state
   but not `peer->acked`.  If changed-epoch READY_ACK send fails, stale prior
   ACK state can make wait/status report success.
8. `src/flpr_ring_mgr.c:385-413`: repeated/concurrent init reinitializes live
   semaphores and ring headers without `ring_data_lock`.  Public contract says
   repeated calls are safe; shell exposes direct init.
9. `tests/unit/flpr_ring_mgr/src/flpr_ring_mgr_hooks.h:54` declares
   `struct k_sem` without including Zephyr kernel declaration, producing an
   actionable compiler warning.
10. `docs/development/refactor-r1-results.md` reports focused counts that do not
    match one observed suite execution and claims nRF54 receiver-side stream
    evidence absent from `/tmp/r1-g2-54l15-console.log`.  Raw probe/controller
    evidence also was not preserved.  R1 must be marked reopened until fresh
    G1/G2 evidence exists.

## Scope

### Production

- `src/audio_i2s.c`
- `src/stream_lifecycle.c`
- `src/flpr_protocol.h`
- `src/flpr_ring_mgr.c`
- `tests/bsim/src/audio_sink_stub.c`

### Tests and test seams

- `tests/unit/audio_i2s_common/test_sink_concurrent.c`
- `tests/unit/audio_i2s_common/audio_i2s_test_hook.h`
- existing shared fake/helper files only where needed for deterministic
  snapshot assertions
- `tests/unit/lifecycle/src/test_lifecycle.c`
- `tests/unit/flpr_handshake/src/test_flpr_handshake.c`
- `tests/unit/flpr_ring_mgr/src/test_flpr_ring_mgr.c`
- `tests/unit/flpr_ring_mgr/src/flpr_ring_mgr_hooks.h`
- test matrix/coverage descriptions only when new witness names require them

### Status metadata

- `docs/development/refactor-plan.md`
- `docs/development/refactor-r1-results.md`
- `docs/testing/coverage-matrix.md` only for corrected focused counts
- this handoff

## Exact repairs

### 1. Sink stop wakeup

Keep existing owner/cohort algorithm.  After owner relocks and changes
`stop_finalizing` from true to false, call `k_condvar_broadcast()` immediately
while holding `stream_mutex`, independent of `stop_callers`.  Keep existing
last-caller broadcast for `audio_sink_stream_open()` waiters.

Add deterministic regression in both audio-I2S variants:

1. pause admitted push at fake I2S write;
2. start owner stop and one non-owner stop;
3. prove both callers joined cohort and non-owner reached its wait loop;
4. release push;
5. bounded-join push and both stops;
6. assert one reset/PREPARE/DROP finalization and zero callers.

Use test-only lock-protected waiter observability under
`AUDIO_I2S_NATIVE_TEST` (for example a `stop_waiters` counter compiled only in
test builds).  Do not use sleeps as proof.  Production build must gain no test
hook/state.

Replace `open_started_flag`/`open_returned_flag` with semaphores.  Worker gives
an `open_entered` semaphore immediately before calling open and an
`open_returned` semaphore after return.  Assertions use bounded semaphore
takes.  Worker result/context publication is consumed only after completion
semaphore or thread join.

### 2. Sink publication and frame snapshot

At `audio_sink_init()` entry, take `stream_mutex`, read `configured`, and return
idempotent success only after unlocking.  Init remains boot-owned: do not add
parallel first-initialization support or hold mutex across device/dependency
calls.  Publish successful `configured=true` and default closed admission under
`stream_mutex` at function end.  All failed paths leave `configured=false`.

Add deterministic shared test:

1. configure 480-frame input and start push;
2. pause first successful startup write with existing fake-I2S gate;
3. while push remains admitted, set input frames to 360;
4. release and complete push;
5. prove every block from that admitted operation retained 480-frame shape/
   bytes and no overflow/corruption occurred;
6. prove next 360-frame push uses new setting and a 480-frame sample count is
   rejected by exact validation.

Use existing write records and variant-specific expected output helpers.  Do
not add a production pause hook.  Preserve known nRF54 behavior: 360-frame ASRC
still falls back to cpuapp because FLPR accepts only 480.

### 3. Lifecycle idle force close

`stream_lifecycle_force_close()` always closes `audio_path_open`, but sets
`force_closed=true` only when at least one slot is currently configured.  If no
slot is configured, leave/force latch false.  Return remains prior gate-open
state.

Add direct test: reset/idle force-close → configure one slot → start opens.
Retain current configured-slot latch tests unchanged.

### 4. BSim admission order

In stub `audio_sink_push()`, check `accepting` before `goal_finalized`.  Closed
valid pushes return `-EBUSY` non-destructively even after scenario goal.  When
admission remains open, post-goal pushes may retain existing ignored-success
behavior.  Stage 1 hashes/counts must remain unchanged.

### 5. READY epoch ACK state

In `flpr_peer_handle_ready()`, changed/new epoch clears `peer->acked=false`
alongside other new-session tracking.  Same-epoch duplicate READY preserves
current state.

Add production-source handshake regression:

1. complete epoch 42 READY/ACK so `acked=true`;
2. drain new-ready token;
3. make IPC send fail;
4. inject changed epoch 99 READY;
5. assert status epoch 99, reboot count incremented, `acked=false`, send error
   incremented, no new-ready token;
6. assert `flpr_handshake_wait_new_ready(42, K_NO_WAIT)` does not take stale
   fast path;
7. successful duplicate READY for epoch 99 restores ACK and permits expected
   fast-path result.

### 6. Ring-manager init idempotence and serialization

Use `ring_data_lock` for full initialization decision and first-init mutation.
After readiness/ACK check, acquire data lock and inspect `rings_initialized`
under `ring_lock`.  If already initialized, release locks and return 0 without
reinitializing semaphores, handlers, headers, epoch, counters, or queued ring
data.  This is public repeated-call safety and makes shell init during an
active stream a non-destructive no-op.

For first init, while holding `ring_data_lock`:

1. initialize semaphores/control pointers before handler registration;
2. register handlers;
3. initialize both headers;
4. publish `rings_initialized=true` and epoch 0 under `ring_lock`;
5. release data lock.

Never take `ring_lock` across `k_sem_init`, handler registration, or ring-memory
initialization.  Preserve lock order `ring_data_lock` → `ring_lock`.

Strengthen repeated-init test: establish nonzero epoch and ring contents/
indices, call init again, and prove epoch, headers, contents, semaphore state,
and pending data remain unchanged.  Add deterministic init-versus-paused-
produce test: producer holds data lock, repeated init starts, release producer,
both finish, and produced data/epoch survive.  Existing reset barrier remains.

Add `#include <zephyr/kernel.h>` to test hook header so `struct k_sem` is
declared with zero warning.

## Metadata truth during repair

In repair commit:

- change R1 heading in `refactor-plan.md` from ACCEPTED to **REOPENED — repair
  verification pending**;
- change results status similarly;
- add concise review-finding/repair-commit section;
- remove or clearly mark prior focused counts and G2 claims as superseded,
  unaccepted evidence;
- do not invent replacement counts or hardware claims before fresh runs.

Final acceptance metadata comes only after later clean G1 and G2.

## Non-scope

- No R2+ refactor work.
- No sink algorithm redesign, timeout-and-proceed behavior, parallel first-init
  support, FLPR ABI/version change, recovery-policy change, audio/hash repin,
  coverage baseline rewrite, Kconfig/devicetree/board change.
- No hardware flash or G2 in this repair handoff.
- No unrelated warning cleanup.

## Focused verification

Run affected suites from production sources with zero actionable warnings:

- `audio_i2s`
- `audio_i2s_identity`
- `lifecycle`
- `flpr_handshake`
- `flpr_ring_mgr`
- BSim Stage 1 full 16-scenario child

Also run:

```bash
bash -n scripts/test-all.sh
python3 scripts/check-test-matrix.py --repo-root "$PWD"
python3 -m json.tool tests/test-matrix.json >/dev/null
git diff --check
```

Use repository test tooling/Twister commands already used by
`scripts/test-all.sh`.  Record exact per-suite counts from one execution only;
do not sum duplicate platform/execution reports.

## Commit and recap

After focused green, inspect `git status`, `git diff`, and recent log.  Stage
only scoped files and create one new commit, suggested message:

`fix: repair R1 concurrency review findings`

Return:

- files/behavior changed;
- exact focused commands, counts, warnings, and results;
- commit hash/message;
- final git status;
- deviations/blockers.

Do not push, amend, merge, open PR, force-push, flash hardware, weaken tests,
normalize warnings, add attribution, or commit known failures.  After two
materially different failed attempts at one blocker, stop with exact evidence
and one question for Orchestrator.
