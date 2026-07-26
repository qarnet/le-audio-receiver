# Phase 4b.2 Results + Diagnostic Log Cleanup

Status: ready for implementation

## Hardware result

Record exact 2026-07-26 autonomous result:

- central: `Done: 4500 frames in 45.00 s (100.0 fps)`;
- PCLK diagnostics ranged approximately +1,500 to +1,757 ppm in observed run;
- before first PCLK measurement, phase-only startup produced 16 drops;
- correction then dominated in correct direction:
  - `Sample adjustments: ins=484 drops=16 (total=500)`;
  - `ins=984 drops=16 (total=1000)`;
  - `ins=1484 drops=16 (total=1500)`;
  - `ins=1984 drops=16 (total=2000)`;
  - `ins=2484 drops=16 (total=2500)`;
  - `ins=2984 drops=16 (total=3000)`;
- clean teardown: gate closed on first disable; no slab-full, I2S underrun,
  warning, fault, or post-disable DMA restart.

Add `docs/development/phase4b2-results.md`. Update STATUS/design to Phase 4b.2
hardware PASS; Phase 4c 10-minute stability/listening remains next.

## Remove temporary INFO spam

From `src/bt_bap.c`, remove temporary bring-up diagnostics:

- first-five `stream_recv[...]` INFO block;
- unconditional every-50 combined valid/invalid tally;
- first-five `push_stereo` INFO block.

Keep configured `CONFIG_INFO_REPORTING_INTERVAL` behavior, errors/warnings,
lifecycle gate OPEN/CLOSED logs, PCLK 5-second diagnostics, and bounded sample
adjustment logs. Restore direct return handling cleanly; do not discard
`audio_sink_push()` errors silently if existing policy expects handling.

## Verification

```bash
fw-build-5340
fw-build-54l15
git diff --check
git status --short
```

No hardware, PI tuning, RADIO, ASRC, push, merge, PR, amend, force,
attribution. Commit scoped docs/log cleanup and return exact results/hash.
