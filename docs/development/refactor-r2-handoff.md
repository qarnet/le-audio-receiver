# Refactor R2 handoff — dead API and historical-source cleanup

## Goal

Remove confirmed no-caller production APIs and move retired sample-adjust
implementation out of `src/`, while preserving every live production path and
historical regression behavior.  Start from clean `6ad11d0` after accepted R1
and dongle-flash tooling follow-up.

R2 changes public surface and coverage denominators only.  No PCM behavior,
Bluetooth behavior, FLPR protocol, timing, lifecycle, build configuration,
BSim output, or hardware behavior may change.

## Reconfirmed phase-HEAD caller evidence

Repository-wide search at `6ad11d0` found:

| Symbol/source | Production callers | Remaining uses before R2 |
|---|---:|---|
| `audio_clock_actuator_consume_sample_adjustment()` | 0 | production declarations/implementations plus actuator tests/matrix/docs only |
| `flpr_ring_mgr_set_consume_cb()` and `flpr_ring_consume_cb_t` | 0 | no-op implementation, direct no-op test, two copied mocks, matrix/docs only |
| `audio_rate_converter_nearest_stereo()` | 0 | five `rate_convert` tests, declaration/implementation, matrix/docs only |
| `audio_offload_is_stopped()` | 0 | three dedicated test cases, declaration, nRF54 implementation, non-nRF stub, matrix/docs only |
| `DEFAULT_VOL` | 0 | definition only in `src/audio_volume.c` |
| `src/audio_clock_actuator_sample_adjust.c` | 0 production builds | historical test CMake and active historical docs/baseline metadata only |

No blocking design question remains.  Smallest supported shape: delete dead
production interfaces; retain sample-adjust algorithm only under its historical
test with a test-local header.

## In scope

### Dead actuator API

- Remove `audio_clock_actuator_consume_sample_adjustment()` declaration from
  `src/audio_clock_actuator.h`.
- Remove implementations from `src/audio_clock_actuator_apll.c` and
  `src/audio_clock_actuator_none.c`.
- Remove consume assertions from APLL/APLL-no-HFCLK/NONE tests.  Preserve
  repeated `apply_ppm()` proof; rename `test_repeated_calls_and_consume` to
  `test_repeated_calls` without changing suite count.
- Update active `AGENTS.md`, README/coverage descriptions, and matrix entries so
  production actuator API is init/apply/reset only.

### Historical sample-adjust isolation

- Move `src/audio_clock_actuator_sample_adjust.c` to
  `tests/unit/actuator_sample_adjust_historical/src/` with an explicit
  historical filename, e.g. `audio_clock_actuator_sample_adjust_historical.c`.
- Add test-local header in same suite declaring its four historical functions,
  including `audio_clock_actuator_consume_sample_adjustment()`.
- Historical implementation and `test_actuator.c` include this test-local
  header, not production `src/audio_clock_actuator.h`.
- Update historical CMake to compile local source; retain seven historical
  regression tests and exact behavior.
- Remove historical source from production key-file/architecture tables.  Active
  docs may point to test-local location and must label it historical.  Dated
  evidence documents may retain old paths as historical records.

### Other dead APIs

- Delete `flpr_ring_consume_cb_t`, `flpr_ring_mgr_set_consume_cb()` declaration,
  no-op implementation, direct no-op test, copied mocks, matrix outcome entry,
  and active docs that present it as callable.
- Delete `audio_rate_converter_nearest_stereo()` declaration/implementation and
  its five dedicated tests:
  `test_nearest_stereo_lr_pairing`, `test_nearest_stereo_endpoints_downsample`,
  `test_nearest_stereo_identity_copy`, `test_nearest_stereo_single_output`, and
  `test_nearest_stereo_empty`.  Keep five `init`/`next_frames` tests and update
  suite descriptions/counts.
- Delete `audio_offload_is_stopped()` declaration, both production/stub
  implementations, the `audio_offload_isstopped` suite and its three tests,
  matrix outcomes, and active STATUS/coverage claims.  Do not replace it with a
  private equivalent: no caller needs one.
- Delete unused `DEFAULT_VOL`; keep actual configured/default volume behavior
  (`CONFIG_BT_AUDIO_VOL_DEFAULT` and current atomic initialization) unchanged.

### Contract comments

- In `audio_clock_actuator_none.c`, replace stale “until Phase 4 sample_adjust
  lands” wording with current truth: NONE is nRF54 production because ASRC
  consumes ppm; actuator itself performs no clock steering.
- In `audio_offload.h`, remove false claim that FLPR negative processing status
  returns API success with zero frames.  Current production at
  `audio_offload.c:1196-1209` records fault/fallback/recovery and returns
  `-EAGAIN`, leaving output/result untouched.
- Correct `audio_offload.c` top accounting wording: valid calls after
  initialization/non-STOPPED increment submit; invalid args and STOPPED/
  uninitialized calls do not; a failed accepted submit increments ASRC fallback
  once on its terminal fault path, while generic counters follow their existing
  lifecycle/fault helpers.  Do not change accounting code.
- Remove only verified stale/duplicate banners in touched active headers; no
  broad comment rewrite.

## Tests and machine metadata

Update:

- `tests/test-matrix.json`: remove deleted public API outcomes/source entries;
  keep all surviving API outcomes exact; move/remove historical source entry so
  production source inventory contains no test-only file.
- `docs/testing/coverage-matrix.md`: corrected focused counts/descriptions,
  historical source provenance, and R2 coverage migration table.
- `README.md` and repo `AGENTS.md`: production key files/API truth.
- `STATUS.md`: remove active `audio_offload_is_stopped()` coverage claim and
  record R2 status only after acceptance.

Historical handoffs/results remain immutable context unless an active link is
factually broken.  Do not delete empty diagnostics directories unless one is
confirmed tracked, empty/stale, and unreferenced; this is optional and should
not expand R2.

## Coverage migration — mandatory two-commit flow

Follow `refactor-plan.md` coverage migration rule exactly.

### Commit 1 — code/tests/contracts, old baseline retained

Implement source/test/matrix/active-doc changes but leave
`tests/coverage-baseline.json` unchanged.  Run focused suites and report-only
coverage while dirty as useful.  On focused green and clean checker state where
possible, commit:

`refactor: remove dead APIs and isolate historical actuator`

The old baseline may intentionally fail enforcement because source denominators
and historical exclusion provenance changed; do not weaken checker logic or
commit an unexplained baseline rewrite in commit 1.

Focused suites:

- `actuator_apll`
- `actuator_apll_nohfclk`
- `actuator_none`
- `actuator_sample_adjust_historical`
- `flpr_ring_mgr`
- `rate_convert` (expected five surviving tests)
- `audio_offload` (expected three fewer tests)
- `offload_asrc`
- `volume`
- matrix checker and JSON validation

Run repository-wide stale-reference search after edits.  Allowed occurrences of
deleted names: historical sample-adjust test-local consume symbol and dated
historical evidence.  No production `src/`, production mocks, active API docs,
or matrix API entries may remain.

### Coverage generation on clean commit 1

Run candidate coverage in report-only/non-enforcing mode supported by repo
tooling.  Capture exact gcovr/gcov versions, population, aggregate totals, and
per-file metrics.  Compare affected surviving files against old baseline:

- `src/audio_clock_actuator_apll.c`
- `src/audio_clock_actuator_none.c`
- `src/audio_rate_convert.c`
- `src/flpr_ring_mgr.c`
- `src/audio_offload.c`
- `src/audio_volume.c` if numeric metrics change

For each, record deleted symbols, zero-production-caller evidence, and old → new
covered/total lines, branches, functions.  Ratios for surviving behavior may not
decrease; unchanged files must remain at or above baseline.  Covered live code
may not be removed to improve percentage.

Historical move must remove
`src/audio_clock_actuator_sample_adjust.c` from baseline exclusion population
and reason map.  Numeric population should remain 26 because source was already
excluded; explain this explicitly.

### Commit 2 — baseline migration

Update `tests/coverage-baseline.json` from exact clean commit-1 candidate and
add migration/provenance table to `docs/testing/coverage-matrix.md`.  Commit:

`coverage: migrate baseline after R2 dead-code cleanup`

Then run canonical enforcement on clean commit 2.

## G1 and acceptance

On clean baseline commit:

```bash
./scripts/test-all.sh
./scripts/test-coverage.sh --output /tmp/r2-coverage --clean-output
fw-build-5340
fw-build-54l15
fw-build-dongle
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
git diff --check
```

Acceptance:

- canonical child count remains 47; changed internal testcase counts are
  recorded honestly;
- coverage population 26; no surviving per-file/aggregate ratio regression;
- builds 3/3 and build contract 76/76;
- BSim hashes/counts unchanged;
- zero new/actionable warnings; documented OpenOCD diagnostics are irrelevant
  because no hardware flash is required;
- repository-wide caller search confirms no stale production references;
- worktree clean.

No G2 expected: deletion proof plus G1 production builds establish no live
production call path changed.

After G1 passes, update R2 in `docs/development/refactor-plan.md` to ACCEPTED and
write `docs/development/refactor-r2-results.md` with exact commits, caller proof,
test counts, coverage migration, G1 results, and warnings.  Commit docs-only:

`docs: accept R2 dead API cleanup`

## Non-scope

- No R3 runner consolidation (including moving new dongle tests out of current
  `gate` child).
- No R4 shell split, R5 offload decomposition, R6 session extraction, R7
  teardown coordinator, or 360-frame FLPR support.
- No behavior replacement for deleted APIs.
- No Kconfig/devicetree/board/ABI/protocol change.
- No BSim expected-value repin or coverage-ratio relaxation.
- No hardware flash/run.

## Escalation and recap

Stop instead of guessing for a newly found production caller, test-matrix
checker contradiction, coverage ratio decrease, unexplained population change,
BSim output change, build warning, or need to alter live behavior.  After two
materially different failed attempts, return blocker evidence and one precise
question.  Never commit failing/incomplete acceptance or normalize warnings.

Executor recap must include files/commits, exact deleted symbols and caller
proof, focused counts, old→new coverage table, G1 results/logs, warnings, final
status, and deviations.  No push, amend, merge, PR, force-push, or attribution.
