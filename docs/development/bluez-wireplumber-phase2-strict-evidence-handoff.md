# Phase 2 correction — strict rendered-audio evidence

## Goal

Remove remaining false-positive paths and prove actual LC3 receive/decode/I2S
activity during stock WirePlumber playback.

## Defects found in review

1. Gate turns `decoder_init && ascs_start` into `nonzero_frames=true` without a
   frame counter.
2. Gate treats boot-time `I2S ready` as DMA start; only exact runtime
   `I2S DMA started` proves output path activation.
3. Firmware silently falls back to 10 ms when Frame Duration LTV getter fails.
   Frame Duration is required codec configuration; fallback was not requested
   and masks malformed client config.
4. Results retain stale “PipeWire has no LC3 encoder/no valid frames” claims.
5. Strict hardware artifacts/counters are not preserved clearly.

## In scope

### Firmware

- Remove 10 ms missing-frame-duration fallback. Getter/conversion errors must
  set ASCS invalid codec response and return a negative errno.
- Keep valid enum-zero 7.5 ms handling (`ret < 0`).
- Add one low-noise stream summary before teardown/reset, using existing
  `recv_cnt` and `audio_stats_get()` values. Include stream index, valid SDUs,
  decoded total, PLC, decode errors, I2S underruns, and stream resets. Avoid
  periodic UART spam.
- Ensure summary occurs before stats reset and remains correct for Mode A and
  Mode B teardown callbacks.

### Gate

- Require explicit nonzero valid-SDU count and explicit nonzero decoded-frame
  count from receiver summary/status/log. Decoder initialization is necessary
  evidence but never substitutes for frames.
- Require exact runtime `I2S DMA started`; boot `I2S ready` must not pass.
- Make any `frame dur not set`, fallback text, decoder-not-ready, malformed,
  decode, I2S, or offload fault fatal.
- Validate negotiated-rate consistency when enough timing/count data exists;
  do not invent FPS from absent data.
- Add tests proving decoder-init-only, boot-I2S-ready-only, zero-SDU,
  zero-decoded, missing-frame-duration, and stale warning logs fail.

### Regression coverage

- Add smallest executable C/BSim regression that exercises 7.5 ms enum-zero
  through receiver codec setup path. Prefer parameterizing existing BSim client
  to negotiate 7.5 ms; preserve separate 10 ms deterministic scenario if hash
  changes. If that is disproportionate, isolate codec getter/conversion parsing
  into a testable helper used by `lc3_enable()` and test enum-zero plus negative
  errno. No source-text assertion as sole regression.

### Docs and hardware

- Rewrite, not append around, stale Phase 2 results. Remove no-encoder/zero-frame
  acceptance claims. Preserve rejected-commit history concisely.
- Run unit/BSim/full gates and both target builds.
- Build/flash nRF54L15, capture serial before reset, run stock
  `main-systemwide` WirePlumber 30 s and 120 s playback.
- Preserve strict gate output and receiver raw log under stable `/tmp` artifact
  names and copy key counters into results.
- Restore normal WirePlumber service and verify no stray process.

## Acceptance

- Explicit valid SDUs > 0.
- Explicit decoded frames > 0.
- Exact `I2S DMA started` observed.
- Stock PipeWire sink playback succeeds for 30 s and 120 s.
- No forbidden warning/fault patterns.
- 7.5 ms enum-zero regression executable in automated gate.
- Docs contain no contradictory accepted-zero-frame claim.

## Constraints

No CAP/CAS, host custom rules, custom endpoint, raw-HCI, `bap_central.py`,
direct ISO, package/NCS edits, mass erase, push, merge, PR update, or analog
audibility claim. Create new correction commit; do not amend/rewrite old commits.
