# RH3 Mode A failed-CIS retry fix handoff

Status: focused source-fixture software correction. RH3 remains open. This
handoff does not authorize a physical matrix, flash, reset, serial session, or
any manual Bluetooth operation.

## Goal

Make the HIL source recover one transient failed CIS establishment for the
currently connecting Mode A stream. Preserve prompt failure for a persistent
failure. The retry must use only public NCS BAP/ISO behavior and remain owned
by the coordinator worker.

## Evidence and root cause

`rh3-20260815-07` fresh mono passed. Fresh Mode A then failed before
streaming:

```text
active status snapshot invalid: state='teardown'; aborted=True; first_errno=-116
```

Source records reached `qos` at `19595 ms` and entered timeout teardown at
`30236 ms`. Receiver evidence shows both ASEs enabled, `Stream[0] started:
CIG 0 CIS 0`, then:

```text
<wrn> bt_conn: conn 0x2000f6d8 failed to establish. RF noise?
```

No stream 1 start occurred. Current source `stream_disconnected_cb()` ignores
this failed second-CIS callback. `hil_app_op_stream_connect()` therefore waits
the full `HIL_SOURCE_OP_TIMEOUT_STREAM_MS` for a `.connected` callback that
cannot arrive.

Installed NCS v3.3.0 evidence:

- `zephyr/subsys/bluetooth/audio/bap_stream.c:860-890` accepts public
  `bt_bap_stream_connect()` while an endpoint is `QOS_CONFIGURED` or
  `ENABLING`.
- `zephyr/subsys/bluetooth/audio/bap_unicast_client.c:373-430` sends only
  `stream_ops.disconnected(stream, reason)` for a failed CIS establishment;
  it leaves the BAP endpoint state intact.
- `zephyr/subsys/bluetooth/host/iso.c:459-517` sets ISO state to
  `BT_ISO_STATE_DISCONNECTED` before that callback. The existing successful
  CIS remains connected and CIG remains active.
- `zephyr/include/zephyr/bluetooth/iso.h:717-738` says CIS disconnection also
  covers connection rejection and recommends deferred work rather than reuse
  inside the callback.

The already connected Mode A sibling must not be restarted or reconfigured.
After the failed stream's deferred disconnect event, one public retry of that
same stream is valid. This is a source-fixture recovery for one transient
transport establishment failure, not a change to receiver behavior or RH3
acceptance policy.

## Scope

Only touch:

- `hil/source/app/src/hil_source_app.h`
- `hil/source/app/src/hil_source_app.c`
- `hil/source/app/src/hil_source_bap.c`
- `tests/unit/hil_source_app/src/fake_hil_source_backend.h`
- `tests/unit/hil_source_app/src/fake_hil_source_backend.c`
- `tests/unit/hil_source_app/src/test_hil_source_app.c`
- `docs/development/system-hil-resume-state.md`
- `docs/development/system-hil-rh3-software-status.md`
- `docs/development/system-hil-rh3-physical-execution-handoff.md`
- this handoff only if factual correction is necessary.

## Out of scope

- No physical HIL matrix, child retry, flash, reset, recovery, serial reader,
  pairing, RF/power setting, or FLPR action.
- No changes to runner, rows, parser, thresholds, output schema, source signal
  data, receiver firmware, BAP presets, TX outstanding depth, controller
  buffers, I2S policy, or acceptance criteria.
- No private NCS API, direct ISO object mutation, group recreation, endpoint
  reconfiguration, or retry from a Bluetooth callback.
- No retry loop beyond one retry of one failed CIS attempt.
- Do not edit `STATUS.md`, commit, push, merge, tag, or open a PR.

## Required implementation shape

### 1. Explicit completion outcome in backend contract

In `hil_source_app.h`, add an internal enum with exactly these semantic
outcomes:

```c
enum hil_source_stream_connect_outcome {
	HIL_SOURCE_STREAM_CONNECT_OUTCOME_CONNECTED,
	HIL_SOURCE_STREAM_CONNECT_OUTCOME_RETRYABLE_FAILURE,
	HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR,
};
```

Add this backend operation next to `sem_stream_connected`:

```c
enum hil_source_stream_connect_outcome (*stream_connect_outcome)(uint8_t stream_idx);
```

The coordinator calls it only after consuming one
`sem_stream_connected()` token. It reports the event for the exact active
stream index. A wrong, stale, missing, or sibling event reports `ERROR`.
Do not expose HCI reason in HIL1 records or change HIL protocol fields.

### 2. Production BAP event ownership

In `hil_source_bap.c`, retain the existing `sem_stream_connected`. Do not add a
second semaphore. Add backend-lock-protected state for:

- pending stream-connect index, initialized to `UINT8_MAX`;
- completion event index, initialized to `UINT8_MAX`;
- completion outcome, initialized to `ERROR`.

`bap_kick_stream_connect(stream_idx)` must set the pending index and clear the
previous event before calling public `bt_bap_stream_connect()`. For its existing
`-EALREADY` path, publish `CONNECTED` for that exact index, clear pending, give
the existing semaphore, and return zero. For every other synchronous failure,
clear matching pending state and return the original error.

`stream_connected_cb()` must publish `CONNECTED` and give
`sem_stream_connected` only when its known stream index matches the pending
index. A known callback for a different stream while another index is pending
must publish `ERROR`, record `-EIO` if no prior operation error exists, and
wake the wait. A late known callback with no pending connect is ignored.

`stream_disconnected_cb()` must no longer ignore every event:

- matching pending stream: publish `RETRYABLE_FAILURE`, clear pending, give the
  existing semaphore, and do not set `op_error`;
- different known stream while another index is pending: publish `ERROR`,
  preserve first `-EIO`, clear pending, and wake the wait;
- no pending stream: ignore it. This preserves normal teardown behavior.

Neither callback may call BAP/ISO APIs, sleep, reset an endpoint, delete a
group, or submit retry work. The worker is deferred relative to callback work
and remains only owner of BAP lifecycle calls.

Implement `bap_stream_connect_outcome(stream_idx)` under `backend_lock`. It
returns stored outcome only for the matching completion index, otherwise
`ERROR`. `bap_reset_segment()` resets all new state and the existing semaphore.
Add operation to `production_ops`.

### 3. One deferred worker retry

In `hil_source_app.c`, add private constants:

```c
#define HIL_APP_STREAM_CONNECT_RETRY_DELAY_MS 10U
#define HIL_APP_STREAM_CONNECT_MAX_RETRIES    1U
```

Refactor the existing stop/fatal check used by enable retry into a generic
`hil_app_retry_state()` helper, then use it for both enable and CIS retry.

Keep stream-index serialization. For each stream in
`hil_app_op_stream_connect()`:

1. Kick that stream and wait on existing `sem_stream_connected()` using the
   unchanged `HIL_SOURCE_OP_TIMEOUT_STREAM_MS` bound.
2. On `CONNECTED`, retain current `op_error()` check and continue to next
   stream.
3. On `RETRYABLE_FAILURE`, check `hil_app_retry_state()`, sleep exactly
   `HIL_APP_STREAM_CONNECT_RETRY_DELAY_MS` without `app_mutex`, check retry
   state again, then retry only same stream index.
4. Permit one retry only. If second failed-CIS outcome occurs, return `-EIO`.
   This must produce normal error teardown, not ten-second timeout teardown.
5. On `ERROR`, return current `op_error()` when nonzero, otherwise `-EIO`.

No retry of stream 0 after it connected. No retry of a synchronous error, no
restart of stream configuration/QoS/enable, and no unbounded retry/backoff.
The 10 ms worker delay ensures source does not reuse the CIS in the ISO callback
that reported disconnection.

### 4. Fake backend and regression coverage

Extend fake backend to implement exactly same outcome contract. Replace its
single boolean pending state with pending index plus completion event index and
outcome. Add test-only API:

```c
void fake_signal_stream_connect_failure(uint8_t stream_idx);
uint32_t fake_stream_connect_failure_count(void);
```

It must only signal a currently pending matching stream, publish
`RETRYABLE_FAILURE`, clear pending, increment failure count, and give existing
stream-connect semaphore. Existing `fake_signal(FAKE_OP_STREAM_CONNECT, ...)`
means real successful `.connected` completion only. Preserve `-EALREADY`
success behavior and serialized `-EBUSY` accounting.

Add two source app tests:

1. **Transient second-CIS recovery:** Mode A stream 0 completes. Stream 1 gets
   one fake failed-CIS event, then its one allowed retry completes. Assert HIL
   run reaches pass with both streams transmitting expected data. Assert stream
   connect ledger order is `0, 1, 1`, exactly three accepted connection
   attempts, two successful completions, one failure event, no pending-CIS
   `-EBUSY`, and no stream 0 retry.
2. **Persistent second-CIS failure:** Mode A stream 0 completes. Stream 1 fails
   twice. Assert prompt terminal fail with error teardown and `first_errno=-5`
   (`-EIO`), universal cleanup completes, exactly three connection attempts,
   two failure events, and no fourth attempt. It must not wait for or report the
   ordinary stream-connect timeout.

Keep `test_modea_stream_connect_serialized` passing unchanged. Do not weaken
existing lifecycle, timeout, cleanup, or state-order tests.

## Documentation updates

After source tests and build pass, update named internal HIL documents with:

- RH3-07 exact diagnosis: CIS 0 started; failed CIS 1 emitted disconnected,
  while source ignored it and timed out.
- One worker-owned, same-stream, 10 ms deferred retry and one-retry cap.
- NCS public API basis and software-only status.
- Exact native test result and post-build source image hashes.
- Explicit statement that no physical run occurred and `rh3-20260815-01`
  through `rh3-20260815-07` remain immutable evidence.

Do not select or execute RH3-08. Delegator reviews build identity and code
before any new physical handoff.

## Verification

Run from repository root. Keep `tests/hil` idle while source build runs.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west twister \
  -T tests/unit/hil_source_app \
  -p native_sim/native/64 \
  --inline-logs \
  --outdir /tmp/hil-source-app-cis-retry-twister

nix develop --command fw-build-hil-source

git diff --check
git status --short
```

Inspect source build diagnostics and resolved configuration. Project policy
treats every actionable warning as failure. Report known NCS informational
notices separately, never as clean warnings.

## Return format

Return only after all scoped work completes. Include:

1. Files changed and behavior change.
2. Exact tests/build commands and results.
3. New source CPUAPP, CPUNET, merged app, and merged CPUNET SHA-256 values.
4. Diagnostics, `git diff --check`, and `git status --short`.
5. Explicit no-commit/no-hardware status.
6. Any blocker or deviation.

Stop and escalate without guessing if NCS behavior contradicts source evidence,
retry needs more than one attempt, callback ownership cannot stay worker-owned,
or any warning/error appears.
