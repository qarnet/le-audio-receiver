---
id: PB-047
title: Prove fresh exact LC3 SDU content and per-stream delivery
status: Blocked
assignee: []
created_date: '2026-10-03 02:33'
updated_date: '2026-10-04 05:00'
labels:
  - 'size:L'
  - 'area:testing'
  - 'area:hil'
  - 'area:bluetooth'
dependencies:
  - PB-043
references:
  - docs/development/independent-firmware-validation-research-20261003.md
priority: p2
type: tech-debt
ordinal: 44000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
Aggregate valid/PLC/error counters do not establish exact transmitted versus received encoded bytes or reject stale session evidence.

### Desired outcome
Fresh session-bound valid LC3 delivery has an exact intended/received payload and sequence ledger with explicit loss/fault accounting.

### Scope
Use independently validated S2 content at source and real receive observation boundaries for mono/Mode A/Mode B. Bind immutable images/session/stimulus identity, sequence and content. Design freshness without inserting incompatible headers into LC3 payloads. Distinguish accepted/enqueued sends from receiver delivery.

### Non-goals
Do not infer on-air success from sent callbacks, require lossless RF by silently changing frozen gates, duplicate PB-041 pin identity, or treat byte delivery as decoded/analog/presentation proof. LC3-only: no LC3plus implementation, source, vectors or licensing assumptions. Preserve existing regression fixtures, frozen HIL/PCM limits and immutable evidence. No public release, Bluetooth qualification or unrelated PB-013 feature claim.

### Technical context
hil/source/core/hil_source_signal.c, hil/source/app/src/hil_source_app.c, src/audio_stream_session.c, scripts/hil/receiver.py and scripts/hil/session.py. Existing BSim transport hashes are useful software witnesses, not independent new physical payload identity.
Research: docs/development/independent-firmware-validation-research-20261003.md

### Open questions
Receive observation/retention design; byte/hash trust and collision policy; stimulus/session freshness challenge; valid metadata placement; handling declared loss, duplicates/reorder and bounded diagnostics overhead.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Fresh intended and received valid SDU evidence matches exact content, stream/session identity and declared ordering for mono/Mode A/Mode B, with immutable image/source/oracle provenance.
- [ ] #2 Loss/fault events are explicitly accounted for; source acceptance/completion is not substituted for peer delivery, and existing frozen transport gates remain unchanged.
- [ ] #3 Deliberate omission, duplication, reorder, corruption, wrong stream/session and stale/replayed evidence fail the correct content/accounting boundary.
- [ ] #4 Freshness mechanism preserves valid LC3 SDU formatting and bounded lifecycle/resource ownership; malformed diagnostics and timeout/cancel/restart fail safely.
- [ ] #5 Acceptance includes fresh physical peer evidence, not only synthetic logs or simulator traces, and makes no decoded PCM, timing or analog claim.
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-10-04 scope recovery: PB-047 waits for PB-043 independent reference content and fresh physical prerequisites before per-stream peer delivery acceptance. No simulator or send-completion substitution and no skip waiver.
<!-- SECTION:NOTES:END -->
