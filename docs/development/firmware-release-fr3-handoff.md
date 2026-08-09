# FR3 handoff: tag-guarded draft release publication

Date: 2026-08-09

## Goal

Implement FR3 from `docs/development/firmware-release-plan.md`: a canonical
`vMAJOR.MINOR.PATCH` tag must build the same factory tuples as FR2, pass exact
tag/version/commit and artifact checks, then create one **draft** GitHub Release
with exact factory ZIPs, top-level checksums, and deterministic provenance.

FR3 implementation must not create or push a tag, create a release during
local/PR verification, publish any release, or begin hardware acceptance. An
actual `v0.1.0` tag and draft-creation acceptance run require later explicit
user approval after implementation review.

## User-observable behavior

- Pull requests, `main`, and manual dispatch keep FR2 behavior: build/package
  workflow artifact only, no release write job.
- Push of tag `vX.Y.Z` runs firmware build/package, requires tag `v` suffix to
  equal root project version exactly, and then runs one tag-only release job.
- Release job downloads exact artifact created by its own workflow run, rechecks
  all release files and manifests, then creates `LE Audio Receiver vX.Y.Z` as a
  draft with exactly four assets:
  - nRF5340 factory ZIP;
  - nRF54L15 factory ZIP;
  - top-level `SHA256SUMS`;
  - `release-provenance.json`.
- Draft notes explicitly say hardware acceptance and manual publication remain
  pending.
- Any mismatch, malformed file, existing release, checksum/ZIP/manifest error,
  missing tag, or API/upload error fails. Workflow never auto-publishes, edits,
  clobbers, or deletes a release.

## In scope

- Tag trigger and exact tag/version/commit checks.
- One tag-only least-privilege release job in existing workflow.
- SHA-pinned official artifact download action.
- One stdlib-only release preparation/validation CLI and public-boundary tests.
- Deterministic draft notes and provenance JSON.
- Static workflow contract tests and active inventory updates for one new
  Python gate child.
- Local implementation commit, focused verification, and clean canonical gate.

## Out of scope

- Creating/pushing `v0.1.0`, creating/deleting a real draft, or remote FR3
  acceptance in this handoff.
- Publishing a release or marking it latest; GitHub-hosted CI must never
  publish.
- Hardware flashing/acceptance or editing draft assets after creation (FR4).
- Final public notes/instructions/manual publication/clean-machine verification
  (FR5).
- MCUboot, DFU, signing keys, signed firmware, cryptographic artifact
  attestations, OIDC, or changes to factory artifacts.
- New GitHub environment/ruleset/branch protection. Repository currently has
  no environments, rulesets, or protected `main`; FR3 protects write authority
  inside workflow with event/job conditions and job-scoped permissions.
- Third-party actions, caches, matrices, package installation, J-Link, secrets,
  raw build trees, or maintainer debug bundles.

## Grounding and immutable pins

Retain all FR2 pins and add:

| Dependency | Version | Immutable reference |
|---|---|---|
| `actions/download-artifact` | `v8.0.1` | `3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c` |

Grounding:

- Official `download-artifact` `action.yml` at this SHA supports exact `name`,
  destination `path`, current-run default, automatic decompression, and default
  `digest-mismatch: error`.
- Installed GitHub CLI 2.97.0 `gh release create` supports `--draft`,
  `--verify-tag`, `--title`, and `--notes-file`, with positional assets.
  `--verify-tag` prevents automatic tag creation. Draft releases remain mutable
  until manual publication, so FR4 must validate exact assets and failed
  creation may need deliberate draft cleanup.
- GitHub's REST endpoint `GET /repos/{owner}/{repo}/releases/tags/{tag}` returns
  HTTP 404 when no matching release exists. Use this endpoint through `gh api
  --include` so only an observed 404 permits creation; authentication, network,
  rate-limit, or other API failures must stop before any write.
- Current public repository has no release, environment, ruleset, or branch
  protection. Job-scoped `contents: write` must therefore exist only on a
  normal tag `push`, after read-only build succeeds.
- Root `VERSION` and `scripts/project-version.py` define accepted version
  `0.1.0`; FR1 packager manifests bind artifact content to version, commit,
  NCS version, target, roles, paths, and checksums.

## Release preparation CLI

Add executable `scripts/prepare-draft-release.py`, stdlib only.

Invocation:

```console
python3 scripts/prepare-draft-release.py \
  --tag v0.1.0 \
  --version 0.1.0 \
  --git-commit 0123456789abcdef0123456789abcdef01234567 \
  --ncs-version v3.3.0 \
  --repository qarnet/le-audio-receiver \
  --workflow "Firmware build" \
  --workflow-ref qarnet/le-audio-receiver/.github/workflows/firmware-build.yml@refs/tags/v0.1.0 \
  --run-id 123456 \
  --run-attempt 1 \
  --artifact-dir dist \
  --output-dir release-metadata
```

### Input contract

- All arguments required.
- Tag exactly `vMAJOR.MINOR.PATCH`, using FR1 canonical decimal rules; suffix
  must exactly equal `--version`.
- Git commit exactly 40 lowercase hexadecimal characters.
- NCS exactly `v3.3.0` for this release track.
- Repository exactly `qarnet/le-audio-receiver` in FR3.
- Workflow exactly `Firmware build`.
- Workflow ref exactly
  `<repository>/.github/workflows/firmware-build.yml@refs/tags/<tag>`.
- Run ID and attempt canonical positive decimal integers.
- Reject control characters/newlines in string inputs.
- Artifact directory must exist as a real directory, not a symlink.
- Output directory must be absent; validate every input and artifact before
  creating it.

### Artifact validation

Artifact directory must contain exactly three direct regular non-symlink files
and no directories/other entries:

- `le-audio-receiver-v<version>-nrf5340-e83-factory.zip`
- `le-audio-receiver-v<version>-nrf54l15-xiao-factory.zip`
- `SHA256SUMS`

Validate:

- top-level checksum file is UTF-8/ASCII, exact GNU two-space format, sorted,
  one line for each ZIP only, no duplicate/absolute/traversal names, and both
  hashes match;
- both ZIPs pass CRC/integrity, have no duplicate/directory/path-traversal
  members, and exact FR1 member order:
  - nRF5340: `FLASHING.md`, `merged.hex`, `merged_CPUNET.hex`,
    `release-manifest.json`, `SHA256SUMS`;
  - nRF54L15: `FLASHING.md`, `cpuapp.hex`, `flpr.hex`,
    `release-manifest.json`, `SHA256SUMS`;
- internal `SHA256SUMS` exact/sorted and hashes every member except itself;
- manifest JSON has exact FR1 top-level keys/schema and reports project
  `le-audio-receiver`, supplied version/commit/NCS, expected target ID/board,
  and exact two image filename/role/original path/flash-order records;
- each manifest image size/hash matches packaged image bytes;
- reject malformed JSON, unknown/missing schema fields, noncanonical hashes,
  malformed sizes/orders, invalid UTF-8 notes, and any mismatch.

Do not import private helpers from FR1 scripts. Release validation is an
independent public boundary, not a unit test of packager internals.

### Outputs

After all validation succeeds, create output atomically through a private
sibling staging directory and rename. On any handled failure, final output and
staging sibling remain absent.

Output exactly two regular UTF-8 files:

1. `release-provenance.json`, sorted keys, two-space indentation, one trailing
   newline, exact schema:

```json
{
  "artifacts": [
    {
      "filename": "SHA256SUMS",
      "sha256": "<64 lowercase hex>",
      "size": 232
    },
    {
      "filename": "le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip",
      "sha256": "<64 lowercase hex>",
      "size": 123
    },
    {
      "filename": "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip",
      "sha256": "<64 lowercase hex>",
      "size": 123
    }
  ],
  "build": {
    "git_commit": "0123456789abcdef0123456789abcdef01234567",
    "ncs_version": "v3.3.0",
    "project": "le-audio-receiver",
    "version": "0.1.0"
  },
  "ci": {
    "repository": "qarnet/le-audio-receiver",
    "run_attempt": 1,
    "run_id": 123456,
    "workflow": "Firmware build",
    "workflow_ref": "qarnet/le-audio-receiver/.github/workflows/firmware-build.yml@refs/tags/v0.1.0"
  },
  "release": {
    "draft": true,
    "tag": "v0.1.0"
  },
  "schema_version": 1,
  "toolchain": {
    "container_image": "ghcr.io/nrfconnect/sdk-nrf-toolchain@sha256:f24d8932ff081ebcd8da9c248f4449bdabe461c0620a7a4ac9e95eb577ba2276",
    "sdk_nrf_commit": "ba167d9f3db4abbdc9b67887ca3ea66c64f2d956"
  }
}
```

`artifacts` sorted by filename. No timestamp, runner path, username, token,
host path, or mutable tag-only dependency enters provenance.

2. `release-notes.md`, deterministic text containing:

- heading `# LE Audio Receiver v<version>`;
- statement that this is a draft factory-flash candidate;
- exact source commit, NCS version, and workflow run URL
  `https://github.com/<repository>/actions/runs/<run-id>`;
- list of two target ZIPs and top-level checksum file;
- companion-image tuple warning;
- statement that settings/bonds are preserved by normal flashing, clean erase
  is separate;
- strong line: `Do not publish this draft until FR4 exact-asset hardware
  acceptance passes.`;
- statement that MCUboot/DFU is not included.

No generated release notes in FR3; FR5 owns final public release-note editing.

### CLI process contract

- Success prints exactly:
  - `prepare-draft-release: wrote release-provenance.json`
  - `prepare-draft-release: wrote release-notes.md`
- Caller/I/O/ZIP/JSON errors produce one
  `prepare-draft-release: error: ...` line, no traceback, nonzero exit.
- Catch expected validation and `OSError`/`zipfile.BadZipFile`; do not broadly
  swallow programmer exceptions.

## Workflow changes

Modify `.github/workflows/firmware-build.yml`.

### Trigger and build job

- Under `push`, retain `main` branch and add quoted tag pattern `'v*'`.
- Keep top-level `permissions: contents: read`.
- Add firmware job output:
  `version: ${{ steps.project-version.outputs.version }}`.
- In project-version step, when `$GITHUB_REF` starts `refs/tags/`, require:
  - `$GITHUB_EVENT_NAME` is `push`;
  - `$GITHUB_REF_NAME` equals `v$version` exactly.
- PR/main/manual behavior unchanged.

### Release job

Add job `release`:

- `needs: firmware`;
- `if: github.event_name == 'push' && startsWith(github.ref, 'refs/tags/v')`;
- `runs-on: ubuntu-22.04`, timeout 15 minutes;
- no Nordic container;
- job-scoped `permissions: contents: write` only;
- default run shell Bash;
- no secrets other than automatic `${{ github.token }}` supplied only as
  `GH_TOKEN` to GitHub CLI steps;
- no `pull_request_target`, environment, OIDC, actions write, or privileged
  configuration.

Steps:

1. SHA-pinned checkout v7.0.1 at workflow tag commit, `fetch-depth: 0`,
   `persist-credentials: false`.
2. Fail-closed tag identity step using environment variables, never direct
   expression interpolation in shell:
   - run `scripts/project-version.py`;
   - version equals `needs.firmware.outputs.version` passed through step `env`;
   - tag equals `v<version>`;
   - `GITHUB_REF` equals `refs/tags/<tag>`;
   - `git rev-parse HEAD`, `git rev-list -n 1 <tag>`, and `$GITHUB_SHA` all
     equal;
   - export validated version/tag through `$GITHUB_OUTPUT`.
3. SHA-pinned `actions/download-artifact` v8.0.1:
   - exact name
     `firmware-v${{ needs.firmware.outputs.version }}-${{ github.sha }}`;
   - `path: dist`;
   - current run/repository defaults;
   - explicit `digest-mismatch: error`.
4. Run `prepare-draft-release.py` with validated tag/version and GitHub
   environment values. Pass values through `env`, quote every shell variable,
   output to `release-metadata`.
5. Before API write, require no existing release with a fail-closed REST probe:
   - call `gh api --include
     "repos/$GITHUB_REPOSITORY/releases/tags/$tag"` and capture its response;
   - HTTP success means a release exists: print one GitHub Actions `::error::`
     and exit nonzero;
   - only an exact HTTP 404 response means absent and permits creation;
   - any authentication, network, rate-limit, malformed-response, or other HTTP
     failure prints one GitHub Actions `::error::` and exits nonzero.

   Do not infer absence from a generic nonzero `gh` exit. Do not
   delete/edit/reuse an existing draft. Remove any private temporary response
   file with a shell trap; never print token-bearing headers.
6. Create release non-interactively:

```bash
gh release create "$tag" \
  --draft \
  --verify-tag \
  --title "LE Audio Receiver $tag" \
  --notes-file release-metadata/release-notes.md \
  "dist/le-audio-receiver-v${version}-nrf5340-e83-factory.zip" \
  "dist/le-audio-receiver-v${version}-nrf54l15-xiao-factory.zip" \
  dist/SHA256SUMS \
  release-metadata/release-provenance.json
```

Never use `--generate-notes`, `--latest`, `--prerelease`, `--clobber`,
`gh release edit`, or publish API calls.

7. Verify created release with `gh release view --json`: tag exact, `isDraft`
   true, `isPrerelease` false, and sorted asset names exactly the four expected
   names. Print release URL. Failure leaves draft unpublished and fails job.

If `gh release create` partially creates a draft before asset failure, rerun
must fail on existing release; deliberate human inspection/deletion is needed.
Do not automate destructive cleanup.

## Tests

### `scripts/test_draft_release.py`

Add as one new Python gate child. Build public fixtures by invoking the real
FR1 packager against copied valid fixture files/notes, or construct equivalent
valid ZIPs independently; do not depend on production build directories.

At minimum prove:

1. Happy path creates exact two metadata files, exact deterministic stdout,
   exact provenance schema/values/order, and required notes content.
2. Repeated identical input/output locations produce byte-identical metadata.
3. Tag/version/commit/NCS/repository/workflow/workflow-ref/run-id/attempt
   invalid or mismatched inputs fail before output creation.
4. Missing/extra/directory/symlink/nonregular artifact entries fail.
5. Top checksum malformed/unsorted/duplicate/wrong-name/wrong-digest fails.
6. Corrupt ZIP, duplicate/extra/traversal/wrong-order member fails.
7. Internal checksum and manifest schema/metadata/image size/hash/role/path/order
   mutations fail.
8. Existing output sentinel remains unchanged; handled output I/O failure is
   clean and leaves no staging sibling.
9. Error diagnostics have stable prefix, no traceback; success output contains
   no host paths.

Tests use CLI/filesystem/ZIP/JSON outputs, not private helper calls.

### Update `scripts/test_firmware_build_ci.py`

Extend static public workflow checks:

- push triggers exactly branch `main` plus tag `'v*'`;
- top permissions remain read; only release job has contents write;
- release condition is exact normal tag push and release needs firmware;
- firmware exposes validated version output and tag guard;
- exact download action SHA/name/path/digest policy;
- exact release preparation inputs;
- create command has `--draft`, `--verify-tag`, exact title/notes/assets;
- existing-release probe permits only exact HTTP 404, fails closed on every
  other API result, and post-create draft/assets are checked;
- forbidden release paths absent: publish, edit, clobber, generate-notes,
  pull_request_target, OIDC, privileged, J-Link, direct untrusted expression
  interpolation in run scripts;
- all five action uses references are full 40-hex SHAs.

New child changes inventory to 35 Twister + 5 exec-only + 22 Python = 62 unit
children; canonical gate becomes 65 total.

## Active inventory updates

In implementation commit only:

- `scripts/test-all.sh`: 22 Python, 62 unit, 65 canonical.
- `docs/testing/coverage-matrix.md`: Python 22, add
  `draft_release (test_draft_release.py)`, total 65. Keep current accepted FR2
  64/64 paragraph unchanged until FR3 remote acceptance; table reflects current
  executable inventory.

Do not update FR3 status, `AGENTS.md`, `STATUS.md`, `docs/design.md`, or
`PLANNED_FEATURES.md` acceptance claims in implementation commit.

## Focused verification and commit

Run:

```bash
python3 scripts/project-version.py
python3 scripts/test_firmware_build_ci.py
python3 scripts/test_package_firmware_release.py
python3 scripts/test_draft_release.py
python3 scripts/test_inventory.py --python
python3 scripts/check-test-matrix.py
git diff --check
```

Expected inventory: 35/5/22; 65 canonical total.

Inspect status, full diff, and recent log. Stage only:

- `docs/development/firmware-release-fr3-handoff.md`
- `.github/workflows/firmware-build.yml`
- `scripts/prepare-draft-release.py`
- `scripts/test_draft_release.py`
- `scripts/test_firmware_build_ci.py`
- `scripts/test-all.sh`
- `docs/testing/coverage-matrix.md`

Commit:

```text
ci: create draft releases from version tags
```

At clean commit run full `./scripts/test-all.sh`; require 65 PASS / 0 FAIL /
65 TOTAL and no unexplained warning. No local production rebuild is required:
firmware/version/build/package implementation is unchanged from accepted FR2.
Re-run focused tests and diff check; finish clean.

Do not write FR3 results or mark accepted. Orchestrator will inspect, push to
existing draft PR 8, and verify PR hosted run passes with release job skipped.
Actual tag/release acceptance remains blocked on explicit user approval and
appropriate merge/tag sequencing.

## Escalation

Stop without incomplete commit if:

- tag-only write authority cannot be isolated from PR/main/manual events;
- action/CLI semantics contradict pins or command shape;
- validator cannot independently prove FR1 release contract;
- deterministic atomic metadata output fails;
- inventory differs from 35/5/22 or any focused/canonical test fails;
- implementing requires real tag/release, destructive cleanup, broader
  permissions, OIDC, third-party actions, or architecture invention;
- two materially different attempts fail on one blocker.

Preserve worktree and return exact evidence, attempts, errors, status/diff, one
question, and smallest hypothesis. Never weaken checks or auto-clean a release.

## Return

Return files/behavior, schema/CLI/workflow contracts, exact pins, focused test
counts, inventory/matrix, implementation commit, canonical 65-child result,
clean status, deviations/blockers, PR hosted run pending, and explicit
statement that no tag/release/remote write occurred.

Do not push, open/edit PR, merge, tag, create/edit/delete/publish release,
amend, force-push, or add attribution.
