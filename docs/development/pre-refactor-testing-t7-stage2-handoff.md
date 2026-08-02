# Phase T7 Stage 2 handoff — close gaps, freeze baseline, enforce coverage

## Goal

Turn Stage 1 instrumentation into enforceable T7 acceptance: remove test-only
seams from production metrics, execute every remaining host-testable production
function, strengthen public-API/outcome/transition manifest completeness, commit
the first honest exact baseline, integrate coverage/matrix checks into the
canonical gate, and validate everything on one exact commit.

Start from clean `test/pre-refactor-behavior` at `e3f97d9`. T7 is not accepted.

## Scope

In scope:

- Precise gcovr exclusions for code compiled only under explicit test macros.
- Focused tests for every real zero-hit production function.
- Coverage runner cleanup/provenance/baseline enforcement.
- Stronger test-matrix API/outcome/transition checks and manifest audit.
- Committed exact line/branch/function baseline.
- Canonical gate integration, docs, full acceptance.

Out of scope:

- BSim gcov aggregation; T4 remains integration evidence for `bt_bap.c`.
- Numeric instrumentation of hardware-only/delegated files.
- Raising baseline above honest post-gap measurements merely for appearance.
- Refactoring algorithms to improve percentages.
- T8 hardware, push, merge, PR, amend, force-push.

## 1. Correct Stage 1 report/tooling defects

### Output hygiene and provenance

`test-coverage.sh` must reject a nonempty output directory unless an explicit
safe `--clean-output` option is passed. `--clean-output` must reject empty,
root, repo-root, or paths outside an allowed caller-selected output tree; never
blindly `rm -rf` arbitrary input. Unit-test argument/safety behavior with fake
tools.

Run manifest must list all suite names and exact per-suite commands (Stage 1's
pre-commit artifact had empty names). For enforcement runs, require clean git
worktree and record exact `HEAD`; `--report-only` may run dirty only when
manifest records `dirty: true` and cannot create/update committed baseline.

### Production-only metrics

Add standard gcovr exclusion markers around self-contained functions/blocks
that exist only under these explicit native test macros:

- `AUDIO_I2S_NATIVE_TEST` helper block;
- `AUDIO_SHELL_TEST` wrapper block;
- `AUDIO_TIMING_NRF54_TEST` helper-accessor/reset block (not production timing
  callbacks/logic);
- `FLPR_HANDSHAKE_NATIVE_TEST` helper block;
- `FLPR_RING_MGR_NATIVE_TEST` helper block;
- `FLPR_RUNTIME_NATIVE_TEST` helper-only block;
- `AUDIO_PERF_TEST` helper-only injection function if present.

Use `GCOVR_EXCL_START` / `GCOVR_EXCL_STOP` comments immediately inside guarded
helper blocks. Do not exclude test-configured branches inside real production
functions, production shell diagnostics, mocks, low-coverage code, or error
paths. Verify gcovr JSON no longer reports these test-only helper functions.

Do not add manifest `function_exclusions` for test-only symbols that should not
exist in production metrics.

## 2. Close every real zero-hit function gap

Add externally observable tests through public APIs/real command dispatch.

### `audio_offload.c`

Execute `audio_offload_is_stopped()` through direct offload suite. Prove true in
initial/stopped states and false in preparing/active/recovering states where
reachable. Add exact manifest outcomes/witnesses.

### `audio_shell.c`

The following are production shell handlers and must execute through real shell
registry/`shell_execute_cmd`, not direct copied logic:

- `cmd_flpr_stress`
- `cmd_flpr_ring_test`
- `cmd_flpr_ring_reset`
- `cmd_flpr_ring_init`
- `cmd_flpr_ring_stall_producer`
- `cmd_flpr_ring_stall_flpr`
- `cmd_flpr_ring_stall_flpr_ms`
- `cmd_flpr_ring_acceptance`
- `cmd_flpr_hang`

Use existing nRF54 fake dependency layer. Cover bounded, fast outcomes:

- stress not-ready, already-active, and successful summary;
- ring test invalid count, uninitialized, and delegated success/failure summary
  where mocks allow without long loops;
- ring reset/init success and exact propagated failures;
- producer stall on/off text and side effect;
- FLPR stall success/failure;
- timed stall usage, zero-mask rejection, duration overflow, success/failure;
- acceptance invalid count and rings-uninitialized early rejection (do not run
  million-block gates in native unit);
- hang not-ready, ACK success, and exact failure errno.

These tests must also lock every return/error outcome reachable before physical
FLPR transport. Long-running successful acceptance remains hardware evidence,
named in manifest.

### `flpr_ring_mgr.c`

Execute production `flpr_ring_mgr_test_run()`,
`flpr_ring_mgr_test_run_rate()`, and `flpr_ring_mgr_wait_consume()` in direct
suite. At minimum prove pre-init `-EAGAIN`, active-test `-EBUSY`, zero-timeout
wait behavior, and any bounded success path the existing fake handshake/rings
can drive. If full successful loop requires remote FLPR worker, classify only
that specific success outcome as hardware acceptance while still executing
each host-testable early/error path. No whole-function exclusion.

After these changes and test-only exclusions, an exact-commit report plus
`check-test-matrix.py --coverage-json` must have **zero zero-hit function
errors**. Any newly revealed real zero-hit function is another gap to test, not
an exclusion by default.

## 3. Strengthen manifest completeness

Enhance `check-test-matrix.py` and fixture tests.

### Public API inventory

For each direct `.c` source, discover top-level non-static function definitions
from current source (comment/string-safe enough for repo style), excluding only
functions inside explicit test-only macro blocks marked above. Require every
discovered public function to appear in `public_outcomes`. Missing public API is
a hard error. Keep explicit fixture tests for static-vs-public, multiline
signatures, test-only blocks, invented APIs, and duplicate APIs.

Header-structural/delegated/integration/hardware classes retain their explicit
rules; do not pretend private static handlers are public APIs.

### Outcome ledger

Replace vague `error-class` entries with exact observable outcomes:

- `0`/success;
- exact negative errno (`-EINVAL`, `-EAGAIN`, `-EBUSY`, `-EIO`, etc.);
- exact enum/status result where API returns one;
- void side-effect/invariant for void APIs.

Audit every direct public API against production return paths and existing
tests. Add missing focused tests where an outcome is host-testable. One witness
may support multiple explicit outcomes only when test genuinely asserts each.
Hardware-dependent outcomes need an exact outcome record whose witness/evidence
is an existing hardware script/doc, not a fake unit witness.

Checker must forbid `error-class`, empty/generic placeholders, duplicate
`(api,outcome)` records, and direct public APIs with no outcomes. It cannot infer
all semantic return values from C; committed manifest is reviewed ledger, but
checker enforces completeness against public function inventory and witness
existence.

### State transitions

Add explicit boolean `stateful` to every manifest entry. If true, require a
nonempty, duplicate-free transition list with concrete `from->to` names and
witnesses. Audit known stateful modules (lifecycle, drift, I2S, offload,
handshake, ring/ring-manager, runtime, timing nRF54, app lifecycle, ASRC/rate
converter where state is retained). Stateless modules set false and must not
carry invented transitions. Checker tests both cases.

Update T4 integration entry to describe full 15-scenario matrix, not old mono
Stage 1. Acceptance commands/paths must resolve.

## 4. Baseline and enforcement

Add committed `tests/coverage-baseline.json`. Extend coverage runner modes:

```text
--report-only              no baseline enforcement; may not write baseline
--write-baseline PATH      clean exact-commit run; writes candidate baseline
--baseline PATH            enforce existing baseline (default committed path)
--output DIR
```

`--write-baseline` and default enforcement require clean worktree. Baseline
stores:

- schema version and generating commit/tool versions;
- exact numeric-population source list;
- overall covered/total counts and rational percentages for lines, branches,
  functions;
- per-file covered/total counts for all three metrics;
- exclusions and reasons.

Enforcement uses integer cross multiplication, not rounded display values:

```text
current_covered/current_total >= baseline_covered/baseline_total
```

Enforce overall and each existing baseline file for lines and branches.
Functions must be 100% for compiled production functions after approved
test-only removal, and must not fall below per-file baseline. New direct files
must appear, pass zero-hit function checking, and join overall denominator; no
automatic exclusion. Missing baseline file/current population drift is hard
failure until manifest/baseline intentionally updated.

Also emit `numeric-summary.json` derived from manifest numeric population, not
all `src/` headers/excluded files. `coverage-summary.json` remains raw gcovr.

Workflow:

1. Commit code/test/tool/checker changes without baseline.
2. On that clean exact commit, run report and checker until zero errors.
3. Run `--write-baseline /tmp/coverage-baseline.json`; inspect exact values.
4. Copy candidate to `tests/coverage-baseline.json`, commit it with gate wiring.
5. On new clean exact commit, run default enforcement again; report must pass
   committed baseline and carry current commit provenance.

Do not choose 80/65 or another cosmetic threshold. Exact post-gap honest ratios
are initial numeric line/branch thresholds. Never lower them in this phase.

## 5. Canonical gate integration

Add focused tooling tests as Python gate child. Add coverage then matrix as
ordered canonical children near end of `scripts/test-all.sh`:

1. coverage writes to `$TMP_ROOT/coverage` and enforces committed baseline;
2. matrix checker consumes `$TMP_ROOT/coverage/coverage.json`;
3. accepted BSim remains mandatory.

Do not make checker use stale `build/coverage`. Update category/total comments
from actual discovered suite count. Coverage internally rebuilds all native C
suites; increased runtime is accepted T7 cost.

## 6. Documentation and final validation

Update:

- `docs/testing/coverage-matrix.md` with exact numeric baseline and per-file
  classifications;
- `docs/testing/behavior-contract.md` with non-decrease/function/manifest gate;
- `STATUS.md` with T7 acceptance evidence;
- plan status references if present.

Mark T7 accepted only after exact final commit passes:

```bash
./scripts/test-coverage.sh --output /tmp/t7-final-coverage
python3 scripts/check-test-matrix.py \
  --coverage-json /tmp/t7-final-coverage/coverage.json
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check
git status --short
```

Run canonical full gate on workstation detached exact-commit worktree so BSim
passes. Coverage, checker, BSim, and every other child must pass; no desktop
environment exception is acceptance. Production builds may run on same exact
worktree. Record exact commit, total child count, final numeric metrics, runtime,
and zero warnings. Remove temp worktrees/refs/bundles/logs and leave both repos
clean. Commit docs separately after validated code/baseline commit.

## Escalation

Stop after two materially different failed attempts or if final report reveals
an unplanned architecture/testability issue. Do not lower baseline, exclude
real low-coverage code, label host-testable paths hardware-only, count test-only
helpers as production, leave generic outcome placeholders, suppress broad gcov
errors, skip canonical coverage/BSim, or keep guessing. Return exact function,
source, commands, logs/report paths, diff/status, one question, and smallest
hypothesis. Do not commit knowingly failing enforcement.
