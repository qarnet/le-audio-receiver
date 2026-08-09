# FR1 results — deterministic factory-firmware packager

Accepted: 2026-08-09.  Handoff `docs/development/firmware-release-fr1-handoff.md`;
implementation commit `f3cd4c4` (`feat: add deterministic firmware packager`);
acceptance commit (this document's commit) `docs: record FR1 firmware packaging
acceptance`.

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

## Focused verification (before implementation commit)

| Command | Result |
|---------|--------|
| `python3 scripts/test_package_firmware_release.py` | 18 tests, OK, 0 failures |
| `python3 scripts/test_inventory.py --python` | 20 children; new `package_firmware_release` discovered |
| `python3 scripts/check-test-matrix.py` | 0 error(s), 0 note(s) |
| `git diff --check` | clean |

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

## Canonical gate

Clean worktree at `f3cd4c4`; `./scripts/test-all.sh`:

```
Gate complete: 63 PASS / 0 FAIL / 63 TOTAL
```

Sub-gates (all PASS):

- 35 twister C suites.
- 5 exec-only C suites.
- 20 Python children (19 prior + `package_firmware_release`).
- Coverage: native suites rebuilt with `CONFIG_COVERAGE=y`, baseline
  enforcement `0 error(s)` against the committed
  `tests/coverage-baseline.json` (unchanged); numeric population 36 files,
  4674/5130 lines (91.1%), 2030/2824 branches (71.9%), 358/358 functions.
- Matrix: `check-test-matrix.py --coverage-json` 0 error(s), 0 note(s).
- BSim Stage 1: 17-scenario T4+R7 matrix strict-checked PASS, all existing
  pins byte-identical (mono 10 ms `0x22AB5C0D`, Mode A/B 10 ms
  `0xBAE24F7E`, reconnect = fresh mono oracle, `duplicate_release_10ms`).

The expected inventory after adding one Python child was 63 total; actual
discovery matched (35 + 5 + 20 + coverage + matrix + BSim).  No gate child
was weakened or skipped.

## Status

FR1 `ACCEPTED`.  FR2-FR5 remain planned in
`docs/development/firmware-release-plan.md`.  The smoke package is evidence
that the packager handles the real build outputs; it is not a release.
