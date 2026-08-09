# FR2 review-fix handoff: upload path and exact package set

Date: 2026-08-09

## Goal

Fix two FR2 review defects before any remote run. Keep FR2 hosted acceptance
pending.

## Review evidence

1. `.github/workflows/firmware-build.yml` checks the application out at
   `$GITHUB_WORKSPACE/workspace/le-audio-receiver` and creates `dist` there.
   `actions/upload-artifact` is a `uses:` step and has no `working-directory`;
   relative `path` inputs resolve from `$GITHUB_WORKSPACE`. Current inputs
   `dist/...` therefore point at nonexistent files and hosted upload will fail
   with `if-no-files-found: error`.
2. Package verification counts three regular top-level files and checks for
   symlinks, but does not reject an extra top-level directory. This is weaker
   than the decided “exactly three top-level entries, all regular files”
   contract.
3. Static tests pin the incorrect upload paths and do not assert total
   top-level entry count, so they pass despite both defects.

## Exact fix

Change only:

- `.github/workflows/firmware-build.yml`
- `scripts/test_firmware_build_ci.py`
- this handoff document

### Workflow

In package verification, require both:

```bash
test "$(find dist -mindepth 1 -maxdepth 1 | wc -l)" -eq 3
test "$(find dist -mindepth 1 -maxdepth 1 -type f | wc -l)" -eq 3
```

Retain the no-symlink check and exact filename checks. Together these reject
extra files, directories, links, and other top-level entry types.

In `actions/upload-artifact` `path`, list exactly:

```text
workspace/le-audio-receiver/dist/le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip
workspace/le-audio-receiver/dist/le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip
workspace/le-audio-receiver/dist/SHA256SUMS
```

Do not use wildcards, absolute host paths, an unsupported
`working-directory` input, or move/copy the release set.

### Tests

Update `scripts/test_firmware_build_ci.py` so public workflow assertions:

- require both exact total-entry and regular-file count commands;
- require the three workspace-root-relative upload paths above;
- reject bare upload entries beginning `dist/`;
- retain all prior workflow/version tests and exact options.

No new gate child and no inventory-count change.

## Verification and commit

Run before commit:

```bash
python3 scripts/test_firmware_build_ci.py
python3 scripts/test_package_firmware_release.py
python3 scripts/check-test-matrix.py
git diff --check
```

Inspect status, diff, and recent log. Stage only the three named files. Commit:

```text
fix: upload packaged firmware from workspace
```

At the clean correction commit run:

```bash
./scripts/test-all.sh
```

Require 64 PASS / 0 FAIL / 64 TOTAL. Production source, root VERSION, build
helpers, generated firmware, and packager are unchanged, so production builds
and real-package smoke need not rerun. Re-run the two focused Python suites and
`git diff --check`; finish clean.

Do not push, open a PR, merge, tag, release, amend, change permissions/pins,
mark FR2 accepted, or add attribution.

## Escalation and return

Stop if action path semantics contradict this evidence, tests fail, canonical
gate fails, or fixing needs artifact-layout/scope changes. Return files,
observable fix, focused/canonical results, commit hash/message, final status,
deviations/blockers, and explicit hosted-CI pending status.
