# FR1 results — deterministic factory-firmware packager

Accepted: 2026-08-09.  Handoff `docs/development/firmware-release-fr1-handoff.md`;
implementation commit `f3cd4c4` (`feat: add deterministic firmware packager`);
review-fix handoff `docs/development/firmware-release-fr1-fix-handoff.md`;
correction commit `1671a9f` (`fix: handle firmware packaging I/O failures`);
acceptance commit (this document's commit) `docs: record FR1 packaging error fix`.

FR1 packages existing production build outputs.  It does not build firmware,
run in GitHub Actions, select a release version, create a tag/release, alter
firmware, or add MCUboot/DFU.  No hosted CI, no `.github/workflows/` change, no
root `VERSION` change, no tag/release created, no push/merge/PR.

## Deliverables

- `scripts/package-firmware-release.py` — stdlib-only public CLI.
- `scripts/test_package_firmware_release.py` — public-boundary subprocess
  tests (one new Python gate child, label `package_firmware_release`).
- `release/flashing/nrf5340-e83.md`, `release/flashing/nrf54l15-xiao.md` —
  target-local release-candidate flashing notes (no U+2014 em dash).
- `AGENTS.md` — no-em-dash user-facing scope now includes
  `release/flashing/*.md`.

## Review defect and correction

Review found one defect in the accepted implementation.  `main()` in
`scripts/package-firmware-release.py` caught `PackagerError` only.  Output
creation and ZIP writing can raise `OSError` after all caller inputs pass
validation; that `OSError` escaped `main()` and Python printed a traceback,
violating the stable `package-firmware-release: error: ...` contract.

Public-boundary reproduction with a valid fixture and a 100-byte
`RLIMIT_FSIZE` on the child process observed:

- exit code 1;
- final output directory absent;
- no `.firmware-release-*` staging sibling remained;
- stderr contained three chained `OSError: [Errno 27] File too large`
  tracebacks rather than one stable prefixed diagnostic.

The existing test 13 failed before staging was created, so it did not prove
cleanup after a packaging write failure.

Correction (`1671a9f`):

- `main()` now catches `(PackagerError, OSError)`, prints exactly one line
  using the existing `ERROR_PREFIX` and exception text, and returns the
  existing nonzero code `1`.  Successful runs still return `0`.  No broad
  `Exception` or `BaseException` handling was added to `main()`.
- The existing `except BaseException` staging cleanup in `_run()` is
  unchanged: it owns cleanup, re-raises, and `main()` owns conversion of
  expected caller/I/O failures into process diagnostics.
- New Linux public-subprocess regression (test 19) in
  `scripts/test_package_firmware_release.py`: launches the copied CLI with a
  `preexec_fn` applying `resource.setrlimit(resource.RLIMIT_FSIZE, (100, 100))`
  so the failure happens during ZIP writing after staging creation, then
  asserts nonzero exit, stderr starts with `ERROR_PREFIX`, no `Traceback`,
  stderr contains `File too large`, final output directory absent, and no
  staging sibling.  Test is Linux-specific and deterministic; it does not
  inspect packager private functions or mock ZIP helpers.

## Focused verification (before implementation commit)

| Command | Result |
|---------|--------|
| `python3 scripts/test_package_firmware_release.py` | 19 tests, OK, 0 failures |
| `python3 scripts/test_inventory.py --python` | 20 children (unchanged); `package_firmware_release` present |
| `python3 scripts/check-test-matrix.py` | 0 error(s), 0 note(s) |
| `git diff --check` | clean |

Focused suite grew from 18 to 19 tests: one new test method inside the
existing `package_firmware_release` Python child, so the inventory child count
and canonical total stayed at 20 Python children and 63 total.

## Real-build smoke package

Packaged the accepted build outputs at the implementation commit into a
temporary directory outside the repository (version `0.1.0`, commit
`0123456789abcdef0123456789abcdef01234567`, NCS `v3.3.0`, build root
`build`).  The real Zephyr Intel HEX outputs pass the full validation
contract (no escalation).

| Artifact | Bytes | SHA-256 |
|----------|-------|---------|
| `le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip` | 578774 | `325eb610ac9a295b2bcf14c157b7f2f580ebdc3aa94bf5850361f42c72e2824d` |
| `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip` | 622227 | `7cab013d52ca64045beb07a97bc761d2a7c5b150294b15076ab7b877374d0006` |

Manifest image facts from the smoke run (source byte sizes):

| Target | Role | Packaged name | Size | SHA-256 |
|--------|------|---------------|------|---------|
| nrf5340-e83 | cpuapp | `merged.hex` | 1032804 | `51d83d7e2585b56d1350dffe65cb6b138d4c8eed9064ea87681f2be26d25d6de` |
| nrf5340-e83 | cpunet | `merged_CPUNET.hex` | 403684 | `2ce0ca1aa9fdc27d9fc1a4b25148af82da2fb52f1834f61113685d0851119619` |
| nrf54l15-xiao | cpuapp | `cpuapp.hex` | 1495982 | `087df1c387a461d984585bb88583eef3d8970e48232833d31bd0d843f244f092` |
| nrf54l15-xiao | flpr | `flpr.hex` | 91857 | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

Verified on the smoke artifacts:

- ZIP member names and order exactly `FLASHING.md`, images by flash order,
  `release-manifest.json`, `SHA256SUMS`.
- Every member: timestamp 1980-01-01 00:00:00, mode 0644, Unix creator
  (system 3), no extra fields, no comments, no directory entries.
- Manifest schema exact (schema version 1, sorted keys, two-space indent,
  one trailing newline, POSIX relative `original_build_path`, no timestamps
  or host paths).
- Top-level `SHA256SUMS` hashes both ZIPs; per-ZIP `SHA256SUMS` hashes
  `FLASHING.md`, both images, and the manifest (never itself).  Both
  verified with `sha256sum -c` after extraction.
- A second invocation into a different output location produced
  byte-identical ZIPs and checksum file.

The correction commit does not alter the artifact contract: ZIP names,
layout, manifest schema, checksum format, compression, success stdout, and
flash notes are unchanged.

## Canonical gate

Clean worktree at `1671a9f`; `./scripts/test-all.sh`:

```
Gate complete: 63 PASS / 0 FAIL / 63 TOTAL
```

Sub-gates (all PASS):

- 35 twister C suites.
- 5 exec-only C suites.
- 20 Python children (19 prior + `package_firmware_release`; test count now
  19 within that child).
- Coverage: native suites rebuilt with `CONFIG_COVERAGE=y`, baseline
  enforcement `0 error(s)` against the committed
  `tests/coverage-baseline.json` (unchanged); numeric population 36 files,
  4674/5130 lines (91.1%), 2030/2824 branches (71.9%), 358/358 functions.
- Matrix: `check-test-matrix.py --coverage-json` 0 error(s), 0 note(s).
- BSim Stage 1: 17-scenario T4+R7 BAP matrix strict-checked PASS, all
  existing pins byte-identical (mono 10 ms `0x22AB5C0D`, Mode A/B 10 ms
  `0xBAE24F7E`, `duplicate_release_10ms` `0xAEBD23A1`, reconnect = fresh
  mono oracle).

The expected inventory at the correction commit was 63 total; actual
discovery matched (35 + 5 + 20 + coverage + matrix + BSim).  No gate child
was weakened or skipped.  The correction touches only Python test/source, so
coverage population, coverage counts, build contract, and BSim facts are
unchanged from the implementation-commit gate.

## Status

FR1 `ACCEPTED` at original implementation plus review correction.  FR2-FR5
remain planned in `docs/development/firmware-release-plan.md`.  The smoke
package is evidence that the packager handles the real build outputs; it is
not a release.
