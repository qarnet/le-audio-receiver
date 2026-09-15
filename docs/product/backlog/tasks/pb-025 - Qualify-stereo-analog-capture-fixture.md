---
id: PB-025
title: Qualify stereo analog capture fixture
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:L'
  - 'area:hardware'
dependencies: []
priority: p3
type: feature
ordinal: 25000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

No qualified simultaneous stereo capture path exists for receiver analog output
acceptance.

### Desired outcome

Qualified stereo fixture freezes identity, measurement metadata, thresholds,
and defect sensitivity.

### Scope

- Acquire and electrically qualify simultaneous stereo capture path.
- Freeze identity, pad, mismatch, ALSA metadata, and thresholds.
- Verify swap, loss, duplication, leakage, clipping, truncation, drift, and
  dropout defects fail correct metric.
- Retain two clean 130 s captures.

### Non-goals

- Treat purchase alone as acceptance.
- Claim calibrated fidelity.

### Technical context

See docs/development/system-hil-milestones.md SA0 and future stereo feedback
extension.

### Open questions

Capture hardware selection and frozen qualification thresholds.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Simultaneous stereo capture path is electrically qualified.
- [ ] #2 Identity, pad, mismatch, ALSA metadata, and thresholds are frozen.
- [ ] #3 Deliberate swap, loss, duplication, leakage, clipping, truncation, drift, and dropout defects fail correct metric.
- [ ] #4 Two clean 130 s captures pass.
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
