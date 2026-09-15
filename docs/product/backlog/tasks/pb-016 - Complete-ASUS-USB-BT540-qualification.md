---
id: PB-016
title: Complete ASUS USB-BT540 qualification
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:M'
  - 'area:interoperability'
dependencies: []
priority: p2
type: research
ordinal: 16000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

ASUS USB-BT540 remains candidate because desktop UI, mono, cold replug,
audibility, codec warning, and PLC questions remain unresolved.

### Desired outcome

Complete dynamic evaluation record or retain candidate verdict with explicit
remaining failure.

### Scope

- Test desktop KDE and Bluedevil route, one-CIS mono, and cold unplug and
  replug repeat.
- Obtain audible observation.
- Resolve or explain codec-capability -22 warning and PLC.
- Run full dynamic checklist.

### Non-goals

- Promote candidate before every acceptance gate passes.

### Technical context

See docs/bluetooth-adapter-evaluation.md ASUS USB-BT540 section and evaluation
record, plus docs/supported-sources.md hardware matrix row.

### Open questions

Cause and disposition of codec-capability -22 warning and PLC.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Desktop KDE and Bluedevil route passes.
- [ ] #2 One-CIS mono and cold unplug and replug repeat pass.
- [ ] #3 Audible observation is recorded.
- [ ] #4 Codec-capability -22 warning and PLC are resolved or explained.
- [ ] #5 Full dynamic checklist is clean and candidate status remains until all gates pass.
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
