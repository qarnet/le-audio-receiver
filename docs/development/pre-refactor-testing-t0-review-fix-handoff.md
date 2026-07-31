# Phase T0 review-fix handoff

## Goal

Correct factual errors found during orchestrator review and complete missing
exact-commit BabbleSim validation. Phase T0 remains open until every item below
is fixed and the full 21/21 gate passes on the exact final commit.

## Scope

Touch only T0 documentation and documentation comments. Do not change
production behavior or test execution.

## Required corrections

### `docs/testing/behavior-contract.md`

1. `CODEC-001` and `CODEC-002`: remove nonexistent
   `CODEC_OUTPUT_FRAME_SAMPLES`. State that `audio_decode_config()` calculates
   `samples_per_ch = frame_us * freq_hz / USEC_PER_SEC`, yielding 480 and 360.
2. `CODEC-006`: do not claim every bad length/header invokes PLC. Production
   selects PLC when ISO VALID is clear and passes null LC3 data. A malformed
   payload marked valid can produce a decoder error instead. Describe both
   outcomes accurately.
3. `I2S-001`: remove nonexistent `INPUT_FRAMES` symbol. Use runtime
   `input_frames`, set by `audio_sink_set_input_frames()`.
4. `I2S-005`: fix emergency repeat behavior. Slab allocation failure logs
   `I2S slab full`, increments `i2s_underruns`, and returns the allocation
   error. Repeat fallback runs after a successful normal write when
   `k_mem_slab_num_free_get(&i2s_slab) >= DRIFT_THRESHOLD`; it allocates a
   separate block and copies `saved_frame`.
5. `OFFLOAD-010`: do not permit output drop. FLPR checks output stall/capacity
   before consuming input; unavailable output leaves input consumer index
   unchanged for retry.
6. `BUILD-004`: replace nonexistent `&swctrl0` wording with exact fixed
   regulator nodes: `rfsw_ctl` P2.05 `GPIO_ACTIVE_LOW`, and `rfsw_pwr` P2.03
   `GPIO_ACTIVE_HIGH`, both `regulator-boot-on`.
7. Remove unsupported phrase in `BT-007` about a “joinable thread”; no such
   connection thread contract exists.
8. Where current code does not yet enforce a desired contract (for example
   unsupported codec shape rejection or one-shot lifecycle open edge), label
   it as required behavior with a known T2/T4/T5 proof/fix gap. Do not imply
   current tests already prove it.

### `docs/testing/coverage-matrix.md`

1. `audio_asrc.c`: remove claim that BSim exercises cpuapp ASRC. Current BSim
   does not compile `audio_asrc.c` or `audio_i2s.c`; ASRC has direct unit and
   hardware evidence only.
2. `audio_offload.c`, `flpr_audio_process.c`, and `flpr_ring.c`: exec-only is a
   canonical native_sim execution mode, not itself a coverage gap. State actual
   remaining gap or “no known functional gap; branch coverage unmeasured.”
3. `audio_perf.c`: verify its CMake compiles real `src/audio_perf.c`, then state
   this accurately.
4. `audio_timing_none.c`: BSim CMake compiles this production source; mark its
   integration evidence accurately while noting direct branch proof absence.
5. `flpr_runtime.c`: responsibility is synchronous FLPR VPR runtime restart
   manager, not IPC submit/watchdog/fault detection.
6. `flpr_runtime` weak-test wording: production nRF54 path is preprocessor-
   excluded and non-nRF54 stubs execute; mock register operations in test file
   are independent of real `flpr_runtime_restart()`.
7. `flpr_cache.c`: assign explicit closing phase. T1 must cover or classify
   hardware-only cache/barrier behavior; no row may have an unresolved gap and
   blank closing phase.
8. `src/flpr/main.c`: assign T1/T4 or hardware-only acceptance explicitly;
   blank phase is not enough for an untested production entry point.
9. Re-check all 26 rows against actual CMake source lists. Keep test evidence
   factual and avoid treating suite runner shape as behavior proof.

### `docs/testing/v0.0.1-baseline.md`

1. User confirmed normal connection only. Remove “and streaming.”
2. Pre-merge PR #3 validation included full 21/21 gate on workstation plus
   `fw-build-5340` and `fw-build-54l15`. Remove `fw-build-dongle` from that
   subsection.
3. Add separate T0 validation subsection after final validation. Dongle build
   may be recorded there when rerun for T0.
4. Phrase “432 tests” as historical Phase 6 count, not complete test count at
   tag after BlueZ/WirePlumber suites landed.

### `STATUS.md`

After all checks pass, change T0 state from “in progress” to “ACCEPTED” and
record:

- exact final T0 commit full gate 21/21 on provisioned workstation;
- all three builds pass;
- T1 is next;
- no production behavior changed.

Do not mark accepted before validation.

### Recap accuracy

Count contracts mechanically before reporting. Current categories sum to 57,
not 56. Report exact count after corrections.

## Exact-commit workstation validation

Desktop lacks BabbleSim binaries. Validate final phase commit without pushing:

1. Commit review fixes as a new commit; do not amend `f8f5101`.
2. Create a git bundle containing branch `test/pre-refactor-behavior`.
3. Copy bundle to `thomas-workstation:/tmp/`.
4. In workstation repo `/home/thomas-workstation/repos/le-audio-receiver`,
   fetch bundle into a temporary validation ref.
5. Add a detached temporary worktree from that exact ref under `/tmp`.
6. Run `./scripts/test-all.sh` in correct development shell from that
   worktree. Require `21 PASS / 0 FAIL / 21 TOTAL`.
7. Return workstation repo to clean `main`; remove temporary worktree,
   temporary ref, and bundle artifacts.
8. Verify final tested commit hash equals desktop branch HEAD.

If full gate fails for any reason, do not mark T0 accepted. Diagnose and report.

## Other verification

Run on final commit:

```bash
bash -n scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check HEAD~2..HEAD
```

No flashing or hardware interaction.

## Commit

Inspect status/diff/log and stage only review-fix file plus corrected T0 docs.
Create a new commit:

```text
docs: correct pre-refactor baseline evidence
```

Do not amend, push, merge, open a PR, or add attribution. Return exact final
commit hash, full gate counts, build results, workstation cleanup state, files
changed, and any deviation.
