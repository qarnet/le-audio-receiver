# Phase 4a.2 — nRF54L15 Fixed-Rate I2S Conversion

Status: required measured integration fix after `67a29fd`

## Root cause

Main receiver supplies 480 stereo frames every 10 ms (48 kHz LC3). With
`clock-source = "PCLK32M"`, I2S20 register evidence shows approximately
47,619 Hz LRCK, so a fixed 480-frame output block takes 10.08 ms. Input arrives
at 100 blocks/s while output drains about 99.206 blocks/s. Queue fills around
one block every 1.26 s; observed slab-full every ~1.57 s agrees in magnitude.

Standalone test proves DMA hardware and driver work when producer rate matches
drain rate. Current PI output is clamped to ±500 ppm, while nominal mismatch is
about 7,936 ppm. Existing SAMPLE_ADJUST edits samples inside a fixed 1920-byte
write, so it does not change queue rate.

## Goal

Add bounded nearest-neighbor, variable-output-frame conversion for nRF54L15
I2S sink. Convert each nominal 480-input-frame block to a 476/477-output-frame
sequence averaging 47,619 output frames per 100 input blocks. Preserve fixed
480-frame behavior on nRF5340.

This is required hardware-rate matching, not Phase 5 quality ASRC. Phase 4b
GRTC remains required for peer-drift correction after this baseline mismatch is
removed.

## Scope

Add/change:

- `src/audio_rate_convert.h` (new pure helper API)
- `src/audio_rate_convert.c` (new pure nearest-neighbor helper)
- `src/audio_i2s.c`
- `Kconfig`
- `CMakeLists.txt`
- `boards/nrf54l15dk_nrf54l15_cpuapp.conf`
- `tests/unit/rate_convert/{CMakeLists.txt,prj.conf,testcase.yaml,src/test_rate_convert.c}`
- `docs/development/phase4a2-rate-conversion-results.md` (new)
- `docs/design.md`, `STATUS.md` only after measured retest result
- this handoff and previously untracked
  `docs/development/phase4a1-new-dac-main-pipeline-handoff.md`

Do not touch existing unstaged receiver overlay or `src/bt_bap.c` diagnostics.

## Configuration/API

Add project Kconfig integer:

```kconfig
config AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ
    int "Actual I2S output sample rate"
    default 48000
    range 1 192000
```

Set `CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ=47619` only in
`boards/nrf54l15dk_nrf54l15_cpuapp.conf`. nRF5340 stays default 48000.

Pure helper shape:

```c
struct audio_rate_converter {
    uint32_t remainder;
    uint32_t input_rate_hz;
    uint32_t output_rate_hz;
};

void audio_rate_converter_init(struct audio_rate_converter *ctx,
                               uint32_t input_rate_hz,
                               uint32_t output_rate_hz);
size_t audio_rate_converter_next_frames(struct audio_rate_converter *ctx,
                                        size_t input_frames);
void audio_rate_converter_nearest_stereo(const int16_t *input,
                                         size_t input_frames,
                                         int16_t *output,
                                         size_t output_frames);
```

`next_frames` must use a remainder accumulator, not floats. For 100 calls of
480 input frames at 48,000→47,619, total output must equal 47,619. Individual
results must be 476 or 477.

## `audio_i2s.c` requirements

1. Treat current decoded data as 48 kHz input. Keep I2S config
   `frame_clk_freq = 48000`; hardware divider still determines actual drain.
2. Replace current fixed-block sample-adjust `memmove` path with converter
   output. It must change `i2s_write()` byte count, not only PCM contents.
3. Reserve maximum output capacity for a possible one-frame insert: 481 stereo
   frames = 1924 bytes. Ensure I2S block size / slab alignment remains valid.
4. For sample-adjust actuator output:
   - `+1` (drop) decrements desired output frames by one;
   - `-1` (insert) increments desired output frames by one;
   - clamp to `[1, 481]`.
   Apply nearest-neighbor conversion once; do not also run old memmove path.
5. For 48k→48k default, converter output stays exactly input frames; nRF5340
   output remains fixed 480 frames / 1920-byte writes.
6. Silence prefill and packet-repeat fallback must use valid variable write
   lengths. Store `saved_frame_len` alongside saved PCM, and never write a
   stale full maximum block when saved output is shorter.
7. Preserve all existing ownership/error rules: unique slab buffers,
   no free after successful `i2s_write`, PREPARE-before-DROP recovery.
8. Add bounded diagnostic counters/log only if needed for verification: total
   input frames, total output frames, and selected output frame count. No
   per-block info logs.

## Unit tests

Native-sim test must cover:

1. Identity: 100 × 480 at 48k→48k yields 48,000 output frames, each 480.
2. nRF54 baseline: 100 × 480 at 48k→47,619 yields exactly 47,619 total,
   every block 476 or 477.
3. Nearest stereo preserves L/R pairing and endpoints for known short input.
4. Remainder state reset/initialization is deterministic.

## Hardware retest

1. Build both firmware targets and native unit test.
2. Flash nRF54 receiver with new DAC. Run same 30-second Mode A stream.
3. Capture serial before flash; query stats *during stream* before disconnect
   so counters are not reset by `disconnected()`.
4. Require no steady-state slab-full/EIO. A stop-time underrun must be
   investigated, not accepted by default.
5. Capture analyzer if hardware is present; otherwise record availability.
6. Record user listening as pending unless user explicitly reports it.

## Documentation

Correct `STATUS.md` / `docs/design.md` root cause from “PI insufficient” to
fixed PCLK32M hardware-rate mismatch plus fixed-size writes. State Phase 4a.2
result exactly; do not mark Phase 4 complete. Update phase result doc with
math, unit results, stream evidence, and residual GRTC requirement.

## Verification

```bash
nix flake check --no-build
west build -b native_sim tests/unit/rate_convert --pristine -d build/test-rate-convert
./build/test-rate-convert/zephyr/zephyr.exe
fw-build-54l15
fw-build-5340
```

## Constraints

- No Phase 4b GRTC/DPPI or Phase 5 quality work.
- No external dependencies.
- Preserve pre-existing unstaged diagnostics exactly and do not stage them.
- User has authorized receiver flashing and normal stream tests. Ask user only
  for physical listening result after technical gates pass, or a true unavailable
  hardware/permission blocker.
- Commit scoped changes only. No amend, push, merge, or PR.

## Executor recap

Return code/files, exact unit/build/stream evidence, counters queried during
stream, analyzer availability/measurements, result status, commit hash/message,
and only remaining hard blocker/user listening question.
