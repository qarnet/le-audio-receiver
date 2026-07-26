# Phase 4c Technical Results

Date: 2026-07-26
Status: technical PASS — audibility pending

## Test configuration

- Target: nRF54L15 (Seeed Xiao), ncs v3.3.0, SDC on cpuapp
- Actuator: SAMPLE_ADJUST (sample insert/drop)
- Controller: PCLK feedforward + phase PI (Phase 4b.2)
- Central: nRF5340DK hci_uart, bap_central.py
- Stream: 48 kHz LC3 stereo Mode A, 100 fps, 10 minutes

## Exact evidence

- central output: `Done: 60000 frames in 600.00 s (100.0 fps)`
- 10-minute uninterrupted stereo stream; no disconnect
- zero slab-full drops, zero DMA underrun/restart
- zero warnings, errors, faults, or assertions on receiver console
- clean first-disable gate close; clean teardown
- PCLK diagnostics active for full run: logged samples roughly +1,523 to
  +2,058 ppm
- sample correction overwhelmingly insert direction:
  - startup settled at 13 drops
  - inserts rose monotonically
  - last logged total: `ins=51487 drops=13 (total=51500)`
  - average insert rate: ~86/s

## Gate status

| Gate | Result |
|---|---|
| 10-minute uninterrupted stream | PASS |
| No disconnect | PASS |
| No slab-full / underrun / storm | PASS |
| Clean teardown | PASS |
| PCLK diagnostics active | PASS |
| Controller converged (insert-only after startup) | PASS |
| Audible quality | PENDING (user observation required) |

## Notes

- Sample adjustments are frequent (~86/s) at ~+1,800 ppm PCLK offset —
  expected for SAMPLE_ADJUST actuator with HFINT/PCLK mismatch. This does
  not indicate controller non-convergence.
- Audible artifact character and acceptability remain unmeasured.
  *(Supersession 2026-07-27: Phase 5 ASRC is now planned implementation work
  per `docs/design.md` revision — engineering risk of ~86 discontinuous
  inserts/s is sufficient rationale, not dependent on listening test.)*
