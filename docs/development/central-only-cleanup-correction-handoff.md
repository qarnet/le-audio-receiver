# Central-Only Cleanup — Residual-Term Correction

Status: required correction after `88b787e`

## Problem

The cleanup verification still finds `smartphone` in negative-prohibition text.
User requires removal of all central-device-type references. Keep central-only
rule, but phrase it without those terms.

## Scope

Edit only:

- `AGENTS.md`
- `docs/development/hardware-verification-handoff.md`
- this handoff document

Replace residual forbidden terms with “human-operated central” or equivalent.
Do not alter source/config/partial rate-conversion work, run hardware, or stage
unrelated files.

## Verification

```bash
git grep -inE 'phone|phones|smartphone|BT540|Realtek' || true
git diff --check
git status --short
```

Expected: no matches. `headphone` is allowed because it is audio-output
hardware, not a central/test source.

## Constraints

Commit only scoped docs. Preserve unstaged source/config changes. No amend,
push, merge, or PR.
