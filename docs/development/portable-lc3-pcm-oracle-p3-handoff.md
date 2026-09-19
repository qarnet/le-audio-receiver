# PB-031 P3 real-decoder fixture migration handoff

## Goal

Move `tests/unit/decode` decoded-PCM acceptance from byte equality and CRC-32
to the shared PB-031 integer comparator at the real `audio_decode_sdu()` public
boundary. Preserve all exact transport, geometry, routing, guard, state,
statistics, and error contracts. Update current fixture and testing
documentation, then record focused P3 evidence.

Current base is clean commit
`2c0c3ba4209d1966388bc532d4eb9a58b416d351` (`test: use payload-aware
portable PCM oracle`).

## In scope

- `tests/unit/decode/CMakeLists.txt`
- `tests/unit/decode/prj.conf`
- `tests/unit/decode/src/test_decode.c`
- One new private configure-time limits template under
  `tests/unit/decode/src/`
- `tests/fixtures/lc3/README.md`
- `docs/testing/t2-audio-pipeline-tests.md`
- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- `docs/development/portable-lc3-pcm-oracle-plan.md`
- PB-031 Implementation Notes in
  `docs/product/backlog/tasks/pb-031 - Make-LC3-PCM-test-oracle-platform-independent.md`
- This handoff document

## Out of scope

- Any production source or header under `src/`
- Any LC3 or PCM fixture byte change
- Any change to `tests/fixtures/lc3/portable-oracle-manifest.json`, its schema,
  or frozen limits
- Any change to liblc3, compiler flags, `-O3`, or `-ffast-math`
- Any BSim, calibration, stateful-recipe, workflow, firmware, release, or
  hardware change
- Threshold widening, new output baselines, corpus regeneration, or test-count
  baseline changes
- Full P4 gate or firmware builds

## Grounded current behavior

- `tests/unit/decode/src/test_decode.c` embeds four one-frame LC3/PCM fixture
  pairs. `assert_golden()` currently requires byte equality, full PCM CRC-32,
  and per-channel CRC-32.
- Five other state-preservation paths use `zassert_mem_equal()` against decoded
  PCM, and the repeat test uses decoded-output CRC equality.
- `tests/support/pcm_oracle.c` accepts strided actual `int16_t` samples and
  strided little-endian reference bytes. It reports sample/frame counts,
  maximum absolute error, integer RMS error, and Q15 correlation.
- `tests/fixtures/lc3/portable-oracle-manifest.json` schema 2 is sole owner of
  limits: maximum absolute error `2048`, maximum RMS error `512`, minimum Q15
  correlation `32750`.
- Legacy PCM is interleaved little-endian stereo. For channel `ch`, compare
  `actual + ch` with actual stride 2 against `reference + 2 * ch` with
  reference-byte stride 4.
- `bash tests/fixtures/lc3/generate.sh` strictly validates portable manifest
  hashes and requires regenerated legacy LC3/PCM bytes to equal checked-in
  legacy fixture bytes. Existing per-fixture LC3 and PCM SHA-256 values remain
  recorded in `tests/fixtures/lc3/README.md`.
- Decode suite currently contains 43 tests. P3 changes acceptance policy, not
  suite population.

## Exact implementation decisions

### 1. Configure comparator and immutable limits

In `tests/unit/decode/CMakeLists.txt`:

1. Define paths for `tests/support/pcm_oracle.c`, `pcm_oracle.h`, and
   `tests/fixtures/lc3/portable-oracle-manifest.json`.
2. Read schema 2 and `pcm_limits` with the same fail-closed CMake checks used by
   `tests/unit/pcm_oracle/CMakeLists.txt`: object type, numeric type, and integer
   spelling for all three members. Do not copy literal threshold values into
   source or CMake.
3. Generate one suite-private header from a new `.h.in` template. Use
   decode-specific include guards and macro names.
4. Add `tests/support/pcm_oracle.c` to `target_sources()` and
   `tests/support/` to private include directories.
5. Add manifest, comparator source/header, and limits template to
   `CMAKE_CONFIGURE_DEPENDS`.
6. Keep all four existing LC3 and PCM fixtures embedded exactly as today.

After CRC acceptance is removed from this suite, remove unused
`CONFIG_CRC=y` from `tests/unit/decode/prj.conf`. Do not change any other
Kconfig setting.

### 2. Add one per-channel portable assertion helper

In `test_decode.c`:

1. Remove `<zephyr/sys/crc.h>`, `channel_crc()`, and all decoded-output CRC
   state and assertions.
2. Include `pcm_oracle.h` and generated decode-limits header.
3. Add a helper that compares one decoded channel against one interleaved
   little-endian reference channel:
   - initialize one `struct pcm_oracle`;
   - call `pcm_oracle_accumulate()` exactly once with `actual_stride = 2`,
     `reference_byte_stride = 4`, and `sample_count = samples_per_ch`;
   - finalize metrics;
   - assert exact `metrics.samples == samples_per_ch` and
     `metrics.frames == 1`;
   - evaluate with `min_samples = samples_per_ch` and manifest-derived frozen
     limits;
   - require `PCM_ORACLE_RESULT_PASS`;
   - on failure, print tag, channel, result name, observed max/RMS/correlation,
     and configured max/RMS/correlation limits.
4. Add a stereo helper that first asserts exact reference byte length
   `4 * samples_per_ch`, then invokes channel helper for left and right using:
   - left: `actual`, `reference`;
   - right: `actual + 1`, `reference + 2`.

Do not combine channels into one metric. Independent channel evaluation is
required so Mode B channel placement remains observable.

### 3. Replace every decoded-PCM exact acceptance

Replace all byte equality and CRC pass/fail checks over real liblc3 output with
the portable stereo helper. This includes:

- four fixture decode tests;
- reconfigure-after-reset;
- reset/reconfigure/repeat sequence;
- malformed-length rejection then valid decode;
- huge-length rejection then valid decode;
- invalid PLC-shape rejection then valid decode.

Rename helpers, comments, and test names away from `golden` where they describe
decoded-byte equality. Use `fixture` or `portable` wording. Keep test count 43.

For the former deterministic-repeat test, independently require each successful
decode to pass the checked-in reference through the portable comparator. Do not
retain exact output-to-output byte or CRC equality as an acceptance condition.

### 4. Preserve exact contracts

Keep these assertions unchanged or equivalently exact:

- LC3 length equals `chan_count * 60` for each fixture call;
- PCM reference length equals `4 * samples_per_ch`;
- output guards after `2 * samples_per_ch` remain untouched;
- mono output requires exact `L == R` for every sample;
- Mode B output requires at least one exact `L != R` sample;
- all pure integer routing helper expected samples;
- configured sample counts and independent decoder pointers;
- rejection leaves output guards, decoder state, and statistics unchanged;
- PLC counts and hard-failure accounting;
- real malformed-valid-length liblc3 behavior.

Do not modify production code to make tests pass.

### 5. Update current documentation truth

- `tests/fixtures/lc3/README.md`: describe all `.pcm` files as checked-in
  comparison anchors, not byte-exact decoded-output contracts. Keep all
  SHA-256 records as exact fixture-integrity evidence. Retain legacy CRC values
  only if clearly labeled historical diagnostics that tests no longer assert;
  otherwise remove that section. State manifest-owned numerical limits and
  per-channel stride comparison used by decode suite. Correct stale text saying
  P2 owns first gate use.
- `docs/testing/t2-audio-pipeline-tests.md`: preserve historical T2 defect
  record, but make current PB-031 policy explicit. Replace current claims that
  decode suite accepts exact decoded PCM/CRC. Update current decode count to 43
  and combined decode/volume/stats count to 65. Mark old BabbleSim PCM hashes
  and T2 byte-exact observations historical, not current acceptance.
- `docs/testing/behavior-contract.md`: add current codec-test verification
  contract stating exact checked-in LC3 integrity and geometry, portable
  per-channel PCM metrics under manifest limits, exact mono duplication and
  Mode B placement, guards, stats, and errors. Do not redefine production audio
  behavior.
- `docs/testing/coverage-matrix.md`: update `audio_decode.c` row. Direct proof
  uses real decoder plus per-channel max/RMS/correlation; BSim uses exact TX
  payload hashes plus payload/recipe-aware portable PCM metrics, not pinned
  decoded-PCM hashes.
- `docs/development/portable-lc3-pcm-oracle-plan.md`: after focused gates pass,
  set status to P0 through P3 accepted with P4 pending and append concise P3
  evidence below P3.
- PB-031 Implementation Notes: append P3 summary and exact focused results. Do
  not check acceptance criteria yet; P4 owns full acceptance.

Project public-doc em-dash rule does not cover these internal files, but retain
existing style and avoid unrelated cleanup.

## Verification

Run from repository root in current NCS v3.3.0 environment:

```bash
bash tests/fixtures/lc3/generate.sh
env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/opencode/pb031-p3-decode \
  tests/unit/decode -p -t run
backlog doctor
git diff --check
```

Expected observable proof:

- generator exits 0, emits no compiler warning, says legacy fixture hashes and
  portable manifest hashes unchanged;
- decode suite reports 43/43 passing;
- no test source contains decoded-output CRC acceptance or PCM
  `zassert_mem_equal()` acceptance;
- `backlog doctor` and `git diff --check` pass;
- `git status --short` contains only intentional P3 files before commit.

Do not use `nix develop -c` for focused P3 verification. Current repository
flake evaluation has an unrelated `x86_64-darwin` attribute failure, while
current shell already exposes `west` and NCS v3.3.0 `ZEPHYR_BASE`.

## Commit and return

Inspect `git status`, full diff, and recent log. Stage only P3 files and commit:

```text
test: migrate decoder fixtures to portable PCM oracle
```

Do not push, amend, open or merge a PR, force any Git operation, or add
AI/tool attribution.

Return files changed, behavior changed, exact commands and results, commit hash
and message, blockers, handoff deviations, and suggested next step.

Stop and escalate without committing incomplete work if two materially
different attempts fail, evidence conflicts with this design, a warning cannot
be explained, fixture bytes change, thresholds appear insufficient, or a fix
would require production behavior changes, threshold widening, architecture
invention, or scope expansion.
