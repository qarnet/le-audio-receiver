---
id: PB-007
title: Accept exact candidate through RH4 and FR4
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:L'
  - 'area:hil'
dependencies:
  - PB-006
priority: p1
type: feature
ordinal: 7000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Release candidate needs exact-artifact validation before it can progress toward
publication.

### Desired outcome

Exact nRF54L15 receiver ZIP and deterministic HIL source ZIP pass strict
validation and full 20-child two-pass matrix.

### Scope

- Validate archive contracts before hardware activity.
- Use identical exact bytes for every matrix child.
- Verify cpuapp and FLPR flashing, frozen transport/runtime/warning/evidence
  gates, and retained exact evidence.
- Record RH4 and FR4 acceptance.

### Non-goals

- Publication.
- Analog, stereo, or audibility claims.

### Technical context

See docs/development/system-hil-milestones.md RH4 and release line,
docs/development/system-hil-rh4-artifact-handoff.md, and
docs/development/firmware-release-plan.md FR4.

### Open questions

Any FR4 evidence beyond RH4 must be explicit during refinement.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Archive contracts are validated before hardware work begins.
- [ ] #2 Every matrix child uses same exact receiver and source bytes.
- [ ] #3 All 20 children pass frozen transport, runtime, warning, and evidence gates across two passes.
- [ ] #4 cpuapp and FLPR flashing are verified.
- [ ] #5 Exact evidence is retained and RH4 and FR4 acceptance is recorded.
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
