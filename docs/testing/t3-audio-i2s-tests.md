# T3 — I2S sink state-machine tests

Phase T3 of the pre-refactor testing track: compile and execute the **real
`src/audio_i2s.c`** under native_sim with a controllable fake I2S driver and
mocked platform dependencies, locking slab ownership, startup, steady-state
drift, ASRC offload/fallback, underrun recovery, and stop behavior for both
production resampler/actuator shapes.

Base: accepted T2 commit `0408b6d` on `test/pre-refactor-behavior`.

## Production source and the two canonical variants

Both suites compile and execute the real current production source
`src/audio_i2s.c` — including the T3 hardening (transactional startup,
output/offload validation, idempotent initialization) — against the fake
driver and mocks, plus the unchanged `src/audio_sink.h` interface.  No
production algorithm is copied into test code.

| Suite | Resampler | Actuator | Offload | Output rate | Purpose |
|-------|-----------|----------|---------|-------------|---------|
| `tests/unit/audio_i2s/` | `AUDIO_RESAMPLER_ASRC_LINEAR` | `AUDIO_CLOCK_ACTUATOR_NONE` | `AUDIO_OFFLOAD_ASRC` | 47619 Hz | nRF54L15 production shape (ASRC consumes ppm; FLPR offload) |
| `tests/unit/audio_i2s_identity/` | `AUDIO_RESAMPLER_IDENTITY` | `AUDIO_CLOCK_ACTUATOR_APLL` | — | 48000 Hz | nRF5340 production shape (APLL steers clock; passthrough data path) |

Variant selection uses **test-only CMake compile definitions**
(`CONFIG_AUDIO_RESAMPLER_*`, `CONFIG_AUDIO_CLOCK_ACTUATOR_*`,
`CONFIG_AUDIO_OFFLOAD_ASRC`, `CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ`) —
those symbols do not exist in the test apps' Kconfig, so they must not be
assigned in `prj.conf`.  Zephyr I2S syscall API (`CONFIG_I2S=y`) and ztest
(`CONFIG_ZTEST=y`) are enabled through valid `prj.conf` symbols; the
performance-instrumentation symbols are defined locally in each suite's
`Kconfig` (same pattern as `tests/unit/volume`).

The fake I2S device is exposed as alias `i2s-audio` via a shared overlay, so
production `DT_ALIAS(i2s_audio)` and `DEVICE_DT_GET()` stay real.

## Fake I2S driver and ownership model

`tests/unit/audio_i2s_common/fake_i2s.c` implements `struct i2s_driver_api`
from NCS v3.3.0 `include/zephyr/drivers/i2s.h` and is instantiated with
`DEVICE_DT_INST_DEFINE` (binding `vnd,audio-i2s-fake`).

- `configure` captures a deep copy of the exact `struct i2s_config`
  (direction, word size, channels, format, options, frame clock, `mem_slab`,
  `block_size`, timeout) and returns a controllable result.
- `write` records ordered pointer, size, and a 16-byte byte snapshot, with
  failure injection by call index and configurable errno.  A successful write
  **transfers block ownership** to the fake and queues the block; a failed
  write **never takes ownership**.
- `trigger` records ordered commands with per-command configurable results.
  A successful `PREPARE`/`DROP` **purges queued blocks through the captured
  config `mem_slab`** (nrfx ownership behavior); `START` never touches the
  queue; a failed trigger performs no state change.
- Tests release selected/all queued blocks (`fake_i2s_release*`) to emulate
  DMA completion; write records persist after release so tests can prove
  pointer history.
- The fake detects a pointer submitted **while still queued** (the
  double-write DMA-corruption class) and records it as a violation; tests
  assert zero violations after every complex sequence.
- The captured slab is retained across `fake_i2s_reset()` because a real
  driver keeps its configured `mem_slab` across stream stop/restart without
  re-configuration (`audio_sink_stop` retains `configured = true`).

### Difference from physical DMA proof

On hardware the nrfx driver frees a TX block back to the slab when its DMA
transfer completes, driven by interrupts.  The fake has no DMA engine: block
release is **explicit test control**.  Physical DMA timing, interrupt
latency, and I2S electrical behavior therefore remain hardware-only evidence
(T8); everything the firmware can prove about ownership, ordering, and error
cleanup is proven here.

## Dependency mocks

`tests/unit/audio_i2s_common/mock_audio.c` implements the exact public
signatures of: audio timing init/reset; drift update/reset; clock actuator
init/apply/reset; rate converter init/next-frames (with per-call return
sequence); ASRC init/process/reset/state-export/state-import; offload ASRC
process; stats underrun/stream-reset; perf cycle start/end/queue/push/repeat/
capacity.  Mocks capture full arguments and write deterministic PCM patterns
(ASRC/offload outputs) so tests prove exactly which output was queued.
`audio_asrc_state_export` reads the live production context, making the
committed post-import context observable without algorithm duplication.

## Narrow production test hooks

Under `AUDIO_I2S_NATIVE_TEST` only (test CMake compile definition; never
present in production firmware), `src/audio_i2s.c` includes the test-owned
header `audio_i2s_test_hook.h` and provides:

| Hook | Purpose |
|------|---------|
| `audio_i2s_test_device_is_ready()` / `set_device_ready()` | controllable `device_is_ready()` result |
| `audio_i2s_test_inject_slab_alloc_failure()` / `set_slab_alloc_failure()` | repeat-fallback allocation failure injection |
| `audio_i2s_test_reset_module_state()` | reset of module-static state between tests (after fake-owned blocks are purged) |
| `audio_i2s_test_is_configured/started()`, `input_frames()`, `saved_frame_len()` | read-only snapshots |
| `audio_i2s_test_offload_sequence()`, `asrc_prev_l/r()`, `asrc_prev_valid()` | ASRC/offload state snapshots (ASRC variant only) |
| `audio_i2s_test_get_slab()` | internal slab accessor (pre-exhaustion, exact free count) |

No production runtime overhead and no test symbols exist outside the guarded
build; production images are hook-free.

## Behavior fixed by T3

Three production defects found and fixed (all covered by tests):

1. **Non-transactional startup.**  The first-push pre-fill silently skipped
   silence allocation/write failures, leaked driver-owned blocks when START
   failed, and could issue START after an incomplete pre-fill.  Startup now
   returns the exact primary failure, frees caller-owned blocks, DROP-purges
   queued driver-owned blocks, never frees driver-owned blocks directly, and
   never STARTs after incomplete pre-fill.
2. **Unbounded input frame setter.**  `audio_sink_set_input_frames()` accepted
   any nonzero `uint16_t`, letting the identity path copy `frames × 4` bytes
   into the fixed 481-frame (1924-byte) slab block.  Only 360/480 are
   accepted now; any other value resets to 480.
3. **Untrusted offload output accepted.**  A nominal offload success with
   `output_frames == 0` (valid FLPR error response) or >481 committed the
   post-state and queued the bogus frame count.  Offload output is usable
   only in [1, 481]; zero/oversized results and import rejections fall back
   to CPU ASRC from the unchanged pre-state, and the CPU run overwrites any
   untrusted offload output.

Additional hardening locked by tests: first-attempt init failure leaves
`configured` false and permits a later retry that performs the full normal
init exactly once; repeated `audio_sink_init()` on an already-configured
(possibly streaming) sink is an idempotent no-op that preserves started
state, the exact queued driver-owned blocks, slab free count, input frame
selection, and ASRC/offload state without any trigger; saved-frame/sequence
state does not leak across stop; CPU-ASRC nominal success with produced 0
or >481 is rejected with `-ENOSPC` and slab release; a rate-converter
silence count outside [1, 481] fails with `-ENOSPC` without writing beyond
slab capacity; repeat fallback never issues a zero-length write; output
bytes are computed only for `1 <= output_frames <= 481`.

### T3 review-fix round (2026-08-01, commit `fix: preserve active I2S state
across reinit`)

Removes the re-initialization regression introduced by T3's init state
clearing: an accidental repeated `audio_sink_init()` on a configured —
possibly streaming — sink previously re-ran the init sequence and cleared
stream state.  `audio_sink_init()` is now idempotent: when `configured`,
it returns 0 immediately without touching device-ready, configure,
dependency init, started, saved frame, input frame selection, ASRC/offload
state, slab ownership, or the I2S queue, and without issuing any trigger.
First-attempt failures still leave `configured` false and are retryable;
the retry performs the full normal init exactly once.  Re-init never
resets input frame selection (360 preserved).  Tests added in both
variants (replacing the old re-init-clears-state test): success-noop,
active-stream queue/pointer/free preservation, retry after configure /
actuator / timing (and ASRC) failure, input-frame-selection preservation.

## Test counts

| Suite | Tests | Result |
|-------|-------|--------|
| `tests/unit/audio_i2s/` (ASRC/offload) | 61 | 61/61 PASS |
| `tests/unit/audio_i2s_identity/` (identity/APLL) | 59 | 59/59 PASS |
| **Total** | **120** | **120/120 PASS** |

The counts and the coverage list below describe the **current** suites:
the original T3 50/48 cases plus the later R1 admission/drain concurrency
additions (+10 per suite across the shared common tests) and the
2026-08-09 startup-reservoir follow-up (11-block startup, +1 per suite),
so 50 + 10 + 1 = 61 and 48 + 10 + 1 = 59.  The original-T3 run evidence
(50/50 and 48/48 focused runs, 25-child gate) is preserved verbatim in
the next section.

Coverage of the required behaviors:

- common/init/input: device-not-ready (no dependency calls), exact I2S
  config (TX, 16-bit, 2ch, I2S format, bit/frame master, 48000 Hz, internal
  slab, block size 1924, timeout 0), configure/ASRC-init/actuator-init/
  timing-init error propagation with no later calls, configured-only-after-
  success, idempotent re-init (success no-op; active-stream queue/pointer/
  free-count preservation; retry after configure/actuator/timing/ASRC
  first-attempt failure; 360 input-frame selection preserved), setter
  360/480 vs 0/1/359/361/479/481/65535, all push rejection classes,
  malformed push zero side effects, push-before-init `-EIO` with no slab
  allocation;
- startup/ownership: eleven-block ordering for 480 and 360 input
  (ten zero-filled silence blocks, then the data block, then START),
  rate-converter-selected silence sizes, distinct pointers, START only
  after the eleventh write, silence-alloc failure at each of the ten
  positions, silence-write failure at each index, data-write failure,
  START failure purging all eleven, DROP-failure observability without
  double free, rate-converter 482/0/partial-then-482 bounds, and the
  ten-block-gap reservoir regression (ten ordered DMA completions leave
  one driver-owned block, slab free 15, zero duplicate writes);
- identity/APLL steady state: no drift before START, one drift update per
  started block with pre-allocation free count, zero ppm never applied,
  ±ppm applied exactly once, exact data bytes/size, non-`-EIO` write error
  ownership, `-EIO` PREPARE recovery + stream reset + fresh pre-fill,
  repeat fallback at threshold with separate slab and exact copy, no repeat
  below threshold, repeat alloc/write failure ownership with single count,
  perf timing balance on every started push (startup not measured), queue
  metrics, slab-full underrun;
- ASRC/offload: pre-state export before every offload attempt, offload
  success at 1/480/481 frames with no CPU process and exact output, import
  commit observability, four error classes falling back from unchanged
  pre-state, zero/oversized output fallback with untrusted-output overwrite,
  import rejection fallback, CPU prev/state update + exact output, capacity
  `-ENOSPC` + slab release + count + no commit, CPU error `-EIO`, invalid
  CPU produced counts, 360-frame fallback, sequence exactly once per
  rendered block, separate-slab repeat, stop reset of ASRC state;
- stop/reconnect: resets (drift/actuator/rate-converter/timing/ASRC) even
  when already stopped, PREPARE-then-DROP order, no extra triggers on
  repeated stop, started=false/configured=true, fresh pre-fill after stop
  without re-init, saved-frame/sequence cleared, stop trigger errors keep
  configuration with no double free.

## Original T3 verification commands and results

Focused suites (desktop `thomas-main`, NCS v3.3.0 dev shell):

```bash
west build --no-sysbuild -b native_sim/native/64 -d /tmp/t3_i2s_asrc \
  tests/unit/audio_i2s -p -t run        # 50/50 PASS, zero warnings
west build --no-sysbuild -b native_sim/native/64 -d /tmp/t3_i2s_identity \
  tests/unit/audio_i2s_identity -p -t run   # 48/48 PASS, zero warnings
```

Production firmware builds on the T3 commit (no compiler warnings; only the
documented pre-existing Kconfig/CMake/DT diagnostics — see STATUS.md):
`fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` — all three pass on
desktop and workstation (review-fix commit re-verified on both).

Full gate: desktop (`thomas-main`) 24 PASS / 1 FAIL / 25 TOTAL — the single
failure is the `bsim: stage1` child, which cannot run locally because the
BabbleSim binaries are not built on `thomas-main` (same as T1/T2; the
workstation provides the authoritative BSim leg).  Workstation
(`thomas-workstation`), detached worktree of the exact final T3 commit via
non-destructive git bundle: **25 PASS / 0 FAIL / 25 TOTAL, run twice
consecutively**, both clean — re-verified on the review-fix commit; BSim
hashes deterministic in every run — 10 ms `0x9225F075`, 7.5 ms `0x2011C0F9`
(corrected T2 values, unchanged by T3 and the review fix).

## Remaining hardware-only I2S evidence

- Physical nrfx DMA timing, interrupt-driven block release, and underrun
  timing on real nRF5340/nRF54L15 hardware (T8 hardware baseline freeze).
- I2S electrical behavior (LRCK/BCK rates, DAC output) — existing hardware
  acceptance (Phase 4/5 evidence) and T8.
- Real FLPR offload round-trip timing over IPC (covered functionally by
  `tests/unit/offload_asrc/` + T1 suites; cross-core timing is hardware-only).

## T4 is next

BAP receive handling (stream receive → decode → sink push integration).
