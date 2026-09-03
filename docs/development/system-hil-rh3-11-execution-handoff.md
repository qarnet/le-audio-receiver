# RH3-11 strict reproducibility execution handoff

Status: execution handoff only. Run exactly one fresh strict RH3
reproducibility matrix after RH3-10, using the existing reviewed images. This
run is not RH3 acceptance evidence and is not root-cause proof. No repository
mutation belongs in this phase.

## Goal

Run one fresh fixed RH3 matrix with immutable run ID `rh3-20260820-03`.
Preserve every aggregate, child, JUnit, manifest, checksum, and raw-log
artifact for independent review. This is a fresh reproducibility test, not a
same-run retry, not an acceptance run, and not proof of a source, receiver,
runner, configuration, radio-timing, or power root cause.

## RH3-10 grounding

RH3-10 ended with the following exact aggregate result:

```text
aggregate: /tmp/opencode/hil-runs/rh3-20260820-02/
child root: /tmp/opencode/hil-runs/rh3-20260820-02.children.403d5f6a2dee/
outcome: failed
scheduled=20 attempted=2 passed=1 failed=1 cancelled=0 cleanup_failures=[]
first failure: pass1 row2 rh3.fresh_mode_a_48_4_1 stopped matrix: log scan
```

Fresh mono passed. Fresh Mode A source completed successfully. Both streams
reported `sub=12644`, `sc=12000`, `sf=0`, and `cb=12644`. Receiver tails were
clean: decode errors `0`, I2S underruns `0`, stream resets `0`, and push
failures `0`. FLPR was ACTIVE with `submit=14035 success=14035`.

The only failed boundary was this exact receiver warning:

```text
[00:51:08.410,510] <wrn> bt_conn: conn 0x2000f6d8 failed to establish. RF noise?
```

The warning occurred after `Stream[0]` started and before `Stream[1]` started.
`Stream[1]` then started at `[00:51:08.719,081]`. The existing source
one-retry mechanism recovered the event, but the strict raw-log scanner
correctly rejected the warning. This is not a scanner bug and is not an
accepted warning. Strict warnings remain failures, with no scanner exemption.

No evidence supports changing source, receiver, runner, configuration, radio
timing, or power. RH3-11 must therefore remain a fresh reproducibility test.
Do not turn it into a same-run retry, an acceptance claim, or root-cause proof.

## Frozen scope

In scope:

1. Recheck the unused output destinations immediately before execution.
2. Validate the fixture and local binding without hardware mutation.
3. Verify the exact SHA-256 identities of the four existing images.
4. Run exactly one runner-owned RH3 matrix with run ID `rh3-20260820-03`.
5. Preserve and report all produced evidence without editing it.

Out of scope:

- all code changes and all edits to existing files;
- result-document edits, including `STATUS.md`, and any change to this
  handoff during execution;
- source, receiver, runner, fixture, binding, row, parser, threshold, policy,
  Kconfig, build-file, or configuration changes;
- every rebuild, including `fw-build-hil-source`, `fw-build-54l15`,
  `fw-build-5340`, `west build`, or any source-fixture build;
- separate tests, including unit tests, Python tests, `tests/hil`, or any
  second HIL command;
- manual serial, `serial-mcp`, an extra serial reader, manual flashing,
  reset, recovery, erase, pairing, Bluetooth control, `btattach`,
  `scripts/bap_central.py`, manual FLPR shell commands, OpenOCD, probe
  control, RF changes, or power changes;
- child retry, matrix retry, a changed or substitute run ID, output deletion,
  cleanup, overwrite, acceptance claim, root-cause claim, commit, push, merge,
  tag, release, or PR.

The worktree is intentionally dirty. Preserve every existing change. Do not
reset, stash, revert, clean, or broadly format the worktree. During execution,
make no code changes, make no result-document edits, make no commits, push
nothing, and create no PR. Do not modify `STATUS.md`.

## Strict warning policy

The raw-log scanner is strict. Any warning not explicitly accepted by the
existing runner policy remains a failure. The exact `bt_conn` warning recorded
above must remain a failure if it recurs, even when the source retry mechanism
later starts both streams successfully. Do not suppress, reclassify, whitelist,
or explain away a warning. Do not edit scanner logic or add an exemption.

## Exact image identities

Use existing files only. No rebuild is allowed. Before any runner hardware
action, all four SHA-256 values must match exactly:

```text
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
bdb898df7cac638def55a86073937d1e3d8cf6ae13a26a02fd79233add9d7de9  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

A mismatch is a hard stop. Do not rebuild, substitute an image, or select a
different candidate.

## Output ownership and immutable IDs

These initially-unused destinations were verified absent at handoff creation:

```text
/tmp/opencode/hil-runs/rh3-20260820-03
/tmp/opencode/hil-runs/rh3-20260820-03.children.a62d866f712c
/tmp/opencode/hil-runs/rh3-20260820-03.junit.xml
```

Recheck all three destinations immediately before starting the matrix. If any
destination exists or is a symlink, hard stop. Do not delete it, clean it,
overwrite it, or use a substitute ID. Preserve all earlier IDs as immutable:

```text
rh3-20260815-01
rh3-20260815-02
rh3-20260815-03
rh3-20260815-04
rh3-20260815-05
rh3-20260815-06
rh3-20260815-07
rh3-20260815-08
rh3-20260820-01
rh3-20260820-02
```

Never retry, delete, overwrite, or reuse any earlier ID.

## Preflight

Run the following commands from the repository root, sequentially and
immediately before the matrix. The output-destination checks must be treated
as hard-stop checks for both ordinary paths and symlinks. Do not run source
builds or tests concurrently with this phase or with the matrix.

1. Require all exact output destinations to be absent:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260820-03
test ! -e /tmp/opencode/hil-runs/rh3-20260820-03.children.a62d866f712c
test ! -e /tmp/opencode/hil-runs/rh3-20260820-03.junit.xml
```

2. Validate the logical fixture and local binding. This command performs no
   hardware mutation:

```bash
nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

Expected JSON contains:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

3. Verify exact images. Do not continue after any `FAILED` result:

```bash
sha256sum --check <<'EOF'
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
bdb898df7cac638def55a86073937d1e3d8cf6ae13a26a02fd79233add9d7de9  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
```

4. Check repository whitespace and record the intentionally dirty worktree:

```bash
git diff --check
git status --short
```

Record exact output from every preflight command. Record `git status --short`
output as the intentionally dirty baseline. No worktree cleanup or mutation is
allowed. Any preflight failure is a hard stop. Do not start the runner, rebuild,
modify files, delete outputs, or choose a replacement ID after a preflight
failure.

## Matrix execution

Use terminal-tool outer timeout `7200000` ms. Do not use shell `timeout`.
Execute exactly this one command from the repository root:

```bash
nix develop --command ./scripts/hil-runner.py run-rh3-matrix \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260820-03 \
  --junit /tmp/opencode/hil-runs/rh3-20260820-03.junit.xml
```

The runner is the sole owner of probe discovery, flash lifecycle, reset,
serial capture, pairing, Bluetooth connection and streaming, FLPR control,
cleanup, the fixed two-pass schedule, and evidence collection. Do not start
any other hardware, serial, Bluetooth, RF, source-build, test, or HIL command
while it runs. Do not interrupt it except for genuine user-requested
cancellation.

## Failure handling and reporting

The runner's matrix is one execution only. On any failure or cancellation:

1. Let the runner finish its owned cleanup unless genuine user-requested
   cancellation prevents that cleanup.
2. Preserve aggregate and child evidence exactly as written. Do not delete,
   rename, copy over, repair, or manually complete any artifact.
3. Do not retry a child, retry the matrix, launch another run, rebuild, run a
   test, change hardware state, or diagnose a repair.
4. Stop after evidence collection and return the failure without calling it an
   acceptance result.

For every outcome, report:

1. Each exact preflight command and its exact output.
2. Matrix CLI exit status.
3. Aggregate path, child-root path, aggregate `result.json`, external JUnit,
   and every attempted child path.
4. Aggregate outcome, `scheduled`, `attempted`, `passed`, `failed`, and
   `cancelled` counts, cleanup failures, and first failed boundary when
   present.
5. Exact image hashes from aggregate or child `images.json` evidence.
6. Relevant raw source and receiver evidence for every failed or cancelled
   row, including the exact warning text and event order when a log scan
   fails.
7. The runner command ledger and confirmation that the runner was sole
   hardware owner.
8. Confirmation that no repository file was edited, no build or separate test
   was run, no retry occurred, and no commit, push, or PR was made.

If the matrix passes, return evidence only. Do not claim RH3 acceptance,
analog output, DAC wiring, audibility, RH4 acceptance, release readiness, or
root-cause proof. A strict warning or any other raw-log failure remains a
failure regardless of later stream recovery.

## Review gate

Only independent review may mark the result reviewable. Review requires all of
the following:

1. CLI exit status `0`.
2. Aggregate outcome `passed`.
3. `scheduled=20`, `attempted=20`, `passed=20`, `failed=0`, and `cancelled=0`.
4. Empty aggregate cleanup failures.
5. Aggregate and every child retain `result.json`, JUnit, `MANIFEST.md`, and
   `SHA256SUMS`.
6. External JUnit contains 20 tests with zero failures, errors, and skipped
   cases.
7. Exact image identity is retained throughout the evidence.
8. Every strict source and receiver check passes. No unexpected warning,
   error, assert, timeout, I2S fault, stream reset, push failure, or FLPR
   mismatch is acceptable.
