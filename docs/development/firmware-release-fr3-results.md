# FR3 results — automatic draft release creation

Accepted: 2026-08-09.  Handoff
`docs/development/firmware-release-fr3-acceptance-handoff.md`; implementation
commit `8ef8a80` (`ci: create draft releases from version tags`); review
corrections `a3eef05` (`fix: validate draft release metadata checks`),
`4892a6a` (`ci: create release tags from trusted main`), `4c837af`
(`fix: verify untagged draft releases`), and `2532fea`
(`fix: detect existing draft releases`); acceptance commit (this document's
commit) `docs: record FR3 draft release acceptance`.

FR3 adds trusted-main automatic release initiation to the accepted FR2
build/package path: a `main` push that changes the root `VERSION` runs the
accepted build/package pipeline, then a protected `contents: write` release
job creates one draft GitHub Release with provenance and exact attachments
whose `tagName`/`targetCommitish` reserve `v<version>` at the exact main
commit.  Drafts stay untagged: GitHub creates the lightweight version tag
only when the draft is manually published after FR4.  A main push without a
`VERSION` change skips the release job.  CI never auto-publishes.

The commit subject `ci: create release tags from trusted main` (`4892a6a`)
predates the final clarified lifecycle.  The implemented behavior creates an
untagged draft; GitHub creates the lightweight tag only on later manual
publication.  This document does not rewrite history and does not describe
draft creation as tag creation.

FR3 acceptance is based on one successful trusted-main creation of the exact
untagged draft, followed by corrections to false post-create tag assumptions
and release-collision detection.  The one-shot creation path cannot be rerun
for `v0.1.0` without violating the existing-release fail-closed contract, so
the corrected read-only checks were recorded against that same draft and
later hosted green runs as part of the acceptance proof.

## Merge commits and pull requests

- PR 8: `https://github.com/qarnet/le-audio-receiver/pull/8`, merge
  `3d9a9186ec288484a637dac1dc7460319daf5e84`.
- PR 9: `https://github.com/qarnet/le-audio-receiver/pull/9`, merge
  `f5a6f6ba28029bd397ae6c62794053eac6c47bed`.
- PR 10: `https://github.com/qarnet/le-audio-receiver/pull/10`, merge
  `b70b978bc92356d0fbeeb31928890b8a0c64745e`.

## Hosted runs

| Run | Event / exact SHA | Firmware | Release | Meaning |
|---|---|---|---|---|
| `31332633455` | PR, `4892a6abb352cb3370fb37c5fa281b7f80b21d01` | PASS, job `93293198539` | SKIPPED | Pull-request write isolation. |
| `31332962453` | main push, `3d9a9186ec288484a637dac1dc7460319daf5e84` | PASS, job `93294018344` | FAILED after creation, job `93294798104` | Created exact draft. Failure was only the obsolete post-create assumption that a draft already had a git tag. |
| `31333583467` | PR, `4c837afefc6e300ecaba35422ac8ebbb3e20cd82` | PASS, job `93295608888` | SKIPPED | Corrected untagged-draft validation passed in PR topology. |
| `31333867895` | main push, `f5a6f6ba28029bd397ae6c62794053eac6c47bed` | PASS, job `93296340441` | SKIPPED | Unchanged `VERSION` correctly skipped release. |
| `31334362361` | PR, `2532feab15d41db3b7bd41ce41d96adfc2e6a2da` | PASS, job `93297660875` | SKIPPED | Paginated draft-collision correction passed in PR topology. |
| `31334643418` | main push, `b70b978bc92356d0fbeeb31928890b8a0c64745e` | PASS, job `93298378307` | SKIPPED | Final merged workflow built successfully; unchanged `VERSION` correctly skipped release. |

Run URL form:
`https://github.com/qarnet/le-audio-receiver/actions/runs/<run>`.

## Draft release `367572702`

- title `LE Audio Receiver v0.1.0`;
- `tag_name=v0.1.0`, draft true, prerelease false;
- target commit
  `3d9a9186ec288484a637dac1dc7460319daf5e84`;
- created `2026-08-09T20:04:01Z`;
- no git ref `refs/tags/v0.1.0`; authenticated git-ref lookup returned exact
  HTTP 404 after the final correction, which is expected until manual
  publication;
- release-by-tag REST lookup also returns 404 for this untagged draft, while
  the authenticated paginated release-list lookup includes it;
- the final exact read-only paginated list parser returned status 2, proving
  the corrected collision check detects the existing `v0.1.0` draft without
  printing release bodies.

## Exact attached assets

| Asset | Bytes | GitHub digest |
|---|---:|---|
| `SHA256SUMS` | 232 | `sha256:0da8d6b3aa1d64bbea773afb4864f067abe29662357c5546c593cd28c0bd1803` |
| `le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip` | 579408 | `sha256:e91e404c9f6016b357a0c6692e43f644ebe05ece5fe89c69df96776e73a54e7b` |
| `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip` | 621968 | `sha256:56086ae43d75ab779228fab2340190edefc26d88c429540c19973ae884beff1d` |
| `release-provenance.json` | 1265 | `sha256:1ea84062d12e3a5550151455132cb5426d51a4a914cceed1f536f60f13a793c9` |

## Source workflow artifact `9043541373`

- name
  `firmware-v0.1.0-3d9a9186ec288484a637dac1dc7460319daf5e84`;
- size 1202122 bytes;
- digest
  `sha256:c35fb920066b31c541ad70f4a5f6bbb754f41d6926e9d03ff9acebecbf82d48c`;
- created `2026-08-09T20:03:39Z`, expires `2026-08-23T20:03:38Z`.

## Independent post-download validation

- top-level checksum verification passed;
- both ZIP integrity and exact member/schema/manifest checks passed through
  `scripts/prepare-draft-release.py`;
- downloaded `release-provenance.json` was byte-equal to regenerated
  provenance;
- draft release notes were byte-equal to regenerated notes;
- exact tag name, draft/prerelease state, target commit, and four asset names
  passed corrected read-only verification;
- no asset was edited or replaced.

## Local verification

- canonical gate: **65 PASS / 0 FAIL / 65 TOTAL**;
- composition: 35 Twister + 5 exec-only + 22 Python + coverage + matrix +
  BSim Stage 1;
- focused firmware workflow/version tests: 24/24;
- focused draft-release validator tests: 48/48;
- focused packager tests: 19/19;
- inventory: 35/5/22;
- build contract: 95/95;
- coverage unchanged: population 36, 4674/5130 lines, 2030/2824 branches,
  358/358 functions;
- BSim pins unchanged and byte-identical.

## Current boundary

- FR3 automatic draft-release creation is accepted.
- Draft `v0.1.0` is private/unpublished and intentionally untagged.
- FR4 must download and test these exact draft assets on both hardware
  targets.
- Failed FR4 leaves the draft unpublished.
- FR5 owns manual publication and post-publication lightweight-tag checks.
- No public binary, hardware acceptance, MCUboot, or DFU claim exists yet.

## Status

FR3 `ACCEPTED`.  FR4-FR5 remain planned in
`docs/development/firmware-release-plan.md`.  FR3 created a private,
unpublished, untagged draft release only; no git tag, published binary,
hardware acceptance, MCUboot, or DFU exists yet.
