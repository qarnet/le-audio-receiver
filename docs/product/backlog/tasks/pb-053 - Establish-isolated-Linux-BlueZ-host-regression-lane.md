---
id: PB-053
title: Establish isolated Linux BlueZ host regression lane
status: Backlog
assignee: []
created_date: '2026-10-03 04:40'
labels:
  - 'size:M'
  - 'area:testing'
  - 'area:ci'
  - 'area:interoperability'
dependencies: []
references:
  - docs/development/bluetooth-testing-repository-research-20261003.md
priority: p2
type: research
ordinal: 50000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
Linux/BlueZ failures and firmware failures need separate reproducible boundaries; pytest-bluezenv offers useful isolation but is Alpha and generic controller passthrough does not match our session-bound UART H4 fixture.
### Desired outcome
A pinned, isolated host-stack regression contract and bounded real execution evidence establish what a Linux BlueZ lane proves and which prerequisites it requires.
### Scope
Evaluate available QEMU/controller/kernel/BlueZ tooling and rights, exact version/config pins, private daemon/bus/state, explicit fresh versus retained state, diagnostics, bounded cancellation and immutable output. Prove selected discovery/pairing/endpoint/ISO lifecycle behavior with required-case accounting. Keep virtual and physical firmware verdicts distinct.
### Non-goals
No auto-selection or mutation of workstation adapters, unsafe root bus/RPC exposure, bypass of nRF DP/AP/FICR identity, LC3plus or PCM oracle claim, and no silently installing/upgrading host kernel/controller drivers.
### Technical context
scripts/hci_dongle.py, scripts/hil/session.py and scripts/hil/evidence.py retain current ownership. The survey records pytest-bluezenv trusted pickle/root-console boundaries, BlueZ test-functional/test-runner mechanisms and kernel ISO prerequisites.
### Open questions
Available virtualization/kernel/test binaries, compatible exact revisions, approved separate host tooling license/distribution and VM execution model require evidence during refinement.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Selected exact kernel/config/BlueZ/emulator/harness/rights contract identifies available prerequisites and fails readiness explicitly for unavailable required capabilities.
- [ ] #2 Bounded real isolated host tests retain encoded public behavior, raw logs and version/case provenance without changing unrelated workstation adapters.
- [ ] #3 Fresh/retained state, restart, timeout/cancellation and ownership cleanup are verified through public outcomes and preserve immutable previous evidence.
- [ ] #4 Required-case accounting rejects incomplete runs, and reports distinguish host simulation from physical nRF RF, codec, I2S and analog acceptance.
<!-- AC:END -->
