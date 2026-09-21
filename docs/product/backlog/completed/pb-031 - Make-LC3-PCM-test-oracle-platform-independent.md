---
id: PB-031
title: Make LC3/PCM test oracle platform-independent
status: Done
assignee: []
created_date: '2026-09-13 03:09'
updated_date: '2026-09-20 19:23'
labels:
  - 'size:L'
  - 'area:testing'
dependencies: []
priority: p1
type: bug
ordinal: 31000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Canonical BSim oracle pins exact FNV hashes over liblc3 floating-point decoded PCM. Same clean source tree can produce different PCM bits on different CPU/compiler instruction paths under required `-O3 -ffast-math`, causing false CI failures despite correct transport, routing, lifecycle, frame counts, PLC counts, and audio energy.

### Desired outcome

Checked-in LC3 inputs and transport/order checks stay exact, while decoded PCM uses documented bounded numerical checks that accept valid NCS v3.3.0 liblc3 output across tested Intel, AMD, and ARM environments and reject channel swaps, frame shifts, corruption, dead channels, and excess decoder error.

### Scope / Non-goals

Scope: BSim Stage 1 TX fixtures/oracle/parser; real decoder fixture tests; fixture provenance; portable tolerance calibration; relevant testing docs.

Non-goals: changing production decoder or audio behavior, removing `-ffast-math`, making liblc3 bit-exact, changing BAP scenario/lifecycle/PLC/count contracts, repinning old host-specific PCM hashes, firmware/release/hardware behavior.

### Technical context

`tests/bsim/client/src/bsim_tx.c` runtime-encodes sequence-dependent PCM; `tests/bsim/src/audio_sink_stub.c` hashes decoded PCM; `tests/bsim/stage1-scenarios.json` pins those hashes; `scripts/bsim_stage1_parse.py` enforces them; `tests/unit/decode/src/test_decode.c` asserts byte-exact checked-in decoded PCM and CRC; `tests/fixtures/lc3/` generator uses `-O3 -ffast-math`; liblc3 v1.1.2 from NCS v3.3.0 is production decoder path. A failed hosted run observed hash-only divergence and an exact rerun passed, so this is an oracle portability defect, not a proven firmware defect.

### Open questions

Numerical thresholds remain technical calibration output, not product behavior. They must be selected from measured cross-platform envelope and adversarial separation before numerical PCM acceptance replaces the diagnostic scaffold; no product question remains.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Checked-in LC3 corpus drives BSim traffic, and exact fixture integrity, transmitted byte order, stream/channel placement, sequence, malformed injection, and send counts are verified without runtime encoder dependence.
- [x] #2 Decoded PCM checks use documented integer max-error, RMS-error, and correlation limits calibrated from identical LC3 bytes on distinct Intel, AMD, and ARM environments, with provenance and raw measurements retained.
- [x] #3 Negative controls prove channel swap, prior/next-frame shift, corruption, dead channel, malformed shape, and out-of-tolerance PCM fail at public test boundaries.
- [x] #4 All 17 BSim scenarios and 26 runs preserve exact lifecycle, routing, frame-count, PLC, decode-error, and teardown contracts without CPU-specific decoded-PCM hash pins.
- [x] #5 Real-decoder fixture tests use same portable comparison policy while exact integer-only routing, dimensions, guards, stats, and LC3 fixture hashes remain exact.
- [x] #6 Canonical software gate and both receiver firmware builds pass with no new warnings; production liblc3 flags and decoder code remain unchanged.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. **P0, baseline and calibration scaffold.** Check in LC3 corpus and manifest, shared test-only integer comparator, and focused comparator tests. Run diagnostic calibration with repeat decode reports and raw provenance on identified Intel x86_64 and AMD x86_64 hosts, plus an identified ARM environment. Exercise every required adversarial control; stop and escalate if any valid/adversarial envelope overlaps. Set immutable thresholds only after separation.
   - Focused verification: fixture-manifest SHA-256s, repeatability, raw host/toolchain evidence, and valid/adversarial reports.

2. **P1, fixed BSim TX and transport oracle.** Replace runtime BSim encoding with fixture-indexed traffic, corpus-derived malformed injection, and exact per-stream transmitted-byte/sequence hash independently recomputed by parser.
   - Focused verification: parser tests plus BSim subset for mono, Mode A, Mode B, malformed, and reconnect.

3. **P2, sequence-aware PCM oracle.** Add `CONFIG_BSIM_OBSERVER`-only source sequence metadata and portable receiver PCM comparator. Preserve exact dimensions, counts, lifecycle, PLC, routing, validity, decoder-error, guard, and teardown contracts; leave production signatures and behavior unchanged.
   - Focused verification: named negative controls and BSim subset across every duration, mode, and loss case.

4. **P3, real-decoder fixture migration and docs.** Move real `audio_decode_sdu()` fixture assertions to shared portable comparator; retain exact fixture integrity, integer routing, dimensions, guards, stats, and error behavior. Update fixture and testing docs.
   - Focused verification: decode suite and fixture-integrity checks.

5. **P4, full acceptance and evidence.** Run full canonical gate, both receiver builds, build-contract checker, and warning checks; record calibrated threshold provenance and validation evidence only after all pass.

```bash
nix develop -c ./scripts/test-all.sh
nix develop -c fw-build-5340
nix develop -c fw-build-54l15
nix develop -c python3 scripts/check-build-contract.py --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15
git diff --check
```
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
P0a scaffold and review repairs complete: checked-in 128-frame LC3/PCM corpus and manifest, shared integer comparator, native_sim comparator suite, fail-closed portable generator, and diagnostic calibration CLI. Comparator denominator overflow now returns `-EOVERFLOW` without mutating output metrics.

Provenance-complete AMD repeat evidence retained at `/tmp/opencode/pb031-calibration/amd-provenance-run-1.json` and `/tmp/opencode/pb031-calibration/amd-provenance-run-2.json`. Both files have mode `0644`, equal 22 metric records, `AuthenticAMD`, and `AMD Ryzen 9 5950X 16-Core Processor`. Each record includes UTC capture timestamp, repository HEAD, bounded raw `git status --porcelain=v1`, stable ordered SHA-256/size records for calibrator inputs, fixture/manifest hashes, compiler, liblc3, and CPU provenance. Older `amd-run-1.json` through `amd-run-5.json` are superseded for provenance-complete calibration evidence and remain retained externally.

Earlier focused standard invocation passed 11/11 without a recorded compiler warning: `nix develop -c env NIX_HARDENING_ENABLE="" west build --no-sysbuild -b native_sim/native/64 -d /tmp/opencode/pb031-review-pcm-repair tests/unit/pcm_oracle -p -t run`. Thresholds remain unset. Identified nRF54L15 ARM calibration result is recorded below.

P0 ARM calibration image added 2026-09-13 at `tests/calibration/lc3_pcm_oracle`. It embeds all four LC3/PCM corpus pairs, emits the bounded 22-record ARM protocol with configure-time manifest and source SHA-256 values, and leaves thresholds unset. First identified ARM execution used the original resolved 1024-byte main stack and failed before its first metric in liblc3 SNS: serial output reported `***** USAGE FAULT *****` and `Stack overflow (context area not valid)`; PC `0x00006460` resolves to `spectral_shaping` at `modules/lib/liblc3/src/sns.c:723`, and LR `0x00007209` resolves to `lc3_sns_synthesize` at line 822. No ARM metrics are accepted from that run. This is superseded diagnostic evidence, retained for stack-repair provenance. Raw identity evidence is retained at `/tmp/opencode/pb031-calibration/arm-identity-preflash.log` with DPIDR `0x6ba02477`, AP IDR map `0x84770001`, `0x84770001`, `0x32880000`, FICR PART `0x00054b15`, and VARIANT `0x41414330`. `/tmp/opencode/pb031-calibration/arm-flash.log` records 591188 bytes downloaded and verified through CMSIS-DAP serial `8EE9B3FF`. No persisted serial fault-capture file exists; the fault details above are observed serial-capture facts.

The repaired calibration image uses `CONFIG_MAIN_STACK_SIZE=8192`, `CONFIG_THREAD_ANALYZER=y`, `CONFIG_THREAD_ANALYZER_USE_PRINTK=y`, and `CONFIG_THREAD_NAME=y`. `thread_analyzer_print(0U)` runs after all 22 metrics and immediately before PASS. `CONFIG_THREAD_ANALYZER_AUTO` remains unset. Codec/comparator math, thresholds, stack protection, and assertion settings were not changed. Pristine repair build passed for `xiao_nrf54l15/nrf54l15/cpuapp` with `CONFIG_FPU=y`, `CONFIG_LIBLC3=y`, no Bluetooth subsystem, `CONFIG_HW_STACK_PROTECTION=y`, `CONFIG_ARM_STACK_PROTECTION=y`, and `CONFIG_BUILTIN_STACK_GUARD=y`. Linker summary: FLASH `592688 B` of `1428 KiB`, RAM `25768 B` of `188 KiB`. The board slot0 code-partition capacity is `664 KiB`, leaving `87248 B` for this image; RAM headroom is `166744 B`. `CONFIG_USE_DT_CODE_PARTITION=n` means this direct calibration build links against full RRAM, so slot0 is a checked board-capacity budget, not the active linker limit. During the implementation session, no target action occurred. Manifest verification produced `/tmp/opencode/pb031-calibration/executor-host-manifest-verify.json`; calibration Python tests previously passed 12/12. The required focused comparator rerun completed 11/11 with `NIX_HARDENING_ENABLE=""`, but the native-simulator runner emitted 12 external glibc `_FORTIFY_SOURCE requires compiling with optimization (-O)` warnings despite resolved `CONFIG_FORTIFY_SOURCE_NONE=y`. No `pcm_oracle` source diagnostic occurred. This host-runner warning is retained evidence, not an ARM calibration or threshold block.

Reviewed repaired execution used fresh identity evidence at `/tmp/opencode/pb031-calibration/arm-identity-stack-fix.log`: CMSIS-DAP serial `8EE9B3FF`, DPIDR `0x6ba02477`, AP IDRs `0x84770001`, `0x84770001`, `0x32880000`, `0x00000000`, PART `0x00054b15`, and VARIANT `0x41414330` (`AAC0`). `/tmp/opencode/pb031-calibration/arm-stack-fix-flash.log` records 592680 bytes downloaded and verified. Run 1 is retained at `/tmp/opencode/pb031-calibration/arm-stack-fix-console.log`, SHA-256 `4d71bbfa7e10ebfad62edfa6cee862c4b10789a1af61717cd29ef1df5711bdb8`: exact BEGIN and SOURCE records, 22 metrics, `PB031_ARM_PASS metrics=22`, and no fault, FAIL, or error. Its main-stack report is usage `2816 / 8192`, unused `5376`, `34%`.

Repeat identity and reset evidence are retained at `/tmp/opencode/pb031-calibration/arm-identity-repeat.log` and `/tmp/opencode/pb031-calibration/arm-repeat-reset.log`. Run 2 is retained at `/tmp/opencode/pb031-calibration/arm-repeat-console.log`, SHA-256 `fe6f2d7ff4889a929773b8e2a3e58784877bb913ac1a01b9603a72df67cde34c`: same 22 records and PASS, no fault, FAIL, or error, and main-stack usage `2816 / 8192`. Machine comparison reports `run1_metrics=22 run2_metrics=22 equal=True`. CPU-cycle telemetry differs between captures and is not a calibration metric.

Valid records report `max_abs_error=1`, `rms_error=1`, and `correlation_q15=32767`. Current P0a diagnostic separation is strong but remains diagnostic only: channel swap has max error `65535`, RMS at least `21686`, and correlation `-202` or `108`; prior and next frame shifts have max error `65535`, RMS at least `21604`, and correlation `79..300`; dead channel has max error `32768`, RMS at least `15283`, and correlation `0`; low-correlation synthetic has max error `65535`, RMS at least `36158`, and correlation `-9..-2`. No threshold is selected from these data.

Production receiver cpuapp and FLPR were rebuilt and restored. Raw evidence: `/tmp/opencode/pb031-calibration/production-restore-identity.log`, `/tmp/opencode/pb031-calibration/production-restore-build.log`, `/tmp/opencode/pb031-calibration/production-restore-flash.log`, and `/tmp/opencode/pb031-calibration/production-restore-console.log`, SHA-256 `e0e59ade36e08cfe24d38c4db050d5a7928a69d0274200f14c4a31609f750bf3`. Boot reached BLE ready, `settings_load() OK`, audio timing and I2S ready, FLPR READY with rings and runtime ready, then advertising. No boot warning or error occurred. Restore-build output contains only repository-known documented diagnostics: dirty-worktree notice, nRF54L15 watchdog no-sources CMake diagnostic, and Zephyr `__ASSERT()` globally enabled CMake diagnostic. Do not call this build warning-free.

Thresholds remain unset. P0 remains blocked only on identified Intel x86_64 calibration and later full mandatory threshold-bound controls. Current P0a diagnostic classes do not complete parent-plan adversarial coverage.

P1 exact one-CIS-loss placement repair completed 2026-09-15. The BSim TX slot
now exposes exact send-cap condition-variable notification; the right loss
stream starts at 48 valid sends, drains accepted completions, waits 17 left
pause-window completions, then resumes with cap 110. Evidence root:
`/tmp/opencode/pb031-p1-exact-gap-fix-20260915`. Parser tests passed
`94 PASS / 0 FAIL`; strict Stage 1 passed all 17 scenarios and 26 runs. Both
loss runs retained `pushes1=100`, `trans1=8`, `szero1=8`, `splc1=16`,
`plc1=34`, `total1=216`, `derr1=0`, `mal1=0`, receiver hashes
`0x30D6BAF0`/`0x32777D65`/`0x9859F1D8`, and TX hashes
`0x8980C79D`/`0xDD25CC21`. No `LOSS_*` output or non-allowlisted runtime
warning appeared. Unit phase passed `71 PASS / 0 FAIL / 71 TOTAL`; `backlog
doctor` and `git diff --check` passed.

P0b freezes manifest schema 2 policy at maximum absolute error `2048`, maximum
RMS error `512`, and minimum correlation Q15 `32750`. The manifest
`pcm_limits` object is the sole calibration-policy source. Reviewed schema-1
Intel evidence from the identified Intel Core i3-6100U with Clang 21.1.8 had
valid maximum error `1977`, RMS error `426`, and minimum correlation Q15
`32757`. The selected maximum is the next power-of-two boundary, 71 samples
above `1977`; RMS is the next power-of-two boundary, 86 samples above `426`;
the correlation floor is seven Q15 counts below `32757`.

P0b host and ARM calibration protocols now emit 26 ordered metric records with
an `evaluation` from `pcm_oracle_evaluate()`: four valid passes, existing
channel/order/dead/synthetic controls, byte-0 XOR `0x04` LC3 corruption over
all 128 10 ms left frames, and maximum, RMS, and correlation boundary controls.
The ARM build reads numeric limits from the manifest at CMake configure time
into generated build information and uses static bounded control buffers while
retaining the 8192-byte main stack.

P0b implementation/review at `c7f64aa76b0525e81ddf31fbdccf238e5d978a25`
was accepted as P0b stop gate on 2026-09-16. P2 still owns first BSim gate
consumption. No PB-031 acceptance criterion is complete.

Reviewed schema-2 evidence includes two 26-record reports each on AMD, Intel,
and ARM. Record identity/order matched across platforms, repeat metrics matched
within each environment, and all four valid records pass. Mandatory controls
evaluate exactly: LC3 byte corruption and maximum-error boundary return
`max-error`; RMS boundary returns `rms-error`; correlation boundary returns
`correlation`. Existing channel/order/dead/synthetic controls return
`max-error`. Valid envelopes are AMD `0/0/32767`, Intel `1977/426/32757`, and
ARM `1/1/32767` for maximum error/RMS error/minimum correlation Q15.

Cross-platform validation is retained at
`/tmp/opencode/pb031-calibration/p0b-c7f64aa-cross-platform-validation.txt`,
SHA-256 `fb399a964d45ebeca7fbd6441806c7b2fa82052b8f7f8b54cbe9a5665cf36ade`,
with final marker
`PB031_P0B_CROSS_PLATFORM_PASS environments=3 runs=6 records_per_run=26 identity_order_equal=true evaluations_expected=true`.
Full reviewed AMD, Intel, and ARM report hashes, ARM fresh-identity/build/flash
evidence, production restoration, and repair-review results are recorded in
`docs/development/portable-lc3-pcm-oracle-p0b-threshold-handoff.md`.

P0c adds schema-3 payload-identity and PLC-aware calibration before P2 resumes.
Receiver controller sequence is diagnostic only and cannot select source fixture
frame identity. Valid exact LC3 payload bytes select fixture sequence. Eight
stateful recipes retain Stage 1 startup, malformed-rejection, and one-CIS-loss
history; PLC and valid decode actions advance recipe state, while only
source-valid decoded output enters numerical metrics. A malformed exact-shape
rejection creates no decoder action. Host and ARM protocols extend from 26 to
38 ordered records: eight stateful valid records pass frozen policy and four
recipe mutations return `max-error`. P0c cross-platform calibration on
identified Intel, AMD, and ARM environments remains mandatory before P2 work.

P0c reviewed acceptance on 2026-09-17 covers implementation commit
`262805eb51731ff7b2511e7e522762e4061bb270` (`test: add PLC-aware PCM
calibration traces`). Six schema-3 reports, two each on AMD Ryzen 9 5950X with
GCC 14.3.0, Intel Core i3-6100U with Clang 21.1.8, and ARM, contain 38 records
each with per-environment repeat equality and cross-platform identity/order
equality. All eight stateful-valid records pass; all four stateful mutations
return `max-error`; stateful max-abs/max-RMS/min-correlation envelopes are AMD
`0/0/32767`, Intel `1977/425/32756`, and ARM `1/1/32767`; frozen limits remain
`2048/512/32750`. ARM identity is CMSIS-DAP serial `8EE9B3FF`, DPIDR
`0x6ba02477`, AP IDRs `0x84770001`, `0x84770001`, `0x32880000`, and
`0x00000000`, FICR PART `0x00054b15`, and VARIANT `0x41414330`. Both ARM runs
reported main stack 2920/8192 (35%), unused 5272; calibration fit full 1428 KiB
RRAM with 689800 bytes headroom. Its 92536-byte excess over production 664 KiB
slot0 is expected with `CONFIG_USE_DT_CODE_PARTITION=n`, and production
partitions remain unchanged. Clean `262805e` cpuapp and FLPR were rebuilt,
flashed, verified, and booted without warning or error. P2 is unblocked.
Detailed evidence is in
`docs/development/portable-lc3-pcm-oracle-p0c-handoff.md`. No acceptance
checkbox is completed by P0c alone.

P0e reconnect correction accepted on 2026-09-18 at implementation commit
`b38cfecebe6842172f2885e9439799a538946b8d` (`test: add reconnect PCM oracle
recipe`). It appends `start7_10ms_l`, seven PLC actions followed by corpus
frames 0 through 99, as the ninth stateful recipe and schema-3 record 34.
Formal AMD GCC 14.3.0, Intel Clang 21.1.8, and nRF54L15 ARM GCC 12.2.0 evidence
has two 39-record reports per environment. Repeats and cross-platform record
identity/order match; all nine stateful-valid records pass; four mutations
return `max-error`; envelope remains `1977/426/32756` inside frozen
`2048/512/32750`. Clean reviewed nRF54L15 production cpuapp and FLPR images
were flashed, verified, and booted with required markers and no UART warning or
error. External evidence:
`/tmp/opencode/pb031-p0e-acceptance-b38cfec/acceptance-report.md`. P2 resumes
with reconnect segment 2 required to map to `start7_10ms_l`; no acceptance
checkbox is completed by P0e alone.

P2 accepted on 2026-09-18. Test-only payload snapshots now carry exact valid
LC3 bytes to the BSim sink, where stateful recipe cursors validate payload
identity and compare only source-valid PCM through frozen portable limits.
Reconnect segment 2 binds `start7_10ms_l` on both channels: seven PLC actions,
100 valid frames, 107 actions, and 48000 compared samples per channel. The
BSim binding table now permits all nine accepted recipes. Strict generator
hashes stayed unchanged; calibration 38/38, parser 140 PASS / 0 FAIL, native
`pcm_oracle` 12/12, native `audio_stream_session` 48/48, `backlog doctor`, and
`git diff --check` passed. Full Stage 1 passed 17 scenarios and 26 runs with
max error 257, max RMS 182, and minimum correlation Q15 32767. All 52
receiver/client logs were inspected in
`/tmp/opencode/pb031-p2-bsim-stage1.C6FBBY`; only exact scenario-17 warning
`Invalid operation in state: releasing` appeared. P3 remains next; no
acceptance checkbox changed.

P3 accepted on 2026-09-18. `tests/unit/decode` now compares each real
`audio_decode_sdu()` output channel against its checked-in interleaved
little-endian PCM anchor through the shared integer comparator with immutable
manifest limits. Exact LC3 geometry, mono duplication, Mode B placement,
guards, state preservation, statistics, and error checks remain; decoded PCM
byte equality and CRC acceptance are removed. Focused results: `bash
tests/fixtures/lc3/generate.sh` reported legacy fixture hashes and portable
corpus manifest hashes unchanged; `env NIX_HARDENING_ENABLE="" west build
--no-sysbuild -b native_sim/native/64 -d /tmp/opencode/pb031-p3-decode
tests/unit/decode -p -t run` reported 43/43 passing; `backlog doctor` and
`git diff --check` passed. P4 owns full acceptance; no acceptance checkbox
changed.

P4 full acceptance accepted 2026-09-18 at tested commit `d8f2a8e4d5eb0d6a7af2310a2c29e21b08542f22` (`test: repair decoder matrix witness`). Immutable raw evidence is `/tmp/opencode/pb031-p4.XgKVNM`, with seven raw-log SHA-256 values in `SHA256SUMS`.

The stale matrix witness was repaired from `test_golden_mono_10ms` to `test_fixture_mono_10ms` for `audio_decode_sdu()` outcome `0`; focused matrix tests passed 42/42 and `check-test-matrix` reported 0 errors and 0 notes.

Full validation passed: canonical gate `74 PASS / 0 FAIL / 74 TOTAL` (41 Twister, 5 exec-only, 25 Python, coverage, matrix, BSim); coverage population 37 at 4969/5427 numeric lines, 2177/2984 numeric branches, and 380/380 numeric functions; BSim 17 scenarios/26 runs with maximum/RMS/minimum-correlation 257/182/32767 inside immutable 2048/512/32750 limits; pristine nRF5340 and nRF54L15 builds; resolved build contract 96/96; `backlog doctor`; and `git diff --check`.

nRF5340 emitted only documented STATUS warning-table classes, including `BT_CTLR_ADVANCED_FEATURES`; nRF54L15 emitted only documented watchdog no-sources and `__ASSERT()` CMake diagnostics. No compiler or Kconfig assigned-value warning appeared. No production behavior, production liblc3 revision or flags, decoder code, fixture bytes, manifest limits, coverage baseline, or firmware feature changed.
<!-- SECTION:NOTES:END -->

## Comments

<!-- COMMENTS:BEGIN -->
created: 2026-09-13 03:20
---
Refinement: detailed plan resolves implementation shape; threshold values are measured P0 output guarded by explicit stop criteria, not an unresolved product decision; size corrected to L because scope is cross-cutting.
---
<!-- COMMENTS:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Outcome: P0 through P4 technical acceptance passed.

Key decisions:
- Exact fixture-derived TX sequence hashes remain strict pass/fail checks.
- Decoded PCM uses payload/recipe-aware portable integer metrics, not byte-identical decoded PCM pins.
- Immutable limits remain maximum error 2048, RMS 512, and minimum correlation Q15 32750.

Validation:
- `nix develop -c ./scripts/test-all.sh`: `74 PASS / 0 FAIL / 74 TOTAL`; coverage population 37, 4969/5427 numeric lines, 2177/2984 numeric branches, 380/380 numeric functions; BSim 17 scenarios and 26 runs passed with 257/182/32767 maximum/RMS/minimum-correlation.
- `nix develop -c fw-build-5340`: passed.
- `nix develop -c fw-build-54l15`: passed.
- `nix develop -c python3 scripts/check-build-contract.py --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15`: 96/96 passed.
- `backlog doctor` and `git diff --check`: passed.

Evidence: `/tmp/opencode/pb031-p4.XgKVNM` with raw logs and `SHA256SUMS`; tested commit `d8f2a8e4d5eb0d6a7af2310a2c29e21b08542f22`.

No production behavior, production liblc3 revision or flags, decoder code, fixture bytes, manifest limits, coverage baseline, or firmware feature changed.

Lifecycle closure: PR #13, `PB-031: Make LC3/PCM test oracle platform-independent`, was human-merged into `main` at `b59e1d8f99b8f4e7435c7086bfe81700007b221d`. Required PR checks passed: `test-unit`, `test-heavy (coverage)`, `test-heavy (bsim)`, aggregate `tests`, and `firmware`; `release` was skipped on the pull request. Human merge satisfies acceptance. Trusted-main follow-on candidate creation belongs to PB-006.
<!-- SECTION:FINAL_SUMMARY:END -->
