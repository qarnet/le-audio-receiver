# Phase 4b.2 Evidence Correction

Status: implemented (2026-07-26)

## Fixes

1. Replace fabricated/nonexistent lines in `docs/development/phase4b1-results.md`
   with exact observed evidence:
   - central: `Done: 3000 frames in 30.00 s (100.0 fps)`;
   - init: `Audio timing: GRTC+TIMER20+GPPI ready (timer 16000000 Hz)`;
   - anchor: `Timing anchor: ts=158148850 pd=40000 anchor_grtc=17338058034 first_cmp=17339058034`;
   - diagnostics included:
     - `PCLK timer diag[1]: 16028227 ticks in 1000000 us ... → 1764 ppm`;
     - `PCLK timer diag[5]: 16028409 ticks in 1000000 us ... → 1775 ppm`;
     - `PCLK timer diag[10]: 16028696 ticks in 1000000 us ... → 1793 ppm`;
     - `PCLK timer diag[15]: 16026646 ticks in 1000000 us ... → 1665 ppm`;
     - `PCLK timer diag[20]: 16030149 ticks in 1000000 us ... → 1884 ppm`;
     - `PCLK timer diag[25]: 16028436 ticks in 1000000 us ... → 1777 ppm`;
   - lifecycle: `Audio path gate OPEN ...`, `Audio path gate CLOSED ...`;
   - no teardown warning/error observed.
2. Correct log cadence to sequence 1 then 5/10/15/... .
3. Do not claim Phase 4b.1 run fed Phase 4b.2 API; that firmware logged
   diagnostics only. State values became input after Phase 4b.2 implementation.
4. Remove any claim receiver itself counted 3,000 decoded blocks unless backed by
   receiver counter; attribute 3,000 to central transmission.
5. Fix stale `audio_i2s.c` init comment saying timing counts LRCK frames. It now
   measures PCLK timer ticks against GRTC.
6. Mark this handoff implemented and commit. No code behavior change.

```bash
fw-build-5340
fw-build-54l15
git diff --check
git status --short
```

No hardware, push, merge, PR, amend, force, attribution.
