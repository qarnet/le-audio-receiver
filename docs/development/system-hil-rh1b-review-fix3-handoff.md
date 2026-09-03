# System HIL RH1B third production-runtime review fix handoff

Status: final focused follow-up. RH1B remains open. Second correction passed 56
native app cases and firmware build, but direct review found one bonded-reconnect
production failure plus one start-error deadlock and response propagation gaps.

## Goal

Fix exact remaining defects without changing RH1 protocol, lifecycle shape, or
scope. Then rerun full RH1B verification for acceptance review.

## Scope

Touch only:

- `hil/source/app/src/hil_source_app.c`
- `hil/source/app/src/hil_source_bap.c`
- `tests/unit/hil_source_app/src/fake_hil_source_backend.[ch]`
- `tests/unit/hil_source_app/src/test_hil_source_app.c`

No other code/docs/config. Preserve user-owned FR4 handoff and all unrelated
uncommitted work. No commit, push, hardware, flash, serial, Bluetooth host,
sudo, recovery, destructive action, or full dirty-tree canonical gate.

## Required fixes

### 1. Production already-secure state must update status gate

Current `bap_kick_security()` detects `bt_conn_get_security(ref) >= L2` and
gives `sem_security`, but it does not update `security_level_now`. Segment reset
sets that field to zero. If bonded reconnect gets no `security_changed`
callback, coordinator wakes then rejects level zero. Native fake test misses
this because fake security level defaults to two.

After `bt_conn_set_security()` returns zero, read `bt_conn_get_security(ref)`.
When level >= L2, under backend lock set:

- `security_level_now = level`;
- `security_error_now = 0`;

then give `sem_security`. Keep callback path and exact bond gate unchanged.

Strengthen fake already-secure test so security level starts zero and
`fake_set_security_auto_complete(true)` updates it to L2 as production does.
Test must fail if auto-complete only gives semaphore without updating level.

### 2. Remove recursive lock on start ACK failure

`hil_source_app_dispatch()` holds `app_mutex` while calling
`hil_app_handle_start()`. ACK failure path currently calls
`k_mutex_lock(&app_mutex, ...)` again, causing permanent deadlock when queue is
full or formatter fails.

Keep handler contract: caller holds app mutex. In ACK failure path, mutate
state and best-effort emit abort/terminal without any nested app lock. Leave
run as immutable terminal FAIL with first errno `-EIO`; do not wake worker.

Add regression:

1. configure successfully;
2. fill output queue;
3. dispatch start directly;
4. it returns `-EIO` within bounded test time, no worker connect/TX starts;
5. drain queue, query status, observe aborted error + verdict fail + first errno
   `-EIO`;
6. idle resets successfully.

Test must exercise real `hil_source_app_dispatch`, not private helper.

### 3. Check idle failure-response emission

Two idle branches still cast status emission to void:

- terminal wait timeout;
- post-run stray cleanup failure.

For each branch, call `hil_app_emit_status_data()` once and preserve return:

- if emission fails, return emission error (`-EIO`/formatter error);
- otherwise return original timeout/cleanup error.

Do not reset active state or start concurrent cleanup. Add or extend queue-full
idle test to prove failed idle response cannot be silently dropped while
returning unrelated success/error. Keep semantic behavior when emission works.

### 4. Lock parse-error active-state mutation

Parse failures are handled before dispatch takes `app_mutex`, but
`hil_app_emit_parse_error()` and `hil_app_emit_record_line()` may read/write
`run_state.active`/`runtime_error`. Fix by taking `app_mutex` around parse-error
emission in `hil_source_app_dispatch()`. Do not add locking inside generic
emit helpers and do not recursively lock normal command paths.

Add active-run malformed-command regression: parse-error record emits under
reserved IDs without mutating lifecycle when output succeeds. When output is
full, dispatch returns `-EIO`, runtime error causes worker error-abort and
terminal fail after queue resumes.

### 5. Never unref temporary connection while spinlock held

`bap_kick_configure()` missing-endpoint branch and `bap_kick_qos()` existing-
group branch call `bt_conn_unref(ref)` before releasing `backend_lock`, contrary
to backend ownership rule.

Restructure both branches:

- record local error/condition while locked;
- unlock;
- unref temporary connection if non-NULL;
- return.

No Bluetooth API or reference operation while backend spinlock is held, except
the intentional atomic `bt_conn_ref()` acquisition that makes post-unlock use
safe. Keep all existing ref balance.

### 6. Comments and focused review

- Change cleanup comment from `generation-guarded` to actual `idempotent,
  TX-state-guarded`; dead generation variable was already removed.
- Inspect all `hil_app_emit_status_data()` calls after edits. No cast-to-void is
  allowed in command handlers. Worker terminal/abort best-effort record helpers
  remain separate.
- Inspect all `bt_conn_unref()` sites. None may execute while backend spinlock
  is held.

## Verification

Run:

```bash
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_app -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-app-review-fix3-twister
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_control -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-control-rh1b-review-fix3-twister
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_signal -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-signal-rh1b-review-fix3-twister
nix develop --command fw-build-hil-source
nix develop --command python3 -W error scripts/test_hil_runner.py
python3 scripts/test_inventory.py --count
python3 scripts/test_inventory.py --json
python3 -m compileall -q scripts/hil scripts/test_hil_runner.py scripts/generate_hil_source_sine_lut.py
git diff --check
git status --short
```

Inspect resolved app/CPUNET configs and every diagnostic again. Expected
inventory remains 66.

## Escalation and recap

Stop after two failed approaches, NCS contradiction, unexplained warning,
missing design, scope expansion, or test weakening. Preserve work and return
evidence plus one precise question.

Success recap: exact files, behavior, tests/counts, config facts, diagnostics,
deviations, blockers, and explicit no-commit status.
