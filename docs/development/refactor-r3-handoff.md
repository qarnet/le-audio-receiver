# R3 handoff — test-runner and hardware-gate consolidation

Start commit: `ee31b05`.  Branch: `handoff/workstation-transfer`.  This
document captures the exact decided shape before implementation; the
implementation must match it and the results document
(`docs/development/refactor-r3-results.md`) must record what actually
happened.

## Goal

One source of truth per suite/parser/hash set.  Adding a suite must not be
able to silently omit it from the canonical gate or from coverage.

## Scope (exact decisions)

### 1) Shared deterministic suite discovery — NO suite manifest

- New stdlib-only `scripts/test_inventory.py` is the **sole** filesystem
  classification source for gate, coverage, and checker.
- Classification (deterministic sorted output):
  - **Twister C**: immediate `tests/unit/*/` dirs containing `testcase.yaml`.
  - **Exec-only C**: immediate `tests/unit/*/` dirs containing
    `CMakeLists.txt` but **no** `testcase.yaml`.  Must yield exactly the
    current 4: `audio_offload`, `flpr_audio_process`, `flpr_ring`,
    `offload_asrc`.  Helper dirs lacking `CMakeLists.txt` are not suites.
  - **Python children**: immediate `tests/unit/*/test_*.py` files in dirs
    with neither `CMakeLists.txt` nor `testcase.yaml`, plus immediate
    `scripts/test_*.py`.  Each discovered file is one canonical child.
- CLI (bash-consumable, stable): `--twister` (suite name per line),
  `--exec-only` (suite name per line), `--python` (`label<TAB>relpath` per
  line), `--count` (total gate children), `--json` (full machine-readable
  record).  Importable API: `discover(repo_root)` returning an `Inventory`
  record with `.twister`, `.exec_only`, `.python_children` (label + path),
  `.total()`, `.to_dict()`; plus per-path helpers.
- Labels: unit python children label = immediate dir basename (keeps
  `gate`, `flpr_stall_gate`, `bsim_runner`, ...); scripts python children
  label = file stem minus leading `test_`
  (`test_bluez_wireplumber_gate.py` → `bluez_wireplumber_gate`).
- Module raises on duplicate discovered labels/paths (defensive); paths are
  repo-relative with `/` separators.
- `scripts/test-all.sh`: consume module for all three categories; each
  discovered python file is one `run_one` child; set
  `PYTHONPATH=$REPO_ROOT/scripts:${PYTHONPATH:-}` uniformly (harmless for
  suites that do not need it); preserve failure aggregation and total
  semantics (47 children).
- `scripts/test-coverage.sh`: consume module for the C suite build list and
  the embedded run-manifest generation.  Remove all copied exec lists and
  copied Twister discovery.  Empty category is valid in fixture repos; at
  least one total C suite is still required.
- `scripts/check-test-matrix.py`: import the inventory module; add an
  inventory-consistency check (duplicates impossible/error, paths exist,
  categories obey shape, python children only from the two allowed roots).
  Preserve existing `resolve_suite` behavior needed by fixtures and
  manifest evidence names.
- Fixture coverage: `tests/unit/test_coverage_runner` fixtures gain the
  inventory module (fixture repo scripts dir) plus missing/duplicate/
  ambiguous/malformed suite cases; `tests/unit/test_matrix` gains direct
  inventory CLI/checker-outcome tests on fixture repos.
- Expected composition after all moves stays **28 twister + 4 exec-only +
  12 Python + coverage + matrix + BSim = 47** gate children.

### 2) Retire obsolete `tests/unit/gate/` without loss

- Delete `tests/unit/gate/` after migration.
- Move the 5 unique `fw-flash-dongle` public-execution tests
  (`test_dongle_flash_*`) into new
  `tests/unit/fw_flash_dongle/test_fw_flash_dongle.py`, unchanged in
  behavioral intent (this replaces python child `gate`; total stays 12).
- Migrate the obsolete stall/parser/runner assertions into
  `tests/unit/flpr_stall_gate/test_flpr_stall_gate.py`:
  individual nonzero integrity-fault key parsing; zero-fault keys; multiple
  timed-ack lines; wrong duration rejected; timeout/full accepted evidence
  path; integrity faults rejected; full gate can pass with an allowed
  timeout fault; seq fault fails; integrity-only faults rejected; ACTIVE /
  FALLBACK / max-exhaustion parse cases; stale-state (never ACTIVE)
  timeout.  Merge equivalent assertions (e.g. max-exhaustion recovery count
  into the existing exhaustion parse test); do **not** retain duplicates
  merely to preserve raw function count.
- Record the one-to-one ownership map in handoff/results.

### 3) Shared FLPR parser/result data, separate state machines

- New stdlib-only `scripts/flpr_status.py`: common offload-status regexes
  (state/counters/recovery/probation/faults/runtime/hb-dedup/recovery-OK)
  and one superset `parse_offload_status(text)` dict consumed by both
  `scripts/flpr_stall_gate.py` and `scripts/flpr_hang_gate.py`.  Superset
  covers the hang schema (epoch/gen/runtime/hb_dedup); stall consumes the
  fields it needs.  Command-specific ACK regexes
  (`RE_STALL_TIMED_ACK`; `RE_FAULT_HANG_ACK`, `RE_FAULT_HANG_FAIL`,
  `RE_RUNTIME_RESTART_OK`) and state machines stay in their scripts.
- Stall/hang modules re-export the shared regexes they previously defined
  so existing imports/tests keep working; duplicated regex definitions are
  removed.
- Hang gate gets explicit fakeable boundaries equivalent to stall:
  injectable serial transport and BAP process/launcher behavior into
  `HangGateRunner`; production defaults retain current pyserial (lazy) and
  subprocess behavior.  No pyserial import requirement for unit import/
  tests.
- Add runner lifecycle/state tests to
  `tests/unit/flpr_hang_gate/test_flpr_hang_gate.py`: success path, console
  not responsive, bap early-exit, FAULT_HANG_ACK timeout with process
  cleanup, recovery timeout.  Do not weaken existing parser tests.

### 4) BlueZ/WirePlumber ownership

- Keep `scripts/bluez-wireplumber-gate.py`,
  `scripts/bluez-wireplumber-phase3-gate.py`, and both test files.
  BZ2 owns many base cases not in BZ3.
- Replace phase3's inline PACS/ASCS/VCS UUID dict in
  `check_remote_uuids` with the base module `REMOTE_UUIDS`; add a small
  shared-source test.  No behavior/output change.

### 5) BSim versioned data

- New versioned JSON `tests/bsim/stage1-scenarios.json`: all 16 scenario
  names, run counts, parser contract metadata (`dec_calls`, channel mode),
  and the exact known full/L/R hashes + totals currently pinned in
  `scripts/bsim-stage1-run.sh`.  Values copied exactly; **no repinning**.
- `scripts/bsim-stage1-run.sh` derives matrix, hashes, counts, baseline
  output, and summary lists from this file via Python stdlib (no jq).
  Validate schema early; fail clearly on missing/duplicate/invalid fields.
- `scripts/bsim_stage1_parse.py` loads the same file by default for
  scenario metadata (`SCENARIOS` replacement) and exposes the file-known
  values.  Explicit CLI `--known-*` flags remain the override path used by
  the shell and baseline tests; precedence documented: explicit flags >
  file defaults; `--no-known` disables known assertions (baseline mode).
- Extend `tests/unit/bsim_runner/test_bsim_stage1_parse.py` with
  malformed/missing/duplicate schema fixture coverage and proof that all 16
  unchanged production pins load.  Baseline mode is not acceptance for
  pins; the full BSim gate proves unchanged runtime outputs.

### 6) Retire stale active artifacts

- Delete tracked `scripts/monitor.sh`.
- Delete tracked `tests/hardware/` diagnostic apps (not gate-wired;
  historical Phase 4 results remain evidence).
- Remove ignored local `scripts/probe-serial.local.bak` (not a commit
  change).  Keep tracked `.example` and the override mechanism.
- Remove the stale active README listing for `tests/hardware/`.

### 7) Docs/results

- Update active docs: new inventory model, exact current child
  composition/counts, parser ownership, moved dongle test path, BSim data
  owner, retirements.  At minimum README.md, STATUS.md,
  docs/testing/coverage-matrix.md,
  docs/development/workstation-transfer-status.md, and any active
  references.  Historical evidence docs stay unchanged
  (`docs/development/fw-flash-dongle-probe-fix-handoff.md` may keep its
  historical path).
- Add `docs/development/refactor-r3-results.md`: one-to-one obsolete test
  ownership map, exact files, focused results, canonical gate result,
  build result, matrix result, coverage population/tool versions, BSim
  result.
- Mark R3 complete in plan/status only after full verification passes.

## Out of scope

- R4 shell command/source separation.
- Production C behavior/API changes.
- Coverage baseline migration or pin changes.
- Hardware flash/stream runs.
- BSim expected value updates.
- Removing either BlueZ test suite.
- Editing historical result claims.

## Verification

Focused first:

- Inventory CLI: exact 28/4/12 before the full gate.
- Python suites: fw_flash_dongle, flpr_stall_gate, flpr_hang_gate, both
  BlueZ suites, bsim_runner, test_matrix, test_coverage_runner.
- `bash -n scripts/test-all.sh scripts/test-coverage.sh
  scripts/bsim-stage1-run.sh`.
- Matrix checker fixture suite.

Then G1 exactly as the repo requires:

- `scripts/test-all.sh` from the dev shell: **47 PASS / 0 FAIL /
  47 TOTAL** including coverage + matrix + full accepted BSim Stage 1.
- `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle`; inspect logs for
  all actionable warnings under AGENTS.md policy.
- `python3 scripts/check-build-contract.py`: expect 76/76.

No hardware run expected.

## Escalation

Stop and report (preserve worktree) if: two materially different attempts
fail, repository evidence contradicts this shape, tests would need
weakening, expected BSim pins differ, the full gate count differs without
explained ownership, a warning cannot be explained, destructive/hardware
action seems needed, or scope must expand.
