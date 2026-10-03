---
id: PB-046
title: Validate closed-loop clock recovery with independent timed output
status: Backlog
assignee: []
created_date: '2026-10-03 02:33'
labels:
  - 'size:L'
  - 'area:testing'
  - 'area:clock-recovery'
  - 'area:audio'
dependencies:
  - PB-045
references:
  - docs/development/independent-firmware-validation-research-20261003.md
priority: p2
type: tech-debt
ordinal: 43000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
Open-loop drift tests and manually completed fake DMA do not prove stable feedback under independent source/output clocks; a reservoir can conceal rate imbalance.

### Desired outcome
Real drift controller, ASRC and production sink integration demonstrate bounded queue/rate behavior in a deterministic timed model with observable failure sensitivity.

### Scope
Use production tuning, independent virtual source/controller/PCLK clocks, a finite frame/time queue and clock-driven descriptor completion. Separate skew, arrival jitter, packet loss, processing delay and presentation phase. Progress from public drift/ASRC boundaries to full clock-driven fake I2S output and supported offload transitions.

### Non-goals
Do not replace the production controller with a test redesign, use block count alone as latency, silently change frozen limits, claim RF/analog/presentation acceptance from a model, or extend PB-013. LC3-only: no LC3plus implementation, source, vectors or licensing assumptions. Preserve existing regression fixtures, frozen HIL/PCM limits and immutable evidence. No public release, Bluetooth qualification or unrelated PB-013 feature claim.

### Technical context
src/audio_drift.c, src/audio_asrc.c, src/audio_rate_convert.c and src/audio_i2s.c; boards/nrf54l15dk_nrf54l15_cpuapp.conf owns production tuning. Existing audio_i2s_common fake driver snapshots/completion behavior need a separately refined timed public boundary.
Research: docs/development/independent-firmware-validation-research-20261003.md

### Open questions
Supported disturbances and durations; settling/occupancy/rate/underrun envelopes; clock model and measured feedforward consistency; full fake-DMA ownership/stream lifecycle and target parity.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Real controller output drives real conversion and timed queue occupancy, which feeds the next update under production tuning and declared independent clock relationships.
- [ ] #2 Refined skew/jitter/loss/processing-delay cases satisfy reviewed finite-queue and output-rate limits without relying on startup reservoir or final occupancy alone.
- [ ] #3 The same declared nonzero-skew case fails its intended rate/queue criterion when correction is disabled, proving feedback is causally necessary.
- [ ] #4 Clock-driven sink completion checks descriptor ownership and all output samples/counts across startup, stop, reconnect, reset and supported CPU/offload transitions.
- [ ] #5 Results retain model/input/tuning identities, event ledger and violations, and distinguish software-model stability from physical timing and presentation claims.
<!-- AC:END -->
