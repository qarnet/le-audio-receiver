# Phase 6 Stage 3A — FLPR ASRC core and ring ABI v4

## Goal

Add deterministic fixed-point ASRC processing to FLPR and prove wire-state
continuity against cpuapp reference. Do not route live audio through ASRC yet;
Stage 2 identity remains production path in this substage.

## Fixed architecture

ASRC state is supplied with every input block and returned with every output
block. FLPR does not own hidden continuity state. This guarantees cpuapp ASRC
fallback can advance during any offload outage; next FLPR request starts from
exact cpuapp state without reset/synchronization protocol.

### Generic ASRC state

In `audio_asrc.h`, add fixed-layout `struct audio_asrc_state`:

```c
uint64_t phase;
uint64_t step_base;
int16_t prev_l;
int16_t prev_r;
uint8_t prev_valid;
uint8_t reserved[3];
```

Size 24 bytes. Add export/import helpers. Export zeroes reserved bytes. Import
rejects null, step_base zero, prev_valid >1, or nonzero reserved; it writes ctx
and previous-frame outputs only after full validation. Keep existing process API
unchanged to avoid unrelated Phase 5 churn.

### Ring ABI v4

- Metadata grows 32→64 bytes; payload offset becomes 64. Slot stride remains
  2016: 64 + 1924 = 1988, leaving 28 pad bytes. Ring still exactly 8192 bytes.
- Embed `struct audio_asrc_state asrc_state` at offset 32.
- Final 8 bytes: `uint32_t processing_cycles`, `int32_t processing_status`.
- Bump `FLPR_RING_ABI_VERSION` 3→4. Add size/offset/total-size assertions.
- Add flag `FLPR_SLOT_FLAG_ASRC_LINEAR = 0x0008`.
- Both cores little-endian; no pointer, bool, size_t, or enum fields cross wire.

### Pure processing module

Add `src/flpr_audio_process.c/.h`, built by FLPR and native tests. One function
accepts input metadata/payload and output buffers/metadata capacity.

- Without ASRC flag: preserve Stage 2 bit-exact identity behavior and metadata.
- With ASRC flag: require 480 input frames; import wire state; call accepted
  `audio_asrc_process()` with metadata correction_ppm and 481-frame capacity;
  require consumed=480 and produced 1..481; export post-state; set output
  valid_frames=produced, payload CRC over produced bytes, status=0, ASRC flag.
- Validate ppm range and state. Return deterministic errno on malformed input or
  capacity failure; never partially publish output.
- No heap, float, static hidden ASRC context, atomics, or GRTC access.

FLPR `main.c` calls processor after CRC validation, measures only processor via
`k_cycle_get_32()`, stores delta in output metadata, then publishes. Failed
processing increments a diagnostic and consumes input only after emitting an
explicit error output metadata with same sequence/epoch, zero valid_frames,
negative `processing_status`, valid flag + ASRC flag. Adjust ring validation to
allow zero-frame output only when `processing_status < 0`; normal slots still
require nonzero frames. CPU Stage 3B will treat error output as fallback/fault.

## Tests

1. Generic state export/import roundtrip and transactional rejection.
2. Metadata exact size/offset and unchanged 8 KiB ring layout.
3. Identity vectors remain bit-exact.
4. ASRC one block and multi-block output bytes/count/post-state exactly match
   direct host reference for ppm: 0, +2000, -2000, sign-changing sequence.
5. 60,000-block cumulative count/error bound matches Phase 5 reference.
6. Fallback continuity simulation: process N blocks FLPR helper, M blocks direct
   cpuapp from returned state, then resume FLPR helper; compare uninterrupted
   direct reference bytes/count/state.
7. Invalid state/ppm/frame/capacity and explicit error-output behavior.
8. Existing ring/protocol/offload/ASRC tests all pass.

## Verification

- Inspect FLPR ELF/map/disassembly: no floating-point instructions/symbols, no
  heap allocation; report code/RAM and processor cycle statistics from a
  synthetic FLPR ring command if already available.
- Both nRF54 and nRF5340 builds clean. nRF5340 must not compile FLPR module.
- Flash nRF54; protocol/ring ABI handshake clean; 60 s Stage 2 identity Mode A
  remains 100 fps and zero-fault.

Commit Stage 3A code/tests/docs. No live ASRC routing, bt_bap/audio_i2s changes,
recovery-policy changes, HPF/VEVIF optimization, BabbleSim, security changes,
mass erase, push, or analog claim.
