# FR3 acceptance handoff: record automatic draft-release evidence

Date: 2026-08-09

## Goal

Record complete local and GitHub-hosted FR3 evidence, mark FR3 accepted, and
leave FR4-FR5 open. This is documentation-only. Do not alter workflow, scripts,
tests, `VERSION`, release assets, tags, firmware, or hardware configuration.

FR3 acceptance is based on one successful trusted-main creation of the exact
untagged draft, followed by corrections to false post-create tag assumptions
and release-collision detection. The one-shot creation path cannot be rerun for
`v0.1.0` without violating its existing-release fail-closed contract. Record
the corrected read-only checks against that same draft and later hosted green
runs as part of the acceptance proof.

## In scope

1. Add `docs/development/firmware-release-fr3-results.md`.
2. Mark FR3 accepted in `docs/development/firmware-release-plan.md` and leave
   FR4-FR5 planned.
3. Refresh current-state summaries in `AGENTS.md`, `STATUS.md`, and
   `docs/design.md` from FR2/64 children to FR3/65 children.
4. Refresh current accepted-gate prose in
   `docs/testing/coverage-matrix.md`; do not alter historical evidence.
5. Change item F in `PLANNED_FEATURES.md` to say FR1-FR3 are accepted, exact
   untagged draft `v0.1.0` awaits FR4 hardware acceptance, and nothing is
   published yet.
6. Include this handoff in the documentation commit.

## Out of scope

- Any edit under `.github/`, `scripts/`, `src/`, `tests/`, `boards/`,
  `release/`, or root `VERSION`.
- Downloading, replacing, editing, publishing, deleting, or recreating draft
  release `367572702`.
- Creating or pushing `refs/tags/v0.1.0`.
- FR4 hardware execution, FR5 publication, public flashing closeout, MCUboot,
  DFU, signing, or firmware behavior changes.
- Re-running full canonical gate or production builds for documentation-only
  acceptance. Preserve already observed evidence exactly.

## Exact implementation and hosted evidence

Record these FR3 implementation/review commits:

- `8ef8a80` (`ci: create draft releases from version tags`)
- `a3eef05` (`fix: validate draft release metadata checks`)
- `4892a6a` (`ci: create release tags from trusted main`)
- `4c837af` (`fix: verify untagged draft releases`)
- `2532fea` (`fix: detect existing draft releases`)

The commit subject `ci: create release tags from trusted main` predates the
final clarified lifecycle. The implemented behavior creates an untagged draft;
GitHub creates the lightweight tag only on later manual publication. Do not
rewrite history or describe draft creation as tag creation.

Record merge commits and pull requests:

- PR 8: `https://github.com/qarnet/le-audio-receiver/pull/8`, merge
  `3d9a9186ec288484a637dac1dc7460319daf5e84`.
- PR 9: `https://github.com/qarnet/le-audio-receiver/pull/9`, merge
  `f5a6f6ba28029bd397ae6c62794053eac6c47bed`.
- PR 10: `https://github.com/qarnet/le-audio-receiver/pull/10`, merge
  `b70b978bc92356d0fbeeb31928890b8a0c64745e`.

Record hosted runs:

| Run | Event / exact SHA | Firmware | Release | Meaning |
|---|---|---|---|---|
| `31332633455` | PR, `4892a6abb352cb3370fb37c5fa281b7f80b21d01` | PASS, job `93293198539` | SKIPPED | Pull-request write isolation. |
| `31332962453` | main push, `3d9a9186ec288484a637dac1dc7460319daf5e84` | PASS, job `93294018344` | FAILED after creation, job `93294798104` | Created exact draft. Failure was only the obsolete post-create assumption that a draft already had a git tag. |
| `31333583467` | PR, `4c837afefc6e300ecaba35422ac8ebbb3e20cd82` | PASS, job `93295608888` | SKIPPED | Corrected untagged-draft validation passed in PR topology. |
| `31333867895` | main push, `f5a6f6ba28029bd397ae6c62794053eac6c47bed` | PASS, job `93296340441` | SKIPPED | Unchanged `VERSION` correctly skipped release. |
| `31334362361` | PR, `2532feab15d41db3b7bd41ce41d96adfc2e6a2da` | PASS, job `93297660875` | SKIPPED | Paginated draft-collision correction passed in PR topology. |
| `31334643418` | main push, `b70b978bc92356d0fbeeb31928890b8a0c64745e` | PASS, job `93298378307` | SKIPPED | Final merged workflow built successfully; unchanged `VERSION` correctly skipped release. |

Use each run URL in the form
`https://github.com/qarnet/le-audio-receiver/actions/runs/<run>`.

Record draft release `367572702`:

- title `LE Audio Receiver v0.1.0`;
- `tag_name=v0.1.0`, draft true, prerelease false;
- target commit
  `3d9a9186ec288484a637dac1dc7460319daf5e84`;
- created `2026-08-09T20:04:01Z`;
- no git ref `refs/tags/v0.1.0`; authenticated git-ref lookup returned exact
  HTTP 404 after final correction, which is expected until manual publication;
- release-by-tag REST lookup also returns 404 for this untagged draft, while
  authenticated paginated release-list lookup includes it;
- final exact read-only paginated list parser returned status 2, proving the
  corrected collision check detects existing `v0.1.0` draft without printing
  release bodies.

Record exact attached assets:

| Asset | Bytes | GitHub digest |
|---|---:|---|
| `SHA256SUMS` | 232 | `sha256:0da8d6b3aa1d64bbea773afb4864f067abe29662357c5546c593cd28c0bd1803` |
| `le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip` | 579408 | `sha256:e91e404c9f6016b357a0c6692e43f644ebe05ece5fe89c69df96776e73a54e7b` |
| `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip` | 621968 | `sha256:56086ae43d75ab779228fab2340190edefc26d88c429540c19973ae884beff1d` |
| `release-provenance.json` | 1265 | `sha256:1ea84062d12e3a5550151455132cb5426d51a4a914cceed1f536f60f13a793c9` |

Record source workflow artifact `9043541373`:

- name
  `firmware-v0.1.0-3d9a9186ec288484a637dac1dc7460319daf5e84`;
- size 1202122 bytes;
- digest
  `sha256:c35fb920066b31c541ad70f4a5f6bbb754f41d6926e9d03ff9acebecbf82d48c`;
- created `2026-08-09T20:03:39Z`, expires `2026-08-23T20:03:38Z`.

Record independent post-download validation:

- top-level checksum verification passed;
- both ZIP integrity and exact member/schema/manifest checks passed through
  `scripts/prepare-draft-release.py`;
- downloaded `release-provenance.json` was byte-equal to regenerated
  provenance;
- draft release notes were byte-equal to regenerated notes;
- exact tag name, draft/prerelease state, target commit, and four asset names
  passed corrected read-only verification;
- no asset was edited or replaced.

## Local evidence and current-state wording

Record FR3 local verification:

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

Do not imply FR3 published a release. Use explicit current boundary:

- FR3 automatic draft-release creation is accepted.
- Draft `v0.1.0` is private/unpublished and intentionally untagged.
- FR4 must download and test these exact draft assets on both hardware targets.
- Failed FR4 leaves the draft unpublished.
- FR5 owns manual publication and post-publication lightweight-tag checks.
- No public binary, hardware acceptance, MCUboot, or DFU claim exists yet.

In `docs/development/firmware-release-plan.md`:

- change stale FR1 and FR2 tails from `FR3-FR5 remain planned` to
  `FR4-FR5 remain planned`;
- add a bold FR3 acceptance paragraph dated 2026-08-09 naming implementation
  commit `8ef8a80`, corrections `a3eef05`, `4892a6a`, `4c837af`, `2532fea`,
  successful creation target `3d9a918...`, accepted merged state `b70b978...`,
  and the new FR3 results document;
- preserve FR4 and FR5 scope exactly.

In `STATUS.md`, update the top current-state block and add a concise FR3
acceptance section before historical P1 sections. In `AGENTS.md`, update only
the active current-status paragraph. In `docs/design.md`, replace only the
current authoritative FR2 paragraph with FR3 evidence while retaining FR2 as
historical evidence. In `docs/testing/coverage-matrix.md`, update only active
accepted-gate prose to 65 children and link the FR3 results; preserve historical
FR1/FR2 facts.

`PLANNED_FEATURES.md` is user-facing. Do not introduce U+2014 em dashes.

## Verification

Run:

```bash
python3 scripts/test_firmware_build_ci.py
python3 scripts/test_draft_release.py
python3 scripts/test_package_firmware_release.py
python3 scripts/test_inventory.py
git diff --check
python3 - <<'PY'
from pathlib import Path

public = [Path("PLANNED_FEATURES.md")]
bad = [str(path) for path in public if "\u2014" in path.read_text(encoding="utf-8")]
if bad:
    raise SystemExit("U+2014 found in: " + ", ".join(bad))
print("public-doc U+2014 check: PASS")
PY
```

Inspect `git status`, full diff, and recent log. Stage only these eight files:

- `docs/development/firmware-release-fr3-acceptance-handoff.md`
- `docs/development/firmware-release-fr3-results.md`
- `docs/development/firmware-release-plan.md`
- `AGENTS.md`
- `STATUS.md`
- `docs/design.md`
- `docs/testing/coverage-matrix.md`
- `PLANNED_FEATURES.md`

Stage exactly all eight paths and nothing else.
Commit once with:

```text
docs: record FR3 draft release acceptance
```

Do not push, merge, open a PR, amend, publish, edit remote release state, or add
AI/tool attribution. Return files changed, evidence recorded, verification
results, commit hash/message, blockers, deviations, and FR4 follow-up. Stop and
escalate before committing if any observed repository or GitHub evidence
contradicts this handoff.
