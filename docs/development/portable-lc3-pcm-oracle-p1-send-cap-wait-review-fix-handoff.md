# PB-031 P1 send-cap wait lifecycle review-fix handoff

Status: Approved review fix after commit `63e818194f3cccb0d6467b8cb54155ebde99c135`.

Parent implementation handoff:
`docs/development/portable-lc3-pcm-oracle-p1-loss-placement-repair-handoff.md`.

## Goal

Preserve exact accepted one-CIS-loss behavior while making
`bsim_tx_wait_send_limit()` satisfy its documented stale-state contract across
send-limit changes and unregister/re-register slot reuse.

## Reviewed defect

Commit `63e8181` embeds `struct k_sem send_limit_reached` inside reusable
`struct bsim_tx_stream`. `bsim_tx_set_send_limit()` resets the semaphore, and
`bsim_tx_register()` clears/reinitializes it during slot reuse.

Installed Zephyr v4.3.99 documents and implements:

- `k_sem_reset()` aborts a pending `k_sem_take()` with `-EAGAIN`;
- current wait implementation maps every nonzero take result to `-ETIMEDOUT`,
  so a concurrent limit change violates documented `-ESTALE` behavior;
- unregister does not wake the waiter, and re-register can clear/reinitialize
  the embedded semaphore while that waiter remains pending.

Current one-CIS scenario does not trigger this race. Commit `63e8181` passed all
17 scenarios / 26 runs. This is an API lifecycle-contract review defect, not an
accepted behavior failure.

## Decided repair

Replace per-slot semaphore with stable per-slot condition-variable storage.
Use existing `tx_lock` as the condition mutex. Condition wait atomically
releases/reacquires `tx_lock`, removing the state-check-to-wait lost-wakeup
window. All state changes broadcast while holding `tx_lock`; waiter revalidates
the complete snapshot after wake.

## Scope

Modify implementation:

- `tests/bsim/client/src/bsim_tx.c`
- `tests/bsim/client/src/bsim_tx.h`, documentation wording only if needed

Update factual current-state text only:

- this handoff;
- `docs/development/portable-lc3-pcm-oracle-p1-loss-placement-repair-handoff.md`;
- `docs/development/portable-lc3-pcm-oracle-p1-loss-comparison-handoff.md`;
- PB-031 `Implementation Notes` only.

## Non-scope

- No scenario timing, cap value 48, completion count 17, refill lead 1, final
  limit 110, receiver/oracle/hash/transport pin, fixture, parser, JSON, warning
  policy, production source, or matrix topology change.
- No new diagnostic output, hidden sleep, polling, spin, heap allocation, or
  callback.
- No P2/P3, Intel calibration, hardware, HIL, workflow, release, push, PR, tag,
  amend, merge, attribution, stash, reset, clean, or destructive action.
- Do not edit product-owned PB-031 fields outside Implementation Notes.

## Exact implementation

### Stable condition-variable storage

In `bsim_tx.c`:

1. Remove `struct k_sem send_limit_reached` from `struct bsim_tx_stream`.
2. Add stable file-static storage parallel to `tx_streams`:

   ```c
   static struct k_condvar send_limit_changed[BSIM_TX_MAX_STREAMS];
   ```

3. In the existing one-time branch of `bsim_tx_init()`, initialize every
   condition variable with `k_condvar_init()` before creating the TX thread.
   If any initialization fails, return that error and do not start the thread.
4. Never clear or reinitialize a condition variable from register/unregister.
   Its lifetime is the process lifetime, independent of reusable slot fields.

### Broadcast state changes

All calls below occur while `tx_lock` is held. Ignore no return silently:
`k_condvar_broadcast()` returns a nonnegative woken-thread count. It has no
expected failure for valid initialized kernel objects; use `(void)` cast where
the count is intentionally unused.

Broadcast `send_limit_changed[i]`:

1. In successful-send commit, immediately after exact cap sets
   `s->paused = true`.
2. In `bsim_tx_set_send_limit()`, after assigning the new limit and applying
   existing already-reached auto-pause semantics. Broadcast on every limit
   change, whether or not new limit is already reached.
3. In `bsim_tx_unregister()`, after clearing `bap_stream` and bumping
   generation, before unlocking. Compute slot index while association is still
   known.
4. In `bsim_tx_register()`, after fully initializing slot state and publishing
   `bap_stream`, before unlocking. This is defensive for rapid slot reuse; old
   waiters revalidate generation/association, new waiters snapshot only after
   registration returns.

Remove all `k_sem_init()`, `k_sem_reset()`, `k_sem_give()`, and `k_sem_take()`
operations associated with send-limit notification.

### Wait implementation

Keep public signature and documented return values:

```c
int bsim_tx_wait_send_limit(struct bt_bap_stream *bap_stream,
                            uint32_t timeout_ms);
```

Implement under one mutex/condition protocol:

1. Return `-EINVAL` for null stream or zero timeout.
2. Lock `tx_lock`; look up stream. Return `-ENODATA` after unlocking when
   absent or current limit is zero.
3. Snapshot slot index, generation, and nonzero limit.
4. If current count has reached snapshot limit and stream is paused, unlock and
   return `0`.
5. Call:

   ```c
   k_condvar_wait(&send_limit_changed[index], &tx_lock,
                  K_MSEC(timeout_ms));
   ```

   The call atomically releases and reacquires `tx_lock`.
6. If wait returns nonzero, unlock and return `-ETIMEDOUT`.
7. With lock reacquired, fetch `tx_streams[index]` again. Return `-ESTALE`
   after unlocking if any changed:
   - `bap_stream` association;
   - generation;
   - configured limit.
8. Return `0` only if count reached snapshot limit and stream is paused.
   Otherwise return `-ESTALE`. Every broadcast source either satisfies cap or
   changes expected state, so no polling/repeated wait is needed.
9. Unlock on every return path.

Do not retain a pointer to mutable slot state across unlocked execution. Slot
index and external condition-variable address remain stable.

Update `bsim_tx.c` ownership comment to describe process-lifetime per-slot
condition variables, broadcasts under `tx_lock`, atomic wait release, and full
post-wake revalidation. Update `bsim_tx.h` wording from semaphore-specific to
notification/wait timeout wording while preserving behavior and return values.

## Tests and acceptance

No standalone `bsim_tx` unit harness exists. Do not create a private-field test
or mock away Zephyr synchronization. Public acceptance remains real encoded BAP
traffic through all matrix clients; source review verifies condition-variable
lifetime and state-transition semantics.

Run from repository root in order, using new external evidence root:

```bash
nix develop -c python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py
nix develop -c env \
  BSIM_LOG_ROOT=/tmp/opencode/pb031-p1-send-cap-wait-fix-20260916 \
  bash scripts/bsim-stage1-run.sh
nix develop -c ./scripts/test-all.sh --phase unit
nix develop -c backlog doctor
git diff --check
```

Acceptance:

- parser `94 PASS / 0 FAIL`;
- matrix 17 scenarios / 26 runs, all strict PASS;
- both loss runs remain byte-identical with:

  ```text
  pushes1=100 trans1=8 szero1=8 splc1=16 plc1=34 total1=216 derr1=0 mal1=0
  h1=0x30D6BAF0 lh1=0x32777D65 rh1=0x9859F1D8
  txc0=110 txh0=0x8980C79D txc1=110 txh1=0xDD25CC21
  ```

- every other scenario pin/count/lifecycle/transport result unchanged;
- no compiler warning;
- no client/receiver runtime warning or error except exact allowed scenario 17
  receiver `Invalid operation in state: releasing`;
- unit phase, `backlog doctor`, and diff check pass;
- no send-limit semaphore operation remains;
- current factual docs say condition-variable notification, while historical
  approved handoff design remains retained and gets a concise review-correction
  note rather than erased history.

## Documentation and commit

After all gates pass:

1. Append a review-correction result to parent placement-repair handoff,
   explaining semaphore implementation from `63e8181` was replaced before P1
   closure because reset/reuse did not satisfy stale-state contract.
2. Change current resolution wording in comparison handoff and PB-031
   Implementation Notes from binary semaphore to exact send-cap
   condition-variable notification. Preserve all evidence paths and outcomes.
3. Append exact command results to this handoff.
4. Inspect status, full diff, and recent log. Stage only scoped files.
5. Commit as a new commit, never amend:

   ```text
   test: harden send-cap wait lifecycle
   ```

Do not push, create PR, tag, amend, merge, or add attribution.

Return changed files, exact gate totals, both loss records, warning scan,
evidence root, commit hash/message, final status, deviations, and blockers.

## Escalation

Stop without commit if condition-variable API is unavailable in installed NCS,
matrix behavior/pins change, warning appears, synchronization requires polling
or sleep, or scope must expand. Preserve worktree and evidence. Do not weaken
the `-ESTALE` contract or one-CIS acceptance.

## Execution result (2026-09-16)

Replaced reusable slot-embedded send-limit semaphore state with
process-lifetime per-slot condition variables. Every send-limit cap, limit
change, unregister, and register state transition broadcasts while holding
`tx_lock`; waiters atomically release/reacquire that mutex and fully
revalidate association, generation, limit, count, and pause state.

Evidence root:

```text
/tmp/opencode/pb031-p1-send-cap-wait-fix-20260916
```

Command results:

- `nix develop -c python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py`:
  `94 PASS / 0 FAIL`.
- `nix develop -c env BSIM_LOG_ROOT=/tmp/opencode/pb031-p1-send-cap-wait-fix-20260916 bash scripts/bsim-stage1-run.sh`:
  strict PASS, 17 scenarios / 26 runs.
- `nix develop -c ./scripts/test-all.sh --phase unit`:
  `71 PASS / 0 FAIL / 71 TOTAL`.
- `nix develop -c backlog doctor`: passed, no duplicate IDs,
  self-referential dependencies, or dependency cycles.
- `git diff --check`: passed.

Both `modea_one_cis_loss_10ms` runs were byte-identical:

```text
pushes1=100 trans1=8 szero1=8 splc1=16 plc1=34 total1=216 derr1=0 mal1=0
h1=0x30D6BAF0 lh1=0x32777D65 rh1=0x9859F1D8
txc0=110 txh0=0x8980C79D txc1=110 txh1=0xDD25CC21
```

No compiler warning appeared. Runtime warning scan found only scenario 17
receiver `Invalid operation in state: releasing`. Source review found no
send-limit `k_sem_init()`, `k_sem_reset()`, `k_sem_give()`, or `k_sem_take()`
operation.
