# Firmware CI canonical test gate plan

Status: implementation plan for PR 11. Date: 2026-08-10.

## Goal

Make every pull request and every protected `main` merge pass the repository's
canonical software gate before either production receiver firmware build can
start. A failed test gate must prevent firmware packaging, artifact upload, and
draft-release creation.

## Grounded current state

- `.github/workflows/firmware-build.yml` has one `firmware` job followed by a
  trusted-`main` `release` job. The firmware job initializes an exact NCS v3.3.0
  west workspace in Nordic's digest-pinned toolchain container, then builds
  nRF5340 and nRF54L15.
- `scripts/test-all.sh` is the canonical software gate and discovers suites
  only through `scripts/test_inventory.py`.
- Current inventory is 35 Twister C suites, 5 exec-only C suites, and 22 Python
  suites. Coverage baseline enforcement, matrix validation, and BabbleSim
  Stage 1 make the public gate total 65 children.
- `scripts/test-coverage.sh` requires a clean exact commit in baseline mode,
  `gcovr 8.4`, gcov 14.3.0, west, Python, and `ZEPHYR_BASE`.
- `scripts/bsim-stage1-run.sh` builds the repository receiver/client and runs
  17 scenarios across 26 simulations. It requires the imported BabbleSim
  components, `bs_2G4_phy_v1`, `nrfutil`, and the NCS toolchain environment.
- NCS v3.3.0's west manifest imports pinned BabbleSim projects through
  `tools/bsim`; `make -C tools/bsim everything` builds the required simulator
  components.
- `scripts/check-build-contract.py` against real nRF5340/nRF54L15 build trees is
  a post-build contract, not a pre-build test. Its own 52-test Python suite is
  already one of the 22 canonical Python children.

## Scope

### In scope

1. Add a distinct `tests` job to `.github/workflows/firmware-build.yml`.
2. Use the same runner, digest-pinned Nordic toolchain container, Bash shell,
   exact application checkout, exact sdk-nrf commit, and west initialization
   contract as the existing firmware job.
3. Install exact `gcovr==8.4` in the test job, verify `gcovr` and `gcov` first
   lines, build the pinned BabbleSim components with fail-fast behavior, then
   run `scripts/test-all.sh` once.
4. Preserve canonical test output outside the checkout and upload it with
   `if: always()` so failures remain diagnosable.
5. Make `firmware` depend on successful `tests` completion.
6. Extend workflow contract tests so topology, isolation, artifact retention,
   and release gating cannot regress silently.
7. Update CI/release documentation to state the hosted pre-build gate and its
   explicit boundary.

### Out of scope

- Physical hardware, flashing, UART, pairing, audible-output, BlueZ/PipeWire,
  FLPR fault-injection, and FR4 exact-artifact acceptance runs. Hosted runners
  have no lab hardware.
- `fw-build-dongle`. It is a production build, not a canonical gate child.
- Moving the resolved production build contract before firmware compilation.
- Coverage-baseline changes, BSim hash changes, scenario repinning, warning
  suppression, test weakening, or count hardcoding as a substitute for shared
  inventory discovery.
- Automatic public release publication.

## Implementation decisions

### 1. Workflow topology

Edit `.github/workflows/firmware-build.yml`:

```text
tests -> firmware -> release (trusted main only)
```

- `tests` runs on every existing workflow event: PR, protected `main` push, and
  manual dispatch.
- `firmware` declares `needs: tests`; normal GitHub job semantics skip it when
  tests fail or are cancelled.
- `release` continues to declare `needs: firmware`, so no write-capable release
  step can run unless both tests and firmware passed.
- Keep top-level permissions read-only. Only the existing trusted-main release
  job receives `contents: write`.

### 2. Test workspace and prerequisites

The `tests` job duplicates the existing verified checkout/workspace sequence
rather than sharing mutable build state with `firmware`:

- checkout application at `workspace/le-audio-receiver`, `fetch-depth: 0`,
  credentials disabled;
- checkout sdk-nrf at exact commit
  `ba167d9f3db4abbdc9b67887ca3ea66c64f2d956`;
- `west init -l nrf`, `west update --narrow -o=--depth=1`, and
  `west zephyr-export`;
- verify sdk HEAD, NCS `3.3.0`, west topdir, and Zephyr directory before
  publishing `ZEPHYR_BASE` through `$GITHUB_ENV`.

Install `gcovr==8.4` only in the unprivileged test-job Python environment. Do
not alter the firmware job or repository dependencies. Verify:

```text
gcovr 8.4
gcov (GCC) 14.3.0
```

Build imported BabbleSim components before the gate:

```bash
BSIM_BUILD_FAIL_ASAP=1 make -C "$GITHUB_WORKSPACE/workspace/tools/bsim" everything
test -x "$GITHUB_WORKSPACE/workspace/tools/bsim/bin/bs_2G4_phy_v1"
```

No component build failure may be normalized by BabbleSim's default
continue-on-error behavior.

### 3. Canonical gate invocation and retained evidence

Add optional output-root support without changing gate discovery or results:

- `scripts/test-all.sh` accepts `TEST_OUTPUT_DIR` as an absolute directory
  outside the repository. When set, coverage writes to
  `$TEST_OUTPUT_DIR/coverage`; otherwise current temporary behavior remains.
- `scripts/bsim-stage1-run.sh` accepts `BSIM_LOG_ROOT` as an absolute output
  directory. When set, it must use that directory instead of `mktemp`, refuse
  a nonempty destination, preserve logs, and never delete caller-owned output.
  Existing local behavior remains unchanged when unset.
- CI invokes the gate with both variables under `${{ runner.temp }}` and tees
  full console output to `test-all.log` while preserving the gate's real exit
  status through `set -o pipefail`.
- Upload `${{ runner.temp }}/le-audio-test-results/` with the already pinned
  `actions/upload-artifact` action, `if: always()`, seven-day retention, and
  `if-no-files-found: warn`.

Expected retained evidence:

- `test-all.log`;
- coverage JSON, summaries, text, HTML, traces, and logs;
- receiver, client, and PHY logs for every BSim scenario/run.

### 4. Executable workflow contract

Extend `scripts/test_firmware_build_ci.py` to prove public workflow behavior:

- exactly one `tests` job exists and uses the pinned runner/container/shell;
- test checkout and NCS initialization retain exact pins and identity checks;
- exact gcovr/gcov checks and fail-fast BabbleSim build exist;
- canonical invocation is `scripts/test-all.sh`, not copied suite lists;
- test artifacts upload with `if: always()`;
- `firmware` has `needs: tests`;
- `release` still has `needs: firmware` and trusted-main guard;
- no PR path gains write permission;
- existing version-driven package/release contracts remain intact.

Add focused Python tests for new runner output-root behavior. These tests use
temporary directories and fake tools; they must not execute real Zephyr or
BabbleSim builds.

### 5. Documentation and required checks

Update `docs/development/firmware-release-plan.md`, `STATUS.md`, and `AGENTS.md`
after hosted proof. Before hosted proof, describe implementation as pending PR
validation, not accepted. Once PR checks exist, branch protection should require
both `tests` and `firmware`; `release` remains unrequired because it skips on
pull requests.

## Verification

Focused local checks:

```bash
python3 scripts/test_inventory.py --count
python3 scripts/test_firmware_build_ci.py
python3 tests/unit/test_coverage_runner/test_test_coverage_runner.py
python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py
git diff --check
```

Expected inventory count is 62 unit children. The complete local acceptance
command remains:

```bash
./scripts/test-all.sh
```

Expected gate result is `65 PASS / 0 FAIL / 65 TOTAL` with unchanged coverage
baseline and BSim pins. Hosted PR acceptance requires observable ordering:

1. `tests` starts and passes.
2. `firmware` starts only after `tests` passes.
3. Pull request never runs `release`.
4. A forced test failure in workflow contract fixtures proves firmware would
   be skipped by the `needs` edge; no production source is weakened to create
   this proof.

After merge, protected-main acceptance requires the same
`tests -> firmware -> release` dependency chain. Hardware acceptance remains a
separate FR4 operation on exact draft assets.
