# Current-state refactoring plan

Status: **accepted plan of record, 2026-08-04**.  Ready for phased execution.
This plan replaces any refactoring assumptions made before the T0–T8
behavior-lock track.  It does not replace `docs/design.md` as historical
architecture/evidence; Phase R0 reconciles that document with current truth.

## Goal

Reduce coupling, duplicated state transitions, dead interfaces, embedded test
harnesses, and monolithic host tooling without changing accepted receiver
behavior.  Refactoring must make ownership and lifecycle boundaries clearer
while preserving the T8 hardware baseline and the exact public contracts in
`docs/testing/behavior-contract.md`.

## Accepted baseline

- Branch at plan creation: `handoff/workstation-transfer`.
- Current docs HEAD: `5fd1eaa`.
- Exact accepted production code: `971e6a4`.
- Coverage baseline: `1a5842d`.
- T8 hardware baseline: `docs/testing/pre-refactor-hardware-baseline.md`.
- Canonical software gate: **47 PASS / 0 FAIL / 47 TOTAL**, exit 0,
  1016.45 s on `thomas-workstation`.
- Coverage population: 26 production files, lines 3281/3722 (88.2%),
  branches 1433/2041 (70.2%), functions 205/205 (100%).
- Production builds: nRF5340, nRF54L15, central dongle all accepted.
- Resolved build contract: 76/76.
- Hardware: nRF54L15 and nRF5340/E83 Mode A/Mode B/reconnect accepted;
  nRF54L15 FLPR fault recovery accepted; bonded-only connection filtering
  accepted.

No phase may use `origin/main` as its behavioral baseline.  The anchors above
are authoritative until the first refactor phase lands; every later phase uses
the previous accepted refactor commit.

## Current architecture after T8

### Production pipeline

```text
BT RX WQ
  bt_bap.c: ASCS/PACS/connection callbacks + stream receive orchestration
    ├─ bt_pairing_policy.c: OPEN/BONDED_ONLY policy
    ├─ audio_iso_seq.c: omitted-callback sequence-gap detection
    ├─ audio_modea.c: two-CIS event assembly and per-channel PLC
    ├─ audio_decode.c: mono/Mode B LC3 decode and channel routing
    ├─ audio_volume.c
    └─ audio_sink_push()
         audio_i2s.c: slab/DMA/start/stop/drift/resampler selection
           ├─ audio_drift.c + platform actuator
           ├─ audio_offload.c → FLPR handshake/ring/runtime stack
           └─ audio_asrc.c cpuapp transactional fallback
```

### Existing good boundaries to preserve

- `app_lifecycle.c`: pure fatal boot coordinator; `main.c` remains wiring.
- `audio_decode.c`: codec/decode behavior independent from BAP lifecycle.
- `audio_modea.c`: bounded compressed-half assembler; no `net_buf` ownership.
- `audio_iso_seq.c`: pure sequence tracker with bounded concealment.
- `bt_pairing_policy.c`: pure policy snapshot; HCI work remains in `bt_bap.c`.
- `stream_lifecycle.c`: pure configured/started/audio-path gate state.
- `audio_asrc.c`: cpuapp/FLPR-shared algorithm and transactional state format.
- `flpr_ring.c`: protocol layout and SPSC ownership independent from manager.
- Test-only seams remain compile-gated and excluded from production coverage.

### Current pressure points

| Area | Current state | Refactor target |
|---|---|---|
| `bt_bap.c` | ~1,550 lines; services, pairing, slot ownership, decode orchestration, teardown | Small BAP front end plus receive-pipeline and stream-session owners |
| `audio_offload.c` | ~1,590 lines; `audio_offload_process_asrc()` ~590 lines with repeated fault epilogues | Explicit offload transaction stages and one fault finalizer |
| `flpr_ring_mgr.c` | ~1,380 lines; production ring manager plus acceptance/test harness | Production manager separated from diagnostics/test commands |
| `audio_shell.c` | ~1,180 lines; audio, BT, FLPR commands and acceptance harness | Command groups split by subsystem; acceptance harness config-gated |
| `bap_central.py` | ~1,770 lines despite policy/writer helpers | Explicit connect, security, BAP configure, stream, teardown components |
| Stream teardown | disable, stop, release, disconnect compose overlapping operations | One owner with explicit per-slot/all-slot transitions |
| Test runner | suite inventories duplicated and current docs already stale | Machine-readable inventory/discovery as one source of truth |

## Contracts and invariants

Every phase must preserve all current `BT-*`, `CODEC-*`, `STAT-*`, `LIFE-*`,
`I2S-*`, `CLOCK-*`, `OFFLOAD-*`, `APP-*`, `BUILD-*`, and `CV-*` contracts.
Phase R0 fixes stale numbering/text but does not weaken a contract.

Load-bearing invariants include:

1. nRF5340 remains reference target: APLL + identity path, SW Split dual-core.
2. nRF54L15 remains NONE actuator + ASRC, FLPR when healthy, cpuapp fallback
   from unchanged pre-state on every offload fault.
3. 48 kHz only; 7.5 ms and 10 ms; at most two sink channels; one frame block
   per SDU; source ASEs rejected.
4. Mode A uses timestamp/event assembly with PLC for missing halves; Mode B
   uses two independent decoders; per-CIS omitted callbacks use sequence-gap
   concealment.
5. The 4 KiB BT RX WQ must never receive decoder work-memory locals.  Keep the
   scalar `codec_shape` pattern.
6. `settings_load()` remains after `bt_enable()` and before PACS registration.
7. BONDED_ONLY advertising rebuilds controller FAL from persisted bonds;
   pairing reset uses public `bt_bap_pairing_reset()`.
8. I2S blocks have distinct ownership.  `-EIO` recovery uses PREPARE; stop uses
   PREPARE then DROP; `configured` survives stream stop.
9. Drift update happens once per rendered block in thread/work context, never
   ISR.  SDC/MPSL RADIO ownership remains untouched.
10. FLPR IPC/rings retain epoch checks, transactional ASRC state transfer,
    bounded recovery attempts, 100-success probation, and cpuapp fallback.
11. Warning policy remains strict.  No compiler/Kconfig/boot/OpenOCD warning is
    normalized.
12. Probe identity remains runtime-resolved by `nrf-probes`; no static mapping.

## Scope

### In scope

- Documentation and gate truth consolidation.
- Ownership/concurrency corrections required before moving code.
- Dead production API and retired-source cleanup.
- Test-runner and hardware-gate duplication removal.
- Splitting monolithic files along already-proven behavior boundaries.
- Consolidating repeated error/fault and teardown transitions.
- Moving acceptance-only code out of normal production modules or behind an
  explicit diagnostic Kconfig.
- Updating tests/manifests/coverage baselines mechanically when files split.

### Out of scope

- New codecs, rates, channels, source ASEs, CAP/CAS/TMAS/CSIS, A2DP, phones.
- CI revival, new boards, custom PCB, physical pairing-button GPIO.
- Audio-quality changes, drift gain/threshold retuning, slab-size changes.
- New FLPR protocol/ABI, new offload deadline, HPF, raw VEVIF replacement.
- Broad BabbleSim scenario expansion beyond scenarios needed to preserve an
  already-supported behavior during a touched phase.
- Analog fidelity/audibility claims.
- nRF54L15 APPROTECT recovery.

## Known behavior questions kept outside refactoring

These must be made visible in R0 but not silently changed during structural
work:

1. **7.5 ms nRF54L15 FLPR limitation.** `audio_i2s.c` passes dynamic 360/480
   input frames, but `audio_offload_process_asrc()` and FLPR processing require
   `FLPR_RING_PAYLOAD_MAX_INPUT == 480`.  A 360-frame call returns `-EINVAL`
   and uses cpuapp ASRC.  R0 must add truthful status/test evidence.  Supporting
   360-frame FLPR offload is a separate behavior feature after refactoring.
2. **Emergency repeat fallback.** It stays unchanged and zero in accepted
   steady-state hardware runs.  Crossfade smoothing remains backlog.
3. **Controller/tool limitations.** Sequence-gap hardware activation remains
   non-deterministic; direct production-module tests remain primary proof.

## Gate levels

### G0 — focused phase gate

Tests for files/behavior touched by the phase, plus `git diff --check` and
warning scan.

### G1 — canonical software/build gate

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

Current expected child count is 47.  R0 may change runner organization but not
the set of executed suites without explicit evidence.

### G2 — short autonomous hardware smoke

- nRF54L15 Mode A 30 s + Mode B 30 s.
- nRF5340/E83 Mode A 30 s + Mode B 30 s.
- Capture console before flash/reset; autonomous nRF5340DK central only.
- Zero warnings, assertions, decode errors, I2S underruns, stream resets,
  integrity faults, or unexplained fallback.

### G3 — affected full hardware acceptance

Use exact rows from `docs/testing/pre-refactor-hardware-baseline.md` relevant to
the changed subsystem.  For desktop lifecycle, preserve exact command,
counters, and pass criteria in its “Phase 3 lifecycle full” row.  Accepted
closure and full evidence live in
`docs/development/bluez-wireplumber-interoperability-plan.md` and
`docs/development/phase3-results.md`:

- BAP/pairing/lifecycle: both targets Mode A/B 120 s, bonded reconnect, and
  nRF54L15 Phase-3 BlueZ/WirePlumber full lifecycle.
- I2S/decode/receive: both targets Mode A/B 120 s; APLL/repeat checks on E83.
- FLPR/offload: nRF54L15 Mode A/B 120 s plus FLPR hang Mode A and Mode B.
- Final phase: complete both T8 matrices.

## Coverage migration rule for source splits or deletions

`test-coverage.sh` hard-fails population drift and enforces per-file ratios, so
source splitting or covered dead-code deletion needs a controlled migration
rather than an unexplained baseline rewrite:

1. Commit code/tests with the old baseline still present.
2. Generate candidate coverage on the clean code commit.
3. For every split source, compare aggregate covered/total lines, branches,
   and functions of its replacement files against the old file.  Aggregate
   ratios may not decrease; zero-hit functions remain forbidden.
4. For every dead-function/API deletion, record deleted symbols and production
   caller proof, compare the surviving file's ratios against its old record,
   and explain each line/branch/function denominator change.  Covered live
   behavior may not be deleted to improve a percentage.  If a ratio decreases,
   add tests for surviving behavior before updating the baseline.
5. All unchanged files must remain greater than or equal to their old records.
6. Moving or deleting an excluded historical source still requires synchronized
   updates to `tests/test-matrix.json`, exclusion metadata in
   `tests/coverage-baseline.json`, `docs/testing/coverage-matrix.md`, and its
   historical test CMake.  Record this provenance change even though it does
   not alter numeric aggregate coverage.
7. Commit the new baseline separately with exact generated commit and aggregate
   migration table in `docs/testing/coverage-matrix.md`.
8. Run canonical enforcement again on the baseline commit.

BSim hashes and exact outcome counts must remain unchanged during
behavior-preserving phases.  Any change stops the phase until explained by a
contract correction; never re-pin unexplained output.

---

# Phased plan

## R0 — Canonical truth and gate inventory — **ACCEPTED (2026-08-04)**

### Goal

Make repository documentation and gate metadata describe current T8 state
before structural edits begin.

### Results

Exact corrected implementation commit **`53d42db`** (commit chain
`2de5e33` → `7ab4b39` → `d7b6873` → `e462aba` → `1ee8af7` → `85bbcf3` →
`53d42db`; `53d42db` includes the last focused review-fix corrections);
full G1 passed on the clean `53d42db` (47 PASS / 0 FAIL / 47 TOTAL,
coverage exact against `1a5842d`, builds 3/3, build contract 76/76, zero
actionable warnings, BSim hashes unchanged).  Full results, commands,
runtimes, composition, and warning disposition:
`docs/development/refactor-r0-results.md`.

### Files

- Maintain and link this accepted plan of record; keep the plan current as
  phases land.
- Update `AGENTS.md`, `README.md`, `docs/design.md`, `STATUS.md`.
- Update `docs/testing/behavior-contract.md` and
  `docs/testing/coverage-matrix.md`.
- Update `scripts/test-all.sh` comments/inventory.
- Update `docs/development/pre-refactor-testing-plan.md` status only; preserve
  historical phase body.
- Update stale hardware-acceptance paths in `tests/test-matrix.json`.

### Work

1. Rename the desktop compatibility track in prose to **BZ1–BZ4**; keep audio
   architecture phases 0–6 unchanged.  Remove ambiguous duplicate “Phase 4”.
2. Mark T0–T8 COMPLETE and link this plan from AGENTS/design/README/STATUS.
3. Add current modules (`app_lifecycle`, `audio_modea`, `audio_iso_seq`,
   `bt_pairing_policy`) to architecture/key-file tables.
4. Fix current suite inventory to 28 Twister, 4 exec-only, 12 Python,
   coverage, matrix, BSim = 47 children.
5. Refresh behavior-contract version, remove duplicate `CODEC-013`, and update
   CV-001 to the 26-file `1a5842d` baseline.
6. Replace manifest references to dead `scripts/monitor.sh` with current
   `scripts/read_acm.py`/hardware-baseline evidence.  Mark official BSim smoke
   PARTIAL honestly; do not use it as production acceptance.
7. Record the 7.5 ms offload limitation as a known behavior question with a
   direct test/status witness; do not implement 360-frame offload here.
8. Enforce or explicitly pin recorded gcovr/gcov versions; baseline metadata
   must not be decorative.

### Non-scope

No production behavior or source reorganization.  Do not archive historical
handoffs in this phase; link cleanup can happen after the plan is accepted.

### Verification

- Focused checker/Python suites.
- Verify machine-derived suite count equals 47.
- G1.  No hardware run unless build output changes unexpectedly.

### Exit

One current plan, one current gate inventory, no contradictory active contract
IDs/counts/baseline numbers, and all known behavior limitations visible.

## R1 — Ownership and concurrency hardening — **ACCEPTED (2026-08-04)**

### Goal

Fix real cross-context ownership ambiguities before moving code.

### Results

Exact implementation commits **`bcd623b`** (tests + source) and **`4f426f3`**
(contracts/metadata), starting from clean `f654b58`; full G1 passed on the
clean `4f426f3` (47 PASS / 0 FAIL / 47 TOTAL, coverage enforcement exit 0
with population 26 and no ratio regression, builds 3/3, build contract
76/76, zero actionable warnings, BSim Stage 1 hashes/counts unchanged);
autonomous G2 passed on both targets (nRF54L15 Mode A/B and nRF5340/E83
Mode A/B, FLPR offloaded/ACTIVE and APLL stable, zero decode errors,
underruns, resets, faults, or warnings).  Full results, commands, runtimes,
probe evidence, and raw log paths:
`docs/development/refactor-r1-results.md`.

### Files

- `src/audio_i2s.c`, `src/audio_sink.h`, `src/audio_shell.c`,
  `src/bt_bap.c/.h`.
- `src/flpr_ring_mgr.c`, `src/flpr_handshake.c`,
  `src/audio_timing_nrf54.c` comments or narrow synchronization.
- `tests/unit/audio_i2s`, `tests/unit/audio_i2s_identity`,
  `tests/unit/audio_shell`, `tests/unit/audio_shell_nrf54`,
  `tests/unit/audio_shell_noperf`, `tests/unit/lifecycle`, and
  `tests/test-matrix.json`.

### Work

1. Add explicit per-stream accept/close state to audio sink.  Proposed
   `audio_sink_stream_open()` enables pushes;
   `audio_sink_stream_close()` atomically rejects new pushes without blocking;
   `audio_sink_stop()` calls close idempotently, waits for already-admitted
   pushes to drain, then resets/DROPs I2S and returns.  Use a short state lock,
   in-flight count, and completion semaphore; release lock before allocation,
   offload, or I2S calls.  Stop completes after admitted pushes drain and before
   DROP; a push admitted before close may finish, one beginning after close is
   rejected.
   **Primitive refinement (R1, evidence-backed):** the decided synchronization
   is one short `k_mutex` plus `k_condvar` in `audio_i2s.c` — not a spinlock
   plus single semaphore — because multiple overlapping stop callers and an
   open waiter need broadcast wakeup, and the resolved receiver configuration
   (`CONFIG_BT_RECV_WORKQ_BT=y`) plus NCS v3.3.0 host source confirm all
   relevant application callbacks run in thread context where `k_mutex`/
   `k_condvar` are legal.  The mutex is held only around state changes; never
   across allocation, decode, offload, I2S, or Bluetooth calls.  Stop never
   times out and proceeds to DROP against a still-running push.
2. Add proposed `bt_bap_audio_path_stop()` in `src/bt_bap.c/.h` and route shell
   `audio stop` through it.  It closes `stream_lifecycle` gate, calls
   nonblocking `audio_sink_stream_close()`, calls `audio_sink_stop()` to drain
   every admitted push, then stops offload.  This order prevents offload reset
   racing an admitted `audio_sink_push()`.  Reopen occurs only through next
   valid BAP gate closed→open transition, which calls
   `audio_sink_stream_open()`; direct sink stop must not leave BAP gate open.
3. Preserve push fast-path timing and exact I2S ownership.  Add concurrent
   stop/push tests proving post-close rejection, admitted-push drain before
   stop return, reconnect reopen, and no slab leak/double free.
4. Document intentional semaphore/generation ordering in ring reset ACK,
   handshake diagnostics, timing ISR/reset, and ring epoch reads.  Add locking
   only where an actual torn/unsafe state exists.
5. Audit boot-lifetime diagnostics such as the function-local `gate_blocked`
   log throttle.  Leave it boot-scoped unless a named behavior contract
   requires per-session logs; never reset or alter separate BSim observer
   counters.

### Verification

- `audio_i2s`, `audio_i2s_identity`, `audio_shell`, timing/FLPR focused suites.
- Threaded race tests under native_sim/UBSan where supported.
- G1 + G2.

### Exit

Every cross-thread mutable field has one documented owner or synchronization
primitive; no lock spans blocking offload/I2S operations.

## R2 — Dead API and historical-source cleanup

### Goal

Remove interfaces with no production callers and move retired implementation
out of production API surface.

### Candidates confirmed at plan creation

- `audio_clock_actuator_consume_sample_adjustment()` — production has no caller;
  current tests are its only users.
- `flpr_ring_mgr_set_consume_cb()` — documented no-op; tests/mocks only.
- `audio_rate_converter_nearest_stereo()` — tests only; production uses
  `audio_rate_converter_next_frames()`.
- `audio_offload_is_stopped()` — tests only; no production caller.
- `DEFAULT_VOL` macro — unused.
- `audio_clock_actuator_sample_adjust.c` — historical and not linked into
  production CMake.
- Stale comments and duplicate banners in NONE actuator/ASRC/offload headers.

### Files

- `src/audio_clock_actuator.h`, `src/audio_clock_actuator_apll.c`,
  `src/audio_clock_actuator_none.c`,
  `src/audio_clock_actuator_sample_adjust.c`.
- `src/flpr_ring_mgr.c/.h`, `src/audio_rate_convert.c/.h`,
  `src/audio_offload.c/.h`, `src/audio_volume.c`.
- `tests/unit/actuator_apll`, `tests/unit/actuator_apll_nohfclk`,
  `tests/unit/actuator_none`, `tests/unit/actuator_sample_adjust_historical`,
  `tests/unit/flpr_ring_mgr`, `tests/unit/rate_convert`,
  `tests/unit/audio_offload`, and `tests/unit/offload_asrc`.
- `tests/test-matrix.json`, `tests/coverage-baseline.json`, and
  `docs/testing/coverage-matrix.md`.

### Work

1. Reconfirm callers on phase HEAD before deletion.
2. Delete unused public declarations/implementations and their copied mocks,
   outcome-ledger entries, and tests whose only purpose was preserving dead API.
3. Move sample-adjust historical implementation under its historical test or
   retain it in `src/` only if build tooling requires it; it must not appear as
   production architecture.
4. Correct specific stale `audio_offload.h` claims that a negative FLPR
   processing status is returned as success and that every nonzero submit is
   counted identically.  Preserve its correct submit-mutex contract.
5. Delete empty/stale diagnostics directories and artifacts only when no active
   evidence link uses them.
6. Apply the split/deletion coverage migration rule, including excluded-source
   provenance if `src/audio_clock_actuator_sample_adjust.c` moves or is deleted.

### Verification

- Caller search shows zero stale references.
- Matrix checker proves all remaining public APIs have exact outcomes.
- G1.  No hardware run expected because no production call path changes.

### Exit

Public headers describe callable production behavior only; historical code is
clearly outside production.

## R3 — Test-runner and hardware-gate consolidation

### Goal

Make gate composition discoverable and remove duplicated parser/test ownership.

### Files

- `scripts/test-all.sh`, `scripts/test-coverage.sh`,
  `scripts/check-test-matrix.py`.
- New machine-readable suite inventory only if discovery cannot express all
  categories.
- `scripts/flpr_hang_gate.py`, `scripts/flpr_stall_gate.py`,
  `tests/unit/flpr_hang_gate`, `tests/unit/flpr_stall_gate`, and obsolete
  `tests/unit/gate`.
- `scripts/bluez-wireplumber-gate.py`,
  `scripts/bluez-wireplumber-phase3-gate.py`,
  `scripts/test_bluez_wireplumber_gate.py`, and
  `scripts/test_bluez_wireplumber_phase3_gate.py`.
- `scripts/bsim-stage1-run.sh`, `scripts/bsim_stage1_parse.py`, and
  `tests/unit/bsim_runner`.

### Work

1. Replace four copies of exec-only suite inventory with one discovery rule or
   manifest consumed by gate, coverage, and checker.
2. Discover Python suites deterministically; make matrix checker verify each is
   wired into the canonical gate.
3. Merge obsolete `tests/unit/gate/test_gate.py` ownership into the current
   FLPR stall suite.
4. Extract one shared FLPR status parser/result schema for hang and stall gates.
   Keep command-specific state machines separate.
5. Give hang gate a fakeable transport boundary equivalent to stall gate.
6. Keep `scripts/test_bluez_wireplumber_gate.py`: it independently covers base
   discovery, parsing, path, and duration behavior not exercised by BZ3 tests.
   Keep `bluez-wireplumber-gate.py` as base implementation imported by
   `bluez-wireplumber-phase3-gate.py`.  Remove a BZ2 suite only after a
   one-to-one ownership map proves every case migrated, not merely because BZ3
   imports the module.
7. Move BSim expected hashes/counts from shell tables into one versioned data
   file consumed by runner and parser.  Values remain unchanged.
8. Retire dead `monitor.sh`, empty hardware test directories, and stale backup
   artifacts from active documentation.

### Verification

- Focused runner/parser fixture tests, including missing/duplicate suite cases.
- Canonical gate executes same suite set and still reports 47 children unless
  deliberate duplicate-suite removal changes count; if count changes, provide
  one-to-one test ownership map proving no behavior coverage was lost.
- G1.

### Exit

One source of truth per suite/parser/hash set; adding a suite cannot silently
omit it from gate or coverage.

## R4 — Shell and acceptance-harness separation

### Goal

Split user diagnostics from FLPR acceptance machinery without changing command
names or output consumed by gates.

### Target shape

- `audio_shell.c`: `audio status`, `audio perf`, stop/reset commands.
- `bt_shell.c` or pairing command file: `bt unpair` wrapper around
  `bt_bap_pairing_reset()`.
- `flpr_shell.c`: FLPR status/runtime/restart commands.
- `flpr_acceptance_shell.c`: ring/stall/acceptance commands, enabled by proposed
  `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`.
- Shared formatting helpers only where exact output remains byte-compatible.

### Files

- `src/audio_shell.c`, proposed `src/bt_shell.c`, proposed
  `src/flpr_shell.c`, proposed `src/flpr_acceptance_shell.c`.
- `src/bt_bap.c/.h`, `src/flpr_ring_mgr.c/.h`, `src/flpr_handshake.c/.h`.
- Root `Kconfig`, root `CMakeLists.txt`, and
  `boards/nrf54l15dk_nrf54l15_cpuapp.conf`.
- `tests/unit/audio_shell`, `tests/unit/audio_shell_nrf54`,
  `tests/unit/audio_shell_noperf`, `tests/unit/flpr_handshake`,
  `tests/unit/flpr_ring_mgr`, `tests/unit/flpr_hang_gate`, and
  `tests/unit/flpr_stall_gate`.

### Work

1. Move code mechanically first; do not rename commands or parseable labels.
2. Move test wrappers next to owning command group or replace them with public
   shell execution tests.
3. Add proposed cpuapp `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS` in root `Kconfig`,
   depending on `SOC_NRF54L15 && SHELL`, wire it in root `CMakeLists.txt`, and
   enable it in `boards/nrf54l15dk_nrf54l15_cpuapp.conf`.  nRF5340 has no FLPR
   acceptance commands and leaves it disabled.  FLPR-image fault-handler
   gating is separate `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS` work in R8.
4. Move ring-manager acceptance buffers/state out of generic audio shell.

### Verification

- Existing audio shell/noperf/nRF54 suites against production files.
- Hardware-gate parser fixtures compare exact output before/after.
- Coverage split migration rule.
- G1 + nRF54 FLPR status/fault focused hardware smoke.

### Exit

Each command group has one owner; hardware scripts consume unchanged output;
normal audio diagnostics no longer compile the acceptance harness by accident.

## R5 — Offload transaction decomposition

### Goal

Make the FLPR ASRC request path auditable without changing one counter,
transition, deadline, or fallback outcome.

### Target shape

Decompose `audio_offload_process_asrc()` into private stages:

1. argument/pre-state capture;
2. submit-lock acquisition and lifecycle recheck;
3. ring produce/notify/wait/consume;
4. metadata/state/shadow validation;
5. one fault finalizer;
6. one success commit/linearization point;
7. caller output copy after commit.

The fault finalizer accepts captured lifecycle, sequence, errno, category
counter, recovery eligibility, and lock ownership.  It must preserve every
existing `fallback_count`, category counter, `last_error`, recovery scheduling,
and `-EAGAIN` return.

`CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY` shadow processing stays in validation stage
4 and uses same lifecycle/fault finalizer.  Do not remove or demote it.

### Files

- `src/audio_offload.c/.h`.
- `tests/unit/audio_offload`, `tests/unit/offload_asrc`, proposed
  `tests/unit/offload_asrc_verify`, and mocks.
- `tests/test-matrix.json` transitions/outcomes.

### Non-scope

No 360-frame FLPR support, deadline change, protocol change, counter rename, or
recovery policy change.

### Verification

- Table-driven tests for every current fault stage before extraction.
- Exact before/after counter/status/result snapshots for all fault classes.
- Add a distinct verify-enabled production-source suite (proposed
  `tests/unit/offload_asrc_verify`) with
  `CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=1`.  Cover exact match, sample/state/CRC/
  sequence/frame-count mismatch, cpuapp import failure, output untouched,
  category counters, and shared finalizer behavior.
- Build nRF54L15 with
  `fw-build-54l15 -- -DCONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=y`; confirm resolved
  config and zero warnings.  Run verify-enabled Mode A/B hardware row when R5
  changes shadow/finalizer code, then restore and build production verify-off
  image.
- Coverage split not needed unless file is physically split; helper extraction
  may stay in one file first.
- G1 + nRF54 Mode A/B 120 s + FLPR hang Mode A/B.

### Exit

One fault epilogue, one success linearization point, unchanged public behavior
and hardware counters.

## R6 — BAP receive-pipeline decomposition

### Goal

Reduce `bt_bap.c` to Bluetooth service/lifecycle orchestration and move audio
receive mechanics behind a session API.

### Target shape

Create an app-owned receive/session component, for example
`audio_stream_session.{c,h}`, owning per-sink app state only:

- validated codec shape;
- decoder context;
- ISO sequence tracker;
- Mode A assembler state;
- receive counters and presentation delay;
- decode/conceal/volume/push orchestration.

`struct bt_bap_stream` remains owned by ASCS/`bt_bap.c`; never move or clear
stack-owned conn/ep/codec/ISO fields.  `bt_bap.c` translates callbacks into
session events and ASCS responses.

`stream_lifecycle.c` remains pure configured/started/audio-path gate; session
component does not subsume it.  `bt_bap.c` composes lifecycle decisions with
session decode state until R7 consolidates teardown orchestration.

### Files

- `src/bt_bap.c`, `src/bt_bap.h`.
- New `src/audio_stream_session.c/.h`.
- `src/audio_decode.c/.h`, `src/audio_modea.c/.h`, and
  `src/audio_iso_seq.c/.h` only for narrow adapter changes.
- `src/stream_lifecycle.c/.h` for slot-occupancy API narrowing only.
- Root `CMakeLists.txt`, `tests/unit/lifecycle`, and proposed
  `tests/unit/audio_stream_session`.
- `tests/bsim/src/bsim_observer.c/.h`,
  `tests/bsim/src/audio_sink_stub.c`, `tests/bsim/src/bsim_sink_oracle.h`, and
  proposed `tests/unit/audio_stream_session`.

### Work

1. Extract common decode→volume→sink-push tail used by normal and concealed
   mono/Mode B paths.
2. Move Mode A event decode/interleave/push adapter next to session state while
   keeping pure ordering in `audio_modea.c`.
3. Move malformed SDU checks and sequence-gap injection only if tests prove
   mutation order remains identical.
4. Keep timing-reference update and BT callback context explicit.
5. Make session component exclusive owner of validated codec/session shape,
   populated directly from successful config events.  Narrow
   `stream_lifecycle_sink_configured()` to slot occupancy only (remove
   `chan_count` parameter/storage); lifecycle owns only configured/started gate
   state.  Remove `num_sink_ase` mode inference without adding mixed-duration
   support.
6. Add per-session generation and receive leases.  RX acquires a lease under
   session lock, captures generation, then decodes outside lock; teardown first
   closes admission and waits for admitted RX work before resetting decoder,
   assembler, or sequence state.  No lock spans decode, sink, or Bluetooth
   stack calls.
7. Reuse R1's `lifecycle_lock` (introduced in `bt_bap.c` during R1) to
   serialize config/start/stop/disable/release/disconnect admission
   transitions in this phase rather than introducing a duplicate lock.
   Fixed nesting is `lifecycle_lock` then session lock; RX never acquires
   `lifecycle_lock` while holding a session lease.  R7 reuses this contract
   rather than adding it after session extraction.

### Verification

- Add direct session tests compiling production source for mono/Mode A/Mode B,
  malformed input, gaps, PLC, gate closed, reset, release, and hard decode
  errors.
- BSim hashes/counts must remain byte-for-byte unchanged.
- Coverage split migration rule; `bt_bap.c` remains integration-classified.
- G1 + G3 BAP/I2S rows.

### Exit

`bt_bap.c` owns Bluetooth objects and callbacks; session component owns app
audio state; no duplicated decode/push paths.

## R7 — Stream teardown state machine

### Goal

Replace overlapping disable/stop/release/disconnect compositions with one
explicit, idempotent transition owner.

### Preconditions

R6 accepted.  Do not merge teardown while receive/session ownership is still
split across globals.

### Files

- `src/bt_bap.c` callback and teardown helpers.
- `src/audio_stream_session.c/.h` slot-release and all-session-reset APIs.
- `src/stream_lifecycle.c/.h` gate transitions; keep this pure owner separate
  from session state.
- `src/audio_sink.h`, `src/audio_offload.h`, `src/audio_stats.c/.h` only where
  coordinator calls existing idempotent stop/reset APIs.
- `tests/bsim/src/bsim_observer.c/.h`,
  `tests/bsim/src/audio_sink_stub.c`, `tests/bsim/src/bsim_sink_oracle.h`,
  `tests/unit/lifecycle`, proposed direct session tests, and
  `tests/test-matrix.json`.

### Target behavior

One private teardown coordinator in `bt_bap.c`, serialized by R6's short
`lifecycle_lock`, distinguishes:

- close audio gate for first stop/disable/release/disconnect;
- run global gate/offload/sink close side effects once;
- release each distinct app slot once while ASCS retains `bt_bap_stream`
  ownership, even when global gate already closed;
- reset all slots on ACL disconnect;
- stop offload and sink once;
- reset decoder/assembler/sequence state in defined order;
- retain `audio_sink.configured` across stream stops;
- expose whether advertising restart semaphore must fire.

No callback should directly compose low-level stop/reset calls after this
phase.

Coordinator calls `stream_lifecycle` for gate ownership,
`audio_stream_session` for app-owned per-slot state, and sink/offload/stats
idempotent global APIs once when gate closes.  Per-slot release is independent:
second Mode A slot still cleans up after first slot closed global gate; duplicate
release of same slot is no-op.  Do not create second state machine that mirrors
`stream_lifecycle`.

All config/start/stop/disable/release/disconnect transitions take
`lifecycle_lock` only while changing lifecycle/session admission state.  RX
uses R6 generation leases.  Coordinator closes RX admission and invokes
nonblocking `audio_sink_stream_close()` under lock, releases lock, waits for RX
leases, calls `audio_sink_stop()` to drain sink pushes, then stops offload and
runs state reset.  This defines ordering across ASCS, stream-op, ACL, and ISO
callback contexts without holding lock across Bluetooth, decode, offload, or
I2S calls.

### Verification

- Expand direct lifecycle/session transition matrix before implementation.
- BSim scenarios: first ASE stop, release without disable, disconnect while
  streaming, reconnect, late RX after close, first Mode A slot release followed
  by second-slot cleanup, duplicate same-slot release, slot reuse.
- Exact sink-oracle segment count and hashes unchanged.
- G1 + full G3 BAP/pairing/lifecycle rows.

### Exit

One teardown owner; first global close wins; each slot cleans up once; duplicate
callback for an already-cleaned slot is observable no-op; ASCS-owned structures
remain untouched.

## R8 — FLPR production/diagnostic boundary

### Goal

Remove acceptance-only machinery from core ring/handshake/FLPR runtime files
without weakening production-source tests.

### Work

1. Inventory every test/acceptance entry point in `flpr_ring_mgr.c`,
   `flpr_handshake.c`, and `src/flpr/main.c` and its shell caller.
2. Move host-side acceptance orchestration into a diagnostic module; core ring
   manager retains only production produce/consume/reset/notify/status APIs.
3. Add proposed `src/flpr/Kconfig` with
   `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS`, source `Kconfig.zephyr`, wire handlers
   in `src/flpr/CMakeLists.txt`, and enable it in `src/flpr/prj.conf` for current
   nRF54L15 acceptance builds.  Cpuapp shell orchestration remains under R4's
   `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`.  Add build-contract checks proving
   both images agree; release default-off policy remains separate work.
4. Replace macro redirects with dependency tables or narrow link seams where
   that reduces production-file test scaffolding.  Do not replace direct
   production-source tests with copied models.
5. Preserve FLPR wire protocol values and ABI version exactly.

### Files

- `src/flpr_ring_mgr.c/.h`, `src/flpr_handshake.c/.h`.
- `src/flpr/main.c`, `src/flpr_protocol.h`.
- Root `Kconfig`, root `CMakeLists.txt`,
  `boards/nrf54l15dk_nrf54l15_cpuapp.conf`, proposed `src/flpr/Kconfig`,
  `src/flpr/CMakeLists.txt`, and `src/flpr/prj.conf`.
- Proposed cpuapp `src/flpr_acceptance.c/.h` and FLPR-image
  `src/flpr/acceptance.c/.h`.
- `src/audio_shell.c`/`src/flpr_acceptance_shell.c` after R4 split.
- `tests/unit/flpr_handshake`, `tests/unit/flpr_ring`,
  `tests/unit/flpr_ring_mgr`, `tests/unit/flpr_runtime`,
  `tests/unit/flpr_protocol`, `tests/unit/flpr_audio_process`,
  `tests/unit/audio_shell_nrf54`, `tests/unit/flpr_hang_gate`,
  `tests/unit/flpr_stall_gate`, and `tests/test-matrix.json`; mocks inside those
  owning test directories move with their production seam.
- `scripts/check-build-contract.py` and `tests/unit/build_contract`.

### Verification

- All FLPR direct suites, protocol/ring bit-exact tests, build contract memory
  checks, and warning scans.
- Coverage split migration rule.
- G1 + nRF54 FLPR hang Mode A/B and status parser checks.

### Exit

Core FLPR files contain production runtime; diagnostics are explicit and
configurable; direct tests still execute real production logic.

## R9 — Host central orchestration split

### Goal

Split `bap_central.py` without changing CLI, D-Bus object paths, security flow,
LC3 payloads, pacing, teardown tail, or sudo boundary.

### Target modules

- adapter/device discovery and exact-peer resolution;
- raw-HCI fresh-connect strategy;
- BlueZ preserve-bond connect/security strategy;
- BAP endpoint registration/configuration/acquire;
- LC3 source/writer lifecycle;
- deterministic teardown/resource ownership;
- thin CLI coordinator.

Existing `bap_central_policy.py` and `bap_central_writer.py` remain useful
boundaries; do not merge them back.

### Files

- `scripts/bap_central.py`, `scripts/bap_central_policy.py`,
  `scripts/bap_central_writer.py`, and integration calls to
  `scripts/hci_raw_connect.py`.
- Proposed `scripts/bap_central_device.py`,
  `scripts/bap_central_security.py`, `scripts/bap_central_endpoint.py`, and
  `scripts/bap_central_session.py`.
- Existing `tests/unit/hci_raw_connect`, `tests/unit/bap_central_policy`, and
  `tests/unit/bap_central_writer`, plus proposed matching fake-D-Bus tests for
  each new module.

### Verification

- Fake D-Bus/unit tests for every ownership boundary and cancellation/error
  path.
- Existing raw-HCI, policy, writer, BZ2/BZ3 tests using names established by
  R0.
- Live nRF54L15 and E83 fresh + bonded 30 s sessions, then G1.

### Exit

No function owns discovery, security, BAP configuration, streaming, and
teardown simultaneously; CLI/output remains compatible with hardware gates.

## R10 — Final integration and documentation closeout

### Goal

Prove refactoring preserved all accepted behavior and make resulting
architecture the new baseline.

### Work

1. Update architecture diagrams/key files in AGENTS, README, and design.
2. Update behavior contracts only for clarified ownership, never weakened
   outcomes.
3. Regenerate coverage baseline only for accepted source splits using aggregate
   migration evidence.
4. Archive superseded handoffs only after all active links are updated; keep
   testing contracts and hardware baseline in place.
5. Record exact final commits, gate results/runtimes, build contract, probe
   evidence, and hardware counters.

### Files

- `AGENTS.md`, `README.md`, `STATUS.md`, `docs/design.md`.
- `docs/development/refactor-plan.md` and accepted phase handoffs/results.
- `docs/testing/behavior-contract.md`, `docs/testing/coverage-matrix.md`, and
  pre/post-refactor hardware baseline evidence.
- Test/build manifests only for final accepted inventory and baseline anchors.

### Verification

- G1 from clean exact final commit.
- Complete T8 hardware matrix on both targets.
- FLPR hang Mode A and Mode B.
- BZ3 full lifecycle.
- Pairing reset + BONDED_ONLY reconnect/interference check.
- Zero warnings/faults and clean worktree.

### Exit

All behavior contracts pass; coverage does not regress; both hardware matrices
pass; documentation describes only current architecture; refactor track marked
COMPLETE.

---

## Implementation order and risk

| Order | Phase | Risk | Hardware gate |
|---|---|---|---|
| 1 | R0 truth/gate inventory | Low | none normally |
| 2 | R1 ownership/concurrency | Medium | G2 |
| 3 | R2 dead API cleanup | Low | none normally |
| 4 | R3 test/tool consolidation | Medium | none normally |
| 5 | R4 shell/harness split | Medium | focused nRF54 |
| 6 | R5 offload decomposition | High | full FLPR rows |
| 7 | R6 receive-pipeline split | High | full BAP/I2S rows |
| 8 | R7 teardown state machine | Highest | full lifecycle rows |
| 9 | R8 FLPR diagnostic split | High | full FLPR rows |
| 10 | R9 central script split | High | both live connect modes |
| 11 | R10 final closeout | Highest integration | complete T8 matrix |

R5 and R6 may swap only if neither has started.  R7 must follow R6.  R10 is
always last.  Do not combine two high-risk phases into one commit or handoff.

## Rules for every handoff

1. Start from clean previous accepted phase commit.
2. Name exact files, APIs, ownership, state transitions, and non-scope.
3. Add/strengthen public-boundary tests before moving behavior.
4. Commit implementation/tests before any required baseline migration.
5. Run focused gate, then required G1/G2/G3 level.
6. Treat every warning, hash change, counter change, or unexplained flaky test
   as a blocker.
7. Commit evidence/docs separately after exact code commit passes.
8. Never push/merge/amend from Executor unless explicitly requested.
9. Never accept partial phase completion.  Fix review findings before writing
   the next phase handoff.

## Deferred feature list after refactoring

- Decide/implement 360-frame (7.5 ms) FLPR offload or document cpuapp-only
  offload policy as permanent.
- Multi-rate audio.
- Pairing-reset physical button GPIO and debounce/hold UX.
- Emergency-fallback crossfade.
- External fractional-N clock actuator/custom PCB.
- CI revival.

These are separate features.  They must not be smuggled into behavior-preserving
refactor phases.
