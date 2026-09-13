# System HIL RH1B production-runtime review fix handoff

Status: focused correction handoff. RH1B remains open. Do not start RH2 or run
hardware. This handoff supersedes conflicting RH1B details only where stated.

## Goal

Make RH1B source firmware production path capable of one bounded, lossless
mono/Mode A/Mode B run and reconnect segment. Current native fake path passes,
but real firmware cannot transmit because TX streams are never attached.
Callback completion, cleanup, output-size, stack, and concurrency defects also
block RH1B acceptance.

## Scope

Touch only:

- `hil/source/app/src/hil_source_app.[ch]`
- `hil/source/app/src/hil_source_bap.c`
- `hil/source/app/src/hil_source_tx.[ch]`
- `hil/source/app/src/hil_source_output.[ch]`
- `hil/source/app/prj.conf`
- `tests/unit/hil_source_app/src/fake_hil_source_backend.[ch]`
- `tests/unit/hil_source_app/src/test_hil_source_app.c`
- `tests/unit/hil_source_app/CMakeLists.txt` only if needed
- `docs/development/system-hil-milestones.md` only for a factual contract
  correction that cannot remain in this handoff

Preserve all RH0/RH1A wire semantics, source signal generation, receiver code,
BSim, build helpers, inventory, and user-owned
`docs/development/firmware-release-fr4-summary-wait-fix-handoff.md`.

Do not commit, push, flash, open serial, use Bluetooth hardware, run sudo, or
run the full dirty-tree canonical gate.

## Grounded defects

1. `hil_source_tx_attach()` has no caller. `tx_stream_count` remains zero, so
   first production `hil_source_tx_send()` returns `-EINVAL`. Cleanup also sets
   `tx_stopped`, with no segment reactivation.
2. `hil_source_app.c` has raw `printk("DBG TS ...")` in every sent callback.
   This races queued HIL1 output and violates single-writer ownership.
3. Resolved `build/hil-source/app/zephyr/.config` has
   `CONFIG_SHELL_STACK_SIZE=2048`. Dispatch nests strict-parser state plus
   status/hello buffers beyond this budget.
4. Output line capacity 768 cannot honor accepted 63-byte command IDs and
   64-byte run IDs. A normal two-stream status record already exceeds 768 with
   maximum IDs. Formatter currently fails instead of emitting required status.
5. `CONFIG_NCS_BOOT_BANNER=y` remains resolved, creating non-HIL1 serial text.
6. Mode A uses one global TX activity time. Healthy callbacks on one CIS can
   mask a stuck outstanding queue on the other forever.
7. `sem_configured`, `sem_disabled`, and `sem_released` are signaled by ASCS
   response listeners rather than successful endpoint-state callbacks. Release
   currently receives both listener and `stream_ops.released` signals, so one
   stream can consume another stream's completion token.
8. Sink ASEs are server-started. NCS v3.3.0 upstream sample waits
   `stream_ops.started`; it does not call `bt_bap_stream_start()` for a sink.
   Current code swallows `-EINVAL`/`-EBADMSG` as success.
9. Backend callback/status fields are unsynchronized despite Bluetooth RX,
   worker, and shell thread access.
10. Stop-aware wait helper short-circuits cleanup waits after stop. Cleanup can
    reset backend state and return terminal while disable/release/disconnect are
    still pending.
11. `idle` resets `sem_run_done` after requesting stop. A worker completion in
    that race is lost, causing a 15-second stall. On timeout it also starts a
    second cleanup concurrently with worker cleanup.
12. Worker state/record helpers are often called without `app_mutex`, racing
    status dispatch. Record submission failure jumps to abort with `err == 0`,
    losing first errno.
13. `hil_source_bap_init()` ignores return values from both auth callback
    registration functions.

NCS v3.3.0 evidence:

- `zephyr/samples/bluetooth/bap_unicast_client/src/main.c`: `start_streams()`
  skips `bt_bap_stream_start()` for sink ASEs and waits `sem_stream_started`;
  `stream_started()` registers TX-capable streams.
- `zephyr/subsys/bluetooth/audio/bap_unicast_client.c`: successful ASCS response
  listener and endpoint state callback are distinct. Release listener precedes
  `stream_ops.released`; both normally occur.
- `zephyr/include/zephyr/bluetooth/conn.h`: auth and auth-info registration both
  return errors.

## Required implementation shape

### 1. Explicit TX activation in coordinator contract

Add backend op:

```c
int (*tx_start)(uint8_t stream_count);
```

Call it in worker after all `sem_started` completions and before advancing to
`streaming`. Failure follows normal error-abort cleanup.

Production `tx_start` calls:

```c
hil_source_tx_attach(bap_streams, stream_count)
```

Fake `tx_start` records a new `FAKE_OP_TX_START` ledger entry and can return a
scripted error. Full lifecycle and reconnect tests must assert one activation
per segment, correct count, activation before first send, and fresh sequence
zero after reconnect. Add a production-facing TX driver test only if practical
without mocking away `bt_bap_stream_send`; explicit coordinator `tx_start`
coverage is minimum required boundary.

`hil_source_tx_stop()` must reject sends and clear attached stream pointers and
count. Next `tx_start` must reactivate only exact current segment stream count.

### 2. Single output owner and capacity

- Delete raw debug `printk`; add no replacement output.
- Change complete HIL1 line capacity from 768 to 1024 bytes. Queue remains eight
  complete lines. Change `CONFIG_SHELL_PRINTF_BUFF_SIZE=1024`.
- Keep status data scratch bounded separately; do not use 1024-byte TX SDU
  scratch. TX SDU maximum is `CONFIG_BT_ISO_TX_MTU` (255), so use a dedicated
  255-byte buffer or exact protocol maximum.
- Add tests using command ID length 63 and run ID length 64. Two-stream status
  must format and queue successfully without truncation. Keep tiny-buffer and
  over-capacity rejection tests.
- At output init, require non-NULL `shell_backend_uart_get_ptr()` before marking
  output active or starting writer. Test build remains capture-backed.
- Set `CONFIG_NCS_BOOT_BANNER=n` and `CONFIG_BOOT_BANNER=n`. Keep empty prompt,
  no echo, no log backend output. Do not change raw shell registration. With
  mandatory count 1 plus `SHELL_OPT_ARG_RAW`, bare `hil` reaches handler with
  `argc == 1`; handler's negative return is silent in Zephyr v3.3.0.

### 3. Stack budget

Set `CONFIG_SHELL_STACK_SIZE=8192`. Keep worker stack 8192 unless measured
stack use proves it insufficient. Build inspection must confirm resolved 8192,
both boot banners disabled, log disabled, and shell printf buffer 1024.

### 4. Correct BAP completion ownership

Use endpoint-state callbacks as successful completion gates:

- `stream_ops.configured` gives `sem_configured`.
- `stream_ops.qos_set` gives `sem_qos`.
- `stream_ops.enabled` gives `sem_enabled`.
- `stream_ops.connected` gives `sem_stream_connected`.
- `stream_ops.started` gives `sem_started`.
- add `stream_ops.disabled`, giving `sem_disabled`.
- `stream_ops.released` alone gives successful `sem_released`.

Register ASCS listeners for config, qos, enable, disable, and release. Each
listener records first response code/reason. On success it does not signal the
operation semaphore. On rejection it stores first operation error and signals
that operation semaphore once so worker fails promptly. Start listener is not
needed because fixture configures sink ASEs only and never sends client Start.

Replace `bap_kick_start()` behavior with sink readiness validation only:

- verify each attached endpoint direction is `BT_AUDIO_DIR_SINK`;
- do not call `bt_bap_stream_start()`;
- return zero and let worker consume retained/future `stream_ops.started`
  completions;
- invalid/missing endpoint or wrong direction is an error, never synthetic
  success.

Callbacks for wrong connection or unknown stream record first error and wake
current wait where applicable. Late callbacks after segment reset are counted
or ignored under generation state, never applied to current operation.

### 5. Backend synchronization

Add one backend lock. Prefer `k_spinlock` for short state copies. Never call
Bluetooth APIs, unref objects, submit output, or wait while holding it.

Guard callback-written/status-read fields: operation error, security fields,
disconnect reason, ASCS fields, endpoint pointers/count, connection pointer,
group pointer, and reset-visible generation/state. Operations may copy a
pointer under lock, then call API after unlock. Preserve one owned connection
reference and avoid double unref. Do not hold backend lock while invoking app
sent callback.

Operation error resets once at kick start and preserves first async error until
worker reads it; later success callback must not overwrite earlier failure.

Check and return errors from `bt_conn_auth_cb_register()` and
`bt_conn_auth_info_cb_register()`. `bt_gatt_cb_register()` and stream callback
registration are void in installed NCS and need no invented checks.

### 6. Per-stream TX progress

Replace global `tx_last_activity` with one timestamp per stream. Update a
stream timestamp on its successful send and its valid sent callback.

When Mode A cannot send a semantic pair because either stream reached
outstanding depth two, timeout if any blocking stream has had no own progress
for 2 seconds. Activity on other stream must not mask it. Mono and Mode B keep
same two-deep behavior. Drain remains bounded at 5 seconds.

Add fake support to suppress sent callbacks per stream. Regression: Mode A,
stop stream 0 callbacks while stream 1 callbacks continue, assert bounded
timeout abort, universal cleanup, terminal fail, and no unbounded loop.

### 7. Cleanup and idle ownership

Create cleanup-specific bounded wait that does not exit because stop/runtime
error is already set. Universal cleanup remains worker-owned during active run:

1. deactivate TX and clear logical outstanding;
2. disable attached streaming/enabling streams, wait endpoint disabled;
3. release attached streams, wait endpoint released/detached;
4. disconnect exact connection and wait callback;
5. delete group only when every stream detached;
6. release any remaining app-owned connection reference once;
7. reset segment callback/semaphore state.

Continue after individual failures and return first cleanup errno. If streams
remain attached, do not delete group and report cleanup failure. Cleanup timeout
prevents reconnect and ends current run in fail.

Reset `sem_run_done` in accepted `start` before waking worker, not in `idle`.
For active `idle`, request stop then take existing completion semaphore for at
most 15 seconds. If wait times out, emit failed idle status and return timeout;
do not run concurrent cleanup or reset run state. After completion, inactive
idle may perform idempotent stray-resource cleanup and then reset state.

Add test where worker completes immediately after idle stop request; idle must
not lose completion or stall. Add timeout test proving idle does not reset an
active run or start second cleanup.

### 8. App-state locking and record failure

Every read/write of mutable `run_state`, `runtime_error`, TX counters, and IDs
shared with shell/callback threads must be under `app_mutex`. Use explicit
`*_locked` helpers or take lock at call sites. Do not recursively lock. Never
hold app mutex across Bluetooth operation waits or cleanup waits.

For every worker record-format/submit failure, set `err = -EIO` before abort,
preserve it as first errno, attempt cleanup, and emit terminal fail best effort.
Start ACK format/submit failure must not launch worker silently: roll start into
a diagnosed fail path or return failure without leaving an active unreachable
run. Status/hello/parse-error output failures return errors and mark runtime
failure when a run is active.

Terminal record emission remains best effort after state terminalization, but
its failure must not be reported as pass by recap/tests.

## Test additions and acceptance behavior

Extend native suite to prove public records and operation ledger for:

1. `tx_start` once per segment, before send; failure aborts with first errno.
2. TX stop then reconnect activation; segment 1 sends sequence zero.
3. Mode A asymmetric callback stall times out despite other CIS progress.
4. Stop cleanup waits for disable/release/disconnect completion rather than
   short-circuiting on stop.
5. Exactly one completion token per stream for configure/disable/release in fake
   contract; no cross-stream token consumption.
6. Idle completion race and idle timeout ownership.
7. Maximum legal command/run IDs produce complete two-stream status under the
   1024-byte bound.
8. Record submission failure preserves `-EIO`, cleanup, abort error, terminal
   fail where output can resume.

Keep existing six mode/profile lifecycle cases, timeout boundaries, exact stage
caps, stale callback, output validation, and cleanup-failure tests.

## Verification

Run, in order:

```bash
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_app -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-app-review-fix-twister
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_control -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-control-rh1b-review-fix-twister
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_signal -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-signal-rh1b-review-fix-twister
nix develop --command fw-build-hil-source
nix develop --command python3 -W error scripts/test_hil_runner.py
python3 scripts/test_inventory.py --count
python3 scripts/test_inventory.py --json
python3 -m compileall -q scripts/hil scripts/test_hil_runner.py scripts/generate_hil_source_sine_lut.py
git diff --check
git status --short
```

Inspect resolved app and CPUNET configs plus build logs. Expected inventory stays
66 unless added test suite changes prospective child count; adding cases inside
existing suite does not change it. Report every warning/diagnostic. Existing
documented global NCS diagnostics are identified, not renamed clean warnings.

## Escalation and recap

Stop after two materially different failed attempts, contradictory installed
NCS behavior, warning without explanation, architecture need beyond this
handoff, or inability to preserve cleanup/ownership invariants. Preserve partial
work and return exact blocker, commands/logs, status, one decision needed, and
smallest hypothesis.

On success return changed files, behavior, exact test/build counts, resolved
config facts, diagnostics, deviations, blockers, and no commit hash because this
handoff forbids commits.
