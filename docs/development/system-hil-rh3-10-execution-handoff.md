# RH3-10 post-start slab-backpressure execution handoff

Status: approved physical reproducibility phase. Run one fresh, fixed RH3
matrix against the reviewed receiver image. This is not an RH3 acceptance
record. No source, receiver, runner, row, parser, threshold, policy, or result
documentation edit belongs in this phase.

## Goal

Validate that bounded nRF54L15 post-start backpressure removes the RH3-09
Mode A slab-exhaustion failure while preserving the 15-block startup reservoir,
strict receiver-tail checks, fixed two-pass matrix, and nRF5340 no-wait
behavior.

Run exactly one fresh matrix as `rh3-20260820-02`. Preserve every produced
artifact. Return evidence to orchestrator for review; do not claim acceptance.

## Grounding

`rh3-20260820-01` is immutable failed evidence. Fresh mono passed, but fresh
Mode A failed receiver-tail validation after this receiver sequence:

```text
14 silence blocks + first audio block = 15 driver-owned slab blocks
BLOCK_COUNT = 16
first post-start render consumes block 16
second early Mode A render finds no free slab block
```

Exact RH3-09 receiver evidence:

```text
[00:26:51.063,809] <wrn> audio_i2s: I2S slab full — dropping frame

I2S underruns  : 1
Stream resets  : 0
Push failures  : 1
RX callback gap: 9234 us (max)
I2S write gap  : 13712 us (max)
offload submit=14040 success=14040 fallback=0
```

Source reached `streaming`; both Mode A streams completed `sc=12000` with
`sf=0`. FLPR was healthy. Failure was neither source completion nor FLPR.

Reviewed correction in `src/audio_i2s.c`:

- preserves `STARTUP_SILENCE_BLOCKS=14`, `STARTUP_TOTAL_BLOCKS=15`,
  `BLOCK_COUNT=16`, and `BLOCK_SIZE=1924`;
- preserves `CONFIG_I2S_NRFX_TX_BLOCK_COUNT=15` on both production targets;
- keeps startup pre-fill and repeat-fallback slab allocation at `K_NO_WAIT`;
- uses `K_MSEC(CONFIG_AUDIO_I2S_WRITE_TIMEOUT_MS)` only for primary slab
  allocation after DMA START when configured value is positive;
- keeps nRF5340 at timeout `0`, so its primary slab allocation stays no-wait;
- sets nRF54L15 timeout to `20 ms` for descriptor-queue backpressure and
  post-start primary slab exhaustion;
- leaves lifecycle admission open during the bounded wait. `audio_sink_stop()`
  waits for admitted pushes before PREPARE/DROP.

NCS v3.3.0 evidence:

- `zephyr/kernel/mem_slab.c:245-268`: no-wait exhaustion returns `-ENOMEM`; a
  finite wait uses the slab wait queue.
- `zephyr/kernel/mem_slab.c:271-301`: freed DMA blocks wake one pending slab
  waiter.
- `zephyr/drivers/i2s/i2s_nrfx.c:239-248`: nrfx frees released TX slab blocks
  in its data-handler path.
- `zephyr/drivers/i2s/i2s_nrfx.c:527-531`: `i2s_write()` uses configured
  descriptor-queue timeout.

The BAP receive path executes in `BT RX WQ`, not ISR. A finite wait is legal,
but it can delay queued Bluetooth receive work. This matrix is required to
validate strict end-to-end behavior. Any unexpected warning, error, fault, or
tail failure remains a failure; do not tune or retry during this phase.

Software review completed:

```text
tests/unit/audio_i2s:           67/67 PASS
tests/unit/audio_i2s_identity:  65/65 PASS
tests/unit/perf:                26/26 PASS
check-test-matrix:              0 errors, 0 notes
audio_i2s.c report coverage:    13/13 functions
audio_perf.c report coverage:   18/18 functions
fw-build-5340:                  PASS
fw-build-54l15:                 PASS
check-build-contract:           96/96 PASS
git diff --check:               PASS
```

Fresh local nRF54L15 build resolved:

```text
CONFIG_AUDIO_I2S_WRITE_TIMEOUT_MS=20
CONFIG_I2S_NRFX_TX_BLOCK_COUNT=15
RAM: 161668 B / 163840 B (98.67%)
```

The known `No SOURCES given to Zephyr library: drivers__watchdog` CMake
diagnostic is documented project-wide for this target. No new actionable build
warning was introduced.

## Frozen scope

In scope:

1. No-mutation fixture and output-root preflight.
2. Exact image-identity verification.
3. One fresh runner-owned RH3 matrix with run ID `rh3-20260820-02`.
4. Exact evidence collection after runner exit.

Out of scope:

- every repository edit, including `STATUS.md`, source, build files, runner,
  fixture, binding, rows, parser, threshold, result docs, or handoff edits;
- every rebuild, including `fw-build-hil-source`, `fw-build-54l15`,
  `fw-build-5340`, or source-fixture build;
- manual serial, `serial-mcp`, manual flashing/reset/recovery/erase, pairing,
  RF or power adjustment, `btattach`, `bap_central.py`, FLPR shell command, or
  extra serial reader;
- child retry, matrix retry, changed run ID, output deletion, result claim,
  commit, push, merge, tag, release, or PR.

Worktree is intentionally dirty. Preserve every existing change. Do not reset,
stash, revert, clean, or broadly format it.

## Exact image identities

Use existing files only. Before any hardware action, all four SHA-256 values
must match exactly:

```text
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
bdb898df7cac638def55a86073937d1e3d8cf6ae13a26a02fd79233add9d7de9  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

The receiver CPUAPP hash is new after post-start slab-backpressure correction.
Source hashes remain exact known-good source-fixture inputs. A mismatch is a
hard stop. Do not rebuild, substitute an image, or select another candidate.

## Output ownership

This phase consumes exactly these previously unused destinations:

```text
/tmp/opencode/hil-runs/rh3-20260820-02
/tmp/opencode/hil-runs/rh3-20260820-02.children.403d5f6a2dee
/tmp/opencode/hil-runs/rh3-20260820-02.junit.xml
```

They were absent during handoff preparation. Recheck all immediately before
execution. If any exists or is a symlink, stop. Do not delete it and do not
choose another ID.

Run IDs `rh3-20260815-01` through `rh3-20260815-08` and
`rh3-20260820-01` are immutable evidence. Never retry, delete, overwrite, or
reuse them.

## Preflight

Run commands from repository root, sequentially, immediately before matrix.
Do not run source builds or `tests/hil` concurrently with this phase.

1. Require all exact output destinations absent:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260820-02
test ! -e /tmp/opencode/hil-runs/rh3-20260820-02.children.403d5f6a2dee
test ! -e /tmp/opencode/hil-runs/rh3-20260820-02.junit.xml
```

2. Validate logical fixture and local binding. This command performs no
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

4. Run `git diff --check`. Record dirty `git status --short`; do not modify
the worktree to make it clean.

Any preflight failure is a hard stop. Return exact output. Do not start runner,
do not rebuild, and do not choose a replacement ID.

## Execution

Use terminal-tool outer timeout `7200000` ms. Do not use shell `timeout`.

```bash
nix develop --command ./scripts/hil-runner.py run-rh3-matrix \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260820-02 \
  --junit /tmp/opencode/hil-runs/rh3-20260820-02.junit.xml
```

Once runner starts, it solely owns probe discovery, flash lifecycle, reset,
serial, pairing, FLPR control, cleanup, fixed two-pass schedule, and evidence.
Do not start any other hardware, serial, source-build, or HIL command while it
runs. Do not interrupt it except genuine user-requested cancellation.

## Result handling

Do not edit repository documentation in this phase. Preserve every result.

For every outcome, return:

1. Exact preflight commands and outputs.
2. Matrix CLI exit status.
3. Aggregate path, child-root path, aggregate `result.json`, external JUnit,
   and every attempted child path.
4. Aggregate outcome; scheduled, attempted, passed, failed, and cancelled
   counts; cleanup failures; first failed boundary when present.
5. Exact image hashes from aggregate or child `images.json`.
6. Relevant raw source and receiver evidence for every failed/cancelled row.
7. Confirmation that runner was sole hardware owner and that no repository
   edit, commit, retry, or manual hardware action occurred.

If failed or cancelled, stop after evidence collection. Do not retry a child,
launch another matrix, diagnose a repair, or modify files.

If passed, return evidence only. Do not claim RH3 acceptance, analog output,
DAC wiring, audibility, RH4, release, or full system acceptance.

## Orchestrator review criteria

Only later review may mark result reviewable. Require all of these:

1. CLI exit `0`.
2. Aggregate outcome `passed`.
3. `scheduled=20`, `attempted=20`, `passed=20`, `failed=0`, `cancelled=0`.
4. Empty aggregate cleanup failures.
5. Aggregate and every child retain `result.json`, JUnit, `MANIFEST.md`, and
   `SHA256SUMS`.
6. External JUnit has 20 tests, zero failures, zero errors, and zero skipped.
7. Exact image identity retained throughout evidence.
8. Every strict source and receiver parser check passes. No unexpected warning,
   error, assert, timeout, I2S fault, stream reset, push failure, or FLPR
   mismatch is acceptable. Named fault-window behavior remains narrow and
   runner-owned.
