# Portable LC3/PCM test oracle plan

Status: P0, P0b, P0c, P0d, P0e, P1, and P2 accepted; P3 and P4 pending.

Product item: [PB-031](../product/backlog/tasks/pb-031%20-%20Make-LC3-PCM-test-oracle-platform-independent.md), Make LC3/PCM test oracle platform-independent.

## Decision

Retain production liblc3 v1.1.2 and `-O3 -ffast-math`. Move determinism
boundary from decoded PCM bytes to checked-in LC3 bytes plus portable numerical
PCM acceptance.

After migration, old decoded-PCM hashes remain diagnostic evidence only. They
are never pass/fail values.

## Goal

Keep BSim transport, routing, lifecycle, frame-count, PLC, and decoder-error
contracts strict while making decoded-PCM acceptance valid across supported
host CPU and compiler instruction paths.

## Evidence

- Current BSim TX in `tests/bsim/client/src/bsim_tx.c` generates integer PCM
  from channel, sequence, and sample, then calls `lc3_encode()` at runtime.
- Receiver sink in `tests/bsim/src/audio_sink_stub.c` FNV-hashes interleaved
  and per-channel `int16_t` PCM. `tests/bsim/stage1-scenarios.json` and
  `scripts/bsim_stage1_parse.py` pin exact values.
- `tests/unit/decode/src/test_decode.c` similarly compares decoded PCM
  byte-for-byte and CRC against host-generated PCM.
- NCS v3.3.0 liblc3 CMake requires `-O3 -std=c11 -ffast-math`. Generated
  native x86 code uses reciprocal-square-root instructions in liblc3 LTPF/SNS
  paths. Exact PCM bytes are therefore wrong portability boundary.
- Hosted failure retained correct observable counts and energy but changed PCM
  hash. Same-SHA rerun passed. Do not claim root CPU model unless evidence
  names it.

## Scope and non-scope

### Expected future implementation files

Future implementation may change only the following areas, subject to phase gates:

- `tests/fixtures/lc3/README.md`
- `tests/fixtures/lc3/generate.sh`
- `tests/fixtures/lc3/gen_fixtures.c`, or separate checked-in corpus generator
- New fixture manifest and corpus files under `tests/fixtures/lc3/`
- `tests/bsim/client/CMakeLists.txt`
- `tests/bsim/client/src/bsim_tx.c` and `tests/bsim/client/src/bsim_tx.h`
- `tests/bsim/src/bsim_observer.c` and `tests/bsim/src/bsim_observer.h`
- `src/audio_stream_session.c`, only for `CONFIG_BSIM_OBSERVER` sequence
  metadata
- `tests/bsim/src/audio_sink_stub.c`
- `tests/bsim/src/bsim_sink_oracle.h`
- `tests/bsim/src/bsim_test_main.c`
- `tests/bsim/stage1-scenarios.json`
- `scripts/bsim_stage1_parse.py`
- `scripts/bsim-stage1-run.sh`, only if artifact or report plumbing requires it
- `tests/unit/bsim_runner/`
- `tests/unit/decode/CMakeLists.txt` and `tests/unit/decode/src/test_decode.c`
- New shared test-only integer comparator under `tests/support/`, plus focused
  unit suite
- `docs/testing/t2-audio-pipeline-tests.md`
- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- `STATUS.md`, only when implementation evidence exists

### Non-scope

Non-goals are changing production decoder or audio behavior, removing
`-ffast-math`, making liblc3 bit-exact, changing BAP scenario, lifecycle, PLC,
or count contracts, repinning old host-specific PCM hashes, and changing
firmware, release, or hardware behavior.

No workflow runner diversification belongs in this item except a temporary or
bounded calibration mechanism. Permanent multi-architecture CI is separate
work unless required to keep oracle calibration current.

## Target architecture

### 1. Checked-in BSim corpus

- Provide 128 logical frames per channel, enough for current maximum
  110-send scenarios and reconnect reset.
- Provide 48 kHz 10 ms/120-byte and 7.5 ms/90-byte LC3. Left and right source
  patterns must be distinct, deterministic, and evolve by logical sequence.
- Store raw concatenated per-channel LC3 and little-endian decoded reference
  PCM. Reference PCM is comparison anchor, not exact-output contract.
- Manifest records schema, NCS/liblc3 version, flags, frame geometry and count,
  source formula, and SHA-256 for every binary.
- Generator is reproducibility-only and never runs in normal tests. Generation
  output must be warning-free. Regeneration requires explicit manifest and
  documentation update, never silent baseline rewrite.

### 2. Exact TX boundary

- Replace runtime `lc3_encode()` in BSim client with sequence-indexed fixture
  lookup. Mono uses left corpus. Mode A uses left and right corpus by stream.
  Mode B appends left then right for same sequence. Reconnect restarts at
  sequence zero.
- Preserve malformed scenario by deriving one-byte-short SDU from fixture path,
  not old synthetic bytes.
- Maintain running per-stream hash over sequence marker plus final transmitted
  SDU bytes. Emit it in client `PASS` output.
- Python parser independently computes expected hash from checked-in corpus,
  scenario send count, layout, and injection. It rejects duplication, omission,
  reorder, wrong channel, and corruption.
- Do not put platform PCM hashes back in scenario JSON.

### 3. Payload-and-recipe-aware receiver oracle

- Extend `CONFIG_BSIM_OBSERVER`-only pre-push metadata to carry exact accepted
  LC3 payload identity and recipe progress per channel. Receiver controller
  sequence remains diagnostic data, not source fixture identity. Mono and Mode
  B carry one source identity. Mode A carries per-half source identity and
  recipe state, including a synthetic lost-half action when required.
- Production signatures outside test macro and production behavior stay
  unchanged.
- Sink uses exact valid payload bytes to select fixture sequence and stateful
  recipe reference PCM. Recipe cursor advances for PLC and valid decode actions;
  only source-valid decoded outputs enter numerical metrics. Source-invalid
  startup halves and expected one-CIS PLC halves remain covered by exact
  validity and PLC counts.
- Malformed exact-shape rejection creates no decoder action and does not advance
  source or recipe cursor.

### 4. Shared integer comparator

- Add test-only module accepting actual `int16_t` channel samples,
  little-endian reference PCM, sample count, and immutable threshold set.
- Report compared sample and frame count, maximum absolute sample error, sum
  squared error and integer RMS, and scale-safe signed correlation score. Use
  no floating-point acceptance math.
- Give accumulators explicit overflow bounds and static or runtime guards.
- Pass requires exact dimensions, enough compared samples, maximum and RMS at
  or below limits, and correlation at or above floor.
- Mono duplication remains exact `L == R`. Stereo channel distinction remains
  required. Existing exact energy, source-validity, lifecycle, stats, guard,
  malformed-shape, and no-push-after-stop assertions remain.
- Diagnostic output includes observed values and configured limits, enough to
  diagnose hosted failure without raw PCM logs.

### 5. Scenario data and parser

- Bump scenario schema deliberately. Remove `known.full`, `known.l`, and
  `known.r` decoded PCM pins. Preserve exact `known.total` and all scenario
  count metadata.
- Add explicit fixture, duration, and layout references where needed instead of
  parsing scenario names.
- Parser requires numerical metric fields and threshold compliance, exact TX
  hashes, and all existing counts and fault scans.

### 6. Real decoder unit tests

- Replace only byte-exact decoded PCM and CRC acceptance with shared comparator
  against checked-in reference PCM.
- Retain exact LC3 fixture integrity, sizes, output sample counts, guards, mono
  duplication, Mode B channel placement, decoder behavior, stats, and error
  behavior.
- Exact integer helpers and transforms remain exact.

## Calibration phase and stop gate

Before thresholds become pass/fail constants, run same checked-in LC3 corpus
and comparator diagnostics on at least one identified Intel x86_64 environment,
one identified AMD x86_64 environment, and one identified ARM environment.
When raw CPU identity proves vendor for environment that reproduced old
mismatch, include it in matching Intel or AMD calibration evidence. Record raw
CPU identity, including vendor and model, OS, architecture, compiler and
toolchain, NCS/liblc3 revision, flags, fixture SHA-256, per-shape and
per-channel maximum, RMS, correlation, and repeatability.

Do not call random GitHub runner labels Intel or AMD without raw CPU evidence.

Select smallest documented integer thresholds above measured valid envelope with
explicit bounded headroom. Thresholds cannot proceed if any adversarial mutation
overlaps valid envelope.

Mandatory adversarial controls:

1. Exact channel swap.
2. Prior-frame reference shift.
3. Next-frame reference shift.
4. One-byte LC3 corruption that still decodes.
5. Dead or zero channel.
6. Sample corruption just above maximum-error limit.
7. Distributed error just above RMS limit.
8. Low-correlation waveform.

Every control must fail for named reason. If valid and adversarial distributions
cannot be separated, stop. Do not widen tolerance or weaken routing or order
assertions. Escalate with measurements and redesign fixture or comparator.

P0c adds payload-identity proof and PLC-aware stateful reference calibration.
P2 must not resume until identical schema-3, 38-record calibration reports pass
on identified Intel, AMD, and ARM environments. Valid payload bytes, not
receiver-controller sequence, select source fixture identity.

Reviewed P0c acceptance on 2026-09-17 records six schema-3 reports across
identified AMD Ryzen 9 5950X with GCC 14.3.0, Intel Core i3-6100U with Clang
21.1.8, and ARM environments. Each report has 38 records; repeats are equal
within each environment, and record identity/order is equal across platforms.
All eight stateful-valid records pass, all four stateful mutations return
`max-error`, and embedded ARM manifest, policy, and support hashes match host
provenance. Stateful-valid max-abs/max-RMS/min-correlation envelopes are AMD
`0/0/32767`, Intel `1977/425/32756`, and ARM `1/1/32767`; frozen limits remain
`2048/512/32750`. Production cpuapp and FLPR were restored after calibration.
Detailed evidence is in
`docs/development/portable-lc3-pcm-oracle-p0c-handoff.md`. P0c unblocked P2
before later P2 diagnostic evidence found the Mode A 7.5 ms recipe error below.

### P0d measured Mode A 7.5 ms correction

P2 exact payload observation measured 113 decoder actions per Mode A 7.5 ms
channel. Left has PLC actions 0 through 11, corpus frame 0 at action 12, then
corpus frames 1 through 100. Right has PLC actions 0 through 9, corpus frame 0
at action 10, PLC actions 11 and 12, then corpus frames 1 through 100. Both
channels therefore have 101 source-valid outputs and 12 PLC actions. The right
post-valid PLC history changes later decoder output, so it needs a generated
stateful PCM reference. P0d corrects recipes five and six while retaining the
schema-3 38-record protocol and frozen `2048/512/32750` policy.

P0d cross-platform acceptance completed on 2026-09-17 at reviewed commit
`4fbe9bc135d9077aff90a56f0f6f70fe92637ebb`. Two schema-3 reports per
identified AMD Ryzen 9 5950X with GCC 14.3.0, Intel Core i3-6100U with Clang
21.1.8, and nRF54L15 ARM with Zephyr SDK 0.17.0 GCC 12.2.0 each contained 38
records. Record identity and order matched across environments, and repeats
matched within each environment. Corrected Mode A 7.5 ms left/right
max-error/RMS/correlation Q15 metrics were AMD `0/0/32767` and `0/0/32767`,
Intel `1902/424/32761` and `1883/426/32760`, and ARM `1/1/32767` and
`1/1/32767`.

The eight-stateful-valid envelope was maximum absolute error 1977, maximum RMS
426, and minimum correlation Q15 32756, inside frozen `2048/512/32750`.
All eight stateful-valid records passed and all four mutations returned
`max-error`. Clean reviewed production source was restored after calibration:
cpuapp and FLPR image hashes and flash bytes verified, required boot markers
were present, and no UART warning or error lines occurred. Evidence remains
external, not checked into this repository, at
`/tmp/opencode/pb031-p0d-acceptance-4fbe9bc/acceptance-report.md`. P0d
unblocks P2, now in progress.

### P0e reconnect seven-PLC correction

P2 reconnect diagnostic evidence measured seven startup PLC actions followed
by corpus frames 0 through 99 for second segment left output. P0e appends
`start7_10ms_l` as the ninth recipe without changing existing recipe order,
stateful references, source corpus, mutation records, schema, or frozen
`2048/512/32750` policy. The new record is schema-3 record 34 with 107 actions,
100 valid frames, and 48000 samples.

P0e cross-platform acceptance completed on 2026-09-18 at reviewed commit
`b38cfecebe6842172f2885e9439799a538946b8d`. Two schema-3 reports each on AMD
Ryzen 9 5950X with GCC 14.3.0, Intel Core i3-6100U with Clang 21.1.8, and
nRF54L15 ARM with Zephyr SDK 0.17.0 GCC 12.2.0 contained 39 records. Record
identity and order matched across environments, and repeats matched within each
environment. All nine stateful-valid records passed and all four stateful
mutations returned `max-error`.

For `start7_10ms_l`, max-error/RMS/correlation Q15 values were AMD
`0/0/32767`, Intel `1862/389/32756`, and ARM `1/1/32767`. The nine-recipe
cross-platform envelope was maximum absolute error 1977, maximum RMS 426, and
minimum correlation Q15 32756, inside frozen `2048/512/32750` limits. Clean
reviewed production cpuapp and FLPR firmware was restored, flash verified, and
booted with required markers and no UART warning or error lines. Detailed
external evidence is at
`/tmp/opencode/pb031-p0e-acceptance-b38cfec/acceptance-report.md`.

P0e resumes P2. Reconnect second segment must map to `start7_10ms_l`; P2
remains responsible for applying that mapping and proving Stage 1 behavior.

## Phases and verification

### P0: Baseline and calibration scaffold

Files: fixture generator, manifest, corpus files, `tests/fixtures/lc3/README.md`,
shared comparator diagnostic, focused comparator unit suite, and any bounded
calibration artifact plumbing.

Work: establish reproducible fixture provenance and diagnostic-only comparison.
Do not replace gate acceptance in this phase.

Verify: fixture manifest hashes; repeat decode reports; retained cross-platform
artifacts with raw host and toolchain provenance; all valid and adversarial
envelopes separable before setting thresholds.

### P1: Fixed TX corpus and exact transport oracle

Files: `tests/bsim/client/CMakeLists.txt`, `tests/bsim/client/src/bsim_tx.c`,
`tests/bsim/client/src/bsim_tx.h`, fixture corpus and manifest,
`tests/bsim/stage1-scenarios.json`, `scripts/bsim_stage1_parse.py`, and
`tests/unit/bsim_runner/`.

Work: remove runtime encoder dependence from BSim traffic, add corpus-derived
malformed injection, and make exact TX hash parser-owned.

Verify: focused Python parser tests, then BSim subset covering mono, Mode A,
Mode B, malformed, and reconnect.

### P2: Payload-and-recipe-aware PCM oracle

Files: `tests/bsim/src/bsim_observer.c`, `tests/bsim/src/bsim_observer.h`,
`src/audio_stream_session.c` test hook, `tests/bsim/src/audio_sink_stub.c`,
`tests/bsim/src/bsim_sink_oracle.h`, `tests/bsim/src/bsim_test_main.c`, and
shared comparator plus focused tests.

Work: carry test-only exact payload identity and stateful recipe metadata to sink
and enforce portable PCM comparison without changing production signatures or
behavior.

Verify: named negative controls and BSim subset for every duration, mode, and
loss case.

Accepted 2026-09-18. P2 carries bounded exact payload snapshots through the
test-only observer, identifies source-valid LC3 payloads against immutable
stateful recipes, and evaluates only those outputs with the frozen portable
limits. Reconnect segment 2 uses `start7_10ms_l` on both channels, with seven
PLC actions followed by corpus frames 0 through 99. BSim binding capacity is
nine accepted recipes. Focused generator, calibration, parser, `pcm_oracle`,
and `audio_stream_session` gates passed. Full Stage 1 passed all 17 scenarios
and 26 runs with max error 257, max RMS 182, and minimum correlation Q15
32767. All receiver/client logs in
`/tmp/opencode/pb031-p2-bsim-stage1.C6FBBY` were inspected; only exact
scenario-17 warning `Invalid operation in state: releasing` appeared. P3 is
next.

### P3: Real-decoder fixture migration and documentation

Files: `tests/unit/decode/CMakeLists.txt`, `tests/unit/decode/src/test_decode.c`,
fixture documentation and manifest, `docs/testing/t2-audio-pipeline-tests.md`,
`docs/testing/behavior-contract.md`, and `docs/testing/coverage-matrix.md`.

Work: apply same portable comparison policy at real `audio_decode_sdu()` public
boundary while retaining exact integer-only contracts.

Verify: decode suite and fixture integrity checks.

### P4: Full acceptance

Run after P0 through P3 pass their focused gates:

```bash
nix develop -c ./scripts/test-all.sh
nix develop -c fw-build-5340
nix develop -c fw-build-54l15
nix develop -c python3 scripts/check-build-contract.py --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15
git diff --check
```

Require warning-free generation, tests, and both receiver builds. Update test
inventory or count baseline only if adding a new suite changes count. Do not
change a baseline solely to mask migration failure.

## Acceptance mapping

| PB-031 criterion | Smallest public-boundary proof |
| --- | --- |
| 1. Checked-in corpus and exact transport | BSim client `PASS` TX hash plus parser test independently deriving corpus bytes for each layout, send count, and malformed injection. |
| 2. Portable numerical PCM policy | Recorded identified Intel x86_64 and AMD x86_64 reports, plus an identified ARM environment corpus/comparator report with fixture hashes and raw provenance, followed by sink comparator acceptance at public `audio_sink_push()` boundary. |
| 3. Negative controls | Focused comparator and BSim sink tests inject each named mutation through public test boundaries and assert named failure. |
| 4. Stage 1 contracts | Full `scripts/bsim-stage1-run.sh` public result covering all 17 scenarios and 26 runs with exact lifecycle, routing, count, PLC, decode-error, and teardown checks. |
| 5. Real-decoder fixture policy | Decode suite drives real `audio_decode_sdu()` with checked-in LC3 and portable comparator while exact routing, dimensions, guards, stats, and fixture hashes remain asserted. |
| 6. Gate and build preservation | Canonical software gate, both receiver builds, and resolved build-contract command pass with no warnings and no production decoder or flag changes. |

## Rollback criteria

Rollback proposed implementation if any condition occurs:

- Exact transport hashes are removed or weakened.
- Runtime encoding remains in BSim traffic.
- Thresholds lack identified Intel x86_64, AMD x86_64, and ARM environment evidence.
- Valid and mutation envelopes overlap.
- BSim count or lifecycle contract changes.
- Production liblc3 flags or decoder code changes.
- Any warning appears.
- Either receiver target build regresses.

Old platform hash mismatch is expected to disappear because decoded PCM byte
hash is no longer accepted. Do not preserve old hashes as fallback.

## Open decisions

- Threshold numbers deliberately remain unset until P0 evidence exists.
- Exact ARM calibration environment may be CI or controlled local hardware, but it
  must expose raw identity and provenance and execute same corpus and comparator.
- Permanent CI architecture matrix is follow-up decision, not hidden scope.
