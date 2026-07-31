# Phase T0 handoff — behavior contract and honest coverage map

## Goal

Create authoritative pre-refactor behavior contract and truthful test-coverage
inventory. Correct stale canonical-gate documentation. Make no production
behavior change.

Phase plan: `docs/development/pre-refactor-testing-plan.md`.

## Git base and branch

- Fetch `origin` with prune.
- PR #3 is merged. Base this phase on `origin/main`, expected merge commit
  `20b37c405835e5c2c747fa7b072c4c0b752b29cd` or a later fast-forward containing
  it.
- Current worktree may still be on merged `fix/xiao-rf-switch` and contains
  uncommitted plan/handoff documents. Preserve both documents.
- Create and use branch `test/pre-refactor-behavior` from `origin/main`.
- Do not commit on the merged feature branch.

## In scope

Create:

- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- `docs/testing/v0.0.1-baseline.md`

Include in the phase commit:

- `docs/development/pre-refactor-testing-plan.md`
- `docs/development/pre-refactor-testing-t0-handoff.md`

Update:

- `STATUS.md`
- documentation comments only in `scripts/test-all.sh`

## Out of scope

- Production C code changes.
- New tests or testability seams.
- Coverage scripts, gcovr, or flake changes; those belong to T7.
- Build configuration changes.
- Flashing, resetting, UART, RF, or other hardware control.
- Refactoring or deleting weak tests.
- Changing accepted historical evidence in existing result documents.

## Exact behavior-contract shape

Write `docs/testing/behavior-contract.md` as numbered, reviewable contracts.
Use stable IDs with these sections:

### Bluetooth and service contract (`BT-*`)

- `BT-001`: BAP Unicast Server is sink-only; source direction rejected.
- `BT-002`: PACS advertises 48 kHz LC3 only, 7.5/10 ms, one or two channels,
  one frame block per SDU.
- `BT-003`: supported sink contexts equal current
  `AVAILABLE_SINK_CONTEXT`; available contexts remain non-NONE through ACL
  connection and streaming.
- `BT-004`: ASCS advertising contains ASCS UUID, general announcement,
  supported sink contexts, no source contexts, and full device name.
- `BT-005`: Just Works pairing is accepted; MITM is not required; successful
  bonds persist through settings.
- `BT-006`: PACS registration happens after `settings_load()` and before
  advertising.
- `BT-007`: disconnect releases retained connection reference, resets stream
  state, stops audio/offload, and wakes advertising restart loop.

### Codec and routing contract (`CODEC-*`)

- `CODEC-001`: 48 kHz 10 ms yields 480 samples/channel.
- `CODEC-002`: 48 kHz 7.5 ms yields 360 samples/channel.
- `CODEC-003`: mono single ASE is duplicated to left and right.
- `CODEC-004`: Mode A uses two mono ASEs and emits only after both channel
  halves are available.
- `CODEC-005`: Mode B uses one two-channel ASE and independent left/right LC3
  decoder state.
- `CODEC-006`: invalid ISO input invokes PLC; decode failures are counted.
- `CODEC-007`: unsupported frequency, duration, channel shape, or frame-block
  shape is rejected rather than guessed.

### Stream lifecycle contract (`LIFE-*`)

- `LIFE-001`: mono/Mode B opens after its single configured ASE starts.
- `LIFE-002`: Mode A opens only after both configured ASEs start, independent
  of start order.
- `LIFE-003`: opening is a closed-to-open edge, not a level event.
- `LIFE-004`: first stop/disable/release closes path and stops offload.
- `LIFE-005`: close, sink stop, and offload stop are idempotent.
- `LIFE-006`: late receive callbacks after closure cannot decode, push, update
  timing, or restart DMA.
- `LIFE-007`: disconnect clears configured/started stream lifecycle and decoder
  state so reconnect starts cleanly without reinitializing I2S configuration.

### Audio sink and I2S contract (`I2S-*`)

- `I2S-001`: input is non-null, non-empty, stereo-paired, and exactly the
  configured frame duration.
- `I2S-002`: startup queues six distinct silence blocks, then first audio
  block, then starts DMA.
- `I2S-003`: each queued I2S write owns a distinct slab block.
- `I2S-004`: once started, drift controller runs once per rendered block.
- `I2S-005`: emergency repeat fallback uses a separately allocated block.
- `I2S-006`: `-EIO` records a stream reset, triggers PREPARE, and requires
  fresh startup on next push.
- `I2S-007`: stop resets timing/drift/actuator/resampler, then PREPARE before
  DROP; configured state remains usable for reconnect.
- `I2S-008`: slab exhaustion, underrun, push failure, repeat fallback, and ASRC
  capacity failure remain observable.

### Clock and rate contract (`CLOCK-*`)

- `CLOCK-001`: nRF5340 uses identity resampling and APLL actuator.
- `CLOCK-002`: nRF54L15 uses linear ASRC and NONE actuator; ASRC consumes ppm.
- `CLOCK-003`: positive local PCLK error produces negative feedforward
  correction; phase term sign follows documented slab-fill convention.
- `CLOCK-004`: drift output and integral remain clamped with directional
  anti-windup.
- `CLOCK-005`: reset returns controller to INIT and clears frequency,
  integral, and output state.
- `CLOCK-006`: nRF54 timing uses GRTC compare via GPPI to TIMER20 capture and
  never accesses RADIO.
- `CLOCK-007`: stale timing work from a prior generation cannot feed drift.

### ASRC and FLPR contract (`OFFLOAD-*`)

- `OFFLOAD-001`: cpuapp and FLPR use same fixed-point ASRC state contract.
- `OFFLOAD-002`: FLPR result commits output/post-state transactionally only
  after complete validation.
- `OFFLOAD-003`: any offload failure falls back from unchanged cpuapp pre-state.
- `OFFLOAD-004`: stream start is nonblocking and enters PREPARING; stream stop
  enters STOPPED and invalidates generation.
- `OFFLOAD-005`: timeout, full, stale, sequence, frame, CRC, payload, and state
  faults are distinguishable and counted once.
- `OFFLOAD-006`: recovery is bounded to current five-attempt/backoff policy.
- `OFFLOAD-007`: successful recovery enters probation; 100 consecutive
  successes clear probation; relapse escalates backoff.
- `OFFLOAD-008`: runtime restart holds DMACTIVE enabled, asserts reset through
  copy/flush/CRC/INITPC/reconnect/CPURUN, then releases reset as final launch
  edge.
- `OFFLOAD-009`: ring epochs reject stale slots and stale notifications.
- `OFFLOAD-010`: output-ring backpressure cannot consume/drop input.

### Initialization and diagnostics contract (`APP-*`)

- `APP-001`: init order is watchdog, Bluetooth, settings, volume, BAP, I2S,
  nRF54 FLPR services, advertising.
- `APP-002`: fatal initialization or advertising failure requests cold reboot.
- `APP-003`: disconnect restarts advertising.
- `APP-004`: status and stream-summary fields used by automation remain stable
  and parseable.
- `APP-005`: warnings, assertions, boot errors, and flashing warnings are not
  normalized as success; recorded SDK diagnostics remain separately listed.

### Board and build contract (`BUILD-*`)

- `BUILD-001`: both production targets and central dongle build against NCS
  v3.3.0.
- `BUILD-002`: nRF5340 cpunet applies both SW Split Kconfig and devicetree
  overlays.
- `BUILD-003`: host/controller ACL and ISO counts match per target.
- `BUILD-004`: nRF54 I2S/UART/RF-switch pins, conflicting peripheral disables,
  and 16 pF crystal configuration remain exact.
- `BUILD-005`: FLPR source, execution, and shared-ring regions remain exact and
  non-overlapping.
- `BUILD-006`: probe identity remains runtime-resolved; no static mapping enters
  source or docs.

End contract with explicit unsupported/non-scope list from the phase plan.

## Exact coverage-matrix shape

Write `docs/testing/coverage-matrix.md` with:

1. Definitions for `direct`, `integration`, `build`, `hardware`, `model`,
   `structural`, `stub`, and `historical` evidence.
2. Table columns:
   - production source;
   - responsibility;
   - direct production-source unit proof;
   - integration/BSim proof;
   - build-contract proof;
   - hardware proof;
   - current gap;
   - closing phase.
3. One row for every `.c` implementation in `src/` and `src/flpr/`:
   `audio_asrc.c`, `audio_clock_actuator_apll.c`,
   `audio_clock_actuator_none.c`, `audio_clock_actuator_sample_adjust.c`,
   `audio_decode.c`, `audio_drift.c`, `audio_i2s.c`, `audio_offload.c`,
   `audio_perf.c`, `audio_rate_convert.c`, `audio_shell.c`, `audio_stats.c`,
   `audio_timing_math.c`, `audio_timing_none.c`, `audio_timing_nrf54.c`,
   `audio_volume.c`, `bt_bap.c`, `flpr_audio_process.c`, `flpr_cache.c`,
   `flpr_handshake.c`, `flpr_ring.c`, `flpr_ring_mgr.c`, `flpr_runtime.c`,
   `main.c`, `stream_lifecycle.c`, and `src/flpr/main.c`.
4. Mark `audio_clock_actuator_sample_adjust.c` historical/non-production.
5. State these current suite facts explicitly:
   - actuator suite compiles retired sample-adjust implementation;
   - FLPR runtime suite compiles non-nRF54 stub;
   - FLPR ring-manager suite tests copied model, not production source;
   - FLPR handshake suite tests protocol helpers, not production source;
   - BSim compiles production BAP/decode but one mono ASE with fake sink and no
     I2S/FLPR;
   - build and historical hardware evidence do not replace direct branch/error
     tests.
6. Summarize current suite inventory: 12 testcase/Twister suites, four exec-only
   C suites, four Python suites, and one accepted BSim gate = 21 gate children.

Do not claim numeric code coverage; no honest report exists yet.

## Exact baseline document shape

Write `docs/testing/v0.0.1-baseline.md` and distinguish:

- tag `v0.0.1` target `c608e460627ce7f9858c9dcea890848aa3a6a28f`;
- RF fix was not in that tag;
- PR #3 merged RF fix as
  `20b37c405835e5c2c747fa7b072c4c0b752b29cd`;
- manual connection confirmation applies after RF fix;
- pre-merge PR #3 verification: full gate 21/21 on provisioned workstation,
  `fw-build-5340` pass, `fw-build-54l15` pass;
- evidence provenance: current conversation/session and existing
  `docs/development/xiao-rf-switch-fix-handoff.md`; do not invent counters;
- tag is release/history anchor, while merged main after PR #3 is testing-plan
  implementation base.

## STATUS update

Add a short top-level section near top of `STATUS.md`:

- pre-refactor testing track started;
- accepted master plan path;
- T0 purpose;
- no claim that 432 tests mean production branch coverage;
- manual connection after RF fix recorded as hardware evidence.

Do not rewrite historical sections or change accepted results.

## Canonical gate comment correction

In `scripts/test-all.sh`, update comments only:

- four Python suites, not two;
- name gate, flpr_stall_gate, bluez_wp_gate, and bluez_wp_phase3_gate;
- preserve executable behavior exactly.

Run `git diff --word-diff` or equivalent inspection to verify no shell command
changed.

## Validation

Required checks:

```bash
bash -n scripts/test-all.sh
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
```

BabbleSim prerequisites are known to exist on workstation
`thomas-workstation`, repo `/home/thomas-workstation/repos/le-audio-receiver`.
If local desktop lacks them, do not report gate success from a partial run.
Validate the exact phase commit in a temporary clean workstation worktree or
equivalent non-destructive transfer, then return workstation to clean `main`.
Because this phase changes documentation and comments only, build outputs must
remain behavior-identical.

No flashing or hardware interaction.

## Acceptance

- All three docs exist and agree with plan/source.
- Every production `.c` file is classified.
- Weak-test facts are explicit and accurate.
- Unsupported behavior is explicit.
- `test-all.sh` executable behavior is unchanged.
- Full gate and all three builds pass.
- Worktree clean after one scoped commit.

## Commit and return

Inspect `git status`, `git diff`, and recent log. Stage only T0 files. Commit
with normal human-authored message:

```text
docs: define pre-refactor behavior baseline
```

Do not push, merge, open PR, amend, or add attribution. Return:

- files changed;
- behavior/doc changes;
- exact verification commands and results;
- commit hash/message;
- blockers;
- deviations from this handoff;
- suggested follow-up for T1.
