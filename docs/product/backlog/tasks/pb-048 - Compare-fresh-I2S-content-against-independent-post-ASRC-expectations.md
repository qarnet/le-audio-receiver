---
id: PB-048
title: Compare fresh I2S content against independent post-ASRC expectations
status: Backlog
assignee: []
created_date: '2026-10-03 02:33'
labels:
  - 'size:L'
  - 'area:testing'
  - 'area:hil'
  - 'area:hardware'
  - 'area:audio'
dependencies:
  - PB-041
  - PB-044
  - PB-045
  - PB-047
references:
  - docs/development/independent-firmware-validation-research-20261003.md
priority: p2
type: tech-debt
ordinal: 45000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
Passive analyzer captures currently establish geometry, not independently derived PCM words, fresh content or complete post-ASRC processing correctness.

### Desired outcome
Fresh identified digital output is compared against independently derived expected sample sequence/count/routing through the real production processing pipeline.

### Scope
Reuse PB-041 harness identity. Bind S2/S3 decoded reference content and S4 independent conversion with declared public gain/rate/control inputs, then decode freshly captured I2S words. Cover distinct channels and current lifecycle/offload fallback transitions with safe bounded stimulus and immutable capture evidence.

### Non-goals
Do not copy observed DUT words as golden truth, duplicate PB-041, infer DAC presence/function from driven wires, qualify analog fixtures or claim calibrated sound. Do not alter PB-013 or frozen gates. LC3-only: no LC3plus implementation, source, vectors or licensing assumptions. Preserve existing regression fixtures, frozen HIL/PCM limits and immutable evidence. No public release, Bluetooth qualification or unrelated PB-013 feature claim.

### Technical context
docs/testing/logic-analyzer-setup.md owns wiring and safety; src/audio_i2s.c owns actual output. Existing analyzer results explicitly distinguish clock/word geometry from content; current timing/rate/control observability needs refinement.
Research: docs/development/independent-firmware-validation-research-20261003.md

### Open questions
Actual word-decoder/capture identity; exposure and synchronization of public control inputs; sample alignment versus timeline discontinuities; tolerances, duration, limits and safe downstream load/mute.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Fresh PB-041-identified session/capture retains real pin/channel mapping, exact images, stimulus/reference/control identities and raw data hashes.
- [ ] #2 Real I2S words, complete sample counts and channel placement meet independently derived post-processing expectations, not captured-output golden regeneration.
- [ ] #3 Swap, missing/repeated words, flat output, truncation, wrong control history, stale/replayed capture and ambiguous alignment fail correct public-boundary checks.
- [ ] #4 Startup, stop/reconnect, reset and current supported CPU/offload/fallback transitions have correct observable output continuity/accounting within refined bounds.
- [ ] #5 Electrical safety and bounded capture/diagnostic cleanup are retained; result distinguishes digital content from presentation timing, DAC presence and analog quality.
<!-- AC:END -->
