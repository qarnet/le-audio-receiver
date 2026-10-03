# PB-033 phase 3B handoff: XIAO physical proof

Status: physical execution handoff prepared 2026-09-22 after phase 3A
acceptance at commits `4c8caf1` and `cb0b3c0`.

## Goal

Build exact local receiver and source images from one clean commit, create one
external immutable session for two operator-selected XIAO nRF54L15 boards, then
run these checked-in 10 ms rows once each through the public session-bound
runner:

1. `rh3.fresh_mono_48_4_1`
2. `rh3.fresh_mode_a_48_4_1`
3. `rh3.fresh_mode_b_48_4_1`

Each row must pass its existing transport/runtime oracle, retain complete
identity, session, command, serial, image, and result evidence, and contain no
unexpected firmware, controller, transport, audio, OpenOCD, or host-tool
warning. This phase proves PB-033 XIAO-to-XIAO transport/runtime behavior only.

## Scope

### In scope

- Commit this handoff before building so physical evidence names one clean git
  commit.
- Create the gitignored XIAO physical binding from its checked-in example
  without adding permanent probe serials to repository files.
- Rebuild production nRF54L15 receiver CPUAPP/FLPR and XIAO nRF54L15 source
  images from the clean handoff commit.
- Audit complete build logs against repository warning policy.
- Create exactly one external immutable session manifest under
  `/tmp/opencode/hil-sessions/` from the fresh operator-selected role mapping.
- Reuse that exact manifest and binding bytes across three direct runner calls.
- Stop at first failed or cancelled row, preserve evidence, and return failure
  evidence for Delegator diagnosis. No blind retry.
- Review each passing run at its public evidence boundary and verify every
  `SHA256SUMS` entry.
- Add one internal result document and append PB-033 implementation notes with
  evidence paths, hashes, metrics, warnings, and exact scope.
- Run focused host regressions after physical proof and commit only result/task
  documentation.

### Out of scope

- No source, receiver, runner, fixture, row, threshold, protocol, Kconfig,
  devicetree, build-helper, or flash-helper change.
- No manual flash, reset, erase, recovery, debugger, serial terminal,
  `serial-mcp`, `btattach`, BlueZ, or `scripts/bap_central.py` action. Runner
  owns all target mutation, serial descriptors, clean-state transitions, and
  cleanup.
- No rerun after an unclassified failure. No skipped row, changed order,
  reduced duration, warning bypass, trace option, or offload-disabled option.
- No 7.5 ms, preserved-bond, reconnect, fault-injection, full RH3 matrix,
  analog, audibility, channel-wiring, RH4/FR4, release, or publication claim.
- No permanent probe-to-board table, committed probe serial, committed tty
  path, or committed local binding.
- No product status or acceptance-criteria transition in this phase. Final
  repository gates and PR readiness remain a later review step.

## Grounding and fixed decisions

- Standing lab authority in `AGENTS.md` and
  `docs/development/system-hil-milestones.md` authorizes identity resolution,
  flashing, resetting, and RF testing of attached Nordic boards.
- `tests/hil/fixture-xiao-source.json` requires nRF54L15 CPUAPP plus FLPR for
  receiver and one nRF54L15 CPUAPP image for source.
- `tests/hil/fixture-xiao-source.local.example.json` is the exact local-binding
  template. `.gitignore` owns
  `/tests/hil/fixture-xiao-source.local.json`.
- Role serials are supplied only in Executor task context after fresh read-only
  preflight. They must stay external. Current tty numbers are observations, not
  identity.
- `create-session` resolves both explicit probes, full DPIDR/AP/FICR identity,
  USB parent, stable udev properties, and current tty observations before it
  creates read-only `devices.json`.
- `run --session-manifest` revalidates manifest bytes, fixture/binding bytes,
  both probe fingerprints, USB/udev identity, and current tty paths at six
  checkpoints before any guarded action.
- Receiver images are:
  - `build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex`
  - `build/nrf54l15/flpr/zephyr/zephyr.hex`
- Source image is:
  - `build/hil-source-nrf54l15/zephyr/zephyr.hex`
- `scripts/hil/rows.py` freezes every requested row at profile `48_4_1`,
  12,000 scored SDUs per stream, 120 seconds minimum scored time, seed
  `1218649181`, and fresh state.
- Normal one-segment source terminal counters must be exact on every stream:
  `sub=12644`, `sc=12000`, `cb=12644`, `sf=0`, and `out=0`. Source terminal
  verdict must be `pass`; lifecycle/security errors and lead-under counters
  must be zero.
- Existing runner oracle requires per-stream `rx_valid` at least 90 percent of
  expected submissions, PLC at most 5 percent of decoded frames, and zero
  `rx_error`, `rx_unknown`, `empty_sdu`, `decode_err`, `i2s_underrun`, and
  `stream_reset`.
- Healthy 10 ms receiver offload must be ACTIVE during streaming and end with
  submit equal success, fallback zero, and every offload fault/recovery counter
  zero.
- Production receiver build may emit only exact diagnostics already classified
  in `STATUS.md` under `Build warning diagnostics`: nRF54L15 reserved-memory
  devicetree diagnostics, stock RRAM `avoid_unnecessary_addr_size`, watchdog
  `No SOURCES given`, FLPR `UART_CONSOLE` resolution, and Zephyr global
  `__ASSERT()` information. Source build may emit only global `__ASSERT()`
  information. Any compiler warning, Kconfig assigned-value warning, new CMake
  or devicetree warning, OpenOCD warning/error, or unexplained host message is a
  stop condition.

## Exact external names

Use these unused names once. Never delete or overwrite a pre-existing path.

```text
session_id: pb033-xiao-proof-20260922
manifest:   /tmp/opencode/hil-sessions/pb033-xiao-proof-20260922/devices.json
binding:    tests/hil/fixture-xiao-source.local.json
output:     /tmp/opencode/hil-runs

mono run:   pb033-xiao-mono-20260922
Mode A run: pb033-xiao-modea-20260922
Mode B run: pb033-xiao-modeb-20260922
```

External logs:

```text
/tmp/opencode/pb-033-phase3b-fw-build-54l15.log
/tmp/opencode/pb-033-phase3b-fw-build-hil-source-54l15.log
/tmp/opencode/pb-033-phase3b-image-sha256.txt
/tmp/opencode/pb-033-phase3b-session-create.log
/tmp/opencode/pb-033-phase3b-mono-command.log
/tmp/opencode/pb-033-phase3b-modea-command.log
/tmp/opencode/pb-033-phase3b-modeb-command.log
```

## Execution procedure

### 1. Commit handoff and freeze source commit

Before hardware or builds:

1. Inspect `git status --short --branch`, full handoff diff, and recent log.
2. Require this handoff to be the only worktree change.
3. Run `git diff --check`.
4. Commit only this handoff with message `PB-033: define XIAO physical proof`.
5. Record full commit SHA and require clean `git status --short`.

Do not amend any earlier commit.

### 2. Create local binding and preflight destinations

Copy exact checked-in example bytes to the gitignored local path:

```bash
install -m 600 \
  tests/hil/fixture-xiao-source.local.example.json \
  tests/hil/fixture-xiao-source.local.json
cmp \
  tests/hil/fixture-xiao-source.local.example.json \
  tests/hil/fixture-xiao-source.local.json
```

Require every session, run, JUnit, and external-log destination named above to
be absent and not a symlink. Do not remove any existing destination. Confirm
the binding stays ignored and `git status --short` stays empty.

Run fresh read-only identity checks before target mutation:

```bash
nix develop --command nix-nrf probes
```

Require exactly the two task-supplied XIAO serials, each reporting target
`nRF54L15`, DPIDR `0x6ba02477`, PART `0x00054b15`, and variant `AAC0`. Also
confirm current serial ports correlate each XIAO USB interface 02 to its own
`ID_SERIAL_SHORT`. Do not open either serial port outside runner.

### 3. Build exact images

Run from repository root at clean handoff commit, sequentially:

```bash
nix develop --command fw-build-54l15 \
  > /tmp/opencode/pb-033-phase3b-fw-build-54l15.log 2>&1
nix develop --command fw-build-hil-source-54l15 \
  > /tmp/opencode/pb-033-phase3b-fw-build-hil-source-54l15.log 2>&1
```

Require both commands to exit zero. Read both logs completely and classify
every diagnostic against fixed warning rules above. Do not continue on an
unclassified line.

Require all three exact HEX files to be regular, non-symlink, and nonempty.
Write their exact sizes and SHA-256 values to
`/tmp/opencode/pb-033-phase3b-image-sha256.txt`. Confirm XIAO source build has
no `domains.yaml`, CPUNET, or merged image and receiver build contains expected
CPUAPP plus FLPR images.

### 4. Create one immutable session

Executor task context supplies two environment values after Delegator's fresh
preflight:

```text
PB033_RECEIVER_PROBE=<session receiver serial>
PB033_SOURCE_PROBE=<session source serial>
```

Do not write their values into this handoff, result documentation, task file,
or any other tracked file.

Create one manifest:

```bash
nix develop --command ./scripts/hil-runner.py create-session \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.json \
  --session-id pb033-xiao-proof-20260922 \
  --receiver-probe "$PB033_RECEIVER_PROBE" \
  --source-probe "$PB033_SOURCE_PROBE" \
  > /tmp/opencode/pb-033-phase3b-session-create.log 2>&1
```

Require exit zero, exactly one manifest at expected path, parent mode `0700`,
manifest mode `0400`, regular non-symlink file, expected fixture ID, distinct
roles, exact fingerprint/AP/USB schema, and exact role serials from task
context. Record manifest SHA-256 externally. Do not chmod, edit, replace, or
copy over external manifest.

### 5. Run exact three-row proof

Use separate runner calls, in fixed order. Redirect each command's complete
host output to its named external log. Do not pipe through a command that could
hide exit status.

Mono:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id pb033-xiao-mono-20260922 \
  --junit /tmp/opencode/hil-runs/pb033-xiao-mono-20260922.junit.xml \
  --row rh3.fresh_mono_48_4_1 \
  --session-manifest /tmp/opencode/hil-sessions/pb033-xiao-proof-20260922/devices.json \
  > /tmp/opencode/pb-033-phase3b-mono-command.log 2>&1
```

Mode A:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id pb033-xiao-modea-20260922 \
  --junit /tmp/opencode/hil-runs/pb033-xiao-modea-20260922.junit.xml \
  --row rh3.fresh_mode_a_48_4_1 \
  --session-manifest /tmp/opencode/hil-sessions/pb033-xiao-proof-20260922/devices.json \
  > /tmp/opencode/pb-033-phase3b-modea-command.log 2>&1
```

Mode B:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id pb033-xiao-modeb-20260922 \
  --junit /tmp/opencode/hil-runs/pb033-xiao-modeb-20260922.junit.xml \
  --row rh3.fresh_mode_b_48_4_1 \
  --session-manifest /tmp/opencode/hil-sessions/pb033-xiao-proof-20260922/devices.json \
  > /tmp/opencode/pb-033-phase3b-modeb-command.log 2>&1
```

After each command, require exit zero before starting next row. On nonzero,
failed result, cancellation, identity drift, warning, cleanup failure, or
evidence-finalization failure, stop immediately. Preserve all files and report
exact boundary, detail, commands, logs, images, session hash, and status. Do not
rerun or patch code.

### 6. Review public evidence after each pass

For each run, verify:

- `result.json`: `outcome=passed`, matching row name, no failed boundary,
  `failure_detail=null`, and `cleanup_failures=[]`.
- Internal and external JUnit both report one passing test and no failure,
  error, or skip.
- `session.json` points to expected external manifest and carries its exact
  hash; `session-devices.json` matches external bytes.
- `session-revalidations.jsonl` contains exactly six successes in order:
  `setup identity`, `receiver serial open`, `source serial open`, `source
  flash`, `receiver flash`, `row action`.
- Every snapshot resolves task-supplied role serials and stable identities.
  Current tty paths may differ only before their descriptor opens. No later
  drift is allowed.
- `images.json` contains exactly source app, receiver CPUAPP, receiver FLPR in
  that order with hashes matching external image preflight. All three rows use
  identical image hashes.
- `commands.jsonl` retains every discovery/revalidation command and exactly
  one `fw-flash-hil-source-54l15` with only source serial override plus one
  `fw-flash-54l15` with only receiver serial override. All statuses are zero.
- Source hello identity is `le-audio-hil-source-rh1`, protocol version 1.
- Source final counters and verdict match fixed values above for every stream.
- Receiver stream, transport-limit, offload, handshake, audio-fault, and
  teardown evidence satisfy runner oracle. Review actual values and record
  them, not only runner verdict.
- Raw source/receiver consoles, flash logs, command ledger stdout/stderr, and
  host command log contain no unexpected warning, error, assertion, fault, or
  recovery line.
- `MANIFEST.md` covers session files and normal run evidence. From each run
  directory, `sha256sum -c SHA256SUMS` succeeds for every entry.

Do not infer analog output, audibility, physical L/R mapping, or broader RH3/RH4
acceptance from passing rows.

### 7. Record result and focused regressions

Add `docs/development/pb-033-phase3b-xiao-physical-proof-result.md`. Include:

- clean source commit and NCS/toolchain identity;
- exact build commands, artifact sizes/hashes, and diagnostic classification;
- session ID, external manifest path/hash, fixture/binding hashes, and role
  identity contract without probe serial values or tty paths;
- run IDs, external JUnit paths, row verdicts, source terminal counters,
  receiver per-stream counters, active/final offload values, warning audit,
  cleanup result, and evidence-integrity result;
- explicit transport/runtime-only scope and excluded claims;
- any failed evidence unchanged if execution stopped.

Append matching concise phase 3B notes to PB-033 `Implementation Notes`. Do not
edit Description, acceptance-criteria text/check boxes, title, priority, type,
status, or dependencies.

Run sequentially:

```bash
nix develop -c python3 scripts/test_hil_runner.py
nix develop -c python3 tests/hil/rh2_test.py
nix develop -c python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil
nix develop -c backlog doctor
git diff --check
```

Inspect status, complete diff, and recent log. Stage only result document and
PB-033 task notes. Commit with concise message containing `PB-033`. Do not stage
gitignored binding, build output, external session, external logs, or run
evidence.

## Files allowed after handoff commit

- `docs/development/pb-033-phase3b-xiao-physical-proof-result.md`
- `docs/product/backlog/tasks/pb-033 - Prove-XIAO-nRF54L15-as-a-session-bound-HIL-source-fixture.md`

No production, test, fixture, helper, configuration, public-doc, status, or
historical-evidence file may change.

## Commit and escalation

Make two new commits only:

1. handoff commit before builds and hardware;
2. result/task-note commit after successful physical proof and focused gates.

Do not push, open or merge a PR, amend, force-push, or add AI/tool attribution.

Stop without a result commit and report to Delegator if any destination exists,
identity differs, build has an unexplained diagnostic, session creation fails,
a row fails or is cancelled, evidence integrity fails, hardware becomes
unavailable, a code/config change appears needed, or two materially different
attempts would be required. Preserve worktree and all external evidence.

Return commits; clean/dirty status; build results and diagnostics; image and
manifest hashes; exact run/evidence paths; row metrics and verdicts; warning
audit; focused gate results; deviations; blockers; and next-step recommendation.
