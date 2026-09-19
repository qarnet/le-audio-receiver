---
id: PB-002
title: Use perceptual fixed-point volume mapping
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:M'
  - 'area:audio'
dependencies: []
priority: p2
type: feature
ordinal: 2000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Current linear PCM volume scaling does not provide a perceptually even user
volume sweep.

### Desired outcome

Use a justified fixed-point mapping that remains monotonic, has exact endpoints,
and cannot overflow audio samples.

### Scope

- Define and test fixed-point gain mapping and saturation behavior.
- Validate curve semantics against VCP and collect listening evidence on a
  production receiver and DAC scope selected during refinement.

### Non-goals

- Preserve a stale mandatory both-board release obligation.
- Assume every DAC and receiver combination is required before refinement.

### Technical context

Migrated from PLANNED_FEATURES.md item B. docs/known-limitations.md section 3
describes observed behavior; src/audio_volume.c implements current mapping.

### Open questions

Curve choice, full-scale threshold, and selected hardware matrix scope.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Mapping is monotonic with exact mute and exact full scale.
- [ ] #2 Mapping cannot overflow scaled PCM samples.
- [ ] #3 Curve choice is justified against VCP semantics.
- [ ] #4 Listening evidence covers supported production receiver and DAC scope selected during refinement.
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
