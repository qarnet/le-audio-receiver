# Workstation transfer status — pre-refactor testing track

Date: 2026-08-06 (T8 ACCEPTED final update; R3–R8 ACCEPTED addenda).

## R8 addendum (2026-08-06)

R8 (FLPR production/diagnostic boundary) is ACCEPTED; see
`docs/development/refactor-r8-results.md` and the handoff
`docs/development/refactor-r8-handoff.md`.  Core FLPR cpuapp files and
the FLPR image contain production runtime only; acceptance machinery is
explicit and configurable (`CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS` cpuapp,
new `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS` FLPR image with
`src/flpr/Kconfig`).  Shared ACK correlation engine
`src/flpr_control_ack.c` (one owner); handshake split production/
diagnostic handler slots; stress + fault-hang moved out of the handshake;
gates 1–6 moved to the acceptance module with byte-identical shell
output.  Build contract 76 → **79** with acceptance parity checks.
Canonical gate **51 PASS / 0 FAIL / 51 TOTAL** on `35bc635` (two new
direct suites: flpr_acceptance 48, flpr_acceptance_flpr 8); coverage
population 30 → **33** (migration `54a6b8e`); BSim pins byte-identical.
Hardware: nRF54L15 Mode A/B 120 s (offload submit==success fallback=0,
faults 0), flpr hang gate Mode A + Mode B 16/16, flpr stall gate PASSED,
`flpr status`/`flpr ring status`/`flpr stress`/`flpr ring test` exercise
the moved handlers.  Evidence: `/tmp/r8-hw/` (MANIFEST + SHA256SUMS).

## R7 addendum (2026-08-05, G3 completed 2026-08-06)

R7 (stream teardown transition owner) is ACCEPTED; see
`docs/development/refactor-r7-results.md`.  One private teardown
transition owner in `bt_bap.c` (`teardown_transition` +
`teardown_close_path`) owns every stop/disable/disabled/release/
disconnect/shell-stop composition; first close wins, per-slot release
once with duplicate-release no-op, ASCS stream objects untouched,
universal close→drain→sink-stop→offload-stop→reset order.  Direct tests
before implementation (lifecycle 28→33, session 29→35); BSim Stage 1 now
17 scenarios with a deliberate new `duplicate_release_10ms` pin
(total=56, two identical baseline runs) and exact strengthened teardown
asserts — all existing pins byte-identical.  Canonical gate **49 PASS /
0 FAIL / 49 TOTAL** on `3473127`, coverage population 30 with zero drift
(no baseline migration), builds 3/3, contract 76/76.  G3: nRF54L15 3/3
clean (Mode A/B fresh + bonded reconnect Mode A 120 s, offload
submit==success fallback=0, faults 0); nRF5340/E83 **4/4 clean** (Mode A
fresh, Mode B fresh, Mode B bonded reconnect 120 s, APLL evidence Drift
ACTIVE ppm −500; zero ISO gap/i2s warnings; Mode B fresh 11660/23320 and
bonded 11665/23330 match the R6 baseline).  A transient dongle/RF
degradation delayed the E83 Mode B rows (~22:30–02:05, 75–90 % CIS
delivery despite reflashes and every safe recovery); differential
diagnosis proved it environmental (teardown-only change, Xiao clean at
65–89 % delivery, Mode A 88–92 %, btmon host TX complete, ~55 recovery
attempts) and the environment recovered — all rows then passed with the
standard ritual (E83 OpenOCD reset + dongle power-cycle + bond cleanup),
no firmware change, no criterion weakened.

## R6 addendum (2026-08-05)

R6 (BAP receive-pipeline decomposition) is ACCEPTED; see
`docs/development/refactor-r6-results.md`.  App audio receive/session
state moved from `bt_bap.c` into `src/audio_stream_session.{c,h}` with an
admission/lease design; `stream_lifecycle_sink_configured()` narrowed to
occupancy; new 29-test direct Twister suite; coverage population migrated
29 → 30; canonical gate **49 PASS / 0 FAIL / 49 TOTAL** on `67d2a18`,
BSim pins byte-identical; G3 hardware PASS on both targets (nRF54L15
Mode A/B/reconnect 120 s + nRF5340/E83 Mode A/B/reconnect 120 s, zero
decode/underrun/reset faults, offload submit==success fallback=0, APLL
ACTIVE).

## R4 addendum (2026-08-05)

R4 (shell and acceptance-harness separation) is ACCEPTED; see
`docs/development/refactor-r4-results.md`.  Shell command ownership split
by subsystem (audio / bt / flpr diagnostics / flpr acceptance) with no
command, output, or return change; `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`
gates the acceptance harness; coverage baseline migrated 26 → 29 with an
exactly-equal mechanical aggregate.  nRF54L15 focused hardware smoke
passed (command paths, hang gate, stall gate, `bt unpair` on hardware).

## R3 addendum (2026-08-04)
Documentation-only state handoff.  No production code, scripts, tests,
baseline, or configs were touched by the edits that accompany this document.

## R3 addendum (2026-08-04)

R3 (test-runner and hardware-gate consolidation) is ACCEPTED; see
`docs/development/refactor-r3-results.md`.  Host tooling/tests only — no
production firmware or coverage-baseline change.  Suite discovery now uses
`scripts/test_inventory.py`; `tests/unit/gate/` retired (tests moved to
`fw_flash_dongle` + `flpr_stall_gate` with a one-to-one ownership map);
FLPR status parsing shared via `scripts/flpr_status.py`; BSim pins moved
unchanged to `tests/bsim/stage1-scenarios.json`; `scripts/monitor.sh` and
`tests/hardware/` retired.  Canonical gate **47 PASS / 0 FAIL / 47 TOTAL**,
builds 3/3, build contract 76/76, BSim pins unchanged.

## Objective

Record the exact repository state of the pre-refactor testing track (T0–T8)
so a fresh executor on this workstation (or a transferred one) can continue
without re-deriving facts: what is accepted, what is implemented-but-unproven,
what evidence is still missing, which files are dirty, and what the next
bounded steps and final commands are.

## Current state (2026-08-04, after the T8 acceptance closeout)

- Branch: `handoff/workstation-transfer`.
- T0–T8 all ACCEPTED.  Exact accepted production code commit: **`971e6a4`**
  (per-CIS ISO sequence-gap concealment); coverage-baseline commit
  `1a5842d`; coverage docs commit `3c29421`; first T8 acceptance closeout
  `5ceb719`; final docs HEAD = the evidence-fix commit.
- Final software gate on the exact final code: **47 PASS / 0 FAIL /
  47 TOTAL** (28 twister + 4 exec + 12 Python + coverage + matrix + BSim);
  exact observed re-run retained: `./scripts/test-all.sh` on
  2026-08-04T05:26:57+02:00 on `thomas-workstation` (worktree clean,
  production tree == `971e6a4`), `Gate complete: 47 PASS / 0 FAIL /
  47 TOTAL`, exit 0, elapsed **1016.45 s** (bash `time` builtin),
  log `/tmp/t8-final-47.log` (transient through review, NOT
  repository-retained).  Coverage baseline accepted at `1a5842d` (26 files:
  lines 3281/3722, branches 1433/2041, functions 205/205); builds 3/3;
  build contract 76/76 (direct run retained: `76 assertions, 0 failed`,
  `BUILD CONTRACT PASSED`, exit 0, `/tmp/t8-final-build-contract.log`
  transient through review); zero actionable warnings.  Full hardware
  evidence: `docs/testing/pre-refactor-hardware-baseline.md` (T8 ACCEPTED).
- Working tree **clean** at the final docs HEAD (evidence-fix commit).
- Base / upstream `main`: `20b37c405835e5c2c747fa7b072c4c0b752b29cd`
  (PR #3 merge, Xiao RF-switch fix).

## Historical state (as of the transfer, preserved verbatim in meaning)

- Historically, this track lived on branch `test/pre-refactor-behavior`
  with HEAD `042290c78027a781eda0caa66ec73c0ccefe42c4` — "docs: record T7
  acceptance evidence, baseline numbers, and gate contracts" (docs-only).
  That branch was **95 commits ahead** of `origin/main` (verified
  `git rev-list --count origin/main..HEAD` at the time).
- The transfer step (commit `98e4920`) moved the track to
  `handoff/workstation-transfer`; `test/pre-refactor-behavior` remains the
  historical source branch, and `042290c` remains its historical HEAD.
  The docs it carried now live on the transfer branch.

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
| T7 — coverage enforcement | **ACCEPTED (2026-08-02)** | `STATUS.md` (T7 section), `docs/testing/coverage-matrix.md`, `docs/development/pre-refactor-testing-t7-stage1-handoff.md`, `-t7-stage2-handoff.md`, `-t7-evidence-fix-handoff.md` |
| T8 — hardware baseline freeze | **ACCEPTED (2026-08-04)** | `docs/testing/pre-refactor-hardware-baseline.md` — both matrices pass on the exact final code `971e6a4` (nRF54L15 Mode A/B 120, bonded reconnect, FLPR hang Mode A 16/16 + Mode B 16/16 earlier, Phase 3 3/3; E83 Mode A 120, Mode B 120 fresh + bonded, Mode B 300; zero underruns/faults; APLL ppm −500; sequence-gap activation documented as an evidence limitation) |

## Key T7 commit chain

Code/tooling (oldest → newest), per `git log` on `8f7bfca`:

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
  matrix into `test-all.sh`.  Default enforcement reran on clean `4a31324`
  with identical ratios.
- `8f7bfca` — test: drop `CONFIG_LOG=n` from the shell unit test suites.
  **Exact accepted T7 code commit.**  Removes the two contradictory
  `CONFIG_LOG=n` lines that the accepted run's review classified as Kconfig
  assigned-value warnings (the shell subsystem forces `LOG=y`); the
  canonical gate on this commit emits zero such warnings.  Test-config-only
  change; baseline coverage identical.
- `042290c` (historical branch HEAD on `test/pre-refactor-behavior`) —
  docs: record T7 acceptance evidence, baseline numbers, and gate
  contracts.  Docs only; introduced the acceptance wording that was later
  qualified because no exact canonical gate run was retained at that time.
- `98e4920` (transfer anchor on `handoff/workstation-transfer`) — docs:
  prepare pre-refactor work for workstation transfer.  Committed the
  qualified T7 status, the provenance corrections, and this document.
- `b342aae` (review-intermediate evidence, superseded) — docs: record the
  first exact canonical gate evidence (41/41 on `4a31324`) and mark T7
  ACCEPTED.  That record classified 2 Kconfig assigned-value warnings as
  accepted; review rejected the classification under the repo hard-warning
  policy, and the warning-fix commit `8f7bfca` plus the canonical re-run on
  it supersede that record (see "Exact canonical gate evidence" below).
- current HEAD (this docs-only update) — record the accepted gate evidence
  on `8f7bfca` with zero Kconfig assigned-value warnings; T7 ACCEPTED,
  T8 NOT STARTED.
- T8 execution (2026-08-02, commits `2988e1c`/`e8dbc1c`/`ace13ff`/
  `1d90873`) — nRF54L15 Stage 2 run to completion with one flagged row;
  nRF5340 Stage 3 not started (hardware absent).  See
  `docs/testing/pre-refactor-hardware-baseline.md`; T8 NOT ACCEPTED.
  *(Historical record of the Aug 2 state — superseded by the T8 ACCEPTED
  closeout below.)*
- T8 closeout (2026-08-03/04, commits `8fd7bb0`/`6578a9c`/`19bec75`/
  `46100a9`/`a40f75e`/`4488f53`/`c056936`/`7c1205b`/`9b78d87`/`ac1fa06`/
  `1a4d27f`/`4ef25b2`/`3df6da8`/`971e6a4`/`1a5842d`/`3c29421`/`5ceb719`)
  — pairing filter, BlueZ preserve-bond fixes, Mode A assembler, teardown
  writer, hang-gate baseline hardening, and per-CIS ISO sequence-gap
  concealment (`971e6a4`, final production code).  Both hardware matrices
  pass; final software gate 47 PASS / 0 FAIL / 47 TOTAL — exact observed
  re-run retained (2026-08-04T05:26:57+02:00, `thomas-workstation`, exit
  0, elapsed 1016.45 s, `/tmp/t8-final-47.log` transient through review);
  coverage baseline accepted at `1a5842d`; builds 3/3; build contract
  76/76 (direct run retained: `76 assertions, 0 failed`, exit 0,
  `/tmp/t8-final-build-contract.log` transient through review).
  **T8 ACCEPTED (2026-08-04)** — see
  `docs/testing/pre-refactor-hardware-baseline.md`.

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

## Exact canonical gate evidence (T7 ACCEPTED, 2026-08-02)

T7 final acceptance is **granted** with exact observed evidence:

- The canonical `test-all.sh` gate was run on the exact accepted commit
  `8f7bfca` from a detached fresh clone on `thomas-workstation` (HEAD ==
  `8f7bfcadde2cfd6446f5493bff7b88c6aa9d5a02`, worktree clean), in the flake
  dev shell (`nix develop`, which provides `gcovr` for the coverage child).
  A `git worktree` cannot host the gate because `test-coverage.sh` requires
  a real `.git` directory (`[ -d .git ]`), so the detached checkout is a
  fresh clone.
- Observed exact result: **`Gate complete: 41 PASS / 0 FAIL / 41 TOTAL`**,
  script exit 0, elapsed **816 s (13m36s)**.  Composition: 25 Twister C + 4
  exec-only C + 9 Python + coverage + matrix + BSim (see "Canonical gate
  composition").  The coverage child enforced the committed baseline on the
  test-config-only commit with zero drift.
- Warning-fix context: the review-intermediate record (commit `b342aae`)
  classified 2 upstream Zephyr Kconfig `LOG` assigned-`n`-got-`y`
  messages from `audio_shell`/`audio_shell_nrf54` (`CONFIG_LOG=n` in those
  suites' `prj.conf` overridden by the shell subsystem's `select
  LOG_OUTPUT`) as accepted; review rejected the classification under the
  repo hard-warning policy.  Commit `8f7bfca` removed the two
  contradictory `CONFIG_LOG=n` lines; the canonical gate on `8f7bfca`
  emits **zero Kconfig assigned-value warnings** (and zero compiler
  warnings).
- Log provenance: full stdout/stderr captured to a transient `/tmp` log
  during the run and removed after evidence extraction; the committed
  evidence (exact line, exit code, runtime, commit, date) in `STATUS.md`
  is the durable record.
- Warnings in the accepted run (all classified): 30 native_sim
  `Using a test - not safe - entropy source` notices (pre-existing
  informational line, every twister suite) and **zero Kconfig
  assigned-value warnings**.  All `<wrn>`/`<err>` lines are deliberate
  failure-injection output of negative-path tests.

## Historical dirty files (committed in `98e4920`, the transfer anchor)

Before the transfer commit, these files were dirty on
`test/pre-refactor-behavior` at `042290c`:

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
  (was untracked) — evidence-fix handoff corrected to 41 children / 41/41
  expected, marked pending.

The transfer commit `98e4920` committed those edits, making the tree clean.
The T7 evidence-fix commit `b342aae` then recorded the first exact accepted
gate evidence (41/41 on `4a31324`) and marked T7 ACCEPTED / T8 NOT
STARTED; that record's classification of 2 Kconfig assigned-value warnings
was superseded by the warning-fix commit `8f7bfca` and the docs-only update
at HEAD, which record the accepted gate evidence on `8f7bfca` with zero
such warnings.

No production code, scripts, tests, or baseline changed; the only config
change in the T7 chain is the warning-only test-config fix in `8f7bfca`
(the two `CONFIG_LOG=n` lines removed from the shell test suites).

## No running task

At the time of writing there is **no running task**: no long test, no
hardware session, no build, no flash, no gate run, no commit/push/merge/PR
in progress.  The workstation repo is clean on `handoff/workstation-transfer`
at the final docs HEAD (evidence-fix commit; T8 ACCEPTED).

## Next bounded steps

1. T7 and T8 are ACCEPTED with exact evidence.  Nothing further for the
   pre-refactor hardware baseline.  The pre-refactor track (T0–T8) is
   complete; the final pre-refactor gate (test-all 47/47, coverage, three
   builds, build contract 76/76) is recorded in
   `docs/testing/pre-refactor-hardware-baseline.md` and the plan's
   "Final pre-refactor gate" section.
2. Refactoring analysis may begin per the plan (duplicate implementations,
   missing abstractions, ownership/state-machine improvements, readability,
   diagnostics).

## Canonical gate composition (47 children)

From `scripts/test-all.sh` (discovery via `scripts/test_inventory.py`, the
single filesystem classification source shared with `test-coverage.sh` and
`check-test-matrix.py`; BSim scenario data from
`tests/bsim/stage1-scenarios.json`):

- **28 Twister C suites** (testcase.yaml): actuator_apll,
  actuator_apll_nohfclk, actuator_none, actuator_sample_adjust_historical,
  app_lifecycle, asrc, audio_i2s, audio_i2s_identity, audio_shell,
  audio_shell_noperf, audio_shell_nrf54, bt_pairing_policy (T8), decode,
  drift, flpr_handshake, flpr_protocol, flpr_ring_mgr, flpr_runtime,
  iso_seq (T8), lifecycle, modea (T8), perf, rate_convert, stats, timing,
  timing_none, timing_nrf54, volume.
- **4 exec-only C suites** (CMakeLists.txt, no testcase.yaml):
  audio_offload, flpr_audio_process, flpr_ring, offload_asrc.
- **12 Python suites**: fw_flash_dongle, flpr_stall_gate, flpr_hang_gate,
  bluez_wireplumber_gate, bluez_wireplumber_phase3_gate, bsim_runner,
  build_contract, hci_raw_connect, bap_central_policy, bap_central_writer,
  test_matrix, test_coverage_runner.
- **1 coverage child**: `test-coverage.sh` default mode (rebuilds the
  twister + exec suites with `CONFIG_COVERAGE=y`, enforces the committed
  baseline at `1a5842d`; requires a clean worktree).
- **1 matrix child**: `check-test-matrix.py --coverage-json` on the
  coverage run's `coverage.json` (zero-hit functions, public API outcome
  ledger, state transitions, witnesses).
- **1 BSim child**: `bsim: stage1` (T4 16-scenario BAP matrix,
  scenarios 1–9 twice, remaining seven once, pinned hashes, strict parse).

28 + 4 + 12 + 1 + 1 + 1 = **47**.

## Final commands (T8 final gate + final pre-refactor gate)

```bash
# Final pre-refactor gate (exact final code 971e6a4 / baseline 1a5842d /
# coverage docs 3c29421 / first closeout 5ceb719; detached clone — a git
# worktree cannot host the gate because test-coverage.sh requires a real
# .git directory; run in the flake dev shell so gcovr is present).
# Recorded ACCEPTED (evidence-fix re-run, 2026-08-04T05:26:57+02:00,
# thomas-workstation, worktree clean, production tree == 971e6a4):
#   Gate complete: 47 PASS / 0 FAIL / 47 TOTAL
#   PASS, exit 0, elapsed 1016.45 s (bash time builtin)
#   log /tmp/t8-final-47.log (transient through review, not repo-retained)
./scripts/test-all.sh          # observed 47 PASS / 0 FAIL / 47 TOTAL
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
# Recorded ACCEPTED (evidence-fix re-run on the existing final builds,
# provenance == 971e6a4; no pristine rebuild needed):
#   76 assertions, 0 failed / BUILD CONTRACT PASSED, exit 0
#   log /tmp/t8-final-build-contract.log (transient through review)
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
(final record: T8 ACCEPTED, 2026-08-04).  User may add audibility evidence,
but measurable automated gates do not depend on it.

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
   worktree.  Require the exact **47 PASS / 0 FAIL / 47 TOTAL** line plus
   elapsed runtime (the final T8 gate; earlier phases used the then-current
   child count — 41 at T7).
7. Return the workstation repo to clean `main`; remove the temporary
   worktree, temporary ref, and bundle artifacts.
8. Verify the final tested commit hash equals the desktop branch HEAD.

**T7 gate note (2026-08-02):** step 6 must use a detached **clone**, not a
`git worktree` — `scripts/test-coverage.sh` requires a real `.git`
directory (`[ -d .git ]`), which a worktree does not have.  The accepted
T7 gate ran from a detached clone at exact `8f7bfca` (in the flake dev
shell, so `gcovr` is on PATH for the coverage child).

## Bundle caveat — historical (resolved by the transfer commit)

Git bundles contain commits only.  The documentation edits that were dirty
at `042290c` (`STATUS.md`, `docs/testing/behavior-contract.md`,
`docs/testing/coverage-matrix.md`, and the then-untracked
`docs/development/pre-refactor-testing-t7-evidence-fix-handoff.md`) were
**not** included in any git bundle until they were committed.  The transfer
commit `98e4920` resolved this: it committed those docs on
`handoff/workstation-transfer`, so the full state (including this document
and the evidence-fix handoff) is now in the commit graph and transferable by
normal clone/bundle.  Nothing in the current state is uncommitted.

## Historical documents

Phase handoffs and results files written at the time
(`pre-refactor-testing-t0-handoff.md` … `-t6-handoff.md`, the T7 stage 1/2
handoffs, `docs/testing/t4-bap-bsim-matrix.md`, and older `STATUS.md` gate
totals such as 21, 23, 25, 26, 30, 36 children) retain their contemporaneous
wording and child counts.  They are historical evidence for their own
commits; only this document, `STATUS.md`, the plan, the coverage matrix, and
the evidence-fix handoff state the CURRENT status.
