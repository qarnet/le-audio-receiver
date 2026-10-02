# PB-039: combined migration PR wrap-up, 2026-10-02

## Scope and product lifecycle

The owner requested one pull request for the completed all-nRF54L15 migration
and Done transitions for completed backlog items. The combined PR includes the
24 prior implementation/evidence commits from `origin/main` through `85b944e`
and this documentation/backlog closure. No new production code is introduced
by PR preparation. Tested firmware checkpoint remains exact `104e67a`.

Backlog.md transitions these nine evidence-backed items to Done and moves them
to `docs/product/backlog/completed/` in the same PR as the implementation:

| Item | Completed scope |
| --- | --- |
| PB-019 | Stock-bridge XIAO Linux HCI fixture and normal CLI qualification |
| PB-033 | Explicit paired session identity and historical additive XIAO source proof |
| PB-034 | Canonical nRF54L15BSim Stage 1 |
| PB-035 | XIAO source and immutable session propagation across active HIL matrices |
| PB-036 | Single-image source artifacts and capture infrastructure contracts |
| PB-037 | E83/nRF5340 production receiver retirement |
| PB-038 | Aggregate migration audit and clean integration verification |
| PB-039 | Approved all-nRF54L15 migration |
| PB-040 | Exact NCS v3.4.1 software/toolchain migration |

Acceptance text, product descriptions, priorities and types are unchanged.
PB-033's completed historical proof satisfied legacy retention before PB-039
explicitly superseded that policy. PB-040's software-only evidence does not
borrow later hardware qualification as an upgrade acceptance claim.

Done is PR-gated, not official human acceptance before merge. The PR is not
merged by an agent. PB-018 remains explicitly superseded, not falsely Done.
PB-013, PB-041, analog qualification and active-draft FR4/publication are not
completed by this work. User-owned PB-013 bytes and private graph data remain
outside the commit and PR.

## Recorded firmware and hardware verification

[Final local migration verification](nrf54l15-migration-verification-results-20261001.md)
records clean `104e67a`: canonical 80/0/80; strict BSim 17 scenarios/26 runs;
three physical build shapes and resolved contract 73/73; six normal-image HCI
cases (72,000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts); and
the frozen exact-local-artifact matrix 20/20 with 120 ordered identity checks
and independently rehashed aggregate/child payloads.

These are previously verified results, not new PR-preparation hardware runs.
The private raw `/tmp/opencode` run roots referenced in the prior reports are
**absent in this session**. Committed reports retain recorded outcomes, exact
image/archive hashes, raw identity values and failure history; this PR does
not include or claim a currently available downloadable raw lab evidence
bundle. No independent rehash of unavailable raw runs is claimed now. A
reviewer cannot reopen those raw captures from this checkout alone.

Frozen physical transport limits remain at least 90% valid SDUs per stream,
at most 5% PLC among decoded frames, and zero required error counters. Passing
does not mean zero packet loss or universal RF reliability. Analog output,
long-duration/range/interference qualification and active draft-release
acceptance remain separate.

## Fresh PR-preparation verification

Fresh host checks ran after the Done transitions and link updates:

```bash
env -u ZEPHYR_BASE nix develop -c python3 -m pytest -q tests/hil
env -u ZEPHYR_BASE nix develop -c python3 -m unittest \
  scripts.test_package_firmware_release \
  scripts.test_package_hil_source_artifact \
  scripts.test_firmware_build_ci \
  tests.unit.build_contract.test_build_contract \
  tests.unit.bsim_target.test_nrf54l15bsim_contract \
  tests.unit.hil_source_target.test_hil_source_target \
  tests.unit.hci_uarte.test_hci_uarte \
  tests.unit.hci_h4.test_hci_h4
env -u ZEPHYR_BASE nix develop -c backlog doctor
git diff --check
```

HIL host outcome: **340 passed / one intentional hardware-opt-in skip**.
Packaging, CI/build/target contracts and real UART/H4 boundaries: **127 tests,
OK**. Backlog doctor reports no duplicate IDs, self-dependencies or dependency
cycles. Documentation closure checks verify completed-item paths, local links,
unchanged product descriptions/criteria and PB-013 byte/staging preservation.

Fresh raw host logs are external and were created exclusively during this
session: `/tmp/opencode/migration-pr-hil-host-20261002.log` (SHA-256
`38d77e3a2e8d1ca88c6d2705a31707b731479e2bbb5371c1a95311305d6a34c6`)
and `/tmp/opencode/migration-pr-boundaries-20261002.log` (SHA-256
`302760bc28299b8f9efc66a67477be1e49b75d6f209940b6c318917279363a11`).
Each has an adjacent command/UTC/exit/hash JSON record. Ordinary Nix
dirty-worktree provenance is retained. Concurrent Nix evaluation briefly
reported a busy SQLite cache in the first doctor log; a sequential doctor
rerun avoids that host-cache contention. No firmware warning is waived.

Read-only review covered all 24 prior commits and the combined implementation
diff against current `origin/main`; it found no blocking defect or added
credential/raw-lab payload. Full canonical/hardware results above are not
relabeled as fresh runs; hosted CI is pending the new pull request. This closure
changes documentation/backlog only and preserves exact tested firmware source.

## Subsequent hosted failure and repair

The first hosted unit gate failed in run `37041093569`: optional BabbleSim
source dependencies were provisioned only in the BSim worker, although unit
compiler-policy tests also require them. Coverage passed; the skipped firmware
job was not a firmware-build failure. PB-040 was reopened for correction.
[Source-provisioning repair](nrf54l15-ci-source-provisioning-results-20261002.md)
records shared cache-independent preparation, real offline west/Git regression
tests, unchanged compiler/hash policy and a local 77/0/77 unit gate. Hosted
acceptance of the repair remains pending its own run; prior reports are not
rewritten into hosted success.
