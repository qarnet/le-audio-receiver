# Firmware release FR1 handoff

## Goal

Implement the deterministic factory-firmware packaging contract defined by
`docs/development/firmware-release-plan.md`.

FR1 packages existing production build outputs. It does not build firmware,
run in GitHub Actions, select a release version, create a tag/release, alter
firmware, or add MCUboot/DFU.

## In scope

1. Add a stdlib-only public CLI at
   `scripts/package-firmware-release.py`.
2. Add public-boundary tests at
   `scripts/test_package_firmware_release.py` so test inventory discovers one
   new Python child.
3. Add target-local release-candidate flashing notes:
   - `release/flashing/nrf5340-e83.md`
   - `release/flashing/nrf54l15-xiao.md`
4. Update the `AGENTS.md` no-em-dash user-facing scope to include
   `release/flashing/*.md`.
5. After implementation acceptance, update current gate evidence in
   `AGENTS.md` and `STATUS.md`, mark FR1 accepted in the plan, and add
   `docs/development/firmware-release-fr1-results.md`.
6. Commit implementation and acceptance documentation separately as specified
   below.

## Out of scope

- `.github/workflows/` and any hosted CI work.
- Root `VERSION` and choice of next release version.
- Git tags, GitHub Releases, upload/publish logic, or credentials.
- Firmware source, Kconfig, devicetree, partitions, build helpers, or flash
  helpers.
- MCUboot, MCUmgr, SMP, signing keys, signed images, or DFU packages.
- Debug bundles and SBOM generation.
- Final non-developer flashing procedure. FR1 notes are honest
  release-candidate tuple/requirements notes; FR5 owns final public commands.

## Public CLI

Invocation:

```console
python3 scripts/package-firmware-release.py \
  --version 0.1.0 \
  --git-commit 0123456789abcdef0123456789abcdef01234567 \
  --ncs-version v3.3.0 \
  --build-root build \
  --output-dir dist
```

All five arguments are required. The script must:

- accept project version only as canonical `MAJOR.MINOR.PATCH`, each numeric,
  with no leading zeros except `0`;
- accept Git commit only as exactly 40 lowercase hexadecimal characters;
- accept NCS version only as canonical `vMAJOR.MINOR.PATCH`;
- resolve build root and output parent, but never write absolute host paths into
  archives or manifests;
- reject an output directory that already exists;
- validate every input for both targets before creating the output directory;
- return zero only after both ZIPs and top-level `SHA256SUMS` are complete;
- print stable, concise success lines naming both ZIPs and top-level checksum;
- print one clear `package-firmware-release: error: ...` diagnostic to stderr
  and return nonzero for caller errors, without a traceback.

No optional target mode exists in FR1. One invocation always packages both
receiver targets as one release set.

## Canonical input mapping

Paths are relative to `--build-root`:

| Target ID | Board | Role | Input | Packaged name | Flash order |
|---|---|---|---|---|---|
| `nrf5340-e83` | `ebyte_e83_nrf5340/nrf5340/cpuapp` | `cpuapp` | `nrf5340/merged.hex` | `merged.hex` | 0 |
| `nrf5340-e83` | `ebyte_e83_nrf5340/nrf5340/cpuapp` | `cpunet` | `nrf5340/merged_CPUNET.hex` | `merged_CPUNET.hex` | 1 |
| `nrf54l15-xiao` | `nrf54l15dk/nrf54l15/cpuapp` | `cpuapp` | `nrf54l15/le-audio-receiver/zephyr/zephyr.hex` | `cpuapp.hex` | 0 |
| `nrf54l15-xiao` | `nrf54l15dk/nrf54l15/cpuapp` | `flpr` | `nrf54l15/flpr/zephyr/zephyr.hex` | `flpr.hex` | 1 |

Flashing-note source paths are repository-relative and fixed by target:

- `release/flashing/nrf5340-e83.md`
- `release/flashing/nrf54l15-xiao.md`

Copy each into its ZIP as `FLASHING.md`.

## Intel HEX validation

Every image input must be a regular, non-symlink, nonempty Intel HEX file.
Validate the full file before output creation:

- UTF-8/ASCII text only;
- no blank records;
- every line starts with `:` and contains an even number of hexadecimal
  characters;
- byte-count field matches record length;
- record checksum sums to zero modulo 256;
- exactly one EOF record (`type 01`, byte count 0, address 0) exists;
- EOF is the final record;
- no record appears after EOF.

Support normal Intel HEX record types used by Zephyr output. Do not restrict
valid extended-address/start-address records beyond structural and checksum
validation.

The flashing-note sources must be regular, non-symlink, nonempty UTF-8 files.

Reject symlinked build inputs and flashing notes, including symlinks that point
back inside the expected tree. Release provenance must describe direct files.

## Manifest schema

Each ZIP contains `release-manifest.json`, UTF-8 JSON with sorted keys, two-space
indentation, and one trailing newline. Exact schema:

```json
{
  "git_commit": "0123456789abcdef0123456789abcdef01234567",
  "images": [
    {
      "filename": "merged.hex",
      "flash_order": 0,
      "original_build_path": "nrf5340/merged.hex",
      "role": "cpuapp",
      "sha256": "<64 lowercase hex>",
      "size": 123
    }
  ],
  "ncs_version": "v3.3.0",
  "project": "le-audio-receiver",
  "schema_version": 1,
  "target": {
    "board": "ebyte_e83_nrf5340/nrf5340/cpuapp",
    "id": "nrf5340-e83"
  },
  "version": "0.1.0"
}
```

`images` follows ascending `flash_order`. Numeric `size` is exact source byte
length. `original_build_path` is POSIX-style and relative to build root. No
timestamps, runner paths, usernames, or other nondeterministic values enter the
manifest.

## Checksums

Inside each ZIP, `SHA256SUMS` contains sorted GNU-style lines for:

- `FLASHING.md`
- both packaged image files;
- `release-manifest.json`.

Format exactly:

```text
<64 lowercase hex>  <filename>
```

Terminate file with one newline. Do not hash `SHA256SUMS` into itself.

Top-level output `SHA256SUMS` contains the two ZIP hashes, sorted by ZIP
filename, same format and final newline.

## Deterministic ZIP contract

For identical input bytes and metadata, ZIP bytes must be identical across
separate invocations and output locations.

- Member order: `FLASHING.md`, images by flash order,
  `release-manifest.json`, `SHA256SUMS`.
- Fixed member timestamp: 1980-01-01 00:00:00.
- Fixed regular-file mode: `0644`.
- Fixed Unix creator metadata.
- UTF-8 member names, no directory entries, no absolute paths, no `..`.
- Use one explicit compression algorithm and level for every member.
- Do not carry source mtimes, uid/gid, comments, or extra fields.

## Atomic output and ownership

1. Validate metadata, all four HEX files, and both flashing notes before any
   output-directory creation.
2. Require output directory absent. Its parent may be created after validation.
3. Build the entire release set in one temporary sibling directory under the
   output parent.
4. On any failure, recursively clean only that private temporary directory and
   leave final output directory absent.
5. After both ZIPs and top-level checksum are complete, atomically rename the
   private directory to the requested output directory.
6. Never remove or overwrite a caller-owned path.

Cancellation/process kill atomicity is bounded by filesystem rename: a killed
process may leave its uniquely named private temporary sibling, but never a
partially populated final output directory. Name private staging directories
with a clear `.firmware-release-` prefix. Normal handled failures clean them.

## Flashing-note content

Both notes are user-facing and contain no U+2014 em dash. Each must:

- identify exact target and both required image files;
- say images are one versioned tuple and must not be mixed;
- list flash order;
- state normal flashing preserves settings/bonds;
- state destructive clean-state/recovery is separate and target-specific;
- link to the repository's `docs/flashing.md` and `docs/user-guide.md` using
  full GitHub URLs rooted at `https://github.com/qarnet/le-audio-receiver/` so
  links still work after extraction;
- state FR1 packages are release-candidate inputs and final non-developer
  flashing commands must be accepted before public publication.

Do not invent a flashing command not supported by current scripts.

## Public-boundary test plan

`scripts/test_package_firmware_release.py` uses stdlib `unittest` and launches
the CLI as a subprocess. Tests create independent temporary repo/build/output
fixtures and valid Intel HEX records with computed checksums.

Required observable tests:

1. Happy path creates exactly two expected ZIPs and top-level `SHA256SUMS`.
2. Both ZIP member names and member order are exact.
3. Both manifest objects match exact schema, target mapping, roles, flash
   order, paths, sizes, and source hashes.
4. Internal and top-level checksums match actual bytes and exact formatting.
5. Repeated packaging into two output locations is byte-identical.
6. Archive members expose fixed timestamp, mode, creator, and no extra/comment
   metadata.
7. No archive member or manifest value leaks absolute fixture paths.
8. Missing each one of four HEX inputs fails with final output absent.
9. Empty, symlinked, malformed-record, bad-byte-count, bad-checksum,
   missing-EOF, duplicate-EOF, and record-after-EOF image inputs fail with
   final output absent.
10. Missing, empty, invalid-UTF-8, and symlinked flashing notes fail with final
    output absent.
11. Invalid version, Git commit, and NCS version fail before output creation.
12. Existing output directory is rejected without changing its sentinel file.
13. A handled failure leaves no `.firmware-release-*` staging sibling.
14. Changing one image changes its manifest/internal checksum and target ZIP,
    but not the other target ZIP.
15. Error paths emit the stable stderr prefix and no traceback.
16. Success stdout is deterministic and names only output-relative files.

Avoid private-field/helper-call assertions. Test the CLI, resulting filesystem,
archives, JSON, checksums, and process exit/output.

## Focused verification before implementation commit

```bash
python3 scripts/test_package_firmware_release.py
python3 scripts/test_inventory.py --python
python3 scripts/check-test-matrix.py
git diff --check
```

If accepted build outputs exist, run one smoke package using them into a
temporary directory outside the repository and inspect both ZIPs. Do not make
current build outputs a test prerequisite.

## Implementation commit

Before committing, inspect `git status`, `git diff`, and `git log --oneline
-10`. Stage only:

- `docs/development/firmware-release-fr1-handoff.md`
- `scripts/package-firmware-release.py`
- `scripts/test_package_firmware_release.py`
- `release/flashing/nrf5340-e83.md`
- `release/flashing/nrf54l15-xiao.md`
- `AGENTS.md` (style-scope addition only at this commit)

Commit:

```text
feat: add deterministic firmware packager
```

Do not include gate-count/status evidence yet.

## Clean canonical gate

After implementation commit, require clean worktree and run:

```bash
./scripts/test-all.sh
```

Expected inventory after adding one Python child: 35 twister + 5 exec-only +
20 Python + coverage + matrix + BSim = 63 total. This expectation is not a
license to hardcode or weaken inventory; stop if actual discovery differs.

Any failure blocks FR1 acceptance. Do not normalize warnings or commit failing
acceptance docs.

## Acceptance documentation commit

Only after clean 63/63 gate:

1. Add `docs/development/firmware-release-fr1-results.md` with exact focused
   and canonical results, commit hash, artifact smoke facts, and no inflated
   release/CI claim.
2. Mark FR1 `ACCEPTED` in
   `docs/development/firmware-release-plan.md`; leave FR2-FR5 planned.
3. Update top current-state gate evidence in `AGENTS.md` and `STATUS.md` from
   62 to 63 total and Python count 19 to 20, citing the clean implementation
   commit. Coverage population and build contract change only if observed
   output says so; never guess.
4. Run `git diff --check` and rerun the focused packager test. Verify worktree
   contains only intended acceptance docs/status changes.

Commit:

```text
docs: record FR1 firmware packaging acceptance
```

The canonical gate need not rerun after this documentation-only evidence
commit, but focused tests and diff checks must remain green.

## Escalation

Stop without committing incomplete work and report to Orchestrator if:

- two materially different attempts fail on one blocker;
- real Zephyr HEX files violate the decided validation contract;
- atomic directory rename cannot meet the contract on supported local/CI
  filesystems;
- output determinism cannot be explained;
- test inventory differs from the expected single new Python child;
- any canonical gate child fails;
- satisfying FR1 requires changing firmware, build outputs, CI, versioning, or
  the artifact contract.

Return exact blockers, attempts, commands/output, diff/status, and one precise
question. Do not weaken validation or silently expand scope.

## Final return

Return:

- files changed;
- CLI and artifact behavior;
- focused test counts/results;
- real-build smoke-package results, if run;
- canonical gate totals and important sub-gates;
- both commit hashes/messages;
- clean/dirty status;
- blockers, deviations, and FR2 follow-up.

Do not push, merge, open a PR, tag, or create a GitHub Release.
