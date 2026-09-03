# RH3-07 replacement matrix execution handoff

Status: approved physical reproducibility phase. This is not an acceptance
record. No production, fixture-source, receiver, runner, or acceptance-policy
code change belongs in this phase.

## Goal

Run one fresh, complete RH3 matrix candidate after review of the immutable
`rh3-20260815-06` failure. Preserve evidence and determine whether the current
known-good source and receiver image pair completes both frozen passes.

## Grounding

`rh3-20260815-06` failed its first fresh mono child at receiver tail:

```text
invalid receiver status: I2S underruns=1; Stream resets=1; Push failures=1
```

The retained receiver evidence recorded a `268003 us` maximum RX callback gap,
one `i2s_nrfx: Next buffers not supplied on time`, and `8884` PLC frames. It
also recorded a healthy FLPR plane with `submit=success=12650`, zero offload
faults, and zero fallback.

This does not establish a deterministic source-image regression. The prior
`rh3-20260815-04` fresh mono child used the same source CPUAPP SHA-256
`fea5ff443c0feaeaf9b1133c60a79c1884dc280714981ca3f88a91cec68a0049` and the
same receiver CPUAPP/FLPR SHA-256 values, then passed with `10` PLC frames,
zero receiver faults, and a `90232 us` maximum RX callback gap. Do not change
RF power, source outstanding depth, source/controller TX buffer counts,
receiver startup reservoir, PLC policy, or strict receiver validation based on
one unexplained physical-loss observation.

NCS v3.3.0 documents `bt_bap_stream_ops.sent` as controller completion, which
may mean enqueue, on-air transmission, or flush. It is not proof of CIS
air-time pacing. The current source completed every expected send callback in
both the passing and failing rows, so source terminal counters do not explain
the receiver-side loss.

## Scope

1. Record verified RH3-06 evidence in active internal HIL status documents.
2. Validate current local fixture binding and exact existing image hashes.
3. Run exactly one new fixed two-pass RH3 matrix with ID `rh3-20260815-07`.
4. Record only observed RH3-07 result and evidence paths after completion.

## Out of scope

- No source or receiver firmware edits.
- No runner, row, parser, threshold, warning-policy, or hardware-binding edits.
- No source or receiver rebuild.
- No manual serial, flash, reset, recovery, pairing, RF, or FLPR action.
- No retry of any child or matrix after a failure/cancellation.
- No `STATUS.md` edit, acceptance claim, commit, push, PR, tag, or release work.

## Required files

- `docs/development/system-hil-resume-state.md`
- `docs/development/system-hil-rh3-software-status.md`
- `docs/development/system-hil-rh3-physical-execution-handoff.md`
- `docs/development/system-hil-rh3-07-execution-handoff.md`

## Preflight

Run from repository root. Keep source build idle. Do not run `tests/hil` at the
same time as any source build.

1. Verify `/tmp/opencode/hil-runs/rh3-20260815-07` and its JUnit path do not
   exist. If either exists, stop and report it. Do not select another ID.
2. Run fixture-only validation:

```bash
nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

3. Hash existing images without rebuilding. Values must equal:

```text
build/hil-source/app/zephyr/zephyr.hex
fea5ff443c0feaeaf9b1133c60a79c1884dc280714981ca3f88a91cec68a0049

build/hil-source/hci_ipc/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48

build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
4838edf32ef2754f02757756d70fabfe009ad4ae6a079a69081b8293657fb6a4

build/nrf54l15/flpr/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2
```

Stop and report any mismatch. Do not rebuild, substitute, or update expected
hashes.

## Execution

Use only matrix runner. Invoke terminal tool with a `7200000 ms` outer timeout;
do not wrap command in shell `timeout`.

```bash
nix develop --command ./scripts/hil-runner.py run-rh3-matrix \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260815-07 \
  --junit /tmp/opencode/hil-runs/rh3-20260815-07.junit.xml
```

Matrix runner is exclusive hardware owner. Do not use `serial-mcp`, manual
serial readers, `btattach`, `bap_central.py`, manual flashing/reset/recovery,
manual pairing, manual FLPR commands, or RF configuration while it runs.

## Result handling

- Preserve every aggregate and child file exactly.
- On failed or cancelled outcome, do not retry a child or launch another matrix.
- Update named internal HIL documents with RH3-06 and RH3-07 exact outcome,
  evidence paths, first failed boundary, relevant raw logs/counters, image
  hashes, and the absence or presence of cleanup failures.
- Run `git diff --check` and `git status --short` after documentation updates.
- Do not commit.

## Acceptance criteria for review

Only report matrix as reviewable when all conditions hold:

1. CLI exit `0`.
2. Aggregate outcome `passed`.
3. `scheduled=20`, `attempted=20`, `passed=20`, `failed=0`, `cancelled=0`.
4. Empty aggregate cleanup failures.
5. Every child and aggregate retains `result.json`, JUnit, `MANIFEST.md`, and
   `SHA256SUMS`.
6. No strict receiver fault, unexpected warning/error/assertion, or source
   terminal counter violation.

Return exact commands/results, image hashes, evidence paths, changed files,
`git diff --check` result, dirty status, and explicit no-commit status.
