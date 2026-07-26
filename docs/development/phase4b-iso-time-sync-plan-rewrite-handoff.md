# Phase 4b Plan Rewrite — Supported ISO Timestamp Synchronization

Status: implemented (2026-07-26)

## Discovery

Nordic ISO time-sync documentation supports nRF54L15 GRTC-based, hardware-timed
actions from received ISO SDU timestamps. The reference sample is:

`nrf/samples/bluetooth/iso_time_sync/`

Official documentation:

`https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/samples/bluetooth/iso_time_sync/README.html`

On nRF54 Series, ISO timestamps and controller time use the controller clock;
the sample uses GRTC and DPPI to schedule a hardware action at timestamp plus
presentation delay. This avoids final-action callback jitter.

MPSL/SDC owns RADIO. No plan may configure/read direct RADIO RX events, RADIO
IRQ, or RADIO DPPI publication while SDC runs.

## Goal

Replace obsolete Phase 4b RADIO-RX capture plan with supported ISO-timestamp
presentation scheduling plus I2S FRAMESTART capture. Document exact boundaries
between supported production path and optional diagnostics.

## Scope

Edit only:

- `docs/design.md`
- `STATUS.md`
- `AGENTS.md`
- `docs/development/phase4a2-rate-conversion-results.md`
- this handoff document

No source, Kconfig, test, overlay, or superseded
`docs/nrf54l15-drift-compensation.md` change.

## Required `docs/design.md` changes

Replace Phase 4b bullets with this supported design:

1. Validate `BT_ISO_FLAGS_TS` before consuming `info->ts`. Treat it as
   controller-clock ISO SDU reference time; do not use callback arrival as RX
   timestamp.
2. Use Nordic ISO-time-sync pattern: schedule a future GRTC compare/action at
   ISO timestamp plus receiver presentation delay. GRTC+DPPI executes final
   reference action independent of callback wake latency.
3. Route I2S20 `FRAMESTART` through DPPI to GRTC capture. This captures exact
   local LRCK frame timing.
4. Derive drift estimate from controller-timeline presentation reference and
   I2S frame capture. Keep slab fill as phase feedback.
5. State `sdc_hci_cmd_vs_set_event_start_task()` is optional ACL timing-event
   diagnostic only, not CIS RX/SDU timing and not PI input.
6. State direct RADIO RX `ADDRESS`/`END` captures are forbidden with SDC/MPSL;
   no fallback direct-RADIO implementation.
7. Keep Phase 4b mandatory before 4c, but remove wording claiming ISO
   timestamps are merely a fallback or that both ISO and hardware paths must
   not coexist. ISO timestamp is required input to supported hardware schedule.

Reference Nordic ISO time-sync sample and nRF Audio synchronization module as
conceptual references only. Do not claim direct architecture portability from
dual-core nRF Audio to this single-core application.

## Required `STATUS.md` changes

Replace generic “GRTC/DPPI drift measurement” next action with concise supported
path: ISO SDU timestamp → future GRTC presentation trigger; I2S FRAMESTART →
GRTC capture; no direct RADIO access. Link design doc and ISO-time-sync sample.

## Required `AGENTS.md` addition

Add concise gotcha near clock recovery:

- SDC/MPSL owns RADIO; never access RADIO registers/events/IRQ/DPPI directly.
- On nRF54L15, use ISO `info->ts` with `BT_ISO_FLAGS_TS`, GRTC future trigger,
  and I2S FRAMESTART capture. Cite local ISO time-sync sample path.
- SDC Event Start Task is ACL-event diagnostic, not CIS RX timestamp.

## Required phase4a2-results correction

Replace “GRTC-based measurement + PID” residual wording with the supported
ISO-timestamp/GRTC presentation-reference path. Correct imprecise
“~0.6 s repeat/drop cadence” claim: nearest-neighbor conversion removes about
381 output frames/s at nominal 47,619-vs-48,000 mismatch; artifact audibility
remains unmeasured and Phase 5 quality work remains conditional.

## Verification

```bash
git diff --check
  docs/development/phase4a2-rate-conversion-results.md \
  docs/development/phase4b-iso-time-sync-plan-rewrite-handoff.md
```

## Constraints

- Preserve any existing uncommitted changes exactly.
- Do not stage unrelated files.
- Do not run hardware or modify source.
- Do not amend, push, merge, or open PR.

## Executor recap

Return changed files, supported Phase 4b path summary, verification output, and
commit hash/message.
