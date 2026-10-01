---
id: PB-018
title: Validate nRF5340 DK as Linux HCI UART adapter
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
updated_date: '2026-09-30 23:09'
labels:
  - 'size:M'
  - 'area:interoperability'
dependencies: []
priority: p3
type: research
ordinal: 18000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

nRF5340 DK HCI UART source-controller route exists but lacks full dynamic
adapter acceptance evidence.

### Desired outcome

Record evidence-backed verdict for nRF5340 DK as Linux HCI UART adapter.

### Scope

- Retain exact H4 setup and identity evidence.
- Run full dynamic adapter checklist or record blockers.

### Non-goals

- Claim USB HCI ISO support.
- Remove or clean up nRF5340 receiver path.

### Technical context

See docs/bluetooth-adapter-evaluation.md Nordic nRF5340 DK HCI UART section,
docs/supported-sources.md hardware matrix row, and dongle/.

### Open questions

Dynamic acceptance verdict and any recorded blockers.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Exact H4 setup and identity evidence are retained.
- [ ] #2 Full dynamic adapter checklist passes or blockers are recorded.
- [ ] #3 No USB HCI ISO claim or receiver cleanup or removal is introduced.
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-10-01 disposition: SUPERSEDED by PB-019 and PB-039 under approved all-nRF54L15 migration. Current dongle is XIAO nRF54L15 only; old nRF5340 DK route is not an active build, fixture, test commitment or support recommendation. Clean b21c7a7 XIAO HCI six-case replacement proof is retained under /tmp/opencode/nrf54-b21c7a7-hci-clean-20261001-r1. Keep old proposal and unchecked DK criteria as historical research, not a claimed qualification. Marked superseded rather than archived or Done; no nRF5340 acceptance invented.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Superseded, not implemented or accepted: PB-019 supplies the stock-bridge XIAO Linux HCI role; PB-039 retires the prior nRF5340 infrastructure. Historical external SDK facts remain available, but this item creates no current nRF5340 implementation or lab testing obligation. No dynamic nRF5340 DK qualification or USB HCI ISO claim.
<!-- SECTION:FINAL_SUMMARY:END -->
