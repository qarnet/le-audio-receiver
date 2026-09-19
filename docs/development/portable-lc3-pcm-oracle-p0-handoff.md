# PB-031 P0 handoff: corpus and calibration scaffold

Status: Approved implementation handoff for P0a. P0 remains open until
cross-platform measurements establish thresholds.

Parent plan: `docs/development/portable-lc3-pcm-oracle-plan.md`.

## Goal

Create reproducible checked-in BSim LC3 corpus, shared integer PCM comparison
engine, focused comparator tests, and diagnostic host calibration command.
Capture local AMD evidence. Do not replace current BSim or decode pass/fail
oracles and do not choose production thresholds in this subphase.

## Scope

Modify or add only:

- `tests/fixtures/lc3/gen_fixtures.c`
- `tests/fixtures/lc3/generate.sh`
- `tests/fixtures/lc3/README.md`
- new BSim corpus binaries and manifest under `tests/fixtures/lc3/`
- `tests/support/pcm_oracle.c`
- `tests/support/pcm_oracle.h`
- new `tests/unit/pcm_oracle/` suite
- `tests/fixtures/lc3/calibrate.c`
- `scripts/lc3_pcm_calibrate.py`
- focused tests for calibration script and generator behavior if needed
- PB-031 Implementation Notes
- this handoff only for factual correction after implementation

Correct parent plan and PB-031 execution-owned plan from `ARM64` to an
identified ARM environment. PB-031 acceptance says ARM, and production decoder
runs on ARM Cortex-M. Prefer later calibration on attached nRF54L15 rather than
narrowing evidence to ARM64. Do not change acceptance-criteria text.

## Non-scope

- No BSim client, receiver oracle, observer, parser, scenario JSON, or decode
  test migration yet.
- No production source, liblc3 source, compiler flags, workflow, firmware,
  release, or hardware behavior changes.
- No pass/fail tolerance constants. Manifest thresholds remain absent or null.
- No repinning old PCM hashes.
- No commit, push, PR, or permanent CI architecture matrix.

## Corpus contract

Extend existing generator without changing existing eight fixture binaries.
Generate four logical streams, each with 128 continuous codec frames:

| Stem | Duration | LC3 bytes/frame | PCM samples/frame |
| --- | ---: | ---: | ---: |
| `bsim_48k_10ms_120b_l` | 10 ms | 120 | 480 |
| `bsim_48k_10ms_120b_r` | 10 ms | 120 | 480 |
| `bsim_48k_7p5ms_90b_l` | 7.5 ms | 90 | 360 |
| `bsim_48k_7p5ms_90b_r` | 7.5 ms | 90 | 360 |

Each stem has raw concatenated `.lc3` and mono little-endian `int16_t` `.pcm`.
Keep one encoder and one decoder alive across all 128 frames for each stream.
This preserves codec history. Generate source sample for frame sequence `seq`,
frame-local sample `i`, and channel `ch` with current BSim formula:

```c
v = seq ^ ((i + 1U) * 747796405U) ^ ((uint32_t)ch << 24);
v = bsim_tx_hash_mix(v);
sample = (int16_t)(v & 0xFFFFU);
```

Use defined unsigned arithmetic. Left is `ch=0`; right is `ch=1`.

Add `tests/fixtures/lc3/portable-oracle-manifest.json`. It records:

- `schema_version: 1`
- NCS `v3.3.0`
- liblc3 semantic label `1.1.2` and exact west revision
  `48bbd3eacd36e99a57317a0a4867002e0b09e183`
- exact generator flags
- source formula identifier and corpus frame count
- per stem: duration, frequency, channel, frame bytes, samples/frame, frame
  count, LC3 path/size/SHA-256, PCM path/size/SHA-256
- no accepted tolerance values

Generator remains manual and warning-free. `generate.sh` prints all hashes.
Before regeneration, preserve hashes for existing eight binaries; after run,
require those hashes unchanged. New corpus hashes become immutable fixture
integrity, not decoded-output acceptance.

## Shared integer comparator

Add test-only `tests/support/pcm_oracle.h/.c`. API must support incremental
multi-frame accumulation because BSim later compares 100 pushes.

Required public data:

```c
struct pcm_oracle_metrics {
    uint64_t squared_error;
    uint64_t actual_energy_scaled;
    uint64_t reference_energy_scaled;
    int64_t dot_product_scaled;
    uint32_t samples;
    uint32_t frames;
    uint32_t max_abs_error;
    uint32_t rms_error;
    int32_t correlation_q15;
};

struct pcm_oracle_limits {
    uint32_t min_samples;
    uint32_t max_abs_error;
    uint32_t max_rms_error;
    int32_t min_correlation_q15;
};
```

Expose init, accumulate, finalize, evaluate, and result-name operations.
`accumulate` accepts actual `int16_t` samples plus sample stride, little-endian
reference bytes plus byte stride, and sample count. It increments frame count
once per successful call.

Arithmetic rules:

- Decode reference bytes explicitly as little-endian signed 16-bit.
- Absolute difference uses 32-bit signed intermediates, covering 65535.
- Squared error uses 64-bit unsigned accumulation with checked overflow.
- Correlation downscales actual and reference samples by division by 256 before
  accumulating energies and dot product. C signed division truncation is
  defined. Do not right-shift negative values.
- Final integer RMS is conservative: ceiling square root of ceiling
  `squared_error / samples`.
- Correlation uses integer square roots of scaled actual and reference energy,
  then `dot * 32768 / (sqrt(actual_energy) * sqrt(reference_energy))`, with
  checked arithmetic and clamp to signed Q15 range. Zero energy fails
  correlation.
- No floating point.
- Invalid pointers, zero count, zero strides, sample/frame counter overflow, or
  accumulator overflow return negative errno without partial state mutation.
- Evaluation precedence: insufficient samples, max error, RMS error,
  correlation, pass. Named result supports diagnostic output.

## Comparator unit suite

Add discovered Twister suite `tests/unit/pcm_oracle`, native_sim only. Test
public behavior:

1. Exact little-endian positive/negative samples pass and produce zero error.
2. Small bounded differences pass and metrics match hand calculations.
3. One outlier fails maximum error by name.
4. Distributed differences fail RMS by name.
5. Negated or low-correlation waveform fails correlation by name.
6. Dead zero channel fails correlation.
7. Left data compared with distinct right reference fails (channel swap).
8. Frame N compared with N-1 and N+1 fails (order shift).
9. Invalid arguments and overflow fail without mutating accumulator.
10. Boundary values at limits pass; values immediately beyond limits fail.

Tests use small synthetic vectors with deliberate limits. They prove comparator
semantics, not production tolerance values.

## Diagnostic calibration command

Add `tests/fixtures/lc3/calibrate.c`. It loads all four corpus LC3/PCM pairs,
decodes all 128 frames with one continuous decoder per stream using real
liblc3, feeds shared comparator, and prints machine-readable metrics. It also
runs diagnostic adversarial comparisons for channel swap, prior-frame shift,
next-frame shift, dead channel, and low-correlation synthetic output. These are
measurements only in P0a.

Add public CLI `scripts/lc3_pcm_calibrate.py`:

```text
python3 scripts/lc3_pcm_calibrate.py --output ABSOLUTE_NEW_FILE.json
```

Behavior:

- stdlib only;
- rejects relative paths, existing output, repository-contained output, and
  malformed/unknown manifest fields;
- verifies every manifest size and SHA-256 before compiling;
- resolves NCS from `NCS` or exact default `$HOME/ncs/v3.3.0` and verifies
  liblc3 revision when source is a Git checkout;
- compiles `calibrate.c`, `tests/support/pcm_oracle.c`, and liblc3 sources with
  exact `-O3 -std=c11 -ffast-math` plus warning flags used by generator;
- uses temporary build output and removes it;
- captures UTC timestamp, raw `lscpu`, `uname`, compiler version, architecture,
  liblc3 revision, flags, fixture hashes, fixed calibration-input SHA-256/size
  records, repository HEAD, bounded raw `git status --porcelain=v1`, and metric
  records. Do not claim repository state is clean;
- writes one bounded JSON document atomically with mode 0644 and refuses
  overwrite;
- exits nonzero and creates no final file on any validation, compile, decode,
  parse, or write failure.

Add focused Python public-boundary tests if practical without compiling liblc3:
manifest rejection, relative/existing/repository output rejection, and atomic
no-output failure. Do not mock away file validation boundary.

Run local command twice to two new files under
`/tmp/opencode/pb031-calibration/`. Confirm metric equality and raw identity
contains `AuthenticAMD` and `AMD Ryzen 9 5950X 16-Core Processor`. Retain both
files as diagnostic evidence. Do not infer threshold from AMD-only data.

## Verification

Run:

```bash
bash tests/fixtures/lc3/generate.sh
git diff --exit-code -- \
  tests/fixtures/lc3/mono_48k_7p5ms_60b.lc3 \
  tests/fixtures/lc3/mono_48k_7p5ms_60b.pcm \
  tests/fixtures/lc3/mono_48k_10ms_60b.lc3 \
  tests/fixtures/lc3/mono_48k_10ms_60b.pcm \
  tests/fixtures/lc3/modeb_48k_7p5ms_60b.lc3 \
  tests/fixtures/lc3/modeb_48k_7p5ms_60b.pcm \
  tests/fixtures/lc3/modeb_48k_10ms_60b.lc3 \
  tests/fixtures/lc3/modeb_48k_10ms_60b.pcm \
  tests/fixtures/lc3/bsim_48k_10ms_120b_l.lc3 \
  tests/fixtures/lc3/bsim_48k_10ms_120b_l.pcm \
  tests/fixtures/lc3/bsim_48k_10ms_120b_r.lc3 \
  tests/fixtures/lc3/bsim_48k_10ms_120b_r.pcm \
  tests/fixtures/lc3/bsim_48k_7p5ms_90b_l.lc3 \
  tests/fixtures/lc3/bsim_48k_7p5ms_90b_l.pcm \
  tests/fixtures/lc3/bsim_48k_7p5ms_90b_r.lc3 \
  tests/fixtures/lc3/bsim_48k_7p5ms_90b_r.pcm
nix develop -c env NIX_HARDENING_ENABLE="" west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/opencode/pb031-pcm-oracle tests/unit/pcm_oracle -p -t run
nix develop -c python3 scripts/lc3_pcm_calibrate.py \
  --output /tmp/opencode/pb031-calibration/amd-provenance-run-1.json
nix develop -c python3 scripts/lc3_pcm_calibrate.py \
  --output /tmp/opencode/pb031-calibration/amd-provenance-run-2.json
nix develop -c ./scripts/test-all.sh --phase unit
nix develop -c backlog doctor
```

The fixture generator and all builds/tests must emit no warning. If liblc3
itself emits an existing compiler warning under direct host compilation, stop
and report exact diagnostic. Do not suppress it or weaken warning policy.

## Escalation

Stop without commit if two materially different fixes fail, fixture output is
not repeatable, existing fixture bytes change, comparator arithmetic cannot be
bounded, a requested API conflicts with target compiler support, or warning-free
generation cannot be retained. Preserve worktree and report evidence.

Return changed files, generated corpus sizes/hashes, comparator API, test and
calibration results, raw evidence paths, warnings, deviations, and git status.
