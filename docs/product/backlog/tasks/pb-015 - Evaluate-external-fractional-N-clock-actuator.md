---
id: PB-015
title: Evaluate external fractional-N clock actuator
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:L'
  - 'area:clock-recovery'
dependencies: []
priority: p3
type: research
ordinal: 15000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Current boards have no approved external fractional-N actuator path.

### Desired outcome

If custom PCB is pursued, establish architecture for external clock actuation
behind current ppm interface.

### Scope

- Evaluate oscillator, I2S slave mode, I2C actuator, hardware evidence, and
  failure behavior only if custom PCB work is approved.

### Non-goals

- Promise implementation on current boards.
- Commit to custom PCB before need is decided.

### Technical context

See docs/design.md design backlog and docs/development/refactor-plan.md Deferred
feature list.

### Open questions

Whether custom PCB is pursued and whether external actuator is needed.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 If custom PCB is pursued, architecture covers oscillator, I2S slave mode, I2C actuator behind current ppm interface, hardware evidence, and failure behavior.
- [ ] #2 No current-board implementation promise is made.
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
