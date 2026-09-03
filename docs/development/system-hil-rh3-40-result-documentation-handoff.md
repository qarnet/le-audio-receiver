# RH3-40 result documentation handoff

Status: document completed H40 diagnostic evidence. Do not run builds or touch
hardware.

## Goal

Record one completed RH3-40 physical diagnostic accurately, make restart state
safe, and remove stale wording that says H40 execution is pending. Keep one
canonical detailed result document. This is documentation work only.

## Ground truth

The completed runner-owned execution is:

```text
run ID: rh3-20260826-40-sdc-iso-tx-notify-wq
row:    rh3.fresh_mode_b_48_3_1
root:   /tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq
```

Read these immutable files before editing:

```text
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq/result.json
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq/environment.json
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq/images.json
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq/sdc-hci-remove-iso-path-trace.json
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq/receiver-post-stop-status.txt
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq/SHA256SUMS
docs/development/system-hil-rh3-40-tx-notify-workqueue-handoff.md
docs/development/system-hil-rh3-40-tx-notify-workqueue-execution-handoff.md
docs/development/system-hil-resume-state.md
```

Already verified facts:

- Runner command status was `0`, `result.json` outcome was `passed`, first
  failed boundary and failure detail were `null`, and cleanup failures were
  empty.
- External JUnit contains one test and zero failures, skipped tests, or errors.
- `SHA256SUMS` verifies all 27 retained evidence files.
- Trace parser and validation errors were both empty.
- H40 receiver image was CPUAPP
  `c487f021a0ff29076e0a1b312a6826b4f2007623098f22c34fa19358f1bedb52` and
  FLPR `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
  Source app and CPUNET hashes were respectively
  `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` and
  `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`.
- H40 used only trace fragment
  `tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf`, with
  `CONFIG_BT_CONN_TX_NOTIFY_WQ=y`, stack size `1536`, and
  `CONFIG_WARN_EXPERIMENTAL=n`. It had a 104-byte CPUAPP RAM margin. It is not
  a production configuration.
- Source terminal status had verdict `pass`, `sub=16859`, `sc=16000`,
  `sf=0`, `cb=16859`, and `out=0`.
- H40 trace retained one zero-outstanding `disable` lifetime snapshot,
  successful command and SDC completion records, and no `unavailable`
  snapshot. The H39 trace instead retained an `unavailable` snapshot with
  `outstanding=3`, `pend_thread_marked_pending=1`, and `give_entered=0`.
  This comparison is observation only. Do not call it causal proof.
- H40 receiver summary retained `rx_valid=23`, `rx_lost=19463`,
  `decoded=38972`, and `plc=38926`. Record these high-loss values plainly.
  Do not hide them and do not infer a cause. At 7.5 ms, zero FLPR submits are
  consistent with existing CPUAPP ASRC fallback for the 360-frame shape.
- Local normal build output was restored after H40. Current hashes are normal
  CPUAPP `e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f`
  and unchanged FLPR/source hashes above. The local build restoration did not
  flash either board.

## Scope

Touch only these files:

```text
docs/development/system-hil-rh3-40-tx-notify-workqueue-result.md
docs/development/system-hil-rh3-40-tx-notify-workqueue-handoff.md
docs/development/system-hil-resume-state.md
```

### In scope

1. Create the canonical detailed H40 result document.
2. Update the H40 software-handoff status line so it links to the completed
   result instead of claiming physical execution remains pending.
3. Update restart state with a concise H40 pointer and correct only facts made
   stale by H40.

### Out of scope

- `STATUS.md`, public documentation, code, Kconfig, board configuration,
  tests, HIL runner, fixtures, image hashes, generated output, builds, Nix
  configuration, or build-contract changes.
- Any flash, reset, erase, recovery, debugger, serial, RF, Bluetooth, or HIL
  runner operation.
- Repeating H40, creating another run ID, or changing immutable evidence.
- Staging, committing, pushing, merging, or cleaning the existing dirty tree.
- Storage cleanup, Nix garbage collection, deleting build directories, or
  deleting retained HIL evidence.

## Exact documentation shape

### 1. Canonical result

Create `docs/development/system-hil-rh3-40-tx-notify-workqueue-result.md`.
Use concise sections for:

1. **Status and scope.** State that one diagnostic H40 execution completed and
   passed. State that this is neither RH3 acceptance nor a production change.
2. **Immutable evidence.** Give exact root, external JUnit path, runner exit
   status, outcome, empty cleanup failures, and `27/27` checksum verification.
3. **Exact image and configuration identities.** Record the four hashes and
   trace-only private-workqueue settings. State RAM margin was 104 bytes.
4. **Observed execution.** Record row, source final counters, post-stop receiver
   health counters, exact high-loss summary values, active/post-stop lifecycle,
   and empty trace parser/validation errors.
5. **Bounded H39 comparison.** State H39 observed `unavailable` at three
   outstanding buffers with `pend_thread_marked_pending=1` and `give_entered=0`.
   State H40 instead completed the row with no `unavailable` snapshot and a
   zero-outstanding `disable` snapshot. Explicitly say this does not prove
   ownership, cause, a general repair, or production safety.
6. **Restoration and stop point.** State local normal build output was restored,
   but do not claim the hardware itself was reflashed to normal. H40 must not be
   retried. Any later hardware or production phase requires a new reviewed
   plan.

Do not copy raw console logs into the repository. Link or cite immutable paths
instead. Use no em dash characters.

### 2. H40 software handoff

In `docs/development/system-hil-rh3-40-tx-notify-workqueue-handoff.md`, replace
only its stale status sentence at the top. Say software preparation and the
single physical diagnostic completed, then link to the new canonical result.
Do not rewrite historical planning content.

### 3. Resume state

In `docs/development/system-hil-resume-state.md`:

1. Correct the retained physical-run count from twenty to twenty-one: fifteen
   failed, one cancelled, and five passed direct diagnostic/control executions.
   H40 is a passed bounded diagnostic, not acceptance.
2. Replace stale wording that says the receiver currently hashes to the old
   selected-layout telemetry CPUAPP. State instead that H40 was the last
   runner-owned flash recorded here, name its CPUAPP and FLPR hashes, and state
   that local normal output restoration does not prove current hardware image.
3. Append a short `## RH3-40 current stop point` section. Link to the canonical
   result, preserve its immutable evidence root, prohibit a retry and production
   adoption, and require a new reviewed plan before further hardware work.

Keep detailed H40 facts only in the new canonical result. Do not duplicate them
in the resume document.

## Disk rule

At handoff start, `/`, `/tmp`, and `/nix/store` shared 102 GiB free. This is
docs-only work. Do not invoke a build, test suite, Nix command, or hardware
tool. Do not create build trees or copies of HIL artifacts. Do not delete
anything to free space.

## Verification

Run only read-only or metadata checks after editing:

```bash
git diff --check
git diff -- \
  docs/development/system-hil-rh3-40-tx-notify-workqueue-result.md \
  docs/development/system-hil-rh3-40-tx-notify-workqueue-handoff.md \
  docs/development/system-hil-resume-state.md
! rg -n '—' \
  docs/development/system-hil-rh3-40-tx-notify-workqueue-result.md \
  docs/development/system-hil-rh3-40-tx-notify-workqueue-handoff.md \
  docs/development/system-hil-resume-state.md
git status --short
df -h . /tmp /nix/store
```

`rg` must return no em dash matches. Treat any unexpected generated artifact or
unrelated modified path as a blocker. Do not remove pre-existing files to make
status clean.

## Return report

Return changed paths, exact result facts recorded, verification output, final
status, free-space result, explicit no-build/no-hardware/no-commit statement,
and blockers or deviations. Stop and escalate if evidence disagrees with this
handoff or if a result claim needs a new architecture or hardware action.
