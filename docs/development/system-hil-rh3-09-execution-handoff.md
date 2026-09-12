# RH3-09 Mode B startup-reservoir execution handoff

Status: approved physical reproducibility phase. This phase runs one fresh,
fixed RH3 matrix against current reviewed receiver firmware. It is not an RH3
acceptance record. No source, receiver, runner, row, parser, threshold, or
policy edit belongs in this phase.

## Goal

Run one fresh complete RH3 two-pass matrix using receiver firmware with the
reviewed 15-block I2S startup reservoir. Preserve all evidence. Determine
whether strict, unchanged receiver-tail validation now passes the frozen
schedule.

## Grounding

`rh3-20260815-08` is immutable failed evidence. Its first fresh mono and Mode
A rows passed. Its fresh one-CIS Mode B row failed receiver-tail validation:

```text
I2S underruns=1; Stream resets=1; Push failures=1;
offload submit/success mismatch
```

The retained receiver evidence showed a `142362 us` maximum RX callback gap,
then `i2s_nrfx: Next buffers not supplied on time`. The receiver correction is
narrow and software-reviewed:

```text
CONFIG_I2S_NRFX_TX_BLOCK_COUNT=15
STARTUP_SILENCE_BLOCKS=14
STARTUP_TOTAL_BLOCKS=15
BLOCK_COUNT=16
BLOCK_SIZE=1924
```

This queues fourteen distinct silence blocks plus first audio block before
`I2S_TRIGGER_START`, retaining one of sixteen slab blocks for first post-start
audio. No PCM slab allocation grew.

Review evidence is complete:

```text
tests/unit/perf:                 26/26 PASS
tests/unit/audio_i2s:            65/65 PASS
tests/unit/audio_i2s_identity:   63/63 PASS
check-test-matrix:               0 errors, 0 notes
audio_perf.c report coverage:    18/18 functions
fw-build-5340:                   PASS
fw-build-54l15:                  PASS
check-build-contract:            96/96 PASS
scripts/test_hil_runner.py:      83/83 PASS
tests/hil/rh3_matrix_test.py:    14/14 PASS
```

Fixture validation completed without hardware mutation. The runner remains the
sole owner of probe discovery, flashing, reset, serial, pairing, FLPR commands,
cleanup, and evidence. Do not use static probe or console assumptions: runner
resolves the local binding at execution time.

## Frozen scope

In scope:

1. Repeat no-mutation preflight.
2. Verify exact existing image identities.
3. Run exactly one fresh RH3 matrix as `rh3-20260820-01`.
4. Retain and report exact evidence for orchestrator review.

Out of scope:

- all repository edits, including result documentation, `STATUS.md`, source,
  receiver, HIL runner, rows, parser, thresholds, Kconfig, and build files;
- any rebuild, including `fw-build-hil-source`, `fw-build-54l15`, or source
  fixture rebuild;
- manual serial, `serial-mcp`, flash, reset, recovery, erase, pairing, RF or
  power adjustment, `btattach`, `bap_central.py`, FLPR shell command, or extra
  serial reader;
- child retry, matrix retry, changed run ID, output deletion, acceptance claim,
  commit, push, merge, tag, release, or PR.

The worktree is intentionally dirty. Preserve all existing changes. Do not
reset, stash, revert, clean, or broadly format it.

## Exact image identities

Use existing files only. Do not rebuild or substitute an image. Before hardware
work, require every SHA-256 to match:

```text
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
6d9caa89d272059858c2fce874776fa01b55ac0dfd8364a7ade8ddc43e7fe4cf  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

The receiver CPUAPP hash is new because it contains the reviewed reservoir and
performance-proof changes. The source CPUAPP and CPUNET hashes are unchanged
from the failed-CIS retry source reviewed before RH3-08. A mismatch is a hard
stop. Do not rebuild, edit, or select another artifact.

## Preflight

Run from repository root. Repeat all checks immediately before the matrix.

1. Require these paths to be absent:

```text
/tmp/opencode/hil-runs/rh3-20260820-01
/tmp/opencode/hil-runs/rh3-20260820-01.children.f271c7e01015
/tmp/opencode/hil-runs/rh3-20260820-01.junit.xml
```

If any path exists, stop. Do not select another ID.

2. Validate fixture and binding. This command does not mutate hardware:

```bash
nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

Expected result contains:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

3. Check image identities exactly:

```bash
sha256sum --check <<'EOF'
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
6d9caa89d272059858c2fce874776fa01b55ac0dfd8364a7ade8ddc43e7fe4cf  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
```

Any preflight failure is a hard stop. Return exact command output and do not
start the matrix.

## Execution

Use terminal-tool outer timeout `7200000` ms. Do not use shell `timeout`.

```bash
nix develop --command ./scripts/hil-runner.py run-rh3-matrix \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260820-01 \
  --junit /tmp/opencode/hil-runs/rh3-20260820-01.junit.xml
```

After this command starts, do not interact with hardware. Runner owns fixed
two-pass schedule, all twenty rows, flash lifecycle, and fail-fast behavior.
Do not start another command that might touch source builds or `tests/hil`
while it runs.

## Result handling

Do not edit repository documentation in this phase. Preserve evidence and
return control to orchestrator for independent evidence review.

For every outcome, return:

1. preflight commands and results;
2. matrix CLI exit status;
3. aggregate path, child-root path, aggregate `result.json`, external JUnit,
   and every attempted child path;
4. aggregate outcome, scheduled/attempted/passed/failed/cancelled counts,
   cleanup failures, and first failed boundary when present;
5. exact `images.json` hashes from aggregate or attempted child evidence;
6. relevant raw source/receiver evidence for a failed or cancelled row;
7. confirmation that no manual hardware action, repository edit, commit, or
   retry occurred.

If outcome is failed or cancelled, stop after evidence collection. Do not retry
one child or launch a replacement matrix. Do not guess a repair.

If outcome is passed, do not claim RH3 acceptance, analog output, DAC wiring,
audibility, RH4, release, or full system acceptance. Return evidence only for
orchestrator review.

## Review criteria after execution

Only a later orchestrator review may call matrix result reviewable. It must
confirm all of these:

1. CLI exit `0`.
2. Aggregate outcome `passed`.
3. `scheduled=20`, `attempted=20`, `passed=20`, `failed=0`, `cancelled=0`.
4. Aggregate cleanup failures empty.
5. Aggregate and each child retain `result.json`, JUnit, `MANIFEST.md`, and
   `SHA256SUMS`.
6. External JUnit has 20 tests and zero failures, errors, and skipped cases.
7. Every frozen source counter and strict receiver validation passes. Any
   unexpected warning, error, assert, or fault fails review. Named fault-window
   exceptions remain runner-owned and narrow.
