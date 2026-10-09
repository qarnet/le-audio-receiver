---
id: PB-044
title: Validate production LC3 decode against independent reference PCM
status: Blocked
assignee: []
created_date: '2026-10-03 02:33'
updated_date: '2026-10-04 05:00'
labels:
  - 'size:M'
  - 'area:testing'
  - 'area:audio'
dependencies:
  - PB-043
references:
  - docs/development/independent-firmware-validation-research-20261003.md
priority: p2
type: tech-debt
ordinal: 41000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
Real decoder/session tests predominantly compare against same-library PCM anchors, which cannot independently expose every shared codec defect.

### Desired outcome
Production decoder output meets independently established Bluetooth LC3 comparison criteria with correct geometry, channel routing and state history.

### Scope
Feed S2 frames through real public decoder/session boundaries before volume or ASRC. Start both initial 48 kHz durations, then distinct L/R mono/Mode A/Mode B routing, resets/reconnect and stateful loss/recovery. Separate numerical decode comparison from PLC invariants and host versus actual-target evidence.

### Non-goals
Do not compare lossy output byte-for-byte with original PCM, import the project's 2048/512/32750 portability bounds as official conformance, require another implementation's identical PLC waveform, or claim full conformance from a small corpus. LC3-only: no LC3plus implementation, source, vectors or licensing assumptions. Preserve existing regression fixtures, frozen HIL/PCM limits and immutable evidence. No public release, Bluetooth qualification or unrelated PB-013 feature claim.

### Technical context
src/audio_decode.c, src/audio_stream_session.c, tests/unit/decode and tests/unit/audio_stream_session expose real processing; tests/support/pcm_oracle.c owns existing regression metrics. Official reference procedure and stateful manifests require correct alignment.
Research: docs/development/independent-firmware-validation-research-20261003.md

### Open questions
Selected applicable comparison normalization/alignment and tolerances; target execution/reporting mechanism; precise admitted profile coverage; normative versus implementation-specific PLC assertions.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Both initial independent streams pass the real public production decoder boundary with exact output frame/sample counts and retained reference/DUT/configuration provenance.
- [ ] #2 Distinct-channel mono/Mode A/Mode B checks detect swap, duplication and contamination independently of the numerical codec metric.
- [ ] #3 Applicable reference comparison uses reviewed alignment, delay/padding and normalization criteria; it does not obtain a pass by automatic resynchronization or threshold relaxation.
- [ ] #4 Corrupt/truncated content, wrong channel or state history and incorrect reset/reconnect behavior fail the intended public-boundary checks, with successful valid recovery demonstrated.
- [ ] #5 Loss cases prove output geometry, channel isolation and recovery without assuming universal bit-identical PLC; native and actual target evidence are labeled separately.
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-10-04 scope recovery: PB-044 waits for PB-043 independent vectors and strict frame adapter before production decoder comparison. No independent PCM acceptance or skip waiver claimed.
<!-- SECTION:NOTES:END -->
