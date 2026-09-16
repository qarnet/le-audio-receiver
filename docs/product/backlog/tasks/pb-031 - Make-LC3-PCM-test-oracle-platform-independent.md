---
id: PB-031
title: Make LC3/PCM test oracle platform-independent
status: In Progress
assignee: []
created_date: '2026-09-13 03:09'
updated_date: '2026-09-13 05:26'
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
- [ ] #1 Checked-in LC3 corpus drives BSim traffic, and exact fixture integrity, transmitted byte order, stream/channel placement, sequence, malformed injection, and send counts are verified without runtime encoder dependence.
- [ ] #2 Decoded PCM checks use documented integer max-error, RMS-error, and correlation limits calibrated from identical LC3 bytes on distinct Intel, AMD, and ARM environments, with provenance and raw measurements retained.
- [ ] #3 Negative controls prove channel swap, prior/next-frame shift, corruption, dead channel, malformed shape, and out-of-tolerance PCM fail at public test boundaries.
- [ ] #4 All 17 BSim scenarios and 26 runs preserve exact lifecycle, routing, frame-count, PLC, decode-error, and teardown contracts without CPU-specific decoded-PCM hash pins.
- [ ] #5 Real-decoder fixture tests use same portable comparison policy while exact integer-only routing, dimensions, guards, stats, and LC3 fixture hashes remain exact.
- [ ] #6 Canonical software gate and both receiver firmware builds pass with no new warnings; production liblc3 flags and decoder code remain unchanged.
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
<!-- SECTION:NOTES:END -->

## Comments

<!-- COMMENTS:BEGIN -->
created: 2026-09-13 03:20
---
Refinement: detailed plan resolves implementation shape; threshold values are measured P0 output guarded by explicit stop criteria, not an unresolved product decision; size corrected to L because scope is cross-cutting.
---
<!-- COMMENTS:END -->
