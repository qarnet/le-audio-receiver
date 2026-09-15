---
id: PB-023
title: Qualify mono analog capture fixture
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:M'
  - 'area:hardware'
dependencies: []
priority: p2
type: feature
ordinal: 23000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Existing mono USB capture adapter cannot be connected to DAC output until
passive summing, attenuation, and DC-blocking fixture is electrically reviewed
and qualified.

### Desired outcome

Qualified mono aggregate capture fixture has measured electrical behavior and
frozen capture and oracle metadata.

### Scope

- Keep fixture disconnected before electrical review.
- Measure passive sum, attenuation, and DC block.
- Freeze 0d8c:0014 ALSA, mixer, gain, and AGC metadata and approved thresholds.
- Capture two clean 130 s runs and prove synthetic defects fail correctly.

### Non-goals

- Stereo or fidelity claim.

### Technical context

See docs/development/system-hil-milestones.md MA0 and mono adapter extension,
plus capture software status in STATUS.md.

### Open questions

Electrical review outcome and frozen approved thresholds.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Electrical review and measured passive sum, attenuation, and DC block are retained.
- [ ] #2 Exact 0d8c:0014 ALSA, mixer, gain, and AGC metadata is frozen.
- [ ] #3 Two clean 130 s captures pass frozen approved thresholds.
- [ ] #4 Synthetic defects fail correct metric.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
<!-- SECTION:FINAL_SUMMARY:END -->
