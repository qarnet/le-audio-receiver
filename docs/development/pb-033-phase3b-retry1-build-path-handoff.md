# PB-033 phase 3B retry 1 handoff: clean worktree build paths

Status: focused execution repair prepared 2026-09-22 after phase 3B stopped
before hardware at commit `76c4a0d`.

## Goal

Repair two execution-preflight defects from the first phase 3B attempt, then
resume the unchanged XIAO physical proof from
`pb-033-phase3b-xiao-physical-proof-handoff.md` with fresh external names.

The stopped attempt built both images but performed no session creation, flash,
reset, RF action, row, host regression, or result commit. Its build logs stay
immutable:

```text
/tmp/opencode/pb-033-phase3b-fw-build-54l15.log
/tmp/opencode/pb-033-phase3b-fw-build-hil-source-54l15.log
```

## Failure classification

Classification: execution-environment and handoff defect, not receiver/source
firmware behavior.

1. Receiver configure rewrote three tracked, CMake-owned
   `compile_commands.json` symlinks to this worktree's build paths. Source build
   then started from a dirty git tree, so Nix emitted the mandatory stop line
   `warning: Git tree ... is dirty`.
2. NCS v3.3.0 sysbuild derives root image name from `APP_DIR` basename before
   child `project()` runs. This worktree basename is
   `le-audio-receiver-pb-fixture`, so receiver CPUAPP landed below
   `build/nrf54l15/le-audio-receiver-pb-fixture/`. Runner and
   `fw-flash-54l15` intentionally consume stable path
   `build/nrf54l15/le-audio-receiver/`.

Grounding:

- `CMakeLists.txt`, `src/flpr/CMakeLists.txt`, and
  `hil/source/app/CMakeLists.txt` call `file(CREATE_LINK)` on configure.
- NCS v3.3.0 `zephyr/share/sysbuild/cmake/modules/sysbuild_images.cmake`
  derives `DEFAULT_IMAGE` from `APP_DIR` basename.
- NCS v3.3.0 `zephyr/share/sysbuild/CMakeLists.txt` accepts command-line
  `APP_DIR` before its environment fallback.
- No `APP_DIR_NAME` or command-line `DEFAULT_IMAGE` override exists in this SDK
  version.

## Scope

In scope:

- Restore only build-generated tracked symlink target changes.
- Commit this repair handoff before retry builds.
- Use one external source-path alias named `le-audio-receiver` and pass it as
  CMake `APP_DIR` to the existing receiver build helper.
- Restore CMake-owned tracked symlinks to committed targets immediately after
  each successful build, then prove clean status before the next Nix command.
- Use fresh retry-1 logs, session ID, run IDs, JUnits, and run directories.
- Continue every unchanged identity, warning, row, evidence, review, result,
  regression, and commit rule from the original phase 3B handoff.

Out of scope:

- No production, test, helper, runner, fixture, CMake, symlink, Kconfig,
  devicetree, row, threshold, or protocol source change.
- No deletion or overwrite of stopped-attempt logs or any prior evidence.
- No manual flash/reset/serial operation and no hardware retry after a row
  failure.
- No committed probe serial, tty path, local binding, build output, alias, log,
  session manifest, or run directory.

## Exact retry names

All paths must be absent and not symlinks before creation.

```text
source alias parent: /tmp/opencode/pb-033-phase3b-source-alias-r1
source alias:        /tmp/opencode/pb-033-phase3b-source-alias-r1/le-audio-receiver

receiver build log:  /tmp/opencode/pb-033-phase3b-fw-build-54l15-r1.log
source build log:    /tmp/opencode/pb-033-phase3b-fw-build-hil-source-54l15-r1.log
image hashes:        /tmp/opencode/pb-033-phase3b-image-sha256-r1.txt
session create log:  /tmp/opencode/pb-033-phase3b-session-create-r1.log

session_id:          pb033-xiao-proof-20260922-r1
manifest:            /tmp/opencode/hil-sessions/pb033-xiao-proof-20260922-r1/devices.json

mono run:            pb033-xiao-mono-20260922-r1
Mode A run:          pb033-xiao-modea-20260922-r1
Mode B run:          pb033-xiao-modeb-20260922-r1

mono command log:    /tmp/opencode/pb-033-phase3b-mono-command-r1.log
Mode A command log:  /tmp/opencode/pb-033-phase3b-modea-command-r1.log
Mode B command log:  /tmp/opencode/pb-033-phase3b-modeb-command-r1.log
```

## Exact procedure

### 1. Restore stopped-attempt generated changes and commit repair handoff

Initial tracked modifications must be exactly:

```text
compile_commands.json
hil/source/compile_commands.json
src/flpr/compile_commands.json
```

Verify each is a symlink and its diff is only absolute target replacement from
committed path to current worktree build path. Restore only these three paths:

```bash
git restore --source=HEAD -- \
  compile_commands.json \
  hil/source/compile_commands.json \
  src/flpr/compile_commands.json
```

Then require this repair handoff to be the sole worktree change, run
`git diff --check`, stage only it, and commit:

```text
PB-033: repair phase 3B build preflight
```

Require clean status and record full commit SHA. Do not amend `76c4a0d`.

### 2. Revalidate local binding, destinations, and identity

The gitignored local binding from the stopped attempt may be reused only if
`cmp` proves it is byte-identical to
`tests/hil/fixture-xiao-source.local.example.json`. It must remain ignored.

Require every retry path named above to be absent and not a symlink. Preserve
all stopped-attempt files unchanged.

Run fresh read-only identity and USB correlation preflight exactly as required
by original handoff. Use task-context role serials only in process environment;
never add them to tracked files.

### 3. Create external stable source alias

Require canonical parent `/tmp/opencode` already exists. Create one private
external directory and one symbolic link:

```bash
mkdir -m 700 /tmp/opencode/pb-033-phase3b-source-alias-r1
ln -s -- "$PWD" \
  /tmp/opencode/pb-033-phase3b-source-alias-r1/le-audio-receiver
```

Require alias to be a symlink whose exact target is repository root, and its
resolved target to equal repository root. Keep it present through builds and
all runner calls. It is external execution state, not evidence or source.

### 4. Retry receiver build from clean tree

Run:

```bash
nix develop --command fw-build-54l15 \
  -DAPP_DIR:PATH=/tmp/opencode/pb-033-phase3b-source-alias-r1/le-audio-receiver \
  > /tmp/opencode/pb-033-phase3b-fw-build-54l15-r1.log 2>&1
```

Require exit zero. Require log root-image heading and output path use exact
domain `le-audio-receiver`, not worktree basename. Audit complete log under
original warning rules.

Immediately afterward, tracked changes must be only CMake-owned root and FLPR
compile-database symlink targets. `hil/source/compile_commands.json` must remain
unchanged. Restore only generated root/FLPR targets:

```bash
git restore --source=HEAD -- \
  compile_commands.json \
  src/flpr/compile_commands.json
```

Require clean git status before source build. Do not delete receiver build
output.

### 5. Retry source build from clean tree

Run:

```bash
nix develop --command fw-build-hil-source-54l15 \
  > /tmp/opencode/pb-033-phase3b-fw-build-hil-source-54l15-r1.log 2>&1
```

Require exit zero and no dirty-tree warning. Audit complete log under original
warning rules.

Immediately afterward, tracked changes must be only
`hil/source/compile_commands.json` target replacement. Restore only it:

```bash
git restore --source=HEAD -- hil/source/compile_commands.json
```

Require clean git status before any session or runner command.

### 6. Verify exact images

Require regular, non-symlink, nonempty files at original stable paths:

```text
build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
build/nrf54l15/flpr/zephyr/zephyr.hex
build/hil-source-nrf54l15/zephyr/zephyr.hex
```

Require receiver `domains.yaml` default/domain name `le-audio-receiver` and
separate `flpr`. Require no worktree-basename receiver domain. Record exact
sizes and SHA-256 values in retry hash file. Preserve stopped-attempt images
only if still present naturally; do not use them.

### 7. Create retry session

Using task-context `PB033_RECEIVER_PROBE` and `PB033_SOURCE_PROBE` values, run:

```bash
nix develop --command ./scripts/hil-runner.py create-session \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.json \
  --session-id pb033-xiao-proof-20260922-r1 \
  --receiver-probe "$PB033_RECEIVER_PROBE" \
  --source-probe "$PB033_SOURCE_PROBE" \
  > /tmp/opencode/pb-033-phase3b-session-create-r1.log 2>&1
```

Apply every manifest mode, schema, identity, hash, and immutability check from
original handoff using retry manifest path.

### 8. Run retry rows in fixed order

Mono:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id pb033-xiao-mono-20260922-r1 \
  --junit /tmp/opencode/hil-runs/pb033-xiao-mono-20260922-r1.junit.xml \
  --row rh3.fresh_mono_48_4_1 \
  --session-manifest /tmp/opencode/hil-sessions/pb033-xiao-proof-20260922-r1/devices.json \
  > /tmp/opencode/pb-033-phase3b-mono-command-r1.log 2>&1
```

Mode A:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id pb033-xiao-modea-20260922-r1 \
  --junit /tmp/opencode/hil-runs/pb033-xiao-modea-20260922-r1.junit.xml \
  --row rh3.fresh_mode_a_48_4_1 \
  --session-manifest /tmp/opencode/hil-sessions/pb033-xiao-proof-20260922-r1/devices.json \
  > /tmp/opencode/pb-033-phase3b-modea-command-r1.log 2>&1
```

Mode B:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id pb033-xiao-modeb-20260922-r1 \
  --junit /tmp/opencode/hil-runs/pb033-xiao-modeb-20260922-r1.junit.xml \
  --row rh3.fresh_mode_b_48_4_1 \
  --session-manifest /tmp/opencode/hil-sessions/pb033-xiao-proof-20260922-r1/devices.json \
  > /tmp/opencode/pb-033-phase3b-modeb-command-r1.log 2>&1
```

Use every original stop rule. Require each command and evidence review to pass
before next row. No retry after a row failure.

### 9. Evidence, result, regressions, and result commit

Apply every review check from original handoff to retry session and run paths.
Verify all three `SHA256SUMS` files completely.

Result document must record both:

- stopped first build attempt, exact cause, immutable logs, and explicit no
  hardware action;
- successful retry evidence, if all rows pass.

Do not record probe serials or tty paths in tracked docs. Update only original
handoff's allowed result document and PB-033 Implementation Notes. Run same
focused regression commands. Stage only those two files and commit a concise
`PB-033` result message. Repair handoff is already committed separately.

## Escalation and return

Stop and return to Delegator without result commit on any unexpected tracked
change, dirty-tree warning in retry source build, unstable receiver domain,
build diagnostic, identity/session issue, row failure, warning, evidence
integrity failure, unavailable hardware, or need for code/config changes.

Do not push, open or merge a PR, amend, force-push, or add attribution. Return
all fields required by original handoff plus stopped-attempt preservation,
retry handoff commit, alias verification, symlink restore evidence, clean-tree
checks between commands, and retry result commit if successful.
