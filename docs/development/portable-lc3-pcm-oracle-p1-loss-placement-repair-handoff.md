# PB-031 P1 exact one-CIS-loss placement repair handoff

Status: Approved final P1 repair after exact i386 LC3/FNV placement modeling.

Parent handoff: `docs/development/portable-lc3-pcm-oracle-p1-handoff.md`.

Checkpoint: commit `15282df443e7`, local-only tag
`pb031-p1-diagnostic-baseline-20260915`.

## Goal

Make warning-clean `modea_one_cis_loss_10ms` produce exactly 18 right-channel
concealments after exactly 48 valid post-boundary right pushes. Preserve exact
accepted receiver and transport pins. Replace polling-based pause placement with
an exact send-limit notification; keep completion-counted gap duration.

## Grounded result

Current failed 17-completion candidate produces exact count but wrong placement:

```text
LOSS_SUMMARY count=18 pre_ord=59 pre_r8=B1924E697DF350CF first_ord=60 last_ord=77 recovery_ord=78 recovery_r8=7538E43181B32E80
pre right fixture frame=50 recovery right fixture frame=51
h1=0x134C23B0 lh1=0x32777D65 rh1=0xAE966FB0
```

Exact model in
`docs/development/portable-lc3-pcm-oracle-p1-loss-comparison-handoff.md`
reproduced both candidate and accepted hashes:

```text
51 valid right pushes before gap -> h1=0x134C23B0 rh1=0xAE966FB0
48 valid right pushes before gap -> h1=0x30D6BAF0 rh1=0x9859F1D8
```

Accepted left hash remains `0x32777D65`. Required placement is therefore three
right frames earlier: send fixture frames 0 through 47, conceal 18 pushes, then
resume at fixture frame 48.

Current `wait_for_sends(..., 50)` uses 50 ms polling and reaches right accepted
send count 51. Polling cannot define exact gap placement. `bsim_tx.c` already
enforces an exact send cap under `tx_lock`; expose a one-shot notification for
that existing state transition.

## Scope

Modify implementation only:

- `tests/bsim/client/src/bsim_tx.c`
- `tests/bsim/client/src/bsim_tx.h`
- `tests/bsim/client/src/bsim_client_main.c`

After all validation passes, append factual results only to:

- this handoff;
- `docs/development/portable-lc3-pcm-oracle-p1-handoff.md`;
- `docs/development/portable-lc3-pcm-oracle-p1-loss-diagnostic-handoff.md`;
- `docs/development/portable-lc3-pcm-oracle-p1-loss-repair-handoff.md`;
- `docs/development/portable-lc3-pcm-oracle-p1-loss-comparison-handoff.md`;
- PB-031 `Implementation Notes` only.

## Non-scope

- No production receiver, Mode A assembler, decoder, sink oracle, expected
  count, PCM hash, transport pin, fixture, scenario JSON, parser, controller
  buffer count, warning policy, lifecycle, or final send-count change.
- No wall-clock loss duration, polling-interval tuning, alternate completion
  count, accepted-warning expansion, or retained diagnostic output.
- No P2/P3 threshold work, Intel calibration, hardware, HIL, flash, workflow,
  release, push, PR, remote/local tag action, amend, merge, attribution, stash,
  reset, clean, or destructive action.
- Do not edit PB-031 title, status, priority, type, Description, acceptance
  criteria, or implementation-plan text.

## Exact TX notification API

### State

In private `struct bsim_tx_stream`, add one `struct k_sem send_limit_reached`.
The semaphore reports the existing transition where a successful committed
send reaches nonzero `send_limit` and sets `paused = true`.

In `bsim_tx_register()`, after the slot `memset()` and before publishing
`bap_stream`, initialize this semaphore with initial count 0 and limit 1:

```c
k_sem_init(&s->send_limit_reached, 0, 1);
```

No waiter may survive unregister/re-register. Existing unregister ownership
already removes the stream and drains in-flight TX before slot reuse. Do not add
heap state or a production dependency.

### Signal

In the successful-send commit block, when existing code detects
`send_limit > 0 && send_count >= send_limit`:

1. retain `paused = true`;
2. give `send_limit_reached` exactly once for that cap; and
3. retain all count/hash/sequence commit ordering.

Semaphore max count 1 makes repeated checks idempotent. Giving while `tx_lock`
is held is allowed; waiter rechecks state after waking and may briefly block on
the mutex. Do not print or invoke callbacks.

In `bsim_tx_set_send_limit()` while holding `tx_lock`:

1. reset `send_limit_reached` before installing the new limit;
2. assign the new limit;
3. if the new nonzero limit is already reached, set `paused = true` and give
   the semaphore immediately.

This preserves existing cap semantics and supports changing the right stream
from initial cap 48 to final cap 110 before resume.

### Wait API

Add to `bsim_tx.h`:

```c
int bsim_tx_wait_send_limit(struct bt_bap_stream *bap_stream,
                            uint32_t timeout_ms);
```

Document:

- waits until stream has committed its configured nonzero exact send limit and
  is auto-paused;
- returns `0` on reached cap;
- returns `-EINVAL` for null stream or zero timeout;
- returns `-ENODATA` when stream is not registered or has no nonzero limit;
- returns `-ETIMEDOUT` when semaphore wait expires;
- returns `-ESTALE` if registration generation/stream association or expected
  limit state changed while waiting.

Implementation steps:

1. Validate arguments.
2. Under `tx_lock`, find slot; reject missing/zero-limit state; snapshot slot
   pointer, `generation`, and configured limit.
3. If `send_count >= limit && paused`, return success without consuming stale
   time.
4. Otherwise release lock and take `send_limit_reached` with
   `K_MSEC(timeout_ms)`.
5. Propagate timeout as `-ETIMEDOUT`.
6. Reacquire `tx_lock` and require same stream pointer, generation, configured
   limit, reached count, and paused state. Return `-ESTALE` on mismatch, else
   `0`.

Do not hold `tx_lock` while waiting. Do not use polling or sleep.

Update top-of-file ownership comment to include semaphore initialization,
exact-cap notification, and wait/revalidation behavior.

## Exact scenario repair

In `tests/bsim/client/src/bsim_client_main.c`:

1. Keep:
   - `MODEA_ONE_CIS_LOSS_COUNT 18U`;
   - `MODEA_ONE_CIS_REFILL_LEAD_COMPLETIONS 1U`;
   - completion wait timeout;
   - final 110 sends per stream;
   - warning-clean endpoint-state observation in `start_streams()`.
2. Add:

   ```c
   #define MODEA_ONE_CIS_PRE_GAP_SENDS 48U
   ```

3. Add build assertions:
   - pre-gap sends is nonzero;
   - pre-gap sends is less than final send limit 110;
   - loss count remains greater than refill lead.
4. During one-CIS scenario registration, keep left send limit 110 but set right
   initial send limit to `MODEA_ONE_CIS_PRE_GAP_SENDS`. Do not change any other
   scenario.
5. After `stream_up()`, remove both polling `wait_for_sends(..., 50)` calls.
   Wait with `bsim_tx_wait_send_limit(&streams[1], COMPLETION_WAIT_MS)`.
6. On success, verify `bsim_tx_send_count(&streams[1])` equals exactly 48.
   Treat mismatch as `-ESTALE`; do not continue silently.
7. Right is already auto-paused at exact cap. Retain concise pause output, then
   drain right controller completions to the right send snapshot with existing
   `wait_for_completions(1, right_send_count)`.
8. Snapshot left completion count only after right drain. Wait for:

   ```c
   left_completion_count +
       (MODEA_ONE_CIS_LOSS_COUNT - MODEA_ONE_CIS_REFILL_LEAD_COMPLETIONS)
   ```

9. Before resuming right, always change its send limit to 110. Then call
   `bsim_tx_resume(&streams[1])` immediately. Preserve current behavior that
   unblocks right even after a wait error so scenario teardown cannot leave the
   TX fixture capped.
10. Keep final `wait_for_sends()` calls for both 110-send limits and existing
    teardown margin.
11. Rewrite scenario comment to state:
    - exact send cap places gap after 48 valid right fixture frames;
    - right completion drain ensures already accepted frames finish;
    - 17 left completions plus measured one-completion refill lead produce 18
      peer-observed missing-right events;
    - `.sent` synchronizes fixture state but receiver remains peer truth.

No `LOSS_*` diagnostics remain in final source.

## Verification

Run in order from repository root. Every external evidence root must be new and
empty before use; never delete or overwrite prior evidence.

```bash
nix develop -c python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py
nix develop -c env \
  BSIM_LOG_ROOT=/tmp/opencode/pb031-p1-exact-gap-fix-20260915 \
  bash scripts/bsim-stage1-run.sh
nix develop -c ./scripts/test-all.sh --phase unit
nix develop -c backlog doctor
git diff --check
```

Public-boundary acceptance:

- parser tests `94 PASS / 0 FAIL`;
- matrix 17 scenarios / 26 runs, all strict PASS;
- both loss runs exactly:

  ```text
  pushes1=100 trans1=8 szero1=8 splc1=16 plc1=34 total1=216 derr1=0 mal1=0
  h1=0x30D6BAF0 lh1=0x32777D65 rh1=0x9859F1D8
  txc0=110 txh0=0x8980C79D txc1=110 txh1=0xDD25CC21
  ```

- both loss runs byte-identical in receiver/full/left/right and TX hashes;
- every other scenario pin, count, lifecycle result, and transport audit remains
  unchanged;
- no compiler warning;
- no client/receiver runtime warning or error except scenario 17 receiver exact
  allowlisted `Invalid operation in state: releasing`;
- no `LOSS_SUMMARY`, `LOSS_CLIENT`, `LOSS_PRE`, `LOSS_GAP`, `LOSS_RECOVERY`,
  `LOSS_TX_*`, `LOSS_BOUNDARY`, `LOSS_RX`, or `LOSS_EMIT` output;
- unit phase and `backlog doctor` pass;
- `git diff --check` passes.

## Documentation and commit

Only after every verification passes:

1. Append concise factual results and evidence root to scoped technical docs.
2. Mark earlier refill-lead repair handoff as failed/superseded, without erasing
   its evidence.
3. Add factual implementation/evidence note to PB-031 Implementation Notes.
4. Inspect `git status`, full `git diff`, and `git log --oneline -10`.
5. Stage only:
   - three implementation files;
   - five scoped development handoffs;
   - PB-031 task file.
6. Commit:

   ```text
   test: stabilize one-CIS loss placement
   ```

Do not amend, push, create PR, create/push tag, merge, or add attribution.

Return changed files, exact matrix summary, both loss PASS records, unit/parser
totals, warning scan, `backlog doctor`, evidence root, commit hash/message,
final status, deviations, and blockers.

## Escalation

If exact cap 48 does not produce both accepted count and hashes, any other pin
changes, wait API needs polling, or a warning appears, stop without commit.
Preserve worktree and logs. Do not try another cap/count/duration, repin, relax
oracle/parser, or add diagnostics. Return exact evidence to Delegator.

## Execution result (2026-09-15)

Implemented the per-slot binary exact-cap semaphore and
`bsim_tx_wait_send_limit()`. The one-CIS-loss right stream now starts with an
exact 48-send cap, drains accepted right completions, waits 17 left
pause-window completions, changes its cap to 110, and resumes.

Evidence root:

```text
/tmp/opencode/pb031-p1-exact-gap-fix-20260915
```

Parser tests passed `94 PASS / 0 FAIL`. The strict Stage 1 matrix passed all
17 scenarios and 26 runs. Both `modea_one_cis_loss_10ms` runs were
byte-identical:

```text
pushes1=100 trans1=8 szero1=8 splc1=16 plc1=34 total1=216 derr1=0 mal1=0
h1=0x30D6BAF0 lh1=0x32777D65 rh1=0x9859F1D8
txc0=110 txh0=0x8980C79D txc1=110 txh1=0xDD25CC21
```

No `LOSS_*` output remained. No compiler or non-allowlisted runtime warning
appeared; the only runtime warning was the exact scenario 17 receiver
allowlist, `Invalid operation in state: releasing`. Unit phase passed
`71 PASS / 0 FAIL / 71 TOTAL`; `backlog doctor` and `git diff --check` passed.

## Review correction (2026-09-16)

Commit `63e8181` used a reusable slot-embedded binary semaphore for
`bsim_tx_wait_send_limit()`. Before P1 closure, that notification was replaced
with process-lifetime per-slot condition variables under `tx_lock`: semaphore
reset during a limit change and reinitialization during slot reuse could not
satisfy documented `-ESTALE` semantics for pending waiters. The exact cap 48,
completion count 17, refill lead 1, final cap 110, and accepted one-CIS-loss
records remain unchanged. Review-fix evidence is retained at
`/tmp/opencode/pb031-p1-send-cap-wait-fix-20260916`; parser tests passed
`94 PASS / 0 FAIL`, the strict matrix passed 17 scenarios / 26 runs, and unit
phase passed `71 PASS / 0 FAIL / 71 TOTAL`.
