# FR2 handoff: reproducible firmware build CI

Date: 2026-08-09

## Goal

Implement FR2 from `docs/development/firmware-release-plan.md`: add one
least-privilege GitHub Actions workflow that checks out the exact NCS 3.3.0
source in the exact Nordic toolchain container, builds both production receiver
targets, verifies resolved build and application-version contracts, packages
the FR1 factory tuples, verifies checksums, and uploads exactly the three
release-set files as an immutable workflow artifact.

Selected project version: **0.1.0**. FR2 remains pending until this workflow
runs successfully on GitHub-hosted Actions. This handoff lands locally
verifiable implementation only; do not claim hosted acceptance and do not
begin FR3.

## User-observable behavior

- Pull requests, pushes to `main`, and manual dispatch run the firmware build.
- Successful runs expose one workflow artifact named
  `firmware-v0.1.0-<full github.sha>` for 14 days.
- Downloaded artifact contains exactly:
  - `le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip`
  - `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip`
  - `SHA256SUMS`
- Both target ZIPs describe the checked-out application commit, NCS `v3.3.0`,
  and project version `0.1.0` through the accepted FR1 manifest contract.
- Any checkout mismatch, NCS mismatch, version mismatch, build failure,
  build-contract failure, packaging failure, checksum failure, missing file, or
  upload failure makes the job fail. No partial artifact is accepted.

## In scope

- Root Zephyr `VERSION` for project version 0.1.0.
- One stdlib-only project-version reader and public-boundary tests.
- One GitHub Actions workflow plus static contract tests for load-bearing pins,
  permissions, commands, and artifact paths.
- Both existing production build helpers.
- Existing 95-assertion resolved build-contract checker.
- Existing FR1 packager and checksum verification.
- Current test-inventory comments/docs changed by one new Python test child.
- Local implementation commit, production build verification, package smoke,
  and clean canonical gate.

## Out of scope

- `v*` tag triggers, tag/version consistency, GitHub Release creation,
  `contents: write`, release attachments, provenance attestations, or draft
  publication. FR3 owns these.
- Hardware flashing or acceptance. FR4 owns exact draft-asset acceptance.
- Final public flashing commands or release publication. FR5 owns these.
- MCUboot, image signing, DFU, keys, rollback, or companion-update design.
- Dongle build, J-Link installation, flashing tools, hardware access, secrets,
  caches, matrix jobs, partial target selection, or raw build-tree uploads.
- Changing production firmware behavior, board/Kconfig/devicetree settings,
  build output mapping, FR1 archive contract, or build helpers.
- Pushing, opening a PR, or claiming hosted-CI success in this implementation
  handoff. Orchestrator will request explicit user approval before remote work.

## Grounding and immutable pins

Use these exact values:

| Dependency | Human version | Immutable reference |
|---|---|---|
| Runner | Ubuntu | `ubuntu-22.04` |
| Nordic toolchain container | `sdk-nrf-toolchain:v3.3.0` | `ghcr.io/nrfconnect/sdk-nrf-toolchain@sha256:f24d8932ff081ebcd8da9c248f4449bdabe461c0620a7a4ac9e95eb577ba2276` |
| `actions/checkout` | `v7.0.1` | `3d3c42e5aac5ba805825da76410c181273ba90b1` |
| `actions/upload-artifact` | `v7.0.1` | `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a` |
| sdk-nrf | annotated tag `v3.3.0` peeled commit | `ba167d9f3db4abbdc9b67887ca3ea66c64f2d956` |

Grounding evidence:

- `docker manifest inspect --verbose` resolves the pinned container digest as
  Linux/amd64.
- Installed NCS 3.3.0
  `nrf/scripts/docker/README.rst:43-45,67-96` says toolchain variables load
  only in Bash and GitHub Actions overrides the image entrypoint, so job
  `defaults.run.shell` must be `bash`.
- The same Nordic document installs J-Link only when
  `ACCEPT_JLINK_LICENSE=1`; this build-only job must not set that variable.
- `git ls-remote` maps the `v3.3.0` annotated tag to peeled sdk-nrf commit
  `ba167d9...`. Checkout the commit, not a mutable tag, then verify HEAD and
  `nrf/VERSION` (`3.3.0`) before `west update`.
- Installed workspace `.west/config` uses manifest path `nrf`, file
  `west.yml`; official NCS setup is `west init -l nrf`, `west update`, and
  `west zephyr-export`.
- `nrf/west.yml` pins Zephyr to `ncs-v3.3.0` and exact module revisions.
- Existing build helpers produce FR1's four canonical inputs:
  `scripts/bin/fw-build-5340`, `scripts/bin/fw-build-54l15`.
- Existing checker invocation is documented by
  `scripts/check-build-contract.py:15-18`.
- Zephyr 3.3.0 source documentation
  `zephyr/doc/build/version/index.rst:22-59` defines the five-field root
  application `VERSION`; `zephyr/CMakeLists.txt:736-759` generates
  `<image>/zephyr/include/generated/zephyr/app_version.h` from it.

## Root VERSION and version reader

### `VERSION`

Add exact content:

```text
VERSION_MAJOR = 0
VERSION_MINOR = 1
PATCHLEVEL = 0
VERSION_TWEAK = 0
EXTRAVERSION =
```

### `scripts/project-version.py`

Add a stdlib-only executable CLI:

```console
python3 scripts/project-version.py
python3 scripts/project-version.py --version-file /path/to/VERSION
```

Behavior:

- default file is repository-root `VERSION`, resolved relative to script path,
  not caller CWD;
- explicit `--version-file` exists for tests and future tooling;
- read a regular, non-symlink, nonempty UTF-8 file;
- accept the five Zephyr keys exactly once; allow blank lines and `#` comments,
  but reject unknown non-comment lines, duplicate keys, missing keys, and
  malformed assignments;
- numeric values must use canonical unsigned decimal (no leading zeros except
  `0`) and fit Zephyr's 0..255 range;
- require `VERSION_TWEAK = 0` and empty `EXTRAVERSION` because public artifact
  versions use the FR1 canonical `MAJOR.MINOR.PATCH` contract;
- print exactly `0.1.0\n` for repository `VERSION` and nothing to stderr;
- caller/file errors print one
  `project-version: error: <reason>` line, no traceback, and return nonzero;
- no environment, Git, timestamps, host paths, or NCS state affect output.

Keep parsing logic local to this tool. Do not import private FR1 packager
helpers or weaken FR1's explicit `--version` interface.

## Workflow

Add `.github/workflows/firmware-build.yml` with this exact shape.

### Events and permissions

- `name: Firmware build`
- triggers:
  - `pull_request`
  - pushes to branch `main` only
  - `workflow_dispatch`
- top-level `permissions: contents: read`
- no `pull_request_target`, `workflow_run`, secrets, write permission, OIDC,
  environments, or privileged container options;
- concurrency group per workflow/ref with `cancel-in-progress: true`;
- one job named `firmware`, `runs-on: ubuntu-22.04`, timeout 120 minutes;
- pinned Nordic container digest from the table;
- job-wide `defaults.run.shell: bash`;
- never set `ACCEPT_JLINK_LICENSE`.

### Checkout and workspace

Use SHA-pinned `actions/checkout` twice:

1. Checkout triggering application revision to
   `workspace/le-audio-receiver`, with `fetch-depth: 0` and
   `persist-credentials: false`.
2. Checkout public `nrfconnect/sdk-nrf` at exact commit `ba167d9...` to
   `workspace/nrf`, with `persist-credentials: false`.

Do not checkout fork code through a privileged event. Normal `pull_request`
has read-only permissions and no secrets.

Initialize from `$GITHUB_WORKSPACE/workspace`:

```bash
test "$(git -C nrf rev-parse HEAD)" = "ba167d9f3db4abbdc9b67887ca3ea66c64f2d956"
test "$(tr -d '\r\n' < nrf/VERSION)" = "3.3.0"
west init -l nrf
west update --narrow -o=--depth=1
west zephyr-export
```

After update, repeat sdk-nrf HEAD/version checks and assert
`west topdir` equals `$GITHUB_WORKSPACE/workspace`. Do not run `apt`, `pip`,
J-Link installers, or downloaded scripts.

### Project identity

In `workspace/le-audio-receiver`, a step with `id: project-version` must:

- assert `git rev-parse HEAD` equals `$GITHUB_SHA`;
- run `python3 scripts/project-version.py`;
- assert output is `0.1.0`;
- write `version=0.1.0` to `$GITHUB_OUTPUT` using the validated value.

Never interpolate unvalidated event text into shell commands.

### Build and verify

From application root, run sequential named steps:

```bash
./scripts/bin/fw-build-5340
./scripts/bin/fw-build-54l15
```

Then run:

```bash
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
```

Require exact generated application version in both production app images by
checking these files contain
`#define APP_VERSION_STRING           "0.1.0"` (spacing may be matched with a
regex rather than assumed):

- `build/nrf5340/le-audio-receiver/zephyr/include/generated/zephyr/app_version.h`
- `build/nrf54l15/le-audio-receiver/zephyr/include/generated/zephyr/app_version.h`

The FLPR and cpunet companion applications have separate source directories;
do not claim their application version headers derive from root `VERSION`.
Tuple compatibility is expressed by the single FR1 package manifest.

### Package, verify, upload

Create only `dist` through the FR1 packager:

```bash
version="$(python3 scripts/project-version.py)"
python3 scripts/package-firmware-release.py \
  --version "$version" \
  --git-commit "$GITHUB_SHA" \
  --ncs-version v3.3.0 \
  --build-root build \
  --output-dir dist
```

Verify:

- `dist` has exactly three regular top-level files and no symlinks;
- names are the two exact v0.1.0 ZIPs plus `SHA256SUMS`;
- `(cd dist && sha256sum --strict -c SHA256SUMS)` succeeds;
- each ZIP passes `python3 -m zipfile --test`.

Upload one artifact with SHA-pinned `actions/upload-artifact`:

- name: `firmware-v${{ steps.project-version.outputs.version }}-${{ github.sha }}`
- path lists the three exact files individually, no wildcard and no build tree;
- `if-no-files-found: error`
- `retention-days: 14`
- `compression-level: 0` (factory ZIPs are already compressed)
- `overwrite: false`
- do not use `archive: false`, which supports only one file.

## Public-boundary tests

Add `scripts/test_firmware_build_ci.py` as one new Python gate child. It may
use stdlib subprocess/tempfile/text/regex tools only.

Tests must prove observable/config behavior, not private helper calls:

1. Repository `project-version.py` prints exactly `0.1.0`, empty stderr, exit 0.
2. Valid explicit five-field fixture prints its canonical semantic version.
3. Missing/unreadable/symlink/empty/non-UTF-8 version files fail cleanly.
4. Missing, duplicate, unknown, malformed, leading-zero, >255 numeric fields,
   nonzero tweak, and nonempty extraversion fail cleanly with no traceback.
5. Workflow event/permission contract: normal PR, main push, manual dispatch,
   read-only contents; no privileged trigger or write permission.
6. Workflow pins exact runner, container digest, Bash default, action SHAs, and
   sdk-nrf commit; no mutable `uses:` reference and no J-Link acceptance.
7. Workflow contains exact workspace setup, both production helper commands,
   95-assertion checker invocation, both generated app-version checks, and FR1
   packager metadata inputs.
8. Workflow checksum/ZIP verification and upload contract lists only the three
   exact files with version+commit artifact name and decided retention/options.

Static workflow tests are appropriate here because workflow YAML is the public
declarative behavior and hosted execution is unavailable before remote push.
Do not add a third-party YAML parser or duplicate a general YAML implementation.
Use focused anchored regex/string checks and useful failure messages.

Adding this file changes inventory from 20 to 21 Python children and canonical
composition from 63 to 64 total.

## Active inventory updates in implementation commit

Update current, non-historical inventory only:

- `scripts/test-all.sh`: 21 Python, 61 unit children, 64 canonical children.
- `docs/testing/coverage-matrix.md`: Python count 21, add
  `firmware_build_ci (test_firmware_build_ci.py)`, total 64. Before hosted/local
  acceptance is recorded, retain the paragraph's prior accepted 63/63 evidence
  as historical FR1 evidence; do not falsely claim FR2 acceptance.

Do not update `AGENTS.md`, `STATUS.md`, or `docs/design.md` current gate facts
until clean local verification is observed and hosted CI later passes.

## Focused verification and implementation commit

Run:

```bash
python3 scripts/project-version.py
python3 scripts/test_firmware_build_ci.py
python3 scripts/test_package_firmware_release.py
python3 scripts/test_inventory.py --python
python3 scripts/check-test-matrix.py
git diff --check
```

Expected inventory: 35 Twister + 5 exec-only + 21 Python = 61 unit children;
64 canonical children after coverage, matrix, and BSim.

Inspect `git status`, `git diff`, and `git log --oneline -10`. Stage only:

- `docs/development/firmware-release-fr2-handoff.md`
- `VERSION`
- `scripts/project-version.py`
- `scripts/test_firmware_build_ci.py`
- `.github/workflows/firmware-build.yml`
- `scripts/test-all.sh`
- `docs/testing/coverage-matrix.md`

Commit:

```text
ci: build and package receiver firmware
```

Do not include unobserved results or mark FR2 accepted in this commit.

## Clean local verification after commit

At clean implementation commit:

1. Run `./scripts/test-all.sh`; require 64 PASS / 0 FAIL / 64 TOTAL and no
   unexplained warning.
2. Run `fw-build-5340` and `fw-build-54l15` from the repository root in the
   NCS 3.3.0 dev shell. Both must complete with no new/actionable warning.
3. Run the exact 95-assertion build-contract checker.
4. Verify both generated cpuapp application version headers report 0.1.0.
5. Package real outputs twice into two different temporary directories outside
   the repository with version 0.1.0, implementation commit SHA, and NCS
   v3.3.0; compare both ZIPs and checksum files byte-for-byte; run top-level
   `sha256sum --strict -c` and both ZIP integrity tests.
6. Re-run focused CI/package tests and `git diff --check`; verify worktree
   remains clean and build outputs are ignored.

Do not commit a results document or change FR2 status yet. Return exact local
evidence to Orchestrator, who will inspect implementation and request explicit
user approval before push/PR. Hosted CI success is mandatory before FR2 can be
accepted.

## Escalation

Stop without committing incomplete work and report to Orchestrator if:

- exact container/action/NCS pins are invalid or unsupported;
- workflow shape requires credentials, write permission, J-Link, privileged
  mode, extra package installation, or architecture invention;
- root VERSION does not generate expected cpuapp version headers;
- existing build helpers do not work in decided workspace topology;
- hosted-only behavior cannot be represented without weakening static tests;
- inventory differs from 35/5/21 or any focused/canonical/build/contract check
  fails;
- package output differs across repeated packaging of identical real inputs;
- two materially different attempts fail on one blocker.

Preserve worktree. Return exact blocker, attempts, commands/errors/logs,
status/diff, one precise question, and smallest suspected next step. Do not
weaken tests, normalize warnings, broaden permissions, float dependency refs,
or silently change artifact contract.

## Return

Return:

- files and behavior changed;
- exact pins and workflow contract;
- focused test/inventory/matrix results;
- implementation commit hash/message;
- canonical 64-child result;
- both production build and 95-assertion contract results;
- generated version-header and repeated real-package facts;
- final git status;
- blockers/deviations;
- explicit statement that hosted GitHub Actions remains pending and no remote
  operation occurred.

Do not push, merge, open a PR, tag, create a release, amend, force-push, or add
AI/tool attribution.
