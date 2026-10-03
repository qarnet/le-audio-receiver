---
id: PB-045
title: Add independent ASRC arithmetic and full-waveform oracle
status: In Progress
assignee: []
created_date: '2026-10-03 02:33'
updated_date: '2026-10-03 13:27'
labels:
  - 'size:M'
  - 'area:testing'
  - 'area:audio'
  - 'area:flpr'
dependencies: []
references:
  - docs/development/independent-firmware-validation-research-20261003.md
priority: p2
type: tech-debt
ordinal: 42000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
ASRC determinism tests compare production instances; irregular-chunk checks predominantly compare counts. Offload comparison also shares the production algorithm.

### Desired outcome
Every produced sample and frame count is checked against a separately expressed reference model, including state/lifecycle boundaries and current offload behavior.

### Scope
Model continuous/global input coordinates with high-precision or rational arithmetic, explicit signed rounding and independently justified tolerance. Exercise silence/DC, ramps, fractional extrema, boundary impulses, distinct L/R, irregular chunks, changing ppm, capacity failure/retry, reset and CPU/FLPR transitions.

### Non-goals
Do not use production ASRC or private phase state to calculate expected samples, substitute a different filter as a bit-exact linear-ASRC oracle, redesign resampling policy, or implement PB-013 360-frame FLPR offload. LC3-only: no LC3plus implementation, source, vectors or licensing assumptions. Preserve existing regression fixtures, frozen HIL/PCM limits and immutable evidence. No public release, Bluetooth qualification or unrelated PB-013 feature claim.

### Technical context
src/audio_asrc.c, tests/unit/asrc/src/test_asrc.c and tests/unit/offload_asrc_verify/src/test_offload_asrc_verify.c show current boundaries. src/audio_offload.c and src/flpr_ring.h retain current 480-frame offload contract; current 360-frame CPUAPP behavior is separately testable.
Research: docs/development/independent-firmware-validation-research-20261003.md

### Open questions
Independent numeric model, signed rounding and tolerance derivation; approved current ratio/ppm envelope; host/target offload evidence and signal-quality limits distinct from arithmetic.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Real public ASRC outputs are compared sample-by-sample and by consumed/produced counts against an independently expressed model that does not call the DUT or inspect private phase state.
- [x] #2 Regular and irregular partitions follow the same global input and control-change schedule and preserve complete expected waveform/count behavior.
- [x] #3 Signed fractional/extreme cases, block-edge impulses, distinct channels, ppm changes, insufficient capacity/retry and reset exercise observable arithmetic and lifecycle outcomes.
- [x] #4 Current CPUAPP and supported FLPR paths meet the refined same reference contract, including fallback/handover continuity, without adding unsupported 360-frame offload.
- [x] #5 Rounding/sign, boundary-history and routing defects are rejected by meaningful negative controls; arithmetic, spectral-quality and implementation-parity verdicts stay distinct.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Add stdlib Python Fraction global-coordinate oracle and native C adapter over public audio_asrc and real flpr_audio_process, with opaque state tokens. Compile real sources using warnings-as-errors. 2. Compare every PCM sample/count for authored signals, regular/irregular partitions, fixed global control schedule, capacity failure/retry, reset and CPU/FLPR/360-frame fallback handover. Add known-value reference self-checks and compiled source mutation controls. 3. Retain pre-fix failure for signed correction rounding; evaluate practical physical diagnostic boundary before behavior-changing repair. Fix proven arithmetic/documentation defects without new resampling policy. 4. Run focused oracle and native Zephyr suites, inventory/matrix/documentation and full repository gates; build physical images and collect fresh hardware evidence where actual execution is claimed. 5. Record acceptance evidence and remaining boundaries, commit intended files only, push PR #16 and confirm all hosted checks before next item.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Refinement: use independently derived rational global source coordinates, quantized rate and symmetric nearest/ties-away signed ppm correction; exact full stereo samples and counts, no DUT phase reads. CPU ratio cases 1:2, 1:1, 2:1 and production 48000:47619; ppm 0, ±1, ±500, ±2000, API edges ±3000. Align control changes at global input offsets across all partitions. Native real FLPR processor plus opaque export/import tests establish processor arithmetic and continuity, not physical mailbox/I2S/analog acceptance. Separate physical execution remains necessary for actual offload claims. Inspection found negative correction rounding discrepancy (identity -1 ppm: current -4294 ticks versus nearest -4295), and tiny-chunk header promises stronger than implementation. Preserve failing regression evidence; do not copy wrong expression into oracle or widen tolerance.

Independent oracle and native real sink/offload/processor integration now pass. Eight compiled arithmetic/count/guard mutations are rejected. Physical CPUAPP isolation reproduced the old signed-ppm defect (20 pass/1 fail) and verified repair (21 pass/0 fail) with five fresh raw DP/AP/FICR+USB identity checkpoints; see docs/development/pb-045-independent-asrc-results-20261003.md. Actual physical FLPR transport/execution, spectral/analog/presentation claims remain excluded from these arithmetic verdicts. Primary dirty-tree canonical run had 80 pass/2 fail/82 total solely because coverage requires a clean exact commit, then matrix lacked coverage JSON. Next clean detached candidate verification preserves PB-013. Done and final acceptance remain pending clean gates and hosted checks.
<!-- SECTION:NOTES:END -->
