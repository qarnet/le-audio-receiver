# Workstation transfer status — pre-refactor testing track

Date: 2026-08-02.  Documentation-only state handoff.  No production code,
scripts, tests, baseline, or configs were touched by the edits that accompany
this document.

## Objective

Record the exact repository state of the pre-refactor testing track (T0–T8)
so a fresh executor on this workstation (or a transferred one) can continue
without re-deriving facts: what is accepted, what is implemented-but-unproven,
what evidence is still missing, which files are dirty, and what the next
bounded steps and final commands are.

## Branch and commit anchors

- Branch: `test/pre-refactor-behavior`.
- HEAD: `042290c78027a781eda0caa66ec73c0ccefe42c4` — "docs: record T7
  acceptance evidence, baseline numbers, and gate contracts" (docs-only).
- Base / upstream `main`: `20b37c405835e5c2c747fa7b072c4c0b752b29cd`
  (PR #3 merge, Xiao RF-switch fix).  Branch is **95 commits ahead** of
  `origin/main` (verified `git rev-list --count origin/main..HEAD`).
- Worktree is dirty with the provenance edits and the T7 evidence-fix handoff
  listed below; nothing is committed beyond `042290c`.

## Phase table

| Phase | Status | Evidence location |
|-------|--------|-------------------|
| T0 — behavior contract + honest coverage map | ACCEPTED (2026-07-31) | `STATUS.md`, `docs/testing/behavior-contract.md`, `docs/testing/coverage-matrix.md`, `docs/testing/v0.0.1-baseline.md` |
| T1 — FLPR production-source tests | ACCEPTED (2026-07-31) | `STATUS.md`, `docs/testing/t1-flpr-production-tests.md` |
| T2 — audio pipeline unit characterization | ACCEPTED (2026-08-01) | `STATUS.md`, `docs/testing/t2-audio-pipeline-tests.md` |
| T3 — I2S state-machine tests | ACCEPTED (2026-08-01) | `STATUS.md`, `docs/testing/t3-audio-i2s-tests.md` |
| T4 — BAP/BabbleSim matrix | ACCEPTED (2026-08-01) | `STATUS.md`, `docs/testing/t4-bap-bsim-matrix.md` |
| T5 — lifecycle, timing, drift, actuators | ACCEPTED (2026-08-01) | `STATUS.md` |
| T6 — boot, shell, resolved-config contracts | ACCEPTED (2026-08-02) | `STATUS.md`, `docs/development/pre-refactor-testing-t6-handoff.md` |
| T7 — coverage enforcement | **IMPLEMENTED, FINAL EVIDENCE PENDING** | `STATUS.md` (T7 section), `docs/testing/coverage-matrix.md`, `docs/development/pre-refactor-testing-t7-stage1-handoff.md`, `-t7-stage2-handoff.md`, `-t7-evidence-fix-handoff.md` |
| T8 — hardware baseline freeze | **NOT STARTED** | `docs/development/pre-refactor-testing-plan.md` (Phase T8) |

## Key T7 commit chain

Code/tooling (oldest → newest), per `git log` on `4a31324`:

- `e3f97d9` — T7 Stage 1: coverage tooling, `timing_none` suite,
  test-matrix manifest/checker.
- `bd51054` — T7 Stage 2: close zero-hit gaps, exact outcome ledger,
  coverage baseline modes.
- `bf10c20` — fix: restore audio shell `reset-stats`/`perf-reset` command names.
- `5ece4d1` — coverage: honor gcovr exclusion markers in checker and numeric
  summary.
- `c6adce8` — test: fix partially-excluded zero-hit fixture (mixed
  excluded/normal lines).  **This is the clean commit on which the baseline
  was generated** (`generated_commit` in `tests/coverage-baseline.json` =
  `c6adce8209c24d67c65acd9813f014714aaca6b3`).
- `4a31324` — gate: commit first honest coverage baseline, wire coverage +
  matrix into `test-all.sh`.  **Exact final T7 code commit.**  Default
  enforcement reran on clean `4a31324` with identical ratios.
- `042290c` (HEAD) — docs: record T7 acceptance evidence, baseline numbers,
  and gate contracts.  Docs only; introduced the acceptance wording now
  being qualified because no exact canonical gate run was retained.

## T7 metrics (committed baseline, never lowered)

`tests/coverage-baseline.json` (schema v1), 23-file numeric population
(all `src/*.c` except `main.c`, `bt_bap.c`, `src/flpr/main.c`,
`audio_clock_actuator_sample_adjust.c`; test-only helper blocks
`GCOVR_EXCL_START`/`GCOVR_EXCL_STOP` excluded):

- lines **3070/3503 (87.6%)**
- branches **1332/1921 (69.3%)**
- functions **182/182 (100.0%)**

Zero zero-hit production functions (checker `--coverage-json` local run
reports zero errors).  Per-file records live in
`docs/testing/coverage-matrix.md` ("T7 numeric baseline").

## Exact pending evidence

T7 final acceptance is **NOT yet granted**.  The blocker:

- The canonical `test-all.sh` gate on the exact T7 commit `4a31324` has
  **41 children**, not 40.  Composition: 25 Twister C + 4 exec-only C +
  9 Python + coverage + matrix + BSim (see "Canonical gate composition").
- **No retained observed exact canonical gate result or runtime is
  documented.**  The T7 section of `STATUS.md` (added by `042290c`)
  asserted "all children pass" without a retained log, elapsed runtime, or
  exact `41 PASS / 0 FAIL / 41 TOTAL` line.  That vague acceptance statement
  is removed/qualified by the current dirty edits.
- Until a retained exact-commit gate run proves **41 PASS / 0 FAIL /
  41 TOTAL** (plus elapsed runtime and log source), T7 stays IMPLEMENTED /
  FINAL EVIDENCE PENDING.  Do not invent or claim 41/41 from script
  structure.

## Current dirty files (uncommitted)

- `STATUS.md` — T7 status qualified (IMPLEMENTED / FINAL EVIDENCE PENDING),
  provenance correction, date/summary update, T8 NOT STARTED marker, link to
  this document.
- `docs/testing/behavior-contract.md` — CV-001 provenance correction
  (baseline generated on clean `c6adce8`, committed in `4a31324`,
  enforcement rerun identical).
- `docs/testing/coverage-matrix.md` — suite inventory corrected to
  41 children (25/4/9/coverage/matrix/BSim), baseline provenance corrected,
  explicit no-exact-gate-acceptance note.
- `docs/development/pre-refactor-testing-t7-evidence-fix-handoff.md`
  (untracked) — evidence-fix handoff corrected to 41 children / 41/41
  expected, marked pending.

No production code, scripts, tests, baseline, or configs changed.

## No running task

At the time of writing there is **no running task**: no long test, no
hardware session, no build, no flash, no gate run, no commit/push/merge/PR
in progress.  The workstation repo is idle; the desktop repo is dirty only
with the documentation edits above.

## Next bounded steps

1. Finish the provenance corrections in `docs/testing/behavior-contract.md`
   (done in working tree) and `docs/testing/coverage-matrix.md` (add the
   no-exact-gate-acceptance note; done in working tree).
2. Correct `docs/development/pre-refactor-testing-t7-evidence-fix-handoff.md`
   to 41 children / expected `41 PASS / 0 FAIL / 41 TOTAL`; state task
   pending (done in working tree).
3. Update `STATUS.md` top summary/date and the T7 section; mark T8 NOT
   STARTED; link this document (done in working tree).
4. Update `docs/development/pre-refactor-testing-plan.md` — T7
   IMPLEMENTED/FINAL EVIDENCE PENDING, T8 NOT STARTED (done in working
   tree).
5. After the executor commits these docs-only edits: rerun
   `./scripts/test-all.sh` on a detached workstation worktree of the exact
   T7 code commit `4a31324`, capture the complete log and elapsed runtime,
   and verify the exact **41 PASS / 0 FAIL / 41 TOTAL** line.  Only then
   record T7 ACCEPTED.  Do not alter workstation `main`; remove the
   temporary worktree/ref/bundle after extracting evidence.
6. Then start T8 (hardware baseline freeze; matrix below).

## Canonical gate composition (41 children)

From `scripts/test-all.sh` (header + run order):

- **25 Twister C suites** (testcase.yaml): actuator_apll,
  actuator_apll_nohfclk, actuator_none, actuator_sample_adjust_historical,
  app_lifecycle, asrc, audio_i2s, audio_i2s_identity, audio_shell,
  audio_shell_noperf, audio_shell_nrf54, decode, drift, flpr_handshake,
  flpr_protocol, flpr_ring_mgr, flpr_runtime, lifecycle, perf,
  rate_convert, stats, timing, timing_none, timing_nrf54, volume.
- **4 exec-only C suites** (CMakeLists.txt, no testcase.yaml):
  audio_offload, flpr_audio_process, flpr_ring, offload_asrc.
- **9 Python suites**: gate, flpr_stall_gate, flpr_hang_gate,
  bluez_wp_gate, bluez_wp_phase3_gate, bsim_runner, build_contract,
  test_matrix, test_coverage_runner.
- **1 coverage child**: `test-coverage.sh` default mode (rebuilds the 25
  twister + 4 exec suites with `CONFIG_COVERAGE=y`, enforces the committed
  baseline; requires a clean worktree).
- **1 matrix child**: `check-test-matrix.py --coverage-json` on the
  coverage run's `coverage.json` (zero-hit functions, public API outcome
  ledger, state transitions, witnesses).
- **1 BSim child**: `bsim: stage1` (T4 15-scenario BAP matrix,
  scenarios 1–8 twice, pinned hashes, strict parse).

25 + 4 + 9 + 1 + 1 + 1 = **41**.

## Final commands (T7 gate + T8 preconditions)

```bash
# Canonical full gate (exact T7 commit 4a31324, detached workstation worktree)
./scripts/test-all.sh          # expect 41 PASS / 0 FAIL / 41 TOTAL
./scripts/test-coverage.sh     # default mode: baseline enforcement
# matrix checker consumes the coverage run's coverage.json; inside
# test-all.sh the coverage child writes it under its $TMP_ROOT/coverage
python3 scripts/check-test-matrix.py --repo-root "$PWD" \
  --coverage-json "$TMP_ROOT/coverage/coverage.json"
# Production builds (separate from test-all.sh by design)
fw-build-5340
fw-build-54l15
fw-build-dongle
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
```

## T8 hardware matrix (from the plan)

From `docs/development/pre-refactor-testing-plan.md`, Phase T8.  Only the
autonomous nRF5340DK `hci_uart` central and repository scripts are allowed;
capture console before reset; resolve probes with `nrf-probes`; never record
a static probe mapping.

nRF54L15:

- Fresh build and flash.
- Mode A 120 seconds.
- Mode B 120 seconds.
- Disconnect/reconnect and repeat.
- Forced timed FLPR stall proving cpuapp fallback, runtime restart,
  probation, and FLPR reactivation.
- Stock BlueZ/WirePlumber clean pair, 120-second playback, reconnect
  playback, receiver reset, bonded reconnect, and third playback.
- Zero warnings, assertions, decode errors, underruns, ASRC capacity
  failures, and integrity faults.

nRF5340:

- Fresh dual-core build and flash.
- Mode A 120 seconds.
- Mode B 120 seconds.
- Reconnect stream.
- APLL active and repeat fallback zero in steady state.
- Zero warnings, assertions, and faults.

Record commands, commits, hashes, counters, logs, durations, and raw probe
identity evidence in `docs/testing/pre-refactor-hardware-baseline.md`
(not yet created — T8 not started).  User may add audibility evidence, but
measurable automated gates do not depend on it.

## Hardware safety / probe / central / serial rules

- **Probe identity**: resolve probes at flash time with `nrf-probes`
  (fingerprints over SWD; works even when APPROTECT-locked).  Never assume
  or document a static serial↔board mapping; `scripts/probe-serial.local`
  is a manual override only.  Hardware-identity claims must carry raw
  evidence (DPIDR, AP IDR map, FICR PART).
- **Flashing**: openocd-master only (`fw-flash-5340`, `fw-flash-54l15`).
  Never probe-rs (reset-catches the nRF5340 core before the APPROTECT
  soft-unlock, then mass-erases UICR).  nRF5340 APPROTECT is a soft branch:
  an erased UICR bricks debug access; `flash_nrf5340.tcl` reprograms
  `UICR.APPROTECT`/`SECUREAPPROTECT` after every flash — do not remove those
  calls.  Mass erase only via openocd `nrf53_recover` (nRF5340 only; the
  nRF54L15 has NO recovery path in current tooling).
- **Central-only test rule**: all autonomous stream tests use the nRF5340DK
  `hci_uart` central attached as `hci0` via `btattach -B /dev/ttyACM2
  -S 1000000`; run `btmgmt power off/on`, `io-cap 3`, `sc on` before every
  session.  Dongle BD_ADDR is compile-time `C0:AA:BB:CC:DD:EE`.  Use
  `scripts/bap_central.py` (no sudo for the main script; `--peer-addr` skips
  discovery when scanning fails).  No human-operated central.
- **Serial consoles**: E83 (nRF5340 app) = `/dev/ttyUSB0` @ 115200 8N1;
  Xiao (nRF54L15) = `/dev/ttyACM0` @ 115200 (SAMD11 USB CDC).  Capture boot
  logs BEFORE reset — start `scripts/read_acm.py` (auto-reopen) first, then
  reset via OpenOCD.  Use serial-mcp, not `stty`/`cat`, for the receiver
  console.  `printk`/`LOG` interleaving is solved by `CONFIG_LOG_PRINTK=y`.
- **Stale bonds**: `west flash` does not erase the settings partition; a
  stale bond blocks PACS/ASCS reads.  Fix by mass erase (`nrf53_recover`)
  or deleting the bond on the central.  Never skip `settings_load()`.

## Workstation detached-worktree / bundle workflow

Per `docs/development/pre-refactor-testing-t0-review-fix-handoff.md`
(the established transfer procedure):

1. Commit the scoped changes on the desktop branch.
2. Create a git bundle containing branch `test/pre-refactor-behavior`.
3. Copy the bundle to `thomas-workstation:/tmp/`.
4. In the workstation repo, fetch the bundle into a temporary validation
   ref.
5. Add a detached temporary worktree from that exact ref under `/tmp`.
6. Run `./scripts/test-all.sh` in the correct development shell from that
   worktree.  Require the exact **41 PASS / 0 FAIL / 41 TOTAL** line plus
   elapsed runtime.
7. Return the workstation repo to clean `main`; remove the temporary
   worktree, temporary ref, and bundle artifacts.
8. Verify the final tested commit hash equals the desktop branch HEAD.

## Bundle caveat — uncommitted docs are NOT in the bundle

Git bundles contain commits only.  The current dirty documentation edits
(`STATUS.md`, `docs/testing/behavior-contract.md`,
`docs/testing/coverage-matrix.md`, and the untracked
`docs/development/pre-refactor-testing-t7-evidence-fix-handoff.md`) are
**not included in any git bundle until they are committed**.  Any transfer
of this exact state must either commit the docs first or carry the dirty
files outside the bundle.  Nothing in this state handoff may be treated as
transferred until that is resolved.

## Historical documents

Phase handoffs and results files written at the time
(`pre-refactor-testing-t0-handoff.md` … `-t6-handoff.md`, the T7 stage 1/2
handoffs, `docs/testing/t4-bap-bsim-matrix.md`, and older `STATUS.md` gate
totals such as 21, 23, 25, 26, 30, 36 children) retain their contemporaneous
wording and child counts.  They are historical evidence for their own
commits; only this document, `STATUS.md`, the plan, the coverage matrix, and
the evidence-fix handoff state the CURRENT status.
