---
id: PB-052
title: Enforce complete execution of external Bluetooth test results
status: Backlog
assignee: []
created_date: '2026-10-03 04:40'
labels:
  - 'size:M'
  - 'area:testing'
  - 'area:ci'
dependencies: []
references:
  - docs/development/bluetooth-testing-repository-research-20261003.md
priority: p2
type: tech-debt
ordinal: 49000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
Inspected upstream wrappers can report success with Not Run, skipped or empty selected cases, and optional environment readiness can hide missing execution.
### Desired outcome
A strict public result-accounting boundary rejects incomplete external Bluetooth runs and reports exact required executed/failed/skipped case coverage.
### Scope
Validate declarative required-case inventory against actual BlueZ tester summary and pytest JUnit output, child exit status and prerequisite results. Explicitly label declared non-required exclusions; fail missing/duplicate/malformed/zero-execution or unexpected skip evidence. Exercise the real CLI with encoded reports and retain raw input/output provenance. Do not deploy privileged upstream actions.
### Non-goals
No arbitrary skipped-case waiver, fabricated hosted/firmware acceptance, GPL source copying, replacement of canonical gate ownership, or unrelated application features.
### Technical context
scripts/test_inventory.py and scripts/test-all.sh own current gate discovery/accounting. The survey records action-ci TestRunner and TestFunctional gaps plus pytest-bluezenv result handling. A separately owned adapter can validate real report formats without importing upstream implementation.
### Open questions
Versioned report/required-inventory schema and CI extension points need refinement; no external suite is made optional to obtain a pass.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Public CLI accepts complete conforming tester-summary and JUnit runs only when all required cases and successful child/prerequisite outcomes are proven.
- [ ] #2 Zero execution, Not Run/unexpected skips, missing/duplicate cases, malformed counts/XML, child failure and required inventory drift fail with actionable reasons.
- [ ] #3 Declared non-required exclusions remain separate from required execution and never make an unexecuted mandatory test accepted.
- [ ] #4 Process-boundary tests use real encoded report inputs and retained provenance; existing canonical case discovery, warning policy and aggregate gates are preserved.
<!-- AC:END -->
