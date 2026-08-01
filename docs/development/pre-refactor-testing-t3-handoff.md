# Phase T3 handoff — I2S sink state-machine tests

## Goal

Compile and execute real `src/audio_i2s.c` under native_sim with a controllable
I2S driver and mocked platform dependencies. Lock slab ownership, startup,
steady-state drift, ASRC offload/fallback, underrun recovery, and stop behavior
for both production resampler/actuator shapes.

Base: accepted T2 commit `0408b6d` on `test/pre-refactor-behavior`.

## Git/execution

- Executor: `deepseek/deepseek-v4-flash`, variant `max`.
- Include this handoff in T3 commits.
- No amend, push, merge, PR, hardware, flash, or serial action.
- T3 remains open until both variants, full gate, and all builds pass.

## Scope

- `src/audio_i2s.c`
- `src/audio_sink.h`
- new `tests/unit/audio_i2s/` (ASRC/NONE/offload variant)
- new `tests/unit/audio_i2s_identity/` (identity/APLL variant)
- optional shared test support under `tests/unit/audio_i2s_common/`
- `scripts/test-all.sh` count comments
- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- new `docs/testing/t3-audio-i2s-tests.md`
- `STATUS.md`

## Non-scope

- Real nrfx DMA timing or physical I2S electrical behavior.
- Production ASRC math (already directly tested).
- Production offload state machine (already directly tested).
- BAP receive handling (T4).
- Timing HAL internals (T5).
- Broad sink API redesign.

# Test architecture

## Two canonical variants

The canonical gate builds every `tests/unit/*/testcase.yaml` directory once, so
create two suites rather than relying on Twister-only parameterization:

1. `tests/unit/audio_i2s/`
   - define `CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR=1`;
   - define `CONFIG_AUDIO_CLOCK_ACTUATOR_NONE=1`;
   - define `CONFIG_AUDIO_OFFLOAD_ASRC=1`;
   - define `CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ=47619`;
   - define `AUDIO_I2S_NATIVE_TEST=1` and ASRC test marker.
2. `tests/unit/audio_i2s_identity/`
   - define `CONFIG_AUDIO_RESAMPLER_IDENTITY=1`;
   - define `CONFIG_AUDIO_CLOCK_ACTUATOR_APLL=1`;
   - define `CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ=48000`;
   - define `AUDIO_I2S_NATIVE_TEST=1` and identity marker.

Use test-only CMake compile definitions without invalid Kconfig assignments.
Enable Zephyr I2S syscall API and ztest through valid `prj.conf` symbols.

Both suites compile real `src/audio_i2s.c`. Shared support may be compiled into
both; do not copy production state-machine logic.

## Fake I2S driver

Create a test-local binding, overlay, and `DEVICE_DT_INST_DEFINE` fake driver.
Overlay must expose alias `i2s-audio` so production `DT_ALIAS(i2s_audio)` and
`DEVICE_DT_GET()` remain real.

Implement `struct i2s_driver_api` directly, grounded in NCS v3.3.0
`include/zephyr/drivers/i2s.h`:

- configure captures exact `struct i2s_config` and returns controllable result;
- write records ordered pointer, size, and a byte snapshot, with failure
  injection by call index;
- a successful write transfers block ownership to fake driver and queues it;
- failed write never takes ownership;
- trigger records ordered command and returns configurable result per command;
- successful DROP/PREPARE purges queued blocks through captured config slab,
  matching nrfx I2S ownership behavior;
- tests can release selected/all queued blocks to emulate DMA completion;
- device readiness is controllable through a narrow production test hook;
- reset purges fake-owned blocks before clearing records.

Track pointer history even after release so tests prove no pointer is submitted
twice within one ownership lifetime. Snapshot enough bytes to prove silence,
audio, repeat, and ASRC output content.

## Dependency mocks

Implement exact public signatures and controllable results/call logs for:

- audio timing init/reset;
- drift update/reset;
- clock actuator init/apply/reset;
- rate converter init/next-frames;
- ASRC init/process/reset/state export/state import;
- offload ASRC process;
- audio stats underrun/stream-reset;
- audio performance start/end/queue/push/repeat/capacity hooks.

Mocks must capture full arguments and allow deterministic output/state. They do
not reproduce algorithms. Use distinct sentinel states and PCM patterns to
prove transactional behavior.

## Narrow production hooks

Under `AUDIO_I2S_NATIVE_TEST` only, include a test-owned hook header and provide:

- controllable `device_is_ready()` result;
- reset of module-static state between tests after fake-owned blocks are purged;
- read-only snapshot of configured, started, input frames, saved-frame length,
  offload sequence, ASRC previous samples/valid state where compiled;
- pointer/accessor for internal slab so tests can pre-exhaust it and inspect
  exact free count.

No production runtime overhead or test symbols outside guarded build.

# Required production hardening

## Input frame bound

`audio_sink_set_input_frames()` currently accepts any nonzero `uint16_t`; the
identity path then copies `frames * 4` bytes into a fixed 481-frame slab block.
Restrict accepted values to supported 360 or 480. Zero or any other value resets
to 480. Document behavior. No buffer write may exceed `BLOCK_SIZE`.

## Initialization state

At start of `audio_sink_init()`, ensure stale `configured/started/saved` state
cannot survive a failed reinitialization in tests. Keep production call-once
semantics and return each exact dependency error. Do not claim hardware cleanup
beyond calls actually made.

## Transactional startup ownership

First push must queue exactly six distinct silence blocks, then one distinct
data block, then START. Current code silently skips silence allocation/write
failures and leaks driver-owned blocks on START failure.

Required behavior for any startup allocation/write/START failure:

- return exact primary failure (`-ENOMEM`/driver errno);
- free any block whose write failed or was never submitted;
- issue DROP to purge previously successful queued writes;
- leave `started=false`, `configured=true`;
- leave slab fully reclaimable after fake DROP;
- never free a driver-owned block directly;
- no START after incomplete prefill.

If rate-converter silence frame count exceeds `MAX_OUTPUT_FRAMES`, fail safely
with `-ENOSPC`, release data block, purge already queued silence, and do not
write beyond slab capacity.

## Output bounds and offload fallbacks

Before calculating/writing output bytes require `1 <= output_frames <= 481`.
Invalid CPU-ASRC output after nominal success returns `-ENOSPC` and frees slab.

An offload call returning zero with output_frames 0 (valid FLPR error response)
or >481 must not be treated as usable output. Fall back to CPU ASRC from the
unchanged exported pre-state. Import post-state only for output in 1..481.

All offload errors/import failures/invalid frames fall back from unchanged
pre-state. Only fully validated import commits ASRC continuity state.

# Required tests — common/init/input

Run where applicable in both variants:

- fake device not ready → `-ENODEV`, no configure/dependency calls;
- exact I2S config: TX, 16-bit, 2 channels, I2S format, bit/frame master,
  nominal 48000 Hz, internal slab, block size 1924, timeout 0;
- configure error propagated; later dependencies not called;
- ASRC init error in ASRC variant propagated; later calls not made;
- actuator init error propagated; timing not called;
- timing init error propagated;
- full init success sets configured only after every stage succeeds;
- re-init failure cannot leave stale configured/started state;
- setter 360/480 accepted; 0/1/359/361/479/481/65535 map safely to 480;
- push null, zero, odd, too short, too long, wrong configured frame count;
- malformed push rejected before drift, slab, I2S, stats, or perf side effects;
- valid push before init returns `-EIO` without slab allocation.

# Required tests — startup/ownership

- first 480-frame push writes exactly seven blocks then START;
- first 360-frame push same ordering with correct input/data sizes;
- first six writes are zero-filled silence with rate-converter-selected sizes;
- seventh write is exact data (identity) or mock ASRC/offload output;
- all seven pointers distinct; no write before allocation; no pointer reuse;
- START occurs only after seventh successful write;
- each of six silence allocation positions can fail via controlled slab
  pre-exhaustion; cleanup exact;
- each silence write index can fail; failed block caller-freed, earlier driver
  blocks DROP-purged, data caller-freed, no START;
- data write failure ownership/cleanup;
- START failure purges all seven driver-owned blocks;
- DROP cleanup failure is recorded/observable but never causes double free;
- startup rate-converter output 482 fails without overflow.

# Required tests — identity/APLL steady state

- no drift update before START;
- exactly one drift update per later accepted block before slab allocation;
- returned ppm zero causes no actuator apply;
- positive/negative nonzero ppm passed exactly once to actuator;
- main audio block exact bytes/size;
- normal write error frees caller block and keeps started for non-`-EIO`;
- `-EIO` write frees caller block, calls PREPARE, increments stream reset, and
  sets started false;
- next valid push after `-EIO` performs fresh six-silence prefill + data + START;
- repeat fallback occurs only at free-count threshold, uses separate slab block,
  copies latest saved frame exactly, and counts once;
- repeat allocation failure and repeat write failure preserve ownership and
  count attempted fallback exactly once;
- performance push timing starts/ends exactly once for every started accepted
  push, including all failure exits; startup push is not measured per current
  contract;
- queue metrics receive pre-allocation slab count and output frame count.

# Required tests — ASRC/offload path

- export exact current CPU pre-state before every offload attempt;
- offload success 1, 480, and 481 frames: no CPU process; exact output queued;
- successful valid post-state import commits context/prev state transactionally;
- offload error classes (`-EAGAIN`, `-EINVAL`, `-ETIMEDOUT`, `-EIO`) each run CPU
  fallback from unchanged pre-state;
- offload zero-frame and oversized-frame nominal success run CPU fallback;
- import rejection runs CPU fallback from unchanged pre-state and overwrites any
  untrusted offload output;
- CPU fallback success updates prev/state and queues exact CPU output;
- CPU capacity return 1 frees slab, returns `-ENOSPC`, counts capacity failure,
  and does not commit prev/sequence;
- CPU negative error frees slab, returns `-EIO`, no commit;
- CPU nominal success with produced 0 or >481 is rejected/freed;
- 360-frame input with offload rejection falls back correctly;
- offload sequence increments exactly once per successfully rendered block,
  including CPU fallback, never on failed block;
- repeat fallback uses separate slab in ASRC variant too;
- stop resets ASRC context, prev-valid, sequence, rate converter, drift,
  actuator, and timing.

# Required tests — stop/reconnect

- stop always resets drift/actuator/rate-converter/timing and ASRC state where
  compiled, even if already stopped;
- when started, trigger order is PREPARE then DROP;
- repeated stop emits no extra triggers after first stop;
- started becomes false but configured remains true;
- push after stop works without re-init and starts fresh prefill;
- saved-frame/sequence state does not leak across stop;
- trigger errors during stop do not flip configured false or cause double free.

# Documentation/evidence

Create `docs/testing/t3-audio-i2s-tests.md` with:

- exact production source and two variants;
- fake-driver ownership model and difference from physical DMA proof;
- test-hook inventory and production exclusion;
- test counts and behavior/defects fixed;
- exact focused/gate/build results;
- remaining hardware-only I2S evidence.

Update I2S behavior contract for supported input frame setter, transactional
startup cleanup, offload frame validation, and exact stop/recovery semantics.
Update coverage matrix `audio_i2s.c` row to direct production proof while
retaining physical nrfx/DMA as hardware-only. Update gate suite-count comments.
Mark T3 ACCEPTED only after final validation; T4 next.

# Verification

```bash
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t3_i2s_asrc tests/unit/audio_i2s -p -t run
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t3_i2s_identity tests/unit/audio_i2s_identity -p -t run
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check
```

Validate exact final commit on workstation via bundle + detached worktree. Run
full gate twice consecutively with corrected T2 BSim hashes, plus all three
builds. Any warning/failure must be diagnosed. Remove temp refs/worktrees/
bundles/logs/build dirs.

# Commits

Suggested:

```text
tests: add fake I2S sink harness
fix: make I2S startup ownership transactional
tests: cover ASRC and identity sink states
docs: record T3 I2S state-machine evidence
```

Return changed files, test counts, ownership proof, defects, gates, builds,
commits, deviations, cleanup, and blockers.
