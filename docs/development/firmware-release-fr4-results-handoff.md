# FR4 results and current-state reconciliation handoff

Date: 2026-08-10

## Goal

Record the completed FR4 evidence accurately:

1. exact draft candidate `v0.1.0` failed mandatory nRF5340 mono hardware
   acceptance and remains private, unpublished, and untagged;
2. the cadence-concealment and stack fixes at local HEAD `5e7f502` passed the
   approved six-row local hardware matrix on both receiver targets;
3. that local result is replacement-candidate preflight only, not FR4
   exact-artifact acceptance;
4. FR4 and FR5 remain blocked until a new immutable draft is created and its
   exact assets pass the full procedure.

Update current-state documentation without changing firmware, tests, release
state, tags, `VERSION`, CI, or remote branches.

## In scope

- Add `docs/development/firmware-release-fr4-results.md` as canonical evidence
  for the failed exact candidate and successful local replacement preflight.
- Update current-state text in:
  - `STATUS.md`;
  - `AGENTS.md`;
  - `PLANNED_FEATURES.md`;
  - `docs/design.md`;
  - `docs/development/firmware-release-plan.md`;
  - `docs/development/firmware-release-fr4-procedure.md`.
- Correct the FR4 procedure's draft-body byte-comparison command. The executed
  `gh api --jq .body > file` form appended one CLI newline and was not
  byte-exact. Extract `.body` from the already-private saved
  `metadata/release.json` with stdlib Python, require a string, UTF-8 encode it
  without adding bytes, and compare that output to regenerated notes.
- Commit this handoff with the results documentation in one documentation-only
  commit.

## Out of scope

- Any firmware, configuration, test, packaging-script, or workflow change.
- Selecting or writing a replacement version into `VERSION`.
- Changing the failed GitHub draft, deleting it, replacing assets, publishing
  it, or creating another draft.
- Tag creation, push, PR, merge, hosted CI, flashing, reset, recovery, serial
  work, or further hardware runs.
- Claiming FR4 or FR5 acceptance.
- Updating historical counts inside earlier accepted result sections when
  those counts correctly describe their own revision.
- MCUboot, DFU, signing, or public release instructions.

## Grounding evidence

### Repository state

- Expected starting HEAD: `5e7f50235640...`, commit
  `fix: increase nRF54L15 pairing workqueue stack`.
- Expected worktree: clean.
- Current branch: `feature/firmware-release-acceptance`, ahead of its remote;
  do not push.
- Canonical gate already passed at this code state:
  **65 PASS / 0 FAIL / 65 TOTAL**.
- Focused suites already passed: `audio.iso_seq` 39/39,
  `audio_stream_session` 47/47, build-contract checker tests 52/52.
- Build contract: **96 assertions, 0 failed**.
- BSim Stage 1 pins remain byte-identical.
- Fresh report-only coverage run after final hardware work:

  ```text
  ./scripts/test-coverage.sh --report-only \
    --output /tmp/opencode/fr4-final-coverage --clean-output
  numeric lines: 4777/5234 (91.3%)
  numeric branches: 2091/2896 (72.2%)
  numeric functions: 363/363 (100.0%)
  numeric population files: 36
  exit 0
  ```

  The committed coverage baseline remains unchanged.

### Failed exact draft candidate

Canonical retained evidence:
`/tmp/opencode/fr4-v0.1.0-OEp9Kh/MANIFEST.md`.

- Draft release ID `367572702`, `tag_name=v0.1.0`, draft true, prerelease
  false, target
  `3d9a9186ec288484a637dac1dc7460319daf5e84`.
- Draft and its four assets passed identity, checksum, ZIP, internal checksum,
  provenance, notes-body, and untagged-ref validation.
- nRF5340 Mode A diagnostic passed on exact draft bytes.
- After central mono selection fix `9f456b1`, strict mono established exactly
  one CIS and sent 12000 frames over 120 seconds, but receiver reported:

  ```text
  SDUs=8876 decoded=8876 plc=0 decode_err=0
  i2s_underrun=0 stream_reset=225 empty_sdu=0
  ```

- Receiver emitted 225 each of:
  - `i2s_nrfx: Next buffers not supplied on time`;
  - `i2s_nrfx: Cannot write in state: 4`;
  - `audio_i2s: I2S underrun, restarting DMA`.
- This deterministic mandatory failure stopped the matrix before nRF54L15.
- Exact `v0.1.0` candidate therefore failed FR4. It remains private,
  unpublished, and intentionally untagged. Nothing may describe it as awaiting
  a first run or as accepted.

### Local fix progression

Retained manifests:

1. `/tmp/opencode/fr4-cadence-local-b6jNTm/MANIFEST.md`: cadence concealment
   activated and removed DMA resets, but four cadence RESYNC warnings exposed
   an overly tight fixed timestamp tolerance.
2. `/tmp/opencode/fr4-cadence-local-bcrVW1/MANIFEST.md`: scaled tolerance was
   present, but nRF5340 faulted with `ZEPHYR FATAL ERROR 2: Stack overflow on
   CPU 0`, current thread `sysworkq`.
3. `/tmp/opencode/fr4-cadence-local-stSZAJ/MANIFEST.md`: six stream rows
   passed after increasing nRF5340 sysworkq to 2048, but review measured
   nRF54L15 `g_pairing_wq` at 1012/1024, 98 percent, only 12 bytes unused.
   Review correctly blocked final acceptance of the local preflight.
4. `/tmp/opencode/fr4-cadence-local-yc4N3U/MANIFEST.md`: final rerun at
   `5e7f502` after increasing `g_pairing_wq` to 1536. All six rows passed on
   first attempt.

Implementation sequence to cite concisely where useful:

```text
9f456b1 fix: reserve mono ASE during BlueZ selection
60e2ac1 fix: conceal timestamp-detected ISO omissions
606fbed fix: complete ISO cadence verification
0f3ad2c fix: scale ISO cadence timestamp tolerance
7d4fc71 test: complete ISO cadence diagnostic coverage
ec8c846 fix: increase nRF5340 system workqueue stack
5e7f502 fix: increase nRF54L15 pairing workqueue stack
```

### Final local hardware evidence

Canonical retained evidence:
`/tmp/opencode/fr4-cadence-local-yc4N3U/MANIFEST.md`.

Both pristine builds passed with only the documented NCS v3.3.0 diagnostic
set. Both targets flashed and verified normally. No recovery, mass erase,
probe-rs, settings erase, release write, tag, push, or CI action occurred.

Final image identities from committed local HEAD `5e7f502`:

| Image | Bytes | SHA-256 |
|---|---:|---|
| nRF5340 `merged.hex` | 1037672 | `ab8abda54987d2cd0cb664ca58ee95a42907d0713e562f95f1e4fb7463a990fd` |
| nRF5340 `merged_CPUNET.hex` | 403684 | `2ce0ca1aa9fdc27d9fc1a4b25148af82da2fb52f1834f61113685d0851119619` |
| nRF54L15 cpuapp | 1500940 | `85253a4b69a89c6bc8d7073a0d0c8ccbc50ba559ea6c6eefe5cbf5f3839cbbc0` |
| nRF54L15 FLPR | 91857 | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

Final row results:

| Target | Mode | Central | Receiver summary | Result |
|---|---|---|---|---|
| nRF5340 | mono | 12000 frames / 120 s / 100 fps, one CIS | `SDUs=8862 decoded=12007 plc=3145` | PASS |
| nRF5340 | Mode B | 12000 frames / 120 s / 100 fps, one CIS | `SDUs=9162 decoded=24034 plc=5710` | PASS |
| nRF5340 | Mode A | 3000 frames / 30 s / 100 fps, two CISes | stream 0 `SDUs=2789 decoded=5578 plc=1`; stream 1 `SDUs=2805` | PASS |
| nRF54L15 | mono | 12000 frames / 120 s / 100 fps, one CIS | `SDUs=10153 decoded=12040 plc=1887` | PASS |
| nRF54L15 | Mode B | 12000 frames / 120 s / 100 fps, one CIS | `SDUs=10319 decoded=24080 plc=3442` | PASS |
| nRF54L15 | Mode A | 3000 frames / 30 s / 100 fps, two CISes | stream 0 `SDUs=2195 decoded=6088 plc=1698`; stream 1 `SDUs=2208` | PASS |

Every final row had `decode_err=0`, `i2s_underrun=0`, `stream_reset=0`, zero
cadence RESYNC warnings, and zero unexplained warnings. nRF54L15 FLPR stayed
ACTIVE/Ready/ACKed/Healthy, submit equaled success, fallback was zero, and all
fault/recovery counters were zero.

Stack gates:

- nRF5340 sysworkq: 844/2048 used, 41 percent, 1204 bytes unused.
- nRF54L15 `g_pairing_wq`: 1012/1536 used, 65 percent, 524 bytes unused,
  identified by runtime thread-object address matching ELF symbol
  `g_pairing_wq`.

Cadence activation was observed on both targets. Physical audibility was not
observed and was not required for this local diagnostic preflight.

## Required documentation shape

### New FR4 results document

Create `docs/development/firmware-release-fr4-results.md` with:

- status at top: **BLOCKED**, not accepted;
- separate sections for exact `v0.1.0` candidate failure, defect/fix
  progression, final local matrix, software gates, retained evidence, safety
  boundary, and next step;
- exact distinction between immutable draft evidence and local build evidence;
- concise tables for final rows, stacks, and image identities;
- explicit statement that a replacement candidate must be created through the
  accepted trusted-main lifecycle, then its exact immutable assets must rerun
  FR4 before FR5 can publish anything;
- no claim that a replacement version has already been chosen;
- no raw log or binary copied into the repository.

### Current-state files

1. `STATUS.md`
   - Date/current state becomes 2026-08-10.
   - Current coverage becomes population 36, 4777/5234 lines, 2091/2896
     branches, 363/363 functions.
   - Current build contract becomes 96/96.
   - Keep historical FR1/FR2/FR3 counts unchanged where they describe those
     earlier revisions.
   - Add a prominent FR4 section before FR3: exact `v0.1.0` candidate FAILED;
     local fix preflight PASSED; FR4 remains BLOCKED pending replacement exact
     assets; FR5 remains blocked; nothing published.

2. `AGENTS.md`
   - Update only current operational status paragraph: new coverage totals,
     96/96 contract, failed exact draft, passed local preflight, replacement
     exact-artifact requirement.
   - Preserve historical baseline figures and all operational rules.

3. `PLANNED_FEATURES.md`
   - Update public-release status to: FR1-FR3 accepted; exact `v0.1.0` failed
     FR4 and remains unpublished; local replacement preflight passed; new exact
     candidate still required; nothing published.
   - This file is user-facing. Do not introduce U+2014 em dashes.

4. `docs/design.md`
   - Preserve historical architecture/evidence framing.
   - Replace only stale "current authoritative state is FR3" wording with a
     concise pointer to current FR4 blocked/local-preflight state and the new
     results document. Keep historical FR3 numbers as historical evidence if
     useful, but do not present 95/95 or old coverage as current.

5. `docs/development/firmware-release-plan.md`
   - Update status to show FR1-FR3 accepted and FR4 blocked after failed first
     candidate.
   - In release lifecycle, retain immutable failure rule and clarify that a
     failed draft stays unpublished while a later versioned trusted-main
     candidate must be created rather than mutating accepted evidence.
   - Mark `v0.1.0` exact candidate failed, local fix preflight passed, and
     replacement exact-artifact run pending under FR4.
   - FR5 remains planned/blocked.

6. `docs/development/firmware-release-fr4-procedure.md`
   - Change status from "prepared, not executed" to historical procedure
     executed on 2026-08-10 and failed at mandatory nRF5340 mono.
   - Point to the new results document and retained run directory.
   - State that future candidate execution needs a newly pinned procedure or
     evidence set; do not reuse this document's IDs/hashes as current inputs.
   - Replace the newline-adding `gh --jq` body extraction with byte-exact
     stdlib Python extraction from the private saved release JSON.
   - Preserve exact `v0.1.0` IDs, hashes, commands, and history otherwise.

## Documentation hygiene constraints

- Treat current-state contradictions above as rewrites, not historical data to
  erase.
- Do not globally replace `95/95`; older result sections must retain values
  that were true at their recorded commits.
- Do not update the managed documentation-hygiene marker.
- Do not broaden into unrelated source-comment cleanup or agent-guide
  restructuring.
- Keep one canonical current FR4 results document and link to it instead of
  duplicating all row detail across every status file.
- Preserve normal prose in repository files. Handoff and results documents may
  use Unicode em dashes because they are internal; `PLANNED_FEATURES.md` may
  not.

## Verification

Run from repository root:

```bash
git diff --check
python3 scripts/check-test-matrix.py
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
python3 - <<'PY'
from pathlib import Path

public = Path("PLANNED_FEATURES.md")
if "\N{EM DASH}" in public.read_text(encoding="utf-8"):
    raise SystemExit(f"em dash found in {public}")
PY
```

Expected build contract: `96 assertions, 0 failed`, `BUILD CONTRACT PASSED`.
No firmware rerun is required because this commit changes documentation only
and the full gate/coverage/hardware evidence is already retained at the exact
starting code state.

Before committing, inspect:

```bash
git status --short
git diff --stat
git diff
git log --oneline -10
```

Stage only the handoff, new results document, and six named current-state
files. Commit once with:

```text
docs: record FR4 candidate hardware results
```

Do not amend, push, merge, create a PR, or add attribution.

## Escalation rule

Stop without committing and report to Orchestrator if evidence contradicts
this handoff, any current-state claim cannot be grounded, a verification fails
for an unexplained reason, or completing the work appears to require changing
firmware, tests, workflow, release state, or version policy. Preserve worktree
state and provide exact commands/errors plus one focused question.

## Required recap

Return:

- files changed and current behavior/state now documented;
- exact verification commands and results;
- commit hash/message;
- git status after commit;
- deviations, blockers, or unresolved contradictions;
- suggested next phase, without implementing it.
