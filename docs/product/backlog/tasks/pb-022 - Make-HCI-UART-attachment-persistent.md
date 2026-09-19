---
id: PB-022
title: Make HCI UART attachment persistent
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:S'
  - 'area:hil'
dependencies: []
priority: p2
type: tech-debt
ordinal: 22000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

HCI UART attachment through btattach does not persist across reboot or USB
re-enumeration.

### Desired outcome

Selected udev or systemd mechanism restores attachment and required btmgmt
settings with visible failure behavior.

### Scope

- Select udev or systemd ownership and supported distribution scope.
- Restore attachment and required btmgmt settings after reboot and USB
  re-enumeration.
- Retain manual workflow.

### Non-goals

- Remove manual attachment workflow.

### Technical context

See STATUS.md btattach not persistent entry and dongle/README.md.

### Open questions

Supported distributions and owner.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Selected udev or systemd mechanism restores attachment after reboot and USB re-enumeration.
- [ ] #2 Required btmgmt settings are restored.
- [ ] #3 Failure is visible and manual workflow remains available.
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
