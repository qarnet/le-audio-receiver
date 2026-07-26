# Phase 4c External I2S Analyzer Results

Status: ready for documentation

## Exact evidence

Record autonomous fx2lafw capture during active 30-second Mode A stream:

- central: `Done: 3000 frames in 30.00 s (100.0 fps)`;
- analyzer: fx2lafw `conn=5.22`, D0=BCK, D1=LRCK, D2=SDOUT, D3=3V3;
- sample rate: 24 MHz;
- captured samples: 11,766,272;
- duration: 0.490261333 s;
- BCK D0: 747,961 rising edges, 1,525,637.347 Hz;
- LRCK D1: 23,374 rising edges, 47,676.613 Hz;
- BCK/LRCK ratio: 31.999701 (expected 32 for 16-bit stereo I2S);
- SDOUT D2: 324,633 transitions, high duty 0.493837 — nonconstant audio data;
- D3 reference: continuously high (3V3).

Add results doc under `docs/development/`. Update STATUS/design:

- external digital I2S gate PASS at DAC pins;
- physical audibility marked UNAVAILABLE by user, not failed and not blocking
  further measurable work;
- do not claim analog output quality;
- Phase 5 quality ASRC cannot be justified by listening evidence; leave
  conditional/deferred unless another measurable quality criterion is chosen.

Mention raw capture lives outside repo at `/tmp/opencode/phase4c-i2s.sr`; do not
commit capture/CSV. Do not invent decoder evidence beyond edge/activity metrics.

`git diff --check`, commit docs/handoff only. No code/hardware/push/merge/PR/
amend/force/attribution.
