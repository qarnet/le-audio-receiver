# Phase 6 Stage 3B — live FLPR ASRC integration

## Goal

Route nRF54 decoded PCM through FLPR ASRC, while retaining bit-identical cpuapp
ASRC fallback and untouched nRF5340 identity/APLL behavior. This substage ends
with unit/build integration; long hardware acceptance follows separately.

## Fixed data path

```text
bt_bap decode/volume -> audio_sink_push(480 stereo frames)
  -> cpuapp drift PI computes correction_ppm
  -> FLPR ASRC request with exact pre-state + ppm
  -> verified variable 1..481-frame output directly into I2S slab
  -> on any offload fault: cpuapp audio_asrc_process from unchanged pre-state
```

Offload moves from `bt_bap.c` into `audio_i2s.c` because drift ppm and ASRC state
are owned there. Never ASRC twice.

## Exact changes

### Ring manager

- Add typed ASRC produce/consume APIs; preserve Stage 1/2 wrappers/tests.
- Produce: exactly 480 frames, ASRC flag, correction ppm, typed pre-state,
  payload CRC always enabled.
- Consume result includes sequence, output frames, payload CRC, post-state,
  processing_cycles/status, RTT. Error output (`status<0`, frames=0) is valid
  transport response; normal output requires 1..481 frames.
- Snapshot metadata before `consume_done`; never expose shared slot pointers.

### Audio offload

Add:

```c
struct audio_offload_asrc_result {
    uint16_t output_frames;
    struct audio_asrc_state post_state;
    uint32_t processing_cycles;
};

int audio_offload_process_asrc(
    const int16_t *input, uint16_t input_frames, uint32_t sequence,
    int32_t correction_ppm, const struct audio_asrc_state *pre_state,
    int16_t *output, uint16_t output_capacity,
    struct audio_offload_asrc_result *result);
```

- nRF54 only; output/result untouched on every failure.
- Module-static ring receive scratch is 481 stereo frames (1924 B).
- Validate status=0, ASRC flag, sequence, frame range/capacity, correction echo,
  output payload CRC, typed post-state import, unchanged step_base, and reserved
  bytes. Then copy to caller and commit result.
- Existing lifecycle/recovery/probation accounting applies unchanged.
- Add status counters and min/max/sum/count for FLPR processing_cycles.
- Keep identity submit for existing tests until Stage 5 cleanup.

### Optional shadow verification

Kconfig `AUDIO_OFFLOAD_ASRC_VERIFY`, nRF54+ASRC only, default n. When enabled,
offload module runs direct cpuapp ASRC from same pre-state/ppm into static 481
frame buffer and compares return, frame count, every sample, and post-state.
Mismatch records payload/state validation fault, poisons offload, returns error
with caller output untouched. Production build disables this option.

### I2S integration

- In `fill_block_asrc`, allocate/zero slab, export current cpuapp state, call
  offload ASRC with current ppm and monotonic sequence.
- Success: transactionally import returned post-state into temporary context,
  then commit `asrc_ctx`, prev L/R/valid; use FLPR output frame count directly.
- Failure/PREPARING/RECOVERING/FALLBACK: call existing cpuapp ASRC once into same
  slab from unchanged current state and commit existing way.
- Increment sequence once per accepted 480-frame push; reset in `drift_reset`.
- CPU ASRC perf counter measures fallback/shadow work only. Add distinct offload
  counters rather than pretending FLPR cycles are cpuapp cycles.

### BAP cleanup

Remove `offload_out`, Stage 2 identity submit calls, and BAP-owned offload block
sequence from all Mode A/Mode B/mono decode branches. Keep offload lifecycle
start/stop calls. BAP always passes decoded `stereo_out` directly to sink.

### Build selection

Add `AUDIO_OFFLOAD_ASRC` bool depending on nRF54 + ASRC linear; enable in nRF54
board conf. Calls compile out on nRF5340. nRF5340 source/binary behavior remains
decode -> sink identity/APLL with no FLPR symbols in path.

## Tests

1. Ring typed metadata/output/error transport tests.
2. Offload success variable frames/post-state/cycles; all validation faults;
   output/result untouched; lifecycle races and recovery preserved.
3. I2S integration with mocked offload: success skips cpu ASRC, PREPARING/fault
   uses cpu fallback once, state continuity across FLPR->fallback->FLPR, sequence
   reset, output sizes 479/480/481.
4. Shadow verifier exact pass and sample/count/state mismatch failures.
5. BAP Mode A/B routing tests prove one sink call and no direct offload call.
6. Existing ASRC/ring/protocol/offload/decode/lifecycle tests pass.

## Verification

- Both builds clean; compare RAM before/after. Removing BAP 1920-byte buffer
  should offset any production metadata growth. Verification-only buffer may
  consume that headroom only when enabled.
- FLPR remains no FPU/heap; report code/RAM.
- No hardware run in this substage. Commit code/tests/docs, no push/amend,
  security/recovery-policy change, HPF, BabbleSim, mass erase, or analog claim.
