# PB-032 implementation handoff

## Goal

Make the Seeed XIAO nRF54L15 the sole supported final receiver target. Remove
the nRF5340 Ebyte receiver from product-choice, release, and future-feature
obligations without deleting its legacy engineering path or any nRF5340-based
test fixture in this phase.

## Product decisions already made

- XIAO nRF54L15 is the only final, supported, production, HIL-acceptance, and
  public-release receiver target.
- Ebyte nRF5340 receiver becomes a best-effort legacy engineering/regression
  path. It has no release asset, product-parity, physical-control, or future
  feature obligation.
- PB-005 is already archived with this rationale.
- nRF5340BSim, the nRF5340DK HIL source, and the nRF5340DK HCI-UART dongle are
  separate test roles and remain required for now.
- Existing nRF54L15-only CI, packaging, provenance, draft-release, RH4, and FR4
  boundaries remain unchanged.
- No runtime firmware behavior changes in PB-032.

## Current verified state

- `.github/workflows/firmware-build.yml` builds and packages only nRF54L15
  receiver firmware.
- `scripts/package-firmware-release.py` emits only the nRF54L15 factory ZIP and
  `SHA256SUMS`.
- `scripts/prepare-draft-release.py` and
  `scripts/test_firmware_build_ci.py` reject nRF5340 receiver release assets.
- Public docs still tell users to choose between two receiver targets.
- `scripts/check-test-matrix.py` defines duplicate `BUILD_COMMANDS` and
  `PRODUCTION_BUILD_COMMANDS` sets containing `fw-build-5340`,
  `fw-build-54l15`, and `fw-build-dongle`; `Checker.resolve_suite()` uses the
  misleading production-named set.
- `tests/test-matrix.json` records both receiver builds as evidence for
  `src/main.c` and calls them production builds in its reason.
- `docs/testing/behavior-contract.md` BUILD-001 calls both receivers
  production targets.
- `AGENTS.md` still requires every change to keep the physical nRF5340 receiver
  build working.

## Existing dirty worktree

Worktree contains intended uncommitted backlog cleanup from prior work:

- PB-006 and PB-031 moved to completed.
- PB-007 candidate notes updated.
- PB-005 moved to archive.
- PB-032 created and moved to In Progress.
- Current release-state paragraphs in `AGENTS.md`, `STATUS.md`, `docs/design.md`,
  and `docs/development/firmware-release-plan.md` already contain active draft
  release facts.

Preserve all those changes. Edit overlapping files on top of them. Do not
revert, commit, stash, or split the worktree.

### Canonical-gate exception approved by product owner

The product owner explicitly authorized one temporary local validation commit
under `/tmp/opencode` because baseline-enforced coverage requires a clean exact
commit. This exception applies only to a detached temporary worktree containing
the complete current patch. Do not commit the primary worktree, push the
temporary commit, or retain the temporary worktree after validation. Remove the
temporary worktree after the gate and report its throwaway commit SHA and gate
result. All other no-commit constraints remain.

## In scope

### Public product documentation

Update these files:

- `README.md`
- `docs/user-guide.md`
- `docs/hardware-wiring.md`
- `docs/known-limitations.md`
- `docs/technology/nrf5340.md`
- `docs/technology/nrf54l15.md`
- `docs/flashing.md`
- `release/flashing/nrf5340-e83.md`

Required wording and navigation behavior:

1. README presents XIAO nRF54L15 as the sole supported receiver hardware.
   Remove platform-choice instructions. Keep Nordic's nRF5340 recommendation
   as honest context for the nRF54L15 caveat, not as a project hardware choice.
2. Quick start and user guide describe only the XIAO receiver path. They must
   not tell users to build or flash E83 receiver firmware.
3. Hardware wiring leads with XIAO. Retain verified E83 pin data only in a
   clearly labeled legacy engineering-reference section.
4. Remove the nRF5340 physical-button item from current product limitations.
   Renumber later limitation headings. Link completed PB-006 through its
   `completed/` path.
5. `docs/technology/nrf5340.md` gets a prominent top status note: legacy
   engineering/regression path, no supported final hardware or release asset,
   retained temporarily for technical evidence.
6. `docs/technology/nrf54l15.md` states this is the sole supported final
   receiver. Related nRF5340 link is labeled legacy engineering background.
7. `docs/flashing.md` becomes explicitly legacy nRF5340 developer flashing
   reference, not general user flashing guidance.
8. `release/flashing/nrf5340-e83.md` remains for historical FR1 provenance but
   must be titled and bannered as historical, not an active release candidate
   or current package input.
9. Do not add Unicode em dash characters to any user-facing file.

### Current policy and status

Update:

- `AGENTS.md`
- `STATUS.md`
- `flake.nix`

Decisions:

- Replace standing dual-receiver support rule with exact role split:
  nRF54L15 is sole final receiver; physical E83 receiver is best-effort legacy
  regression with no parity or feature obligation; PB-032 does not delete it;
  nRF5340BSim, HIL source, and HCI-UART dongle remain current required test
  infrastructure.
- A future product change does not need a physical `fw-build-5340` acceptance
  result unless it explicitly touches the legacy E83 path. Canonical BSim and
  fixture gates remain mandatory where currently defined.
- Add a concise current-state note to STATUS without rewriting dated evidence.
- Change flake description to identify nRF54L15 as the final receiver target.

### Test-contract terminology

Update:

- `scripts/check-test-matrix.py`
- `tests/unit/test_matrix/test_check_test_matrix.py`
- `tests/test-matrix.json`
- `docs/testing/behavior-contract.md`

Exact implementation:

1. Replace `PRODUCTION_BUILD_COMMANDS` with three disjoint named sets:

   ```python
   FINAL_RECEIVER_BUILD_COMMANDS = {"fw-build-54l15"}
   LEGACY_RECEIVER_BUILD_COMMANDS = {"fw-build-5340"}
   FIXTURE_BUILD_COMMANDS = {"fw-build-dongle"}
   BUILD_COMMANDS = (
       FINAL_RECEIVER_BUILD_COMMANDS
       | LEGACY_RECEIVER_BUILD_COMMANDS
       | FIXTURE_BUILD_COMMANDS
   )
   ```

2. `Checker.resolve_suite()` must accept `BUILD_COMMANDS`. Update module wording
   from production build commands to recognized build commands.
3. Add focused public-behavior coverage proving all three accepted command
   names resolve to `("build-cmd", None)` and an unknown build name does not.
4. Keep both receiver build witnesses in the `src/main.c` matrix entry, but
   rewrite its reason to call `fw-build-54l15` final receiver evidence and
   `fw-build-5340` retained legacy regression evidence. Do not change source
   classification or remove historical boot-log evidence.
5. BUILD-001 must say final nRF54L15 receiver, legacy nRF5340 receiver build,
   and central dongle all use NCS v3.3.0. BUILD-002 may retain exact nRF5340
   legacy build contract.

### Link hygiene

Update only links, not historical conclusions:

- `docs/development/firmware-release-plan.md`: PB-006 link points to
  `docs/product/backlog/completed/`.
- `docs/development/refactor-plan.md` and
  `docs/development/user-pairing-control-plan.md`: PB-005 links point to
  `docs/product/backlog/archive/tasks/`.
- Remove current public links that present PB-005 as planned product work.

### PB-032 record

Use `backlog task edit`, not manual frontmatter edits.

- Append implementation notes with policy decisions and validation evidence.
- Check each acceptance criterion only after its evidence passes.
- Fill Final Summary with changed behavior, commands, results, and deferred
  fixture migration.
- Move PB-032 to Review, not Done. No commit or PR was requested, so PR-gated
  Done is unavailable.

## Out of scope and forbidden changes

Do not delete or behaviorally change:

- `src/`
- `boards/`
- `CMakeLists.txt`, `Kconfig`, `Kconfig.sysbuild`, `sysbuild.cmake`, `prj.conf`
- `scripts/bin/fw-build-5340`, `scripts/bin/fw-flash-5340`
- `tests/bsim/`, `scripts/bsim-stage1-run.sh`, `scripts/bsim-env.sh`
- `hil/source/` or any HIL source package/runner contract
- `dongle/` or its build/flash/reset helpers
- `.github/workflows/firmware-build.yml`
- release package/provenance implementation
- historical acceptance results, handoffs, or evidence text beyond link-path
  maintenance explicitly listed above

Do not build or flash hardware for this documentation/policy phase. Do not
publish, delete, or edit GitHub release assets. Do not weaken release tests,
matrix checks, BSim contracts, coverage, warnings, or artifact validation.

## Validation

Run focused checks first:

```bash
nix develop -c python3 tests/unit/test_matrix/test_check_test_matrix.py
nix develop -c python3 scripts/check-test-matrix.py
nix develop -c python3 scripts/test_firmware_build_ci.py
nix develop -c python3 scripts/test_package_firmware_release.py
nix develop -c python3 scripts/test_draft_release.py
nix develop -c python3 scripts/test_package_hil_source_artifact.py
nix develop -c python3 tests/hil/rh4_artifact_test.py
backlog doctor
git diff --check
```

Run canonical software gate:

```bash
nix develop -c ./scripts/test-all.sh
```

Expected inventory remains 74 children and final summary remains:

```text
Gate complete: 74 PASS / 0 FAIL / 74 TOTAL
```

Also inspect changed user-facing docs for U+2014 and verify current public text
contains no dual-choice phrases such as `Choose the nRF5340`, `both supported
boards`, or `Both production targets`. Verify `PRODUCTION_BUILD_COMMANDS` is
gone. Verify all changed Markdown links to PB-005 and PB-006 resolve to archive
or completed storage.

## Escalation

Stop and report instead of guessing if tests require deleting a retained
nRF5340 path, changing final release assets, weakening a gate, or changing
runtime firmware. Preserve worktree and provide exact command/error evidence.

## Return

Return files changed, user-visible behavior, task status/criteria, exact test
commands and outcomes, warnings, deviations, and blockers. Do not commit,
push, open a PR, or merge.
