# PB-045: testing framework expansion execution loop

## Owner direction and scope

The owner requested a new branch and pull request before implementation, then
an item-by-item loop of specific research/refinement, implementation, local
verification, push and hosted CI validation. Genuine hard blockers are recorded
and skipped, not converted into weakened tests. The loop is implemented directly;
specialists may perform read-only research/review, not own implementation.

Base: human-merged PR #15, `origin/main` at
`0c9d2391f8532684e5014225b82749f0d930ce6e`.
Branch: `feature/independent-firmware-validation`.

Requested framework scope is PB-042 through PB-050 plus survey-derived PB-051
(encoded ASCS lifecycle/rejection integration), PB-052 (strict external result
execution accounting) and PB-053 (isolated Linux/BlueZ host regression lane).
PB-041 is a prerequisite where fresh physical harness identity is necessary.
Existing analog fixture/matrix work, release acceptance/publication and unrelated
product features are not silently absorbed into this framework track.

### Continuation direction (2026-10-03)

The owner requested continuous execution through the entire selected scope, not
a report or permission round-trip after each item or push. Individual commits,
pushes and hosted CI checks are internal verification steps. Continue to the
next eligible item after the current item's verification passes. Report the
combined completion or a genuine consequential blocker; finish independent work
before treating a blocked dependency chain as a reason to stop the whole track.

Compaction/restart must recover scope from the explicit PB-042 through PB-053
selection above and PB-041 where required, not from a transient todo list or an
unqualified query for all repository In Progress items. The backlog remains the
sole owner of current status and dependencies. Preserve Ready-before-start and
record actual missing prerequisites rather than marking unready dependent items
In Progress merely as a memory aid. No unrelated backlog item is selected by
this continuation direction.

Research records:

- [Independent validation boundaries and LC3-only reference architecture](independent-firmware-validation-research-20261003.md).
- [Pinned Bluetooth repository testing survey](bluetooth-testing-repository-research-20261003.md).

## Invariants

- LC3plus remains excluded. Codec/reference/corpus rights and Win32 execution
  choices must be resolved by evidence/owner authority, not assumed.
- Begin only Ready items after grounded refinement. Existing criteria are not
  rewritten to fit easier work. Underspecification is not a technical blocker.
- Preserve frozen PCM/HIL acceptance, baseline ratios and 17/26 canonical BSim;
  new lanes are additive. No source-library self-comparison called independent.
- Tests prove public behavior and encoded traffic, including failure and lifecycle
  controls, not private-field shape or mocked helper-call counts.
- Actual offload/board claims require fresh identity and hardware evidence.
  Source/model/host simulation is not physical RF, analog or presentation proof.
- PB-013 user bytes remain unstaged and untouched. Private graph/checkpoints and
  raw lab evidence remain untracked/external. No SDK-source modifications.
- Compiler/Kconfig/runtime warnings are diagnosed, never quietly normalized.
- Each completed item ships with evidence and PR-gated Done metadata in this
  existing ID-prefixed PR. Human merge alone is official acceptance. No agent merge.

## Loop and verification contract

1. Inspect item, exact source, public APIs, dependencies and relevant normative
   references. Resolve ordinary technical choices autonomously.
2. Refine open technical details and record the implementation/public-boundary
   test contract; transition through Ready and In Progress using Backlog.md.
3. Implement smallest meaningful additive boundary and negative controls. Repair
   uncovered defects without deleting scenarios, lowering limits or inventing
   independent expected data from the DUT.
4. Run focused tests, inventory/documentation checks and applicable local gates.
   Update checked criteria and Final Summary only from actual evidence.
5. Commit intended files, push to the already open PR, observe hosted unit,
   coverage/matrix, BSim and firmware contexts. Resolve CI defects before next
   accepted item, retaining failed evidence.
6. When evidence establishes an unavailable capability, access/right or necessary
   owner-level decision, record that exact blocker and dependent impact, skip it
   and continue independent items. Routine engineering failure is not a blocker.

An implementation-complete but unverified item remains open. Final report will
name completed items, exact CI evidence and skipped blockers; it will not claim
all research/licensing/physical lanes complete from a few native tests.
