# FR3 handoff: automatic draft release publication from trusted main

Date: 2026-08-09

## Goal

Implement FR3 from `docs/development/firmware-release-plan.md`: a trusted
`main` push that changes the root `VERSION` file must build the same factory
tuples as FR2, pass exact version/commit and artifact checks, then have CI
create one **draft** GitHub Release with exact factory ZIPs, top-level
checksums, and deterministic provenance. The draft's `tagName` and
`targetCommitish` reserve `v<version>` at the exact main commit; GitHub
creates the lightweight git tag only when the draft is manually published.
Maintainers must not push release tags manually; CI owns release initiation.

FR3 implementation must not create or push a tag, create a release during
local/PR verification, publish any release, or begin hardware acceptance.
Merging PR 8 and the first real automatic draft run require later explicit
user approval after implementation review.

## User-observable behavior

- Pull requests and manual dispatch build/package and upload workflow
  artifacts, with no release write job.
- A trusted `push` to `main` builds and packages normally.
- If that push changed root `VERSION` relative to `github.event.before`, the
  release job derives `v<version>`, proves the exact commit and artifact set,
  requires both tag and release to be absent, then creates one draft release
  with `--target "$GITHUB_SHA"`. GitHub does not create the git tag while the
  release stays a draft: the draft's `tagName` and `targetCommitish` reserve
  `v<version>` at that exact commit, and the lightweight tag is created
  automatically only when the draft is manually published after FR4.
- A normal `main` push that did not change `VERSION` skips the release job.
  This lets later documentation or maintenance commits use the same version
  without touching the pending or published release.
- Reusing an existing tag or release for a changed `VERSION` fails closed. CI
  never edits, reuses, deletes, clobbers, or publishes it.
- The created draft has exactly the two target ZIPs, top-level `SHA256SUMS`,
  and `release-provenance.json`; draft notes explicitly say hardware
  acceptance and manual publication remain pending.
- Any mismatch, malformed file, existing release or tag, checksum/ZIP/manifest
  error, or API/upload error fails. Workflow never auto-publishes, edits,
  clobbers, or deletes a release.

## In scope

- Trusted-main automatic draft creation replacing manual tag-push initiation;
  the version tag is created by GitHub only at manual publication.
- Root `VERSION` diff gate for the current main push.
- Concurrency that prevents a newer main run from cancelling a run that began
  release creation.
- Fail-closed absence probes for both the release and the git tag before the
  first write.
- One untagged draft release at exact `GITHUB_SHA` through `gh release create
  --target`; the lightweight tag is created by GitHub at manual publication,
  not by draft creation.
- Provenance workflow-ref contract `refs/heads/main`.
- Static workflow contract tests and the already-accepted Python gate child
  for the release preparation CLI.
- Local implementation commit, focused verification, and clean canonical gate.

## Out of scope

- Merging PR 8, creating `v0.1.0`, creating/deleting a real draft, or any
  other remote write in this handoff.
- Publishing a release or marking it latest; GitHub-hosted CI must never
  publish.
- Editing draft assets after creation, reusing a partial draft, or automatic
  destructive cleanup (FR4 owns exact-asset validation; failed creation may
  need deliberate human cleanup).
- Hardware flashing/acceptance (FR4) and final public notes/instructions/
  manual publication/clean-machine verification (FR5).
- MCUboot, DFU, signing keys, signed firmware, cryptographic artifact
  attestations, OIDC, or changes to factory artifacts.
- New GitHub environment/ruleset/branch protection. FR3 protects write
  authority inside the workflow with event/job conditions and job-scoped
  permissions.
- Third-party actions, caches, matrices, package installation, J-Link, secrets,
  raw build trees, or maintainer debug bundles.

## Grounding and immutable pins

Retain all FR2 pins and add:

| Dependency | Version | Immutable reference |
|---|---|---|
| `actions/download-artifact` | `v8.0.1` | `3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c` |

Grounding:

- `serial-mcp/.github/workflows/ci.yml` gates its privileged release call with
  `github.event_name == 'push' && github.ref == 'refs/heads/main'` and passes
  immutable `github.sha`.
- `serial-mcp/.github/workflows/release.yml` derives `v<version>` and uses
  `gh release create --target "$SHA"`; no maintainer `git push` creates its
  release tag.
- Installed GitHub CLI 2.97.0 documents that `--target` selects the branch or
  full commit for the tag. Hosted evidence (main run `31332962453` and
  `serial-mcp/.github/workflows/release.yml` lines 225-228) shows a draft
  release stays untagged: `git/ref/tags/<tag>` does not exist until the draft
  is published, at which point GitHub creates the lightweight tag at the
  stored target SHA. `--verify-tag` forbids automatic tag creation and must
  not be used.
- `origin/main` has no root `VERSION`; PR 8 adds `VERSION` 0.1.0. The eventual
  merge push therefore changes `VERSION` and requests the first release
  exactly once.
- Repository currently has no release and no `v0.1.0` tag. The historical
  `v0.0.1` tag is annotated and unrelated.
- Hosted evidence: draft release ID 367572702 has `tag_name=v0.1.0`, draft
  true, target commit `3d9a918...`, and the exact four assets. `gh release
  view v0.1.0` succeeds, but REST
  `GET /repos/{owner}/{repo}/releases/tags/v0.1.0` returns HTTP 404 because
  the draft is untagged, while the authenticated
  `GET /repos/{owner}/{repo}/releases` list includes the draft. Therefore the
  release collision proof must use the authenticated paginated release list,
  not the release-by-tag endpoint. The git-ref endpoint
  `GET /repos/{owner}/{repo}/git/ref/tags/{tag}` returns HTTP 404 when no tag
  exists and stays the tag-collision proof; use `gh api --include` so only an
  observed 404 permits continuation; authentication, network, rate-limit, or
  other API failures must stop before any write.
- Current public repository has no release, environment, ruleset, or branch
  protection. Job-scoped `contents: write` must therefore exist only on a
  trusted main push that changed `VERSION`, after read-only build succeeds.
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
  --workflow-ref qarnet/le-audio-receiver/.github/workflows/firmware-build.yml@refs/heads/main \
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
  `<repository>/.github/workflows/firmware-build.yml@refs/heads/main` —
  the trusted-main contract, independent of the tag. Tag refs and other
  branch refs are rejected.
- Run ID and attempt canonical positive decimal integers.
- Reject control characters/newlines in string inputs, including the
  artifact-directory and output-directory path arguments, before any
  filesystem action.
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
- each ZIP's `FLASHING.md` is strict UTF-8;
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
    "workflow_ref": "qarnet/le-audio-receiver/.github/workflows/firmware-build.yml@refs/heads/main"
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

### Trigger and concurrency

- Under `push`, keep only branch `main`; remove the tag pattern entirely.
- Keep pull request and manual dispatch triggers.
- Keep top-level `permissions: contents: read`.
- `concurrency.cancel-in-progress` is the expression
  `${{ github.event_name == 'pull_request' }}`: stale PR builds may cancel;
  trusted main and manual runs may not. A newer main push must never cancel a
  run after it begins release creation.

### Firmware job outputs and VERSION-change decision

- Keep firmware output `version` and add output
  `release-requested: ${{ steps.project-version.outputs.release-requested }}`.
- In the existing `Project version` step, pass `${{ github.event.before }}`
  through step `env` as `BEFORE_SHA`; never interpolate it directly into
  shell. Retain current checkout-HEAD, project-version, and fixed 0.1.0
  checks. Initialize `release_requested=false`.
- Only for `GITHUB_EVENT_NAME=push` and `GITHUB_REF=refs/heads/main`:
  - require `BEFORE_SHA` exactly 40 lowercase hex and not all zero;
  - require `BEFORE_SHA^{commit}` exists in the fetch-depth-0 checkout;
  - require `GITHUB_SHA` is the checked-out commit;
  - run `git diff --quiet "$BEFORE_SHA" "$GITHUB_SHA" -- VERSION`;
  - set `release_requested=true` only when that command reports a difference
    (status 1); a status greater than 1 is an error and fails the step, never
    a release request;
  - emit both `version` and `release-requested` through `$GITHUB_OUTPUT`.
- PR and manual events must emit `release-requested=false`. Do not infer
  release intent from branch names, commit messages, tags, or artifact names.

### Release job condition and identity

Release job condition is exactly:

```yaml
if: github.event_name == 'push' && github.ref == 'refs/heads/main' && needs.firmware.outputs.release-requested == 'true'
```

Keep `needs: firmware`, ubuntu-22.04, 15-minute timeout, no container, Bash,
and job-scoped `permissions: contents: write` only.

Identity step:

- derive version from `scripts/project-version.py`;
- compare it to firmware job output passed through `env`;
- derive `tag=v$version`;
- require event `push`, ref `refs/heads/main`, ref name `main`;
- require firmware `release-requested` output passed through `env` equals
  `true`;
- require `git rev-parse HEAD == GITHUB_SHA`;
- export validated version/tag through `$GITHUB_OUTPUT`.

No local tag exists yet and no `git rev-list <tag>` check belongs here.

### Artifact and metadata

Keep the exact same-run download action SHA, artifact name, path, and digest
policy.

`prepare-draft-release.py` workflow-ref contract is exactly:

```text
qarnet/le-audio-receiver/.github/workflows/firmware-build.yml@refs/heads/main
```

The workflow passes `${{ github.workflow_ref }}` through `env` as before. All
other CLI validation, artifact validation, deterministic notes/provenance, and
atomic output behavior remain unchanged.

### Fail-closed tag and release absence

Before any write, prove both the release and the git tag absent, using
separate private `mktemp` files removed by one trap:

1. Release collision: an authenticated paginated release list
   `gh api --paginate --slurp "repos/$GITHUB_REPOSITORY/releases?per_page=100"`
   into a private file. Draft releases are untagged, so the release-by-tag
   REST lookup (`releases/tags/<tag>`) returns 404 even when a draft exists
   and cannot be used as the absence proof. Any nonzero API status prints one
   `::error::existing-release list probe failed for tag <tag>` and exits.
   Parse the slurped JSON (outer list of pages, every page a list, every
   record an object with string `tag_name`) with inline stdlib Python, tag
   passed as argv; malformed shape fails nonzero. If any record has exact
   `tag_name == tag`, the shell emits `::error::release already exists for
   tag <tag>` and exits. Never use `gh release view` generic failure, jq,
   grep over JSON, or print release bodies.
2. Tag collision: `gh api --include "repos/$GITHUB_REPOSITORY/git/ref/tags/$tag"`
   with the fail-closed exact-404 rule below.

For the tag probe:

- HTTP success means collision and fails with one `::error::`;
- only an exact HTTP 404 status permits continuation;
- auth, network, rate-limit, malformed response, or any other status fails;
- never print captured headers or bodies.

This is a one-shot version release. Never reuse an existing lightweight or
annotated tag, existing draft, or published release.

### Draft creation with target commit (tag at publication)

Create command:

```bash
gh release create "$tag" \
  --target "$GITHUB_SHA" \
  --draft \
  --title "LE Audio Receiver $tag" \
  --notes-file release-metadata/release-notes.md \
  "dist/le-audio-receiver-v${version}-nrf5340-e83-factory.zip" \
  "dist/le-audio-receiver-v${version}-nrf54l15-xiao-factory.zip" \
  dist/SHA256SUMS \
  release-metadata/release-provenance.json
```

Never use `--verify-tag`, `--generate-notes`, `--latest`, `--prerelease`,
`--clobber`, `gh release edit`, `gh release upload`, or publish API calls.

### Post-create verification

Verify the created release and additionally request/check
`targetCommitish` equals `GITHUB_SHA` passed as a Python argument or env
value. The draft is untagged: GitHub creates `refs/tags/<tag>` only at manual
publication, so no git-ref query belongs here. Exact checks: `tagName`,
draft true, prerelease false, `targetCommitish` equals `GITHUB_SHA`, the
exact four sorted asset names, and the release URL printed last.

Use `gh release view --json` piped/filed into a small stdlib Python check,
not `jq`. Print the release URL only after every release field and asset
passes. Failure leaves draft unpublished and fails job; no cleanup. The
git-ref endpoint appears exactly once in the whole workflow: the pre-write
collision probe. FR5 verifies the lightweight tag (`ref`, `object.type`,
`object.sha`) after manual publication. If `gh release create` partially
creates a draft before asset failure, rerun must fail on existing
release/tag; deliberate human inspection/deletion is needed. Do not automate
destructive cleanup.

## Tests

### `scripts/test_draft_release.py`

Already one Python gate child. Build public fixtures by invoking the real
FR1 packager against copied valid fixture files/notes, or construct equivalent
valid ZIPs independently; do not depend on production build directories.

At minimum prove:

1. Happy path creates exact two metadata files, exact deterministic stdout,
   exact provenance schema/values/order (including workflow ref
   `@refs/heads/main`), and required notes content.
2. Repeated identical input/output locations produce byte-identical metadata.
3. Tag/version/commit/NCS/repository/workflow/workflow-ref/run-id/attempt
   invalid or mismatched inputs fail before output creation; the workflow-ref
   contract accepts only `refs/heads/main` and rejects tag refs and other
   branch refs.
4. Missing/extra/directory/symlink/nonregular artifact entries fail.
5. Top checksum malformed/unsorted/duplicate/wrong-name/wrong-digest fails.
6. Corrupt ZIP, duplicate/extra/traversal/wrong-order member fails.
7. Internal checksum and manifest schema/metadata/image size/hash/role/path/order
   mutations fail, and an invalid UTF-8 `FLASHING.md` fails.
8. Existing output sentinel remains unchanged; handled output I/O failure is
   clean and leaves no staging sibling; control characters/newlines in path
   arguments fail before any filesystem action.
9. Error diagnostics have stable prefix, no traceback; success output contains
   no host paths.

Tests use CLI/filesystem/ZIP/JSON outputs, not private helper calls.

### Update `scripts/test_firmware_build_ci.py`

Extend static public workflow checks:

- push triggers exactly branch `main` and no tags block/pattern;
- concurrency cancellation expression is PR-only;
- firmware exposes exact `release-requested` output;
- event-before value is passed through env, validated, and used in an exact
  VERSION-only diff with status 1 vs error handling;
- PR/manual paths default false;
- release condition is exact trusted main push plus true output;
- identity checks event/ref/ref-name/current SHA and no pre-existing tag check;
- release collision uses the authenticated paginated `--slurp` release list
  with an exact `tag_name` scan (no release-by-tag REST endpoint), and the
  git-tag probe keeps the exact-404 fail-closed contract, both with trap
  files;
- create command includes exact `--target "$GITHUB_SHA"` and `--draft`, and
  excludes `--verify-tag` plus every old forbidden mutation/publication path;
- metadata workflow ref is passed unchanged and CLI tests pin `refs/heads/main`;
- post-create release checks `tagName`, draft, prerelease, `targetCommitish`,
  and the exact four sorted assets; the git-ref endpoint appears exactly once,
  in the pre-write collision probe only, and no tag-check/tag-data appears in
  the post-create step;
- all five action uses references are full 40-hex SHAs;
- no direct `${{ ... }}` interpolation appears in any run script.

Python child count stays 22 and canonical total stays 65; no inventory or
coverage matrix count change is needed.

## Active documentation updates

In implementation commit only:

- `docs/development/firmware-release-plan.md`: lifecycle and FR3 phase
  describe the trusted-main model (VERSION-changing main push creates an
  untagged draft whose tagName and targetCommitish reserve the version at the
  exact main commit; unchanged-version pushes skip; CI never publishes;
  manual publication after FR4 creates the lightweight tag, and FR5 verifies
  it).
- `docs/development/firmware-release-fr3-handoff.md`: this document, carrying
  the final automatic trusted-main design.

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

- `.github/workflows/firmware-build.yml`
- `scripts/prepare-draft-release.py`
- `scripts/test_draft_release.py`
- `scripts/test_firmware_build_ci.py`
- `docs/development/firmware-release-plan.md`
- `docs/development/firmware-release-fr3-handoff.md`
- `docs/development/firmware-release-fr3-main-push-handoff.md`

Commit:

```text
ci: create release tags from trusted main
```

At clean commit run full `./scripts/test-all.sh`; require 65 PASS / 0 FAIL /
65 TOTAL and no new warning. No local production rebuild is required:
firmware/version/build/package implementation is unchanged from accepted FR2.
Re-run focused tests and `git show --check HEAD`; finish clean.

Do not write FR3 results or mark accepted. Orchestrator will inspect, push to
existing draft PR 8, and verify the PR hosted run passes with the release job
skipped. Actual merge/tag/release acceptance remains blocked on explicit user
approval and appropriate merge/tag sequencing.

## Escalation

Stop without incomplete commit if:

- automatic tag creation semantics contradict the installed GitHub CLI;
- VERSION-change detection cannot distinguish changed (1) from error states;
- main runs can still cancel after write begins;
- safe tag/release collision checks cannot be isolated;
- tests require weakening, or any focused/canonical test fails;
- implementing requires real tag/release, destructive cleanup, broader
  permissions, OIDC, third-party actions, or architecture invention;
- two materially different attempts fail on one blocker.

Preserve worktree and return exact evidence, attempts, errors, status/diff, one
question, and smallest hypothesis. Never weaken checks or auto-clean a release.

## Return

Return files/behavior, trusted-main draft trigger and one-shot collision
semantics, untagged-draft/targetCommitish tag reservation, provenance ref
change, schema/CLI/workflow contracts, exact pins, focused test
counts, inventory/matrix, implementation commit, canonical 65-child result,
clean status, deviations/blockers, PR hosted run pending, and explicit
statement that no tag/release/merge/push/PR edit or hardware action occurred.

Do not push, open/edit PR, merge, tag, create/edit/delete/publish release,
amend, force-push, or add attribution.
