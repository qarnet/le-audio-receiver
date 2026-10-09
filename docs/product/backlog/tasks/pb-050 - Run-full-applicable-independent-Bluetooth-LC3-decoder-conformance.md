---
id: PB-050
title: Run full applicable independent Bluetooth LC3 decoder conformance
status: Blocked
assignee: []
created_date: '2026-10-03 02:33'
updated_date: '2026-10-04 05:00'
labels:
  - 'size:L'
  - 'area:testing'
  - 'area:audio'
  - 'area:ci'
dependencies:
  - PB-044
references:
  - docs/development/independent-firmware-validation-research-20261003.md
priority: p2
type: tech-debt
ordinal: 47000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
A small independently validated corpus or upstream liblc3 report cannot establish complete applicable conformance for this compiled decoder/wrapper.

### Desired outcome
An explicitly provisioned, version-aligned LC3-only conformance lane retains complete applicable procedure results and precise host/target/product claim boundaries.

### Scope
Use S1 approved tooling/rights and S3 DUT adapter. Map current claimed decoder capabilities to applicable normative test points/procedures, provision approved corpus and comparator, run complete selected official checks and retain commands/results/inputs/toolchain identities.

### Non-goals
Do not infer product Bluetooth qualification from codec testing, certify producer encoders as receiver capabilities, reuse LC3plus results, expand capability to satisfy another profile, or run unauthorized automatic downloads/public corpus distribution. LC3-only: no LC3plus implementation, source, vectors or licensing assumptions. Preserve existing regression fixtures, frozen HIL/PCM limits and immutable evidence. No public release, Bluetooth qualification or unrelated PB-013 feature claim.

### Technical context
src/bt_bap.c owns advertised capabilities; tests/fixtures/lc3 and S3 cover only bounded cases. Local TCRL worksheets enumerate points but are not full procedures; official SIG package/script and actual TS/ICS/TCRL selection must agree.
Research: docs/development/independent-firmware-validation-research-20261003.md

### Open questions
Exact applicability/version set, complete corpus and comparison metrics; reference execution/CI provisioning; actual-target evidence export versus host builds; legally permitted result/artifact distribution.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Applicability matrix binds actual current decoder claims to selected LC3-only specification/errata/TS/ICS/TCRL and reference-tool versions; unsupported points are not silently enabled.
- [ ] #2 Approved lane runs every applicable selected procedure with retained input, DUT/reference binary, toolchain, configuration, commands and complete pass/fail results.
- [ ] #3 Partial runs, omitted applicable cases, wrong versions, reference failures, invalid alignment and comparison failures cannot produce an accepted complete-conformance verdict.
- [ ] #4 DUT-native, actual-target and full product qualification claims are distinct; upstream reports or host-only results cannot stand in for unexecuted target evidence.
- [ ] #5 Existing fast regressions, thresholds and historical corpus remain intact, and reference/corpus handling follows approved rights and retention constraints.
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-10-04 scope recovery: PB-050 waits for PB-044 independent decoder validation and approved applicable decoder/corpus/tool lane. No complete conformance claim or skip waiver.
<!-- SECTION:NOTES:END -->
