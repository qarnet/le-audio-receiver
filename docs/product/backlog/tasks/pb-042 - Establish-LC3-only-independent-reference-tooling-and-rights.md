---
id: PB-042
title: Establish LC3-only independent reference tooling and rights
status: Backlog
assignee: []
created_date: '2026-10-03 02:33'
labels:
  - 'size:M'
  - 'area:testing'
  - 'area:audio'
dependencies: []
references:
  - docs/development/independent-firmware-validation-research-20261003.md
priority: p2
type: research
ordinal: 39000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
Independent Bluetooth LC3 reference execution and lawful handling of tools, audio and derived artifacts are not provisioned. Local package v1.0.8 contains Win32 tooling; current SIG page advertises v1.0.10. Production liblc3 licensing does not authorize those materials.

### Desired outcome
An evidence-backed approved reference/version/rights/execution contract permits successor work without assuming a Win32 solution or treating LC3plus as LC3.

### Scope
Review actual Bluetooth LC3 tool/package identities, applicable specification/errata/TS/ICS/TCRL versions, EULA and corpus/output terms. Evaluate the owner's Win32 execution ideas before selecting an isolated execution and retention model. Define an explicit go/no-go and reproducibility check.

### Non-goals
Do not choose Wine, Windows or CI topology before refinement, accept legal terms on the owner's behalf, run unapproved downloads/installers, or copy restricted reference material into the repository. LC3-only: no LC3plus implementation, source, vectors or licensing assumptions. Preserve existing regression fixtures, frozen HIL/PCM limits and immutable evidence. No public release, Bluetooth qualification or unrelated PB-013 feature claim.

### Technical context
opencode.json exposes the read-only Bluetooth library. Its README.md, ASSESSMENT.md and LC3_Reference_Binary/Readme.txt distinguish package/script/binary versions and container format. Existing docs/development/audio-validation-handoff-20260925.md supplies prior technical context.
Research: docs/development/independent-firmware-validation-research-20261003.md

### Open questions
Owner's preferred Win32 arrangement; exact package/platform and normative versions; authorized purposes and derived-artifact redistribution; private/local/hosted execution and retention. Unresolved rights do not authorize successor implementation.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Retained source/version matrix identifies the selected Bluetooth LC3 package, binary/script hashes, applicable specification/errata and TS/ICS/TCRL; local v1.0.8 is not silently labeled latest.
- [ ] #2 Rights decision names tool, audio and derived-output restrictions and the responsible approval; unresolved or prohibited uses produce explicit no-go rather than inferred permission.
- [ ] #3 Execution recommendation incorporates the owner's Win32 ideas and states isolation, provenance, platform repeatability and retention requirements without choosing an unreviewed installer path.
- [ ] #4 After required approval, repeated reference execution on an authored deterministic sample retains input/configuration/tool/output identities and demonstrates repeatability; if approval is unavailable, the missing prerequisite and successor no-go are recorded.
- [ ] #5 No LC3plus tool/code/vector or interchangeable-license assumption forms part of the selected oracle.
<!-- AC:END -->
