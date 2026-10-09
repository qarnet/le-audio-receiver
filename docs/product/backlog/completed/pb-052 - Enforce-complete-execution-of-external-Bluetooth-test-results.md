---
id: PB-052
title: Enforce complete execution of external Bluetooth test results
status: Done
assignee: []
created_date: '2026-10-03 04:40'
updated_date: '2026-10-03 18:00'
labels:
  - 'size:M'
  - 'area:testing'
  - 'area:ci'
dependencies: []
references:
  - docs/development/bluetooth-testing-repository-research-20261003.md
  - docs/testing/external-test-result-accounting.md
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
### Refined contract
Schema v1 is specified in docs/testing/external-test-result-accounting.md: independently trusted raw inventory digest, exact required/excluded case partition, discovered universe, caller child/prerequisite outcomes and same-byte report hash/parse. Supported profiles are pinned BlueZ tester text and pytest 8.4.2 native xunit2. Exclusions are unselected and absent, not skip waivers. The parser checks reported evidence consistency; PB-053 owns authentic isolated execution and sealing. Existing canonical discovery automatically picks up the new process-boundary test child; no external suite is made optional to obtain a pass.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Public CLI accepts complete conforming tester-summary and JUnit runs only when all required cases and successful child/prerequisite outcomes are proven.
- [x] #2 Zero execution, Not Run/unexpected skips, missing/duplicate cases, malformed counts/XML, child failure and required inventory drift fail with actionable reasons.
- [x] #3 Declared non-required exclusions remain separate from required execution and never make an unexecuted mandatory test accepted.
- [x] #4 Process-boundary tests use real encoded report inputs and retained provenance; existing canonical case discovery, warning policy and aggregate gates are preserved.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Implement stdlib public result-accounting CLI and versioned contract for independently anchored inventory, caller run record and same-bytes report hash/parse. Keep accounting trust boundary explicit. 2. Add process-boundary tests using real report files for both profiles, actual pytest emitted JUnit where dependency is available as required test prerequisite, complete pass cases plus malformed/count/identity/hash/skip/failure/child/prerequisite/lifecycle controls. Preserve canonical discovery and frozen gates. 3. Run focused tests, matrix/documentation/backlog checks and code review; repair ordinary defects. Commit candidate with item still open and include requested persistent scope/Windows hold docs, excluding PB-013/private data. 4. Run clean detached candidate full canonical gate and hostedCI on existingPR16. Record evidence and Done transition only after checks pass; verify final head and continue PB051/PB046/PB053 without per-push reports.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Refinement frozen: schema-v1 accounting CLI accepts pinned BlueZ tester summary (4dc15be8ee3f7422d447087f1893d215575cb2c8) and pytest8.4.2 xunit2 reports. Required IDs are exact structured string tuples; reviewed universe partitions into nonempty required cases plus explicitly excluded/unselected cases with reasons. Excluded cases must be absent from execution report, not skipped. Caller supplies independently trusted inventory digest; run record binds inventory, producer pin, discovered universe, normal child exit0, exact prerequisite identities/exit outcomes and raw report size/SHA256. Reject missing/duplicate/unknown IDs, failed/NotRun/skipped cases, inconsistent summaries, malformed or unsafe XML/JSON, bad hashes/status and inventory shrink. Pytest profile requires --runxfail in recorded argv because non-strict XPASS cannot be distinguished from pass in JUnit alone. Native pytest single testsuite wrapper supported; arbitrary nested/merged dialects fail closed. Parser validates consistent reported evidence, not authentic execution or freshness; PB-053 owns actual execution/isolation and evidence sealing. No external-suite inventory or Bluetooth acceptance fabricated. Smallest verification: real CLI processes encoded reports and actual pytest emission, changing any required-case/count/child/prerequisite/hash input fails with reason. Source evidence: BlueZ src/shared/tester.c lines56-58,146-168,371-424; pytest junitxml.py8.4.2 lines116-152,190-250,639-675. Technical defaults resolved autonomously; existing acceptance text unchanged.

Implemented public stdlib accounting CLI and schema contract; eight focused process-boundary methods passed, including actual pytest8.4.2 emitted reports and honest XPASS argument-terminator bypass negative/positive. Read-only review exposed and verified repair of --runxfail-as-path false acceptance and non-specific XML controls; follow-up found no new substantive issue. Exact report/input hashes and rejection reasons are returned; accounting trust remains caller-owned, not execution authenticity/freshness. New Python child discovered automatically; matrix0errors and diffcheck pass. Results/proof boundaries: docs/development/pb-052-external-result-accounting-results-20261003.md. Clean canonical and hostedCI acceptance pending; keep In Progress.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Implemented fail-closed public result-accounting CLI for pinned BlueZ tester and pytest8.4.2 native xunit2 reports, independently anchored required-case inventory, exact discovered/required/excluded partition, child/prerequisite outcomes and same-bytes report size/SHA256 parsing. Exclusions are unselected/absent, never skip waivers. Eight process-boundary test methods include actual pytest pass/failure/skip/xfail/teardown/double-failure/collection/empty reports, missing/duplicate/count/hash/child/prerequisite/schema controls and valid unsafe XML. Review exposed honest XPASS acceptance through positional --runxfail path; explicit launcher/effective-option validation repaired it and real negative/positive tests prove regression rejection. CLI validates consistent reported evidence, not authenticity/freshness; PB-053 owns real execution, isolation, discovery and sealing. Contract: docs/testing/external-test-result-accounting.md. Clean candidate08b85230e0d53281a0316e8d2f9a7c351c4a8f04 passed canonical83/0/83, matrix0errors/notes, unchanged coverage baseline and BSim17/26. Hosted run37140245411 passed unit,coverage,BSim,tests,firmware;release skipped. Production code, board configs,toolchain pins/workflow unchanged. Full evidence: docs/development/pb-052-external-result-accounting-results-20261003.md. Done through existing PR16; human merge official acceptance. No external Bluetooth/firmware/codec/physical or release acceptance claimed.
<!-- SECTION:FINAL_SUMMARY:END -->
