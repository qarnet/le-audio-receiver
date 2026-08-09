# FR2 acceptance handoff: record hosted firmware CI evidence

Date: 2026-08-09

## Goal

Record complete local and GitHub-hosted FR2 evidence, mark FR2 accepted, and
leave FR3-FR5 planned. This is documentation-only. Do not alter workflow,
version, packager, tests, firmware, artifacts, or permissions.

## Accepted implementation

- Main implementation: `a54e17d` (`ci: build and package receiver firmware`).
- Upload-path correction: `117bc92` (`fix: upload packaged firmware from
  workspace`).
- Hosted environment correction: `75a8093` (`fix: export Zephyr workspace to
  firmware builds`).
- Clean local canonical gate at `75a8093`: 64 PASS / 0 FAIL / 64 TOTAL
  (35 Twister + 5 exec-only + 21 Python + coverage + matrix + BSim).
- Coverage unchanged: population 36, 4674/5130 lines, 2030/2824 branches,
  358/358 functions.
- Local receiver builds 2/2; build contract 95/95; both cpuapp generated
  `APP_VERSION_STRING` values are `0.1.0`; real package double-run
  byte-identical with checksum and ZIP integrity verification.

## Hosted evidence

Draft PR: `https://github.com/qarnet/le-audio-receiver/pull/8`.

### First run, expected failed evidence

- Run `31325518554`, job `93275127545`, failed in 3m15s.
- Container initialization, both checkouts, workspace setup, and project
  version passed.
- `Build nRF5340` failed before CMake because existing build helper correctly
  reported `firmware tool error: ZEPHYR_BASE not set`.
- Root cause: `west zephyr-export` registers CMake package state but does not
  export a workspace-specific environment variable to later Actions steps.
- Corrected by `75a8093`, publishing verified
  `$GITHUB_WORKSPACE/workspace/zephyr` through `$GITHUB_ENV` only after west
  initialization/update/export and topdir checks.

### Successful run

- Run: `31326612845`
- Job: `93277895593`
- URL:
  `https://github.com/qarnet/le-audio-receiver/actions/runs/31326612845/job/93277895593`
- Result: PASS, 6m20s.
- PR head: `75a8093`; Actions checked out and packaged exact synthetic PR merge
  commit `4cfa1297f29e7c98d15f2c623626b597f9144546`, with HEAD asserted equal to
  `$GITHUB_SHA`.
- All steps passed: pinned container startup, application checkout, exact
  sdk-nrf checkout, NCS workspace, project version, nRF5340 build, nRF54L15
  build, 95-assertion contract, both app-version headers, package, package
  verification, upload, and cleanup.
- Build memory summaries:
  - nRF5340 cpuapp FLASH 376084 B, RAM 145256 B;
  - nRF5340 cpunet FLASH 146780 B, RAM 40512 B;
  - nRF54L15 cpuapp FLASH 532324 B, RAM 161044 B (98.29%);
  - nRF54L15 FLPR RAM 43632 B of 64 KiB (66.58%).
- Build diagnostics were the existing set classified in `STATUS.md` "Build
  warning diagnostics": partition-manager deprecation, assert/experimental/
  SW Split policy and advanced-feature notices, nRF54L15 intentional memory
  overlay DTC notices, FLPR UART console assigned-but-got, and nRF54L15
  watchdog no-sources notice. No compiler warning and no new/actionable
  warning.

### Uploaded artifact

- Name:
  `firmware-v0.1.0-4cfa1297f29e7c98d15f2c623626b597f9144546`
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
exact member lists, and both manifests. Manifests report schema 1, project
`le-audio-receiver`, version `0.1.0`, NCS `v3.3.0`, exact built commit
`4cfa1297...`, and correct two-image role/order/path tuples.

## Exact documentation changes

1. Add `docs/development/firmware-release-fr2-results.md` with scope, exact
   pins, local results, first failure/correction, successful hosted run,
   warning classification, artifact metadata, downloaded verification, and
   status. State clearly that FR2 creates workflow artifacts only, not a tag,
   GitHub Release, published binary, hardware acceptance, MCUboot, or DFU.
2. In `docs/development/firmware-release-plan.md`, mark FR2 accepted on
   2026-08-09 at `75a8093` with successful run/evidence link. Leave FR3-FR5
   planned and lifecycle unchanged.
3. Update top current-state summaries in `AGENTS.md`, `STATUS.md`, and
   `docs/design.md` to FR2: 64/64, 35/5/21 composition, clean local run at
   `75a8093`, unchanged coverage, build contract 95/95, BSim facts, hosted
   firmware run PASS. Preserve historical FR1/P1-R10 evidence.
4. Update `docs/testing/coverage-matrix.md` current accepted-gate paragraph to
   64 children and clean 64/64 at `75a8093`, linking FR2 results. Table already
   correctly says 21 Python / 64 total.
5. Change `PLANNED_FEATURES.md` item F status from "Planned; nothing published
   yet" to "In progress; FR1-FR2 accepted, nothing published yet". Do not mark
   item F complete because FR3-FR5 and first public release remain open.

Do not rewrite dated historical 63/62/etc. results that accurately describe
their own commits.

## Verification and commit

Run:

```bash
python3 scripts/project-version.py
python3 scripts/test_firmware_build_ci.py
python3 scripts/test_package_firmware_release.py
python3 scripts/test_inventory.py --python
python3 scripts/check-test-matrix.py
git diff --check
```

Inspect status, full diff, and recent log. Stage only:

- `docs/development/firmware-release-fr2-acceptance-handoff.md`
- `docs/development/firmware-release-fr2-results.md`
- `docs/development/firmware-release-plan.md`
- `AGENTS.md`
- `STATUS.md`
- `docs/design.md`
- `docs/testing/coverage-matrix.md`
- `PLANNED_FEATURES.md`

Commit:

```text
docs: record FR2 firmware CI acceptance
```

No full local gate rerun is needed after this documentation-only commit; cite
the clean `75a8093` run and rerun focused checks/diff check after commit.
Finish clean. Do not push; Orchestrator will review, push to PR 8, and require
the resulting final hosted run to pass before closing the review gate.

Do not alter PR state/body, workflow/code/tests, push, merge, tag, release,
amend, start FR3, or add attribution.

## Escalation and return

Stop if evidence contradicts the values above, an active current-state claim
cannot be reconciled without rewriting valid history, or focused checks fail.
Return changed files, evidence summary, test results, commit hash/message,
clean status, deviations/blockers, and final hosted rerun pending.
