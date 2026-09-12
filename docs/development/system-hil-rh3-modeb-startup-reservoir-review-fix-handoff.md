# RH3 Mode B startup-reservoir review-fix handoff

Status: software review follow-up. This phase closes test evidence and current
documentation gaps found after the startup-reservoir change. It does not
authorize hardware execution or RH3 acceptance.

## Goal

Make the startup-reservoir software phase reviewable by:

1. proving the new public audio-performance timing wrappers through a
   deterministic direct unit test;
2. completing the `audio_perf.c` public API outcome ledger;
3. correcting current I2S suite counts and coverage descriptions; and
4. rerunning the required software checks.

## Review evidence

The completed reservoir implementation is correct in its narrow scope:

```text
CONFIG_I2S_NRFX_TX_BLOCK_COUNT=15
STARTUP_SILENCE_BLOCKS=14
STARTUP_TOTAL_BLOCKS=15
BLOCK_COUNT=16
BLOCK_SIZE=1924
```

Focused suites passed:

```text
tests/unit/audio_i2s:          65/65 PASS
tests/unit/audio_i2s_identity: 63/63 PASS
```

Both production builds passed. nRF54L15 reports RAM `161668/163840`; its I2S
slab remains `0x7840` bytes, so this change did not add a PCM slab block.

`python3 scripts/check-build-contract.py --nrf5340 build/nrf5340 --nrf54l15
build/nrf54l15` passed `96/96`.

Review found two incomplete evidence issues:

### Missing matrix outcomes

`python3 scripts/check-test-matrix.py --repo-root .` reports exactly:

```text
public API missing outcome: src/audio_perf.c: audio_perf_i2s_dma_restart
public API missing outcome: src/audio_perf.c: audio_perf_i2s_dma_started
public API missing outcome: src/audio_perf.c: audio_perf_i2s_write_end
public API missing outcome: src/audio_perf.c: audio_perf_i2s_write_failure
public API missing outcome: src/audio_perf.c: audio_perf_i2s_write_start
public API missing outcome: src/audio_perf.c: audio_perf_rx_callback_start
```

### Public-wrapper coverage and stale counts

Report-only native coverage at
`/tmp/opencode/rh3-modeb-review-coverage/numeric-summary.json` shows
`src/audio_perf.c` at `13/17` functions. The direct timing-injection tests
exercise internal accumulator logic, but four public wrappers remain unhit:

- `audio_perf_rx_callback_start`;
- `audio_perf_i2s_dma_started`;
- `audio_perf_i2s_write_start`;
- `audio_perf_i2s_write_end`.

The current I2S documentation claims 62/60 tests. The existing shared
performance-timing tests already make actual focused counts 65/63. This is
documentation drift, not a changed reservoir-test count.

## Exact design

### 1. Add a narrow deterministic test clock seam

Touch only `src/audio_perf.c`, `src/audio_perf.h`, and
`tests/unit/perf/src/test_perf.c` for this behavior.

Under `CONFIG_ZTEST` only, add a test clock override API:

```c
void audio_perf_test_set_cycle_now(uint32_t now);
void audio_perf_test_clear_cycle_now(void);
```

These declarations and implementations must be test-only, like the existing
`audio_perf_test_inject_*` helpers. They must be static inline no-ops in the
`!CONFIG_AUDIO_PERF_MEASUREMENT` header branch, so config-off test builds stay
link-safe. They are not production APIs and must not appear in the public API
ledger. Keep their implementation inside the existing `GCOVR_EXCL_START/STOP`
test-only region.

Factor the cycle read used by these public timing wrappers through one internal
helper:

- `audio_perf_cycle_start`;
- `audio_perf_cycle_end`;
- `audio_perf_i2s_dma_started`;
- `audio_perf_i2s_write_start`;
- `audio_perf_i2s_write_end`.

Production behavior must remain exactly `k_cycle_get_32()`. In a ZTEST build,
the helper returns the injected clock value only while the override is active;
otherwise it still returns `k_cycle_get_32()`. Reset/clear the override in the
perf test fixture so no test leaks an artificial timestamp into another test.

Do not change metric state shape, locking, production signatures, snapshot
text, or runtime behavior.

### 2. Add one public-boundary timing-wrapper test

In `tests/unit/perf/src/test_perf.c`, add one deterministic test that uses the
new test clock seam and calls the public wrappers, not only the existing
internal-injection helpers.

Required exact observable sequence:

1. set test clock to 100;
2. call `audio_perf_i2s_dma_started()`;
3. assert `audio_perf_i2s_write_start()` returns 100;
4. set test clock to 130 and call `audio_perf_i2s_write_end(100, true)`;
5. call `audio_perf_rx_callback_start(200)` then
   `audio_perf_rx_callback_start(260)`;
6. snapshot and prove exact values:
   - I2S write duration maximum = 30;
   - successful-write completion gap maximum = 30;
   - RX callback gap maximum = 60.
7. clear the test clock before exit, including fixture reset for future tests.

This one public-boundary test covers the four previously unhit wrappers while
the existing `test_i2s_write_failure_categories_and_restart` continues to
cover `audio_perf_i2s_write_failure` and `audio_perf_i2s_dma_restart`.

Do not use wall-clock sleeps, busy waits, polling, or timing tolerances.

### 3. Complete exact matrix records

In the existing `src/audio_perf.c` entry of `tests/test-matrix.json`, add six
truthful `public_outcomes` records:

| API | Outcome | Witness |
|---|---|---|
| `audio_perf_i2s_write_failure` | `void` | `test_i2s_write_failure_categories_and_restart` |
| `audio_perf_i2s_dma_restart` | `void` | `test_i2s_write_failure_categories_and_restart` |
| `audio_perf_rx_callback_start` | `void` | new public-wrapper timing test |
| `audio_perf_i2s_dma_started` | `void` | new public-wrapper timing test |
| `audio_perf_i2s_write_start` | `UINT32` | new public-wrapper timing test |
| `audio_perf_i2s_write_end` | `void` | new public-wrapper timing test |

Use the actual new ZTEST name consistently in every matrix record. Do not add
state transitions, exclusions, vague outcomes, or duplicate records.

### 4. Correct current documentation only

Update only current facts in:

- `docs/testing/t3-audio-i2s-tests.md`:
  counts are ASRC `65/65`, identity `63/63`, total `128/128`; explain that
  three shared performance-timing regressions account for the extra three per
  suite. Preserve the 15-block startup facts.
- `docs/testing/coverage-matrix.md`:
  the `audio_i2s.c` row must say 65/63 and accurately describe 15-block
  startup plus current timing telemetry. The `audio_perf.c` row must state
  direct deterministic coverage of public timing wrappers through the injected
  test clock.

Do not edit historical test-count evidence or `STATUS.md`. Do not update
`tests/coverage-baseline.json`: this intentionally dirty worktree cannot write
a committed baseline, and the current report-only coverage artifact is
diagnostic evidence only.

## Scope and prohibitions

In scope:

- `src/audio_perf.c`
- `src/audio_perf.h`
- `tests/unit/perf/src/test_perf.c`
- `tests/test-matrix.json`
- `docs/testing/t3-audio-i2s-tests.md`
- `docs/testing/coverage-matrix.md`
- this handoff only if a factual correction is needed

Out of scope:

- `src/audio_i2s.c`, `prj.conf`, queue/slab/startup constants, I2S timeout,
  source fixture, HIL runner, rows, parser, QoS, PLC/cadence, timing/drift,
  FLPR, pairing, release files, `STATUS.md`, coverage baseline;
- hardware, flashing, reset, serial, RF, pairing, `btattach`,
  `bap_central.py`, HIL matrix execution, or source-image rebuild;
- commits, pushes, merges, PRs, tags, releases, stashing, reset, or broad
  formatting.

## Worktree discipline

The worktree is intentionally dirty. Inspect every touched-file diff first.
Preserve unrelated in-progress performance instrumentation and documentation
changes. Do not reset, stash, revert, or reformat unrelated files. No commit.

## Verification

Run from repository root inside the NCS v3.3.0 shell:

```bash
env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/rh3-modeb-perf-review \
  tests/unit/perf -p -t run

env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/rh3-modeb-i2s-asrc-review \
  tests/unit/audio_i2s -p -t run

env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/rh3-modeb-i2s-identity-review \
  tests/unit/audio_i2s_identity -p -t run

python3 scripts/check-test-matrix.py --repo-root .

nix develop --command bash scripts/test-coverage.sh \
  --report-only \
  --output /tmp/opencode/rh3-modeb-review-coverage-after

fw-build-5340
fw-build-54l15
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
git diff --check
git status --short
```

The report-only coverage result must show all `src/audio_perf.c` functions
executed. Do not try baseline enforcement or baseline writing on this dirty
tree. All actionable warnings are errors.

## Completion report

Return exact files changed, exact public-wrapper test behavior, focused test
counts, matrix result, `audio_perf.c` function coverage result, production
build/build-contract results, diff/status results, no-commit/no-hardware
confirmation, and any blocker. Stop rather than widening scope or weakening
the ledger, coverage, or strict HIL validation.
