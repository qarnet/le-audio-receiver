# RH3 Mode A post-start slab-backpressure fix handoff

Status: focused software repair after reviewed RH3-09 physical failure. This
phase changes no HIL runner rule and performs no hardware action. It does not
authorize another matrix.

## Goal

Keep the reviewed 15-block startup reservoir while preventing the second
post-start render block from being dropped when every one of the 16 PCM slab
blocks is still driver-owned. nRF54L15 must wait only for one bounded DMA block
release; nRF5340 must retain its existing immediate no-wait behavior.

## RH3-09 evidence

`rh3-20260820-01` is immutable failed evidence. Exact image identities were
verified before runner-owned flashing. Fresh mono passed. Fresh Mode A failed
at receiver tail, so the fixed schedule stopped before fresh Mode B.

```text
aggregate: /tmp/opencode/hil-runs/rh3-20260820-01/
children:  /tmp/opencode/hil-runs/rh3-20260820-01.children.f271c7e01015/

outcome=failed
scheduled=20 attempted=2 passed=1 failed=1 cancelled=0
cleanup_failures=[]
first_failed_boundary=pass1 row2 rh3.fresh_mode_a_48_4_1 stopped matrix: receiver tail
```

Failed child:

```text
/tmp/opencode/hil-runs/rh3-20260820-01.children.f271c7e01015/
  rh3-20260820-01.p1.r2.rh3.fresh_mode_a_48_4_1.ce1046d1e077/
```

Receiver evidence:

```text
[00:26:51.034,150] <inf> audio_i2s: I2S DMA started
[00:26:51.063,809] <wrn> audio_i2s: I2S slab full — dropping frame

I2S underruns  : 1
Stream resets  : 0
Push failures  : 1
RX callback gap: 9234 us (max)
I2S write gap  : 13712 us (max)
offload submit=14040 success=14040 fallback=0
```

Source reached `streaming` and both Mode A streams reached `sc=12000` with
`sf=0`. This is neither source completion failure nor FLPR failure. It is not
the RH3-08 Mode B callback-gap failure. The fresh mono child passed with the
same exact image tuple.

## Root cause and chosen repair

Current receiver startup intentionally does this:

```text
14 silence blocks + 1 first audio block = 15 driver-owned slab blocks
BLOCK_COUNT = 16
```

NCS v3.3.0 source proves TX descriptor capacity is separate from slab
ownership:

- `zephyr/drivers/i2s/Kconfig.nrfx` defines
  `CONFIG_I2S_NRFX_TX_BLOCK_COUNT` as a descriptor-queue length.
- `zephyr/drivers/i2s/i2s_nrfx.c:503-564` transfers ownership only after a
  successful `k_msgq_put()`.
- `start_transfer()` at `:567-617` removes first descriptor but keeps its slab
  block in `last_tx_buffer`; descriptor dequeue does not free PCM memory.
- `data_handler()` at `:239-293` frees a completed TX block only after nrfx
  reports it released.
- `zephyr/include/zephyr/kernel.h:5833-5855` specifies `k_mem_slab_alloc()`:
  `K_NO_WAIT` returns `-ENOMEM`; an expired finite wait returns `-EAGAIN`.

Therefore first post-start output can consume block 16 before any DMA release.
Second output then reaches the existing `K_NO_WAIT` allocation and drops. This
is exactly reachable for early Mode A event pairs. It is independent of the
nrfx TX descriptor wait configured by `i2s_config.timeout`.

Do not reduce startup depth: fourteen total blocks would undercut the
approximately 150 ms reservoir selected to cover RH3-08's `142362 us` Mode B
gap plus render work. Do not increase `BLOCK_COUNT`: current nRF54L15 build
uses `161668/163840` bytes, and no extra 1924-byte PCM block is needed.

Chosen repair: primary rendered-block allocation after DMA START uses the
already configured finite nRF54L15 backpressure budget (`20 ms`). It waits for
one driver DMA release only when the slab is exhausted. Startup pre-fill and
emergency repeat allocation remain nonblocking. nRF5340 has configured timeout
zero and remains nonblocking.

`audio_sink_push()` is called from thread/work context, not ISR. It increments
its admission lease before `do_push()` and releases `stream_mutex` before
allocation. `audio_sink_stop()` already waits for admitted pushes to drain, so
one bounded wait is safe and preserves teardown ownership.

## Exact implementation

### 1. Bound only post-start primary slab allocation

Touch `src/audio_i2s.c`.

1. Add one private helper used by both `fill_block_asrc()` and
   `fill_block_identity()`. It takes `bool stream_started` and calls
   `k_mem_slab_alloc(&i2s_slab, block, timeout)`.
2. Select `timeout` exactly:
   - `K_MSEC(CONFIG_AUDIO_I2S_WRITE_TIMEOUT_MS)` only when
     `stream_started` is true and the configured value is greater than zero;
   - `K_NO_WAIT` otherwise.
3. Snapshot `started` once at entry to `do_push()` and pass that snapshot to
   both resampler fill functions. Keep existing push timing behavior: the
   `sink_push` performance interval begins before this allocation, so any
   bounded wait remains observable in that existing metric.
4. Startup calls pass false and remain transactional/no-wait. Do not change
   startup order, `STARTUP_SILENCE_BLOCKS=14`, `STARTUP_TOTAL_BLOCKS=15`,
   `BLOCK_COUNT=16`, block size, driver queue count, rate conversion, ASRC,
   offload, repeat fallback, I2S write logic, `-EIO` recovery, or stop order.
5. Keep existing allocation-failure log/counter behavior. A finite wait timeout
   returns `-EAGAIN`, logs the existing slab-full warning, increments the
   existing I2S-underrun counter once, performs no I2S write, and leaves a
   started/configured sink intact. A zero-timeout attempt retains `-ENOMEM`.
6. Do not use `K_FOREVER`, spin/poll loops, sleeps, heap memory, a second slab,
   a descriptor-count change, or an I2S driver fork.

### 2. Add deterministic direct regressions

Touch only these test files for behavior:

- `tests/unit/audio_i2s_common/audio_i2s_test_hook.h`
- `src/audio_i2s.c` test-only hook region
- `tests/unit/audio_i2s_common/test_sink_queue_wait.c`

Add narrow `AUDIO_I2S_NATIVE_TEST`-only observability for the next primary slab
allocation:

```c
void audio_i2s_test_arm_main_slab_alloc(struct k_sem *entered);
bool audio_i2s_test_last_main_slab_alloc_waited(void);
void audio_i2s_test_note_main_slab_alloc(bool waits);
```

The private allocation helper calls `audio_i2s_test_note_main_slab_alloc()`
records whether a positive timeout is being used and signals the armed semaphore
once. Reset clears the semaphore pointer and recorded flag. Production builds
contain no hook branch or symbol. Keep hook implementation inside the existing
GCOV exclusion region.

In `test_sink_queue_wait.c`, reuse bounded semaphores and worker infrastructure.
Never synchronize with a timing sleep or polling loop. Add these two shared
ZTEST cases, so both production variants execute real `audio_i2s.c`:

1. `test_post_start_slab_backpressure_respects_variant_timeout`
   - Start normal 15-block stream.
   - Perform one post-start primary push without releasing DMA blocks. It uses
     the sixteenth and final slab block.
   - ASRC/nRF54L15 branch: arm the allocation semaphore; start second push in
     worker; wait for semaphore; prove zero free blocks and positive slab-wait
     selection; explicitly release one fake driver-owned startup block; worker
     must return 0. Prove 16 driver-owned blocks again, zero underrun/push/I2S
     write failure telemetry, started/configured state retained, and no duplicate
     pointer submission.
   - identity/nRF5340 branch: second direct push returns `-ENOMEM` with zero
     wait selection, one underrun count, no extra write, started/configured
     state retained, and no duplicate pointer submission.
2. `test_post_start_slab_backpressure_timeout_is_bounded`
   - Repeat full-slab setup but never release a fake DMA block.
   - ASRC/nRF54L15 returns `-EAGAIN` after its configured finite wait. Assert
     positive wait selection, exactly one underrun, no extra write or I2S write
     failure telemetry, state retention, and no duplicate submission.
   - identity/nRF5340 returns immediate `-ENOMEM` with no-wait selection and
     same ownership/state assertions.

The tests must not assert private production data layout or elapsed wall-clock
duration. Semaphore-controlled release proves public `audio_sink_push()`
behavior at real slab/I2S boundaries.

### 3. Make timeout semantics truthful

Update only these current facts:

- `Kconfig`: retain symbol name/range/value semantics, but explain that
  `CONFIG_AUDIO_I2S_WRITE_TIMEOUT_MS` bounds both a full nrfx TX descriptor
  queue and post-start primary slab allocation. State that startup pre-fill and
  repeat fallback stay no-wait; `0` remains no-wait; `K_FOREVER` is never used.
- `boards/nrf54l15dk_nrf54l15_cpuapp.conf`: retain value `20`; describe its
  bounded post-start backpressure role and safe drain behavior.
- `docs/testing/behavior-contract.md`: preserve I2S-002 reservoir and
  I2S-003 ownership. Clarify post-start finite slab allocation (`-EAGAIN` on
  timeout for nRF54L15, `-ENOMEM` no-wait for nRF5340), unchanged nonblocking
  startup/repeat allocation, and the same configured 20 ms bound independently
  applied to descriptor-queue and post-start slab backpressure.
- `docs/testing/t3-audio-i2s-tests.md`: update suite counts to ASRC `67/67`,
  identity `65/65`, total `132/132`; explain two shared post-start slab
  backpressure regressions. Describe semaphore-controlled driver-release proof
  and nRF5340 no-wait boundary.
- `docs/testing/coverage-matrix.md`: update only `audio_i2s.c` row with 67/65
  counts and both post-start slab-backpressure outcomes.

### 4. Record immutable RH3-09 result

Update only current internal state documents:

- `docs/development/system-hil-resume-state.md`
- `docs/development/system-hil-rh3-software-status.md`

Record exact RH3-09 facts from this handoff: run ID, aggregate/child evidence
paths, image hashes, one passed fresh-mono child, failed fresh Mode A child,
`20/2/1/1/0` counts, empty cleanup failures, strict receiver-tail failure,
the one slab-full warning and counter values, healthy FLPR/source completion,
and no RH3 acceptance. Mark RH3-09 immutable and prohibit retry/reuse. Do not
rewrite historical RH3-01 through RH3-08 text. Do not claim this proposed
software repair has hardware proof.

## Scope and prohibitions

In scope:

- `src/audio_i2s.c`
- `Kconfig`
- `boards/nrf54l15dk_nrf54l15_cpuapp.conf`
- the three named I2S test files
- current behavior/test docs named above
- the two named internal RH3 state documents
- this handoff only for factual correction

Out of scope:

- `prj.conf`, startup or slab constants, new buffers, I2S driver source,
  descriptor queue size, source fixture, HIL runner/parser/rows/thresholds,
  Mode A assembly, Bluetooth, FLPR, timing/drift, pairing, release files,
  `STATUS.md`, coverage baseline;
- hardware, flashing, reset, serial, RF, pairing, `btattach`,
  `bap_central.py`, `serial-mcp`, HIL execution, or source-image rebuild;
- commit, push, merge, PR, tag, release, reset, stash, broad formatting, or
  cleanup of unrelated dirty work.

## Verification

Run from repository root inside NCS v3.3.0 development shell. Do not run HIL
or source-image build work.

```bash
env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/rh3-modea-slab-asrc \
  tests/unit/audio_i2s -p -t run

env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/rh3-modea-slab-identity \
  tests/unit/audio_i2s_identity -p -t run

python3 scripts/check-test-matrix.py --repo-root .

nix develop --command bash scripts/test-coverage.sh \
  --report-only \
  --output /tmp/opencode/rh3-modea-slab-coverage

fw-build-5340
fw-build-54l15
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
git diff --check
```

All functions in `src/audio_i2s.c` must execute in report-only coverage. Do not
write or enforce the committed coverage baseline on this intentionally dirty
tree. Treat actionable compiler, Kconfig, linker, and test diagnostics as
errors.

## Completion report

Return exact files changed; allocation policy and ownership semantics; direct
ASRC and identity test outcomes; current test counts; matrix/coverage/build and
build-contract results; exact RH3-09 state-document facts; diff/status; and
explicit no-hardware/no-commit confirmation. Stop and report rather than
increasing slab memory, weakening strict HIL validation, or widening scope.
