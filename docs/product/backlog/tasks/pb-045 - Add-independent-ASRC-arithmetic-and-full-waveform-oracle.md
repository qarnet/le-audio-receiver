---
id: PB-045
title: Add independent ASRC arithmetic and full-waveform oracle
status: Backlog
assignee: []
created_date: '2026-10-03 02:33'
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
- [ ] #1 Real public ASRC outputs are compared sample-by-sample and by consumed/produced counts against an independently expressed model that does not call the DUT or inspect private phase state.
- [ ] #2 Regular and irregular partitions follow the same global input and control-change schedule and preserve complete expected waveform/count behavior.
- [ ] #3 Signed fractional/extreme cases, block-edge impulses, distinct channels, ppm changes, insufficient capacity/retry and reset exercise observable arithmetic and lifecycle outcomes.
- [ ] #4 Current CPUAPP and supported FLPR paths meet the refined same reference contract, including fallback/handover continuity, without adding unsupported 360-frame offload.
- [ ] #5 Rounding/sign, boundary-history and routing defects are rejected by meaningful negative controls; arithmetic, spectral-quality and implementation-parity verdicts stay distinct.
<!-- AC:END -->
