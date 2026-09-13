# Firmware CI logical parallelization acceptance handoff

## Goal

Record the completed local and GitHub-hosted acceptance of the logical CI
parallelization already committed at source HEAD `463fa6b`. Replace the pending
language in the plan and update the current-status summaries without changing
workflow behavior, test code, release state, or hardware claims.

## Grounding evidence

GitHub Actions run `34725825883` is the PR 12 acceptance run for source HEAD
`463fa6b57042e026985ffd0a25d1ba0687f651b7`. The workflow checkout/artifact SHA
is the pull-request merge SHA
`11c4c6fda43e93d7214f4d463b25b50874342d02`.

Exact run timing is `2026-09-12T23:35:44Z` through
`2026-09-13T00:02:15Z`, or 26m31s. The prior monolithic comparison run
`34719725244` took 58m02s. The observed reduction is 31m31s, approximately
54 percent.

Accepted jobs and evidence:

- `test-unit`, job `103639640307`: SUCCESS, 21m34s, exact phase summary
  `69 PASS / 0 FAIL / 69 TOTAL`.
- `test-heavy (coverage)`, job `103639640227`: SUCCESS, 21m40s, exact phase
  summary `2 PASS / 0 FAIL / 2 TOTAL`; 45 traces merged; numeric population 37
  with 4962/5420 lines, 2153/2960 branches, and 380/380 functions; baseline
  enforcement PASS; matrix checker 0 errors and 0 notes.
- `test-heavy (bsim)`, job `103639640334`: SUCCESS, 16m59s, exact phase summary
  `1 PASS / 0 FAIL / 1 TOTAL`; all 17 scenarios and 26 runs strict-checked with
  unchanged pinned hashes.
- The three phase summaries combine to the unchanged canonical inventory:
  `72 PASS / 0 FAIL / 72 TOTAL`.
- Aggregate required context `tests`, job `103642036590`: SUCCESS, 9s, and ran
  after all logical workers completed.
- Required context `firmware`, job `103642024613`: SUCCESS, 4m53s; started after
  `test-unit` completed and retained build contract, version, package,
  verification, and upload steps.
- `release`, job `103642582348`: SKIPPED on pull_request as required.
- PR 12 was CLEAN at `463fa6b`; active ruleset `20658259` still requires exact
  contexts `tests` and `firmware` with no strict latest-main requirement.

Accepted artifacts:

- unit ID `10307838803`, digest
  `sha256:14b08bd07c4bab18cd9272777c23f338186e15d76dddd60defa1963f3df3e05f`;
- coverage ID `10307798767`, digest
  `sha256:e676b744c9638e10893255c9ad5be3821a5e18c0116e33f2696e903dd7164875`;
- BSim ID `10307039786`, digest
  `sha256:0e5a51d01c1fb74fdbfe709b5bbed7175245c2096acdcaade6827dcc1de2ab41`;
- firmware ID `10308350180`, digest
  `sha256:7c3b313916dc32b2813f0f1825be42ed9c461ab3b8808279a27b8b5da544c6bf`.

The test artifacts retain seven days. The firmware artifact retains 14 days.
Names contain merge SHA `11c4c6fd...`, not source HEAD `463fa6b...`.

Local implementation acceptance already completed before the hosted run:

- inventory count 69 unit children;
- workflow contract 35 tests passed;
- coverage-runner boundary suite 40 tests passed;
- BSim parser suite `61 PASS / 0 FAIL`;
- full local gate `72 PASS / 0 FAIL / 72 TOTAL` with the same population 37,
  unchanged baseline, and unchanged BSim pins;
- `git diff --check` passed.

## Files and exact edits

### `docs/development/firmware-ci-test-gate-plan.md`

Change the parallelization amendment status from pending hosted acceptance to
ACCEPTED. Preserve the current DAG and design rationale. Replace the final
pending paragraph with a concise hosted acceptance record containing the run,
source HEAD versus merge SHA distinction, job IDs/results, combined 72-child
result, coverage numbers, BSim result, firmware/release ordering, exact total
timing, prior timing comparison, artifact IDs/digests, and unchanged branch
contexts.

### `STATUS.md`

Update the document date to 2026-09-13. Add a new current section immediately
before the historical PR 11 canonical-gate section, titled for PR 12 logical
parallelization acceptance. Include the accepted DAG, local result, hosted run
and job evidence, 26m31s versus 58m02s comparison, artifact provenance, and
unchanged release/FR4 boundaries.

In the top current-state block, update only the current canonical software-gate
facts that the accepted change supersedes: 72 children = 40 Twister + 5
exec-only + 24 Python + coverage + matrix + BSim, and numeric coverage
population 37 = 4962/5420 lines, 2153/2960 branches, 380/380 functions. Add a
concise PR 12 acceptance sentence. Keep old 65-child and PR 11 evidence clearly
historical rather than deleting it. Do not alter HIL acceptance, FR4 blocked
state, release version, or hardware claims.

### `AGENTS.md`

Update the compact current-status paragraph to the same current 72-child and
population-37 facts. Describe PR 12's accepted parallel DAG and hosted run,
while retaining PR 11's monolithic 65-child acceptance as historical setup
evidence where useful. State exact required contexts remain `tests` and
`firmware`; release remains joined on both and skipped on pull requests. Keep
all product, HIL, FR4, target, SDK, hardware, and warning policies unchanged.

### This handoff

Keep this handoff in the documentation commit as the provenance for the exact
acceptance update.

## Scope boundaries

In scope: the four documentation files above only.

Out of scope: workflow YAML, scripts, tests, source, coverage baseline, BSim
oracles, Kconfig/devicetree, `VERSION`, tags, releases, hardware/HIL, branch
rules, PR merge, nRF5340 cleanup, and unrelated historical-result rewrites.

Do not claim a protected-main run, draft creation, exact-artifact hardware
acceptance, publication, or FR4 completion. PR 12 is still unmerged.

## Verification and commit

Run:

```bash
git diff --check
git diff --name-only
git status --short
```

The changed-file list must contain only:

```text
AGENTS.md
STATUS.md
docs/development/firmware-ci-parallelization-acceptance-handoff.md
docs/development/firmware-ci-test-gate-plan.md
```

Documentation-only evidence update does not require rerunning the 72-child
gate. Inspect `git diff`, `git status`, and `git log --oneline -10`, stage only
the four files, and commit with:

```text
docs(ci): record parallel gate acceptance
```

Do not push, merge, amend, tag, publish, or operate hardware. Return changed
files, verification results, commit hash/message, final status, and any
deviation or blocker. Stop and escalate rather than changing scope or
inventing evidence.
