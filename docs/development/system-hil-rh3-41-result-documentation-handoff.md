# RH3-41 result documentation handoff

Status: document completed H41 failure evidence. Do not run builds or touch
hardware.

## Goal

Record immutable H41 untraced workqueue evidence, correct restart state, and
remove stale wording that says H41 physical execution remains future work. Keep
one canonical detailed result document. This is documentation only.

## Ground truth

The completed execution is:

```text
run ID: rh3-20260830-41-tx-notify-wq-untraced
row:    rh3.fresh_mode_b_48_3_1
root:   /tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced
```

Read these immutable files before editing:

```text
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced/result.json
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced/environment.json
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced/images.json
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced/identity.json
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced/source-records.jsonl
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced/source-console.bin
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced/receiver-console.bin
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced/SHA256SUMS
docs/development/system-hil-rh3-41-tx-notify-workqueue-untraced-software-result.md
docs/development/system-hil-rh3-41-tx-notify-workqueue-untraced-execution-handoff.md
docs/development/system-hil-resume-state.md
```

Already reviewed facts:

- `result.json` records `outcome=failed`, first failed boundary `run row`, and
  exact detail `active status snapshot invalid: state='teardown'; aborted=True;
  first_errno=-116`.
- Cleanup contains one failure: `source idle cleanup failed: no status response
  for command cmd-0009`.
- `environment.json` records status `0` for the runner command while the
  retained result outcome is failed. State both facts without trying to resolve
  them into a new exit-status claim.
- `SHA256SUMS` verification passed all `22/22` retained artifacts. JUnit records
  one test and one failure.
- H41 used untraced receiver CPUAPP
  `f2db9de95a17db47792f9601de033e9db4e851ae534df6fa00fdbd8fa7fcbd18`, FLPR
  `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`, source
  app `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`, and
  source CPUNET `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`.
- Source reached `qos`, then emitted `teardown` with `cause=timeout`; active
  status retained `first_errno=-116`, zero stream submissions, and no streaming
  state. It then asserted during cleanup on source HCI Disconnect `0x0406`:
  `Controller unresponsive, command opcode 0x0406 timeout with err -11`.
- Receiver completed connection, security, codec configuration, QoS, and logged
  `Enable: stream[0] meta_len 4` plus LC3 decoder initialization. It retained
  no `Stream[0] started` line. No receiver status snapshot exists because the
  row failed before streaming and terminal collection.
- This untraced failure and H40 traced pass do not prove a trace effect,
  workqueue causation, a root cause, or a repair.
- Normal local build output was restored after evidence review. It did not
  reflash boards. Disk remained `101.3 GiB` free, with no cleanup.

## Scope

Touch only:

```text
docs/development/system-hil-rh3-41-tx-notify-workqueue-untraced-result.md
docs/development/system-hil-rh3-41-tx-notify-workqueue-untraced-software-result.md
docs/development/system-hil-resume-state.md
```

### In scope

1. Create canonical detailed H41 result document.
2. Correct the H41 software-result stop text to link completed physical result.
3. Correct concise current restart state facts made stale by H41.

### Out of scope

- `STATUS.md`, public docs, code, Kconfig, board config, tests, runner,
  fixtures, source images, HIL execution, hardware, or builds.
- Retrying H41, creating another run ID, changing evidence, deletion, storage
  cleanup, Nix garbage collection, staging, or commit.

## Exact documentation shape

### 1. Canonical H41 result

Create `docs/development/system-hil-rh3-41-tx-notify-workqueue-untraced-result.md`.
Use concise sections for status/scope, immutable evidence, exact images,
observed failure, bounded H40 comparison, restoration, and stop point.

Record exact source and receiver progress. State explicitly that missing source
streaming and receiver `Stream[0] started` are observations, not proof of which
side caused the timeout. State that no receiver status summary or trace payload
exists for this untraced pre-stream failure. Preserve raw identity facts from
`identity.json`. Do not copy binary console logs into repository.

The stop point must prohibit H41 retry and production adoption. It must say the
smallest next investigation needs a new reviewed plan to distinguish receiver
enable callback/start behavior from source enabled-completion observation.

### 2. H41 software-result stop text

Replace only final future-tense stop text in
`system-hil-rh3-41-tx-notify-workqueue-untraced-software-result.md` with a link
to the new canonical result. Preserve the build facts.

### 3. Resume state

In `system-hil-resume-state.md`:

1. Correct retained physical-run count to twenty-two: sixteen failed, one
   cancelled, and five passed direct diagnostic/control executions.
2. Replace H40 as last recorded runner-owned flash with H41's exact CPUAPP and
   FLPR hashes. Preserve statement that normal local restoration does not prove
   live hardware state.
3. Append concise `## RH3-41 current stop point` section linking canonical
   result, preserving immutable root, prohibiting retry/adoption, and requiring
   a new reviewed plan before hardware work.

Keep detailed H41 facts only in canonical result. Do not revise historical H40
or older-run counts in their local historical sections.

## Disk rule and verification

This is docs-only work. Do not invoke a build, test, Nix command, or hardware
tool. Do not create build trees, copy HIL artifacts, delete files, or run
garbage collection.

```bash
git diff --check
! rg -n '—' \
  docs/development/system-hil-rh3-41-tx-notify-workqueue-untraced-result.md \
  docs/development/system-hil-rh3-41-tx-notify-workqueue-untraced-software-result.md \
  docs/development/system-hil-resume-state.md
git status --short
df -h . /tmp /nix/store
```

Return changed paths, exact evidence facts recorded, verification output, final
status, free-space result, no-build/no-hardware/no-commit confirmation, and
blockers or deviations.
