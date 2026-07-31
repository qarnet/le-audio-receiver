# Phase 2 correction — duration and zero-fault proof

## Goal

Produce independent 30 s and 120 s strict stock-desktop evidence with expected
SDU volume and zero summary faults.

## Review failures

- Both claimed passes report `i2s_underrun=1`; warnings/faults must be zero.
- 30 s and 120 s counters are nearly identical. At 7.5 ms, 120 s should yield
  roughly 16,000 SDUs for one ASE, not 4,579. Evidence is stale/truncated or
  playback did not run requested duration.
- Gate ignores summary fault fields. Existing test explicitly allows
  `decode_err=5` to pass.
- “Enum-zero regression” reimplements C logic in Python; it does not execute
  receiver code.
- Gate retains disproven no-MediaEndpoint diagnosis.
- Strict evidence handoff file remains untracked.

## In scope

1. Parse all summary fields: SDUs, decoded, PLC, decode errors, I2S underruns,
   stream resets. Require decode_err=0, i2s_underrun=0, stream_reset=0. PLC may
   be nonzero but report rate and investigate excessive startup/steady loss.
2. Require SDU count consistent with requested duration and negotiated frame
   duration. For one 7.5 ms ASE, enforce a bounded count around
   `duration * 1e6 / 7500`; similarly for 10 ms. Account explicitly for stream
   startup/teardown tolerance, not stale maxima.
3. Scope parsing to current run only. Flush UART RX before capture, truncate
   output, mark/capture run boundaries, and preserve independent raw logs:
   `/tmp/phase2-strict-30s-receiver.log`, `/tmp/phase2-strict-120s-receiver.log`,
   plus matching gate and WirePlumber logs.
4. Diagnose exact `i2s_underrun=1` timestamp/path. Fix root cause; do not relabel
   slab-full/drop as harmless. Preserve prior ASRC/I2S invariants.
5. Replace Python-mirrored enum test with executable receiver-code coverage.
   Parameterize or add BSim 48 kHz 7.5 ms preset scenario so `lc3_enable()` and
   I2S sink-size path execute. Keep existing 10 ms scenario or equivalent
   regression. Record deterministic hashes separately.
6. Remove stale `_no_media_endpoint()` diagnosis and misleading endpoint object
   inference from gate. Use SPA-monitor and sink evidence already established.
7. Add tests for nonzero summary fault counters, stale/multiple summaries,
   insufficient duration counts, exact-duration bounds, and independent logs.
8. Run full test gate and both builds, flash nRF54L15, then fresh 30 s and 120 s
   stock WirePlumber runs. Restore normal service.
9. Rewrite results with raw artifact names, actual elapsed playback, negotiated
   duration, expected/actual SDUs, decoded/PLC/fault counters, I2S start, and
   zero warnings.

## Acceptance

- Independent 30 s count near 4,000 SDUs at 7.5 ms.
- Independent 120 s count near 16,000 SDUs at 7.5 ms.
- decoded > 0; exact I2S DMA start.
- decode_err=0, i2s_underrun=0, stream_reset=0, malformed/offload faults=0.
- Actual BSim/C path covers valid enum-zero 7.5 ms.
- All tests/builds pass; worktree clean; docs truthful.

## Constraints

No weakened thresholds, custom endpoint, raw-HCI, `bap_central.py`, direct ISO,
host rules, package/NCS edits, CAP/CAS, mass erase, push/PR, amend/rewrite.
Create correction commit only after strict acceptance.
