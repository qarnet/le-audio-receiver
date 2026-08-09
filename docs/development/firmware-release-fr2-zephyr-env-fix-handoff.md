# FR2 hosted-CI fix handoff: export workspace ZEPHYR_BASE

Date: 2026-08-09

## Goal

Fix first hosted FR2 run failure without changing build, package, permission,
or dependency contracts.

## Evidence and root cause

Draft PR 8 workflow run `31325518554`, job `93275127545`:

- container initialization, both checkouts, NCS workspace initialization, and
  project version passed;
- first production step `./scripts/bin/fw-build-5340` failed before CMake with
  `firmware tool error: ZEPHYR_BASE not set`;
- later steps were correctly skipped;
- job exit 1.

Nordic's toolchain container Bash initialization exposes bundled tools, but
`ZEPHYR_BASE` is workspace-specific. `west zephyr-export` registers Zephyr's
CMake package; it does not mutate environment of future GitHub Actions steps.
Existing `scripts/bin/fw-common.sh` deliberately requires a set, existing
Zephyr directory.

Do not set `ZEPHYR_BASE` before `west init`: a pre-set value can alter west
workspace resolution. Publish it only after update/export and directory
verification.

## Exact fix

Change only:

- `.github/workflows/firmware-build.yml`
- `scripts/test_firmware_build_ci.py`
- this handoff document

At end of `Initialize NCS workspace`, after current post-update HEAD/version
and `west topdir` assertions, add:

```bash
zephyr_base="$GITHUB_WORKSPACE/workspace/zephyr"
test -d "$zephyr_base"
printf 'ZEPHYR_BASE=%s\n' "$zephyr_base" >> "$GITHUB_ENV"
```

`$GITHUB_ENV` makes value available to subsequent build steps. Do not add
job-level/container-level `ZEPHYR_BASE`, source arbitrary shell files, weaken
`_fw_require_env`, install packages, or bypass build helpers.

Update public workflow contract tests to require all three lines, ensure export
appears after `west update`/`west zephyr-export`, and retain all prior pins,
permissions, setup, build, verification, and upload assertions.

No inventory count change.

## Verification, commit, and return

Run:

```bash
python3 scripts/test_firmware_build_ci.py
python3 scripts/test_package_firmware_release.py
python3 scripts/check-test-matrix.py
git diff --check
```

Inspect status, diff, and recent log. Stage only three named files. Commit:

```text
fix: export Zephyr workspace to firmware builds
```

At clean correction commit run `./scripts/test-all.sh`; require 64 PASS / 0
FAIL / 64 TOTAL. Re-run focused tests and diff check. Production source and
build commands are unchanged, so do not run local firmware builds again.

Do not push; Orchestrator will inspect and push correction to existing draft
PR, then watch replacement hosted run. Do not open another PR, merge, tag,
release, amend, change pins/permissions, mark FR2 accepted, or add attribution.

Escalate if GitHub environment-file semantics conflict, tests/gate fail, or
fix requires bypassing build helpers. Return files, exact fix, tests/gate,
commit, clean status, deviations/blockers, and hosted rerun pending.
