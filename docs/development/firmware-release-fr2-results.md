# FR2 results — reproducible firmware build CI

Accepted: 2026-08-09.  Handoff
`docs/development/firmware-release-fr2-handoff.md`; implementation commit
`a54e17d` (`ci: build and package receiver firmware`); review-fix correction
`117bc92` (`fix: upload packaged firmware from workspace`); hosted environment
correction `75a8093` (`fix: export Zephyr workspace to firmware builds`);
acceptance commit (this document's commit) `docs: record FR2 firmware CI
acceptance`.

FR2 adds one least-privilege GitHub Actions workflow that checks out the exact
NCS 3.3.0 source in the exact Nordic toolchain container, builds both
production receiver targets, verifies resolved build and application-version
contracts, packages the FR1 factory tuples, verifies checksums, and uploads
exactly the three release-set files as an immutable workflow artifact.

FR2 creates **workflow artifacts only**.  It creates no tag, no GitHub
Release, no published binary, no hardware acceptance, no MCUboot, and no DFU.
FR3 owns tag/version checks and draft release publication; FR4 owns
exact-artifact hardware acceptance; FR5 owns the first public release.
Nothing was pushed from this repository during FR2 implementation; all hosted
evidence below came from the draft PR 8 run by the Orchestrator.

## Exact pins

| Dependency | Human version | Immutable reference |
|---|---|---|
| Runner | Ubuntu | `ubuntu-22.04` |
| Nordic toolchain container | `sdk-nrf-toolchain:v3.3.0` | `ghcr.io/nrfconnect/sdk-nrf-toolchain@sha256:f24d8932ff081ebcd8da9c248f4449bdabe461c0620a7a4ac9e95eb577ba2276` |
| `actions/checkout` | `v7.0.1` | `3d3c42e5aac5ba805825da76410c181273ba90b1` |
| `actions/upload-artifact` | `v7.0.1` | `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a` |
| sdk-nrf | annotated tag `v3.3.0` peeled commit | `ba167d9f3db4abbdc9b67887ca3ea66c64f2d956` |

Permissions are `contents: read` only.  No secrets, no write permission, no
OIDC, no environments, no `ACCEPT_JLINK_LICENSE`, no privileged container
options.  Job shell is Bash; one `firmware` job, 120 minute timeout,
concurrency per workflow/ref with `cancel-in-progress: true`.

## Local verification (clean commit `75a8093`)

- Canonical gate `./scripts/test-all.sh`: **64 PASS / 0 FAIL / 64 TOTAL**
  (35 Twister + 5 exec-only + 21 Python + coverage + matrix + BSim), BSim
  pins byte-identical.
- Coverage unchanged: population 36, 4674/5130 lines (91.1%), 2030/2824
  branches (71.9%), 358/358 functions.
- Local receiver builds 2/2 (`fw-build-5340`, `fw-build-54l15`), no new
  actionable warning.
- Build contract: **95 assertions, 0 failed**, `BUILD CONTRACT PASSED`.
- Both cpuapp generated `app_version.h` report
  `#define APP_VERSION_STRING           "0.1.0"`.
- Real package double-run into two output locations: both ZIPs and the
  top-level `SHA256SUMS` byte-identical; top-level `sha256sum --strict -c`
  passes; both ZIPs pass `python3 -m zipfile --test`; manifests report
  schema 1, project `le-audio-receiver`, version `0.1.0`, NCS `v3.3.0`, the
  implementation commit, and correct two-image role/order/path tuples.

## First hosted run, expected failed evidence

Draft PR: `https://github.com/qarnet/le-audio-receiver/pull/8`.

- Run `31325518554`, job `93275127545`, failed in 3m15s.
- Container initialization, both checkouts, NCS workspace initialization, and
  project version passed.
- `Build nRF5340` failed before CMake with the existing build helper's
  correct `firmware tool error: ZEPHYR_BASE not set`.
- Root cause: `west zephyr-export` registers Zephyr's CMake package state but
  does not export a workspace-specific environment variable to later Actions
  steps; the toolchain container Bash initialization does not set
  `ZEPHYR_BASE`, which is workspace-specific.
- Corrected by `75a8093`: publish verified
  `$GITHUB_WORKSPACE/workspace/zephyr` through `$GITHUB_ENV` only after west
  initialization/update/export and HEAD/version/topdir checks.  `ZEPHYR_BASE`
  is never set before `west init`.

## Successful hosted run

- Run: `31326612845`, job: `93277895593`, URL:
  `https://github.com/qarnet/le-audio-receiver/actions/runs/31326612845/job/93277895593`
- Result: PASS, 6m20s.
- PR head `75a8093`; Actions checked out and packaged the exact synthetic PR
  merge commit `4cfa1297f29e7c98d15f2c623626b597f9144546`, with HEAD asserted
  equal to `$GITHUB_SHA`.
- All steps passed: pinned container startup, application checkout, exact
  sdk-nrf checkout, NCS workspace, project version, nRF5340 build, nRF54L15
  build, 95-assertion contract, both app-version headers, package, package
  verification (exact three top-level entries, no symlinks, exact names,
  `sha256sum --strict -c`, both ZIP integrity tests), upload, and cleanup.
- Build memory summaries:
  - nRF5340 cpuapp FLASH 376084 B, RAM 145256 B;
  - nRF5340 cpunet FLASH 146780 B, RAM 40512 B;
  - nRF54L15 cpuapp FLASH 532324 B, RAM 161044 B (98.29%);
  - nRF54L15 FLPR RAM 43632 B of 64 KiB (66.58%).

### Warning classification

Build diagnostics were the existing set classified in `STATUS.md` "Build
warning diagnostics": partition-manager deprecation, assert/experimental/
SW Split policy and advanced-feature notices, nRF54L15 intentional memory
overlay DTC notices, FLPR UART console assigned-but-got, and nRF54L15
watchdog no-sources notice.  No compiler warning and no new/actionable
warning.

## Uploaded artifact

- Name: `firmware-v0.1.0-4cfa1297f29e7c98d15f2c623626b597f9144546`
- Artifact ID: `9041780309`
- Download URL:
  `https://github.com/qarnet/le-audio-receiver/actions/runs/31326612845/artifacts/9041780309`
- Size: 1202130 bytes.
- GitHub artifact digest:
  `sha256:cfbe1066dbb9dda9c65243f908827cdecc97d2637f8ba2343c7001d3fea06f33`.
- Created `2026-08-09T17:37:33Z`, expires `2026-08-23T17:37:33Z`.

The downloaded artifact contained exactly three files:

| File | Bytes | SHA-256 |
|---|---:|---|
| `SHA256SUMS` | 232 | `4e8c3785a3dbb0919154c747962b19aa4607c5cce1bc3bec4d8b5fe8ec540ac9` |
| `le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip` | 579413 | `27cddb9b81f3cd3f6d0aee2ae31db1f86b1cad83eb3911b848f596ccba0fdf86` |
| `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip` | 621971 | `7a187cd5a3d0d6883296527f4f52a0819a7677ad0ab8d9d72d6f5cc98253165f` |

Independent post-download verification passed top-level
`sha256sum --strict -c SHA256SUMS`, both `python3 -m zipfile --test` checks,
exact member lists, and both manifests.  Manifests report schema 1, project
`le-audio-receiver`, version `0.1.0`, NCS `v3.3.0`, exact built commit
`4cfa1297...`, and correct two-image role/order/path tuples.

## Status

FR2 `ACCEPTED`.  FR3-FR5 remain planned in
`docs/development/firmware-release-plan.md`.  FR2 ships workflow artifacts
only; no tag, GitHub Release, published binary, hardware acceptance, MCUboot,
or DFU exists yet.  The final hosted run after this acceptance commit must
still pass before the PR 8 review gate closes.
