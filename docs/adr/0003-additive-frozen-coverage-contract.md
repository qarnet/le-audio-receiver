# 0003: Additive frozen coverage contract

- Date: 2026-10-08
- Status: Proposed
- Approval: Pending human product-owner review on PR 16

## Context

The committed coverage baseline `tests/coverage-baseline.json` is frozen: its
numeric values and its file SHA-256 anchor must not be rewritten or lowered.
PB-051 added a new measured source, `src/bt_audio_ltv_guard.c` (13/13 lines,
12/12 branches, 1/1 functions), which coverage must actually measure. The
installed gcovr is 8.4, and its default filtering excludes functions with a
`__`-prefixed name such as the guard's `__wrap_bt_audio_data_parse`, so the
guard would be silently dropped from a default trace.

## Options considered

- Exclude the new guard source from coverage. Rejected: excluding newly added
  real code hides it from measurement entirely.
- Rebaseline the frozen `tests/coverage-baseline.json` with the new file.
  Rejected: the frozen baseline is an explicit boundary and must not be
  rewritten or lowered.

## Decision and rationale

Coverage uses include-internal-functions only where needed: the per-suite
trace for the `ltv_bounds` suite adds `--include-internal-functions` so the
guard is traced despite its `__`-prefixed entry points; all older traces keep
the existing default filter, and the final merge step enables the same relaxed
filter for assembling totals. The committed baseline JSON keeps its exact
frozen SHA-256 and population. A separate additive sidecar,
`tests/coverage-additions.json`, pins the frozen baseline hash and records the
new guard's measured 13/13 lines, 12/12 branches and 1/1 functions.

Comparison rules enforced by `scripts/test-coverage.sh`:

- Old overall and per-file comparisons run only against the original
  population. Added sources are tracked independently at full coverage.
- Each additive per-file entry carries its own reference minimum totals and
  ratio from the sidecar: the current file's count must be at least the
  sidecar's recorded total (the guard pins 13/13 lines, 12/12 branches,
  1/1 functions) and its covered ratio must not drop, checked by integer
  cross multiplication. This is per-file only; the old population's overall
  and per-file checks remain ratio comparisons against the unchanged frozen
  baseline, with no combined old-absolute total floor introduced.
- When the sidecar is absent, the legacy enforcement is unchanged and the
  run population must stay exactly the frozen baseline; any new source in
  the population then fails as an unknown file. A new source such as the
  guard therefore requires a sidecar entry, and a missing, zero-coverage,
  unknown-population or malformed sidecar entry is an error, never an
  accepted pass; the sidecar is optional by presence, not by tolerance.
- A perfectly covered additive source can never mask a regression in the old
  population, because the frozen-population comparisons are computed only
  from the old sources' current records and are unaffected by sidecar totals.
- Tool versions and gcovr filtering must match the recorded setup for the
  numbers to be comparable.

## Consequences

The contract needs review when new files are added or instrumentation changes,
because the sidecar population is authored per addition. There is no synthetic
0/0 waived entry: every sidecar file holds real measured numbers. The frozen
baseline stays byte-identical, so historical comparisons remain valid.

## References

- Sidecar: `tests/coverage-additions.json`; frozen baseline:
  `tests/coverage-baseline.json`
- Enforcement: `scripts/test-coverage.sh` (additive sidecar section)
- Measured evidence: PB-051 results record the guard's 13/12/1 numbers and
  the sidecar SHA-256, in `docs/development/pb-051-ascs-results-20261005.md`
- Public coverage runner tests: `tests/unit/test_coverage_runner/` checks the
  coverage runner contract used by the public gate; the frozen baseline and
  sidecar comparisons in `scripts/test-coverage.sh` are that gate's own
  enforcement and acceptance boundary
- Completed item: [PB-051 in backlog completed](../product/backlog/completed/pb-051%20-%20ASCS-protocol-rejection-and-lifecycle-regression-matrix.md)
- Git history note: the 22 retired PB-051 handoffs and one refinement document
  are preserved only in Git history at revision
  `a94f010de00e25d4a2433f7b4c56b31a5377446e`
