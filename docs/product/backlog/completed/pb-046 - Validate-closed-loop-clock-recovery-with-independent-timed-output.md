---
id: PB-046
title: Validate closed-loop clock recovery with independent timed output
status: Done
assignee: []
created_date: '2026-10-03 02:33'
updated_date: '2026-10-03 22:52'
labels:
  - 'size:L'
  - 'area:testing'
  - 'area:clock-recovery'
  - 'area:audio'
dependencies:
  - PB-045
references:
  - docs/development/independent-firmware-validation-research-20261003.md
  - docs/development/pb-046-clock-model-refinement-20261003.md
  - docs/development/pb-046-clock-loop-results-20261003.md
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

### Refined contract
docs/development/pb-046-clock-model-refinement-20261003.md fixes independent clock relationships, production tuning, declared disturbance envelope, horizon and queue/rate bounds. The additive native driver consumes live words against independently generated expectations, with matched correction-disabled controls, bounded remote recovery and stop/reopen ownership. Software-model acceptance is distinct from actual-target clock measurement, physical FLPR/IPC and presentation proof; no physical parity is inferred.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Real controller output drives real conversion and timed queue occupancy, which feeds the next update under production tuning and declared independent clock relationships.
- [x] #2 Refined skew/jitter/loss/processing-delay cases satisfy reviewed finite-queue and output-rate limits without relying on startup reservoir or final occupancy alone.
- [x] #3 The same declared nonzero-skew case fails its intended rate/queue criterion when correction is disabled, proving feedback is causally necessary.
- [x] #4 Clock-driven sink completion checks descriptor ownership and all output samples/counts across startup, stop, reconnect, reset and supported CPU/offload transitions.
- [x] #5 Results retain model/input/tuning identities, event ledger and violations, and distinguish software-model stability from physical timing and presentation claims.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Add independent clock-driven native I2S driver with exact fractional frame credit, finite descriptor ownership, full word capture, bounded backpressure and timer/stop cleanup; link real production sink/drift/ASRC/offload/processor with board tuning imported. 2. Add independently expressed global-coordinate expected samples/counts and timestamp/clock/event ledger. Verify baseline and declared skew/source/jitter/missed-input/delay/step cases over1000virtual seconds; complete settled trajectory and derived output-rate bounds, not final occupancy. 3. Add same-plant correction-disabled controls and public stop/reopen/reset/CPU360/remote480/fault/recovery continuity; fix uncovered real defects without weakening criteria, evaluate practical hardware boundaries before behavior-changing production repair. 4. Run focused tests, independent review, inventory/matrix/backlog/docs gates, clean candidate full canonical and latest hostedCI onPR16. Record all evidence and only PR-gated Done; continue remaining eligible selected work without individual push reports.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Refined contract in docs/development/pb-046-clock-model-refinement-20261003.md: real production tuning imported2000/150, independent source/reference/PCLK clocks, asynchronous clock-driven15-descriptor I2S owner, full waveform/count oracle through actual sink/offload/processor, initial0/±1000pclk and0/±100source ppm envelope, separated jitter/missed-input/processing-delay/step stimuli, 1000s horizon900s settle derived from phase authority and startup budget. Settled postpushdepth10..12 derives from16slabs/6free target/new submission plus descriptor quantization. Rate bounds derive from geometry/queued-word spread; no frozen acceptance limits changed. External public drift+ASRC diagnostic prototype supports feasibility and disabled-correction queue failure, explicitly not full sink/physical acceptance. Final driver enforces real15-entry cap including timed blocked writes; do not manually complete one DMA block per source push. Numeric/control choices now bounded and grounded; preserve original acceptance criteria.

Implemented clock-driven finite15-descriptor native I2S device with independent integer word-clock credit, real production controller/conversion/sink/offload/processor and imported board2000/150 tuning. Independent global-coordinate oracle validates submission and live timed RAM consumption; post-submission corruption negative proves consumed-word sensitivity. Long stability1000s/settle900s cases include isolated skew/jitter/missed-input/delay/step/fault stimuli and mixed360; exact same enabled/disabled plants prove causal necessity. Each fault validates public categories and bounded subsequent remote success; permanent peer failure cannot pass from CPU correctness. Atomic rate-step timestamp/credit, final cancellation accounting, stop-idle quiescence, supervised overlapping backpressure/stop and no-helper-reset reopen are covered; reopened authored data actually transfers. Reviews repaired falseacceptance and fixture issues; finalreview found no remaining substantive issue in single-CPU software scope. Complete focused6/0/6 run r4 passed; results docs named above. No production source, physicalIPC/FLPR/RF/codec/analog/presentation or full-clamp guarantee claim. Clean canonical and hostedCI pending, keep In Progress.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Implemented independent clock-driven native15-descriptor I2S plant coupled to real production drift/ASRC/sink/rate conversion/NONE actuator/offload manager/FLPR processor with board2000/150 tuning imported. Independent global-coordinate oracle validates every submitted and timed live-RAM stereo word/count; owned-buffer mutation negative proves enqueue-only correctness cannot pass. Long1000s/900s-settle trajectories cover isolated clock/source skew, step,jitter,missed timed input,processing delay and remote faults plus mixed360 CPU fallback. Exact same enabled/disabled plants prove causal correction necessity through repeat/deadline rejection. Each fault requires exact public fault/fallback attribution, bounded successful remote resumption; permanent peer failure and late-success deadline decision reject despite correctCPU waveform. Final transferred/remaining/cancelled accounting, stopped-clock quiescence, bounded overlapping backpressure/stop, closed admission and no-helper-reset reopen with actual timed authored-data consumption pass. Six focused methods6/0/6; source/fixtures were reviewed and gaps repaired. Clean candidatef5d05c8958dd020c5bba59d6c354ce057993c0c3 passed canonical84/0/84, unchanged coverage baseline, matrix0errors/notes and BSim17/26. Hosted run37157141418 passed unit,coverage,BSim,tests,firmware;release skipped. Generated per-suite CDB ignore and native compile-variant alignment repaired gate issues without weakening behavior; alignedfocusedlog byteidentical. Results docs/development/pb-046-clock-loop-results-20261003.md. Production code/config unchanged; no physicalCLK/IPC/FLPR,RF,codec,analog,presentation,SMP,full-clamp or release claim. Done through PR16; human merge official acceptance.
<!-- SECTION:FINAL_SUMMARY:END -->
