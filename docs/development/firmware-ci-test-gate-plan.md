# Firmware CI canonical test gate plan

Status: accepted implementation and plan of record for PR 11. Date:
2026-08-10. Corrected after the first hosted runs; every correction was
validated on the acceptance run below. Historical failed attempts remain
as diagnosis evidence. Hosted run `31422292550` failed pre-gate because
the Nordic container's gcovr (8.6) and gcov first lines did not match
the committed baseline (`gcovr 8.4`, `gcov (GCC) 14.3.0`); `firmware`
and `release` were correctly skipped. The tests job now runs on the
plain host runner inside the locked Nix shell, which provides the exact
flake tools. Hosted run `31424437357` passed Nix, sdk-manager install,
and environment verification but failed the BabbleSim build: `tools/bsim`
had no usable Makefile because the sdk-manager bundle ships the bsim_west
checkout with a dangling `Makefile` symlink to
`components/common/Makefile` and the root group-filter excludes the
`babblesim`-group components; the correction adds a west workspace
population step with `--group-filter +babblesim`. Hosted run
`31426937629` passed the exact Nix/NCS environment checks, the coverage
baseline, the matrix, and the 17-scenario/26-run BabbleSim Stage 1, then
ended `64 PASS / 1 FAIL / 65 TOTAL`: the single failure was the
host-dependent mocked unit test `test_enable_pairing_agent`, which
mocked `subprocess.run` but still launched a real `bt-agent` through
`subprocess.Popen`; the hosted locked Nix shell intentionally lacks
bluez-tools. `firmware` and `release` were correctly skipped; the
process-boundary mocking correction was validated on the acceptance run.
Hosted acceptance run `31432411543` (PR head `32bdc98`) PASSED the full
canonical gate: `tests` job `93598711857` SUCCESS (50m22s) with exact
console summary `Gate complete: 65 PASS / 0 FAIL / 65 TOTAL`; `firmware`
job `93611002998` SUCCESS (6m21s) started only after tests completed,
with both nRF5340 and nRF54L15 builds, build contract, version headers,
packaging, verification, and artifact upload; `release` job
`93612477731` SKIPPED as required on pull_request. PR #11
mergeStateStatus CLEAN; the active ruleset `20658259` (targets
`~DEFAULT_BRANCH`, requires pull request, blocks deletion and
non-fast-forward, no bypass actors,
`strict_required_status_checks_policy=false`) requires status contexts
`tests` and `firmware`. Hosted PR acceptance for this implementation is
complete; protected-main runs and release creation remain separate
operations.

## Release-line narrowing update (2026-09-12)

The PR 11 hosted two-target firmware run is historical evidence. The current
firmware workflow builds, contract-checks, packages, uploads, and prepares
draft releases for nRF54L15 only. Local `scripts/check-build-contract.py`
keeps optional `--nrf5340` validation for legacy local use pending separate
cleanup; it is not part of current GitHub workflow execution.

## Goal

Make every pull request and every protected `main` merge pass the repository's
canonical software gate before the active nRF54L15 release firmware build can
start. A failed test gate must prevent firmware packaging, artifact upload, and
draft-release creation.

## Grounded current state

- `.github/workflows/firmware-build.yml` has one `firmware` job followed by a
  trusted-`main` `release` job. The firmware job initializes an exact NCS v3.3.0
  west workspace in Nordic's digest-pinned toolchain container, then builds
  nRF54L15 only.
- `scripts/test-all.sh` is the canonical software gate and discovers suites
  only through `scripts/test_inventory.py`.
- Current inventory is 40 Twister C suites, 5 exec-only C suites, and 24 Python
  suites. Coverage baseline enforcement, matrix validation, and BabbleSim
  Stage 1 make the public gate total 72 children.
- `scripts/test-coverage.sh` requires a clean exact commit in baseline mode,
  `gcovr 8.4`, gcov 14.3.0, west, Python, and `ZEPHYR_BASE`.
- `scripts/bsim-stage1-run.sh` builds the repository receiver/client and runs
  17 scenarios across 26 simulations. It requires the imported BabbleSim
  components, `bs_2G4_phy_v1`, `nrfutil`, and the NCS toolchain environment.
- NCS v3.3.0's west manifest imports pinned BabbleSim projects through
  `tools/bsim`; `make -C tools/bsim everything` builds the required simulator
  components.
- `scripts/check-build-contract.py` against the real nRF54L15 build tree is a
  post-build contract, not a pre-build test. Its optional `--nrf5340` local
  legacy validation remains pending cleanup. Its own 56-test Python suite is
  already one of the 24 canonical Python children.

## Scope

### In scope

1. Add a distinct `tests` job to `.github/workflows/firmware-build.yml`.
2. Run the canonical gate on the plain `ubuntu-22.04` host runner inside the
   locked repository Nix dev shell (the same flake the local gate uses), with
   the exact NCS v3.3.0 SDK and `911f4c5c26` toolchain installed by
   `nrfutil sdk-manager` 1.16.1.
3. Verify exact `gcovr 8.4` and `gcov (GCC) 14.3.0` first lines, build the
   pinned BabbleSim components with fail-fast behavior, then run
   `scripts/test-all.sh` once.
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

The `tests` job runs on the plain `ubuntu-22.04` host runner (no Nordic
container): the repository's locked Nix dev shell provides the exact toolchain
tools (gcovr 8.4, gcov 14.3.0, nrfutil, west), and `nrfutil sdk-manager` owns
the exact NCS v3.3.0 installation. It does not share mutable build state
with `firmware` and does not checkout sdk-nrf separately:

- checkout application at the repository root, `fetch-depth: 0`, credentials
  disabled;
- free disk space early (grounded in the accepted serial-mcp hosted
  native-sim pattern): remove only well-known preinstalled toolchain caches
  (`/usr/share/dotnet`, `/usr/local/lib/android`, `/opt/ghc`,
  `/opt/hostedtoolcache/CodeQL`, docker image/builder prunes) because the
  Nix closure (~4.5 GiB) plus the NCS SDK/toolchain (~4.6 GiB) plus retained
  native build trees exceed the ephemeral runner disk; no project or user
  data path is touched;
- install Nix with the pinned `DeterminateSystems/nix-installer-action`, cache
  the Nix store with the pinned `nix-community/cache-nix-action` keyed from
  `flake.lock` with a bounded `gc-max-store-size` (6G, grounded in the measured
  dev-shell closure), and cache `/home/runner/ncs` with the pinned
  `actions/cache` keyed `ncs-v3.3.0-911f4c5c26` and `id: cache-ncs`;
- consume `nix-nrf-dev`'s default `nrfutil` supply: Nixpkgs core plus exact
  `nrfutil sdk-manager` 1.16.1 from a versioned Nordic archive fixed by its
  Nix SHA-256. The receiver does not override `mkNrfShell`'s package; before
  any NCS installation, CI checks `nrfutil sdk-manager --version` for 1.16.1
  and never downloads or PATH-injects nrfutil;
- install the SDK through the locked shell, setting the install directory on
  every run and branching on the NCS cache step's exact `cache-hit` output
  (`CACHE_HIT: ${{ steps.cache-ncs.outputs.cache-hit }}`), never on directory
  presence alone: on an exact hit, require `$HOME/ncs/v3.3.0/nrf` to exist
  and skip install; otherwise run `nrfutil sdk-manager install v3.3.0`;
- populate the installed workspace through the locked shell, from
  `$HOME/ncs/v3.3.0`, asserting `west topdir` equality first, then running
  `west update --narrow -o=--depth=1 --group-filter +babblesim`, and
  requiring `$HOME/ncs/v3.3.0/tools/bsim/Makefile` to resolve (`test -f`
  follows the symlink). The `+babblesim` re-enable is required because the
  sdk-manager bundle carries the bsim_west checkout but the root manifest
  group-filter excludes the `babblesim` group, so the bundle's
  `tools/bsim/Makefile` (a symlink to `components/common/Makefile`) is
  dangling until west fetches the components. Every fetched revision stays
  pinned by the imported bsim manifest; no floating clones or ad hoc BSim
  URLs are used;
- verify through the locked shell and fail closed: `gcovr` first line `gcovr 8.4`,
  `gcov` first line `gcov (GCC) 14.3.0`, `ZEPHYR_BASE` resolves to
  `$HOME/ncs/v3.3.0/zephyr` (the shell hook derives it from the sdk-manager
  toolchain env with a `$HOME/ncs` fallback), sdk-nrf HEAD is exactly
  `ba167d9f3db4abbdc9b67887ca3ea66c64f2d956`, `nrf/VERSION` is exactly `3.3.0`,
  and `nrfutil sdk-manager toolchain env --ncs-version v3.3.0` output contains
  toolchain ID/path `911f4c5c26`.

The committed baseline intentionally requires the Nix-flake tools; weakening
tool-version enforcement or changing the baseline is forbidden.

Build imported BabbleSim components before the gate:

```bash
BSIM_BUILD_FAIL_ASAP=1 make -C "$HOME/ncs/v3.3.0/tools/bsim" everything
test -x "$HOME/ncs/v3.3.0/tools/bsim/bin/bs_2G4_phy_v1"
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
- CI invokes the gate through the locked shell with both variables pointing
  at `$HOME/le-audio-test-results` (the host runner home; no container path
  translation). Full console output is teed to `test-all.log` while
  preserving the gate's real exit status through `set -o pipefail`.
- Upload `/home/runner/le-audio-test-results/` with the already pinned
  `actions/upload-artifact` action, `if: always()`, seven-day retention, and
  `if-no-files-found: warn`.

Expected retained evidence:

- `test-all.log`;
- coverage JSON, summaries, text, HTML, traces, and logs;
- receiver, client, and PHY logs for every BSim scenario/run.

### 4. Executable workflow contract

Extend `scripts/test_firmware_build_ci.py` to prove public workflow behavior:

- exactly one `tests` job exists, on the host runner (no container) with the
  pinned runner/shell and read-only permissions;
- checkout at the repository root only, with the exact Nix installer, Nix
  store cache, and NCS cache pins;
- Nix-managed sdk-manager 1.16.1 supply (versioned URL, fixed SHA-256, no CI
  PATH injection) and locked-shell NCS install;
- exact gcovr/gcov/ZEPHYR_BASE/sdk-HEAD/VERSION/toolchain-ID verification and
  fail-fast BabbleSim build exist;
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
after hosted proof. Hosted proof exists: acceptance run `31432411543`
passed the full gate, and the active ruleset `20658259` now requires both
`tests` and `firmware` status contexts on pull requests; `release` remains
unrequired because it skips on pull requests.

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
baseline and BSim pins. Hosted PR acceptance is recorded:

1. `tests` starts and passes. Historical failed attempts are diagnosis
   evidence only: `31422292550` failed pre-gate on the incompatible
   container gcov first-line assertion, `31424437357` failed the
   BabbleSim build on the dangling `tools/bsim/Makefile` symlink, and
   `31426937629` passed the exact Nix/NCS environment, coverage
   baseline, matrix, and BSim Stage 1 but ended
   `64 PASS / 1 FAIL / 65 TOTAL` solely because
   `test_enable_pairing_agent` launched a real `bt-agent` through an
   unmocked `subprocess.Popen` (the locked shell has no bluez-tools).
   The acceptance run `31432411543` passed with exact console summary
   `Gate complete: 65 PASS / 0 FAIL / 65 TOTAL` (tests job
   `93598711857`).
2. `firmware` starts only after `tests` passes: on run `31432411543`
   the firmware job `93611002998` started after tests completed and
   SUCCEEDED (both builds, build contract, packaging, upload).
3. Pull request never runs `release`: the release job `93612477731` was
   SKIPPED as required on the acceptance run.
4. The executable workflow contract in `scripts/test_firmware_build_ci.py`
   pins `firmware: needs: tests`; hosted ordering on run `31432411543`
   observed `tests` passing before `firmware` started. No forced hosted test
   failure is required: the `needs` edge is proven by the static workflow
   contract, not by a runtime experiment, and no production source is
   weakened.

After merge, protected-main acceptance requires the same
`tests -> firmware -> release` dependency chain. Hardware acceptance remains a
separate FR4 operation on exact draft assets.
