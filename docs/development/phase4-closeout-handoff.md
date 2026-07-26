# Phase 4 plan closeout handoff

## Goal

Close accepted Phase 4 plan using existing evidence. Make current project docs
internally consistent, run complete non-hardware regression gate, and commit
closeout. Do not add firmware behavior.

## In scope

- Update `docs/design.md` so Phase 4 is unambiguously complete on measurable
  exit criteria recorded on 2026-07-26.
- Update `STATUS.md` to remove stale claims that new-DAC testing, analyzer work,
  or audibility is pending. Preserve audibility as **UNAVAILABLE**, not PASS or
  FAIL, and state analog quality is not claimed.
- Update `README.md` and `AGENTS.md` only where current status or key-file lists
  are stale. In particular, nRF54L15 is supported rather than merely “in
  progress,” and current timing/rate-conversion modules should be discoverable.
- Resolve contradictions inside those files, including stale Phase 4 opening
  text, old 4a.1 requirements, “analysis pending,” and “Adopted” wording for
  conditional/deferred Phase 5/6 options.
- Keep historical evidence clearly labeled as historical where retained.
- Run all repository unit suites and pristine builds for nRF5340 and nRF54L15.
- Fix any warning or failure attributable to repository state. Never normalize
  warnings.
- Commit completed closeout.

## Out of scope

- No Phase 5 ASRC implementation.
- No Phase 6 FLPR implementation.
- No firmware feature/refactor unless required to fix a regression exposed by
  mandated verification.
- No flash, reset, UART interaction, central stream, new sigrok capture, raw
  analyzer processing, physical-listening claim, push, PR, release, amend, or
  force-push.
- Do not update superseded `docs/nrf54l15-drift-compensation.md`.

## Accepted state and invariants

- Phase 4 technical gate: 60,000 frames / 600.00 s, zero disconnect,
  slab-full, underrun, warning, error, fault, or assertion; clean teardown.
- External digital I2S gate: PASS at DAC pins; BCK 1,525,637.347 Hz, LRCK
  47,676.613 Hz, ratio 31.999701, SDOUT active.
- Physical audibility: UNAVAILABLE by user. This neither fails Phase 4 nor
  supports analog-quality claims.
- Phase 5 remains conditional/deferred until measurable quality evidence or a
  physical listening result demonstrates need.
- Phase 6 remains conditional/deferred until CPU-headroom evidence demonstrates
  need.
- I2S20 FRAMESTART is a DMA-buffer event (~100 Hz), not LRCK.
- SDC/MPSL owns RADIO; direct RADIO access remains forbidden.
- Both target builds must remain warning-free.
- Do not rely on or reproduce invalid sigrok PCM metrics. Existing external
  clock/data-activity evidence is sufficient for closeout.

## Primary files

- `docs/design.md`
- `STATUS.md`
- `README.md`
- `AGENTS.md`
- Evidence references under `docs/development/phase4*-results.md`

## Verification

First discover repository-native unit commands from executable test metadata or
existing scripts; do not invent commands from prose. Run every unit suite listed
in `STATUS.md`: drift, actuator, timing, lifecycle, decode, and rate_convert.
Then run from repository root inside project development shell:

```sh
fw-build-5340
fw-build-54l15
```

Both builds must pass with zero warnings. Use pristine behavior supplied by
helpers. Do not flash hardware.

Also search changed current-state docs for contradictory combinations such as
Phase 4 “not done,” new-DAC/analyzer “pending,” or Phase 5/6 “adopted” without
their deferred gates. Historical sections may retain old facts only when
clearly dated/labeled.

## Return contract

Before returning, inspect `git status`, full scoped diff, and recent log. Stage
only intended files and commit with concise human-style message, no attribution.
Do not push. Return:

- files changed and exact status corrections;
- commands/tests run and results, including warning counts;
- commit hash/message;
- blockers or deviations;
- suggested follow-up, if any.
