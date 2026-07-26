# Phase 4b.2 — PCLK feedforward + phase PI: HARDWARE PASS

Date: 2026-07-26
Status: PASS

## Autonomous hardware evidence

All values are exact, from serial-mcp captured logs. No tuning, no
manual intervention.

### Central stream

```
Done: 4500 frames in 45.00 s (100.0 fps)
```

Stream ran at correct rate for full duration, zero central-side stalls.

### PCLK frequency diagnostics

PCLK diagnostics ranged approximately +1,500 to +1,757 ppm in observed run.

### Before first PCLK measurement

Phase-only startup produced 16 drops before PCLK feedforward engaged.

### Correction track — sample adjustments dominated by inserts

Correction operated in the correct direction (PCLK fast → consume faster →
insert samples):

```
Sample adjustments: ins=484 drops=16 (total=500)
Sample adjustments: ins=984 drops=16 (total=1000)
Sample adjustments: ins=1484 drops=16 (total=1500)
Sample adjustments: ins=1984 drops=16 (total=2000)
Sample adjustments: ins=2484 drops=16 (total=2500)
Sample adjustments: ins=2984 drops=16 (total=3000)
```

Insert-to-drop ratio ≈ 186:1, consistent with +1,500..+1,757 ppm feedforward.
Drops are from the 16 PCLK-blind startup events; steady-state corrections are
insert-only.

### Clean teardown

Gate closed on first disable. No slab-full drops, no I2S underruns, no
warnings, no faults, no post-disable DMA restart.

## Conclusion

Phase 4b.2 hardware PASS. Closed-loop PI controller with PCLK feedforward
corrects peer drift on nRF54L15. Phase 4c (10-minute stability + listening
test) remains next.
