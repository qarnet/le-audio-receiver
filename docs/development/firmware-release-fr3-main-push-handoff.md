# FR3 correction handoff: automatic draft creation from trusted main

Date: 2026-08-09

## Goal

Correct FR3 release initiation to match the trusted-main model used by
`serial-mcp`: merging a project `VERSION` change to `main` must run the already
accepted firmware build/package path, then CI must create the draft release at
that exact trusted commit. The draft's `tagName` and `targetCommitish` reserve
`v<version>` at that commit; GitHub creates the lightweight git tag only when
the draft is manually published. Maintainers must not push release tags
manually.

Keep the LE Audio lifecycle difference: CI creates only a draft. It never edits
the draft, uploads replacement assets, publishes it, or marks it latest. FR4
must test the exact draft assets before manual publication.

No tag or release may be created while implementing or testing this correction.
The existing draft PR run must continue to build artifacts and skip the release
job. Merging PR 8 and the first real automatic draft run require later explicit
user approval after review.

## User-observable behavior

- Pull requests and manual dispatch build/package and upload workflow artifacts,
  with no release write job.
- A trusted `push` to `main` builds/packages normally.
- If that push changed root `VERSION` relative to `github.event.before`, the
  release job derives `v<version>`, proves the exact commit and artifact set,
  requires both tag and release to be absent, then creates one draft release
  with `--target "$GITHUB_SHA"`. GitHub does not create the git tag while the
  release stays a draft: the draft's `tagName` and `targetCommitish` reserve
  `v<version>` at that exact commit, and the lightweight tag is created
  automatically only when the draft is manually published after FR4.
- A normal `main` push that did not change `VERSION` skips the release job. This
  lets later documentation or maintenance commits use the same version without
  touching the pending or published release.
- Reusing an existing tag or release for a changed `VERSION` fails closed. CI
  never edits, reuses, deletes, clobbers, or publishes it.
- The created draft still has exactly the two factory ZIPs, top-level
  `SHA256SUMS`, and `release-provenance.json`; notes still block publication
  until FR4 exact-asset acceptance passes.

## In scope

- Replace tag-push workflow initiation with trusted-main automatic draft
  creation; the version tag is created by GitHub only at manual publication.
- Gate release creation on a root `VERSION` diff for the current main push.
- Prevent main release runs from being cancelled by a newer workflow run.
- Require both release and tag absence before the first write.
- Create an untagged draft at exact `GITHUB_SHA` through `gh release create
  --target`; the lightweight tag is created by GitHub at manual publication
  (FR4/FR5), not by draft creation.
- Update provenance workflow-ref contract from tag ref to `refs/heads/main`.
- Update FR3 workflow/CLI tests and active release-plan/handoff text.
- Focused tests, clean 65-child canonical gate, and one local correction commit.

## Out of scope

- Merging PR 8, creating `v0.1.0`, creating/deleting a real draft, or any other
  remote write during this handoff.
- Publishing, editing, replacing assets, reusing a partial draft, marking latest,
  or automatic destructive cleanup.
- FR4 hardware acceptance and FR5 public closeout.
- MCUboot, DFU, signing, OIDC, environments, rulesets, immutable-release setup,
  third-party actions, package installation, or firmware behavior changes.
- Changing `VERSION` from `0.1.0` or rebuilding production firmware locally.

## Grounding

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
- Hosted evidence: draft release ID 367572702 has `tag_name=v0.1.0`, draft
  true, target commit `3d9a918...`, and the exact four assets. `gh release
  view v0.1.0` succeeds, but REST
  `GET /repos/{owner}/{repo}/releases/tags/v0.1.0` returns HTTP 404 because
  the draft is untagged, while the authenticated
  `GET /repos/{owner}/{repo}/releases` list includes the draft. Therefore the
  release collision proof must use the authenticated paginated release list,
  not the release-by-tag endpoint. The git-ref endpoint
  `GET /repos/{owner}/{repo}/git/ref/tags/{tag}` returns HTTP 404 when no tag
  exists and stays the tag-collision proof.
- `origin/main` has no root `VERSION`; PR 8 adds `VERSION` 0.1.0. Therefore the
  eventual merge push changes `VERSION` and requests the first release exactly
  once.
- Repository currently has no release and no `v0.1.0` tag. The historical
  `v0.0.1` tag is annotated and unrelated.
- Current FR3 commits are `8ef8a80` plus review fix `a3eef05`. Local clean gate
  at `a3eef05` is 65 PASS / 0 FAIL / 65 TOTAL; hosted PR run `31330373763`
  passed firmware and skipped release.

## Workflow correction

Modify `.github/workflows/firmware-build.yml`.

### Triggers and concurrency

- Under `push`, keep only branch `main`; remove tag pattern `'v*'` entirely.
- Keep pull request and manual dispatch triggers.
- Keep top-level `permissions: contents: read`.
- Change `concurrency.cancel-in-progress` to the expression
  `${{ github.event_name == 'pull_request' }}`. Stale PR builds may cancel;
  trusted main and manual runs may not. A newer main push must never cancel a
  run after it begins release creation.

### Firmware job outputs and VERSION-change decision

Keep firmware output `version` and add output:

```yaml
release-requested: ${{ steps.project-version.outputs.release-requested }}
```

In the existing `Project version` step:

- pass `${{ github.event.before }}` through step `env` as `BEFORE_SHA`; never
  interpolate it directly into shell;
- retain current checkout-HEAD, project-version, and fixed 0.1.0 checks;
- initialize `release_requested=false`;
- only for `GITHUB_EVENT_NAME=push` and `GITHUB_REF=refs/heads/main`:
  - require `BEFORE_SHA` exactly 40 lowercase hex and not all zero;
  - require `BEFORE_SHA^{commit}` exists in the fetch-depth-0 checkout;
  - require `GITHUB_SHA` is the checked-out commit;
  - run `git diff --quiet "$BEFORE_SHA" "$GITHUB_SHA" -- VERSION`;
  - set `release_requested=true` only when that command reports a difference;
  - distinguish diff status 1 (changed) from status greater than 1 (error), and
    fail on an error rather than treating it as a release request;
- emit both `version` and `release-requested` through `$GITHUB_OUTPUT`.

PR and manual events must emit `release-requested=false`. Do not infer release
intent from branch names, commit messages, tags, or artifact names.

### Release job condition and identity

Change release job condition to exactly:

```yaml
if: github.event_name == 'push' && github.ref == 'refs/heads/main' && needs.firmware.outputs.release-requested == 'true'
```

Keep `needs: firmware`, ubuntu-22.04, 15-minute timeout, no container, Bash,
and job-scoped `contents: write` only.

Rename checkout/identity labels as useful. Checkout current main-push commit
with fetch-depth 0 and persisted credentials disabled.

Identity step must:

- derive version from `scripts/project-version.py`;
- compare it to firmware job output passed through `env`;
- derive `tag=v$version`;
- require event `push`, ref `refs/heads/main`, ref name `main`;
- require `git rev-parse HEAD == GITHUB_SHA`;
- require firmware `release-requested` output passed through `env` equals
  `true`;
- export validated version/tag through `$GITHUB_OUTPUT`.

No local tag exists yet and no `git rev-list <tag>` check belongs here.

### Artifact and metadata

Keep exact same-run download action SHA, artifact name, path, and digest policy.

Change `prepare-draft-release.py` workflow-ref contract to exactly:

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
- do not print captured headers or bodies.

This is a one-shot version release. Never reuse an existing lightweight or
annotated tag, existing draft, or published release.

### Draft creation with target commit (tag at publication)

Change create command to:

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

Remove `--verify-tag`. Do not add generated notes, latest, prerelease, clobber,
edit, upload, delete, or publish operations.

### Post-create verification

Retain release verification and additionally request/check
`targetCommitish` equals `GITHUB_SHA` passed as a Python argument or env value.
The draft is untagged: GitHub creates `refs/tags/<tag>` only at manual
publication, so no git-ref query belongs here. The exact release checks are
`tagName`, draft true, prerelease false, `targetCommitish` equals
`GITHUB_SHA`, the exact four sorted asset names, and the release URL printed
last.

Use `gh release view --json` piped/filed into a small stdlib Python check, not
`jq`. Print release URL only after every release field and asset passes.
Failure leaves draft unpublished and fails job; no cleanup. The git-ref
endpoint appears exactly once in the whole workflow: the pre-write collision
probe. FR5 verifies the lightweight tag (`ref`, `object.type`, `object.sha`)
after manual publication.

## CLI and documentation changes

Modify:

- `scripts/prepare-draft-release.py`: usage example and exact
  `WORKFLOW_REF_TEMPLATE`/validation now use `refs/heads/main`, independent of
  tag. Keep provenance schema unchanged except value.
- `scripts/test_draft_release.py`: happy-path expected workflow ref and invalid
  cases use main-ref contract; ensure tag-ref and other branch refs fail.
- `docs/development/firmware-release-plan.md`: lifecycle says a trusted main
  push that changes root VERSION creates an untagged draft whose tagName and
  targetCommitish reserve the version at the exact main commit; later
  unchanged-version pushes skip; CI never publishes; manual publication after
  FR4 creates the lightweight tag, and FR5 verifies it.
- `docs/development/firmware-release-fr3-handoff.md`: correct goal,
  user-observable behavior, workflow-ref examples, trigger/condition/identity,
  collision probes, create command, tests, and return text to final automatic
  trusted-main design. Remove claims that a pre-existing tag or tag push is
  required. Keep its historical date and all still-valid validator details.

Do not rewrite historical FR0/FR2 handoff files; active plan plus final FR3
handoff carry the correction.

## Tests

Update `scripts/test_firmware_build_ci.py` to prove public workflow behavior:

- push trigger has exactly branch main and no tags block/pattern;
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
- metadata workflow ref is passed unchanged and CLI tests pin refs/heads/main;
- post-create release checks `tagName`, draft, prerelease, `targetCommitish`,
  and the exact four sorted assets; the git-ref endpoint appears exactly once,
  in the pre-write collision probe only, and no tag-check/tag-data appears in
  the post-create step;
- all five external action uses remain full 40-hex SHAs;
- no direct `${{ ... }}` interpolation appears in any run script.

Keep Python child count 22 and canonical total 65. No inventory or coverage
matrix count change is needed.

## Verification and commit

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
65 TOTAL and no new warning. Re-run focused tests and `git show --check HEAD`;
finish clean.

Do not update FR3 acceptance claims or results yet. Do not push, merge, mark PR
ready, create/edit/delete/publish a release, create/push/delete a tag, touch
hardware, amend, force-push, or add attribution.

## Escalation

Stop without incomplete commit if GitHub CLI semantics contradict automatic
tag creation, VERSION-change detection cannot distinguish changed/error states,
main runs can still cancel after write begins, safe tag/release collision checks
cannot be isolated, tests require weakening, or any focused/canonical test has
an unexplained failure. Return exact evidence and one precise question.

## Return

Return files and behavior, trusted-main draft trigger and one-shot collision
semantics, untagged-draft/targetCommitish tag reservation, provenance ref
change, test counts, commit hash/message, canonical
gate result, clean status, deviations/blockers, and explicit confirmation that
no tag, release, merge, push, PR edit, or hardware action occurred.
