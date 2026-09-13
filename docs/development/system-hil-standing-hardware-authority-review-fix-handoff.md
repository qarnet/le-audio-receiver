# Standing lab hardware authority review fix handoff

Status: documentation-only review repair. No hardware, firmware, test, runner,
fixture, evidence, commit, or repository cleanup action belongs in this phase.

## Review defect

The standing-authority update correctly changed `AGENTS.md` and the active
System HIL milestones plan. Two active RH3 documents still read as an absolute
future ban on direct board work:

- `docs/development/system-hil-rh3-software-status.md` says later remaining RH3
  work needs `matrix-runner-owned flash/reset/radio execution`.
- `docs/development/system-hil-resume-state.md` is a working restart handoff,
  but says `Matrix runner solely owns hardware. Do not use manual serial,
  flashing, reset, or FLPR work`.

This conflicts with current user authority: direct identity-proven nRF board
diagnosis is permitted. Formal matrix evidence may still use the runner, and
all acceptance/evidence rules remain strict.

## Exact repair

### `docs/development/system-hil-rh3-software-status.md`

In its existing `## Current standing lab hardware authority (2026-08-23)`
section, add a short explicit clarification:

- `matrix-runner-owned` describes required ownership for a formal matrix
  execution when that runner is used. It does not prohibit direct manual
  nRF-board diagnostics.
- References below to `approval` describe evidence/acceptance governance, not
  per-action permission to flash, erase, reset, read, or debug an attached nRF
  board.
- Direct diagnostics retain identity proof, raw evidence, immutable-evidence
  preservation, and central-only requirements.

Do not edit the historical `## Hardware execution controls` or per-run records.

### `docs/development/system-hil-resume-state.md`

After its opening warning block, insert a dated
`## Current standing lab hardware authority (2026-08-23)` section.

State that this current policy supersedes only forward-looking manual-hardware
restrictions elsewhere in the restart handoff. State all of:

- all attached Nordic nRF boards may be read, debugged, flashed, reset, erased,
  or recovered without fresh approval, subject to target/tool support;
- run `nrf-probes` or the appropriate resolver before each target-changing
  action, retain raw identity evidence, and never touch unknown or non-Nordic
  hardware;
- `scripts/hil-runner.py` remains useful for formal end-to-end/matrix evidence,
  but is not the sole permitted owner for direct diagnostic work;
- preserve immutable run evidence and all central-only requirements;
- test a simulator-reported behavior on physical nRF hardware where practical
  before accepting a behavior-changing source fix.

Immediately before current bullet 5 in the resume instructions, add one
sentence that identifies bullet 5 as historical execution control for the
recorded diagnostics, superseded for future work by the new standing policy.
Do not rewrite individual run entries, immutable IDs, results, hashes, or
historical phrasing.

## Verification

Run from repository root:

```bash
git diff --check
git diff -- docs/development/system-hil-rh3-software-status.md \
  docs/development/system-hil-resume-state.md \
  docs/development/system-hil-standing-hardware-authority-review-fix-handoff.md
git status --short
```

No tests are needed. Do not commit.
