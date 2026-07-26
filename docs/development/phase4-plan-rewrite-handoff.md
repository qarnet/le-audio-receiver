# Phase 4 Plan Rewrite — I2S Hardware Findings

Status: **implemented 2026-07-26**

## Goal

Update project truth after GPIO, DAC-isolation, and standalone I2S20 work.
The previous plan and `STATUS.md` state that nRF54L15 I2S hardware never
completes DMA. That conclusion is disproven: standalone I2S20 ran 20 seconds,
fed 2,016 blocks, had no EIO/underrun, and showed enabled master/TX plus
correct PSEL and FRAMESTART registers.

The old DAC breakout must no longer be treated as known-good. With it connected
and MUTE low, D1/LRCK was held high; with its digital wires removed, D1 toggled.
This supports an incompatible or defective old breakout/wiring assembly. A new
DAC is connected, but audible output and externally measured I2S waveform are
still unconfirmed.

## Scope

Edit only:

- `docs/design.md`
- `STATUS.md`
- `README.md`
- this handoff document

Do not edit source, overlays, Kconfig, standalone test apps, historical result
documents, or `AGENTS.md`.

## Required `docs/design.md` rewrite

1. Update date/status in heading from 2026-07-25 to 2026-07-26.
2. Update “What works today” nRF54L15 bullet:
   - BLE/CIS transport works through nRF5340DK `hci_uart` central, not stale
     BT540-only wording;
   - GPIO and standalone I2S20 hardware prove D0/P1.4, D1/P1.5, D2/P1.6;
   - 20-second standalone DMA run is verified;
   - main receiver end-to-end audio is still not accepted;
   - new DAC connected, audible result pending.
3. Rewrite Phase 4 preamble and Phase 4a to contain ordered gates:
   - **4a.0 completed hardware characterization:** correct XIAO map, old DAC
     isolation result, standalone I2S evidence, PCLK32M working;
   - **4a.1 current main-pipeline retest:** first run unchanged receiver with
     new DAC and working central, capture serial + analyzer + user listening;
   - if app still produces slab-full/EIO, compare its initial queue/producer
     behavior with standalone test before changing source. Do not pre-decide
     a fix;
   - correct expected 48kHz/16-bit/stereo BCK from stale 3.072 MHz to
     **approximately 1.536 MHz** and BCK/LRCK ratio 32;
   - external analyzer measurement and user audible report remain required;
   - **4a.2 integration fix only if retest fails**, limited to measured
     queue/producer issue; preserve no-double-write and error recovery rules.
4. Keep 4b GRTC/DPPI mandatory before long-duration 4c stability. Do not move
   ASRC/FLPR phases or declare Phase 4 complete.
5. Replace Appendix B “no audio has streamed” snapshot with a dated current
   Phase 4 evidence summary. Preserve clear separation among:
   - established BLE ISO delivery;
   - established standalone I2S hardware/DMA;
   - old DAC failure/isolation evidence;
   - pending main-pipeline/new-DAC listening and analyzer analysis;
   - pending GRTC/DPPI and stability.

## Required `STATUS.md` rewrite

1. Replace “I2S DMA stall is primary blocker” with current concise bottom line:
   BLE transport verified; standalone I2S hardware/DMA verified; old DAC caused
   LRCK anomaly; main receiver with new DAC needs end-to-end retest.
2. Replace entire stale “I2S DMA stall” section with evidence table:
   - old breakout D1 behavior connected vs removed;
   - standalone test 20.001 seconds / 2,016 blocks / no EIO;
   - correct D0/D1/D2 mapping;
   - PCLK32M works; standalone PCLK32M_HFXO UsageFault is tracked separately;
   - raw analyzer capture exists but frequency/data analysis pending;
   - new DAC audible result pending.
3. Make next actions exact and ordered:
   - reflash standalone tone test only when user ready to listen; analyze raw
     capture;
   - then run new-DAC main receiver stream unchanged;
   - only if that fails, instrument/compare application queue behavior;
   - then Phase 4b GRTC/DPPI.
4. Remove stale claims that I2S never completes, no upstream test exists,
   D1/D2 probes may be loose, external slave clock is next, or Phase 5 is next.
5. Retain central/dongle setup and historical transport evidence.

## Required `README.md` correction

1. Keep generic UDA1334A/PCM5102A wiring guidance, but remove “UDA1334A is
   drop-in” as an unconditional claim.
2. Add brief hardware-validation note: before treating a new breakout as
   working, validate BCK/LRCK/SDOUT with standalone test; a tested old
   CJMCU-1334-compatible unit held LRCK high when unmuted and is not suitable
   for this receiver.
3. State MUTE high mutes analog output, so it is not an audio fix.
4. Do not identify a new DAC model or claim audible sound without user evidence.

## Verification

```bash
git diff --check
  docs/development/phase4-plan-rewrite-handoff.md
```

## Constraints

- Documentation must distinguish measured facts from pending work.
- Do not claim raw logic-analyzer files have been frequency/data decoded.
- Do not claim current receiver/new DAC produces sound.
- Preserve unstaged `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` and
  `src/bt_bap.c` exactly.
- Commit only scoped docs. Do not amend, push, merge, or open PR.

## Executor recap

Return changed files, key plan/status changes, verification output, commit
hash/message, and exact next Phase 4a.1 execution gate.
