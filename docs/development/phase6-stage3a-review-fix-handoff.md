# Stage 3A review fixes

## Required corrections

1. Include `audio_asrc.h` from `flpr_ring.h` and embed typed
   `struct audio_asrc_state asrc_state` at offset 32. Remove `asrc_raw[3]` and
   all pointer casts. Preserve size 64/offset assertions.
2. `audio_asrc_state_import()` rejects phase greater than `1ULL << 32`.
   Phase zero through Q32_ONE is valid; validation remains transactional.
3. In ASRC processor, check `audio_asrc_process()` return before reading
   consumed/produced. Map capacity vs invalid errors deterministically. Initialize
   all local outputs defensively.
4. Always verify nonzero input payload CRC inside pure processor before identity
   copy or ASRC. Add explicit BAD_CRC result. Main test diagnostics may count it,
   but must not be sole validation. ASRC output CRC remains over produced bytes.
5. Identity output preserves all validated input flags (`input flags | VALID`),
   matching Stage 2 metadata-copy behavior.
6. Error output retains measured processing_cycles: construct error metadata,
   then call processing setter last. Preserve sequence, epoch, correction_ppm,
   cpu_timestamp, ASRC flag, valid_frames=0, negative status.
7. Increase cumulative test to 60,000 blocks. Verify exact direct-reference
   output count/state and documented one-frame cumulative ideal bound.
8. Add tests for impossible phase, input CRC corruption in identity and ASRC,
   negative/capacity return handling, error processing_cycles preservation,
   identity flag preservation, typed state offsets.

Run full ASRC/processor/ring/protocol/offload tests, both builds, ELF FPU/heap
inspection. Flash nRF54 and run Stage 2 identity 60 s using exact normal command
without `--peer-addr`; if external test blocked, return exact command/log without
claiming an interop defect. Commit new fix; no amend/push/security/live-routing
changes.
