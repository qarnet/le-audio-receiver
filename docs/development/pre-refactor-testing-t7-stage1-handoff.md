# Phase T7 Stage 1 handoff — honest coverage instrumentation and report

## Goal

Build trustworthy coverage and classification infrastructure, then produce the
first honest native production-source report. Do not set thresholds, claim T7
acceptance, or integrate coverage into `test-all.sh` until Thinker reviews the
measured gaps.

Start from clean `test/pre-refactor-behavior` at `e8b3c6f`.

## Scope

In scope:

- Add gcovr 8.x to development shell.
- Add direct no-op timing suite for `audio_timing_none.c`.
- Add `scripts/test-coverage.sh` with report-only mode.
- Add stdlib-only production test manifest and `scripts/check-test-matrix.py`.
- Add focused Python tests for both scripts/checkers.
- Produce first honest line/branch/function report and gap inventory.
- Commit completed Stage 1 tooling/tests and return evidence.

Out of scope for this stage:

- Choosing or committing line/branch thresholds/baseline.
- Adding broad gap-closing tests found by report.
- Canonical `test-all.sh` coverage/checker children.
- BSim gcov aggregation.
- T7 acceptance docs/status.
- T8 hardware.
- Push, merge, PR, amend, force-push.

## 1. Toolchain

Add `pkgs.gcovr` to `flake.nix` packages without changing NCS version or Python
dependency ownership. Current nixpkgs resolves gcovr 8.4. `flake.lock` should
not change merely for a package already in pinned nixpkgs; inspect diff.

Verify inside repo dev shell:

```bash
gcovr --version
gcov --version
```

Do not install with pip or modify global environment.

## 2. Direct no-op timing suite

Create auto-discovered `tests/unit/timing_none/` compiling real
`src/audio_timing_none.c`. Cover `init()==0`, repeated init, zero/extreme SDU
timestamp and presentation-delay inputs, repeated reset, and arbitrary call
order. All functions must execute. This closes a host-testable gap rather than
classifying a trivial module as hardware-only.

## 3. Coverage runner

Create executable `scripts/test-coverage.sh`, bash strict mode, with:

```text
--report-only       allow report generation without committed baseline
--output DIR        report/artifact directory
--keep-builds       optional debugging only
```

Default later will enforce committed baseline, but in Stage 1 only
`--report-only` is required. Unknown flags/missing tools/environment fail.

### Suites

Discover every `tests/unit/*/testcase.yaml` Twister/native suite exactly as
`test-all.sh` does, plus the four explicit exec-only suites:

- `audio_offload`
- `flpr_audio_process`
- `flpr_ring`
- `offload_asrc`

Build every suite into a unique temporary directory with
`CONFIG_COVERAGE=y`; do **not** set `CONFIG_COVERAGE_GCOV` or
`CONFIG_COVERAGE_DUMP` (invalid for native builds). Run each native executable
to normal exit so host libgcov writes `.gcda`. Preserve warnings-as-errors.

Fail immediately if a suite fails, no `.gcno` appears, or expected runtime
`.gcda` is absent. No skipped suite.

### Per-build collection and merge

Use gcovr 8.x in two phases:

1. Per build directory, emit JSON trace using repo root, host `gcov`, that
   build as object directory, `--merge-mode-functions=separate`, and filters
   limited to production files beneath repo `src/`.
2. Merge all traces with `--add-tracefile` into final reports.

Final output directory must contain deterministic names:

- `coverage.json` (full gcovr JSON with function records)
- `coverage-summary.json`
- `coverage.txt`
- `html/index.html` plus details
- `run-manifest.json` listing exact suites, commands/status, source commit,
  gcov/gcovr versions, and exclusions

Use gcovr parse-error flags only for documented gcov metadata quirks:

- `negative_hits.warn_once_per_file`
- `suspicious_hits.warn_once_per_file`

Do not ignore all errors. Test source, mocks, SDK, generated build files, and
system headers must not enter production percentages.

### Honest numeric population

Native numeric threshold population includes host-testable production modules
under `src/`, excluding only:

- `src/main.c` — hardware wiring adapter; coordinator tested directly;
- `src/bt_bap.c` — integration-only through accepted T4 BabbleSim matrix;
- `src/flpr/main.c` — RISC-V VPR hardware-only;
- `src/audio_clock_actuator_sample_adjust.c` — historical/retired, not
  production-selectable.

Do not exclude low-coverage direct modules. `dongle/hci_ipc/src/main.c` belongs
in classification manifest but is outside `src/` numeric population and is
hardware/build-only.

If a supposedly included source never appears in merged report, report it as a
hard Stage 1 gap; do not silently omit it.

## 4. Test matrix manifest and checker

Use committed stdlib JSON, not YAML/PyYAML:

```text
tests/test-matrix.json
scripts/check-test-matrix.py
tests/unit/test_matrix/test_check_test_matrix.py
```

### Inventory

Checker inventories:

- every `src/**/*.c`;
- `dongle/hci_ipc/src/main.c`;
- relevant production static-inline header `src/flpr_protocol.h`.

Manifest must have exactly one entry per inventory item: no missing, duplicate,
or stale path.

### Schema

Each entry includes:

```json
{
  "source": "src/audio_asrc.c",
  "classification": "direct",
  "suites": [
    {"name": "asrc", "evidence": "direct"}
  ],
  "public_outcomes": [
    {"api": "audio_asrc_process", "outcome": "success", "witness": "test_name"},
    {"api": "audio_asrc_process", "outcome": "error-class", "witness": "test_name"}
  ],
  "state_transitions": [
    {"transition": "old->new", "witness": "test_name"}
  ],
  "function_exclusions": [],
  "hardware_acceptance": []
}
```

Allowed classifications:

- `direct`
- `integration-only`
- `delegated-glue`
- `hardware-only`
- `historical-retired`
- `header-structural`

Allowed evidence labels follow `docs/testing/coverage-matrix.md`: direct,
integration, build, hardware, structural, historical. Never call compile-only
or copied models direct.

Checker validates:

1. exact source inventory completeness;
2. suite names resolve to existing unit directories, accepted `bsim:stage1`,
   production build commands, or named hardware scripts;
3. direct sources have at least one direct suite;
4. integration/delegated/hardware entries carry explicit reason and one or more
   concrete acceptance commands/evidence paths;
5. historical source has only historical evidence and is marked excluded from
   numeric coverage;
6. header structural entry names structural suite;
7. each manifest witness string exists in referenced test source (no invented
   witness);
8. each listed public API exists in its production source/header;
9. no empty outcome/transition placeholders;
10. when `--coverage-json PATH` is supplied, every numeric-population direct
    source appears and every compiled function executes at least once, unless a
    precise function exclusion includes reason + hardware/structural evidence.
    Group duplicate variant records by source/function; any executed variant
    satisfies execution, but a zero-hit alternate variant must remain visible
    in report.

`check-test-matrix.py` emits deterministic all-errors output and nonzero exit.
Support `--manifest`, `--repo-root`, and optional `--coverage-json` for fixture
tests. Stdlib only.

Manifest content must reflect current T4 matrix, not old one-scenario BSim
descriptions. Use `docs/testing/coverage-matrix.md` and actual CMake source lists
as truth. Hardware-only acceptance commands must exist in repo; do not write
generic prose where executable command is available.

### Checker tests

Use temporary mini repositories/fixtures to cover valid manifest plus missing,
duplicate, stale, bad classification/evidence, nonexistent suite/command,
invented API/witness, direct-without-direct-suite, hardware-without acceptance,
historical counted numerically, absent coverage source, zero-hit function,
duplicate variant any-hit behavior, and deterministic multiple-error output.

## 5. First honest report

Run:

```bash
./scripts/test-coverage.sh --report-only --output /tmp/t7-coverage
python3 scripts/check-test-matrix.py
python3 scripts/check-test-matrix.py \
  --coverage-json /tmp/t7-coverage/coverage.json
```

Do not fix discovered function/line/branch gaps beyond `timing_none` in this
stage. Return:

- exact overall covered/total/percent for lines, branches, and functions;
- per-production-file line/branch/function numbers;
- every included file missing from report;
- every zero-hit production function;
- manifest/API/outcome/transition failures;
- report paths and runtime;
- exact suite list and any gcovr warnings.

Commit completed tooling, manifest, unit tests, no-op timing suite, and this
handoff before returning. Do not commit `/tmp` reports or threshold baseline.
Leave worktree clean.

## Verification

Focused:

```bash
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/timing_none tests/unit/timing_none -p -t run
python3 tests/unit/test_matrix/test_check_test_matrix.py
bash -n scripts/test-coverage.sh
python3 -m py_compile scripts/check-test-matrix.py
```

No full canonical gate/build/hardware requirement in Stage 1 because behavior
changes only add a direct no-op test and tooling. Stage 2 will close measured
gaps, commit baseline, integrate gate, and run full acceptance.

## Escalation

Stop after two materially different failed attempts or any gcovr/Zephyr
coverage semantic contradiction. Do not set guessed thresholds, exclude a
low-coverage direct file, mark a host-testable function hardware-only to pass,
weaken manifest checks, suppress broad gcov errors, install tools outside Nix,
or keep guessing. Return exact commands, versions, logs, artifacts, git status,
one question, and smallest hypothesis. Do not commit knowingly broken tooling.
