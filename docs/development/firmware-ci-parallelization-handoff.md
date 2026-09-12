# Firmware CI logical parallelization implementation handoff

Status: implementation handoff for PR #12. Date: 2026-09-13.

## Goal

Reduce the hosted firmware workflow from the measured 58-minute critical path
to approximately 28-30 minutes by running independent software evidence in
parallel, while preserving all 72 canonical children, exact release artifacts,
required status contexts, fail-closed release behavior, and the local one-command
full gate.

Implement this on the current `feature/firmware-release-acceptance` branch so it
becomes part of PR #12. Do not push, merge, open another PR, create a tag, create
or modify a GitHub release, change `VERSION`, or operate hardware.

## Grounding evidence and decided architecture

Completed hosted run `34719725244` at commit `3b8954c`:

- `tests`: SUCCESS, 52m28s, exact `72 PASS / 0 FAIL / 72 TOTAL`;
- normal 45 C suites: about 16m38s;
- 24 Python suites: about 54s;
- coverage rebuild + matrix: about 16m52s;
- BabbleSim: about 11m40s (3m17s compile, 8m23s scenarios);
- `firmware`: SUCCESS, 5m17s;
- `release`: SKIPPED on pull request;
- full workflow: 58m02s.

Production and BSim binaries are not interchangeable:

- `scripts/bin/fw-build-54l15` produces ARM nRF54L15 cpuapp and FLPR firmware;
- `scripts/package-firmware-release.py` packages the exact resulting HEX files;
- `tests/bsim/CMakeLists.txt` produces an i386 host BSim receiver with test
  entry points/stubs and `tests/bsim/prj.conf` test-only configuration;
- `scripts/bsim-stage1-run.sh` builds that receiver and its BSim client, then
  runs them with the BSim PHY.

Do not make BSim consume production firmware. Keep BSim compilation and its 26
simulation runs in one job.

Implement this DAG exactly:

```text
test-unit --------------------> firmware -----------+
    |                                               |
    +--------------------------> tests aggregate ----+--> release (trusted main only)
test-heavy (coverage) --------> tests aggregate
test-heavy (bsim) ------------> tests aggregate
```

- `test-unit` is explicit because `firmware` must depend only on this job.
- `test-heavy` is a two-value matrix (`coverage`, `bsim`) with
  `fail-fast: false`.
- `tests` is the tiny aggregate preserving the existing required context.
- `release` needs both `tests` and `firmware`.
- A single matrix containing unit/coverage/bsim is forbidden because GitHub
  dependencies cannot target only one matrix child.

## In scope

1. `.github/workflows/firmware-build.yml`
   - replace monolithic `tests` execution with `test-unit`, matrix
     `test-heavy`, and aggregate `tests`;
   - make `firmware` need `test-unit`;
   - make `release` need both `tests` and `firmware`;
   - retain exact package and release artifact flow;
   - upload unique evidence artifacts from every test worker, even on failure.
2. `scripts/test-all.sh`
   - add default-preserving logical phase selection;
   - retain shared inventory and all existing child behavior.
3. `scripts/test_firmware_build_ci.py`
   - replace monolithic tests-job assertions with exact new DAG, setup,
     isolation, artifact, and aggregate contracts.
4. `tests/unit/test_coverage_runner/test_test_coverage_runner.py`
   - add focused public-boundary tests for phase argument handling and phase
     dispatch without real Zephyr/BSim builds.
5. `docs/development/firmware-ci-test-gate-plan.md`
   - add a dated parallelization amendment describing the new implementation,
     measured motivation, logical boundaries, artifact truth, and hosted
     acceptance still pending.
6. This handoff file remains part of the commit as implementation rationale.

## Out of scope

- Arbitrary test-number or suite-name sharding.
- Scenario-level BSim parallelism.
- Splitting BSim compile from BSim scenario execution.
- Reusing production firmware in BSim.
- Replacing normal unit runs with coverage-instrumented runs.
- Coverage baseline, coverage population, matrix manifest, BSim scenarios,
  hashes, counts, warning policy, or test weakening.
- New prebuilt containers, Cachix, or binary-cache infrastructure.
- Production source, Kconfig, devicetree, board, firmware packaging contents,
  release metadata semantics, permissions, action pins, dependency revisions,
  `flake.lock`, `VERSION`, hardware/HIL, nRF5340 cleanup, `STATUS.md`, or
  `AGENTS.md`.

`STATUS.md` and `AGENTS.md` will be updated only after hosted proof supplies
final run/job IDs and timings.

## Exact `scripts/test-all.sh` behavior

Add this CLI:

```text
./scripts/test-all.sh
./scripts/test-all.sh --phase unit
./scripts/test-all.sh --phase coverage
./scripts/test-all.sh --phase bsim
./scripts/test-all.sh --phase all
```

Rules:

- No argument remains exactly equivalent to `--phase all`.
- Accept at most one `--phase VALUE` pair.
- Accepted values are exactly `all`, `unit`, `coverage`, `bsim`.
- Missing value, duplicate `--phase`, positional arguments, and unknown options
  fail before environment resolution or suite execution with clear usage/error.
- Validate `TEST_OUTPUT_DIR` before environment resolution as today.
- Resolve NCS and create the private temporary root for every valid phase.
- `unit` runs, in current order:
  1. all inventory-discovered Twister suites;
  2. all inventory-discovered exec-only suites;
  3. all inventory-discovered Python suites.
- `coverage` runs coverage baseline enforcement, then the matrix checker against
  that exact phase's `$COVERAGE_DIR/coverage.json`.
- `bsim` runs the existing Stage 1 runner exactly once.
- `all` runs unit, coverage, matrix, then BSim in the existing order.
- Preserve continue-through-child-failure behavior and final nonzero result when
  any selected child fails.
- Per-phase final summaries must report naturally selected totals: current
  `unit` 69, `coverage` 2, `bsim` 1, `all` 72. Do not hardcode these numbers in
  execution logic.
- Keep suite discovery solely in `scripts/test_inventory.py`; workflow YAML
  must not enumerate suites.
- Keep `TEST_OUTPUT_DIR` and `BSIM_LOG_ROOT` safety behavior unchanged.
- Update script comments/usage to document phase behavior and CI ownership.

## Exact workflow worker behavior

All third-party action references remain at their current immutable SHAs. Keep
top-level `contents: read`, PR-only concurrency cancellation, pinned NCS
v3.3.0/sdk-nrf/toolchain identities, and warning/tool-version checks.

### `test-unit`

- `runs-on: ubuntu-22.04`, host runner, no container, read-only permissions.
- Keep checkout at repository root, disk cleanup, Nix install/cache, NCS
  cache/install, and canonical environment verification.
- Do not run the BSim-specific `west update --group-filter +babblesim` or
  `make -C tools/bsim everything` steps.
- Set an external result root under
  `/home/runner/le-audio-test-results/unit` through `$HOME`, and run inside the
  locked shell:

```bash
./scripts/test-all.sh --phase unit 2>&1 | tee "$TEST_OUTPUT_DIR/test-unit.log"
```

- Preserve the real exit status with `set -o pipefail`.
- Upload results with `if: always()`, seven-day retention, warn when no files,
  and unique artifact name `le-audio-test-unit-${{ github.sha }}`.

### `test-heavy`

- `runs-on: ubuntu-22.04`, host runner, no container, read-only permissions.
- Matrix values exactly `coverage` and `bsim`; `strategy.fail-fast: false`.
- Keep the same pinned checkout, disk cleanup, Nix install/cache, NCS
  cache/install, and canonical environment verification as `test-unit`.
- Set `TEST_PHASE` from `${{ matrix.phase }}` through `env`; never interpolate a
  GitHub expression directly into a shell script.
- Run BSim workspace population and fail-fast component build only when matrix
  phase is `bsim`. Keep exact commands and checks currently used by the
  monolithic job.
- Set phase result root to
  `$HOME/le-audio-test-results/$TEST_PHASE`.
- For BSim only, set `BSIM_LOG_ROOT` to
  `$HOME/le-audio-test-results/bsim/scenarios`. This subdirectory must be empty
  before the runner starts; keep the phase console log outside it.
- Run inside the locked shell:

```bash
./scripts/test-all.sh --phase "$TEST_PHASE" 2>&1 \
  | tee "$TEST_OUTPUT_DIR/test-$TEST_PHASE.log"
```

- Preserve the real exit status with `set -o pipefail`.
- Upload each matrix child's results with `if: always()`, seven-day retention,
  warn when no files, and unique artifact name
  `le-audio-test-${{ matrix.phase }}-${{ github.sha }}`.

### Aggregate `tests`

- `needs: [test-unit, test-heavy]`.
- `if: always()` so the existing required `tests` status is reported even when
  a prerequisite fails or is cancelled.
- Plain `ubuntu-22.04`, five-minute timeout, no checkout, no Nix/NCS setup, no
  container, no artifact download, no outputs, inherited read-only permission.
- Pass prerequisite conclusions into shell through environment variables, not
  direct expression interpolation in `run`.
- Succeed only when `needs.test-unit.result == success` and the aggregate
  matrix job result `needs.test-heavy.result == success`; otherwise print both
  named results and fail nonzero.

### `firmware` and `release`

- Change `firmware` to `needs: test-unit`. Do not otherwise alter firmware
  checkout, container, workspace initialization, build, contract, version,
  package, verification, output, or upload steps.
- Change `release` to `needs: [tests, firmware]`.
- Keep trusted-main guard exactly scoped to push on `refs/heads/main`.
- Keep all references to `needs.firmware.outputs.*` and the exact current-run
  artifact download unchanged.
- A test-unit failure skips firmware. A coverage/BSim failure may allow firmware
  artifact creation, but aggregate `tests` fails and release remains blocked.
- Branch protection remains correct because required contexts stay `tests` and
  `firmware`.

## Required focused tests

Update `scripts/test_firmware_build_ci.py` to prove at least:

1. Exact jobs `test-unit`, `test-heavy`, `tests`, `firmware`, and `release`
   exist once.
2. `test-heavy` has only logical matrix phases coverage/bsim and fail-fast false.
3. Host workers have no container/write permission and carry all current pins.
4. Only BSim variant can run group population/component build.
5. Unit invokes only `--phase unit`; heavy invokes matrix-selected phase; no
   suite names or direct `west build` appear in workflow worker commands.
6. Worker result roots and artifact names are phase-unique, uploads always run,
   and BSim scenario root cannot collide with its console log.
7. Aggregate `tests` uses `always()`, needs both worker jobs, receives results
   via env, and fails unless both are success.
8. Firmware needs only `test-unit`.
9. Release needs both `tests` and `firmware`; trusted-main/write/artifact
   contracts stay unchanged.
10. Existing immutable action references, NCS identities, release restrictions,
    and no-direct-expression-in-run policy remain enforced. Update expected
    action-reference counts to the exact new workflow shape, not a loose lower
    bound.

Extend `TestAllGateOutputRoot` or add a neighboring class using controlled fake
tools/scripts to prove public phase behavior without real builds:

- invalid/missing/duplicate phase fails before NCS resolution;
- omitted phase and explicit `all` select the same dispatch order;
- unit selects Twister, exec-only, and Python only;
- coverage selects coverage then matrix only and passes the produced coverage
  path to the checker;
- bsim selects BSim only;
- selected-phase totals and nonzero-on-child-failure behavior are correct;
- existing unsafe/valid output-root tests remain passing.

Do not rely only on private shell variable assertions or source-string checks
for phase acceptance. Exercise the script boundary with fake commands.

## Verification commands

Run from repository root:

```bash
python3 scripts/test_inventory.py --count
python3 scripts/test_firmware_build_ci.py
python3 tests/unit/test_coverage_runner/test_test_coverage_runner.py
python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py
git diff --check
```

Expected inventory remains 69 unit children and 72 complete gate children.

Then run the complete local gate in the locked development environment:

```bash
./scripts/test-all.sh
```

Required result: `72 PASS / 0 FAIL / 72 TOTAL`, unchanged coverage baseline,
unchanged 17-scenario/26-run BSim matrix, no warning normalized.

If the full gate is impractical because an external prerequisite is genuinely
unavailable, stop and escalate with exact evidence. Do not substitute a partial
result or weaken the gate.

## Commit and recap

After all verification succeeds:

1. Inspect `git status`, `git diff`, and `git log --oneline -10`.
2. Stage only the scoped files above.
3. Commit with concise repository-style message, suggested:

```text
ci: parallelize logical test gates
```

Do not push.

Return one recap containing:

- files changed and observable behavior;
- focused and full-gate commands with exact results;
- final inventory/count evidence;
- commit hash and message;
- final `git status`;
- deviations or blockers.

Stop and report instead of guessing after two materially different failed
attempts, any contradiction with GitHub/NCS/runtime evidence, unexplained
warning/flakiness, need to change baseline/hash/count/product behavior, or scope
expansion. Do not commit incomplete or knowingly failing work.
