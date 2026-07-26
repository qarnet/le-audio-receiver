# Phase 4b Plan Rewrite — Review Corrections

Status: **APPLIED 2026-07-26** — required correction after `2b71052`

## Problems

1. `docs/design.md` and `STATUS.md` retain obsolete “~0.6 s repeat/drop
   cadence” wording. Rate conversion removes roughly 381 frames/s; precise
   audible artifact character remains unmeasured.
2. `STATUS.md` says new DAC has not been streamed against, but Phase 4a.2 did
   stream with it for 35 seconds. Audible result remains pending, not stream
   status.
3. Empty untracked repository-root `ts` file is unrelated artifact and must be
   deleted.

## Scope

Edit only:

- `docs/design.md`
- `STATUS.md`
- this handoff document

Delete only root `ts` empty artifact.

## Required changes

1. Replace cadence claims with: nearest-neighbor conversion removes about 381
   frames/s at nominal mismatch; artifact audibility/character is unmeasured;
   Phase 5 quality ASRC stays conditional on listening result.
2. State new DAC has passed autonomous 35-second technical stream with zero
   slab-full/underrun after rate conversion, while audible outcome remains
   pending physical observation.
3. Preserve ISO-timestamp Phase 4b rewrite unchanged.

## Verification

```bash
test ! -e ts
```

## Constraints

Do not touch source/config or existing uncommitted changes. Commit scoped docs
and this handoff only. No hardware, amend, push, merge, or PR.
