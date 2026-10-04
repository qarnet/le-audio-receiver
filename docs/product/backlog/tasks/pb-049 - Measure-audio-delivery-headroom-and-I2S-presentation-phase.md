---
id: PB-049
title: Measure audio delivery headroom and I2S presentation phase
status: Blocked
assignee: []
created_date: '2026-10-03 02:33'
updated_date: '2026-10-04 05:00'
labels:
  - 'size:L'
  - 'area:testing'
  - 'area:hil'
  - 'area:clock-recovery'
dependencies:
  - PB-048
  - PB-046
references:
  - docs/development/independent-firmware-validation-research-20261003.md
priority: p2
type: research
ordinal: 46000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
Aggregate delivery and zero underruns do not prove per-event deadline headroom or that the correct sample is presented at intended media time.

### Desired outcome
An agreed clock/timestamp/uncertainty contract and fresh measurements establish separately labeled delivery and presentation bounds.

### Scope
Define source/controller/receiver/output timing relationships and an event ledger tied to S7 sample identity. Measure receive/decode/queue/deadline and wire presentation separately from average-rate stability. Evaluate optional clock mapping only when source evidence supports it and validate deliberate late/phase-offset controls.

### Non-goals
Do not access SDC-owned RADIO, equate HCI submit/completion timestamps with airtime, derive self-referential offsets as truth, redefine advertised presentation behavior without owner decision, or claim analog/end-to-end source ADC timing from I2S alone. LC3-only: no LC3plus implementation, source, vectors or licensing assumptions. Preserve existing regression fixtures, frozen HIL/PCM limits and immutable evidence. No public release, Bluetooth qualification or unrelated PB-013 feature claim.

### Technical context
src/audio_timing_nrf54.c, src/audio_i2s.c, scripts/hil/receiver.py and docs/development/audio-validation-handoff-20260925.md. Existing first ISO anchor supports clock measurements, not proven per-block presentation scheduling.
Research: docs/development/independent-firmware-validation-research-20261003.md

### Open questions
Chosen media reference, clock mapping/calibration and uncertainty budget; supported latency/jitter envelopes; event observability cost; desired presentation semantics versus current implementation.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Retained contract names every clock/reference point, mapping assumption, timestamp meaning and uncertainty source; unobservable relationships are labeled unknown.
- [ ] #2 Fresh sample-identified physical measurements retain a causal event ledger for delivery headroom and wire presentation, separately from average output rate.
- [ ] #3 Reviewed applicability and uncertainty bounds determine verdicts; late-data, injected phase-offset, stale-ledger and incorrect clock-map controls fail intended checks.
- [ ] #4 Source send acceptance, controller scheduling and receiver/wire observation are not conflated or used to prove their own mapping.
- [ ] #5 Any needed production presentation behavior change is surfaced for separate owner refinement rather than silently implemented by measurement research.
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-10-04 scope recovery: PB-049 waits for PB-048 fresh sample-identified I2S evidence; PB-046 is Done. No delivery-headroom or presentation-phase acceptance and no skip waiver.
<!-- SECTION:NOTES:END -->
