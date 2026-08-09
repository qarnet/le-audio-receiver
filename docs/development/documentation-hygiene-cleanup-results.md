# Documentation-hygiene full cleanup results

Base commit `72abbc4` (approved behavior fixes + clean canonical 62/62).
Mode: full-repository baseline audit (user-approved).  Scope: prose,
comments, and repo-local agent skills only — no behavior change, no
coverage-baseline change, no BSim pin change, no hardware.

Track: apply the exact corrections from the approved full-repository
cleanup handoff (deleted at the 2026-08-08 wrap-up; this results doc
records its scope and outcome), preserve
historical evidence, remove phase/commit/handoff chronology from current
production/API comments while retaining rationale, update repo-local
skills, and reconcile the P8 evidence wording.

## Audit scope and counts

Full-audit inventory (reconciled session ledger, 568 rows):

- Original candidate inventory: **544 human-written files** (source,
  headers, Kconfig, devicetree, CMake, scripts, tests, repo-local skills,
  and docs). Of these, **115 rewritten** and **429 clean** — most audited
  files needed no change.
- **21 valid exclusions** — generated, machine-consumed, or non-prose
  files, all out of audit scope:
  - tool/config files (8): `.clang-format`, `.clangd`, `.envrc`,
    `.gitattributes`, `.gitignore`, `flake.nix`, `opencode.json`,
    `scripts/probe-serial.local.example`;
  - legal boilerplate (1): `LICENSE`;
  - generated lockfile (1): `flake.lock`;
  - machine-consumed structured data (3): `tests/bsim/stage1-scenarios.json`,
    `tests/coverage-baseline.json`, `tests/test-matrix.json`;
  - binary/corpus golden fixtures (8): all files under `tests/fixtures/lc3/`.
- **3 new cleanup-output docs** added during the audit range — the cleanup
  handoff (deleted at the 2026-08-08 wrap-up), this results doc, and the
  managed marker — each audited clean.
- Totals: **547 non-excluded files audited, 115 rewritten, 432 clean,
  0 moved, 0 deleted, 21 excluded.**

Archived historical docs (`docs/development/archive/` and dated
phase/refactor/pre-refactor result and handoff docs) were **audited clean**,
not excluded: the skill preserves valid history in dated evidence docs, so
they remain in scope and require no change. Dongle source (`dongle/` —
`hci_identity.h`, `hci_ipc/`, `hci_uart/`, `README.md`) was likewise
**audited clean**: it is repo-owned, lab-maintained firmware source, not an
excludable artifact, and its comments were verified against the current
dongle build. Not every audited file changed: 429 of the 544 original
candidates (78.9%) were already clean; only 115 were rewritten.

## 1. Active-doc corrections

- `README.md`: current inventory/gate counts (35 twister + 5 exec-only +
  19 Python = 59 unit children, 62 gate children), pairing status, and
  removed stale 31/52/55 figures and R6/R7/P1/P2/P4/R8 chronology tags
  in the repository-layout table.
- `STATUS.md`: top-level current state states gate **62 PASS / 0 FAIL /
  62 TOTAL**, population 36, contract 95/95 (clean-tree run at
  `b8bd633`); R0–R10 (55/33/79) and T0–T8 figures are marked
  explicitly historical.  P8 deviations wording qualified (see §4).
- `AGENTS.md`: current-state paragraph updated to the 62/62 gate,
  population 36, contract 95/95 with R10/T0–T8 as historical baselines.
- `docs/design.md`: R10 55/33/79 state demoted to historical refactor
  baseline; current authoritative state points to P8/STATUS; BSim claims
  updated to 17 scenarios / 26 runs; the "no expansion planned"
  contradiction removed.
- `docs/development/workstation-transfer-status.md`: marked as a
  historical snapshot with a pointer to the P8/STATUS current state.
- `docs/testing/coverage-matrix.md`: Python inventory 16 → 19, gate
  children 59 → 62, current suite counts corrected against the sources
  (lifecycle 33, timing_nrf54 21, audio_shell 14, audio_shell_noperf 11,
  audio_shell_nrf54 43,
  decode 43, audio_stream_session 35, flpr_handshake 34, flpr_ring_mgr
  45, bt_pairing_policy 23), stale "32 twister"/"55 children"/
  "population 33" claims updated, and the `bt_bap.c` row's BSim matrix
  updated to the 17-scenario T4+R7 set.
- `docs/testing/behavior-contract.md`: lifecycle 33, timing_nrf54 21,
  audio_shell 14 (perf enabled; 15 declared incl. the perf-disabled-only
  test that runs in audio_shell_noperf 11), audio_shell_nrf54 43, CV-001
  baseline provenance moved
  to the current P5 baseline (`94c2742`, 36 files, 4665/5121, 2023/2820,
  357/357) with the R4/R6/R8 and P1/P2/P4/P5 migration chain recorded.
- `docs/testing/t4-bap-bsim-matrix.md`: current runner stated as
  17 scenarios / 26 runs; the T4 15-scenario acceptance table remains
  clearly historical.
- `docs/development/user-pairing-control-p8-results.md` and
  `STATUS.md` P8 section: "user requested CLI" attribution removed; the
  combined evidence is stated accurately — the user confirmed the
  threshold/LED observations, the CLI produced the instrumented RESET
  ordering through the same transition owner, and the exact instrumented
  physical-hold ordering was not captured in the same run.

## 2. Production/source comment corrections

- `src/flpr_ring.h`: obsolete sentinel-slot/full-at-N−1 statements
  removed — monotonic counters use all slots, full at N.
- `src/audio_timing_none.c`: nRF54 path described as
  GRTC+TIMER20+GPPI PCLK measurement (not LRCK).
- `src/audio_perf.h`: deadline comparison described at cycle-end
  recording (`audio_perf_cycle_end`), not snapshot/print.
- `src/flpr/main.c`: header rewritten to current FLPR ASRC/IPC/runtime
  responsibility; Stage 0/1/2/3A/4B chronology removed.
- `src/audio_asrc.c`, `src/audio_i2s.c`: "Commit:" history wording
  replaced with current state-update invariants.
- `CMakeLists.txt`, `boards/ebyte/e83_nrf5340/board.cmake`:
  `fw-probes` → `nrf-probes`.
- `scripts/test-all.sh`, `scripts/test-coverage.sh`: static stale counts
  and R8/R9/T7 narration removed; dynamic inventory behavior and the
  current gate stated.
- `scripts/check-build-contract.py`: stale release-default-off and
  pending-P8 wording removed; runtime stack validation now recorded as
  P8-accepted.
- `tests/unit/lifecycle/src/test_lifecycle.c`: test renamed to
  `test_close_clears_gate_then_restart_reopens`; the removed
  `l_received`/`r_received` claim replaced with the lifecycle
  close/reopen behavior actually asserted.  No matrix witness referenced
  the old name.

## 3. Chronology removal across production/shared headers and tests

Phase/commit/handoff markers (`R1`…`R10`, `P1`…`P8`, `T4`–`T8`, `Phase`,
`Stage` where narration) removed from current comments in: `bt_bap.c/h`,
`audio_stream_session.c`, `stream_lifecycle.c`, `pairing_mode.c`,
`user_pairing_io.c`, `bt_bap_pairing_adapter.c`, `main.c`,
`audio_drift.c`, `audio_timing_nrf54.c`, `audio_offload.c/h`,
`audio_shell.c`, `flpr_shell.c`, `flpr_acceptance.c`,
`flpr_acceptance_shell.c`, `flpr_handshake.c/h`, `flpr_ring_mgr.c/h`,
`flpr_runtime.c`, `flpr_protocol.h`, `flpr/main.c`,
`flpr/acceptance.c`, `flpr/prj.conf`, `flpr/CMakeLists.txt`,
`Kconfig`, the nRF54L15 board conf/overlays, `scripts/bap_central_*.py`
(including all "pre-split"/R9 references), `scripts/bsim_stage1_parse.py`,
`scripts/flpr_*_gate.py`, `scripts/bluez-wireplumber-*.py`,
`scripts/check-test-matrix.py`, `scripts/hci_raw_connect.py`,
`scripts/bin/fw-flash-54l15`, and the unit/BSim test files and
CMakeLists.  Actual ownership, concurrency, ordering, protocol,
hardware, compatibility, and error rationale was preserved and rewritten
without the marker.  Stable names were kept: "Stage 1" as the public BSim
gate name, and the BlueZ/WirePlumber "Phase 2/3" gate names.

## 4. Agent-skill corrections

- `.agents/skills/commit-and-push/SKILL.md`: repo-root `fw-build-5340`
  build/artifacts and the autonomous central-only verification rule.
- `.agents/skills/monitor-and-analyze/SKILL.md`: E83 `/dev/ttyUSB0`
  console, current repo path, `fw-build-5340`/`fw-flash-5340`, targeted
  reader cleanup (no global `pkill -9`), OpenOCD-only reset/recovery, no
  `nrfutil device recover`, and "never normalize underrun warnings".

Restart OpenCode for the skill changes to take effect.

Note: these repo-local skills (`.agents/skills/commit-and-push/` and
`.agents/skills/monitor-and-analyze/`) were later **removed** in commit
`9fca1a9` ("Delete old skills"); the paths above describe the prior audit
state of this documentation-hygiene run, not the current repository
layout.

## 5. Remaining script prose

- Phase-3 gate serial lifecycle, current `main.c` refs, and atexit
  limitations verified accurate (only the stale "per handoff"
  references removed).
- `flpr_hang_gate.py` header now matches the exact enforced checks
  (baseline-diff recovery/restart counts, resumed-success tolerance,
  per-field audio-fault presence and zero).
- `hci_raw_connect.py` docstring now describes per-attempt cancel and
  the global force-cancel deadline semantics.
- `bap_central_security.py` termination description kept; all
  pre-split line references removed.
- Official BSim smoke parser contract restated as ">=100 valid RX SDUs".
- `fw-flash-54l15` states the separate-image path only.
- `stage1-scenarios.json` duplicate-release note matches the
  transport-visible ASCS rejection (`INVALID_ASE_STATE`) without R7
  chronology.

## Verification

- `git diff --check` clean on every commit.
- `python3 -m py_compile` on every changed Python module and test;
  `bash -n` on every changed shell script.
- `python3 scripts/test_inventory.py --json`: 35 twister + 5 exec-only +
  19 Python = 59 unit children; gate 62.
- All 17 Python suites (19 children) pass locally: bsim_runner,
  test_matrix, build_contract, test_coverage_runner, all six
  bap_central suites, flpr_hang_gate, flpr_stall_gate, hci_raw_connect,
  bsim_official_smoke, fw_build_dongle, fw_flash_dongle, fw_reset_dongle,
  bluez_wireplumber_gate, bluez_wireplumber_phase3_gate.
- Focused C suites recompiled and executed on native_sim: lifecycle
  (incl. the renamed test), audio_offload, offload_asrc,
  offload_asrc_verify, flpr_protocol, flpr_handshake, flpr_ring_mgr,
  flpr_acceptance, timing_nrf54, drift, pairing_mode, user_pairing_io,
  audio_stream_session, bt_bap_pairing_adapter, audio_shell_nrf54 —
  all pass with zero warnings.
- Build contract on existing resolved builds: **95 assertions, 0 failed,
  BUILD CONTRACT PASSED** (no Kconfig/DT values changed — comment-only).
- Canonical clean-tree gate `./scripts/test-all.sh`: see the gate
  section below.

### Canonical gate

Full `./scripts/test-all.sh` run on the clean committed tree (worktree
clean), captured to the session log:

```
baseline enforcement: 0 error(s)
baseline enforcement PASS (against .../tests/coverage-baseline.json)
  PASS: coverage: native suites + baseline
check-test-matrix: 0 error(s), 0 note(s)
  PASS: matrix: manifest + coverage.json
=== STAGE1 (T4 matrix) PASS — all scenarios strict-checked ===
  PASS: bsim: stage1
Gate complete: 62 PASS / 0 FAIL / 62 TOTAL
```

- Process exit code 0.
- All 62 children pass: twister C suites, exec-only C suites, all 19 python
  children, coverage (baseline enforcement, numeric exactly matching the
  committed baseline — 36 files, 4665/5121 lines, 2023/2820 branches,
  357/357 functions; no migration), matrix (0 errors / 0 notes), and the
  full 17-scenario T4+R7 BSim stage1 matrix with all pinned hashes
  byte-identical.

One gate-discovered regression during the run was fixed before the clean
re-run: comment cleanup had corrupted the shell command registry
(`reset - stats` / `perf - reset` spaced names, unreachable per T6
findings); the exact `reset-stats` / `perf-reset` names were restored,
and incidental clang-format reflow churn in `audio_offload.c` and the
`audio_shell_nrf54` test was reverted — the final diff is comment-only.

## Deviations

- None from the handoff's required fixes.  No behavior, baseline, BSim
  pin, hardware, flashing, push, PR, amend, force-push, or attribution
  footer.

## Documentation hygiene

- Mode: full
- Default branch: `main`
- Audit base: `72abbc4`
- Audited revision: `b3da11a` (cleanup chain `72abbc4..b3da11a`, including
  staged/unstaged/untracked changes at audit time; this results doc was
  later corrected in a receipt-only follow-up commit)
- Files audited: 547
- Excluded: 21
- Clean: 432
- Rewritten: 115
- Moved: 0
- Deleted: 0
- Contributor docs changed: 2 (cleanup results, managed
  baseline marker; the cleanup handoff was deleted at the 2026-08-08
  wrap-up)
- ADRs added/superseded: 0/0
- Behavior changes: none
- Verification: `git diff --check` clean on every commit; `python3 -m
  py_compile` on every changed Python module/test and `bash -n` on every
  changed shell script; `python3 scripts/test_inventory.py --json` (59
  unit children, gate 62); all 17 Python suites (19 children) pass; focused
  native_sim C suites pass with zero warnings; build contract 95/95;
  canonical clean-tree gate 62 PASS / 0 FAIL / 62 TOTAL at `b8e7d85` with
  all BSim pins byte-identical.

### Important findings

- `docs/development/user-pairing-control-p8-results.md`, `STATUS.md`:
  removed "user requested CLI" attribution; stated combined evidence
  accurately — user confirmed threshold/LED observations, CLI produced the
  instrumented RESET ordering through the same transition owner, and the
  exact instrumented physical-hold ordering was not captured in the same
  run.
- `src/flpr_ring.h`: obsolete sentinel-slot/full-at-N−1 statement removed —
  monotonic counters use all slots and are full at N.
- `src/audio_timing_none.c`: nRF54 path corrected to GRTC+TIMER20+GPPI
  PCLK measurement (not LRCK).
- `src/audio_perf.h`: deadline comparison described at cycle-end recording
  (`audio_perf_cycle_end`), not snapshot/print.
- `tests/unit/lifecycle/src/test_lifecycle.c`: renamed to
  `test_close_clears_gate_then_restart_reopens`; removed `l_received`/
  `r_received` claim replaced with the close/reopen behavior actually
  asserted.
- `src/audio_asrc.c`, `src/audio_i2s.c`: "Commit:" history wording
  replaced with current state-update invariants.
- Gate regression found and fixed during the run (`c454a39`): comment
  cleanup had corrupted the shell command registry (`reset - stats` /
  `perf - reset` spaced names); exact `reset-stats` / `perf-reset` names
  restored, incidental clang-format reflow reverted — final diff
  comment-only.
