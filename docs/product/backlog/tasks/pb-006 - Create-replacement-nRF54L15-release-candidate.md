---
id: PB-006
title: Create replacement nRF54L15 release candidate
status: Ready
assignee: []
created_date: '2026-09-13 01:24'
updated_date: '2026-09-18 22:15'
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
- [ ] #1 Root `VERSION` remains `0.1.0`, PB-031 is human-merged, and required trusted-main hosted gates are green before candidate creation.
- [ ] #2 Candidate-creation evidence verifies no existing GitHub release or `refs/tags/v0.1.0` collision; a changed-version collision remains fail-closed.
- [ ] #3 Trusted-main creates a fresh nRF54L15 factory ZIP, top-level `SHA256SUMS`, and provenance bound to its exact post-PB-031 commit.
- [ ] #4 Draft metadata verifies the exact trusted-main target and nRF54L15 asset scope, while the draft remains private, unpublished, and untagged.
- [ ] #5 PB-006 neither restores nor clobbers historical failed assets and performs no manual upload, manual tag, publication, RH4, or FR4 action.
<!-- AC:END -->

## Comments

<!-- COMMENTS:BEGIN -->
created: 2026-09-18 22:15
---
2026-09-19 refinement: Product owner selected unreleased `0.1.0` for a fresh trusted-main nRF54L15 candidate. Failed v0.1.0 draft served its stable-harness purpose and was deleted with its assets; historical FR4 evidence remains. PB-006 now waits for PB-031 human merge and green hosted gates.
---
<!-- COMMENTS:END -->
