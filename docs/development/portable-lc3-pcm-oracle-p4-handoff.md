# PB-031 P4 full acceptance and closeout handoff

## Goal

Run full PB-031 software and firmware acceptance on exact clean P3 result,
retain raw external logs, classify every emitted diagnostic under repository
warning policy, record evidence, and move PB-031 from In Progress to Review.

## Current resume point after matrix failure

Preparation checkpoint is commit
`56472e2ca5a0d0434ee031bc437ee7bdd482407e` (`docs: prepare PB-031 full
acceptance`). Worktree was clean after failed gate. Second P4 attempt evidence
is retained at `/tmp/opencode/pb031-p4.lnNZ6l`; it ended `73 PASS / 1 FAIL /
74 TOTAL`. Coverage passed unchanged population 37 at 4969/5427 numeric lines,
2177/2984 numeric branches, and 380/380 numeric functions. BSim passed 17
scenarios/26 runs. Matrix alone failed with:

```text
error: invented witness: src/audio_decode.c: 'test_golden_mono_10ms'
```

Root cause is exact and local: P3 renamed the successful `audio_decode_sdu`
fixture test from `test_golden_mono_10ms` to `test_fixture_mono_10ms` but left
the sole old name in `tests/test-matrix.json`. New test still calls production
`audio_decode_sdu`, requires return 0, and applies portable per-channel PCM
checks, so it is correct replacement witness for same public outcome.

This repair block supersedes section 1 for resumed execution:

1. Require HEAD `56472e2ca5a0d0434ee031bc437ee7bdd482407e` and only this handoff modified.
2. In `tests/test-matrix.json`, change only source `src/audio_decode.c`, API
   `audio_decode_sdu`, outcome `0` witness from `test_golden_mono_10ms` to
   `test_fixture_mono_10ms`.
3. Verify repository has no remaining `test_golden_mono_10ms`, run
   `python3 tests/unit/test_matrix/test_check_test_matrix.py`, run
   `python3 scripts/check-test-matrix.py`, then run `git diff --check`.
4. Inspect full status/diff/log. Stage exactly `tests/test-matrix.json` and this
   handoff. Commit `test: repair decoder matrix witness`. Do not amend.
5. Require clean status, create a new unique evidence root, then resume at
   section 2. Rerun full gate and all firmware/build-contract checks; do not
   reuse passing results from either superseded attempt.

P3 implementation is commit
`4957a1f37e306c44ff777b6801c73154981b4d69` (`test: migrate decoder
fixtures to portable PCM oracle`) with documentation repair
`d14a1b9a7430918d37891bc973ff93d8f3306aeb` (`docs: fix portable decoder
contract ID`). P3 review reran fixture generation and the decode suite: hashes
unchanged and 43/43 tests passed. Worktree was clean.

## In scope

- Full canonical software gate
- Pristine nRF5340 and nRF54L15 receiver builds
- Resolved two-target build-contract checker
- Raw external logs and diagnostic review
- One stale `tests/test-matrix.json` witness-name repair specified above
- `docs/development/portable-lc3-pcm-oracle-plan.md`
- `docs/development/portable-lc3-pcm-oracle-p4-handoff.md`
- `STATUS.md`
- `AGENTS.md`
- Current inventory comments in `scripts/test-all.sh`,
  `docs/development/firmware-ci-test-gate-plan.md`, and
  `docs/development/firmware-release-plan.md`
- PB-031 acceptance checkbox state, Implementation Notes, Final Summary, and
  status, changed through `backlog` CLI

## Out of scope

- Production, test, fixture, manifest, threshold, build-system, workflow, or
  toolchain changes
- Coverage-baseline or test-count changes
- Hardware flashing, serial use, RF testing, release work, push, PR creation,
  or merge
- Marking PB-031 Done or moving it to `completed/`; without a PB-031 PR and
  human merge, correct terminal state for this phase is Review
- Fixing unrelated Nix, SDK, or repository diagnostics unless acceptance
  evidence proves a new PB-031 regression

If any gate fails because of a real P0-P3 defect, stop closeout, preserve logs
and worktree, and escalate. Do not weaken tests, update baselines, widen
thresholds, or normalize warnings.

## Superseded first P4 attempt and resolved blockers

First attempt at exact HEAD `d14a1b9a7430918d37891bc973ff93d8f3306aeb`
is retained at `/tmp/opencode/pb031-p4.BTF2Vc`. It is not acceptance evidence:
canonical gate ended `72 PASS / 2 FAIL / 74 TOTAL` because this untracked
handoff made worktree dirty, so coverage correctly refused baseline enforcement
and matrix had no `coverage.json`. All 71 unit children and BSim passed. Both
firmware builds and build contract 96/96 also passed.

Three blockers are resolved by repository evidence:

1. Full gate has 74 children, not 72: current deterministic inventory is 41
   Twister + 5 exec-only + 25 Python = 71 unit children, plus coverage, matrix,
   and BSim. PB-031 added `pcm_oracle` Twister and `lc3_pcm_calibrate` Python
   children after prior 72-child baseline. Test expectations and current
   inventory comments must say 74; historical hosted 72-child results remain
   unchanged.
2. Coverage requires a clean exact commit. Commit this corrected handoff and
   current inventory/warning documentation as one preparation checkpoint before
   rerunning any P4 gate. P4 acceptance then tests that clean checkpoint, not
   `d14a1b9` directly.
3. nRF5340 warning `CONFIG_BT_CTLR_ADVANCED_FEATURES=y, Advanced Features'
   default value change could change Zephyr Bluetooth Controller's functional
   behavior` is an upstream NCS v3.3.0 CMake diagnostic. The imported required
   peripheral-ISO SW Split overlay enables `BT_CTLR_ADVANCED_FEATURES` to expose
   subordinate reservation controls and resolves
   `BT_CTLR_EVENT_OVERHEAD_RESERVE_MAX=y`; Zephyr emits this warning whenever
   this menu symbol is enabled. Removing it would hide required controls and
   change controller reservation behavior. Add this exact classification to
   authoritative `STATUS.md` nRF5340 warning table before rerun. It is not a
   compiler or Kconfig assigned-value warning.

## Grounding and expected results

- `scripts/test-all.sh` owns canonical local gate. Current inventory is 74
  children: 41 Twister, 5 exec-only, 25 Python, coverage, matrix, and BSim.
- Committed coverage population is 37. P3 added no suite and shared comparator
  was already in coverage population, so no count or baseline change is
  expected.
- Full BSim Stage 1 is 17 scenarios and 26 runs. Current acceptance uses exact
  TX fixture/sequence hashes and payload/recipe-aware portable PCM metrics.
- Expected build contract is 96/96.
- `fw-build-5340` and `fw-build-54l15` are repo helpers and both build
  pristinely into `build/nrf5340` and `build/nrf54l15`.
- Host `nix develop -c true` was verified successful immediately before this
  handoff. Use exact accepted P4 commands through `nix develop -c`.
- `STATUS.md` section `Build warning diagnostics (2026-07-30)` is authoritative
  for known non-actionable SDK/Kconfig/CMake diagnostics. Both builds must have
  zero compiler warnings, zero new Kconfig assigned-value warnings, and zero
  new/actionable diagnostics. Existing documented diagnostics do not become
  compiler warnings and must be named rather than hidden.
- Repository-local `AGENTS.md` requires every warning to be fixed or classified
  with recorded reason. Do not summarize logs before inspecting all warning,
  error, fatal, failed, assigned-value, experimental, deprecated, and
  no-sources lines in context.

## Execution

### 1. Prove exact starting state and commit preparation checkpoint

From repository root:

```bash
git status --short
git rev-parse HEAD
git log --oneline -5
nix develop -c true
```

Require HEAD `d14a1b9a7430918d37891bc973ff93d8f3306aeb` and exactly one status
entry, the untracked
`docs/development/portable-lc3-pcm-oracle-p4-handoff.md` created by the
orchestrator. Any other entry is a blocker; stop and report.

Before running acceptance, make only these decided preparation edits:

1. `scripts/test-all.sh` current inventory comment: 41 Twister + 5 exec-only +
   25 Python = 71 unit children; canonical total 74.
2. `docs/development/firmware-ci-test-gate-plan.md` grounded current inventory:
   same 41/5/25 and 74. Preserve historical 72-child acceptance records.
3. `docs/development/firmware-release-plan.md` current canonical gate: 74
   children, 71 unit suites plus coverage, matrix, and BSim. Preserve historical
   PR run counts.
4. `STATUS.md` nRF5340 warning table: add exact
   `BT_CTLR_ADVANCED_FEATURES` classification from resolved blocker 3 above.
   Do not yet claim P4 acceptance or change top current gate count.
5. Keep this corrected handoff as written.

Run `git diff --check`, inspect status/full diff/log, stage exactly those five
files, and commit preparation checkpoint:

```text
docs: prepare PB-031 full acceptance
```

Do not amend. Record checkpoint hash. Require clean status before gate.

Create one unique external evidence root without deleting or overwriting prior
evidence:

```bash
EVIDENCE_ROOT="$(mktemp -d /tmp/opencode/pb031-p4.XXXXXX)"
printf '%s\n' "$EVIDENCE_ROOT"
```

Retain exact path in closeout docs and recap.

### 2. Run acceptance commands sequentially with real exit status

Use `set -o pipefail` for every tee pipeline:

```bash
set -o pipefail
nix develop -c ./scripts/test-all.sh 2>&1 | tee "$EVIDENCE_ROOT/test-all.log"

set -o pipefail
nix develop -c fw-build-5340 2>&1 | tee "$EVIDENCE_ROOT/fw-build-5340.log"

set -o pipefail
nix develop -c fw-build-54l15 2>&1 | tee "$EVIDENCE_ROOT/fw-build-54l15.log"

set -o pipefail
nix develop -c python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15 \
  2>&1 | tee "$EVIDENCE_ROOT/build-contract.log"

backlog doctor 2>&1 | tee "$EVIDENCE_ROOT/backlog-doctor.log"
git diff --check 2>&1 | tee "$EVIDENCE_ROOT/git-diff-check.log"
git status --short 2>&1 | tee "$EVIDENCE_ROOT/git-status-before-closeout.log"
sha256sum "$EVIDENCE_ROOT"/*.log | tee "$EVIDENCE_ROOT/SHA256SUMS"
```

Do not continue to closeout edits unless every command exits 0 and status stays
clean.

### 3. Review raw diagnostics

Inspect all raw logs. Record:

- canonical summary, expected 74 PASS / 0 FAIL / 74 TOTAL;
- coverage population and line/branch/function figures;
- BSim 17-scenario/26-run strict result and maximum observed
  max-error/RMS/minimum-correlation metrics if printed;
- both build success markers;
- build-contract assertion total, expected 96/96;
- every compiler warning, Kconfig assigned-value warning, `No SOURCES given`,
  deprecation, experimental-symbol, warning, error, fatal, and failed line;
- classification of each build diagnostic against `STATUS.md` documented list;
- confirmation that fixture/manifest files, production liblc3 flags, and
  production decoder code did not change during P4.

Test names and expected negative-test text containing words such as `error` or
`failed` are not automatically diagnostics. Inspect context. Conversely, do
not discard real diagnostics because overall command passed.

Stop and escalate before edits if:

- any child, build, or contract assertion fails;
- coverage baseline or population changes;
- any compiler warning or new assigned-value warning appears;
- any emitted build diagnostic is absent from documented classification;
- BSim count, exact TX hash, numerical PCM, lifecycle, routing, PLC,
  decoder-error, or teardown acceptance fails;
- worktree gains generated or source changes.

### 4. Record technical acceptance

Only after all evidence passes:

1. Update `docs/development/portable-lc3-pcm-oracle-plan.md` status to P0
   through P4 accepted, implementation complete. Add P4 section evidence with
   exact tested commit, evidence-root path, gate count, coverage figures, BSim
   result, both builds, build-contract total, warning classification, and clean
   diff/status result.
2. Update `STATUS.md` current date to 2026-09-18. In current-state summary,
   replace ambiguous byte-identical BSim PCM-pin wording with exact TX
   fixture/sequence hash plus portable PCM policy wording. Add concise PB-031
   acceptance section near top with exact commit and P4 evidence. State no
   production behavior, liblc3 revision/flags, fixture bytes, manifest limits,
   coverage baseline, or firmware feature changed.
3. Update `AGENTS.md` current-state gate to 74 PASS / 0 FAIL / 74 TOTAL with
   41 Twister, 5 exec-only, and 25 Python plus coverage, matrix, and BSim.
   Replace current ambiguous `BSim Stage 1 pins byte-identical` wording with
   exact TX fixture/sequence hashes plus portable payload/recipe-aware PCM
   metrics. Preserve historical hosted 72-child results later in that section.
4. Use `backlog task edit`, never direct frontmatter/status editing, to:
   - check acceptance criteria 1 through 6;
   - append P4 exact evidence to Implementation Notes;
   - set Final Summary with outcome, key decisions, exact validation commands
     and results, evidence path, no-production-change statement, and remaining
     PR/human-merge lifecycle note;
   - move status from In Progress to Review.

Example command shape, with actual measured values substituted and real
newlines in Markdown fields:

```bash
backlog task edit PB-031 \
  --check-ac 1 --check-ac 2 --check-ac 3 \
  --check-ac 4 --check-ac 5 --check-ac 6 \
  --append-notes "$P4_NOTES" \
  --final-summary "$FINAL_SUMMARY" \
  --status Review --plain
```

Do not alter acceptance-criteria text, title, priority, type, labels,
dependencies, or Description. Do not run `backlog task complete`.

After edits, run:

```bash
backlog task view PB-031 --plain
backlog doctor
git diff --check
git status --short
git diff -- docs/development/portable-lc3-pcm-oracle-plan.md \
  STATUS.md AGENTS.md \
  "docs/product/backlog/tasks/pb-031 - Make-LC3-PCM-test-oracle-platform-independent.md"
```

Verify status Review, all six criteria checked, Final Summary present, only
intended closeout docs changed, and no user-facing em dash issue because no
public docs are touched.

## Commit and return

Inspect full status, diff, and recent log. Stage only four P4 closeout files:

- `docs/development/portable-lc3-pcm-oracle-plan.md`
- `STATUS.md`
- `AGENTS.md`
- PB-031 task file

Commit:

```text
docs: record PB-031 full acceptance
```

Do not push, amend, open or merge a PR, mark task Done, force Git actions, or
add AI/tool attribution.

Return exact tested HEAD, evidence root, files changed, gate/build/contract
results, warning classification, acceptance/status outcome, commit hash/message,
clean post-commit status, blockers, deviations, and suggested next step.

Escalate without committing incomplete closeout if two materially different
attempts fail, evidence conflicts with P0-P3 design, a warning cannot be
classified, or any repair would require source changes, baseline changes,
threshold widening, architecture invention, or scope expansion.
