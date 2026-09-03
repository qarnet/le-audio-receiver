# RH3-08 failed-CIS retry execution handoff

Status: approved physical reproducibility phase. This is not an RH3 acceptance
record. No source, receiver, runner, row, parser, threshold, or policy change
belongs in this phase.

## Goal

Run one fresh complete RH3 matrix with source firmware containing reviewed
Mode A failed-CIS recovery. Determine whether fixed source behavior completes
the frozen two-pass schedule. Preserve every result regardless of outcome.

## Grounding

`rh3-20260815-07` first fresh mono child passed. Its fresh Mode A child then
reached QoS, started receiver CIS 0, and failed receiver CIS 1 establishment:

```text
<wrn> bt_conn: conn 0x2000f6d8 failed to establish. RF noise?
```

The old source ignored the matching BAP `.disconnected` callback and timed out
waiting for `.connected`, reporting `first_errno=-116`. The reviewed source
now records matching failed-CIS disconnect as a retryable result. Its
coordinator worker checks stop/fatal state, waits 10 ms outside `app_mutex`,
then retries only same stream once. A second failed-CIS event returns `-EIO`
through ordinary cleanup. No receiver, signal, pacing, power, buffer, or HIL1
protocol change occurred.

Native source app Twister passed `67/67`, including transient recovery and
persistent failure tests. `fw-build-hil-source` passed. No physical execution
has used this source image.

## Scope

1. Verify RH3-08 output paths are unused.
2. Validate current fixture and exact local image identities without rebuilding.
3. Run exactly one fresh fixed two-pass RH3 matrix as `rh3-20260815-08`.
4. Preserve result evidence and update internal HIL state only with observed
   facts.

## Out of scope

- No source, receiver, runner, parser, row, threshold, Kconfig, build, or
  documentation-policy change before or during matrix execution.
- No manual serial, `serial-mcp`, flash, reset, recovery, pairing, RF/power,
  FLPR, `btattach`, or `bap_central.py` action.
- No source rebuild. `tests/hil` remains idle while source image is in use.
- No child retry, matrix retry, ID reuse, evidence deletion, acceptance claim,
  `STATUS.md` edit, commit, push, merge, tag, release, or PR.

## Required image identities

Hash existing files. Every value must match exactly. Stop and report a mismatch;
do not rebuild or substitute images.

```text
build/hil-source/app/zephyr/zephyr.hex
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317

build/hil-source/hci_ipc/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48

build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
4838edf32ef2754f02757756d70fabfe009ad4ae6a079a69081b8293657fb6a4

build/nrf54l15/flpr/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2
```

## Preflight

Run from repository root. Matrix runner is sole hardware owner.

1. Verify both paths do not exist:

```text
/tmp/opencode/hil-runs/rh3-20260815-08
/tmp/opencode/hil-runs/rh3-20260815-08.junit.xml
```

If either exists, stop. Do not select another ID.

2. Run fixture-only validation. It parses and cross-validates files without
   hardware mutation:

```bash
nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

3. Hash only existing image files:

```bash
sha256sum \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex \
  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex
```

## Execution

Use terminal tool outer timeout `7200000` ms. Do not wrap command in shell
`timeout`.

```bash
nix develop --command ./scripts/hil-runner.py run-rh3-matrix \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260815-08 \
  --junit /tmp/opencode/hil-runs/rh3-20260815-08.junit.xml
```

The matrix runner owns all fixture lifecycle and stops at first failed or
cancelled child. Do not interact with hardware while it runs.

## Result handling

Preserve aggregate and child evidence exactly. On any failed or cancelled
outcome, do not retry a child or launch another matrix.

Update only these internal documents after result inspection:

- `docs/development/system-hil-resume-state.md`
- `docs/development/system-hil-rh3-software-status.md`
- `docs/development/system-hil-rh3-physical-execution-handoff.md`

Record run ID, CLI exit, aggregate outcome/counts/cleanup failures, exact image
hashes from `images.json`, first failed boundary if any, exact child evidence
paths, raw console/source record evidence relevant to a failure, and explicit
absence or presence of physical RH3 acceptance. State that RH3-01 through
RH3-07 remain immutable historical evidence. Do not claim a passed RH3-08
matrix is acceptance before orchestrator review.

Run after documentation updates:

```bash
git diff --check
git status --short
```

## Review criteria

Only present RH3-08 as reviewable when all apply:

1. CLI exit `0`.
2. Aggregate outcome `passed`.
3. `scheduled=20`, `attempted=20`, `passed=20`, `failed=0`, `cancelled=0`.
4. Aggregate cleanup failures empty.
5. Aggregate and each child retain `result.json`, JUnit, `MANIFEST.md`, and
   `SHA256SUMS`.
6. External JUnit has 20 tests and zero failures, errors, or skipped cases.
7. Every frozen source counter and receiver validation passes. Unexpected raw
   warnings/errors/asserts/faults fail review; runner owns named fault-window
   exceptions.

No result proves analog output, DAC wiring, audibility, RH4 artifact
acceptance, release, or full system audio.

## Return format

Return exact preflight commands/results, execution command/exit/outcome,
aggregate and relevant child evidence paths, image hashes, documentation files
changed, `git diff --check`, dirty status, and explicit no-commit status. On
failure/cancellation, return exact boundary and stop. Do not guess a fix.
