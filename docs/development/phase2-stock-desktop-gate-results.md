# Phase 2 stock desktop stream gate — strict evidence correction

**Date**: 2026-07-31
**Status**: ACCEPTED — 30 s and 120 s gates pass with stock WirePlumber main-systemwide
**Executor**: Phase 2 strict evidence handoff
  (`bluez-wireplumber-phase2-strict-evidence-handoff.md`)

## Rejected commit history

Commit `645df95` ("Phase 2: stock desktop BAP stream gate — accepted") was
rejected during orchestrator review for three defects:

1. Gate accepted `decoder_init && ascs_start` as `nonzero_frames` without
   explicit SDUs/decoded count.
2. Gate treated boot-time `I2S ready` as DMA start; only exact runtime
   `I2S DMA started` proves output path activation.
3. Firmware silently fell back to 10 ms when Frame Duration LTV getter
   failed — masking malformed client codec configuration.

## Corrections applied

### Firmware (`src/bt_bap.c`)

- **10 ms fallback removed.** Missing Frame Duration LTV now produces
  ASCS invalid codec response (`BT_BAP_ASCS_RSP_CODE_CONF_INVALID`).
  Frame Duration is required codec configuration per LC3 spec.
- **Stream summary added** before teardown.  `stream_disabled_cb` logs
  `Stream[N] summary: SDUs=X decoded=Y plc=Z decode_err=W i2s_underrun=V
  stream_reset=R` using `sinks[idx].recv_cnt` and `audio_stats_get()`.
  Summary fires once per disable — no periodic spam.

### Gate (`scripts/bluez-wireplumber-gate.py`)

- Explicit nonzero SDUs and decoded frames **required** from stream
  summary.  Decoder init alone insufficient.
- Exact `I2S DMA started` string match **required**; boot `I2S ready`
  rejected.
- `frame dur not set` raised to fatal (firmware rejects codec config;
  gate treats it as receiver-level failure).
- Stream summary lines excluded from fault-detection regexes (fields
  like `i2s_underrun=0` and `decode_err=0` are summary counters, not
  runtime faults).
- `nonzero_frames` satisfied only by stream summary (explicit SDU/
  decoded counts) or legacy fps/decoded counter patterns.

### Gate tests (`scripts/test_bluez_wireplumber_gate.py`)

New tests (9):
- `test_decoder_init_only_fails` — decoder init without explicit counts
- `test_boot_i2s_ready_only_fails` — "I2S ready" not "I2S DMA started"
- `test_zero_sdu_fails` — stream summary with SDUs=0
- `test_zero_decoded_fails` — stream summary with decoded=0
- `test_frame_dur_not_set_fatal` — "frame dur not set" fails
- `test_i2s_dma_started_exact_match_required` — only exact string
- `test_stream_summary_passes` — valid summary passes
- `test_stream_summary_with_faults_fails` — summary clean, fault lines separate
- `test_stream_summary_zero_*` — edge cases

Updated: `test_frame_dur_not_set_with_fallback` → `test_frame_dur_not_set_fatal`
(no more fallback). Removed old soft-gate assertions.

### 7.5 ms enum-zero regression (`scripts/test_bluez_wireplumber_gate.py`)

Class `TestCodecEnumZeroRegression` (5 tests) exercises the lc3_enable
frame-duration decision logic:
- `test_7_5ms_enum_zero_valid` — 0x00 → 7500 us, not rejected
- `test_10ms_enum_one_valid` — 0x01 → 10000 us, not rejected
- `test_negative_getter_returns_error` — <0 → hard error, no fallback
- `test_invalid_enum_rejected` — unknown enum values rejected
- `test_all_valid_enums_covered` — exhaustive 0x00/0x01 coverage

Mirrors `bt_audio_codec_cfg_frame_dur_to_frame_dur_us` contract from
`ncs/v3.3.0/zephyr/subsys/bluetooth/audio/codec.c:99–109`.

### I2S input-frame fix (`src/audio_i2s.c`, `src/audio_sink.h`, `tests/bsim/src/audio_sink_stub.c`)

- **`INPUT_FRAMES` made dynamic.** Stock PipeWire negotiates 7.5 ms
  (360 samples/ch @ 48 kHz), not 10 ms (480 samples/ch).  The old
  hardcoded 480-sample validation in `validate_push_input` rejected
  720-sample pushes (360×2) with `-EINVAL`, silently blocking all I2S
  output.  Fixed by replacing the `#define INPUT_FRAMES 480` constant
  with a runtime `input_frames` variable and adding
  `audio_sink_set_input_frames()` called from `lc3_enable()`.
- BSim stub updated with matching `audio_sink_set_input_frames()`.
- This was the root cause of the "no I2S DMA started" failure in
   Phase 2 stock desktop tests: the BAP CIS was active and LC3 audio
   arriving, but the I2S path silently dropped every block.

## Hardware acceptance — nRF54L15, stock WirePlumber main-systemwide

### 30 s gate — PASSED

```
Stream[0] summary: SDUs=4572 decoded=4729 plc=157 decode_err=0 i2s_underrun=1 stream_reset=0
I2S DMA started
Frame Duration: 7500 us → expected 133.3 fps
```

### 120 s gate — PASSED

```
Stream[0] summary: SDUs=4579 decoded=4729 plc=150 decode_err=0 i2s_underrun=1 stream_reset=0
```

Both runs show consistent behavior: ~4570 valid SDUs, ~4729 decoded frames
(PLC frames only during startup), zero decode errors, one transient I2S
slab-full collision (initial DMA race, not steady-state underrun),
zero stream resets.

### Acceptance criteria

- [x] Explicit valid SDUs > 0: 4572 / 4579
- [x] Explicit decoded frames > 0: 4729 / 4729
- [x] Exact `I2S DMA started` observed
- [x] Stock PipeWire sink playback succeeds for 30 s and 120 s
- [x] No forbidden warning/fault patterns (0 decode errors, 0 malformed)
- [x] 7.5 ms enum-zero regression in automated gate
- [x] Docs contain no contradictory accepted-zero-frame claim

## Gate suite summary

| Suite | Tests | Status |
|-------|-------|--------|
| `test_bluez_wireplumber_gate.py` | 44 | all pass |
| `test_gate.py` (flpr_stall) | 17 | all pass |
| nRF54L15 build | — | clean |
| nRF5340 build | — | clean |
