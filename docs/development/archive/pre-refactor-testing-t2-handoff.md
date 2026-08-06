# Phase T2 handoff — audio pipeline unit characterization

## Goal

Give LC3 decode/routing, volume, and statistics direct production-source proof.
Replace “does not crash” tests with deterministic 48 kHz golden output and
lock safe rejection behavior before larger refactoring.

Base: accepted T1 commit `3899c2f` on `test/pre-refactor-behavior`.

## Execution and git

- Executor: `deepseek/deepseek-v4-flash`, variant `max`.
- Work only on current branch.
- Include this handoff in T2 commits.
- Logical commits allowed; never amend T0/T1.
- No push, merge, PR, hardware, flash, or serial action.

## Scope

- `src/audio_decode.{c,h}`
- `src/audio_volume.{c,h}` only for defects/tests requiring narrow changes
- `src/audio_stats.{c,h}` only for defects/tests requiring narrow changes
- `tests/unit/decode/`
- new `tests/unit/volume/`
- new `tests/unit/stats/`
- new `tests/fixtures/lc3/`
- `scripts/test-all.sh` suite-count comments only
- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- new `docs/testing/t2-audio-pipeline-tests.md`
- `STATUS.md`

## Non-scope

- BAP/ASCS response handling and Mode A receive pairing (T4).
- I2S and sink state machine (T3).
- Alternate rates/codecs, multiple frame blocks per SDU, or >2 channels.
- LC3 encoder in production firmware.
- Audio algorithm/quality tuning.
- Numeric coverage enforcement (T7).
- Hardware claims from native_sim.

# T2A — checked-in LC3 fixtures

Create `tests/fixtures/lc3/` with README, reproducible generator, and binary
fixtures for exactly these supported shapes:

1. mono, 48 kHz, 7.5 ms, one 60-byte LC3 frame;
2. mono, 48 kHz, 10 ms, one 60-byte LC3 frame;
3. Mode B, 48 kHz, 7.5 ms, `[L 60 bytes][R 60 bytes]`;
4. Mode B, 48 kHz, 10 ms, `[L 60 bytes][R 60 bytes]`.

For each input `.lc3` file check in expected interleaved stereo signed-16
little-endian PCM:

- 720 samples / 1440 bytes for 7.5 ms;
- 960 samples / 1920 bytes for 10 ms.

Use deterministic, integer-generated source PCM with distinct left/right
patterns. Mono expected output duplicates each decoded sample. Mode B expected
output interleaves independently decoded left/right channels. Left and right
Mode B channel hashes must differ.

Generator requirements:

- source under `tests/fixtures/lc3/`, not production;
- use installed NCS v3.3.0 open-source liblc3 C API from
  `modules/lib/liblc3/include/lc3.h`;
- call `lc3_setup_encoder()`/`lc3_encode()` and independently
  `lc3_setup_decoder()`/`lc3_decode()`;
- compile with liblc3 module source and same relevant flags (`-O3`, C11,
  `-ffast-math`) through a documented command/script;
- write binary little-endian explicitly;
- never regenerate fixtures during normal tests;
- README records generator command, input formulas, frame sizes, SHA-256 for
  every checked-in binary, and fixed CRC-32 hashes used by tests;
- temporary generator executables/output must not remain.

Normal decode tests embed checked-in binaries using Zephyr
`generate_inc_file_for_target()` or another deterministic CMake binary embed.
Do not paste opaque frame bytes into test source as primary fixture storage.

# T2B — production decoder validation and routing

## Supported configuration

`audio_decode_config()` must return `-EINVAL` without calling liblc3 for:

- null context;
- channel count other than 1 or 2;
- frequency other than 48000 Hz;
- frame duration other than 7500 or 10000 microseconds;
- `frames_per_sdu` other than exactly 1.

This locks receiver-advertised support. T4 still owns translating these failures
into correct ASCS response codes in `bt_bap.c`.

On every failed configuration leave context safely reset. On success:

- mono creates only left decoder;
- Mode B creates two independent decoders;
- samples/channel is exactly 360 or 480;
- setup failure returns a negative errno and leaves no usable decoder state.

`audio_decode_reset(NULL)` must be harmless. Reset a real context to a fully
unconfigured state, including scalar shape fields and both decoder pointers.

## SDU validation

`audio_decode_sdu()` must reject with `-EINVAL` before touching output/decoder
state when:

- context or output is null;
- context is unconfigured/reset;
- Mode B lacks right decoder;
- stored channel/frame/sample shape is unsupported;
- `valid=true` and data pointer is null;
- valid frame length is zero;
- per-channel frame length is outside liblc3 basic 20..400 byte range;
- Mode B length is not exactly divisible by channel count;
- any division/frame-block shape would truncate input.

PLC (`valid=false`) must accept null data and use supplied valid configured
frame-byte shape. It must not dereference frame data.

When liblc3 returns a hard negative error:

- count decode error exactly once per failed decoder invocation;
- return a negative error from `audio_decode_sdu()` after completing any second
  Mode B decoder call needed to keep independent decoder state aligned;
- never report hard decode failure as success.

PLC (`lc3_decode == 1`) remains successful overall and counts PLC + total.

## Required production fixes

1. Current mono path decodes contiguous samples then calls
   `audio_decode_mono_to_stereo(stereo_out, stereo_out, samples)` with a forward
   loop. First write overwrites unread mono sample 1. Make exact in-place use
   overlap-safe, preferably by backward expansion when input and output are the
   same base. Keep normal separate-buffer behavior.
2. Current Mode B path updates success/PLC statistics only for left decoder and
   only hard errors for right. Apply identical result accounting to each LC3
   decoder invocation.
3. Current function always returns zero after liblc3 hard failures. Propagate a
   stable negative result while preserving stats.

No heap allocation.

## Decode tests

Compile real `src/audio_decode.c` and real `src/audio_stats.c` with
`CONFIG_LIBLC3=y`.

Golden tests for all four fixtures must assert:

- exact output byte equality to checked-in PCM;
- exact full-output CRC-32;
- exact left and right channel CRC-32;
- expected sample count and untouched guard values after output capacity;
- mono L==R for every sample;
- Mode B L/R differ and occupy correct even/odd positions;
- deterministic repeat after reset + reconfigure.

Use Zephyr `crc32_ieee()` (`<zephyr/sys/crc.h>`, `CONFIG_CRC=y`) rather than a
copied hash algorithm.

Additional tests:

- mono-to-stereo separate buffers, exact in-place buffer, zero samples;
- interleave exact routing and zero samples;
- every config rejection above;
- 7.5/10 ms scalar setup;
- decode nulls/reset/missing-right-decoder;
- malformed zero/too-short/too-long/odd Mode B lengths preserve output guard;
- malformed-length rejection followed by valid golden decode proves decoder
  state untouched;
- PLC mono and Mode B output shape plus exact stats coupling;
- hard malformed LC3 data of valid length returns negative and increments
  decode errors (do not assert arbitrary corrupted PCM);
- mono valid success stats;
- Mode B valid success counts both channel decoder invocations;
- Mode B right-channel hard failure accounting and returned error;
- `audio_decode_reset()` behavior and reconfiguration.

Avoid assertions about floating-point encoder output beyond checked-in fixture
provenance. Decoder golden PCM is exact for pinned NCS/toolchain.

# T2C — volume production suite

Create `tests/unit/volume/` with `testcase.yaml`, native_sim, and real
`src/audio_volume.c`.

Use a test-local shadow
`zephyr/bluetooth/audio/vcp.h` containing only exact NCS v3.3.0 renderer types
used by production, plus a fake `bt_vcp_vol_rend_register()` that:

- records initial volume=195, mute=unmuted, step=16;
- captures callback pointer (never retains pointer to stack registration param);
- returns controllable errors;
- allows test to invoke real production state callback.

Enable production VCP branch for this suite with test-only compile definitions
for `CONFIG_BT_VCP_VOL_REND=1`, `CONFIG_BT_AUDIO_VOL_DEFAULT=195`, and zero
VOCS/AICS counts as needed. Do not add invalid Kconfig assignments.

Required tests:

- init success, exact registration fields, default state;
- registration failure propagated while default state remains deterministic;
- callback success updates packed volume/mute state;
- callback error leaves prior state unchanged;
- volume 0 zeroes signed extreme/sample patterns;
- mute zeroes regardless of volume;
- volume 255 is bit-exact unity for `INT16_MIN`, `INT16_MAX`, -1, 0, 1;
- intermediate volume matches signed 32-bit `sample * volume / 255`, including
  C truncation toward zero and signed extremes;
- zero samples causes no memory change;
- null + zero samples is harmless; if null/nonzero is made safe, test/document
  it, but do not require unsupported writes;
- performance hook remains balanced on mute, zero, unity, and scale exits
  (compile real `audio_perf.c` or use minimal observation seam only if needed);
- concurrent callback toggling between two packed states while
  `audio_volume_apply()` runs proves each complete buffer reflects one atomic
  snapshot, never mixed scaling within a buffer.

If production needs a guarded test reset, prefer calling `audio_volume_init()`
before each test because that is real lifecycle behavior. No public test setter.

# T2D — statistics production suite

Create `tests/unit/stats/` with `testcase.yaml`, native_sim, and real
`src/audio_stats.c`.

Required tests:

- initial/reset zero snapshot;
- frame decoded increments total only;
- PLC increments PLC and total exactly once;
- decode error does not increment total;
- I2S underrun and stream reset each increment only own counter;
- mixed sequence exact snapshot;
- reset after nonzero clears every counter;
- returned snapshot is by value and does not mutate future state;
- at least four concurrent threads increment each API a fixed large count;
- final atomic counts exact, including `total = decoded + PLC`;
- repeated reset/get behavior deterministic.

No test-only production seam should be needed.

# Documentation

Create `docs/testing/t2-audio-pipeline-tests.md` recording:

- exact production sources linked;
- fixture names, sizes, SHA-256, CRC-32, generation command;
- test counts;
- defects fixed;
- unsupported shapes still rejected at decode layer but ASCS mapping remains T4;
- no audio-quality/hardware claim from native_sim.

Update behavior contract with safe decode-layer rejection and corrected stats
semantics. Preserve T4 known gap for ASCS response validation.

Update coverage matrix rows for decode, volume, stats to direct production
proof. Update canonical gate comments for added Twister suites. Mark T2 ACCEPTED
in STATUS only after final validation; set T3 next.

# Verification

Focused:

```bash
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t2_decode tests/unit/decode -p -t run
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t2_volume tests/unit/volume -p -t run
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t2_stats tests/unit/stats -p -t run
```

Then:

```bash
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check
```

Validate exact final commit on workstation using clean bundle + detached
worktree. Full gate must pass at least twice with unchanged BSim hashes. Build
all three targets on exact commit. Remove all temporary refs/worktrees/bundles.

# Acceptance

- Four checked-in reproducible LC3 fixture sets with exact PCM proof.
- Golden decode/routing tests pass against real production decoder.
- Mono overlap corruption fixed and locked.
- Both Mode B decoder outcomes counted.
- Unsafe inputs rejected before output/state mutation.
- Volume and stats suites execute real production source.
- Full gate and three builds pass without actionable warnings.
- Production APIs/wire/audio formats unchanged except documented safe rejection
  and defect corrections.
- Worktrees clean; no temporary validation artifacts.

# Commit messages

Suggested:

```text
tests: add deterministic LC3 decode fixtures
fix: validate and characterize audio decoding
tests: cover production volume and statistics
docs: record T2 audio pipeline evidence
```

Return files, fixture hashes, tests/counts, gates, builds, commits, defects,
deviations, cleanup, and blockers.
