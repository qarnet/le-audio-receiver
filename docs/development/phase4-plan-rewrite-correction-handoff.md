# Phase 4 Plan Rewrite — Documentation Corrections

Status: completed after `940965a`

## Problems

1. `docs/design.md` has duplicate consecutive `### Phase 4b` headings.
2. `STATUS.md` says old-DAC main-pipeline slab-full/EIO was “not a firmware
   bug.” Evidence proves old DAC assembly held LRCK high, but has not yet
   excluded receiver queue/producer behavior. New-DAC main-pipeline retest is
   explicitly pending, so attribution must remain bounded.

## Scope

Edit only:

- `docs/design.md`
- `STATUS.md`
- this handoff document

## Required corrections

1. Remove duplicate Phase 4b heading, retaining one heading and its content.
2. Reword old-DAC main receiver result to say the old DAC assembly was a proven
   physical blocker/contributor, while firmware queue behavior is not yet
   ruled out. State new-DAC unchanged receiver retest is required before final
   root-cause attribution.
3. Do not weaken established standalone I2S or old-LRCK evidence. Do not
   modify source or run hardware.

## Verification

```bash
git diff --check
  docs/development/phase4-plan-rewrite-correction-handoff.md
```

## Constraints

Preserve unstaged receiver diagnostics. Commit scoped docs only. Do not amend,
push, merge, or open PR.
