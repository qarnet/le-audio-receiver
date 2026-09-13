# RH3 Mode B startup-reservoir software fix handoff

Status: proposed software-only repair. This is not an RH3 acceptance record and
does not authorize hardware execution.

## Goal

Prevent the nRF54L15 receiver's first one-CIS Mode B startup gap from draining
nrfx I2S into ERROR. Keep strict RH3 receiver-tail validation unchanged.

The implementation must increase the existing I2S startup reservoir and nrfx
TX descriptor queue without increasing the PCM slab allocation.

## Grounding

RH3-08 is immutable failed evidence. Fresh mono and fresh Mode A passed, but
fresh Mode B failed at receiver tail:

```text
pass1 row3 rh3.fresh_mode_b_48_4_1 stopped matrix: receiver tail
invalid receiver status: I2S underruns=1; Stream resets=1; Push failures=1;
offload submit/success mismatch
```

Retained evidence:

```text
/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/
  rh3-20260815-08.p1.r3.rh3.fresh_mode_b_48_4_1.d330fbe1d3e9/
    receiver-status.txt
```

That receiver stream used one 240-byte, two-channel, 10 ms Mode B CIS. It
logged:

```text
[01:10:43.036,812] <inf> audio_i2s: I2S DMA started
[01:10:43.286,513] <err> i2s_nrfx: Next buffers not supplied on time
[01:10:43.329,325] <err> i2s_nrfx: Cannot write in state: 4
[01:10:43.329,336] <wrn> audio_i2s: I2S underrun, restarting DMA

sink_push max 8201 us
RX callback gap max 142362 us
I2S write gap max 25531 us
I2S write time max 6051 us
Slab free min/max 2 / 16
```

Current startup queues ten silence blocks plus first data block, eleven total.
At the nRF54L15's measured 47,619 Hz output, that is about 110 ms for 10 ms
source frames. It cannot cover the observed one-CIS delivery gap plus
post-callback decode/offload/write work.

The same RH3-08 artifact shows why this is receiver buffering work, not a
source retry or runner-policy issue:

- source reached `streaming` and `scored_complete`;
- terminal source counters were `sub=12320`, `sc=12000`, `sf=0`, `cb=12320`;
- FLPR stayed ACTIVE, with no timeout/full/stale/sequence/frame/CRC/payload
  faults or fallback;
- fresh Mode A had maximum RX callback gap 10850 us and zero receiver faults.

Installed NCS v3.3.0 evidence:

- `zephyr/drivers/i2s/Kconfig.nrfx` defines
  `CONFIG_I2S_NRFX_TX_BLOCK_COUNT` as an unconstrained integer with default 4;
  15 is valid.
- `zephyr/drivers/i2s/i2s_nrfx.c` stores pointer-plus-size descriptors in the
  TX message queue. A full queue waits for `struct i2s_config.timeout`; a
  successfully written slab block is driver-owned.
- The driver starts from one queued TX block, obtains later blocks from its
  queue, and enters ERROR only after it cannot supply a next block in time.
- `zephyr/include/zephyr/drivers/i2s.h` requires at least two slab blocks per
  queue, not a one-to-one descriptor-to-slab allocation.

Current resolved nRF54L15 values:

```text
CONFIG_I2S_NRFX_TX_BLOCK_COUNT=12
CONFIG_AUDIO_I2S_WRITE_TIMEOUT_MS=20
BLOCK_COUNT=16
BLOCK_SIZE=1924
```

Current map evidence:

```text
build/nrf54l15/le-audio-receiver/zephyr/zephyr_final.map
i2s_slab buffer: 0x7840 = 30784 bytes = 16 × 1924
__kernel_ram_end: 0x20028000
last allocated noinit address: 0x200276ac
remaining mapped space: 0x954 = 2388 bytes
```

Do not increase `BLOCK_COUNT`. The chosen change adds only three nrfx TX queue
descriptors on the 32-bit target, not another 1924-byte PCM slab block.

## Exact implementation

### 1. Increase queue and startup depth, keep slab depth

In `prj.conf`:

```text
CONFIG_I2S_NRFX_TX_BLOCK_COUNT=15
```

In `src/audio_i2s.c`:

```c
#define BLOCK_COUNT 16
#define STARTUP_SILENCE_BLOCKS 14
#define STARTUP_TOTAL_BLOCKS (STARTUP_SILENCE_BLOCKS + 1)
```

Keep `BLOCK_SIZE`, `DRIFT_THRESHOLD`, rate conversion, ASRC/offload, repeat
fallback, timeout selection, and all lifecycle behavior unchanged.

Retain the existing compile-time capacity checks. Add a compile-time assertion
that startup total is strictly less than `BLOCK_COUNT`, so one slab block is
reserved for a first post-START push. The assertion message must state that
purpose.

This creates a fifteen-block startup queue:

- 14 distinct zero-filled, rate-converter-sized silence blocks;
- 1 first audio data block;
- then `I2S_TRIGGER_START`.

At 10 ms it supplies about 150 ms before later successful callbacks can refill
the driver queue. The slab remains 16 blocks, so no PCM RAM budget increase is
permitted. `CONFIG_I2S_NRFX_TX_BLOCK_COUNT=15` permits all fifteen pre-start
writes. After START, nrfx takes an initial block and normal incoming audio can
refill toward the existing 16-block slab limit.

### 2. Keep ownership and failure semantics exact

Do not change:

- successful `i2s_write()` ownership transfer;
- failed-write caller ownership/freeing;
- distinct-pointer rule;
- `-EIO` recovery via PREPARE followed by fresh startup on next push;
- stop's PREPARE then DROP ordering;
- strict source/receiver HIL checks;
- source HIL pacing, QoS, signal, HIL1 schema, runner rows, parser, or
  thresholds;
- PLC/cadence policy, timing/drift, FLPR protocol, queue timeout, or retry
  behavior.

The offload submit/success difference in RH3-08 is downstream evidence from
the failed I2S push. Do not exempt it in `scripts/hil/receiver.py` or relax
receiver-tail validation.

### 3. Update direct regression proof

Keep this a shared test change, so both ASRC/NONE and identity/APLL suites use
the same production-source proof.

Update these existing constants and dependent wording:

- `tests/unit/audio_i2s_common/audio_i2s_test_helpers.h`:
  `STARTUP_SILENCE_BLOCKS=14`, `STARTUP_TOTAL_BLOCKS=15`, data write index 14,
  first steady write index 15, and truthful comments/messages.
- `tests/unit/audio_i2s_common/fake_i2s.h`:
  `FAKE_I2S_QUEUE_CAPACITY=15`, matching production Kconfig.
- `tests/unit/audio_i2s_common/test_sink_startup.c`:
  rename/update the startup regression to prove fourteen ordered simulated DMA
  completions leave one driver-owned startup block. Preserve its public
  behavior assertions: sink remains started/configured, no duplicate writes,
  and all other slab blocks are free.

Update only literal names/comments/assertion messages tied to the old
eleven-block contract in these existing shared tests:

- `tests/unit/audio_i2s_common/test_sink_common.c`
- `tests/unit/audio_i2s_common/test_sink_concurrent.c`
- `tests/unit/audio_i2s_common/test_sink_stop.c`
- `tests/unit/audio_i2s_common/test_sink_startup.c`
- `tests/unit/audio_i2s_common/audio_i2s_test_helpers.h`
- `tests/unit/audio_i2s/src/test_asrc_path.c`
- `tests/unit/audio_i2s_identity/src/test_identity_steady.c`

Do not add a new test solely to change this existing shared reservoir witness.
Its loop already uses `STARTUP_SILENCE_BLOCKS`; changing the contract makes it
prove fourteen-completion coverage in both variants. Test count does not
change.

### 4. Correct current contract documentation

Update only startup-depth facts in:

- `docs/testing/behavior-contract.md`:
  I2S-002 must state fourteen silence plus data, fifteen total, 112.5 ms at
  7.5 ms and about 150 ms at 10 ms. I2S-006 must say fresh fourteen-silence
  pre-fill.
- `docs/testing/t3-audio-i2s-tests.md`:
  replace old 11/ten/12-entry startup and queue facts with 15/fourteen/15-entry
  facts. Keep current suite counts unchanged, because no new test is added.
- `docs/testing/coverage-matrix.md`:
  update only the `audio_i2s.c` row to describe 15-block transactional startup
  and the fourteen-completion reservoir witness. Its current 61/59 count is
  stale relative to the already-present queue-wait tests; correct that row to
  current 62/60 while preserving unrelated in-progress `audio_perf.c` wording.

No public user-guide change is needed. Do not edit `STATUS.md`, historical RH3
result records, or retained `/tmp` evidence.

## Scope

In scope:

- receiver I2S startup depth and nrfx TX descriptor depth;
- direct shared I2S regression proof;
- current I2S contract/testing documentation.

Out of scope:

- hardware flashing, reset, serial, RF, pairing, `btattach`, `bap_central.py`,
  `serial-mcp`, HIL matrix execution, or any physical RH3 claim;
- source fixture code, HIL source image rebuild, `tests/hil`, source retry,
  source pacing, QoS, runner/parser/policy/threshold changes;
- `BLOCK_COUNT` or any new PCM/static buffer allocation;
- I2S timeout, drift, ASRC, FLPR, PLC/cadence, lifecycle, or pairing changes;
- `STATUS.md`, VERSION, release/tag/PR/push/merge actions;
- resetting, stashing, reverting, or reformatting unrelated dirty work.

## Worktree discipline

This repository is intentionally dirty. Before editing, inspect the current
diff for every file you touch. Preserve existing unrelated edits, especially
the current performance-instrumentation changes in `src/audio_i2s.c`,
`src/audio_perf.[ch]`, and documentation. Do not use broad formatting or reset
commands. Do not commit this phase.

## Verification

Run from repository root inside the NCS v3.3.0 development shell:

```bash
env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/rh3-modeb-i2s-asrc \
  tests/unit/audio_i2s -p -t run

env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/rh3-modeb-i2s-identity \
  tests/unit/audio_i2s_identity -p -t run

fw-build-5340
fw-build-54l15
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
git status --short
```

Both production builds must pass with no new actionable compiler, Kconfig,
linker, or memory diagnostics. Do not run `fw-build-hil-source` concurrently
with any HIL Python test. Do not run an HIL matrix in this phase.

## Completion report

Return:

1. exact files changed and pre-existing dirty edits preserved;
2. final startup/queue/slab values and why slab RAM did not grow;
3. focused test results, production build results, build-contract and
   test-matrix results;
4. `git diff --check` and `git status --short` results;
5. explicit no-commit/no-hardware status;
6. blockers or deviations. Stop and report rather than inventing a larger
   buffering architecture or weakening validation.
