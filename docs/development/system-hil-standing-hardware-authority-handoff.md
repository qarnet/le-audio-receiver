# Standing lab hardware authority documentation update handoff

Status: documentation reconciliation before continued RH3 investigation. This
phase changes no firmware, test behavior, historical evidence, or physical
board state.

## Goal

Record the user's standing authorization for this repository: agents may use
any attached Nordic nRF development board for reading, debugging, flashing,
resetting, recovering, erasing, serial interaction, radio testing, and other
board actions without asking for a fresh per-action or per-run confirmation.

This replaces forward-looking RH3 restrictions that required one-use physical
run approval or made the matrix runner the sole hardware owner. It does not
rewrite the constraints, outcomes, or immutability of historical evidence.

## Grounding

- User authorization in current conversation explicitly permits any action on
  attached nRF boards, including flash and erase, and permits source edits.
- `AGENTS.md` currently requires `nrf-probes` identity resolution and contains
  central-only test and nRF5340/nRF54L15 recovery guidance.
- `docs/development/system-hil-milestones.md` is the active System HIL plan.
  It currently prohibits blanket erase, requires explicit approval for erase
  or recovery, and makes the matrix runner sole owner of hardware.
- `docs/development/system-hil-rh3-software-status.md` contains the same
  forward-looking ownership restriction, while its run entries are historical
  evidence and must remain unchanged.

## In scope

1. Update `AGENTS.md` with a clear, repo-local standing lab-board authority
   section.
2. Update `docs/development/system-hil-milestones.md` so active and future
   System HIL work follows that standing authority.
3. Add a clearly dated current-policy note to
   `docs/development/system-hil-rh3-software-status.md` that supersedes only
   its forward-looking hardware-ownership restriction.
4. Preserve required identity discipline, evidence handling, and careful
   physical validation of simulator-reported failures.

## Out of scope

- No hardware action in this documentation phase.
- No source, Kconfig, DTS, test, runner, fixture, or evidence change.
- No rewrite of historical RH3 outcomes, handoffs, run counts, checksums, or
  recorded restrictions that applied when a historical run occurred.
- No change to global OpenCode policy outside this repository.
- No commit, staging, push, merge, tag, release, reset, stash, or clean.

## Exact documentation decisions

### `AGENTS.md`

Insert a `## Standing lab nRF hardware authority` section before
`## Central-only test rule`.

State all of the following explicitly:

- The user grants standing permission for agents working in this repository to
  use any attached Nordic nRF development board.
- Permitted actions include read/debug access, serial interaction, reset,
  flash, full erase/recovery, DTR/RTS, RF/Bluetooth testing, and firmware
  replacement. No per-action confirmation is needed for these nRF boards.
- Before any target-changing action, run `nrf-probes` or the appropriate
  project identity resolver and retain raw identity evidence. Never rely on a
  static probe-to-board mapping. Do not operate on unknown or non-Nordic
  hardware.
- Use `scripts/hil-runner.py` when its owned end-to-end evidence lifecycle is
  useful, but it is not the only permitted hardware owner. Direct debugger,
  serial, and board testing are allowed when they provide clearer diagnosis or
  validation.
- Preserve immutable run directories. Board erasure or reflashing does not
  authorize alteration of prior evidence.
- A simulator or host-test failure is evidence, not automatic proof of a
  production firmware defect. Before making a potentially behavior-changing
  source fix, evaluate the suspected failure on a physical nRF board when
  practical, then retain both simulator and board evidence.
- Keep all existing central-only pairing/streaming requirements unless a later
  plan deliberately changes those requirements.

Use careful language: broad permission applies to attached nRF lab boards, not
to unrelated host peripherals, arbitrary USB devices, or non-Nordic hardware.

### `docs/development/system-hil-milestones.md`

Add a `### Standing lab nRF hardware authority` subsection near the resource
and identity contract. It must reproduce the policy above in plan form:

- all attached nRF boards may be used, flashed, erased, recovered, reset, or
  debugged without separate approval;
- identity resolution and raw evidence remain mandatory;
- direct manual investigation is permitted alongside runner-owned execution;
- a simulator failure requires physical evaluation where practical before a
  behavior-changing source fix is accepted.

Reconcile active forward-looking restrictions without altering historical
claims:

- Replace the non-scope ban on blanket erase with a ban on targeting unknown or
  non-Nordic hardware.
- Replace `Only allowlisted role devices may be flashed or reset` with the
  standing nRF-board authority plus identity-resolution requirement.
- Replace clean-state text that rejects blanket erase or demands explicit
  approval for full erase/recovery. Erase and recovery are allowed diagnostic
  tools; record board identity, reason, command, and resulting firmware/state.
- Remove `request for blanket erase or nRF54L15 recovery` from stop rules.
  Keep stop on ambiguous identity, non-Nordic target, failed image verification,
  or missing evidence capture.
- Replace recommendation text that says not to erase attached hardware as
  generic setup. State that erase is allowed when it helps establish known
  state, but must be targeted, identity-proven, and recorded.

Do not weaken row acceptance, warning rules, artifact integrity, or evidence
requirements.

### `docs/development/system-hil-rh3-software-status.md`

Immediately before `## Hardware execution controls`, add a dated note titled
`## Current standing lab hardware authority (2026-08-23)`.

Clarify that the no-manual-hardware wording in the following historical section
described then-current diagnostic execution controls. It does not constrain
future work after the user's standing authorization. State current permission,
identity/evidence safeguards, physical-first evaluation of simulator failures,
and preservation of prior immutable evidence. Do not edit the historical
`## Hardware execution controls` paragraph or any per-run record.

## Verification

Run from repository root:

```bash
git diff --check
git diff -- AGENTS.md \
  docs/development/system-hil-milestones.md \
  docs/development/system-hil-rh3-software-status.md \
  docs/development/system-hil-standing-hardware-authority-handoff.md
git status --short
```

Verify wording has no conflict: future work may use attached nRF boards without
fresh hardware approval, but every target-changing action still requires
identity proof and retained evidence. No hardware command belongs in this
documentation-only phase.

## Executor return

Report exact files changed, the reconciled policy, verification output, current
git status, and any conflict or ambiguity. Do not commit.
