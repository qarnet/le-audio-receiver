# System HIL RH1B second production-runtime review fix handoff

Status: focused follow-up. RH1B remains open. First correction build and 49-case
native app suite passed, but review found remaining production blockers. Do not
start RH2 or run hardware.

## Goal

Close remaining RH1B correctness gaps in terminal publication, exact-peer
security, callback filtering, cleanup error ownership, group retry, and command
response propagation. Keep first correction architecture and all accepted
RH0/RH1A behavior.

## Scope

Touch only:

- `hil/source/app/src/hil_source_app.[ch]`
- `hil/source/app/src/hil_source_bap.c`
- `hil/source/app/src/hil_source_output.c` for stale comment correction only
- `tests/unit/hil_source_app/src/fake_hil_source_backend.[ch]`
- `tests/unit/hil_source_app/src/test_hil_source_app.c`
- `tests/unit/hil_source_app/CMakeLists.txt` only if required

Do not change wire field names, RH1A parser/state/signal code, Kconfig, sysbuild,
receiver, BSim, host runner, inventory, build helper, or user-owned
`docs/development/firmware-release-fr4-summary-wait-fix-handoff.md`.

No commit, push, hardware, flashing, serial, Bluetooth host action, sudo,
recovery, destructive command, or full dirty-tree canonical gate.

## Grounded findings

1. `hil_source_bap.c:connected_cb()` gives `sem_connected` even when callback
   connection is not `default_conn`.
2. PASS state is committed before terminal record submission. If submission
   fails, attempt to terminalize FAIL is rejected because snapshot is already
   inactive PASS. Later status can report pass without a terminal record.
3. Group pointer is cleared before `bt_bap_unicast_group_delete()`. Failed
   delete leaks group and prevents retry while falsely reporting group absent.
4. Configure, stop, and unpair handlers discard status-emission errors and
   return success.
5. Discovery endpoint/completion callbacks do not filter exact active
   connection.
6. Security operation does not verify level >= L2 plus exact configured peer
   bond. `bt_conn_set_security()` may return zero as a no-op on bonded reconnect
   without a new callback, causing a false 20-second timeout.
7. Cleanup does not reset/read `op_error` around disable/release. ASCS rejection
   can look like successful completion and lose `-EBADMSG` as first cleanup
   error.
8. Several backend operations copy `default_conn` under spinlock, then call a
   Bluetooth API after unlock without taking a temporary connection reference.
   Disconnect callback may unref concurrently.
9. Existing cleanup-order test checks counts, not cross-phase order.
10. Existing record-submission test starts from scripted backend `-EIO`; it does
    not prove record submission itself becomes first errno. No test covers PASS
    terminal submission failure.

Installed NCS v3.3.0 evidence:

- `bt_conn_le_create()` gives caller one reference; `bt_conn_ref()` is public.
- `bt_conn_get_security()` is public.
- `bt_foreach_bond()` enumerates in-memory key entries. On fresh SC pairing,
  key type is installed before successful `security_changed`; on bonded
  reconnect keys are restored by `settings_load()`.
- `bt_conn_set_security()` can return zero without emitting a callback when
  requested security is already met.

## Required corrections

### 1. Atomic terminal publication

Under `app_mutex`, validate terminal PASS against a copy:

```c
struct hil_source_state terminal = run_state;
err = hil_source_state_terminal(&terminal, HIL_SOURCE_VERDICT_PASS);
```

If validation fails, preserve error and finish FAIL. If validation succeeds,
submit PASS terminal record while real state remains active teardown. Only after
successful queue submission assign `run_state = terminal`. Shell status cannot
observe intermediate state because mutex stays held.

If PASS record format/submit fails, real state is still active teardown. Record
`-EIO`, terminalize real state FAIL, then emit FAIL terminal best effort. Later
status must report `verdict:"fail"`, never pass. Do not emit PASS before
validation or commit PASS before queue acceptance.

Add native regression that fills output queue immediately before a normal run
would publish PASS terminal. After freeing queue, status must show fail with
first errno `-EIO`; no captured PASS terminal is allowed.

Add separate regression where a worker state-record submission fails without a
pre-scripted backend error. Output failure itself must become first errno
`-EIO`, followed by cleanup and terminal fail once output resumes.

### 2. Exact-peer security and bonded reconnect

Add backend op:

```c
bool (*peer_bonded)(const bt_addr_le_t *peer);
```

Production implementation enumerates `bt_foreach_bond(BT_ID_DEFAULT, ...)` and
uses `bt_addr_le_eq()` to match exact configured peer, including address type.
No broad bond-count substitute.

`bap_kick_security()`:

1. take temporary `bt_conn_ref(default_conn)` while backend lock is held;
2. clear current operation error;
3. call `bt_conn_set_security(ref, BT_SECURITY_L2)` after unlock;
4. if return is zero and `bt_conn_get_security(ref) >= BT_SECURITY_L2`, give
   `sem_security` to cover already-secure bonded reconnect;
5. unref temporary reference;
6. return API result.

Duplicate semaphore give from a racing `security_changed` is harmless because
semaphore max is one. Fresh pairing waits for callback.

After security wait, coordinator requires all:

- operation error zero;
- backend security error zero;
- security level >= `BT_SECURITY_L2`;
- `peer_bonded(&run_addr_le)` true.

Otherwise fail `-EACCES` before discovery. Fake defaults exact bond true and can
script false. Add regressions for missing bond and already-secure/no-callback
completion shape.

### 3. Exact connection callbacks

For `connected_cb`, only active expected connection may update state or signal
`sem_connected`. Wrong connection records first `-EIO` but does not unref the
backend-owned expected connection. Wake connect wait so failure is prompt only
when a connect operation is pending; simplest accepted implementation may give
`sem_connected` after storing wrong-connection error.

For `endpoint_cb` and `discover_cb`, compare callback connection with
`default_conn` under backend lock. Wrong connection records first `-EIO`; do not
store endpoint. Discovery completion may signal wait after recording error so
worker exits promptly. Do not use `ARG_UNUSED(conn)`.

Unknown stream callbacks keep fail-closed first-error behavior. Do not call app
sent handler for unknown stream.

### 4. Safe temporary connection ownership

Whenever an operation uses `default_conn` after releasing backend lock, take a
temporary `bt_conn_ref()` under lock and `bt_conn_unref()` after API call. Apply
to security, discovery, configure, QoS, and disconnect. Copy endpoint pointers
needed by configure while locked. Never hold spinlock across Bluetooth API.

Do not add a second long-lived connection owner. Existing create/disconnect
reference remains backend-owned and released once by async failure,
disconnected callback, or final `conn_unref` fallback.

### 5. Cleanup ASCS error ownership

At each `kick_disable(stream_idx)` and `kick_release(stream_idx)`, clear
`op_error` before issuing operation. No-op paths may give completion token and
leave error zero.

After each cleanup semaphore wait, coordinator reads `op_error`. If nonzero and
no earlier cleanup error exists, preserve it as first cleanup error. Continue
remaining cleanup operations. Reset next operation error only at next kick.

ASCS rejection listener stores `-EBADMSG` and signals once. Endpoint success
callback remains sole success token. Tests must prove disable and release
rejection become first cleanup errno while later release/disconnect/reset are
still attempted.

### 6. Retryable group delete

`bap_kick_group_delete()` copies group pointer under lock, calls delete after
unlock, and clears `unicast_group` only when delete returns zero. On error retain
pointer so abort cleanup or later idle can retry. Protect clear with equality
check under lock.

Fake group-delete behavior must mirror production: clear scripted group only on
success. Add public coordinator regression: first group delete fails, terminal
is fail, later idle after scripted recovery retries delete and succeeds; status
must not falsely claim absent group before successful retry.

### 7. Command response propagation

Every `hil_app_emit_status_data()` call must have checked return. For configure,
start rejection, status, stop, unpair, idle, and all rejection branches:

- preserve semantic command error when emission succeeds;
- return formatter/submit error when emission fails;
- when run active, runtime error remains set so worker aborts;
- never return zero after response emission failed.

Add focused tests for successful semantic rejection and one queue-full accepted
command response, proving return value is `-EIO` rather than success. Do not
expect shell to print handler return value.

### 8. Test strength and comments

- Strengthen cleanup ledger assertion to enforce global phase order:
  `TX_STOP -> all DISABLE -> all RELEASE -> DISCONNECT -> GROUP_DELETE ->
  optional CONN_UNREF -> RESET_SEGMENT`. Allow operations absent when resources
  were never acquired, but never allow phase regression.
- Stabilize idle completion-race test by blocking later lifecycle kicks so it
  cannot depend on worker/ztest priority.
- In max-ID status test assert captured complete line length is <=
  `HIL_SOURCE_OUTPUT_LINE_SIZE`.
- Correct `hil_source_output.c` header comment from 768 to 1024 bytes.
- Remove dead `tx_generation` if no generation value is used. Do not retain a
  write-only pseudo-guard. Current `tx_active` plus teardown state remains sent
  callback guard for RH1B.

## Verification

Run:

```bash
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_app -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-app-review-fix2-twister
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_control -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-control-rh1b-review-fix2-twister
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_signal -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-signal-rh1b-review-fix2-twister
nix develop --command fw-build-hil-source
nix develop --command python3 -W error scripts/test_hil_runner.py
python3 scripts/test_inventory.py --count
python3 scripts/test_inventory.py --json
python3 -m compileall -q scripts/hil scripts/test_hil_runner.py scripts/generate_hil_source_sine_lut.py
git diff --check
git status --short
```

Inspect resolved app/CPUNET configs and every build diagnostic again. Inventory
expected 66; adding cases inside existing suite does not change prospective
child count.

## Escalation and recap

Stop after two materially different failed attempts, contradictory NCS
behavior, unexplained warning, architecture need beyond this handoff, scope
expansion, or test weakening. Preserve work and return blocker, attempts,
commands/logs, current status, one question, and smallest hypothesis.

On success return exact files, public behavior, test/build counts, resolved
config facts, diagnostics, deviations, blockers, and explicit no-commit status.
