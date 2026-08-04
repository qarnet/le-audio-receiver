# Refactor R0 handoff — canonical truth and gate inventory

## Goal

Make active repository documentation, test metadata, and coverage-tool
enforcement describe accepted T8 state before structural refactoring begins.
Start from clean commit `f0cf1d0`; accepted plan is
`docs/development/refactor-plan.md`.

R0 is truth reconciliation only.  No production firmware source, public
firmware behavior, coverage numeric baseline, BSim expected value, or hardware
configuration changes.

## Accepted anchors

- Exact production code: `971e6a4`.
- Coverage baseline commit: `1a5842d`, generated on clean `971e6a4`.
- Numeric population: 26 files; lines 3281/3722, branches 1433/2041,
  functions 205/205.
- Canonical gate: 47 children = 28 Twister + 4 exec-only + 12 Python +
  coverage + matrix + BSim.
- Build contract: 76/76.
- T8 hardware evidence: `docs/testing/pre-refactor-hardware-baseline.md`.
- Current tools recorded in `tests/coverage-baseline.json`: `gcovr 8.4` and
  `gcov (GCC) 14.3.0`.

## Scope and exact files

### Active documentation

- `AGENTS.md`
- `README.md`
- `STATUS.md`
- `docs/design.md`
- `docs/development/bluez-wireplumber-interoperability-plan.md`
- `docs/development/pre-refactor-testing-plan.md`
- `docs/development/refactor-plan.md` (R0 status/result link after acceptance)
- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- `docs/testing/pre-refactor-hardware-baseline.md` (undefined `T9` wording
  only; preserve all measurements)

### Gate and metadata

- `scripts/test-all.sh`
- `scripts/test-coverage.sh`
- `tests/unit/test_coverage_runner/test_test_coverage_runner.py`
- `tests/test-matrix.json`

### Phase records

- This handoff.
- New `docs/development/refactor-r0-results.md` after clean verification.

## Required documentation changes

### Plan ownership and statuses

1. `AGENTS.md` must name `docs/development/refactor-plan.md` as accepted plan
   of record for R0–R10.  Keep `docs/design.md` as historical architecture and
   evidence, not active structural plan.
2. Mark T0–T8 COMPLETE/ACCEPTED in active summaries.  Replace stale “Phase 5
   landed,” “Phase 6 complete,” and 432-test text when presented as current.
3. `docs/development/pre-refactor-testing-plan.md` gets status-only closeout:
   T0–T8 COMPLETE, superseded by refactor plan.  Preserve historical body and
   dated 21/41-child evidence.
4. On successful R0 verification, mark R0 ACCEPTED in refactor plan and link
   R0 results.  Do not pre-mark acceptance.
5. Correct refactor plan G1 coverage command to include required arguments:
   `./scripts/test-coverage.sh --output /tmp/r0-coverage --clean-output`.

### Phase naming

Rename only desktop BlueZ/WirePlumber track in active prose:

- Phase 1 → BZ1
- Phase 2 → BZ2
- Phase 3 → BZ3
- Phase 4 compatibility expansion → BZ4

Keep audio architecture Phases 0–6 and Phase 4a/4b/4c historical labels
unchanged.  Rename BZ1–BZ4 headings in active
`bluez-wireplumber-interoperability-plan.md`; preserve dated BlueZ handoff and
result filenames/headings as historical evidence.  Avoid blind global
replacement.

### Current architecture tables

Add current production modules where missing:

- `src/app_lifecycle.c`: pure fatal boot coordinator; `src/main.c` is hardware
  wiring, watchdog, and advertising-loop adapter.
- `src/audio_modea.c`: bounded two-CIS event assembler and per-channel PLC.
- `src/audio_iso_seq.c`: pure per-CIS omitted-callback sequence tracker.
- `src/bt_pairing_policy.c`: pure OPEN/BONDED_ONLY policy snapshot; Bluetooth
  controller work remains in `bt_bap.c`.

Update README/AGENTS/design tables without expanding into optional cleanup.
Update README active suite inventory to 28 Twister + 4 exec-only + 12 Python,
47 total gate children.  Describe accepted local BSim as 15-scenario matrix.

### BSim truth

Do not present historical `0xFE0D4245` as current expected hash.  Active values
come from `scripts/bsim-stage1-run.sh` and must not change:

- mono 10 ms: `0x22AB5C0D`
- Mode A/B 10 ms: `0xBAE24F7E`
- reconnect: fresh mono hash

Historical result blocks may retain their dated old hashes, labeled historical
or superseded where needed.

Active docs must state official upstream smoke remains PARTIAL because of
documented upstream teardown disable-race and is **not production acceptance**.
Accepted local gate is `scripts/bsim-stage1-run.sh`.

### Undefined T9 wording

There is no T9 testing phase.  Replace only phase-like phrase “prior T9
failing-hardware provenance” in `STATUS.md`, pre-refactor testing plan, and
hardware-baseline line 290 with precise existing evidence: prior 8–30% RF-loss
hardware sessions documented in hardware baseline.  Preserve valid T8/T9/T10
hardware **session** identifiers, commands, `/tmp/t9/...` paths, measurements,
and matrix rows.  Do not alter evidence limitation or claim new activation.

### Behavior contract and coverage matrix

1. Set active contract versions to T8, 2026-08-04.
2. Keep first, detailed `CODEC-013 — Release slot semantics` block and delete
   second shorter duplicate.
3. Update CV-001 to schema v1 generated on clean `971e6a4`, committed in
   `1a5842d`, 26-file population, exact totals above.
4. Update current suite table in coverage matrix:
   - 28 Twister: existing 25 plus `bt_pairing_policy`, `iso_seq`, `modea`.
   - 4 exec-only unchanged.
   - 12 Python: existing 9 plus `hci_raw_connect`, `bap_central_policy`,
     `bap_central_writer`.
   - coverage text says rebuilds 28 Twister + 4 exec suites.
   - total 47.
5. Preserve clearly dated T7 41-child evidence as historical.

### 7.5 ms FLPR limitation

Add active STATUS “known behavior question” stating nRF54L15 360-frame/7.5 ms
calls fall back to cpuapp ASRC because FLPR payload contract is 480 frames.
This is not a new failure and not permission to implement 360-frame offload.
Cite direct witnesses:

- `tests/unit/audio_offload/src/test_audio_offload.c`:
  `test_asrc_invalid_frames`.
- `tests/unit/flpr_ring/src/test_flpr_ring.c`: MAX_INPUT 480 assertions.
- `src/flpr_ring.h`: `FLPR_RING_PAYLOAD_MAX_INPUT == 480U`.

## Test-matrix metadata changes

Keep production classifications/outcomes unchanged.  Update stale evidence:

1. `src/main.c` suite name `monitor.sh` → `read_acm.py`.
2. `src/main.c` hardware path `scripts/monitor.sh` →
   `scripts/read_acm.py`.
3. Replace its Phase-5-only current evidence path with
   `docs/testing/pre-refactor-hardware-baseline.md`.
4. `src/flpr/main.c` suite/path `monitor.sh` → `read_acm.py` /
   `scripts/read_acm.py`; retain valid Phase-6 FLPR result documents.
5. `dongle/hci_ipc/src/main.c`: remove PARTIAL official-smoke script from
   `hardware_acceptance` and use
   `docs/testing/pre-refactor-hardware-baseline.md` alongside existing build
   and central evidence.  Official smoke may remain documented as PARTIAL,
   never accepted hardware evidence.

Do not delete `scripts/monitor.sh` in R0; retirement belongs to R3.

## Coverage tool-version enforcement

Recorded versions must become enforceable, not decorative.

### Required behavior

In default baseline mode, after current versions are captured and baseline is
loaded:

- Compare current `GCOVR_VERSION` with baseline `gcovr_version` when present.
- Compare current `GCOV_VERSION` with baseline `gcov_version` when present.
- Add clear baseline-enforcement errors for mismatches and direct operator to
  refresh intentionally with `--write-baseline`.
- Keep compatibility with old baselines that omit either version field.
- `--write-baseline` continues recording current versions.
- `--report-only` remains non-enforcing.
- Do not change numeric ratios, population, schema version, or current
  baseline JSON.

Pass current version strings as arguments into existing Python baseline
enforcement block in `scripts/test-coverage.sh`; do not add dependencies.
Also replace stale “add pkgs.gcovr” error with “re-enter dev shell” guidance,
because `flake.nix` already provides gcovr.

Comparison is case-sensitive equality of complete first-line strings already
captured with `head -n1`.  Emit these message shapes (normal enforcement loop
adds its existing `error:` prefix):

```text
gcovr version mismatch: current 'gcovr X' vs baseline 'gcovr Y' (refresh intentionally with --write-baseline PATH)
gcov version mismatch: current 'gcov X' vs baseline 'gcov Y' (refresh intentionally with --write-baseline PATH)
```

Tests must assert tool name, current value, baseline value, and refresh
instruction; shell/Python quoting may follow existing `%r` style.

### Required tests

Extend existing fake-tool public-boundary suite:

- matching recorded gcovr/gcov passes;
- gcovr mismatch fails with exact useful diagnostic;
- gcov mismatch fails with exact useful diagnostic;
- absent version fields remain accepted;
- write-baseline still records fake current versions;
- report-only does not enforce baseline versions.

Test observable script exits/output, not internal helper shape.

## Test-all inventory comment

Update `scripts/test-all.sh` current header from 25 to 28 Twister suites.  Keep
runtime discovery unchanged and do not hardcode a 47 acceptance check in R0.

## Non-scope

- No files under `src/`, `dongle/`, boards, Kconfig, CMake, or firmware config.
- No baseline regeneration or expected hash/count update.
- No test-runner architecture consolidation (R3).
- No deletion of historical handoffs/results or `monitor.sh` (R3).
- No 360-frame FLPR support (deferred feature).
- No CI, hardware, flash, serial, pairing, or RF work.

## Implementation and commit order

1. Implement coverage enforcement/tests, test-all comment, and test-matrix
   metadata.  Run focused checks.  Commit as one tooling/metadata commit.
2. Reconcile active docs and commit this handoff with them as one docs commit.
3. This clean docs commit is the **R0 implementation commit**.  Run full G1 on
   this exact hash.
4. If every criterion passes, write R0 results with exact commits, commands,
   counts, runtimes, warning disposition, and mark R0 accepted in refactor plan.
   Commit evidence/docs separately as the **R0 evidence commit**.  Record the
   tested implementation hash.  Run `git diff --check` and focused docs/matrix
   checks on evidence commit; do not rerun G1 solely because evidence was added.

Never amend.  Any non-evidence correction creates a new implementation commit
and requires full G1 rerun; do not record acceptance until exact implementation
commit passes.

## Focused verification before commits

```bash
bash -n scripts/test-all.sh
bash -n scripts/test-coverage.sh
python3 tests/unit/test_coverage_runner/test_test_coverage_runner.py
python3 tests/unit/test_matrix/test_check_test_matrix.py
python3 scripts/check-test-matrix.py --repo-root "$PWD"
python3 -m json.tool tests/test-matrix.json >/dev/null
git diff --check
```

Also verify machine-derived counts with repository-safe Python or shell logic:
28 Twister, 4 exec-only, 12 Python, 47 total.  Do not use a fragile count that
includes build output or shared `audio_i2s_common` as a child.

## Clean G1 acceptance gate

Run from repo root in dev shell on clean exact commit:

```bash
./scripts/test-all.sh
./scripts/test-coverage.sh --output /tmp/r0-coverage --clean-output
fw-build-5340
fw-build-54l15
fw-build-dongle
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
git diff --check
```

Acceptance:

- Canonical gate exactly 47 PASS / 0 FAIL / 47 TOTAL.
- Coverage baseline passes with exact 26-file population and matching tool
  versions.
- Three production builds and 76/76 build contract pass.
- Zero actionable compiler, Kconfig, boot, build-contract, or script warnings.
- BSim expected hashes/counts unchanged.
- Worktree clean before evidence update.

No hardware run unless docs/tooling unexpectedly change firmware build output
or another unexplained condition appears.

## Escalation

Stop and report without weakening tests or changing scope if:

- live tool versions differ from committed baseline;
- suite count is not 28/4/12/47;
- any replacement evidence path fails matrix checker;
- a full-gate hash/count changes;
- build output gains warning or contract drift;
- documentation evidence conflicts with repository truth;
- two materially different fixes fail for same blocker.

Return exact command/output, current status, partial changes, and one precise
question.  Do not commit incomplete/failing work.

## Executor recap

Return:

- files changed by commit;
- exact commit hashes/messages;
- focused and G1 commands/results/runtimes;
- exact 47-child and coverage totals;
- build-contract result;
- warning/hash disposition;
- final status;
- deviations or blockers.

Do not push, merge, open PR, amend, force-push, or add attribution footers.
