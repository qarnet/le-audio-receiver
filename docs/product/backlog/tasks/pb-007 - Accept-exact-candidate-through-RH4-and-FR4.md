---
id: PB-007
title: Accept exact candidate through RH4 and FR4
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
updated_date: '2026-09-20 19:25'
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

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Active candidate handoff for future RH4 and FR4: GitHub draft release ID `391991202`, trusted-main run `35429538264` attempt `1`, tag label `v0.1.0`, title `LE Audio Receiver v0.1.0`, and exact target SHA `b59e1d8f99b8f4e7435c7086bfe81700007b221d`. Candidate is draft `true`, prerelease `false`, `published_at: null`, private, unpublished, and untagged; no `refs/tags/v0.1.0` exists.

Exact active assets:
- `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip`, 627096 bytes, SHA-256 `bd5fe73636b831e9b685cd20f53704fbe342be60b124f5ae5379dd7969ac9f10`.
- `SHA256SUMS`, 117 bytes, SHA-256 `ea653102fc318d22e0b4b5d3c7b08a05874aa435b73098ea5f65790a913398ff`.
- `release-provenance.json`, 1077 bytes, SHA-256 `49ca660cc83e99a12e07982f466d5d44f64730e708f162a03582dc60fe157238`.

Provenance binds version `0.1.0`, NCS `v3.3.0`, run `35429538264` attempt `1`, exact SHA `b59e1d8f99b8f4e7435c7086bfe81700007b221d`, and the nRF54L15-only factory ZIP. No RH4 or FR4 acceptance has run. PB-007 remains Backlog with all AC unchecked and dependency PB-006 retained; it is not Ready because this size L item has an empty plan and unresolved FR4 refinement note.
<!-- SECTION:NOTES:END -->
