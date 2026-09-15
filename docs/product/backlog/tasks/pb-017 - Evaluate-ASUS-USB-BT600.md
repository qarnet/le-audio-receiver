---
id: PB-017
title: Evaluate ASUS USB-BT600
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:M'
  - 'area:interoperability'
dependencies: []
priority: p3
type: research
ordinal: 17000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

ASUS USB-BT600 identity, controller capability, and receiver dynamic behavior
are unrecorded.

### Desired outcome

Record evidence-backed adapter verdict.

### Scope

- Identify chipset, VID:PID, firmware, and driver.
- Prove cis-central, 2M, and ISO buffer capability.
- Run fresh pair, mono, Mode A, reconnect, and cold-boot sequence with clean
  logs.

### Non-goals

- Make support claim before hardware evaluation.

### Technical context

See docs/bluetooth-adapter-evaluation.md ASUS USB-BT600 section and
docs/supported-sources.md hardware matrix row.

### Open questions

Chipset, VID:PID, firmware, driver, and dynamic verdict.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Chipset, VID:PID, firmware, and driver are identified.
- [ ] #2 cis-central, 2M, and ISO buffer capability is proven.
- [ ] #3 Full fresh-pair mono, Mode A, reconnect, and cold-boot sequence runs with retained clean logs and verdict.
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
