# PB-031 P0b handoff: freeze PCM limits and complete controls

Status: Approved implementation handoff. Intel, AMD, and ARM calibration
envelopes are available. This phase freezes candidate limits and proves all
mandatory controls before P2 may consume them.

Parent plan: `docs/development/portable-lc3-pcm-oracle-plan.md`.

## Goal

Make one strict manifest-owned numerical PCM policy:

```text
max_abs_error=2048
max_rms_error=512
min_correlation_q15=32750
```

Extend host and ARM calibration paths from 22 diagnostic metrics to 26
evaluated records. Every valid corpus record must pass. Every mandatory
mutation must fail with an exact named `pcm_oracle_evaluate()` result. Preserve
fixture bytes, production code, transport contracts, and current BSim/decode
acceptance. P2 owns first gate use of these limits.

## Grounded threshold decision

Identified repeat evidence gives this valid envelope:

| Environment | Maximum error | RMS error | Minimum correlation Q15 |
| --- | ---: | ---: | ---: |
| AMD Ryzen 9 5950X, GCC 14.3.0 | 0 | 0 | 32767 |
| Intel Core i3-6100U, Clang 21.1.8 | 1977 | 426 | 32757 |
| nRF54L15 ARM Cortex-M, NCS toolchain | 1 | 1 | 32767 |

Chosen headroom is explicit and bounded:

- maximum error 2048 is next power-of-two boundary above 1977, 71 samples or
  3.59 percent headroom;
- RMS error 512 is next power-of-two boundary above 426, 86 samples or 20.19
  percent headroom;
- correlation floor 32750 is seven Q15 counts below measured floor 32757.

Current diagnostics are far outside policy: closest current mutation has
maximum error 32768 and RMS 15283; highest mutation correlation is 301. A
temporary real-liblc3 probe also established exact one-byte mutation shape:
XOR bit 2 (`0x04`) of byte 0 in frame 0 of `bsim_48k_10ms_120b_l`. All 128
frames decode successfully. AMD/GCC reports maximum 65535, RMS 2339,
correlation 32389; Intel/Clang reports maximum 65535, RMS 2388, correlation
32372. Both evaluate `max-error` under candidate policy.

Evidence paths:

- AMD: `/tmp/opencode/pb031-calibration/amd-provenance-run-{1,2}.json`
- ARM: `/tmp/opencode/pb031-calibration/arm-stack-fix-console.log` and
  `/tmp/opencode/pb031-calibration/arm-repeat-console.log`
- Intel: `/tmp/opencode/pb031-calibration-intel-d4c321c-i3-6100u/`

## Scope

Modify only:

- `tests/fixtures/lc3/portable-oracle-manifest.json`
- `tests/fixtures/lc3/generate.sh`
- `tests/fixtures/lc3/calibrate.c`
- `tests/fixtures/lc3/README.md`
- `scripts/lc3_pcm_calibrate.py`
- `tests/unit/lc3_pcm_calibrate/test_lc3_pcm_calibrate.py`
- `tests/unit/pcm_oracle/src/test_pcm_oracle.c`
- `tests/calibration/lc3_pcm_oracle/CMakeLists.txt`
- `tests/calibration/lc3_pcm_oracle/src/pb031_arm_build_info.h.in`
- `tests/calibration/lc3_pcm_oracle/src/main.c`
- PB-031 Implementation Notes
- `docs/development/portable-lc3-pcm-oracle-p0-intel-handoff.md`
- this handoff only for factual corrections or reviewed result appendix

No new production or reusable runtime module is needed. Existing
`tests/support/pcm_oracle.c/.h` API is sufficient and must remain unchanged
unless a focused test exposes a real defect.

## Non-scope

- No production source, decoder, compiler flag, firmware configuration, or
  product behavior change.
- No BSim observer, sink, parser, scenario JSON, receiver metric, or old PCM hash
  migration. P2 owns those changes.
- No real-decoder unit migration. P3 owns it.
- No corpus regeneration, fixture-byte change, fixture hash repin, transport
  hash change, scenario count change, or warning suppression.
- No permanent CI architecture matrix.
- No threshold widening after a failed control. Stop and escalate instead.
- No push, PR, tag, amend, force operation, or hardware action in Executor
  session.

## Manifest schema 2

Bump `portable-oracle-manifest.json` to schema 2 and add one top-level object
after `corpus_frame_count`:

```json
"pcm_limits": {
  "max_abs_error": 2048,
  "max_rms_error": 512,
  "min_correlation_q15": 32750
}
```

This JSON object is sole policy source. Do not duplicate policy as manually
maintained constants in Python or C source.

Update both strict Python manifest validators in `generate.sh` and
`lc3_pcm_calibrate.py`:

- require schema 2 and exact top-level field set;
- require `pcm_limits` exact key set;
- reject booleans and non-integers;
- require `max_abs_error` in 0..65535, `max_rms_error` in 0..65535, and
  `min_correlation_q15` in -32768..32767;
- return the validated values to callers;
- preserve all current path, geometry, size, SHA-256, version, revision, and
  generator-flag checks.

The generator does not use limits to create bytes. Its job is fail-closed
schema validation. Default generation must still prove all checked-in corpus
bytes unchanged.

## Host calibration protocol

Keep public CLI unchanged:

```text
python3 scripts/lc3_pcm_calibrate.py --output ABSOLUTE_NEW_FILE.json
```

Change generated report to schema 2. Add exact `pcm_limits` object. Preserve all
current provenance. `manifest_sha256` naturally changes and remains captured.

Pass three validated policy numbers as decimal argv to temporary `calibrate`
binary after fixture directory. `calibrate.c` parses them strictly with full
range and trailing-character checks. Do not use compile-time copied constants.
Any missing, malformed, or out-of-range policy is fatal.

Every metric record adds final string field `evaluation` from
`pcm_oracle_result_name()`. Evaluation uses manifest policy plus
`min_samples=metrics.samples`, because record geometry is separately exact and
scenario-specific sample sufficiency belongs to P2.

Emit records in exact order:

1. Four `valid`, expected `pass`.
2. Two `channel-swap`, expected `max-error`.
3. Existing four records per stream in existing stream order:
   `prior-frame-shift`, `next-frame-shift`, `dead-channel`, and
   `low-correlation-synthetic`; all expected `max-error` under frozen policy.
4. One `lc3-byte-corruption` for 10 ms left, expected `max-error`.
5. One `max-error-boundary`, expected `max-error`.
6. One `rms-error-boundary`, expected `rms-error`.
7. One `correlation-boundary`, expected `correlation`.

Total: 26 records.

`scripts/lc3_pcm_calibrate.py` must require exact identity, geometry, order,
evaluation, and record count. It must reject a record whose metrics would
produce a different result when evaluated against policy. Keep report and raw
metric output bounds. No compiler diagnostic or calibrator stderr is accepted.

## Exact new controls

### One-byte LC3 corruption

Use 10 ms left corpus. Start a fresh decoder and decode all 128 frames in order.
For frame 0 only, copy its 120 bytes into a local bounded buffer and XOR
`buffer[0]` with `0x04`. Never mutate fixture memory. Every `lc3_decode()` call
must return zero; otherwise calibration fails instead of treating decode failure
as control success. Accumulate all 128 decoded frames against unchanged 10 ms
left reference. Require `max-error`.

### Maximum-error boundary

Use first 10 ms left reference frame. Build actual samples equal to reference,
then change exactly one sample inward by `max_abs_error + 1` so signed 16-bit
range cannot overflow. Require observed maximum error 2049, RMS at or below
512, and evaluation `max-error`.

### RMS-error boundary

Use first 10 ms left reference frame. Change every actual sample inward by
`max_rms_error + 1`. Every difference must be exactly 513. Require maximum
error 513, RMS 513, and evaluation `rms-error`.

### Correlation boundary

Use one synthetic 480-sample frame. Reference alternates +256, -256. Actual
uses exact negation. Require maximum error 512, RMS 512, correlation
`INT16_MIN`, and evaluation `correlation`. This proves correlation independently
after maximum and RMS checks pass.

Use explicit little-endian reference bytes and the existing public comparator
API for all three threshold-bound controls. No direct construction of
`pcm_oracle_metrics` is allowed.

## ARM calibration protocol

Parse schema-2 `pcm_limits` from manifest in CMake with `file(READ)` and
`string(JSON)`. Configure numeric macros into generated
`pb031_arm_build_info.h`; do not hand-copy values into `main.c`. CMake must fail
if fields are absent or nonnumeric. Python strict validation remains authority
for full ranges and exact field sets before build/run.

Bump UART BEGIN protocol to schema 2 and append all three policy values. Keep
SOURCE hashes. Emit same 26 records, order, metrics, and evaluations as host.
Set `METRIC_RECORD_COUNT=26`; PASS becomes `PB031_ARM_PASS metrics=26`.

Implement controls with static bounded work buffers. Do not increase main stack
through frame-sized local arrays. Keep `CONFIG_MAIN_STACK_SIZE=8192`, thread
analyzer placement, fault handling, and no-interleaving output. Every decode
and comparator error remains fatal with no PASS line.

## Focused tests

Extend calibration Python tests to prove public behavior:

- schema 1 rejected after migration;
- missing, unknown, boolean, non-integer, and out-of-range policy fields fail
  before compile and create no output;
- exact policy reaches compile/run argv and schema-2 report;
- wrong record count/order/identity/evaluation is rejected;
- a numerically inconsistent claimed evaluation is rejected;
- existing relative/existing/repository output, hash, atomic failure, timestamp,
  repository provenance, and calibration-input tests remain green.

Extend comparator suite with one production-policy test that uses public
accumulate/finalize/evaluate calls for exact controls above. Assert boundary
values pass, maximum 2049 fails `max-error`, distributed 513 fails `rms-error`,
and alternating negation fails `correlation`. Do not weaken existing focused
arithmetic and precedence tests.

## Executor verification

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
python3 -m unittest discover -s tests/unit/lc3_pcm_calibrate -p 'test_*.py' -v
nix develop -c env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 -d /tmp/opencode/pb031-p0b-pcm-oracle \
  tests/unit/pcm_oracle -p -t run
nix develop -c west build --no-sysbuild \
  -b xiao_nrf54l15/nrf54l15/cpuapp \
  -d /tmp/opencode/pb031-p0b-arm-calibration \
  tests/calibration/lc3_pcm_oracle -p
python3 -m unittest discover -s tests/unit/bsim_runner -p 'test_*.py' -v
nix develop -c ./scripts/test-all.sh --phase unit
nix develop -c backlog doctor
git diff --check
```

If known flake evaluation failure prevents a Nix command, record exact failure.
Do not edit `flake.nix` in this phase. Run equivalent already-established
focused command from current dev shell where available. No warning may be
ignored; repository-documented external diagnostics must be reported exactly.

Inspect ARM resolved `.config`: `CONFIG_LIBLC3=y`, `CONFIG_FPU=y`, stack remains
8192, and no Bluetooth subsystem. Report FLASH/RAM usage and stack-budget
impact. No target action occurs in Executor session.

## Commit and return

After focused checks pass, inspect status, diff, and recent log. Stage only
handoff-scoped files and commit:

```text
test: freeze portable PCM oracle limits
```

Do not include external evidence files. Do not push, amend, open PR, or add
attribution. After commit, run local AMD calibration twice into new external
paths under `/tmp/opencode/pb031-calibration/`; reports must record clean status,
26 equal records, four valid PASS results, and exact control evaluations.

PB-031 notes in this commit record reviewed schema-1 Intel evidence, candidate
policy rationale, and P0b implementation facts only. Do not claim post-commit
AMD, Intel, or ARM schema-2 evidence before orchestrator review records it.

Return files changed, schema/API behavior, tests and builds with exact results,
commit hash/message, AMD evidence paths and hashes, warning diagnostics,
deviations, and git status.

## Escalation

Stop without commit and preserve worktree if:

- any corpus byte/hash changes;
- byte-0 XOR `0x04` does not decode all 128 frames or does not fail
  `max-error`;
- valid output fails policy on local AMD;
- any control gives a result other than its exact expected name;
- schema 2 cannot remain one strict policy source;
- ARM buffers exceed static memory or stack headroom becomes unclear;
- two materially different fixes fail;
- a warning, production change, threshold widening, or scope expansion appears
  necessary.

Report exact blocker, attempts, logs, diff, and one precise question. Do not
weaken checks, repin bytes, widen limits, or invent another architecture.

## Post-implementation orchestrator work

After reviewing Executor commit and local AMD evidence, orchestrator will:

1. transfer exact clean commit plus liblc3 revision to `thomas-nuc`;
2. capture two schema-2 Intel reports with raw identity and repeat validation;
3. identify nRF54L15, flash reviewed ARM calibration image, capture two complete
   schema-2 26-record UART runs, and restore production firmware;
4. verify all three environments pass policy and all controls fail by exact
   name;
5. append reviewed evidence and only then open P2 handoff work.
