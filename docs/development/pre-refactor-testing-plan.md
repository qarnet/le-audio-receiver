# Pre-refactor behavior and testing plan

Status: accepted for phased implementation on 2026-07-31.

## Goal

Define and prove current supported behavior before large-scale refactoring. The
test system must detect changes to Bluetooth interoperability, codec routing,
audio output, timing, drift correction, FLPR offload, recovery, board
configuration, and diagnostics.

This plan values tests that execute production source over test count. A test
that duplicates production logic, compiles a stub, or checks copied constants
does not count as proof of production behavior.

## Baseline

- Release tag `v0.0.1` points to `c608e460627ce7f9858c9dcea890848aa3a6a28f`.
- PR #3 adds the accepted Xiao RF-switch and crystal correction on top of that
  release and was merged as `20b37c405835e5c2c747fa7b072c4c0b752b29cd`.
- User manually confirmed normal connection after the RF correction.
- Current local gate passes 21/21 on the provisioned workstation.
- Both production firmware targets and the central dongle build cleanly.

Manual connection is useful acceptance evidence, but it is not automated
regression protection.

## Scope

### Supported behavior to lock

- nRF5340 and nRF54L15 production targets.
- BAP Unicast Server, sink-only.
- 48 kHz LC3 at 7.5 ms and 10 ms frame durations.
- One frame block per SDU.
- Mono single ASE duplicated to stereo.
- Mode A: two mono ASEs routed as left and right.
- Mode B: one two-channel ASE decoded by two independent LC3 decoders.
- Just Works Secure Connections pairing, persistent bonding, and reconnect.
- PACS available contexts remain truthful while an ACL is connected.
- Audio path opens only after the required ASE set has started.
- Audio path closes once on the first stop, disable, release, or disconnect.
- Late packets after closure cannot restart timing, offload, or I2S.
- Invalid ISO packets invoke PLC without corrupting stream state.
- nRF5340 uses identity rate conversion and APLL steering.
- nRF54L15 uses linear ASRC, FLPR offload when healthy, and transactional
  cpuapp ASRC fallback when offload is unavailable.
- I2S underruns are counted and recover through `I2S_TRIGGER_PREPARE`.
- FLPR faults produce bounded fallback, restart, probation, and reactivation.
- Failures remain observable through stable counters and log fields.

### Explicitly unsupported

- Sampling rates other than 48 kHz.
- More than two sink channels.
- More than one frame block per SDU.
- Source ASEs.
- CAP/CAS, TMAS, CSIS, extra codecs, A2DP, or phone interoperability.
- Analog audio-fidelity guarantees.
- Arbitrary malformed LC3 recovery beyond safe rejection or PLC.
- nRF54L15 recovery from APPROTECT lock.

Unsupported configurations must be rejected deterministically rather than
accepted accidentally.

### Non-scope

- Bluetooth qualification, RF certification, or broad vendor matrices.
- New product behavior or protocol support.
- Refactoring production architecture beyond narrow testability seams.
- Claims that finite tests prove every possible scheduler or hardware fault.

## Current confidence gaps

1. `tests/unit/flpr_runtime` compiles the non-nRF54 stub from
   `src/flpr_runtime.c`; most tests check copied constants or enum structure.
2. `tests/unit/flpr_ring_mgr` does not compile `src/flpr_ring_mgr.c`; it tests a
   separate model containing replicated reset and notification logic.
3. `tests/unit/flpr_handshake` does not compile `src/flpr_handshake.c`; it tests
   header-only protocol helpers.
4. `tests/unit/actuator` tests retired
   `src/audio_clock_actuator_sample_adjust.c`, not either production actuator.
5. `src/audio_i2s.c` has no direct unit suite despite owning slab/DMA state,
   underrun recovery, resampler selection, offload fallback, and stop order.
6. BabbleSim exercises one mono ASE. Mode A, Mode B, reconnect, packet-loss,
   malformed configuration, and teardown permutations are not covered there.
7. `audio_volume.c`, direct `audio_stats.c`, `audio_timing_nrf54.c`, `main.c`,
   and `audio_shell.c` lack direct production-source tests.
8. Builds do not automatically assert resolved Kconfig and devicetree
   invariants.
9. No coverage report or production-file-to-test manifest prevents blind
   spots from growing.

## Rules for all phases

- Compile real production source whenever host simulation can support it.
- Do not use copied algorithms as primary proof.
- Name model, structural, integration, and hardware tests honestly.
- Every behavior row has an automated test or an explicit hardware-only gate.
- Every public error outcome and state-machine transition gets a test.
- Fix warnings; do not normalize them.
- Keep both production builds green after every phase.
- Keep nRF5340 behavior intact.
- Testability seams may inject transport, memory, HAL, clock, or driver
  dependencies, but may not redesign behavior during this plan.
- If characterization exposes a defect, add the failing test and make the
  smallest correction before freezing that behavior.
- Each phase ends with a committed implementation, review, and acceptance
  before the next handoff.

## Phase T0 — Behavior contract and honest coverage map

### Files

Create:

- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- `docs/testing/v0.0.1-baseline.md`

Update:

- `STATUS.md`
- `scripts/test-all.sh`

### Work

- Turn supported and unsupported behavior above into numbered contracts.
- Map every production source file to direct unit, integration/BabbleSim,
  build-contract, and hardware evidence.
- Mark stub, copied-model, structural, and historical tests explicitly.
- Record release and RF-fix baseline evidence without claiming manual checks
  are automated.
- Correct stale suite-count documentation in `scripts/test-all.sh`.
- Add a machine-readable classification check if needed so no production file
  remains unclassified.

### Verification

```bash
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
```

Exit: every production source file is classified and all existing gates pass.

## Phase T1 — Replace replicated tests with production-source tests

### FLPR runtime

Touch `src/flpr_runtime.{c,h}` and `tests/unit/flpr_runtime/`. Add narrow seams
for VPR access, source/execution memory, cache operations, CRC, handshake, and
clock calls. Execute real `flpr_runtime_restart()` and prove full operation
order, exact DMCONTROL transitions, all failure stages, stopped-core failure
postcondition, CRC rejection, mutex rejection, timeout accounting, status
counters, and changed-epoch success.

### FLPR ring manager

Touch `src/flpr_ring_mgr.{c,h}` and `tests/unit/flpr_ring_mgr/`. Compile real
production source with injected ring memory and handshake transport. Prove init
readiness, coordinated reset outcomes, invalidation races, semaphore draining,
producer/consumer validation, metadata, payload zeroing, CRC accounting,
backpressure, stall ACKs, timed-stall validation, sequence wrap, and remote
restart. Remove or rename copied-model tests after real equivalents exist.

### FLPR handshake

Touch `src/flpr_handshake.c` and `tests/unit/flpr_handshake/`. Compile production
implementation with mocked IPC. Prove bind/unbind, READY and duplicate READY,
ACK send failures, heartbeat health transitions, callback lock discipline,
malformed/unknown message handling, ring-handler routing, disconnect/reconnect,
semaphore draining, changed-epoch wait, stress timeout and late-PONG handling,
and fault-hang ACK timeout.

Verification: `./scripts/test-all.sh`.

Exit: all three suites link the named production implementation; no copied
algorithm is counted as primary proof.

## Phase T2 — Audio pipeline unit characterization

### Decode and routing

Expand `tests/unit/decode/` and add checked-in LC3 fixtures under
`tests/fixtures/lc3/` for mono and Mode B at 7.5 ms and 10 ms. Assert exact PCM
hashes and channel placement. Add null, reset, channel-count, frame-block,
decoder, and malformed-length rejection tests. Replace existing “does not
crash” checks with meaningful output assertions.

### Volume and statistics

Create `tests/unit/volume/` compiling `src/audio_volume.c`. Prove mute, zero,
unity, default volume, signed extremes, callback error behavior, and atomic
snapshots. Create `tests/unit/stats/` compiling `src/audio_stats.c`; prove each
counter, PLC/total coupling, reset, snapshots, and concurrent increments.

Verification: `./scripts/test-all.sh`.

Exit: valid LC3 decode has golden-output proof and audio transforms have direct
production-source tests.

## Phase T3 — I2S state-machine tests

Create `tests/unit/audio_i2s/` with a fake I2S driver and mocked timing, drift,
actuator, offload, rate-converter, ASRC, statistics, and performance
dependencies. Touch `src/audio_i2s.c` only for narrow injection seams.

Prove:

- all initialization failures and successful configuration;
- null, zero, odd, and wrong frame-count rejection;
- six distinct silence blocks and one data block before START;
- no slab pointer reuse;
- exact ownership on every write/trigger error;
- one drift update per started block;
- APLL application of returned ppm;
- transactional FLPR state import;
- cpuapp fallback from unchanged pre-state for every offload fault;
- capacity-failure slab release;
- separate repeat-fallback slab block;
- `-EIO` PREPARE recovery and fresh restart;
- stop order `PREPARE` then `DROP`, idempotence, and retained configuration;
- reset of timing, drift, actuator, ASRC, and sequence state.

Verification:

```bash
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/test_audio_i2s tests/unit/audio_i2s -p -t run
./scripts/test-all.sh
```

## Phase T4 — BAP and Bluetooth behavior matrix

Extend `tests/bsim/` and `scripts/bsim-stage1-run.sh` while continuing to build
real `src/bt_bap.c` and `src/audio_decode.c`.

Scenarios:

1. Mono 10 ms.
2. Mono 7.5 ms.
3. Mode A 10 ms.
4. Mode A 7.5 ms.
5. Mode A reverse ASE start order.
6. Mode B 10 ms.
7. Mode B 7.5 ms.
8. Controlled invalid ISO packet followed by resumed valid audio.
9. First Mode A ASE stops while second still delivers.
10. Release without prior disable.
11. Disconnect during streaming.
12. Reconnect and second stream.
13. Unsupported source direction.
14. No-free-slot rejection.
15. Missing or invalid codec-field rejection with correct ASCS response.

Normal scenarios run twice with exact channel-specific PCM hashes, exact frame
accounting, no malformed/decode/lifecycle faults, and non-NONE PACS available
contexts. Mode A uses distinct left/right signatures so swap, duplication,
overwrite, stale pairing, and cross-pairing fail visibly.

Verification:

```bash
./scripts/bsim-stage1-run.sh
./scripts/test-all.sh
```

## Phase T5 — Lifecycle, timing, drift, and production actuators

Expand `tests/unit/lifecycle/` across configure/start/close/reset permutations,
including duplicate starts. `stream_lifecycle_sink_started()` must report a
closed-to-open edge once, not report every later start as a new open.

Create `tests/unit/timing_nrf54/` compiling `src/audio_timing_nrf54.c` against
mocked GRTC/GPPI/TIMER HAL. Cover allocation cleanup, one anchor per session,
zero/past timestamps, first-delta skip, timer wrap, late reschedule, schedule
failure, stale generations, reset, and drift-feedforward delivery.

Create production suites for `audio_clock_actuator_apll.c` and
`audio_clock_actuator_none.c`. Cover center/reset, signed conversion,
near-zero truncation, clamps, accepted input boundaries, and no-op behavior.
Retain retired sample-adjust tests only under an explicitly historical label.

Expand drift tests for accepted input boundaries, overflow resistance,
long-run boundedness, concurrent reset/update, setpoint stability, and
feedforward-rail phase unwind.

Verification: `./scripts/test-all.sh`.

## Phase T6 — Boot, diagnostics, and resolved configuration

Extract only a narrow, testable boot coordinator into `src/app_lifecycle.{c,h}`
with `main.c` retaining hardware wiring. Create `tests/unit/app_lifecycle/` and
prove init order, cold-reboot behavior for every fatal failure, advertising
restart, and restart-failure reboot.

Create `tests/unit/audio_shell/` to lock parseable status fields, zero-safe
percentages, performance formatting, reset commands, unpair error propagation,
and FLPR fields consumed by hardware gates.

Create `scripts/check-build-contract.py` plus tests. Parse resolved `.config`
and `zephyr.dts` for both production builds and assert:

- nRF5340 APLL + identity path;
- nRF54L15 NONE + ASRC + FLPR path;
- host/controller ISO buffer agreement;
- two sink ASEs and 48 kHz capability contract;
- Xiao RF-switch polarity and 16 pF clocks;
- I2S pins and conflicting-peripheral disables;
- exact non-overlapping FLPR/ring memory ranges;
- both nRF5340 SW Split Kconfig and devicetree overlays.

Verification:

```bash
fw-build-5340
fw-build-54l15
fw-build-dongle
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
```

## Phase T7 — Coverage enforcement — ACCEPTED (2026-08-02)

Implementation complete on exact code commit `4a31324` (baseline + gate
wiring); baseline generated on the clean commit `c6adce8`, committed in
`4a31324`, default enforcement rerun on clean `4a31324` with identical
ratios.  A warning-only test-config correction then removed
the two contradictory `CONFIG_LOG=n` lines in the shell test suites that
the accepted run's review had classified as Kconfig assigned-value
warnings.  **ACCEPTED (2026-08-02)**: full canonical gate observed on the
exact commit `8f7bfca` from a detached fresh clone on
`thomas-workstation` — **41 PASS / 0 FAIL / 41 TOTAL** (25 twister +
4 exec-only + 9 Python + coverage + matrix + BSim), script exit 0,
elapsed 816 s (13m36s), **zero Kconfig assigned-value warnings**, zero
compiler warnings.  Observed evidence, warning classification, and
provenance: `STATUS.md` (T7 section),
`docs/development/workstation-transfer-status.md`,
`docs/testing/coverage-matrix.md` (numeric baseline).
Committed `tests/coverage-baseline.json`: lines 3070/3503, branches
1332/1921, functions 182/182 (100%) in the 23-file numeric population.
Coverage and matrix checks are canonical `test-all.sh` children.

Add `gcovr` to `flake.nix`, create `scripts/test-coverage.sh` and
`scripts/check-test-matrix.py`, and integrate them into the canonical gate.
`gcov` is already available in the development shell.

Enforce:

- every host-testable production function executes;
- every public return/error outcome is represented;
- every state-machine transition is represented;
- every production source file has a test classification;
- hardware-only exclusions name their acceptance command;
- copied models do not contribute production coverage;
- coverage cannot decrease during refactoring.

Set numeric branch/line thresholds from the first honest report, then raise
them deliberately. Do not manufacture a 100% baseline from stubs.

Verification:

```bash
./scripts/test-coverage.sh
./scripts/check-test-matrix.py
./scripts/test-all.sh
```

## Phase T8 — Hardware baseline freeze

Use only the autonomous nRF5340DK `hci_uart` central and repository scripts.
Capture console before reset. Resolve probes with `nrf-probes`; never record a
static probe mapping.

### nRF54L15

- Fresh build and flash.
- Mode A 120 seconds.
- Mode B 120 seconds.
- Disconnect/reconnect and repeat.
- Forced timed FLPR stall proving cpuapp fallback, runtime restart, probation,
  and FLPR reactivation.
- Stock BlueZ/WirePlumber clean pair, 120-second playback, reconnect playback,
  receiver reset, bonded reconnect, and third playback.
- Zero warnings, assertions, decode errors, underruns, ASRC capacity failures,
  and integrity faults.

### nRF5340

- Fresh dual-core build and flash.
- Mode A 120 seconds.
- Mode B 120 seconds.
- Reconnect stream.
- APLL active and repeat fallback zero in steady state.
- Zero warnings, assertions, and faults.

Record commands, commits, hashes, counters, logs, durations, and raw probe
identity evidence in `docs/testing/pre-refactor-hardware-baseline.md`. User may
add audibility evidence, but measurable automated gates do not depend on it.

## Final pre-refactor gate

Large refactoring starts only after T0–T8 are accepted and these pass:

```bash
./scripts/test-all.sh
./scripts/test-coverage.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
```

Both hardware acceptance matrices must also pass. Then refactoring analysis
may identify duplicate implementations, missing abstractions, ownership and
state-machine improvements, readability work, and diagnostic improvements.

## Implementation order

1. T0 — behavior contract and honest map.
2. T1 — replace false-confidence FLPR tests.
3. T2 — decode, volume, and statistics.
4. T3 — I2S state machine.
5. T4 — BAP/BabbleSim matrix.
6. T5 — lifecycle, timing, drift, and actuators.
7. T6 — boot, shell, and build contracts.
8. T7 — coverage enforcement → ACCEPTED (2026-08-02; baseline commits
   `c6adce8`/`4a31324`, warning-fix commit `8f7bfca`); canonical gate
    observed exact `41 PASS / 0 FAIL / 41 TOTAL`, exit 0, elapsed 816 s
    (13m36s), zero Kconfig assigned-value warnings (see `STATUS.md` T7
   section and `docs/development/workstation-transfer-status.md`).
9. T8 — hardware baseline freeze — **ACCEPTED (2026-08-04)** — both
   hardware matrices pass on the exact final production code `971e6a4`
   (nRF54L15 + nRF5340/E83; coverage-baseline `1a5842d`, coverage docs
   `3c29421`, first acceptance closeout `5ceb719`, final docs HEAD = the
   evidence-fix commit); final software gate **47 PASS / 0 FAIL / 47 TOTAL**
   on the exact final code — exact observed re-run retained:
   `./scripts/test-all.sh` on 2026-08-04T05:26:57+02:00 on
   `thomas-workstation` (worktree clean, production tree == `971e6a4`),
   `Gate complete: 47 PASS / 0 FAIL / 47 TOTAL`, exit 0, elapsed
   **1016.45 s**, log `/tmp/t8-final-47.log` (transient through review);
   coverage baseline accepted; builds 3/3; build contract 76/76 (direct
   run retained: `76 assertions, 0 failed`, exit 0,
   `/tmp/t8-final-build-contract.log` transient through review); zero
   actionable warnings.  See
   `docs/testing/pre-refactor-hardware-baseline.md` (T8 ACCEPTED).  One
   documented evidence limitation: per-CIS ISO sequence-gap activation was
   not observable on hardware (clean E83 link) — covered by the 18-test
   `iso_seq` production-module suite + prior T9 failing-hardware
   provenance, not a hardware activation claim.
