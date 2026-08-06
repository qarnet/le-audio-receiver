# Phase T1 review-fix handoff

## Goal

Close two production-contract violations found during orchestrator review and
replace vague transient-failure evidence with exact diagnosis. T1 remains open
until final exact-commit gate and builds pass.

## Scope

Touch only:

- `src/flpr_handshake.c`
- `src/flpr_ring_mgr.c`
- relevant public header comments if needed
- `tests/unit/flpr_handshake/`
- `tests/unit/flpr_ring_mgr/`
- `docs/testing/t1-flpr-production-tests.md`
- `STATUS.md`
- this handoff document

No T2 work, wire ABI changes, hardware operations, push, merge, PR, or amend.

## Fix 1 — changed epoch requires successful READY_ACK

Current `flpr_handshake_wait_new_ready()` fast path succeeds when:

```c
flpr.ready && flpr.epoch != previous_epoch
```

This can return success after `ep_received(READY)` changed peer epoch but
`ipc_service_send(READY_ACK)` failed. Production already sets `flpr.acked`
only after successful ACK send and gives `new_ready_sem` only then.

Change fast-path condition to require all three:

```c
flpr.ready && flpr.acked && flpr.epoch != previous_epoch
```

Required real-source tests:

- changed epoch + ACK success uses fast path successfully;
- changed epoch + ACK failure does not use fast path and times out;
- changed epoch + ACK failure followed by a later successful duplicate READY
  succeeds only after that successful ACK;
- same epoch remains timeout;
- unavailable session remains `-ECANCELED`;
- semaphore path still signals only after ACK success.

Update T1 evidence: remove statement that ACK-failed fast-path success is an
accepted characterization. Record it as fixed defect.

## Fix 2 — CRC failure must preserve caller PCM output

Current `flpr_ring_mgr_consume_asrc_result()` copies payload into `pcm_out`
before verifying `meta->crc32`. That violates public header contract and T1
handoff requirement that validation failures preserve caller output.

Required order:

1. validate pointers/capacity and zero only `result->output_frames`;
2. acquire slot;
3. validate flags, frame range, status shape, reserved bytes;
4. compute payload CRC over ring payload and reject mismatch;
5. only after every validation succeeds, copy PCM to `pcm_out`;
6. fill remaining result fields;
7. consume slot and return success.

Do not add a scratch buffer. CRC reads ring payload directly. Preserve existing
slot-consumption behavior on invalid output.

Required real-source tests:

- CRC mismatch returns INVALID;
- caller PCM buffer remains byte-for-byte sentinel-filled;
- result fields except documented `output_frames = 0` remain sentinel-filled;
- valid CRC still copies exact payload and fills result;
- zero-frame FLPR error output remains valid transport response and does not
  copy PCM;
- bad flags/frame range/reserved state preserve both caller buffers per header.

Update T1 evidence: remove inaccurate “payload may carry data on CRC failure”
characterization. Record transactional preservation as now proven.

## Transient gate failure diagnosis

STATUS currently says one non-BSim child failed transiently without naming it.
This violates warning/failure policy.

Recover exact suite label, command, exit status, and relevant output from
executor/session logs if available. Determine root cause. Then either:

- fix a repository defect and test it; or
- document exact external/environment cause with evidence and why it is not a
  product failure.

If exact evidence cannot be recovered, remove unsupported claims about what
failed and run the final exact-commit full gate three consecutive times on the
provisioned workstation. Record only verified final runs; do not claim a root
cause without evidence. Any new failure blocks T1 acceptance and must be
diagnosed.

Do not hide known failure evidence or call it harmless merely because a rerun
passes.

## Verification

Focused:

```bash
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t1_fix_ring tests/unit/flpr_ring_mgr -p -t run
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t1_fix_handshake tests/unit/flpr_handshake -p -t run
```

Full:

```bash
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check
```

Desktop BSim absence remains environment-specific; final exact commit must run
on workstation through bundle + detached worktree. Run full gate three
consecutive times if prior transient details cannot be recovered. Accepted
BSim hashes must remain unchanged. Remove every temporary ref/worktree/bundle.

After successful verification, update STATUS/T1 evidence accurately. Keep T1
ACCEPTED only if all final checks pass.

## Commit

Create a new commit, never amend:

```text
fix: enforce FLPR validation before acceptance
```

Return exact focused counts, full gate runs, builds, transient diagnosis,
final commit hash, repo cleanup state, deviations, and blockers.
