---
id: PB-006
title: Create replacement nRF54L15 release candidate
status: Done
assignee: []
created_date: '2026-09-13 01:24'
updated_date: '2026-09-20 19:24'
labels:
  - 'size:S'
  - 'area:release'
dependencies:
  - PB-031
priority: p1
type: feature
ordinal: 6000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

The failed `v0.1.0` draft release and its assets were deleted 2026-09-19 after serving as a stable harness baseline. `v0.1.0` is now free because no GitHub release or `refs/tags/v0.1.0` exists, but no fresh candidate has been created. Nothing was published.

### Desired outcome

After PB-031 human merge and green hosted gates, trusted-main creates one fresh immutable nRF54L15 `v0.1.0` draft candidate from its exact new commit.

### Scope

- Preserve root `VERSION` as `0.1.0`.
- Require PB-031 human merge and green hosted gates before candidate creation.
- Verify no GitHub release or git-tag collision exists for `v0.1.0`.
- Create one exact nRF54L15 factory ZIP, top-level `SHA256SUMS`, and workflow provenance bound to the trusted-main commit.
- Verify draft target metadata and private, unpublished, untagged state.

### Non-goals

- Restore historical failed assets.
- In-place clobbering, manual upload, or manual tag creation.
- Publish a release.
- Perform RH4 or FR4 acceptance, owned by PB-007.

### Technical context

Historical FR4 evidence for failed draft `367572702` remains retained. After deletion, `gh release view v0.1.0` fails, the GitHub release list is empty, and `refs/tags/v0.1.0` is absent. Trusted-main workflow policy permits a fresh draft only when no same-version release or tag exists; unchanged same-version pushes with an existing release skip, while changed-version collisions fail closed. Active release assets are nRF54L15-only.

### Open questions

None.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Root `VERSION` remains `0.1.0`, PB-031 is human-merged, and required trusted-main hosted gates are green before candidate creation.
- [x] #2 Candidate-creation evidence verifies no existing GitHub release or `refs/tags/v0.1.0` collision; a changed-version collision remains fail-closed.
- [x] #3 Trusted-main creates a fresh nRF54L15 factory ZIP, top-level `SHA256SUMS`, and provenance bound to its exact post-PB-031 commit.
- [x] #4 Draft metadata verifies the exact trusted-main target and nRF54L15 asset scope, while the draft remains private, unpublished, and untagged.
- [x] #5 PB-006 neither restores nor clobbers historical failed assets and performs no manual upload, manual tag, publication, RH4, or FR4 action.
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Trusted-main candidate creation completed after PR #13 human-merged PB-031 into `main` at exact SHA `b59e1d8f99b8f4e7435c7086bfe81700007b221d`.

Hosted workflow run `35429538264` (attempt `1`) completed success at that exact SHA. Jobs `test-unit`, `test-heavy (coverage)`, `test-heavy (bsim)`, aggregate `tests`, `firmware`, and `release` all passed.

Active replacement candidate is GitHub draft release ID `391991202`, tag label `v0.1.0`, title `LE Audio Receiver v0.1.0`, target commit `b59e1d8f99b8f4e7435c7086bfe81700007b221d`, draft `true`, prerelease `false`, and `published_at: null`. It remains private, unpublished, and untagged; no `refs/tags/v0.1.0` exists.

Assets:
- `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip`, 627096 bytes, SHA-256 `bd5fe73636b831e9b685cd20f53704fbe342be60b124f5ae5379dd7969ac9f10`.
- `SHA256SUMS`, 117 bytes, SHA-256 `ea653102fc318d22e0b4b5d3c7b08a05874aa435b73098ea5f65790a913398ff`.
- `release-provenance.json`, 1077 bytes, SHA-256 `49ca660cc83e99a12e07982f466d5d44f64730e708f162a03582dc60fe157238`.

Provenance binds version `0.1.0`, NCS `v3.3.0`, run `35429538264` attempt `1`, exact SHA `b59e1d8f99b8f4e7435c7086bfe81700007b221d`, and the nRF54L15-only factory ZIP.

Scope restraint: historical failed draft assets were not restored or clobbered. No manual upload, manual tag, publication, RH4, or FR4 action occurred. PB-007 owns exact-artifact RH4 and FR4 acceptance.
<!-- SECTION:NOTES:END -->

## Comments

<!-- COMMENTS:BEGIN -->
created: 2026-09-18 22:15
---
2026-09-19 refinement: Product owner selected unreleased `0.1.0` for a fresh trusted-main nRF54L15 candidate. Failed v0.1.0 draft served its stable-harness purpose and was deleted with its assets; historical FR4 evidence remains. PB-006 now waits for PB-031 human merge and green hosted gates.
---
<!-- COMMENTS:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Outcome: PB-006 completed. Trusted-main run `35429538264` (attempt `1`) created active replacement draft release `391991202` from exact target SHA `b59e1d8f99b8f4e7435c7086bfe81700007b221d` after PB-031 PR #13 human merge.

Hosted gates: `test-unit`, `test-heavy (coverage)`, `test-heavy (bsim)`, aggregate `tests`, `firmware`, and `release` all passed.

Candidate state: tag label `v0.1.0`; title `LE Audio Receiver v0.1.0`; draft `true`; prerelease `false`; `published_at: null`; private, unpublished, and untagged. No `refs/tags/v0.1.0` exists.

Assets:
- `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip`, 627096 bytes, SHA-256 `bd5fe73636b831e9b685cd20f53704fbe342be60b124f5ae5379dd7969ac9f10`.
- `SHA256SUMS`, 117 bytes, SHA-256 `ea653102fc318d22e0b4b5d3c7b08a05874aa435b73098ea5f65790a913398ff`.
- `release-provenance.json`, 1077 bytes, SHA-256 `49ca660cc83e99a12e07982f466d5d44f64730e708f162a03582dc60fe157238`.

Provenance binds version `0.1.0`, NCS `v3.3.0`, run `35429538264` attempt `1`, exact SHA `b59e1d8f99b8f4e7435c7086bfe81700007b221d`, and the nRF54L15-only factory ZIP.

Scope restraint: historical failed assets were not restored or clobbered. No manual upload, manual tag, publication, RH4, or FR4 action occurred. PB-007 owns exact-artifact acceptance.
<!-- SECTION:FINAL_SUMMARY:END -->
